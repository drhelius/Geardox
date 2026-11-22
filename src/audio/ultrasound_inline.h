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

#ifndef ULTRASOUND_INLINE_H
#define ULTRASOUND_INLINE_H

#include "ultrasound.h"
#include "audio_common.h"

INLINE void UltraSound::Clock(u32 clocks)
{
    if (!m_enabled)
        return;

    if (((m_state.adlib_control & 0x03) != 0) || (m_state.midi_tx_clocks != 0) || ((m_state.sample_control & 0x01) != 0))
    {
        ClockTimers(clocks);
        ClockMIDI(clocks);
        ClockSampling(clocks);
    }

    if (!IsRunning())
        return;

    u64 period = GetFramePeriod();

    m_state.phase += (u64)clocks * k_ultrasound_clock_rate;

    while (m_state.phase >= period)
    {
        m_state.phase -= period;
        Frame();
        period = GetFramePeriod();
    }
}

INLINE void UltraSound::Sample(s16& left, s16& right) const
{
    if (!IsRunning() || !IsOutputEnabled())
    {
        left = 0;
        right = 0;
        return;
    }

    s64 phase = (s64)m_state.phase;
    s64 period = (s64)GetFramePeriod();
    left = lerp_sample(m_state.previous_left_sample, m_state.left_sample, phase, period);
    right = lerp_sample(m_state.previous_right_sample, m_state.right_sample, phase, period);
}

INLINE bool UltraSound::Decodes(u16 port) const
{
    if (!m_enabled)
        return false;

    port &= 0x03FF;

    if ((port & 0x03F0) == k_ultrasound_port)
    {
        switch (port & 0x0F)
        {
            case 0x00:
            case 0x06:
            case 0x08:
            case 0x09:
            case 0x0A:
            case 0x0B:
            case 0x0C:
            case 0x0E:
            case 0x0F:
                return true;
            default:
                return false;
        }
    }

    if ((port & 0x03F8) != (k_ultrasound_port + 0x100))
        return false;

    switch (port & 0x07)
    {
        case 0x00:
        case 0x01:
            return DecodesMIDI();
        case 0x06:
            return false;
        default:
            return true;
    }
}

INLINE bool UltraSound::DecodesMIDI() const
{
    return (m_state.jumper & 0x02) != 0;
}

INLINE bool UltraSound::IsRunning() const
{
    return m_enabled && ((m_state.reset & 0x01) != 0);
}

INLINE bool UltraSound::IsOutputEnabled() const
{
    return (m_state.mix_control & 0x02) == 0;
}

INLINE int UltraSound::GetActiveVoices() const
{
    int voices = (m_state.active_voices & 0x1F) + 1;
    return voices < k_ultrasound_min_voices ? k_ultrasound_min_voices : voices;
}

INLINE u64 UltraSound::GetFramePeriod() const
{
    return (u64)k_ultrasound_voice_clocks * (u64)GetActiveVoices() * GDOX_MACHINE_CLOCK_RATE;
}

INLINE u64 UltraSound::GetSamplePeriod() const
{
    u64 period = (u64)k_ultrasound_voice_clocks * (m_state.sample_rate + 2) * GDOX_MACHINE_CLOCK_RATE;
    return (m_state.sample_control & 0x04) != 0 ? period * 2 : period;
}

INLINE u32 UltraSound::GetFrameRate() const
{
    return k_ultrasound_clock_rate / (k_ultrasound_voice_clocks * (u32)GetActiveVoices());
}

INLINE s16 UltraSound::GetVoiceOutput(int voice) const
{
    return m_voice_output[voice & (ULTRASOUND_VOICES - 1)];
}

INLINE u8 UltraSound::PeekDRAM(u32 address) const
{
    return m_state.dram[address & (ULTRASOUND_DRAM_SIZE - 1)];
}

INLINE u8* UltraSound::GetDRAM()
{
    return m_state.dram;
}

INLINE const u8* UltraSound::GetDirtyPages() const
{
    return m_dirty_pages;
}

INLINE void UltraSound::MarkDirty(u32 address)
{
    if (m_dirty_tracking)
        m_dirty_pages[(address & (ULTRASOUND_DRAM_SIZE - 1)) >> ULTRASOUND_PAGE_SHIFT] = 1;
}

INLINE UltraSound::UltraSound_State* UltraSound::GetState()
{
    return &m_state;
}

#endif /* ULTRASOUND_INLINE_H */
