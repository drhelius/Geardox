/*
 * Geardox - MS-DOS PC/AT Emulator
 * Copyright (C) 2026  Ignacio Sanchez

 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/
 *
 */

#include "sound_blaster.h"
#include "midi_out.h"
#include "../system/dma.h"
#include "../system/machine_profiles.h"
#include "../system/pic.h"
#include "../common/state_serializer.h"
#include "../common/trace_logger.h"

// About 100 microseconds until 0AAh, Creative's "typically"
static const u64 k_sound_blaster_reset_clocks = GDOX_MACHINE_CLOCK_RATE / 10000;
static const u8 k_sound_blaster_ready = 0xAA;
static const u8 k_sound_blaster_asp_version = 0x18;
static const u32 k_sound_blaster_default_block = 0x8000;
static const u32 k_sound_blaster_min_rate = 5000;
static const u32 k_sound_blaster_max_rate = 45000;
static const u32 k_sound_blaster_ahead_rate = 1000;
static const float k_sound_blaster_pro_cutoff = 3200.0f;
static const float k_sound_blaster_cutoff_ratio = 0.45f;
static const char k_sound_blaster_copyright[] = "COPYRIGHT (C) CREATIVE TECHNOLOGY LTD, 1992.";

// Q15, 7 at 0 dB in steps of 3.67 dB and 0 at -46 dB: the one scale that fits every number Creative prints
static const s32 k_sound_blaster_ct1345_gain[8] = { 164, 2603, 3970, 6055, 9235, 14086, 21484, 32768 };

// Q15 of 2 dB steps, 31 at 0 dB
static const s32 k_sound_blaster_ct1745_gain[32] =
{
    26, 33, 41, 52, 65, 82, 104, 130, 164, 207, 260, 328, 413, 519, 654, 823,
    1036, 1305, 1642, 2068, 2603, 3277, 4125, 5193, 6538, 8231, 10362, 13045, 16423, 20675, 26029, 32768
};

static const s32 k_sound_blaster_output_gain[4] = { 32768, 65381, 130452, 260285 };

// 4 bits per code, a sign and three of size, on four steps. 3 bits and 2 bits the same on five and six
static const s8 k_sound_blaster_adpcm4[4][16] =
{
    { 0, 1, 2, 3, 4, 5, 6, 7, 0, -1, -2, -3, -4, -5, -6, -7 },
    { 1, 3, 5, 7, 9, 11, 13, 15, -1, -3, -5, -7, -9, -11, -13, -15 },
    { 2, 6, 10, 14, 18, 22, 26, 30, -2, -6, -10, -14, -18, -22, -26, -30 },
    { 4, 12, 20, 28, 36, 44, 52, 60, -4, -12, -20, -28, -36, -44, -52, -60 }
};

static const s8 k_sound_blaster_adpcm3[5][8] =
{
    { 0, 1, 2, 3, 0, -1, -2, -3 },
    { 1, 3, 5, 7, -1, -3, -5, -7 },
    { 2, 6, 10, 14, -2, -6, -10, -14 },
    { 4, 12, 20, 28, -4, -12, -20, -28 },
    { 5, 15, 25, 35, -5, -15, -25, -35 }
};

static const s8 k_sound_blaster_adpcm2[6][4] =
{
    { 0, 1, 0, -1 },
    { 1, 3, -1, -3 },
    { 2, 6, -2, -6 },
    { 4, 12, -4, -12 },
    { 8, 24, -8, -24 },
    { 6, 48, -16, -48 }
};

static const int k_sound_blaster_e2_table[4][9] =
{
    { 0x01, -0x02, -0x04, 0x08, -0x10, 0x20, 0x40, -0x80, -106 },
    { -0x01, 0x02, -0x04, 0x08, 0x10, -0x20, 0x40, -0x80, 165 },
    { -0x01, 0x02, 0x04, -0x08, 0x10, -0x20, -0x40, 0x80, -151 },
    { 0x01, -0x02, 0x04, -0x08, -0x10, 0x20, -0x40, 0x80, 90 }
};

SoundBlaster::SoundBlaster()
{
    InitPointer(m_pic);
    InitPointer(m_dma);
    InitPointer(m_midi_out);
    m_trace_logger = TraceLogger::GetDisabled();
    InitPointer(m_trace_clocks);
    m_trace_underruns = 0;
    m_version_major = 0;
    m_version_minor = 0;
    m_mixer_type = k_sound_blaster_mixer_none;
    m_asserted_line = -1;
    memset(&m_state, 0, sizeof(m_state));
    memset(m_gain, 0, sizeof(m_gain));
    m_state.rate_numerator = k_sound_blaster_default_rate;
    m_state.rate_denominator = 1;
}

SoundBlaster::~SoundBlaster()
{
}

void SoundBlaster::Init(PIC* pic, DMA* dma, MidiOut* midi_out)
{
    m_pic = pic;
    m_dma = dma;
    m_midi_out = midi_out;
}

void SoundBlaster::SetTraceLogger(TraceLogger* trace_logger)
{
    m_trace_logger = TraceLogger::Resolve(trace_logger);
}

void SoundBlaster::SetTraceClocks(const u64* clocks)
{
    m_trace_clocks = clocks;
}

void SoundBlaster::SetCard(int card, int irq, int dma)
{
    const GDOX_Sound_Card_Info& info = k_sound_cards[card];

    m_version_major = info.dsp_major;
    m_version_minor = info.dsp_minor;
    m_mixer_type = info.mixer;
    m_state.irq_line = irq;
    m_state.dma_8 = dma;
    m_state.dma_16 = 5;
}

// Power-on initializes the DSP as a reset does, so 0AAh waits at the read port (SBG p24). A reset empties the DSP's
// output, not the latch the port reads, which keeps a byte nobody took: Flashback reads it without waiting
void SoundBlaster::Reset(u64 clocks)
{
    UNUSED(clocks);
    m_state.mixer_index = 0;
    m_state.resetting = false;
    m_state.reset_pending = false;
    m_state.irq_mpu = false;
    m_state.last_output = 0;
    ResetDSP();
    AddOutput(k_sound_blaster_ready);
    ResetMixer();
    UpdateIRQ();
}

void SoundBlaster::ResetDSP()
{
    StopTransfer();

    if (m_state.e2_pending && IsValidPointer(m_dma))
        m_dma->SetRequest(m_state.dma_8, false);

    m_state.midi_uart = false;
    m_state.operands_needed = 0;
    m_state.operand_count = 0;

    if (m_state.output_count != 0)
        m_state.last_output = m_state.output[m_state.output_head];

    m_state.output_head = 0;
    m_state.output_count = 0;
    m_state.write_status_reads = 0;
    m_state.test_register = 0;
    memset(m_state.asp_registers, 0, sizeof(m_state.asp_registers));
    m_state.asp_registers[0x05] = 0x01;
    m_state.asp_registers[0x09] = 0xF8;
    m_state.e2_value = 0xAA;
    m_state.e2_count = 0;
    m_state.e2_pending = false;
    m_state.speaker = false;
    m_state.irq_8 = false;
    m_state.irq_16 = false;
    m_state.rate_numerator = k_sound_blaster_default_rate;
    m_state.rate_denominator = 1;
    m_state.block_size = k_sound_blaster_default_block;
    m_state.left_sample = 0;
    m_state.right_sample = 0;
    m_state.previous_left_sample = 0;
    m_state.previous_right_sample = 0;
    m_state.adpcm_reference = 0x80;
    m_state.adpcm_step = 0;
    m_state.adpcm_codes = 0;
    m_state.adpcm_seed = false;
}

// A frame is due. Nothing moves if the DMA hasn't brought it, and the DSP waits as long as the controller does
void SoundBlaster::Tick()
{
    SoundBlaster_Transfer& transfer = m_state.transfer;

    if (transfer.format == SOUND_BLASTER_FORMAT_SILENCE)
    {
        m_state.previous_left_sample = 0;
        m_state.previous_right_sample = 0;
        m_state.left_sample = 0;
        m_state.right_sample = 0;
        ConsumeUnits(1);
        return;
    }

    if ((transfer.format >= SOUND_BLASTER_FORMAT_ADPCM2) && (m_state.adpcm_codes != 0))
    {
        PlayADPCM();
        return;
    }

    if (m_state.transfers_needed != 0)
    {
        m_state.previous_left_sample = m_state.left_sample;
        m_state.previous_right_sample = m_state.right_sample;
        m_trace_underruns++;
        return;
    }

    if (transfer.format >= SOUND_BLASTER_FORMAT_ADPCM2)
    {
        m_state.adpcm_byte = m_state.latch[0];
        m_state.latch_count = 0;

        if (m_state.adpcm_seed)
        {
            m_state.adpcm_seed = false;
            m_state.adpcm_reference = m_state.adpcm_byte;
            m_state.adpcm_step = 0;
            ConsumeUnits(1);
            return;
        }

        m_state.adpcm_codes = (u8)(transfer.format == SOUND_BLASTER_FORMAT_ADPCM2 ? 4 :
            transfer.format == SOUND_BLASTER_FORMAT_ADPCM3 ? 3 : 2);
        PlayADPCM();
        return;
    }

    if (!transfer.input)
        PlayFrame();

    u32 units = GetFrameUnits();
    m_state.latch_count = 0;
    ConsumeUnits(units);
}

void SoundBlaster::PlayFrame()
{
    const u8* latch = m_state.latch;
    SoundBlaster_Transfer& transfer = m_state.transfer;
    bool stereo = GetFrameUnits() > 1;
    s16 left = 0;
    s16 right = 0;

    m_state.previous_left_sample = m_state.left_sample;
    m_state.previous_right_sample = m_state.right_sample;

    if (transfer.format == SOUND_BLASTER_FORMAT_PCM16)
    {
        u16 flip = transfer.is_signed ? 0x0000 : 0x8000;
        left = (s16)(((latch[1] << 8) | latch[0]) ^ flip);
        right = stereo ? (s16)(((latch[3] << 8) | latch[2]) ^ flip) : left;
    }
    else
    {
        u8 flip = transfer.is_signed ? 0x00 : 0x80;
        left = (s16)((u16)(latch[0] ^ flip) << 8);
        right = stereo ? (s16)((u16)(latch[1] ^ flip) << 8) : left;
    }

    m_state.left_sample = left;
    m_state.right_sample = right;
}

void SoundBlaster::PlayADPCM()
{
    SoundBlaster_Transfer& transfer = m_state.transfer;
    u8 byte = m_state.adpcm_byte;
    u8 code = 0;

    m_state.adpcm_codes--;

    switch (transfer.format)
    {
        case SOUND_BLASTER_FORMAT_ADPCM2:
            code = DecodeADPCM((byte >> (m_state.adpcm_codes * 2)) & 0x03, 2);
            break;
        case SOUND_BLASTER_FORMAT_ADPCM3:
            code = DecodeADPCM(m_state.adpcm_codes == 2 ? byte >> 5 : m_state.adpcm_codes == 1 ? (byte >> 2) & 0x07 :
                (byte & 0x03) << 1, 3);
            break;
        default:
            code = DecodeADPCM(m_state.adpcm_codes == 1 ? byte >> 4 : byte & 0x0F, 4);
            break;
    }

    m_state.previous_left_sample = m_state.left_sample;
    m_state.previous_right_sample = m_state.right_sample;
    m_state.left_sample = (s16)((u16)(code ^ 0x80) << 8);
    m_state.right_sample = m_state.left_sample;

    if (m_state.adpcm_codes == 0)
        ConsumeUnits(1);
}

// One code moves the reference by a size that depends on the step, and big codes raise the step while zero lowers it
u8 SoundBlaster::DecodeADPCM(u8 code, int bits)
{
    int top = bits == 4 ? 3 : bits == 3 ? 4 : 5;
    int raise = bits == 4 ? 5 : bits == 3 ? 3 : 1;
    int step = MIN((int)m_state.adpcm_step, top);
    int size = code & BIT_MASK(bits - 1);
    int delta = bits == 4 ? k_sound_blaster_adpcm4[step][code] : bits == 3 ? k_sound_blaster_adpcm3[step][code] :
        k_sound_blaster_adpcm2[step][code];

    if ((size >= raise) && (step < top))
        step++;
    else if ((size == 0) && (step > 0))
        step--;

    m_state.adpcm_step = (u8)step;
    m_state.adpcm_reference = (u8)CLAMP(m_state.adpcm_reference + delta, 0, 255);
    return m_state.adpcm_reference;
}

void SoundBlaster::ConsumeUnits(u32 units)
{
    SoundBlaster_Transfer& transfer = m_state.transfer;

    if (units >= transfer.remaining)
    {
        transfer.remaining = 0;
        EndBlock();
    }
    else
        transfer.remaining -= units;

    if (transfer.active && (transfer.format != SOUND_BLASTER_FORMAT_SILENCE))
        RequestFrame();
}

// A single-cycle command sent during auto-init takes over here, and an exit command ends the transfer
void SoundBlaster::EndBlock()
{
    SoundBlaster_Transfer& transfer = m_state.transfer;
    bool word = transfer.format == SOUND_BLASTER_FORMAT_PCM16;

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_PCM, TRACE_PCM_IRQ)))
    {
        GDOX_Trace_Entry* entry = TraceEvent(TRACE_PCM_IRQ, word ? 16 : 8);

        if (IsValidPointer(m_trace_clocks))
            entry->cycle = *m_trace_clocks;

        entry->flags |= (word ? m_state.irq_16 : m_state.irq_8) ? TRACE_PCM_FLAG_NOT_ACKED : 0;
        entry->pcm.underruns = m_trace_underruns;
    }

    if (word)
        m_state.irq_16 = true;
    else
        m_state.irq_8 = true;

    UpdateIRQ();

    if (transfer.auto_init && transfer.next_pending)
    {
        transfer.auto_init = false;
        transfer.next_pending = false;
        transfer.length = transfer.next_length;
        transfer.remaining = transfer.length;
        return;
    }

    if (transfer.auto_init && !transfer.exit_pending)
    {
        transfer.remaining = transfer.length;
        return;
    }

    StopTransfer();
}

void SoundBlaster::StopTransfer()
{
    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_PCM, TRACE_PCM_END)) && m_state.transfer.active)
        TraceEnd(m_state.transfer.remaining == 0 ? 0 : 1);

    SetRequest(false);
    DropAhead();
    m_state.transfer.active = false;
    m_state.transfer.paused = false;
    m_state.transfer.auto_init = false;
    m_state.transfer.exit_pending = false;
    m_state.transfer.next_pending = false;
    m_state.transfer.high_speed = false;
    m_state.transfers_needed = 0;
    m_state.latch_count = 0;
    m_state.phase = 0;
    m_state.left_sample = 0;
    m_state.right_sample = 0;
    m_state.previous_left_sample = 0;
    m_state.previous_right_sample = 0;
}

// The next frame comes in behind the one playing. The request stays up until the controller has moved all of it
void SoundBlaster::RequestFrame()
{
    const SoundBlaster_Transfer& transfer = m_state.transfer;

    if (transfer.format >= SOUND_BLASTER_FORMAT_ADPCM2)
    {
        if ((m_state.adpcm_codes != 0) || (m_state.transfers_needed != 0))
            return;

        m_state.transfers_needed = 1;
    }
    else
    {
        bool bytes = (transfer.format == SOUND_BLASTER_FORMAT_PCM16) && !IsWordChannel();
        u32 units = GetFrameUnits() * (bytes ? 2 : 1);

        if (TakeAhead(units))
            return;

        if (!transfer.input)
        {
            ReadAhead();

            if (TakeAhead(units))
                return;
        }

        m_state.transfers_needed = (u8)units;
    }

    m_state.latch_count = 0;
    SetRequest(true);
}

// A millisecond of frames from the requested one on comes from memory at once, within the block, so their requests
// are the controller counting them as they play
void SoundBlaster::ReadAhead()
{
    const SoundBlaster_Transfer& transfer = m_state.transfer;
    bool bytes = (transfer.format == SOUND_BLASTER_FORMAT_PCM16) && !IsWordChannel();
    u32 frame = (IsStereo() ? 2 : 1) * (bytes ? 2 : 1);
    u64 rate = (u64)m_state.rate_numerator * GDOX_MACHINE_CLOCK_RATE;
    u32 frames = (u32)(rate / (GetPeriod() * k_sound_blaster_ahead_rate));
    u32 wanted = MIN(frames * frame, transfer.remaining * (bytes ? 2 : 1));

    wanted = MIN(wanted, ((SOUND_BLASTER_AHEAD_SIZE / GetUnitBytes()) / frame) * frame);
    DropAhead();

    if (wanted == 0)
        return;

    u32 frame_clocks = (u32)(GetPeriod() / m_state.rate_numerator);
    u32 read = m_dma->ReadAhead(GetChannel(), 0, wanted, frame, frame_clocks, m_state.ahead);

    if (read < wanted)
        read -= read % frame;

    m_state.ahead_size = (u16)(read * GetUnitBytes());
}

// A frame read ahead comes due, and the controller counts its units as their transfers would have
bool SoundBlaster::TakeAhead(u32 units)
{
    u32 size = units * GetUnitBytes();

    if ((u32)(m_state.ahead_size - m_state.ahead_read) < size)
    {
        DropAhead();
        return false;
    }

    memcpy(m_state.latch, m_state.ahead + m_state.ahead_read, size);
    m_state.ahead_read = (u16)(m_state.ahead_read + size);
    m_state.latch_count = (u8)size;
    m_state.transfers_needed = 0;
    m_dma->Advance(GetChannel(), units);
    return true;
}

void SoundBlaster::DropAhead()
{
    m_state.ahead_size = 0;
    m_state.ahead_read = 0;
}

u32 SoundBlaster::GetFramesAhead() const
{
    u32 frame = (IsStereo() ? 2 : 1) * (m_state.transfer.format == SOUND_BLASTER_FORMAT_PCM16 ? 2 : 1);
    return ((u32)(m_state.ahead_size - m_state.ahead_read) + frame - 1) / frame;
}

u32 SoundBlaster::GetUnitBytes() const
{
    return GetChannel() >= 4 ? 2 : 1;
}

void SoundBlaster::SetRequest(bool active)
{
    if ((m_state.requesting == active) || !IsValidPointer(m_dma))
        return;

    m_state.requesting = active;
    m_dma->SetRequest(GetChannel(), active);
}

// Without a 16-bit channel, 16-bit data travels in bytes through the 8-bit one
int SoundBlaster::GetChannel() const
{
    bool word = (m_state.transfer.format == SOUND_BLASTER_FORMAT_PCM16) && (m_state.dma_16 >= 0);
    return word ? m_state.dma_16 : m_state.dma_8;
}

bool SoundBlaster::IsWordChannel() const
{
    return m_state.dma_16 >= 0;
}

u32 SoundBlaster::GetFrameUnits() const
{
    u32 units = IsStereo() ? 2 : 1;
    return MIN(units, m_state.transfer.remaining);
}

bool SoundBlaster::DMAWriteCallback(void* device, u16 value, bool word)
{
    SoundBlaster* blaster = (SoundBlaster*)device;
    SoundBlaster_State& state = blaster->m_state;

    // A software request on the channel still runs its cycle, with nothing on the card listening
    if (state.transfers_needed == 0)
        return true;

    state.latch[state.latch_count % SOUND_BLASTER_LATCH_SIZE] = (u8)value;
    state.latch_count++;

    if (word)
    {
        state.latch[state.latch_count % SOUND_BLASTER_LATCH_SIZE] = (u8)(value >> 8);
        state.latch_count++;
    }

    state.transfers_needed--;

    if (state.transfers_needed == 0)
        blaster->SetRequest(false);

    return true;
}

// What the channel moves next is no longer what was read ahead
void SoundBlaster::DMAChangeCallback(void* device)
{
    ((SoundBlaster*)device)->DropAhead();
}

// Nothing to record from, so the input is silence at its rate
bool SoundBlaster::DMAReadCallback(void* device, u16& value, bool word)
{
    SoundBlaster* blaster = (SoundBlaster*)device;
    SoundBlaster_State& state = blaster->m_state;

    if (state.e2_pending)
    {
        state.e2_pending = false;
        value = state.e2_value;

        if (!state.requesting || (blaster->GetChannel() != state.dma_8))
            blaster->m_dma->SetRequest(state.dma_8, false);

        return true;
    }

    if (state.transfers_needed == 0)
        return true;

    const SoundBlaster_Transfer& transfer = state.transfer;

    if (transfer.format == SOUND_BLASTER_FORMAT_PCM16)
    {
        u16 silence = transfer.is_signed ? 0x0000 : 0x8000;
        value = word ? silence : (state.transfers_needed & 0x01) != 0 ? (u16)(silence >> 8) : (u16)(silence & 0xFF);
    }
    else
        value = transfer.is_signed ? 0x00 : 0x80;

    state.transfers_needed--;

    if (state.transfers_needed == 0)
        blaster->SetRequest(false);

    return true;
}

void SoundBlaster::UpdateReset(u64 clocks)
{
    if (!m_state.reset_pending || (clocks < m_state.ready_clocks))
        return;

    m_state.reset_pending = false;
    AddOutput(k_sound_blaster_ready);
}

void SoundBlaster::AddOutput(u8 value)
{
    if (m_state.output_count >= SOUND_BLASTER_OUTPUT_SIZE)
        return;

    m_state.output[(m_state.output_head + m_state.output_count) % SOUND_BLASTER_OUTPUT_SIZE] = value;
    m_state.output_count++;
}

// With nothing to read the port repeats its last byte
u8 SoundBlaster::ReadData()
{
    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_PCM, TRACE_PCM_READ)))
    {
        GDOX_Trace_Entry* entry = TraceEvent(TRACE_PCM_READ, m_state.output_count != 0 ?
            m_state.output[m_state.output_head] : m_state.last_output);
        entry->flags |= m_state.output_count == 0 ? TRACE_PCM_FLAG_EMPTY : 0;
        entry->pcm.queue = (u8)m_state.output_count;
    }

    if (m_state.output_count != 0)
    {
        m_state.last_output = m_state.output[m_state.output_head];
        m_state.output_head = (m_state.output_head + 1) % SOUND_BLASTER_OUTPUT_SIZE;
        m_state.output_count--;
    }

    return m_state.last_output;
}

u8 SoundBlaster::Read(u16 port, u64 clocks)
{
    UpdateReset(clocks);

    switch (port & 0x0F)
    {
        case 0x05:
            if (unlikely(m_trace_logger->IsEventEnabled(TRACE_MIXER, TRACE_MIXER_READ)))
                TraceMixer(TRACE_MIXER_READ, ReadMixer());
            break;
        case 0x0A:
            return ReadData();
        case 0x0E:
            if (m_state.irq_8)
            {
                m_state.irq_8 = false;
                UpdateIRQ();

                if (unlikely(m_trace_logger->IsEventEnabled(TRACE_PCM, TRACE_PCM_ACK)))
                    TraceEvent(TRACE_PCM_ACK, 8);
            }
            break;
        case 0x0F:
            if (m_state.irq_16)
            {
                m_state.irq_16 = false;
                UpdateIRQ();

                if (unlikely(m_trace_logger->IsEventEnabled(TRACE_PCM, TRACE_PCM_ACK)))
                    TraceEvent(TRACE_PCM_ACK, 16);
            }
            break;
        case 0x0C:
            if (!m_state.resetting && !m_state.reset_pending && !m_state.transfer.high_speed)
                m_state.write_status_reads++;
            break;
        default:
            break;
    }

    return Peek(port, clocks);
}

// Bit 7 is the only one that means anything, the rest read high. High-speed mode takes no commands
// The write buffer reads busy 8 reads in 16, as drivers wait for busy and then ready before a command
u8 SoundBlaster::Peek(u16 port, u64 clocks) const
{
    bool ready = m_state.reset_pending && (clocks >= m_state.ready_clocks);

    switch (port & 0x0F)
    {
        case 0x05:
            return ReadMixer();
        case 0x0A:
            return m_state.output_count != 0 ? m_state.output[m_state.output_head] :
                ready ? k_sound_blaster_ready : m_state.last_output;
        case 0x0C:
            return (m_state.resetting || (m_state.reset_pending && !ready) || m_state.transfer.high_speed ||
                ((m_state.write_status_reads & 0x08) != 0)) ? 0xFF : 0x7F;
        case 0x0E:
            return (m_state.output_count != 0) || ready ? 0xFF : 0x7F;
        default:
            return 0xFF;
    }
}

void SoundBlaster::Write(u16 port, u8 value, u64 clocks)
{
    UpdateReset(clocks);

    switch (port & 0x0F)
    {
        case 0x04:
            m_state.mixer_index = value;
            break;
        case 0x05:
            WriteMixer(value);
            break;
        case 0x06:
        {
            bool high = (value & 0x01) != 0;

            if (high && !m_state.resetting)
            {
                bool keep = m_state.transfer.high_speed || m_state.midi_uart;
                u32 numerator = m_state.rate_numerator;
                u32 denominator = m_state.rate_denominator;
                u32 block = m_state.block_size;
                bool speaker = m_state.speaker;

                ResetDSP();

                if (keep)
                {
                    m_state.rate_numerator = numerator;
                    m_state.rate_denominator = denominator;
                    m_state.block_size = block;
                    m_state.speaker = speaker;
                }

                m_state.reset_pending = false;
                m_state.resetting = true;
                UpdateIRQ();

                if (unlikely(m_trace_logger->IsEventEnabled(TRACE_PCM, TRACE_PCM_RESET)))
                    TraceEvent(TRACE_PCM_RESET, keep ? 2 : 1);
            }
            else if (!high && m_state.resetting)
            {
                m_state.resetting = false;
                m_state.reset_pending = true;
                m_state.ready_clocks = clocks + k_sound_blaster_reset_clocks;

                if (unlikely(m_trace_logger->IsEventEnabled(TRACE_PCM, TRACE_PCM_RESET)))
                    TraceEvent(TRACE_PCM_RESET, 0);

                if (unlikely(m_trace_logger->IsEventEnabled(TRACE_PCM, TRACE_PCM_CONFIG)))
                    TraceEvent(TRACE_PCM_CONFIG, 0);
            }
            break;
        }
        case 0x0C:
            WriteDSP(value);
            break;
        default:
            break;
    }
}

void SoundBlaster::WriteDSP(u8 value)
{
    if (m_state.midi_uart)
    {
        WriteMIDI(value);
        return;
    }

    if (m_state.resetting || m_state.transfer.high_speed)
    {
        if (unlikely(m_trace_logger->IsEventEnabled(TRACE_PCM, TRACE_PCM_REJECT)))
            TraceEvent(TRACE_PCM_REJECT, value)->pcm.reason = m_state.resetting ? 0 : 1;

        return;
    }

    if (m_state.operands_needed != 0)
    {
        m_state.operands[m_state.operand_count] = value;
        m_state.operand_count++;

        if (m_state.operand_count == m_state.operands_needed)
        {
            m_state.operands_needed = 0;
            Execute();
        }

        return;
    }

    int operands = GetOperands(value);

    if (operands < 0)
    {
        if (unlikely(m_trace_logger->IsEventEnabled(TRACE_PCM, TRACE_PCM_REJECT)))
            TraceEvent(TRACE_PCM_REJECT, value)->pcm.reason = 2;

        return;
    }

    m_state.command = value;
    m_state.operand_count = 0;
    m_state.operands_needed = (u8)operands;

    if (operands == 0)
        Execute();
}

// Operand bytes of a command, or -1 if this DSP doesn't have it. Accepting 90h and its family on 4.xx goes beyond
// Creative's table: a program that follows the table never sends them, and one written for a Pro gets its transfer
int SoundBlaster::GetOperands(u8 command) const
{
    int version = (m_version_major << 8) | m_version_minor;
    bool v200 = version >= 0x0200;
    bool v201 = version >= 0x0201;
    bool v3 = m_version_major == 3;
    bool v4 = m_version_major >= 4;

    switch (command)
    {
        case 0x10:
        case 0x38:
        case 0x40:
        case 0xE0:
        case 0xE2:
        case 0xE4:
            return 1;
        case 0x14:
        case 0x16:
        case 0x17:
        case 0x24:
        case 0x74:
        case 0x75:
        case 0x76:
        case 0x77:
        case 0x80:
            return 2;
        case 0x20:
        case 0x30:
        case 0x31:
        case 0xD0:
        case 0xD1:
        case 0xD3:
        case 0xD4:
        case 0xE1:
        case 0xE3:
        case 0xE8:
        case 0xF2:
            return 0;
        case 0x1C:
        case 0x1F:
        case 0x2C:
        case 0x34:
        case 0x35:
        case 0x36:
        case 0x37:
        case 0x7D:
        case 0x7F:
        case 0xD8:
        case 0xDA:
            return v200 ? 0 : -1;
        case 0x48:
            return v200 ? 2 : -1;
        case 0x90:
        case 0x91:
        case 0x98:
        case 0x99:
            return v201 ? 0 : -1;
        case 0xA0:
        case 0xA8:
            return v3 ? 0 : -1;
        case 0x41:
        case 0x42:
            return v4 ? 2 : -1;
        case 0xD5:
        case 0xD6:
        case 0xD9:
        case 0xF3:
            return v4 ? 0 : -1;
        case 0x04:
        case 0x08:
        case 0x0F:
        case 0xF9:
            return v4 ? 1 : -1;
        case 0x05:
        case 0x0E:
        case 0xFA:
            return v4 ? 2 : -1;
        case 0xF8:
            return v4 ? -1 : 0;
        default:
            return (v4 && (command >= 0xB0) && (command <= 0xCF)) ? 3 : -1;
    }
}

void SoundBlaster::Execute()
{
    const u8* operands = m_state.operands;
    u32 length = (u32)(operands[0] | (operands[1] << 8)) + 1;

    if (unlikely(m_trace_logger->IsEnabled(TRACE_PCM)))
        TraceCommand();

    switch (m_state.command)
    {
        case 0x10:
            SetDirect(operands[0]);
            break;
        case 0x20:
            AddOutput(0x80);
            break;
        case 0x38:
            WriteMIDI(operands[0]);
            break;
        case 0x34:
        case 0x35:
        case 0x36:
        case 0x37:
            m_state.midi_uart = true;
            break;
        case 0x40:
            SetTimeConstant(operands[0]);
            break;
        case 0x41:
        case 0x42:
            SetRate((u32)((operands[0] << 8) | operands[1]));
            break;
        case 0x48:
            m_state.block_size = length;
            break;
        case 0x80:
            StartTransfer(SOUND_BLASTER_FORMAT_SILENCE, false, false, length, false, false, false, false);
            break;
        case 0xD0:
            SetPaused(true, false);
            break;
        case 0xD4:
            SetPaused(false, false);
            break;
        case 0xD5:
            SetPaused(true, true);
            break;
        case 0xD6:
            SetPaused(false, true);
            break;
        case 0xD1:
            SetSpeaker(true);
            break;
        case 0xD3:
            SetSpeaker(false);
            break;
        case 0xD8:
            AddOutput(m_state.speaker ? 0xFF : 0x00);
            break;
        case 0xD9:
        case 0xDA:
            m_state.transfer.exit_pending = m_state.transfer.active && m_state.transfer.auto_init;
            break;
        case 0xE0:
            AddOutput((u8)~operands[0]);
            break;
        case 0xE1:
            AddOutput(m_version_major);
            AddOutput(m_version_minor);
            break;
        case 0xE2:
        {
            int value = m_state.e2_value;

            for (int i = 0; i < 8; i++)
            {
                if (((operands[0] >> i) & 0x01) != 0)
                    value += k_sound_blaster_e2_table[m_state.e2_count & 0x03][i];
            }

            value += k_sound_blaster_e2_table[m_state.e2_count & 0x03][8];
            m_state.e2_value = (u8)value;
            m_state.e2_count++;

            // A channel that can't take it leaves the byte at the read port, which an NT driver relies on
            if (m_dma->GetController(m_state.dma_8 >> 2)->IsMasked(m_state.dma_8 & 0x03))
                AddOutput(m_state.e2_value);
            else
            {
                m_state.e2_pending = true;
                m_dma->SetRequest(m_state.dma_8, true);
            }
            break;
        }
        case 0xE3:
            for (size_t i = 0; i < sizeof(k_sound_blaster_copyright); i++)
                AddOutput((u8)k_sound_blaster_copyright[i]);
            break;
        case 0xE4:
            m_state.test_register = operands[0];
            break;
        case 0xE8:
            AddOutput(m_state.test_register);
            break;
        case 0xF2:
            m_state.irq_8 = true;
            UpdateIRQ();
            break;
        case 0xF3:
            m_state.irq_16 = true;
            UpdateIRQ();
            break;
        case 0xF8:
            AddOutput(0x00);
            break;
        case 0x08:
            AddOutput(k_sound_blaster_asp_version);
            break;
        case 0x0E:
            m_state.asp_registers[operands[0]] = operands[1];
            break;
        case 0x0F:
            AddOutput(m_state.asp_registers[operands[0]]);
            break;
        case 0xF9:
            AddOutput(operands[0] == 0x0E ? 0xFF : operands[0] == 0x0F ? 0x07 : operands[0] == 0x37 ? 0x38 : 0x00);
            break;
        case 0x04:
        case 0x05:
        case 0xFA:
        case 0x30:
        case 0x31:
        case 0xA0:
        case 0xA8:
            break;
        default:
            ExecuteTransfer(m_state.command);
            break;
    }
}

void SoundBlaster::ExecuteTransfer(u8 command)
{
    const u8* operands = m_state.operands;
    u32 length = (u32)(operands[0] | (operands[1] << 8)) + 1;
    u32 block = m_state.block_size;
    u8 pcm = SOUND_BLASTER_FORMAT_PCM8;

    switch (command)
    {
        case 0x14:
            StartTransfer(pcm, false, false, length, false, false, false, false);
            return;
        case 0x24:
            StartTransfer(pcm, false, true, length, false, false, false, false);
            return;
        case 0x1C:
            StartTransfer(pcm, true, false, block, false, false, false, false);
            return;
        case 0x2C:
            StartTransfer(pcm, true, true, block, false, false, false, false);
            return;
        case 0x90:
        case 0x91:
        case 0x98:
        case 0x99:
        {
            bool high_speed = m_version_major < 4;
            StartTransfer(pcm, (command & 0x01) == 0, (command & 0x08) != 0, block, high_speed, false, false, false);
            return;
        }
        case 0x16:
        case 0x17:
            StartTransfer(SOUND_BLASTER_FORMAT_ADPCM2, false, false, length, false, false, false, command == 0x17);
            return;
        case 0x74:
        case 0x75:
            StartTransfer(SOUND_BLASTER_FORMAT_ADPCM4, false, false, length, false, false, false, command == 0x75);
            return;
        case 0x76:
        case 0x77:
            StartTransfer(SOUND_BLASTER_FORMAT_ADPCM3, false, false, length, false, false, false, command == 0x77);
            return;
        case 0x1F:
            StartTransfer(SOUND_BLASTER_FORMAT_ADPCM2, true, false, block, false, false, false, true);
            return;
        case 0x7D:
            StartTransfer(SOUND_BLASTER_FORMAT_ADPCM4, true, false, block, false, false, false, true);
            return;
        case 0x7F:
            StartTransfer(SOUND_BLASTER_FORMAT_ADPCM3, true, false, block, false, false, false, true);
            return;
        default:
            break;
    }

    if ((command < 0xB0) || (command > 0xCF))
        return;

    u8 mode = operands[0];
    u32 samples = (u32)(operands[1] | (operands[2] << 8)) + 1;
    bool word = command < 0xC0;

    StartTransfer(word ? SOUND_BLASTER_FORMAT_PCM16 : SOUND_BLASTER_FORMAT_PCM8, (command & 0x04) != 0,
        (command & 0x08) != 0, samples, false, (mode & 0x20) != 0, (mode & 0x10) != 0, false);
}

void SoundBlaster::StartTransfer(u8 format, bool auto_init, bool input, u32 length, bool high_speed, bool stereo,
    bool is_signed, bool seed)
{
    SoundBlaster_Transfer& transfer = m_state.transfer;

    if (transfer.active && transfer.auto_init && !auto_init && (transfer.format == format) && !high_speed &&
        (transfer.input == input) && (transfer.stereo == stereo) && (transfer.is_signed == is_signed))
    {
        transfer.next_pending = true;
        transfer.next_length = length;

        if (unlikely(m_trace_logger->IsEventEnabled(TRACE_PCM, TRACE_PCM_TRANSFER)))
            TraceTransfer(true);

        return;
    }

    StopTransfer();

    transfer.format = format;
    transfer.auto_init = auto_init;
    transfer.input = input;
    transfer.length = length;
    transfer.remaining = length;
    transfer.high_speed = high_speed;
    transfer.stereo = stereo;
    transfer.is_signed = is_signed;
    transfer.active = true;
    m_state.adpcm_seed = seed;
    m_state.adpcm_codes = 0;

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_PCM, TRACE_PCM_TRANSFER)))
        TraceTransfer(false);

    if (format != SOUND_BLASTER_FORMAT_SILENCE)
    {
        RequestFrame();

        u64 delay = (u64)k_dma_unit_clocks * m_state.rate_numerator;
        u64 period = GetPeriod();
        m_state.phase = delay < period ? period - delay : 0;
    }
}

// The DSP counts microseconds: rate = 1,000,000 / (256 - TC)
void SoundBlaster::SetTimeConstant(u8 value)
{
    m_state.rate_numerator = 1000000;
    m_state.rate_denominator = 256 - (u32)value;
    m_state.phase %= GetPeriod();
}

void SoundBlaster::SetRate(u32 rate)
{
    m_state.rate_numerator = CLAMP(rate, k_sound_blaster_min_rate, k_sound_blaster_max_rate);
    m_state.rate_denominator = 1;
    m_state.phase %= GetPeriod();
}

void SoundBlaster::SetDirect(u8 value)
{
    s16 sample = (s16)((u16)(value ^ 0x80) << 8);

    m_state.left_sample = sample;
    m_state.right_sample = sample;
    m_state.previous_left_sample = sample;
    m_state.previous_right_sample = sample;
}

// On 1.xx the speaker commands pause the DMA as well
void SoundBlaster::SetSpeaker(bool on)
{
    m_state.speaker = on;

    if ((m_version_major == 1) && m_state.transfer.active)
        SetPaused(true, false);
}

void SoundBlaster::SetPaused(bool paused, bool word)
{
    SoundBlaster_Transfer& transfer = m_state.transfer;

    if (!transfer.active || ((transfer.format == SOUND_BLASTER_FORMAT_PCM16) != word))
        return;

    transfer.paused = paused;

    if (m_state.transfers_needed != 0)
        SetRequest(!paused);
}

void SoundBlaster::WriteMIDI(u8 value)
{
    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_MIDI, TRACE_MIDI_DATA)))
    {
        GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_MIDI, TRACE_MIDI_DATA);
        entry->midi.value = value;
        entry->midi.source = TRACE_MIDI_SOURCE_DSP;
        entry->midi.mode = m_state.midi_uart ? 1 : 0;
    }

    if (IsValidPointer(m_midi_out))
        m_midi_out->Write(value, TRACE_MIDI_SOURCE_DSP);
}

// Creative's reset defaults. Bits 4 and 0 of a CT1345 volume aren't used and read high, as on the one clone documented
// Voice, FM, CD and master reset to full volume. Creative's levels would leave most games, which never write the mixer,
// 22 or 28 dB down, and CD audio close to silent
void SoundBlaster::ResetMixer()
{
    u8* mixer = m_state.mixer;

    memset(mixer, 0, SOUND_BLASTER_MIXER_SIZE);

    if (m_mixer_type == k_sound_blaster_mixer_ct1345)
    {
        mixer[0x04] = 0xEE;
        mixer[0x22] = 0xEE;
        mixer[0x26] = 0xEE;
        mixer[0x28] = 0xEE;
    }
    else if (m_mixer_type == k_sound_blaster_mixer_ct1745)
    {
        for (int reg = 0x30; reg <= 0x37; reg++)
            mixer[reg] = 31 << 3;

        mixer[0x3C] = 0x1F;
        mixer[0x3D] = 0x15;
        mixer[0x3E] = 0x0B;

        for (int reg = 0x44; reg <= 0x47; reg++)
            mixer[reg] = 8 << 4;

        UpdateResources();
    }

    UpdateGains();
}

void SoundBlaster::UpdateResources()
{
    int line = m_state.irq_line == k_pic_redirected_irq ? 2 : m_state.irq_line;

    m_state.mixer[0x80] = (u8)(line == 2 ? 0x01 : line == 5 ? 0x02 : line == 7 ? 0x04 : 0x08);
    m_state.mixer[0x81] = (u8)((1 << m_state.dma_8) | (m_state.dma_16 >= 0 ? 1 << m_state.dma_16 : 0));
}

u8 SoundBlaster::ReadMixer() const
{
    const u8* mixer = m_state.mixer;
    u8 index = m_state.mixer_index;

    if (m_mixer_type == k_sound_blaster_mixer_ct1345)
    {
        switch (index)
        {
            case 0x04:
            case 0x22:
            case 0x26:
            case 0x28:
            case 0x2E:
                return (u8)(mixer[index] | 0x11);
            case 0x0A:
            case 0x0C:
            case 0x0E:
                return mixer[index];
            default:
                return 0xFF;
        }
    }

    switch (index)
    {
        case 0x04:
            return (u8)((mixer[0x32] & 0xF0) | (mixer[0x33] >> 4));
        case 0x0A:
            return mixer[0x3A] >> 5;
        case 0x22:
            return (u8)((mixer[0x30] & 0xF0) | (mixer[0x31] >> 4));
        case 0x26:
            return (u8)((mixer[0x34] & 0xF0) | (mixer[0x35] >> 4));
        case 0x28:
            return (u8)((mixer[0x36] & 0xF0) | (mixer[0x37] >> 4));
        case 0x2E:
            return (u8)((mixer[0x38] & 0xF0) | (mixer[0x39] >> 4));
        case 0x80:
        case 0x81:
            return mixer[index];
        case 0x82:
            return GetInterruptStatus();
        default:
            return (index >= 0x30) && (index <= 0x47) ? mixer[index] : 0xFF;
    }
}

// The old registers map onto the new ones, 4 bits filling out 5
void SoundBlaster::WriteCompatibleVolume(u8 left, u8 value)
{
    m_state.mixer[left] = (u8)(((value >> 4) << 4) | 0x08);
    m_state.mixer[left + 1] = (u8)(((value & 0x0F) << 4) | 0x08);
}

void SoundBlaster::WriteMixer(u8 value)
{
    u8 index = m_state.mixer_index;
    u8* mixer = m_state.mixer;

    if (index == 0x00)
    {
        ResetMixer();

        if (unlikely(m_trace_logger->IsEventEnabled(TRACE_MIXER, TRACE_MIXER_CONTROL)))
            TraceMixer(TRACE_MIXER_CONTROL, value);

        return;
    }

    if (m_mixer_type == k_sound_blaster_mixer_ct1345)
    {
        switch (index)
        {
            case 0x04:
            case 0x22:
            case 0x26:
            case 0x28:
            case 0x2E:
                mixer[index] = value & 0xEE;
                break;
            case 0x0A:
                mixer[index] = value & 0x06;
                break;
            case 0x0C:
                mixer[index] = value & 0x2E;
                break;
            case 0x0E:
                mixer[index] = value & (k_sound_blaster_stereo_switch | k_sound_blaster_output_filter);
                m_state.phase %= GetPeriod();
                break;
            default:
                break;
        }
    }
    else
    {
        switch (index)
        {
            case 0x04:
                WriteCompatibleVolume(0x32, value);
                break;
            case 0x0A:
                mixer[0x3A] = (u8)(((value & 0x07) << 5) | 0x10);
                break;
            case 0x22:
                WriteCompatibleVolume(0x30, value);
                break;
            case 0x26:
                WriteCompatibleVolume(0x34, value);
                break;
            case 0x28:
                WriteCompatibleVolume(0x36, value);
                break;
            case 0x2E:
                WriteCompatibleVolume(0x38, value);
                break;
            case 0x80:
            {
                int line = (value & 0x01) != 0 ? k_pic_redirected_irq : (value & 0x02) != 0 ? 5 :
                    (value & 0x04) != 0 ? 7 : (value & 0x08) != 0 ? 10 : m_state.irq_line;

                m_state.irq_line = line;
                UpdateIRQ();
                UpdateResources();
                break;
            }
            case 0x81:
            {
                int low = (value & 0x01) != 0 ? 0 : (value & 0x02) != 0 ? 1 : (value & 0x08) != 0 ? 3 : m_state.dma_8;
                int high = (value & 0x20) != 0 ? 5 : (value & 0x40) != 0 ? 6 : (value & 0x80) != 0 ? 7 : -1;

                SetRequest(false);
                m_state.dma_8 = low;
                m_state.dma_16 = high;
                UpdateResources();

                if (m_state.transfers_needed != 0)
                    SetRequest(!m_state.transfer.paused);
                break;
            }
            default:
                if ((index >= 0x30) && (index <= 0x47))
                    mixer[index] = value;
                break;
        }
    }

    UpdateGains();

    if (unlikely(m_trace_logger->IsEnabled(TRACE_MIXER)))
        TraceMixer((index == 0x0C) || (index == 0x0E) || (index >= 0x3C) ? TRACE_MIXER_CONTROL : TRACE_MIXER_VOLUME,
            value);

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_PCM, TRACE_PCM_CONFIG)) && (index == 0x80 || index == 0x81))
        TraceEvent(TRACE_PCM_CONFIG, index);
}

// The gain of one volume register, in Q15
s32 SoundBlaster::GetLevel(u8 reg, bool right) const
{
    if (m_mixer_type == k_sound_blaster_mixer_ct1345)
        return k_sound_blaster_ct1345_gain[(m_state.mixer[reg] >> (right ? 1 : 5)) & 0x07];

    return k_sound_blaster_ct1745_gain[(m_state.mixer[reg + (right ? 1 : 0)] >> 3) & 0x1F];
}

// The output gain stage can go past full scale, which holds there rather than clip the mix
void SoundBlaster::UpdateGains()
{
    u8 voice = m_mixer_type == k_sound_blaster_mixer_ct1345 ? 0x04 : 0x32;
    u8 fm = m_mixer_type == k_sound_blaster_mixer_ct1345 ? 0x26 : 0x34;
    u8 master = m_mixer_type == k_sound_blaster_mixer_ct1345 ? 0x22 : 0x30;
    u8 cd = m_mixer_type == k_sound_blaster_mixer_ct1345 ? 0x28 : 0x36;

    for (int side = 0; side < 2; side++)
    {
        if (m_mixer_type == k_sound_blaster_mixer_none)
        {
            m_gain[SOUND_BLASTER_SOURCE_VOICE][side] = 32768;
            m_gain[SOUND_BLASTER_SOURCE_FM][side] = 32768;
            m_gain[SOUND_BLASTER_SOURCE_CD][side] = 32768;
            continue;
        }

        s64 output = 32768;

        if (m_mixer_type == k_sound_blaster_mixer_ct1745)
            output = k_sound_blaster_output_gain[(m_state.mixer[0x41 + side] >> 6) & 0x03];

        s64 master_gain = ((s64)GetLevel(master, side != 0) * output) >> 15;
        s64 voice_gain = ((s64)GetLevel(voice, side != 0) * master_gain) >> 15;
        s64 fm_gain = ((s64)GetLevel(fm, side != 0) * master_gain) >> 15;
        s64 cd_gain = ((s64)GetLevel(cd, side != 0) * master_gain) >> 15;

        // The SB16's output switches can take the CD out of the mix, bit 2 for its left and bit 1 for its right
        if ((m_mixer_type == k_sound_blaster_mixer_ct1745) && ((m_state.mixer[0x3C] & (side != 0 ? 0x02 : 0x04)) == 0))
            cd_gain = 0;

        m_gain[SOUND_BLASTER_SOURCE_VOICE][side] = (s32)MIN(voice_gain, (s64)32768);
        m_gain[SOUND_BLASTER_SOURCE_FM][side] = (s32)MIN(fm_gain, (s64)32768);
        m_gain[SOUND_BLASTER_SOURCE_CD][side] = (s32)MIN(cd_gain, (s64)32768);
    }
}

// Only the Pro's 3.2 kHz is documented. The other cards have a filter of unknown cutoff, this one follows the rate
float SoundBlaster::GetFilterCutoff() const
{
    if (m_mixer_type == k_sound_blaster_mixer_ct1345)
        return (m_state.mixer[0x0E] & k_sound_blaster_output_filter) != 0 ? 0.0f : k_sound_blaster_pro_cutoff;

    return k_sound_blaster_cutoff_ratio * (float)GetSampleRate();
}

void SoundBlaster::SetMPUInterrupt(bool active)
{
    m_state.irq_mpu = active;
    UpdateIRQ();
}

// One line for every source on a 4.xx, held until each is acknowledged
void SoundBlaster::UpdateIRQ()
{
    bool asserted = m_state.irq_8 || m_state.irq_16 || m_state.irq_mpu;
    int line = asserted ? m_state.irq_line : -1;

    if (!IsValidPointer(m_pic) || (line == m_asserted_line))
        return;

    if (m_asserted_line >= 0)
        m_pic->SetIRQLine(m_asserted_line, false);

    if (line >= 0)
        m_pic->SetIRQLine(line, true);

    m_asserted_line = line;
}

// Machine clock until which a port reads the same without side effects, 0 when a read changes something
u64 SoundBlaster::GetReadStableUntil(u16 port) const
{
    u64 reset = m_state.reset_pending ? m_state.ready_clocks : GDOX_NO_EVENT;

    switch (port & 0x0F)
    {
        case 0x05:
            return GDOX_NO_EVENT;
        case 0x0A:
            return m_state.output_count != 0 ? 0 : reset;
        case 0x0C:
            return (m_state.resetting || m_state.reset_pending || m_state.transfer.high_speed) ? reset : 0;
        case 0x0E:
            return m_state.irq_8 ? 0 : reset;
        case 0x0F:
            return m_state.irq_16 ? 0 : GDOX_NO_EVENT;
        default:
            return 0;
    }
}

// Frames read ahead play without one, up to the frame whose request reaches the controller again
u64 SoundBlaster::GetClocksToEvent() const
{
    if (!IsRunning())
        return GDOX_NO_EVENT;

    u64 numerator = m_state.rate_numerator;
    u64 frames = 1 + GetFramesAhead();
    return div_ceil_u64((frames * GetPeriod()) - m_state.phase, numerator);
}

void SoundBlaster::TraceCommand()
{
    u8 event = m_state.command == 0x10 ? TRACE_PCM_DIRECT : TRACE_PCM_COMMAND;

    if (!m_trace_logger->IsEventEnabled(TRACE_PCM, event))
        return;

    GDOX_Trace_Entry* entry = TraceEvent(event, m_state.command);
    entry->pcm.operand_count = m_state.operand_count;

    for (int i = 0; i < 3; i++)
        entry->pcm.operands[i] = m_state.operands[i];
}

void SoundBlaster::TraceTransfer(bool handoff)
{
    const SoundBlaster_Transfer& transfer = m_state.transfer;
    GDOX_Trace_Entry* entry = TraceEvent(TRACE_PCM_TRANSFER, m_state.command);
    entry->flags |= handoff ? TRACE_PCM_FLAG_HANDOFF : 0;
    entry->pcm.format = transfer.format;
    entry->pcm.flags = (u8)((transfer.auto_init ? 0x01 : 0) | (transfer.input ? 0x02 : 0) |
        (transfer.high_speed ? 0x04 : 0) | (IsStereo() ? 0x08 : 0) | (transfer.is_signed ? 0x10 : 0));
    entry->pcm.length = handoff ? transfer.next_length : transfer.length;
    entry->pcm.remaining = transfer.remaining;
    entry->pcm.rate = GetSampleRate();
    entry->pcm.rate_source = m_state.rate_denominator == 1 ? 0x41 : 0x40;
    entry->pcm.dma = (u8)(transfer.format == SOUND_BLASTER_FORMAT_PCM16 && IsWordChannel() ? m_state.dma_16 :
        m_state.dma_8);
}

GDOX_Trace_Entry* SoundBlaster::TraceEvent(u8 event, u8 value)
{
    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_PCM, event);
    entry->pcm.command = value;
    entry->pcm.flags = GetInterruptStatus();
    entry->pcm.dma = (u8)m_state.dma_8;
    entry->pcm.dma16 = (u8)m_state.dma_16;
    entry->pcm.irq = (u8)m_state.irq_line;
    entry->pcm.version = m_version_major;
    entry->pcm.minor = m_version_minor;
    entry->pcm.queue = (u8)m_state.output_count;
    return entry;
}

void SoundBlaster::TraceEnd(u8 reason)
{
    GDOX_Trace_Entry* entry = TraceEvent(TRACE_PCM_END, m_state.command);
    entry->pcm.reason = reason;
    entry->pcm.format = m_state.transfer.format;
    entry->pcm.length = m_state.transfer.length;
    entry->pcm.remaining = m_state.transfer.remaining;
    entry->pcm.underruns = m_trace_underruns;
    m_trace_underruns = 0;
}

void SoundBlaster::TraceMixer(u8 event, u8 value)
{
    if (!m_trace_logger->IsEventEnabled(TRACE_MIXER, event))
        return;

    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_MIXER, event);
    entry->mixer.index = m_state.mixer_index;
    entry->mixer.raw = value;
    entry->mixer.value = ReadMixer();
    entry->mixer.mixer = (u8)m_mixer_type;

    if (event != TRACE_MIXER_READ && entry->mixer.value != value)
        entry->flags |= TRACE_ENTRY_IGNORED;
}

void SoundBlaster::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
}

void SoundBlaster::LoadState(std::istream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
    SanitizeState();
}

void SoundBlaster::Serialize(StateSerializer& serializer)
{
    SoundBlaster_Transfer& transfer = m_state.transfer;

    G_SERIALIZE(serializer, m_state.resetting);
    G_SERIALIZE(serializer, m_state.reset_pending);
    G_SERIALIZE(serializer, m_state.ready_clocks);
    G_SERIALIZE(serializer, m_state.midi_uart);
    G_SERIALIZE(serializer, m_state.command);
    G_SERIALIZE_ARRAY(serializer, m_state.operands, SOUND_BLASTER_OPERANDS);
    G_SERIALIZE(serializer, m_state.operand_count);
    G_SERIALIZE(serializer, m_state.operands_needed);
    G_SERIALIZE_ARRAY(serializer, m_state.output, SOUND_BLASTER_OUTPUT_SIZE);
    G_SERIALIZE(serializer, m_state.output_head);
    G_SERIALIZE(serializer, m_state.output_count);
    G_SERIALIZE(serializer, m_state.last_output);
    G_SERIALIZE(serializer, m_state.write_status_reads);
    G_SERIALIZE(serializer, m_state.test_register);
    G_SERIALIZE_ARRAY(serializer, m_state.asp_registers, SOUND_BLASTER_ASP_REGISTERS);
    G_SERIALIZE(serializer, m_state.e2_value);
    G_SERIALIZE(serializer, m_state.e2_count);
    G_SERIALIZE(serializer, m_state.e2_pending);
    G_SERIALIZE(serializer, transfer.length);
    G_SERIALIZE(serializer, transfer.remaining);
    G_SERIALIZE(serializer, transfer.next_length);
    G_SERIALIZE(serializer, transfer.format);
    G_SERIALIZE(serializer, transfer.active);
    G_SERIALIZE(serializer, transfer.paused);
    G_SERIALIZE(serializer, transfer.auto_init);
    G_SERIALIZE(serializer, transfer.input);
    G_SERIALIZE(serializer, transfer.high_speed);
    G_SERIALIZE(serializer, transfer.stereo);
    G_SERIALIZE(serializer, transfer.is_signed);
    G_SERIALIZE(serializer, transfer.exit_pending);
    G_SERIALIZE(serializer, transfer.next_pending);
    G_SERIALIZE(serializer, m_state.block_size);
    G_SERIALIZE(serializer, m_state.rate_numerator);
    G_SERIALIZE(serializer, m_state.rate_denominator);
    G_SERIALIZE(serializer, m_state.phase);
    G_SERIALIZE_ARRAY(serializer, m_state.latch, SOUND_BLASTER_LATCH_SIZE);
    G_SERIALIZE(serializer, m_state.latch_count);
    G_SERIALIZE(serializer, m_state.transfers_needed);
    G_SERIALIZE_ARRAY(serializer, m_state.ahead, SOUND_BLASTER_AHEAD_SIZE);
    G_SERIALIZE(serializer, m_state.ahead_size);
    G_SERIALIZE(serializer, m_state.ahead_read);
    G_SERIALIZE(serializer, m_state.requesting);
    G_SERIALIZE(serializer, m_state.left_sample);
    G_SERIALIZE(serializer, m_state.right_sample);
    G_SERIALIZE(serializer, m_state.previous_left_sample);
    G_SERIALIZE(serializer, m_state.previous_right_sample);
    G_SERIALIZE(serializer, m_state.speaker);
    G_SERIALIZE(serializer, m_state.adpcm_reference);
    G_SERIALIZE(serializer, m_state.adpcm_step);
    G_SERIALIZE(serializer, m_state.adpcm_byte);
    G_SERIALIZE(serializer, m_state.adpcm_codes);
    G_SERIALIZE(serializer, m_state.adpcm_seed);
    G_SERIALIZE(serializer, m_state.irq_8);
    G_SERIALIZE(serializer, m_state.irq_16);
    G_SERIALIZE(serializer, m_state.irq_mpu);
    G_SERIALIZE(serializer, m_state.irq_line);
    G_SERIALIZE(serializer, m_state.dma_8);
    G_SERIALIZE(serializer, m_state.dma_16);
    G_SERIALIZE(serializer, m_state.mixer_index);
    G_SERIALIZE_ARRAY(serializer, m_state.mixer, SOUND_BLASTER_MIXER_SIZE);
}

void SoundBlaster::SanitizeState()
{
    SoundBlaster_Transfer& transfer = m_state.transfer;

    m_state.operands_needed = MIN(m_state.operands_needed, (u8)(SOUND_BLASTER_OPERANDS - 1));
    u8 operand_limit = m_state.operands_needed != 0 ? m_state.operands_needed : (u8)(SOUND_BLASTER_OPERANDS - 1);
    m_state.operand_count = MIN(m_state.operand_count, operand_limit);
    m_state.output_head %= SOUND_BLASTER_OUTPUT_SIZE;
    m_state.output_count = MIN(m_state.output_count, (u8)SOUND_BLASTER_OUTPUT_SIZE);
    m_state.latch_count %= SOUND_BLASTER_LATCH_SIZE + 1;
    m_state.transfers_needed = MIN(m_state.transfers_needed, (u8)SOUND_BLASTER_LATCH_SIZE);
    m_state.ahead_size = MIN(m_state.ahead_size, (u16)SOUND_BLASTER_AHEAD_SIZE);
    m_state.ahead_read = MIN(m_state.ahead_read, m_state.ahead_size);
    transfer.format = MIN(transfer.format, (u8)SOUND_BLASTER_FORMAT_SILENCE);
    m_state.adpcm_step = MIN(m_state.adpcm_step, (u8)5);

    if ((m_state.rate_numerator == 0) || (m_state.rate_denominator == 0))
    {
        m_state.rate_numerator = k_sound_blaster_default_rate;
        m_state.rate_denominator = 1;
    }

    m_state.phase %= GetPeriod();
    m_state.dma_8 &= 0x03;
    m_state.dma_16 = (m_state.dma_16 >= 5) && (m_state.dma_16 <= 7) ? m_state.dma_16 : -1;
    m_state.irq_line = CLAMP(m_state.irq_line, 2, 10);
    m_asserted_line = -1;
    UpdateGains();
    UpdateIRQ();
}
