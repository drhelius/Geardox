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

#ifndef SOUND_BLASTER_INLINE_H
#define SOUND_BLASTER_INLINE_H

#include "sound_blaster.h"
#include "audio_common.h"

INLINE void SoundBlaster::Clock(u32 clocks)
{
    if (!IsRunning())
        return;

    u64 period = GetPeriod();

    m_state.phase += (u64)clocks * m_state.rate_numerator;

    while (m_state.phase >= period)
    {
        m_state.phase -= period;
        Tick();
        period = GetPeriod();
    }
}

// Blended by phase like the PCM chips, the speaker is the card's connection from the DAC to its amplifier
INLINE void SoundBlaster::Sample(s16& left, s16& right) const
{
    if (!IsSpeakerOn())
    {
        left = 0;
        right = 0;
        return;
    }

    s64 phase = (s64)m_state.phase;
    s64 period = (s64)GetPeriod();
    left = lerp_sample(m_state.previous_left_sample, m_state.left_sample, phase, period);
    right = lerp_sample(m_state.previous_right_sample, m_state.right_sample, phase, period);
}

INLINE bool SoundBlaster::HasDSP() const
{
    return m_version_major != 0;
}

INLINE bool SoundBlaster::HasMixer() const
{
    return m_mixer_type != k_sound_blaster_mixer_none;
}

INLINE bool SoundBlaster::IsRunning() const
{
    return m_state.transfer.active && !m_state.transfer.paused;
}

// From 4.xx on, the speaker commands only set the flag D8h reads
INLINE bool SoundBlaster::IsSpeakerOn() const
{
    return m_state.speaker || (m_version_major >= 4);
}

INLINE bool SoundBlaster::IsProStereo() const
{
    return (m_mixer_type == k_sound_blaster_mixer_ct1345) && ((m_state.mixer[0x0E] & k_sound_blaster_stereo_switch) != 0);
}

INLINE bool SoundBlaster::IsStereo() const
{
    return m_state.transfer.stereo || (IsProStereo() && (m_state.transfer.format == SOUND_BLASTER_FORMAT_PCM8));
}

// The time constant counts every byte, so a Pro in stereo makes a frame of two at half the rate
INLINE bool SoundBlaster::IsReadingAhead() const
{
    return m_state.ahead_read < m_state.ahead_size;
}

INLINE u64 SoundBlaster::GetPeriod() const
{
    u64 period = (u64)m_state.rate_denominator * GDOX_MACHINE_CLOCK_RATE;
    return (IsProStereo() && (m_state.transfer.format == SOUND_BLASTER_FORMAT_PCM8)) ? period * 2 : period;
}

INLINE u32 SoundBlaster::GetSampleRate() const
{
    u32 rate = m_state.rate_numerator / m_state.rate_denominator;
    return (IsProStereo() && (m_state.transfer.format == SOUND_BLASTER_FORMAT_PCM8)) ? rate / 2 : rate;
}

INLINE u8 SoundBlaster::GetVersionMajor() const
{
    return m_version_major;
}

INLINE u8 SoundBlaster::GetVersionMinor() const
{
    return m_version_minor;
}

INLINE u8 SoundBlaster::GetInterruptStatus() const
{
    return (u8)((m_state.irq_8 ? 0x01 : 0x00) | (m_state.irq_16 ? 0x02 : 0x00) | (m_state.irq_mpu ? 0x04 : 0x00));
}

INLINE s32 SoundBlaster::GetGain(int source, bool right) const
{
    return m_gain[source][right ? 1 : 0];
}

INLINE SoundBlaster::SoundBlaster_State* SoundBlaster::GetState()
{
    return &m_state;
}

#endif /* SOUND_BLASTER_INLINE_H */
