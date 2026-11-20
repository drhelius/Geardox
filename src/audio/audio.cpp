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

#include <math.h>
#include "audio.h"
#include "../cdrom/cdrom_audio.h"
#include "../system/dma.h"
#include "../system/machine_profiles.h"
#include "../system/pic.h"
#include "../system/scheduler.h"
#include "../common/trace_logger.h"
#include "../common/state_serializer.h"

static const int k_audio_dma_channels[6] = { 0, 1, 3, 5, 6, 7 };

Audio::Audio()
{
    InitPointer(m_opl[0]);
    InitPointer(m_opl[1]);
    InitPointer(m_sound_blaster);
    InitPointer(m_mpu401);
    InitPointer(m_midi_out);
    InitPointer(m_midi_synth);
    InitPointer(m_ultrasound);
    InitPointer(m_dma);
    InitPointer(m_cdrom_audio);
    InitPointer(m_pic);
    InitPointer(m_scheduler);
    InitPointer(m_pit);
    m_trace_logger = TraceLogger::GetDisabled();
    memset(m_trace_fm_status, 0, sizeof(m_trace_fm_status));
    m_mute = false;
    m_master_volume = 1.0f;
    m_fm_volume = 1.0f;
    m_pcm_volume = 1.0f;
    m_cdda_volume = 1.0f;
    m_buzzer_volume = 1.0f;
    m_gus_volume = 1.0f;
    m_mt32_volume = 1.0f;
    m_gm_volume = 1.0f;
    m_opl_chips = 0;
    m_opl3 = false;
    m_dsp = false;
    m_mixer = false;
    m_mpu = false;
    m_card_mpu = false;
    m_mpu_irq = false;
    m_gus = false;
    m_pcm_cutoff = -1.0f;
    m_pcm_lowpass_alpha_q15 = 32768;
    m_fm_gain_left = 32768;
    m_fm_gain_right = 32768;
    m_pcm_gain_left = 32768;
    m_pcm_gain_right = 32768;
    m_cdda_drive_gains[0] = 32768;
    m_cdda_drive_gains[1] = 0;
    m_cdda_drive_gains[2] = 0;
    m_cdda_drive_gains[3] = 32768;

    for (int i = 0; i < 4; i++)
        m_cdda_gains[i] = m_cdda_drive_gains[i];

    m_buffer_index = 0;
    m_buffer_overflow = false;
    m_frame_samples = 0;
    m_channel_scopes = false;
    m_vgm_recording_enabled = false;

    for (int i = 0; i < AUDIO_SOURCE_COUNT; i++)
        m_source_mute[i] = false;

    for (int i = 0; i < 8; i++)
        m_dma_routes[i] = -1;

    memset(m_fm_buffer, 0, sizeof(m_fm_buffer));
    memset(m_pcm_buffer, 0, sizeof(m_pcm_buffer));
    memset(m_cdda_buffer, 0, sizeof(m_cdda_buffer));
    memset(m_buzzer_buffer, 0, sizeof(m_buzzer_buffer));
    memset(m_gus_buffer, 0, sizeof(m_gus_buffer));
    memset(m_midi_buffer, 0, sizeof(m_midi_buffer));
    m_midi_rendered = false;
    memset(m_fm_channel_buffer, 0, sizeof(m_fm_channel_buffer));
}

Audio::~Audio()
{
    SafeDelete(m_ultrasound);
    SafeDelete(m_mpu401);
    SafeDelete(m_midi_synth);
    SafeDelete(m_midi_out);
    SafeDelete(m_sound_blaster);
    SafeDelete(m_opl[1]);
    SafeDelete(m_opl[0]);
}

void Audio::Init(Scheduler* scheduler, CdRomAudio* cdrom_audio, PIC* pic, PIT* pit, DMA* dma)
{
    m_scheduler = scheduler;
    m_cdrom_audio = cdrom_audio;
    m_pit = pit;
    m_pic = pic;
    m_dma = dma;

    for (int i = 0; i < AUDIO_OPL_CHIPS; i++)
    {
        if (!IsValidPointer(m_opl[i]))
            m_opl[i] = new OPL();
    }

    if (!IsValidPointer(m_sound_blaster))
        m_sound_blaster = new SoundBlaster();

    if (!IsValidPointer(m_mpu401))
        m_mpu401 = new MPU401();

    if (!IsValidPointer(m_midi_out))
        m_midi_out = new MidiOut();

    if (!IsValidPointer(m_midi_synth))
        m_midi_synth = new MidiSynth();

    if (!IsValidPointer(m_ultrasound))
        m_ultrasound = new UltraSound();

    m_mpu401->Init(m_midi_out);
    m_midi_synth->Init(m_midi_out);
    m_sound_blaster->Init(pic, dma, m_midi_out);
    m_sound_blaster->SetTraceClocks(&m_state.clocks);
    m_ultrasound->Init(pic, dma, m_midi_out);

    for (int i = 0; i < 8; i++)
        m_dma_routes[i] = -1;

    UpdateDMARouting();
    Reset();
}

void Audio::SetTraceLogger(TraceLogger* trace_logger)
{
    m_trace_logger = IsValidPointer(trace_logger) ? trace_logger : TraceLogger::GetDisabled();
    m_sound_blaster->SetTraceLogger(trace_logger);
    m_mpu401->SetTraceLogger(trace_logger);
    m_midi_out->SetTraceLogger(trace_logger);
    m_ultrasound->SetTraceLogger(trace_logger);
}

void Audio::SetCard(int card, int irq, int dma, int mpu)
{
    const GDOX_Sound_Card_Info& info = k_sound_cards[card];

    m_opl_chips = info.opl_chips;
    m_opl3 = info.opl3;
    m_dsp = info.dsp_major != 0;
    m_mixer = info.mixer != k_sound_blaster_mixer_none;
    m_mpu = mpu != GDOX_MPU_OFF;
    m_card_mpu = info.mpu;
    m_mpu401->SetIntelligent(mpu == GDOX_MPU_INTELLIGENT);
    m_sound_blaster->SetCard(card, irq, dma);

    for (int i = 0; i < AUDIO_OPL_CHIPS; i++)
        m_opl[i]->SetModel(m_opl3 ? OPL::OPL_MODEL_OPL3 : OPL::OPL_MODEL_OPL2);

    UpdateDMARouting();
}

void Audio::SetGUS(bool enabled)
{
    m_gus = enabled;
    m_ultrasound->SetEnabled(enabled);
    UpdateDMARouting();
}

void Audio::UpdateDMARouting()
{
    if (!IsValidPointer(m_dma))
        return;

    const SoundBlaster::SoundBlaster_State* blaster = m_sound_blaster->GetState();
    int dram = m_gus ? m_ultrasound->GetDRAMChannel() : -1;
    int adc = m_gus ? m_ultrasound->GetADCChannel() : -1;

    for (int i = 0; i < 6; i++)
    {
        int channel = k_audio_dma_channels[i];
        bool blaster_channel = m_dsp && ((channel == blaster->dma_8) || (channel == blaster->dma_16));
        int route = 0;

        if (!blaster_channel && (channel == dram))
            route = 1;
        else if (!blaster_channel && (channel == adc))
            route = 2;

        if (route == m_dma_routes[channel])
            continue;

        m_dma_routes[channel] = route;

        if (route == 1)
        {
            GDOX_DMA_Endpoint endpoint = { m_ultrasound, UltraSound::DRAMReadCallback, UltraSound::DRAMWriteCallback,
                UltraSound::DRAMEndCallback, NULL };
            m_dma->SetEndpoint(channel, endpoint);
        }
        else if (route == 2)
        {
            GDOX_DMA_Endpoint endpoint = { m_ultrasound, UltraSound::ADCReadCallback, UltraSound::ADCWriteCallback,
                UltraSound::ADCEndCallback, NULL };
            m_dma->SetEndpoint(channel, endpoint);
        }
        else
        {
            GDOX_DMA_Endpoint endpoint = { m_sound_blaster, SoundBlaster::DMAReadCallback,
                SoundBlaster::DMAWriteCallback, NULL, SoundBlaster::DMAChangeCallback };
            m_dma->SetEndpoint(channel, endpoint);
        }
    }
}

void Audio::Reset()
{
    u64 clocks = m_scheduler->GetClocks();

    for (int i = 0; i < AUDIO_OPL_CHIPS; i++)
        m_opl[i]->Reset();

    m_sound_blaster->Reset(clocks);
    m_ultrasound->Reset();
    m_mpu401->Reset();
    m_midi_out->Reset();
    UpdateMPUInterrupt();

    m_state.sample_clock_counter = 0;
    m_state.pcm_lowpass_left = 0;
    m_state.pcm_lowpass_right = 0;
    m_state.output_dc_left = 0;
    m_state.output_dc_right = 0;
    m_state.clocks = clocks;
    m_state.buzzer_tick = m_pit->GetTick(clocks);
    m_state.buzzer_ticks = 0;
    m_state.buzzer_level = 0;
    m_buffer_index = 0;
    m_buffer_overflow = false;
    UpdateDMARouting();
    UpdateGains();
    UpdatePCMFilter();
    UpdateEvent();
}

// The SB16's MPU shares the card's line (SBG p27). A standalone one drives the bus's IRQ2, which an AT takes as IRQ9
// (MPU-IPC-T p4)
void Audio::UpdateMPUInterrupt()
{
    bool asserted = m_mpu && m_mpu401->IsIRQAsserted();
    bool standalone = asserted && !m_card_mpu;

    m_sound_blaster->SetMPUInterrupt(asserted && m_card_mpu);

    if ((standalone != m_mpu_irq) && IsValidPointer(m_pic))
    {
        m_mpu_irq = standalone;
        m_pic->SetIRQLine(k_mpu401_irq, standalone);
    }
}

// Status comes from the left chip of a pair at 388h, the way Creative's table reads
u8 Audio::Read(u16 port, u64 clocks)
{
    Synchronize(clocks);

    if (IsFMPort(port))
    {
        bool right = (m_opl_chips > 1) && ((port & 0x03F0) == k_sound_blaster_port) && ((port & 0x0E) == 0x02);
        u8 value = m_opl[right ? 1 : 0]->Read((u8)(port & 0x03));

        if (unlikely(m_trace_logger->IsEventEnabled(TRACE_FM, TRACE_FM_STATUS)) && (port & 0x01) == 0 &&
            value != m_trace_fm_status[right ? 1 : 0])
            TraceFMStatus(right ? 1 : 0, value);

        return value;
    }

    // A request that takes the place of the byte just read needs a new edge on the line
    if (IsMPUPort(port))
    {
        u8 value = m_mpu401->Read(port);
        UpdateMPUInterrupt();

        if (m_mpu401->Deliver())
            UpdateMPUInterrupt();

        UpdateEvent();
        return value;
    }

    if (IsGUSPort(port))
    {
        u8 value = m_ultrasound->Read(port);
        UpdateEvent();
        return value;
    }

    return m_sound_blaster->Read(port, clocks);
}

u8 Audio::Peek(u16 port, u64 clocks) const
{
    if (IsFMPort(port))
    {
        bool right = (m_opl_chips > 1) && ((port & 0x03F0) == k_sound_blaster_port) && ((port & 0x0E) == 0x02);
        return m_opl[right ? 1 : 0]->Peek((u8)(port & 0x03));
    }

    if (IsMPUPort(port))
        return m_mpu401->Peek(port);

    if (IsGUSPort(port))
        return m_ultrasound->Peek(port);

    return m_sound_blaster->Peek(port, clocks);
}

void Audio::Write(u16 port, u8 value, u64 clocks)
{
    Synchronize(clocks);

    if (IsFMPort(port))
    {
        if (m_gus && ((port & 0x03FF) == k_audio_fm_port))
            m_ultrasound->SnoopAdLib(value);

        WriteFM(port, value);
        return;
    }

    m_midi_out->SetPosition(m_buffer_index >> 1);

    if (IsMPUPort(port))
    {
        m_mpu401->Write(port, value);
        UpdateMPUInterrupt();
        UpdateEvent();
        return;
    }

    if (IsGUSPort(port))
    {
        m_ultrasound->Write(port, value);
        UpdateDMARouting();
        UpdateEvent();
        return;
    }

    m_sound_blaster->Write(port, value, clocks);
    UpdateDMARouting();
    UpdateGains();
    UpdatePCMFilter();
    UpdateEvent();
}

// A pair of OPL2s splits +0 to +3 between its sides, and takes 388h and +8 together so old software plays on both
// An OPL3 sees A1 on the high pair of each block
void Audio::WriteFM(u16 port, u8 value)
{
    bool block = (port & 0x03F0) == k_sound_blaster_port;
    u8 a0 = port & 0x01;
    u8 a1 = port & 0x02;

    if (m_opl_chips > 1)
    {
        if (block && ((port & 0x0F) <= 0x03))
            WriteChip(a1 != 0 ? 1 : 0, a0, value);
        else
        {
            WriteChip(0, a0, value);
            WriteChip(1, a0, value);
        }

        return;
    }

    WriteChip(0, (u8)(a0 | (m_opl3 && (!block || ((port & 0x08) == 0)) ? a1 : 0)), value);
}

void Audio::WriteChip(int chip, u8 port, u8 value)
{
    bool traced = unlikely(m_trace_logger->IsEnabled(TRACE_FM)) && (port & 0x01) != 0;
    u8 previous = traced ? m_opl[chip]->GetRegister(m_opl[chip]->GetSelectedAddress()) : 0;

    m_opl[chip]->Write(port, value);

    if ((port & 0x01) == 0)
        return;

#ifndef GDOX_DISABLE_VGMRECORDER
    if (m_vgm_recording_enabled)
        m_vgm_recorder.WriteOPL(chip, m_opl[chip]->GetSelectedAddress(), value);
#endif

    if (unlikely(traced))
        TraceFM(chip, value, previous);
}

void Audio::UpdateEvent()
{
    u64 clocks = m_dsp ? m_sound_blaster->GetClocksToEvent() : GDOX_NO_EVENT;

    if (m_mpu)
        clocks = MIN(clocks, m_mpu401->GetClocksToEvent());

    if (m_gus)
        clocks = MIN(clocks, m_ultrasound->GetClocksToEvent());

    m_scheduler->Schedule(SCHEDULER_EVENT_AUDIO, clocks == GDOX_NO_EVENT ? GDOX_NO_EVENT : m_state.clocks + clocks);
}

// One pole: alpha = 1 - exp(-2 * pi * fc / fs), stored as Q1.15. No cutoff, or none under Nyquist, lets all of it through
void Audio::UpdatePCMFilter()
{
    float cutoff = m_dsp ? m_sound_blaster->GetFilterCutoff() : 0.0f;

    if (cutoff == m_pcm_cutoff)
        return;

    m_pcm_cutoff = cutoff;

    if (cutoff <= 0.0f)
    {
        m_pcm_lowpass_alpha_q15 = 32768;
        return;
    }

    float alpha = 1.0f - expf(-2.0f * 3.14159265358979323846f * cutoff / (float)GDOX_AUDIO_SAMPLE_RATE);
    m_pcm_lowpass_alpha_q15 = (u16)(CLAMP(alpha, 0.0f, 1.0f) * 32768.0f + 0.5f);
}

// The drive sets its port gains, ports 0 and 1 to the card's left and right inputs
void Audio::SetCDDAGains(const s32* gains)
{
    for (int i = 0; i < 4; i++)
        m_cdda_drive_gains[i] = gains[i];

    UpdateGains();
}

void Audio::UpdateGains()
{
    m_fm_gain_left = (s32)(((s64)m_sound_blaster->GetGain(SoundBlaster::SOUND_BLASTER_SOURCE_FM, false) * k_audio_fm_gain_q15) >> 15);
    m_fm_gain_right = (s32)(((s64)m_sound_blaster->GetGain(SoundBlaster::SOUND_BLASTER_SOURCE_FM, true) * k_audio_fm_gain_q15) >> 15);
    m_pcm_gain_left = m_sound_blaster->GetGain(SoundBlaster::SOUND_BLASTER_SOURCE_VOICE, false);
    m_pcm_gain_right = m_sound_blaster->GetGain(SoundBlaster::SOUND_BLASTER_SOURCE_VOICE, true);

    for (int i = 0; i < 4; i++)
    {
        s32 card = m_sound_blaster->GetGain(SoundBlaster::SOUND_BLASTER_SOURCE_CD, i >= 2);
        m_cdda_gains[i] = (s32)(((s64)m_cdda_drive_gains[i] * card) >> 15);
    }
}

// Machine clock until which a read of the port repeats the last one, the DSP's change with its events
u64 Audio::GetReadStableUntil(u16 port) const
{
    if (IsFMPort(port))
    {
        if ((port & 0x01) != 0)
            return GDOX_NO_EVENT;

        u64 next = GDOX_NO_EVENT;

        for (int i = 0; i < m_opl_chips; i++)
        {
            u64 cycles = m_opl[i]->GetCyclesToTimerFlag();

            if (cycles != GDOX_NO_EVENT)
                next = MIN(next, m_state.clocks + cycles - m_opl[i]->GetState()->elapsed_cycles);
        }

        return next;
    }

    if (IsMPUPort(port))
    {
        if (m_mpu401->IsIRQAsserted())
            return (port & 0x01) != 0 ? GDOX_NO_EVENT : 0;

        u64 clocks = m_mpu401->GetClocksToEvent();
        return clocks == GDOX_NO_EVENT ? GDOX_NO_EVENT : m_state.clocks + clocks;
    }

    if (IsGUSPort(port))
        return m_ultrasound->GetReadStableUntil(port, m_state.clocks);

    return m_sound_blaster->GetReadStableUntil(port);
}

// One value per channel and output sample for the debugger scopes, only while they are enabled
void Audio::CaptureChannels(int index)
{
    int channels = m_opl3 ? OPL_CHANNEL_COUNT : OPL2_CHANNEL_COUNT;

    for (int chip = 0; chip < m_opl_chips; chip++)
    {
        for (int i = 0; i < channels; i++)
            m_fm_channel_buffer[chip * OPL2_CHANNEL_COUNT + i][index] = m_opl[chip]->GetChannelOutput(i);
    }
}

// Data writes carry the register the address port latched, along with the channel it belongs to
void Audio::TraceFM(int chip, u8 value, u8 previous)
{
    static const s8 k_operators[0x20] =
    {
        0, 1, 2, 3, 4, 5, -1, -1, 6, 7, 8, 9, 10, 11, -1, -1, 12, 13, 14, 15, 16, 17, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
    };
    static const s8 k_opl2_channels[0x10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 0, 1, 2, -1, -1, 0, 1 };

    OPL* opl = m_opl[chip];
    bool opl2 = opl->GetModel() == OPL::OPL_MODEL_OPL2;
    u16 address = opl->GetSelectedAddress();
    int bank = (address >> 8) & 0x01;
    u8 reg = (u8)address;
    u8 group = reg & 0xF0;
    u8 event = TRACE_FM_GLOBAL;
    u8 low = reg & 0x0F;
    s8 channel = opl2 ? k_opl2_channels[low] : (low < 9 ? (s8)low : -1);
    s8 operator_index = -1;
    u16 frequency = 0;
    u16 flags = opl2 ? 0 : TRACE_FM_FLAG_OPL3;

    if ((reg >= 0x20 && reg <= 0x95) || reg >= 0xE0)
    {
        event = TRACE_FM_OPERATOR;
        operator_index = k_operators[reg & 0x1F];
        channel = -1;
        flags |= operator_index < 0 ? TRACE_FM_FLAG_INVALID : 0;
    }
    else if (group == 0xA0 || group == 0xB0 || group == 0xC0)
    {
        event = group == 0xA0 ? TRACE_FM_FREQUENCY : group == 0xB0 ? TRACE_FM_KEY : TRACE_FM_CHANNEL;
        flags |= channel < 0 ? TRACE_FM_FLAG_INVALID : low >= 9 ? TRACE_FM_FLAG_ALIAS : 0;

        if (reg == 0xBD)
            event = TRACE_FM_GLOBAL;
        else if (channel >= 0 && group == 0xA0)
            frequency = (u16)(((opl->GetRegister((u16)((bank << 8) | (0xB0 + channel))) & 0x1F) << 8) | value);
        else if (channel >= 0 && group == 0xB0)
            frequency = (u16)(((value & 0x1F) << 8) | opl->GetRegister((u16)((bank << 8) | (0xA0 + channel))));
    }
    else if (reg >= 0x02 && reg <= 0x04 && bank == 0)
        event = TRACE_FM_TIMER;

    if (!m_trace_logger->IsEventEnabled(TRACE_FM, event))
        return;

    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_FM, event);
    entry->flags |= flags;
    entry->fm.address = address;
    entry->fm.frequency = frequency;
    entry->fm.value = value;
    entry->fm.previous = previous;
    entry->fm.channel = channel < 0 ? 0xFF : (u8)(channel + bank * 9);
    entry->fm.operator_index = operator_index < 0 ? -1 : (s8)(operator_index + bank * 18);
    entry->fm.flags = (u8)chip;
    entry->fm.pairs = opl2 ? 0 : opl->GetRegister(0x104);
}

void Audio::TraceFMStatus(int chip, u8 value)
{
    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_FM, TRACE_FM_STATUS);
    entry->fm.value = value;
    entry->fm.previous = m_trace_fm_status[chip];
    entry->fm.flags = (u8)chip;
    m_trace_fm_status[chip] = value;
}

void Audio::EndFrame(s16* sample_buffer, int* sample_count)
{
    int samples = m_buffer_index;

    m_buffer_index = 0;
    m_buffer_overflow = false;
    m_frame_samples = samples;

    if (m_midi_synth->IsActive() && (m_midi_out->GetCapture() != MidiOut::MIDI_OUT_CAPTURE_OFF) && (samples > 0))
    {
        m_midi_synth->Render(m_midi_buffer, samples >> 1);
        m_midi_rendered = true;
    }
    else if (m_midi_rendered)
    {
        memset(m_midi_buffer, 0, sizeof(m_midi_buffer));
        m_midi_rendered = false;
    }

    if (!IsValidPointer(sample_buffer) || !IsValidPointer(sample_count))
    {
        if (IsValidPointer(sample_count))
            *sample_count = 0;

        return;
    }

    *sample_count = samples;

    if (m_mute || (m_master_volume <= 0.0f))
    {
        memset(sample_buffer, 0, sizeof(s16) * samples);
        return;
    }

    float fm_volume = m_source_mute[AUDIO_SOURCE_FM] ? 0.0f : m_fm_volume;
    float pcm_volume = m_source_mute[AUDIO_SOURCE_PCM] ? 0.0f : m_pcm_volume;
    float cdda_volume = m_source_mute[AUDIO_SOURCE_CDDA] ? 0.0f : m_cdda_volume;
    float buzzer_volume = m_source_mute[AUDIO_SOURCE_BUZZER] ? 0.0f : m_buzzer_volume;
    float gus_volume = m_source_mute[AUDIO_SOURCE_GUS] ? 0.0f : m_gus_volume;
    float midi_volume = m_source_mute[AUDIO_SOURCE_MIDI] ? 0.0f :
        (m_midi_synth->GetEngine() == GDOX_MIDI_ENGINE_MT32) ? m_mt32_volume : m_gm_volume;

    s32 dc_left = m_state.output_dc_left;
    s32 dc_right = m_state.output_dc_right;

    // No source gives the relative levels: FM, PCM and CD-DA all enter at full scale
    if ((m_master_volume == 1.0f) && (fm_volume == 1.0f) && (pcm_volume == 1.0f) && (cdda_volume == 1.0f) &&
        (buzzer_volume == 1.0f) && (gus_volume == 1.0f) && (midi_volume == 1.0f))
    {
        for (int i = 0; i < samples; i += 2)
        {
            s32 left = (s32)m_fm_buffer[i] + m_pcm_buffer[i] + m_cdda_buffer[i] + m_buzzer_buffer[i] + m_gus_buffer[i] +
                m_midi_buffer[i];
            s32 right = (s32)m_fm_buffer[i + 1] + m_pcm_buffer[i + 1] + m_cdda_buffer[i + 1] + m_buzzer_buffer[i + 1] +
                m_gus_buffer[i + 1] + m_midi_buffer[i + 1];
            left = HighPass(dc_left, left);
            right = HighPass(dc_right, right);
            sample_buffer[i] = (s16)CLAMP(left, -32768, 32767);
            sample_buffer[i + 1] = (s16)CLAMP(right, -32768, 32767);
        }
    }
    else
    {
        for (int i = 0; i < samples; i += 2)
        {
            float left = (float)m_fm_buffer[i] * fm_volume + (float)m_pcm_buffer[i] * pcm_volume +
                (float)m_cdda_buffer[i] * cdda_volume + (float)m_buzzer_buffer[i] * buzzer_volume +
                (float)m_gus_buffer[i] * gus_volume + (float)m_midi_buffer[i] * midi_volume;
            float right = (float)m_fm_buffer[i + 1] * fm_volume + (float)m_pcm_buffer[i + 1] * pcm_volume +
                (float)m_cdda_buffer[i + 1] * cdda_volume + (float)m_buzzer_buffer[i + 1] * buzzer_volume +
                (float)m_gus_buffer[i + 1] * gus_volume + (float)m_midi_buffer[i + 1] * midi_volume;
            s32 out_left = (s32)((float)HighPass(dc_left, (s32)left) * m_master_volume);
            s32 out_right = (s32)((float)HighPass(dc_right, (s32)right) * m_master_volume);
            sample_buffer[i] = (s16)CLAMP(out_left, -32768, 32767);
            sample_buffer[i + 1] = (s16)CLAMP(out_right, -32768, 32767);
        }
    }

    m_state.output_dc_left = dc_left;
    m_state.output_dc_right = dc_right;
}

void Audio::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer, GDOX_SAVESTATE_VERSION);

    for (int i = 0; i < AUDIO_OPL_CHIPS; i++)
        m_opl[i]->SaveState(stream);

    m_sound_blaster->SaveState(stream);
    m_mpu401->SaveState(stream);
    m_ultrasound->SaveState(stream);
    m_midi_out->SaveState(stream);
}

void Audio::LoadState(std::istream& stream, int version)
{
    StateSerializer serializer(stream);
    Serialize(serializer, version);

    for (int i = 0; i < AUDIO_OPL_CHIPS; i++)
        m_opl[i]->LoadState(stream);

    m_sound_blaster->LoadState(stream);
    m_mpu401->LoadState(stream);
    m_ultrasound->LoadState(stream);
    m_midi_out->LoadState(stream);
    SanitizeState();
}

// VGM has no chip for the DSP, so only the FM side is recorded
bool Audio::StartVgmRecording(const char* file_path, int clock_rate, const GDOX_VGM_Metadata& metadata)
{
    UNUSED(clock_rate);

    if (m_vgm_recording_enabled || (m_opl_chips == 0))
        return false;

    m_vgm_recorder.Start(file_path, m_opl3 ? GDOX_MASTER_CLOCK_RATE : GDOX_SOUND_CLOCK_RATE, metadata, m_opl3, m_opl_chips > 1);
    m_vgm_recording_enabled = m_vgm_recorder.IsRecording();

    if (m_vgm_recording_enabled)
        WriteVgmInitialState();

    return m_vgm_recording_enabled;
}

void Audio::WriteVgmInitialState()
{
    for (int chip = 0; chip < m_opl_chips; chip++)
    {
        OPL* opl = m_opl[chip];

        if (m_opl3)
        {
            m_vgm_recorder.WriteOPL(chip, 0x105, opl->GetRegister(0x105));
            m_vgm_recorder.WriteOPL(chip, 0x104, opl->GetRegister(0x104));
        }

        for (int bank = 0; bank < (m_opl3 ? 2 : 1); bank++)
        {
            u16 base = (u16)(bank << 8);

            for (int reg = 0x01; reg <= 0xF5; reg++)
            {
                if ((bank != 0) && (reg <= 0x05))
                    continue;

                m_vgm_recorder.WriteOPL(chip, (u16)(base | reg), opl->GetRegister((u16)(base | reg)));
            }
        }
    }
}

void Audio::StopVgmRecording()
{
    if (m_vgm_recording_enabled)
    {
        m_vgm_recorder.Stop();
        m_vgm_recording_enabled = false;
    }
}

bool Audio::IsVgmRecording() const
{
    return m_vgm_recording_enabled;
}

void Audio::Serialize(StateSerializer& serializer, int version)
{
    UNUSED(version);
    G_SERIALIZE(serializer, m_state.sample_clock_counter);
    G_SERIALIZE(serializer, m_state.clocks);
    G_SERIALIZE(serializer, m_state.pcm_lowpass_left);
    G_SERIALIZE(serializer, m_state.pcm_lowpass_right);
    G_SERIALIZE(serializer, m_state.output_dc_left);
    G_SERIALIZE(serializer, m_state.output_dc_right);
    G_SERIALIZE(serializer, m_state.buzzer_tick);
    G_SERIALIZE(serializer, m_state.buzzer_ticks);
    G_SERIALIZE(serializer, m_state.buzzer_level);
}

void Audio::SanitizeState()
{
    m_state.sample_clock_counter %= GDOX_MACHINE_CLOCK_RATE;
    m_state.pcm_lowpass_left = CLAMP(m_state.pcm_lowpass_left, -32768, 32767);
    m_state.pcm_lowpass_right = CLAMP(m_state.pcm_lowpass_right, -32768, 32767);
    m_state.output_dc_left = CLAMP(m_state.output_dc_left, -k_audio_dc_limit, k_audio_dc_limit);
    m_state.output_dc_right = CLAMP(m_state.output_dc_right, -k_audio_dc_limit, k_audio_dc_limit);

    u64 tick = m_pit->GetTick(m_state.clocks);

    if ((m_state.buzzer_tick > tick) || (tick - m_state.buzzer_tick > k_audio_buzzer_max_ticks))
        m_state.buzzer_tick = tick;

    UpdateGains();
    m_pcm_cutoff = -1.0f;
    UpdatePCMFilter();
    UpdateEvent();
    m_mpu_irq = false;
    UpdateMPUInterrupt();

    for (int i = 0; i < 8; i++)
        m_dma_routes[i] = -1;

    UpdateDMARouting();
}
