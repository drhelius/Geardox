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

#include "ultrasound.h"
#include "midi_out.h"
#include "../system/dma.h"
#include "../system/pic.h"
#include "../common/state_serializer.h"
#include "../common/trace_logger.h"

static const u32 k_ultrasound_address_mask = 0x1FFFFFFF;
static const u32 k_ultrasound_loop_mask = 0x1FFFFFE0;
static const u32 k_ultrasound_timer_clocks[2] = { 784, 3136 };
static const u32 k_ultrasound_midi_clocks = GDOX_MACHINE_CLOCK_RATE / 3125;
static const u8 k_ultrasound_sample_units = 4;
static const int k_ultrasound_irqs[8] = { 0, 9, 5, 3, 7, 11, 12, 15 };
static const int k_ultrasound_dmas[8] = { -1, 1, 3, 5, 6, 7, -1, -1 };
static const u16 k_ultrasound_pan[16] = { 0, 13, 26, 41, 57, 75, 94, 116, 141, 169, 203, 244, 297, 372, 500, 4095 };

UltraSound::UltraSound()
{
    InitPointer(m_pic);
    InitPointer(m_dma);
    InitPointer(m_midi_out);
    m_trace_logger = TraceLogger::GetDisabled();
    m_enabled = false;
    m_asserted_irq[0] = 0;
    m_asserted_irq[1] = 0;
    m_dram_request = -1;
    m_adc_request = -1;
    m_dirty_tracking = false;
    memset(&m_state, 0, sizeof(m_state));
    memset(m_dirty_pages, 0, sizeof(m_dirty_pages));
    memset(m_voice_output, 0, sizeof(m_voice_output));

    for (int i = 0; i < ULTRASOUND_VOLUME_LEVELS; i++)
    {
        u32 exponent = (u32)i >> 8;
        u32 mantissa = (u32)i & 0xFF;
        m_gain[i] = ((256 + mantissa) << exponent) >> 8;
    }
}

UltraSound::~UltraSound()
{
}

void UltraSound::Init(PIC* pic, DMA* dma, MidiOut* midi_out)
{
    m_pic = pic;
    m_dma = dma;
    m_midi_out = midi_out;
}

void UltraSound::SetTraceLogger(TraceLogger* trace_logger)
{
    m_trace_logger = IsValidPointer(trace_logger) ? trace_logger : TraceLogger::GetDisabled();
}

void UltraSound::SetEnabled(bool enabled)
{
    m_enabled = enabled;
    UpdateIRQ();
    UpdateDMARequest();
}

void UltraSound::Reset()
{
    memset(m_state.voices, 0, sizeof(m_state.voices));

    for (int i = 0; i < ULTRASOUND_VOICES; i++)
        ResetVoice(m_state.voices[i]);

    m_state.active_voices = 0xCD;
    m_state.voice_select = 0;
    m_state.register_select = 0;
    m_state.data_low = 0;
    m_state.reset = 0;
    m_state.dma_control = 0;
    m_state.dma_address = 0;
    m_state.dma_pointer = 0;
    m_state.dram_address = 0;
    m_state.timer_control = 0;

    for (int i = 0; i < 2; i++)
    {
        m_state.timer_count[i] = 0;
        m_state.timer_preload[i] = 0;
        m_state.timer_phase[i] = 0;
        m_state.timer_irq[i] = false;
    }

    m_state.adlib_select = 0;
    m_state.adlib_control = 0;
    m_state.adlib_status = 0;
    m_state.adlib_data = 0;
    m_state.sb_2xc = 0;
    m_state.sb_2xe = 0;
    m_state.sample_control = 0;
    m_state.sample_rate = 0;
    m_state.sample_phase = 0;
    m_state.sample_units = 0;
    m_state.joystick_trim = 0x1D;
    m_state.dma_tc = false;
    m_state.sample_tc = false;
    m_state.voice_irq = 0xE0;
    m_state.voice_irq_latched = false;
    m_state.mix_control = 0x0B;
    m_state.irq_latch = 0;
    m_state.dma_latch = 0;
    m_state.latch_armed = false;
    m_state.register_bank = 0;
    m_state.jumper = 0x06;
    m_state.midi_control = 0;
    m_state.midi_rx = 0;
    m_state.midi_rx_full = false;
    m_state.midi_tx_clocks = 0;
    m_state.phase = 0;
    m_state.left_sample = 0;
    m_state.right_sample = 0;
    m_state.previous_left_sample = 0;
    m_state.previous_right_sample = 0;
    memset(m_state.dram, 0, sizeof(m_state.dram));
    memset(m_voice_output, 0, sizeof(m_voice_output));

    for (int i = 0; i < ULTRASOUND_PAGES; i++)
        m_dirty_pages[i] = 1;

    UpdateIRQ();
    UpdateDMARequest();
}

void UltraSound::ResetVoice(UltraSound_Voice& voice)
{
    voice.start = 0;
    voice.end = 0;
    voice.address = 0;
    voice.frequency = 0x0400;
    voice.volume = 0;
    voice.control = 0x01;
    voice.volume_control = 0x01;
    voice.ramp_rate = 0;
    voice.ramp_start = 0;
    voice.ramp_end = 0;
    voice.pan = 7;
    voice.ramp_count = 0;
    voice.fetch_key = 0xFFFFFFFF;
    voice.fetch_first = 0;
    voice.fetch_second = 0;
    voice.address_pending = false;
    voice.held_end = 0;
    voice.end_held = false;
}

void UltraSound::ResetGF1()
{
    for (int i = 0; i < ULTRASOUND_VOICES; i++)
        ResetVoice(m_state.voices[i]);

    m_state.dma_control = 0;
    m_state.sample_control = 0;
    m_state.sample_units = 0;
    m_state.timer_control = 0;
    m_state.timer_irq[0] = false;
    m_state.timer_irq[1] = false;
    m_state.dma_tc = false;
    m_state.sample_tc = false;
    m_state.voice_irq = 0xE0;
    m_state.voice_irq_latched = false;
    m_state.reset = 0;
    m_state.left_sample = 0;
    m_state.right_sample = 0;
    m_state.previous_left_sample = 0;
    m_state.previous_right_sample = 0;
    memset(m_voice_output, 0, sizeof(m_voice_output));
}

void UltraSound::Frame()
{
    int voices = GetActiveVoices();
    s32 left = 0;
    s32 right = 0;

    for (int i = 0; i < voices; i++)
        RunVoice(m_state.voices[i], i, left, right);

    m_state.previous_left_sample = m_state.left_sample;
    m_state.previous_right_sample = m_state.right_sample;

    if ((m_state.reset & 0x02) == 0)
    {
        m_state.left_sample = 0;
        m_state.right_sample = 0;
        return;
    }

    m_state.left_sample = (s16)CLAMP(left, -32768, 32767);
    m_state.right_sample = (s16)CLAMP(right, -32768, 32767);
}

void UltraSound::RunVoice(UltraSound_Voice& voice, int index, s32& left, s32& right)
{
    s32 level = voice.volume >> 4;
    s32 left_level = level - k_ultrasound_pan[voice.pan & 0x0F];
    s32 right_level = level - k_ultrasound_pan[15 - (voice.pan & 0x0F)];
    s32 left_gain = left_level > 0 ? (s32)m_gain[left_level] : 0;
    s32 right_gain = right_level > 0 ? (s32)m_gain[right_level] : 0;

    bool audible = (left_gain | right_gain) != 0;

    if (audible)
    {
        if (!voice.address_pending)
            FetchVoice(voice);
        s32 fraction = (s32)(voice.address & 0x1FF);
        s32 first = voice.fetch_first;
        s32 second = voice.fetch_second;
        s32 sample = first + (((second - first) * fraction) >> 9);
        s32 out_left = (s32)(((s64)sample * left_gain) / 65536);
        s32 out_right = (s32)(((s64)sample * right_gain) / 65536);
        left += out_left;
        right += out_right;
        m_voice_output[index] = (s16)CLAMP(out_left + out_right, -32768, 32767);
    }
    else
        m_voice_output[index] = 0;

    if ((voice.control & 0x03) == 0)
    {
        StepAddress(voice);

        if (audible && !voice.address_pending)
            FetchVoice(voice);
    }

    if ((voice.volume_control & 0x03) == 0)
        StepRamp(voice);

    if (!m_state.voice_irq_latched && (((voice.control | voice.volume_control) & 0x80) != 0))
        LatchVoiceIRQ(index);
}

void UltraSound::FetchVoice(UltraSound_Voice& voice)
{
    u32 address = (voice.address >> 9) & (ULTRASOUND_DRAM_SIZE - 1);
    u32 key = address | ((u32)(voice.control & 0x04) << 28);

    if (key == voice.fetch_key)
        return;

    voice.fetch_key = key;

    if ((voice.control & 0x04) != 0)
    {
        u32 bank = address & 0xC0000;
        u32 low = bank | ((address & 0x1FFFF) << 1);
        u32 high = bank | (((address + 1) & 0x1FFFF) << 1);
        voice.fetch_first = (s16)(m_state.dram[low] | (m_state.dram[low + 1] << 8));
        voice.fetch_second = (s16)(m_state.dram[high] | (m_state.dram[high + 1] << 8));
    }
    else
    {
        voice.fetch_first = (s16)((s8)m_state.dram[address] * 256);
        voice.fetch_second = (s16)((s8)m_state.dram[(address + 1) & (ULTRASOUND_DRAM_SIZE - 1)] * 256);
    }
}

void UltraSound::StepAddress(UltraSound_Voice& voice)
{
    s64 step = voice.frequency >> 1;
    s64 address = voice.address;
    s64 start = voice.start;
    s64 end = voice.end_held ? voice.held_end : voice.end;
    bool down = (voice.control & 0x40) != 0;
    s64 next = down ? address - step : address + step;

    if (down ? next >= start : next <= end)
    {
        voice.address = (u32)next & k_ultrasound_address_mask;
        return;
    }

    voice.end_held = false;

    if ((voice.control & 0x20) != 0)
        voice.control |= 0x80;

    if ((voice.control & 0x08) != 0)
    {
        if ((voice.control & 0x10) != 0)
        {
            next = down ? start + (start - next) : end - (next - end);
            voice.control ^= 0x40;
        }
        else
            next = down ? end - (start - next) : start + (next - end);
    }
    else if ((voice.volume_control & 0x04) == 0)
    {
        next = address;
        voice.control |= 0x01;
    }

    voice.address = (u32)next & k_ultrasound_address_mask;
}

void UltraSound::HoldEnd(UltraSound_Voice& voice, u32 previous)
{
    u32 current = voice.end_held ? voice.held_end : previous;
    bool looping = (voice.control & 0x48) == 0x08;

    if (looping && (voice.address > voice.end) && (voice.address <= current))
    {
        voice.held_end = current;
        voice.end_held = true;
    }
    else
        voice.end_held = false;
}

void UltraSound::StepRamp(UltraSound_Voice& voice)
{
    u8 rate = voice.ramp_rate >> 6;

    if (rate >= 2)
    {
        voice.ramp_count++;

        if (voice.ramp_count < (rate == 2 ? 8 : 64))
            return;

        voice.ramp_count = 0;
    }

    s32 increment = voice.ramp_rate & 0x3F;
    s32 delta = rate == 0 ? increment << 3 : increment;
    s32 level = voice.volume >> 1;
    s32 low = voice.ramp_start << 7;
    s32 high = voice.ramp_end << 7;
    bool down = (voice.volume_control & 0x40) != 0;
    s32 next = down ? level - delta : level + delta;

    if (down ? next < low : next > high)
    {
        if ((voice.volume_control & 0x20) != 0)
            voice.volume_control |= 0x80;

        if ((voice.volume_control & 0x08) != 0)
        {
            if ((voice.volume_control & 0x10) != 0)
            {
                next = down ? low + (low - next) : high - (next - high);
                voice.volume_control ^= 0x40;
            }
            else
                next = down ? high - (low - next) : low + (next - high);
        }
        else
        {
            next = down ? low : high;
            voice.volume_control |= 0x01;
        }
    }

    voice.volume = (u16)(CLAMP(next, 0, 0x7FFF) << 1);
}

void UltraSound::LatchVoiceIRQ(int index)
{
    const UltraSound_Voice& voice = m_state.voices[index];
    u8 value = (u8)(index | 0x20);

    if ((voice.control & 0x80) == 0)
        value |= 0x80;

    if ((voice.volume_control & 0x80) == 0)
        value |= 0x40;

    m_state.voice_irq = value;
    m_state.voice_irq_latched = true;

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_GUS, TRACE_GUS_IRQ)))
        TraceEvent(TRACE_GUS_IRQ, (u8)index, value);

    UpdateIRQ();
}

u8 UltraSound::ReadVoiceIRQ()
{
    if (!m_state.voice_irq_latched)
        return (u8)((m_state.voice_irq & 0x1F) | 0xE0);

    u8 value = m_state.voice_irq;
    UltraSound_Voice& voice = m_state.voices[value & 0x1F];

    if ((value & 0x80) == 0)
        voice.control &= ~0x80;

    if ((value & 0x40) == 0)
        voice.volume_control &= ~0x80;

    m_state.voice_irq = (u8)((value & 0x1F) | 0xE0);
    m_state.voice_irq_latched = false;
    UpdateIRQ();
    return value;
}

void UltraSound::ClockTimers(u32 clocks)
{
    for (int i = 0; i < 2; i++)
    {
        if ((m_state.adlib_control & (1 << i)) == 0)
            continue;

        u64 period = (u64)k_ultrasound_timer_clocks[i] * GDOX_MACHINE_CLOCK_RATE;
        m_state.timer_phase[i] += (u64)clocks * k_ultrasound_clock_rate;

        if (m_state.timer_phase[i] < period)
            continue;

        u64 ticks = m_state.timer_phase[i] / period;
        m_state.timer_phase[i] -= ticks * period;

        while (ticks > 0)
        {
            u32 to_overflow = 0x100 - m_state.timer_count[i];

            if (ticks < to_overflow)
            {
                m_state.timer_count[i] = (u8)(m_state.timer_count[i] + ticks);
                break;
            }

            ticks -= to_overflow;
            TickTimer(i);
        }
    }
}

void UltraSound::TickTimer(int timer)
{
    m_state.timer_count[timer] = m_state.timer_preload[timer];

    if ((m_state.adlib_control & (timer == 0 ? 0x40 : 0x20)) == 0)
        m_state.adlib_status |= (u8)((timer == 0 ? 0x40 : 0x20) | 0x80);

    if ((m_state.timer_control & (timer == 0 ? 0x04 : 0x08)) != 0)
    {
        m_state.timer_irq[timer] = true;
        UpdateIRQ();
    }
}

void UltraSound::ClockSampling(u32 clocks)
{
    if ((m_state.sample_control & 0x01) == 0)
        return;

    u64 period = (u64)k_ultrasound_voice_clocks * (m_state.sample_rate + 2) * GDOX_MACHINE_CLOCK_RATE;

    if ((m_state.sample_control & 0x04) != 0)
        period *= 2;

    m_state.sample_phase += (u64)clocks * k_ultrasound_clock_rate;

    if (m_state.sample_phase < period)
        return;

    u64 units = m_state.sample_phase / period;
    m_state.sample_phase -= units * period;
    m_state.sample_units = (u8)MIN((u64)k_ultrasound_sample_units, m_state.sample_units + units);
    UpdateDMARequest();
}

void UltraSound::ClockMIDI(u32 clocks)
{
    if (m_state.midi_tx_clocks == 0)
        return;

    if (clocks < m_state.midi_tx_clocks)
    {
        m_state.midi_tx_clocks -= clocks;
        return;
    }

    m_state.midi_tx_clocks = 0;
    UpdateIRQ();
}

u8 UltraSound::Read(u16 port)
{
    if ((port & 0x03FE) == 0x0388)
        port = (u16)(k_ultrasound_port + 0x08 + (port & 0x01));

    u8 offset = port & 0x0F;

    if ((port & 0x03F0) == k_ultrasound_port)
    {
        switch (offset)
        {
            case 0x06:
                return GetIRQStatus();
            case 0x08:
                return (u8)(m_state.adlib_status | ((m_state.adlib_status & 0x60) != 0 ? 0x80 : 0x00));
            case 0x09:
                return m_state.adlib_data;
            case 0x0A:
                return m_state.adlib_select;
            case 0x0C:
                return m_state.sb_2xc;
            case 0x0E:
                return m_state.sb_2xe;
            default:
                return 0xFF;
        }
    }

    switch (offset & 0x07)
    {
        case 0x00:
            return ReadMIDIStatus();
        case 0x01:
        {
            u8 value = m_state.midi_rx;
            m_state.midi_rx_full = false;
            UpdateIRQ();
            return value;
        }
        case 0x02:
            return m_state.voice_select;
        case 0x03:
            return m_state.register_select;
        case 0x04:
            return ReadRegister(false);
        case 0x05:
            return ReadRegister(true);
        case 0x07:
            return m_state.dram[m_state.dram_address & (ULTRASOUND_DRAM_SIZE - 1)];
        default:
            return 0xFF;
    }
}

u8 UltraSound::Peek(u16 port) const
{
    if ((port & 0x03FE) == 0x0388)
        port = (u16)(k_ultrasound_port + 0x08 + (port & 0x01));

    u8 offset = port & 0x0F;

    if ((port & 0x03F0) == k_ultrasound_port)
    {
        switch (offset)
        {
            case 0x06:
                return GetIRQStatus();
            case 0x08:
                return (u8)(m_state.adlib_status | ((m_state.adlib_status & 0x60) != 0 ? 0x80 : 0x00));
            case 0x09:
                return m_state.adlib_data;
            case 0x0A:
                return m_state.adlib_select;
            case 0x0C:
                return m_state.sb_2xc;
            case 0x0E:
                return m_state.sb_2xe;
            default:
                return 0xFF;
        }
    }

    switch (offset & 0x07)
    {
        case 0x00:
            return ReadMIDIStatus();
        case 0x01:
            return m_state.midi_rx;
        case 0x02:
            return m_state.voice_select;
        case 0x03:
            return m_state.register_select;
        case 0x04:
            return (u8)PeekRegister(m_state.voice_select, m_state.register_select);
        case 0x05:
            return (u8)(PeekRegister(m_state.voice_select, m_state.register_select) >> 8);
        case 0x07:
            return m_state.dram[m_state.dram_address & (ULTRASOUND_DRAM_SIZE - 1)];
        default:
            return 0xFF;
    }
}

void UltraSound::SnoopAdLib(u8 value)
{
    m_state.adlib_select = value;
}

void UltraSound::Write(u16 port, u8 value)
{
    if ((port & 0x03FE) == 0x0388)
        port = (u16)(k_ultrasound_port + 0x08 + (port & 0x01));

    u8 offset = port & 0x0F;
    bool armed = m_state.latch_armed;

    m_state.latch_armed = false;

    if ((port & 0x03F0) == k_ultrasound_port)
    {
        switch (offset)
        {
            case 0x00:
                m_state.mix_control = value;
                m_state.latch_armed = true;
                UpdateIRQ();
                UpdateDMARequest();
                break;
            case 0x08:
                m_state.adlib_select = value;
                break;
            case 0x09:
                if (m_state.adlib_select == 0x04)
                    WriteAdLib(value);
                else
                    m_state.adlib_data = value;
                break;
            case 0x0C:
                m_state.sb_2xc = value;
                break;
            case 0x0E:
                m_state.sb_2xe = value;
                break;
            case 0x0B:
                if (m_state.register_bank == 0)
                {
                    if (armed)
                        WriteLatch(value);
                }
                else if (m_state.register_bank == 6)
                    m_state.jumper = value;
                break;
            case 0x0F:
                m_state.register_bank = value & 0x07;
                break;
            default:
                break;
        }

        return;
    }

    switch (offset & 0x07)
    {
        case 0x00:
            WriteMIDIControl(value);
            break;
        case 0x01:
            WriteMIDIData(value);
            break;
        case 0x02:
            m_state.voice_select = value & 0x1F;
            break;
        case 0x03:
            m_state.register_select = value;
            break;
        case 0x04:
            m_state.data_low = value;
            break;
        case 0x05:
            if (IsWordRegister(m_state.register_select))
                WriteRegister(m_state.register_select, (u16)((value << 8) | m_state.data_low), true);
            else
                WriteRegister(m_state.register_select, value, false);
            break;
        case 0x07:
        {
            u32 address = m_state.dram_address & (ULTRASOUND_DRAM_SIZE - 1);
            m_state.dram[address] = value;
            MarkDirty(address);
            break;
        }
        default:
            break;
    }
}

bool UltraSound::IsWordRegister(u8 index) const
{
    switch (index)
    {
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x05:
        case 0x09:
        case 0x0A:
        case 0x0B:
        case 0x42:
        case 0x43:
            return true;
        default:
            return false;
    }
}

u8 UltraSound::ReadRegister(bool high)
{
    u8 index = m_state.register_select;

    if (high)
    {
        switch (index)
        {
            case 0x8F:
                return ReadVoiceIRQ();
            case 0x41:
            case 0xC1:
            {
                u8 value = (u8)((m_state.dma_control & ~0x40) | (m_state.dma_tc ? 0x40 : 0x00));
                m_state.dma_tc = false;
                UpdateIRQ();
                return value;
            }
            case 0x49:
            case 0xC9:
            {
                u8 value = (u8)((m_state.sample_control & ~0x40) | (m_state.sample_tc ? 0x40 : 0x00));
                m_state.sample_tc = false;
                UpdateIRQ();
                return value;
            }
            default:
                break;
        }
    }

    u16 value = PeekRegister(m_state.voice_select, index);
    return high ? (u8)(value >> 8) : (u8)value;
}

u16 UltraSound::PeekRegister(int voice, u8 index) const
{
    const UltraSound_Voice& v = m_state.voices[voice & (ULTRASOUND_VOICES - 1)];

    if ((index >= 0x80) && (index <= 0x8D))
        return GetRegister(voice, index & 0x7F);

    switch (index)
    {
        case 0x8E:
            return (u16)(m_state.active_voices << 8);
        case 0x8F:
            return (u16)(((m_state.voice_irq_latched ? m_state.voice_irq : (m_state.voice_irq | 0xE0))) << 8);
        default:
            break;
    }

    UNUSED(v);

    switch (index & 0x7F)
    {
        case 0x41:
            return (u16)(((m_state.dma_control & ~0x40) | (m_state.dma_tc ? 0x40 : 0x00)) << 8);
        case 0x42:
            return m_state.dma_address;
        case 0x43:
            return (u16)m_state.dram_address;
        case 0x44:
            return (u16)(((m_state.dram_address >> 16) & 0x0F) << 8);
        case 0x45:
            return (u16)(m_state.timer_control << 8);
        case 0x46:
            return (u16)(m_state.timer_preload[0] << 8);
        case 0x47:
            return (u16)(m_state.timer_preload[1] << 8);
        case 0x48:
            return (u16)(m_state.sample_rate << 8);
        case 0x49:
            return (u16)(((m_state.sample_control & ~0x40) | (m_state.sample_tc ? 0x40 : 0x00)) << 8);
        case 0x4B:
            return (u16)(m_state.joystick_trim << 8);
        case 0x4C:
            return (u16)(m_state.reset << 8);
        default:
            return 0;
    }
}

u16 UltraSound::GetRegister(int voice, u8 index) const
{
    const UltraSound_Voice& v = m_state.voices[voice & (ULTRASOUND_VOICES - 1)];

    switch (index)
    {
        case 0x00:
            return (u16)(v.control << 8);
        case 0x01:
            return v.frequency;
        case 0x02:
            return (u16)((v.start >> 16) & 0x1FFF);
        case 0x03:
            return (u16)(v.start & 0xFFE0);
        case 0x04:
            return (u16)((v.end >> 16) & 0x1FFF);
        case 0x05:
            return (u16)(v.end & 0xFFE0);
        case 0x06:
            return (u16)(v.ramp_rate << 8);
        case 0x07:
            return (u16)(v.ramp_start << 8);
        case 0x08:
            return (u16)(v.ramp_end << 8);
        case 0x09:
            return v.volume;
        case 0x0A:
            return (u16)((v.address >> 16) & 0x1FFF);
        case 0x0B:
            return (u16)(v.address & 0xFFFF);
        case 0x0C:
            return (u16)(v.pan << 8);
        case 0x0D:
            return (u16)(v.volume_control << 8);
        default:
            return 0;
    }
}

void UltraSound::WriteRegister(u8 index, u16 value, bool word)
{
    UNUSED(word);

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_GUS, TRACE_GUS_REGISTER)))
        TraceRegister(index, value);

    if (index >= 0x41)
    {
        WriteGlobal(index, value);
        return;
    }

    if (index == 0x0E)
    {
        m_state.active_voices = (u8)(value | 0xC0);
        m_state.register_select = (u8)value;
        return;
    }

    if ((index > 0x0D) || ((m_state.reset & 0x01) == 0))
        return;

    WriteVoice(m_state.voices[m_state.voice_select & (ULTRASOUND_VOICES - 1)], index, value);
}

void UltraSound::WriteVoice(UltraSound_Voice& voice, u8 index, u16 value)
{
    voice.address_pending = (index == 0x0A);

    switch (index)
    {
        case 0x00:
            voice.control = (u8)value;
            break;
        case 0x01:
            voice.frequency = value & 0xFFFE;
            break;
        case 0x02:
            voice.start = (voice.start & 0xFFFF) | ((u32)(value & 0x1FFF) << 16);
            break;
        case 0x03:
            voice.start = (voice.start & 0x1FFF0000) | (value & 0xFFE0);
            break;
        case 0x04:
        {
            u32 previous = voice.end;
            voice.end = (voice.end & 0xFFFF) | ((u32)(value & 0x1FFF) << 16);
            HoldEnd(voice, previous);
            break;
        }
        case 0x05:
        {
            u32 previous = voice.end;
            voice.end = (voice.end & 0x1FFF0000) | (value & 0xFFE0);
            HoldEnd(voice, previous);
            break;
        }
        case 0x06:
            voice.ramp_rate = (u8)value;
            break;
        case 0x07:
            voice.ramp_start = (u8)value;
            break;
        case 0x08:
            voice.ramp_end = (u8)value;
            break;
        case 0x09:
            voice.volume = value & 0xFFFE;
            break;
        case 0x0A:
            voice.address = (voice.address & 0xFFFF) | ((u32)(value & 0x1FFF) << 16);
            break;
        case 0x0B:
            voice.address = (voice.address & 0x1FFF0000) | value;
            break;
        case 0x0C:
            voice.pan = value & 0x0F;
            break;
        case 0x0D:
            voice.volume_control = (u8)value;
            break;
        default:
            break;
    }
}

void UltraSound::WriteGlobal(u8 index, u16 value)
{
    switch (index)
    {
        case 0x41:
            WriteDMAControl((u8)value);
            break;
        case 0x42:
            m_state.dma_address = value;
            break;
        case 0x43:
            m_state.dram_address = (m_state.dram_address & 0xF0000) | value;
            break;
        case 0x44:
            m_state.dram_address = (m_state.dram_address & 0xFFFF) | ((u32)(value & 0x0F) << 16);
            break;
        case 0x45:
            WriteTimerControl((u8)value);
            break;
        case 0x46:
            m_state.timer_preload[0] = (u8)value;
            break;
        case 0x47:
            m_state.timer_preload[1] = (u8)value;
            break;
        case 0x48:
            m_state.sample_rate = (u8)value;
            break;
        case 0x49:
            WriteSampleControl((u8)value);
            break;
        case 0x4B:
            m_state.joystick_trim = (u8)value;
            break;
        case 0x4C:
        {
            u8 previous = m_state.reset;

            if ((value & 0x01) == 0)
                ResetGF1();
            else
            {
                m_state.reset = value & 0x07;

                if ((previous & 0x01) == 0)
                    m_state.phase = 0;
            }

            UpdateIRQ();
            UpdateDMARequest();
            break;
        }
        default:
            break;
    }
}

void UltraSound::WriteDMAControl(u8 value)
{
    bool start = ((value & 0x01) != 0) && ((m_state.dma_control & 0x01) == 0);

    m_state.dma_control = value;

    if (start)
    {
        m_state.dma_pointer = GetDMAAddress((u32)m_state.dma_address << 4);

        if (unlikely(m_trace_logger->IsEventEnabled(TRACE_GUS, TRACE_GUS_DMA)))
            TraceEvent(TRACE_GUS_DMA, 0, value);
    }

    UpdateDMARequest();
}

void UltraSound::WriteSampleControl(u8 value)
{
    bool start = ((value & 0x01) != 0) && ((m_state.sample_control & 0x01) == 0);

    m_state.sample_control = value;

    if (start)
    {
        m_state.sample_phase = 0;
        m_state.sample_units = 0;
    }

    if ((value & 0x01) == 0)
        m_state.sample_units = 0;

    UpdateDMARequest();
}

void UltraSound::WriteTimerControl(u8 value)
{
    m_state.timer_control = value;

    if ((value & 0x04) == 0)
        m_state.timer_irq[0] = false;

    if ((value & 0x08) == 0)
        m_state.timer_irq[1] = false;

    UpdateIRQ();
}

void UltraSound::WriteAdLib(u8 value)
{
    if ((value & 0x80) != 0)
    {
        m_state.adlib_status = 0;
        return;
    }

    for (int i = 0; i < 2; i++)
    {
        u8 bit = (u8)(1 << i);

        if (((value & bit) != 0) && ((m_state.adlib_control & bit) == 0))
        {
            m_state.timer_count[i] = m_state.timer_preload[i];
            m_state.timer_phase[i] = 0;
        }
    }

    m_state.adlib_control = value & 0x63;
}

void UltraSound::WriteLatch(u8 value)
{
    if ((m_state.mix_control & 0x40) != 0)
        m_state.irq_latch = value & 0x7F;
    else
        m_state.dma_latch = value & 0x7F;

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_GUS, TRACE_GUS_LATCH)))
        TraceEvent(TRACE_GUS_LATCH, (m_state.mix_control & 0x40) != 0 ? 1 : 0, value);

    UpdateIRQ();
    UpdateDMARequest();
}

void UltraSound::WriteMIDIControl(u8 value)
{
    m_state.midi_control = value;

    if ((value & 0x03) == 0x03)
    {
        m_state.midi_rx_full = false;
        m_state.midi_tx_clocks = 0;
    }

    UpdateIRQ();
}

void UltraSound::WriteMIDIData(u8 value)
{
    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_MIDI, TRACE_MIDI_DATA)))
    {
        GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_MIDI, TRACE_MIDI_DATA);
        entry->midi.value = value;
        entry->midi.source = TRACE_MIDI_SOURCE_GUS;
    }

    if ((m_state.mix_control & 0x20) != 0)
    {
        m_state.midi_rx = value;
        m_state.midi_rx_full = true;
    }
    else if (IsValidPointer(m_midi_out))
        m_midi_out->Write(value, TRACE_MIDI_SOURCE_GUS);

    m_state.midi_tx_clocks = k_ultrasound_midi_clocks;
    UpdateIRQ();
}

u8 UltraSound::ReadMIDIStatus() const
{
    bool tx_empty = m_state.midi_tx_clocks == 0;
    bool tx_irq = tx_empty && ((m_state.midi_control & 0x60) == 0x20);
    bool rx_irq = m_state.midi_rx_full && ((m_state.midi_control & 0x80) != 0);

    return (u8)((m_state.midi_rx_full ? 0x01 : 0x00) | (tx_empty ? 0x02 : 0x00) | ((tx_irq || rx_irq) ? 0x80 : 0x00));
}

u8 UltraSound::GetIRQStatus() const
{
    u8 status = 0;

    if ((m_state.midi_tx_clocks == 0) && ((m_state.midi_control & 0x60) == 0x20))
        status |= 0x01;

    if (m_state.midi_rx_full && ((m_state.midi_control & 0x80) != 0))
        status |= 0x02;

    if (m_state.timer_irq[0])
        status |= 0x04;

    if (m_state.timer_irq[1])
        status |= 0x08;

    if (m_state.voice_irq_latched && ((m_state.reset & 0x04) != 0))
    {
        if ((m_state.voice_irq & 0x80) == 0)
            status |= 0x20;

        if ((m_state.voice_irq & 0x40) == 0)
            status |= 0x40;
    }

    if ((m_state.dma_tc && ((m_state.dma_control & 0x20) != 0)) ||
        (m_state.sample_tc && ((m_state.sample_control & 0x20) != 0)))
        status |= 0x80;

    return status;
}

int UltraSound::GetIRQ(int channel) const
{
    u8 code = channel == 0 ? (m_state.irq_latch & 0x07) : ((m_state.irq_latch >> 3) & 0x07);
    return k_ultrasound_irqs[code];
}

int UltraSound::GetDRAMChannel() const
{
    if (!m_enabled || ((m_state.mix_control & 0x08) == 0))
        return -1;

    return k_ultrasound_dmas[m_state.dma_latch & 0x07];
}

int UltraSound::GetADCChannel() const
{
    if (!m_enabled || ((m_state.mix_control & 0x08) == 0))
        return -1;

    if ((m_state.dma_latch & 0x40) != 0)
        return k_ultrasound_dmas[m_state.dma_latch & 0x07];

    return k_ultrasound_dmas[(m_state.dma_latch >> 3) & 0x07];
}

bool UltraSound::IsWordChannel(int channel) const
{
    return channel >= 4;
}

void UltraSound::UpdateIRQ()
{
    int lines[2] = { 0, 0 };

    if (m_enabled && ((m_state.mix_control & 0x08) != 0))
    {
        u8 status = GetIRQStatus();
        bool gf1 = (status & 0xFC) != 0;
        bool midi = (status & 0x03) != 0;
        bool combine = (m_state.irq_latch & 0x40) != 0;
        int gf1_irq = GetIRQ(0);
        int midi_irq = GetIRQ(1);

        if ((gf1_irq != 0) && (gf1 || (combine && midi)))
            lines[0] = gf1_irq;

        if (!combine && (midi_irq != 0) && midi)
            lines[1] = midi_irq;
    }

    if (!IsValidPointer(m_pic))
        return;

    for (int i = 0; i < 2; i++)
    {
        int old = m_asserted_irq[i];

        if ((old != 0) && (old != lines[0]) && (old != lines[1]))
            m_pic->SetIRQLine(old, false);
    }

    for (int i = 0; i < 2; i++)
    {
        if ((lines[i] != 0) && (lines[i] != m_asserted_irq[0]) && (lines[i] != m_asserted_irq[1]))
            m_pic->SetIRQLine(lines[i], true);
    }

    m_asserted_irq[0] = lines[0];
    m_asserted_irq[1] = lines[1];
}

void UltraSound::UpdateDMARequest()
{
    int dram = GetDRAMChannel();
    int adc = GetADCChannel();
    bool dram_active = (dram >= 0) && ((m_state.dma_control & 0x01) != 0);
    bool adc_active = (adc >= 0) && ((m_state.sample_control & 0x01) != 0) && (m_state.sample_units != 0);
    int dram_request = dram_active ? dram : -1;
    int adc_request = adc_active ? adc : -1;

    if (!IsValidPointer(m_dma))
        return;

    if ((m_dram_request >= 0) && (m_dram_request != dram_request) && (m_dram_request != adc_request))
        m_dma->SetRequest(m_dram_request, false);

    if ((m_adc_request >= 0) && (m_adc_request != dram_request) && (m_adc_request != adc_request))
        m_dma->SetRequest(m_adc_request, false);

    if ((dram_request >= 0) && (dram_request != m_dram_request) && (dram_request != m_adc_request))
        m_dma->SetRequest(dram_request, true);

    if ((adc_request >= 0) && (adc_request != m_adc_request) && (adc_request != m_dram_request) &&
        (adc_request != dram_request))
        m_dma->SetRequest(adc_request, true);

    m_dram_request = dram_request;
    m_adc_request = adc_request;
}

u32 UltraSound::GetDMAAddress(u32 pointer) const
{
    if (((m_state.dma_control & 0x04) != 0) && IsWordChannel(GetDRAMChannel()))
        return (pointer & 0xC0000) | ((pointer & 0x1FFFF) << 1);

    return pointer & (ULTRASOUND_DRAM_SIZE - 1);
}

void UltraSound::AdvanceDMA(bool word)
{
    if (word)
        m_state.dma_pointer = (m_state.dma_pointer & 0xC0000) | ((m_state.dma_pointer + 2) & 0x3FFFF);
    else
        m_state.dma_pointer = (m_state.dma_pointer + 1) & (ULTRASOUND_DRAM_SIZE - 1);
}

u8 UltraSound::InvertDMA(u8 value, u32 pointer) const
{
    if ((m_state.dma_control & 0x80) == 0)
        return value;

    if (((m_state.dma_control & 0x40) != 0) && ((pointer & 0x01) == 0))
        return value;

    return value ^ 0x80;
}

bool UltraSound::DRAMWriteCallback(void* device, u16 value, bool word)
{
    UltraSound* gus = (UltraSound*)device;
    UltraSound_State& state = gus->m_state;

    if (((state.dma_control & 0x01) == 0) || ((state.dma_control & 0x02) != 0))
        return true;

    u32 pointer = state.dma_pointer;
    state.dram[pointer] = gus->InvertDMA((u8)value, pointer);
    gus->MarkDirty(pointer);

    if (word)
    {
        state.dram[pointer + 1] = gus->InvertDMA((u8)(value >> 8), pointer + 1);
        gus->MarkDirty(pointer + 1);
    }

    gus->AdvanceDMA(word);
    return true;
}

bool UltraSound::DRAMReadCallback(void* device, u16& value, bool word)
{
    UltraSound* gus = (UltraSound*)device;
    UltraSound_State& state = gus->m_state;

    if (((state.dma_control & 0x01) == 0) && ((state.sample_control & 0x01) != 0) &&
        (gus->GetADCChannel() == gus->GetDRAMChannel()))
        return ADCReadCallback(device, value, word);

    if (((state.dma_control & 0x03) != 0x03))
        return true;

    u32 pointer = state.dma_pointer;
    value = gus->InvertDMA(state.dram[pointer], pointer);

    if (word)
        value |= (u16)(gus->InvertDMA(state.dram[pointer + 1], pointer + 1) << 8);

    gus->AdvanceDMA(word);
    return true;
}

void UltraSound::DRAMEndCallback(void* device)
{
    UltraSound* gus = (UltraSound*)device;
    UltraSound_State& state = gus->m_state;

    if (((state.dma_control & 0x01) == 0) && ((state.sample_control & 0x01) != 0) &&
        (gus->GetADCChannel() == gus->GetDRAMChannel()))
    {
        ADCEndCallback(device);
        return;
    }

    if ((state.dma_control & 0x01) == 0)
        return;

    state.dma_control &= ~0x01;
    state.dma_tc = true;

    if (unlikely(gus->m_trace_logger->IsEventEnabled(TRACE_GUS, TRACE_GUS_DMA)))
        gus->TraceEvent(TRACE_GUS_DMA, 1, state.dma_control);

    gus->UpdateDMARequest();
    gus->UpdateIRQ();
}

bool UltraSound::ADCReadCallback(void* device, u16& value, bool word)
{
    UltraSound* gus = (UltraSound*)device;
    UltraSound_State& state = gus->m_state;

    if (((state.sample_control & 0x01) == 0) || (state.sample_units == 0))
        return true;

    u8 silence = (state.sample_control & 0x80) != 0 ? 0x00 : 0x80;
    value = word ? (u16)((silence << 8) | silence) : silence;
    state.sample_units--;

    if (state.sample_units == 0)
        gus->UpdateDMARequest();

    return true;
}

bool UltraSound::ADCWriteCallback(void* device, u16 value, bool word)
{
    UNUSED(device);
    UNUSED(value);
    UNUSED(word);
    return true;
}

void UltraSound::ADCEndCallback(void* device)
{
    UltraSound* gus = (UltraSound*)device;
    UltraSound_State& state = gus->m_state;

    if ((state.sample_control & 0x01) == 0)
        return;

    state.sample_control &= ~0x01;
    state.sample_units = 0;
    state.sample_tc = true;
    gus->UpdateDMARequest();
    gus->UpdateIRQ();
}

u64 UltraSound::GetVoiceFrames(const UltraSound_Voice& voice) const
{
    if (((voice.control & 0x03) != 0) || ((voice.control & 0x20) == 0))
        return GDOX_NO_EVENT;

    u64 step = voice.frequency >> 1;

    if (step == 0)
        return GDOX_NO_EVENT;

    if ((voice.control & 0x40) == 0)
    {
        if ((u64)voice.address + step > voice.end)
            return 1;

        return ((voice.end - voice.address) / step) + 1;
    }

    if ((u64)voice.address < (u64)voice.start + step)
        return 1;

    return ((voice.address - voice.start) / step) + 1;
}

u64 UltraSound::GetRampFrames(const UltraSound_Voice& voice) const
{
    if (((voice.volume_control & 0x03) != 0) || ((voice.volume_control & 0x20) == 0))
        return GDOX_NO_EVENT;

    u8 rate = voice.ramp_rate >> 6;
    u64 increment = voice.ramp_rate & 0x3F;
    u64 delta = rate == 0 ? increment << 3 : increment;

    if (delta == 0)
        return GDOX_NO_EVENT;

    u64 level = voice.volume >> 1;
    u64 low = (u64)voice.ramp_start << 7;
    u64 high = (u64)voice.ramp_end << 7;
    u64 updates;

    if ((voice.volume_control & 0x40) == 0)
        updates = level + delta > high ? 1 : ((high - level) / delta) + 1;
    else
        updates = level < low + delta ? 1 : ((level - low) / delta) + 1;

    if (rate < 2)
        return updates;

    u64 every = rate == 2 ? 8 : 64;
    return (every - voice.ramp_count) + ((updates - 1) * every);
}

u64 UltraSound::FramesToClocks(u64 frames) const
{
    u64 target = frames * GetFramePeriod();

    if (target <= m_state.phase)
        return 1;

    return (target - m_state.phase + k_ultrasound_clock_rate - 1) / k_ultrasound_clock_rate;
}

u64 UltraSound::GetTimerClocks(int timer) const
{
    u64 period = (u64)k_ultrasound_timer_clocks[timer] * GDOX_MACHINE_CLOCK_RATE;
    u64 ticks = 0x100 - m_state.timer_count[timer];
    u64 target = ticks * period;

    if (target <= m_state.timer_phase[timer])
        return 1;

    return (target - m_state.timer_phase[timer] + k_ultrasound_clock_rate - 1) / k_ultrasound_clock_rate;
}

u64 UltraSound::GetClocksToEvent() const
{
    if (!m_enabled)
        return GDOX_NO_EVENT;

    u64 clocks = GDOX_NO_EVENT;

    if (IsRunning() && ((m_state.reset & 0x04) != 0) && !m_state.voice_irq_latched)
    {
        u64 frames = GDOX_NO_EVENT;
        int voices = GetActiveVoices();

        for (int i = 0; (i < voices) && (frames > 1); i++)
        {
            const UltraSound_Voice& voice = m_state.voices[i];

            if (((voice.control | voice.volume_control) & 0x80) != 0)
            {
                frames = 1;
                break;
            }

            frames = MIN(frames, GetVoiceFrames(voice));
            frames = MIN(frames, GetRampFrames(voice));
        }

        if (frames != GDOX_NO_EVENT)
            clocks = FramesToClocks(frames);
    }

    for (int i = 0; i < 2; i++)
    {
        if ((m_state.adlib_control & (1 << i)) == 0)
            continue;

        u8 flag = i == 0 ? 0x40 : 0x20;
        bool flag_pending = ((m_state.adlib_control & flag) == 0) && ((m_state.adlib_status & flag) == 0);
        bool irq_pending = !m_state.timer_irq[i] && ((m_state.timer_control & (i == 0 ? 0x04 : 0x08)) != 0);

        if (flag_pending || irq_pending)
            clocks = MIN(clocks, GetTimerClocks(i));
    }

    if (m_state.midi_tx_clocks != 0)
        clocks = MIN(clocks, (u64)m_state.midi_tx_clocks);


    if (((m_state.sample_control & 0x01) != 0) && (m_state.sample_units == 0))
    {
        u64 period = (u64)k_ultrasound_voice_clocks * (m_state.sample_rate + 2) * GDOX_MACHINE_CLOCK_RATE;

        if ((m_state.sample_control & 0x04) != 0)
            period *= 2;

        u64 remaining = period > m_state.sample_phase ? period - m_state.sample_phase : 1;
        clocks = MIN(clocks, (remaining + k_ultrasound_clock_rate - 1) / k_ultrasound_clock_rate);
    }

    return clocks;
}

u64 UltraSound::GetReadStableUntil(u16 port, u64 clocks) const
{
    u64 event = GetClocksToEvent();
    u64 next = event == GDOX_NO_EVENT ? GDOX_NO_EVENT : clocks + event;

    if ((port & 0x03F0) == k_ultrasound_port)
    {
        switch (port & 0x0F)
        {
            case 0x06:
            case 0x08:
                return next;
            default:
                return GDOX_NO_EVENT;
        }
    }

    switch (port & 0x07)
    {
        case 0x00:
            return next;
        case 0x02:
        case 0x03:
            return GDOX_NO_EVENT;
        case 0x04:
        case 0x05:
        {
            u8 index = m_state.register_select;

            if ((index == 0x8F) || (index == 0x80) || (index == 0x89) || (index == 0x8A) || (index == 0x8B) ||
                (index == 0x8D) || (index == 0x41) || (index == 0x49) || (index == 0xC1) || (index == 0xC9))
                return IsRunning() || ((m_state.dma_control & 0x01) != 0) ? 0 : next;

            return GDOX_NO_EVENT;
        }
        case 0x07:
            return ((m_state.dma_control & 0x01) != 0) ? 0 : GDOX_NO_EVENT;
        default:
            return 0;
    }
}

void UltraSound::PokeDRAM(u32 address, u8 value)
{
    address &= ULTRASOUND_DRAM_SIZE - 1;
    m_state.dram[address] = value;
    MarkDirty(address);
}

void UltraSound::SetDirtyTracking(bool enabled)
{
    if (enabled && !m_dirty_tracking)
    {
        for (int i = 0; i < ULTRASOUND_PAGES; i++)
            m_dirty_pages[i] = 1;
    }

    m_dirty_tracking = enabled;
}

void UltraSound::ClearDirtyPages()
{
    memset(m_dirty_pages, 0, sizeof(m_dirty_pages));
}

void UltraSound::TraceRegister(u8 index, u16 value)
{
    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_GUS, TRACE_GUS_REGISTER);
    entry->gus.index = index;
    entry->gus.voice = m_state.voice_select;
    entry->gus.value = value;
}

void UltraSound::TraceEvent(u8 event, u8 voice, u16 value)
{
    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_GUS, event);
    entry->gus.voice = voice;
    entry->gus.value = value;
}

void UltraSound::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
}

void UltraSound::LoadState(std::istream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
    SanitizeState();
}

void UltraSound::Serialize(StateSerializer& serializer)
{
    for (int i = 0; i < ULTRASOUND_VOICES; i++)
    {
        UltraSound_Voice& voice = m_state.voices[i];
        G_SERIALIZE(serializer, voice.start);
        G_SERIALIZE(serializer, voice.end);
        G_SERIALIZE(serializer, voice.address);
        G_SERIALIZE(serializer, voice.frequency);
        G_SERIALIZE(serializer, voice.volume);
        G_SERIALIZE(serializer, voice.control);
        G_SERIALIZE(serializer, voice.volume_control);
        G_SERIALIZE(serializer, voice.ramp_rate);
        G_SERIALIZE(serializer, voice.ramp_start);
        G_SERIALIZE(serializer, voice.ramp_end);
        G_SERIALIZE(serializer, voice.pan);
        G_SERIALIZE(serializer, voice.ramp_count);
        G_SERIALIZE(serializer, voice.fetch_key);
        G_SERIALIZE(serializer, voice.fetch_first);
        G_SERIALIZE(serializer, voice.fetch_second);
        G_SERIALIZE(serializer, voice.address_pending);
        G_SERIALIZE(serializer, voice.held_end);
        G_SERIALIZE(serializer, voice.end_held);
    }

    G_SERIALIZE(serializer, m_state.active_voices);
    G_SERIALIZE(serializer, m_state.voice_select);
    G_SERIALIZE(serializer, m_state.register_select);
    G_SERIALIZE(serializer, m_state.data_low);
    G_SERIALIZE(serializer, m_state.reset);
    G_SERIALIZE(serializer, m_state.dma_control);
    G_SERIALIZE(serializer, m_state.dma_address);
    G_SERIALIZE(serializer, m_state.dma_pointer);
    G_SERIALIZE(serializer, m_state.dram_address);
    G_SERIALIZE(serializer, m_state.timer_control);
    G_SERIALIZE_ARRAY(serializer, m_state.timer_count, 2);
    G_SERIALIZE_ARRAY(serializer, m_state.timer_preload, 2);
    G_SERIALIZE_ARRAY(serializer, m_state.timer_phase, 2);
    G_SERIALIZE(serializer, m_state.adlib_select);
    G_SERIALIZE(serializer, m_state.adlib_control);
    G_SERIALIZE(serializer, m_state.adlib_status);
    G_SERIALIZE(serializer, m_state.adlib_data);
    G_SERIALIZE(serializer, m_state.sb_2xc);
    G_SERIALIZE(serializer, m_state.sb_2xe);
    G_SERIALIZE(serializer, m_state.sample_control);
    G_SERIALIZE(serializer, m_state.sample_rate);
    G_SERIALIZE(serializer, m_state.sample_phase);
    G_SERIALIZE(serializer, m_state.sample_units);
    G_SERIALIZE(serializer, m_state.joystick_trim);
    G_SERIALIZE_ARRAY(serializer, m_state.timer_irq, 2);
    G_SERIALIZE(serializer, m_state.dma_tc);
    G_SERIALIZE(serializer, m_state.sample_tc);
    G_SERIALIZE(serializer, m_state.voice_irq);
    G_SERIALIZE(serializer, m_state.voice_irq_latched);
    G_SERIALIZE(serializer, m_state.mix_control);
    G_SERIALIZE(serializer, m_state.irq_latch);
    G_SERIALIZE(serializer, m_state.dma_latch);
    G_SERIALIZE(serializer, m_state.latch_armed);
    G_SERIALIZE(serializer, m_state.register_bank);
    G_SERIALIZE(serializer, m_state.jumper);
    G_SERIALIZE(serializer, m_state.midi_control);
    G_SERIALIZE(serializer, m_state.midi_rx);
    G_SERIALIZE(serializer, m_state.midi_rx_full);
    G_SERIALIZE(serializer, m_state.midi_tx_clocks);
    G_SERIALIZE(serializer, m_state.phase);
    G_SERIALIZE(serializer, m_state.left_sample);
    G_SERIALIZE(serializer, m_state.right_sample);
    G_SERIALIZE(serializer, m_state.previous_left_sample);
    G_SERIALIZE(serializer, m_state.previous_right_sample);

    if (m_enabled)
        G_SERIALIZE_ARRAY(serializer, m_state.dram, ULTRASOUND_DRAM_SIZE);
}

void UltraSound::SanitizeState()
{
    for (int i = 0; i < ULTRASOUND_VOICES; i++)
    {
        UltraSound_Voice& voice = m_state.voices[i];
        voice.start &= k_ultrasound_loop_mask;
        voice.end &= k_ultrasound_loop_mask;
        voice.address &= k_ultrasound_address_mask;
        voice.frequency &= 0xFFFE;
        voice.volume &= 0xFFFE;
        voice.pan &= 0x0F;
        voice.ramp_count &= 0x3F;
    }

    m_state.voice_select &= ULTRASOUND_VOICES - 1;
    m_state.dma_pointer &= ULTRASOUND_DRAM_SIZE - 1;
    m_state.dram_address &= ULTRASOUND_DRAM_SIZE - 1;
    m_state.register_bank &= 0x07;
    m_state.reset &= 0x07;
    m_state.sample_units = MIN(m_state.sample_units, k_ultrasound_sample_units);
    m_state.phase %= GetFramePeriod();

    for (int i = 0; i < 2; i++)
        m_state.timer_phase[i] %= (u64)k_ultrasound_timer_clocks[i] * GDOX_MACHINE_CLOCK_RATE;

    for (int i = 0; i < ULTRASOUND_PAGES; i++)
        m_dirty_pages[i] = 1;

    m_asserted_irq[0] = 0;
    m_asserted_irq[1] = 0;
    m_dram_request = -1;
    m_adc_request = -1;
    UpdateIRQ();
    UpdateDMARequest();
}
