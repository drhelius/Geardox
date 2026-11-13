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

#ifndef AUDIO_INLINE_H
#define AUDIO_INLINE_H

#include "audio.h"
#include "opl.h"
#include "sound_blaster.h"
#include "mpu401.h"
#include "../cdrom/cdrom_audio.h"
#include "../system/pit.h"

static const s32 k_audio_buzzer_level = 4096;
static const u64 k_audio_buzzer_max_ticks = 64;

// The analog output is AC coupled
// One pole at 44.1 kHz / (2 * pi * 2^10), about 6.9 Hz
static const int k_audio_dc_shift = 10;
static const int k_audio_dc_fraction = 12;
static const s32 k_audio_dc_round = 1 << (k_audio_dc_fraction - 1);
static const s32 k_audio_dc_limit = 1 << 30;
static const u16 k_audio_fm_port = 0x0388;
static const s32 k_audio_fm_gain_q15 = 65536;

INLINE void Audio::Mute(bool mute)
{
    m_mute = mute;
}

INLINE void Audio::SetMasterVolume(float volume)
{
    m_master_volume = volume;
}

INLINE void Audio::SetFMVolume(float volume)
{
    m_fm_volume = volume;
}

INLINE void Audio::SetPCMVolume(float volume)
{
    m_pcm_volume = volume;
}

INLINE void Audio::SetCDDAVolume(float volume)
{
    m_cdda_volume = volume;
}

INLINE void Audio::SetBuzzerVolume(float volume)
{
    m_buzzer_volume = volume;
}

INLINE OPL* Audio::GetOPL(int chip)
{
    return m_opl[chip];
}

INLINE int Audio::GetOPLChips() const
{
    return m_opl_chips;
}

INLINE SoundBlaster* Audio::GetSoundBlaster()
{
    return m_sound_blaster;
}

INLINE MPU401* Audio::GetMPU401()
{
    return m_mpu401;
}

INLINE Audio::Audio_State* Audio::GetState()
{
    return &m_state;
}

INLINE void Audio::SetSourceMute(int source, bool mute)
{
    m_source_mute[source] = mute;
}

INLINE bool Audio::IsSourceMuted(int source) const
{
    return m_source_mute[source];
}

INLINE void Audio::EnableChannelScopes(bool enable)
{
    m_channel_scopes = enable;
}

// Stereo values in the last completed frame
INLINE int Audio::GetFrameSamples() const
{
    return m_frame_samples;
}

INLINE const s16* Audio::GetSourceBuffer(int source) const
{
    switch (source)
    {
        case AUDIO_SOURCE_FM:
            return m_fm_buffer;
        case AUDIO_SOURCE_PCM:
            return m_pcm_buffer;
        case AUDIO_SOURCE_CDDA:
            return m_cdda_buffer;
        default:
            return m_buzzer_buffer;
    }
}

INLINE const s16* Audio::GetFMChannelBuffer(int channel) const
{
    return m_fm_channel_buffer[channel];
}

INLINE void Audio::Synchronize(u64 clocks)
{
    while (clocks > m_state.clocks)
        Clock((u32)MIN(clocks - m_state.clocks, 0x10000000ULL));
}

// The speaker plays what it was playing up to this moment, ahead of a change to it
INLINE void Audio::SynchronizeBuzzer(u64 clocks)
{
    Synchronize(clocks);
    AccumulateBuzzer(m_pit->GetTick(clocks));
}

// The DSP reads frames ahead of the controller, which counts them as they play, so a look at the controller first plays
// the frames whose time has come
INLINE void Audio::SynchronizeDMA(u64 clocks)
{
    if (m_dsp && m_sound_blaster->IsReadingAhead())
        Synchronize(clocks);
}

INLINE void Audio::HandleEvent(u64 clocks)
{
    Synchronize(clocks);
    UpdateEvent();
}

INLINE bool Audio::IsFMPort(u16 port) const
{
    u16 offset = port & 0x0F;

    if ((port & 0x03FC) == k_audio_fm_port)
        return (m_opl_chips != 0) && (m_opl3 || ((port & 0x02) == 0));

    if ((port & 0x03F0) != k_sound_blaster_port)
        return false;

    return (m_dsp && ((offset == 0x08) || (offset == 0x09))) || (m_mixer && (offset <= 0x03));
}

INLINE bool Audio::IsMPUPort(u16 port) const
{
    return m_mpu && ((port & 0x03FE) == k_mpu401_port);
}

// The DSP's ports, the mixer's on the cards that have one, and 2xFh from 4.xx on
INLINE bool Audio::Decodes(u16 port) const
{
    if (IsFMPort(port) || IsMPUPort(port))
        return true;

    if ((port & 0x03F0) != k_sound_blaster_port)
        return false;

    switch (port & 0x0F)
    {
        case 0x04:
        case 0x05:
            return m_mixer;
        case 0x06:
        case 0x0A:
        case 0x0C:
        case 0x0E:
            return m_dsp;
        case 0x0F:
            return m_mpu;
        default:
            return false;
    }
}

INLINE void Audio::Clock(u32 clocks)
{
    if (clocks == 0)
        return;

    u64 sample_clock_counter = m_state.sample_clock_counter + (u64)clocks * GDOX_AUDIO_SAMPLE_RATE;

    if (sample_clock_counter < GDOX_MACHINE_CLOCK_RATE)
    {
        ClockSources(clocks);
        m_state.sample_clock_counter = sample_clock_counter;
        m_state.clocks += clocks;
        return;
    }

    while (clocks > 0)
    {
        u32 step = clocks;
        u64 remaining = GDOX_MACHINE_CLOCK_RATE - m_state.sample_clock_counter;
        u64 clocks_to_sample = (remaining + GDOX_AUDIO_SAMPLE_RATE - 1) / GDOX_AUDIO_SAMPLE_RATE;

        if ((clocks_to_sample > 0) && (clocks_to_sample < step))
            step = (u32)clocks_to_sample;

        ClockSources(step);

        m_state.sample_clock_counter += (u64)step * GDOX_AUDIO_SAMPLE_RATE;
        m_state.clocks += step;
        clocks -= step;

        while (m_state.sample_clock_counter >= GDOX_MACHINE_CLOCK_RATE)
        {
            m_state.sample_clock_counter -= GDOX_MACHINE_CLOCK_RATE;
            SampleSources();
        }
    }
}

INLINE void Audio::ClockSources(u32 clocks)
{
    for (int i = 0; i < m_opl_chips; i++)
        m_opl[i]->Clock(clocks);

    if (m_dsp)
        m_sound_blaster->Clock(clocks);
}

INLINE void Audio::SampleSources()
{
    s16 fm_left = 0;
    s16 fm_right = 0;
    s16 pcm_left = 0;
    s16 pcm_right = 0;
    s16 cdda_left = 0;
    s16 cdda_right = 0;

    if (m_opl_chips != 0)
        m_opl[0]->Sample(fm_left, fm_right);

    // The first Pro has one OPL2 on each side
    if (m_opl_chips > 1)
    {
        s16 other = 0;
        m_opl[1]->Sample(fm_right, other);
    }

    if (m_dsp)
        m_sound_blaster->Sample(pcm_left, pcm_right);

    // CD-DA advances one disc sample per output sample since both run at 44.1 kHz
    m_cdrom_audio->Sample(cdda_left, cdda_right);

    // The drive's output ports pick and scale the disc's channels, then the card's CD input level applies
    s32 cd_left = (s32)((((s64)cdda_left * m_cdda_gains[0]) + ((s64)cdda_right * m_cdda_gains[1])) >> 15);
    s32 cd_right = (s32)((((s64)cdda_left * m_cdda_gains[2]) + ((s64)cdda_right * m_cdda_gains[3])) >> 15);

    // The card's filter sits between the DAC and the mixer, FM doesn't go through it
    m_state.pcm_lowpass_left += (s32)(((s64)m_pcm_lowpass_alpha_q15 * (pcm_left - m_state.pcm_lowpass_left)) >> 15);
    m_state.pcm_lowpass_right += (s32)(((s64)m_pcm_lowpass_alpha_q15 * (pcm_right - m_state.pcm_lowpass_right)) >> 15);

#ifndef GDOX_DISABLE_VGMRECORDER
    if (m_vgm_recording_enabled)
        m_vgm_recorder.UpdateTiming();
#endif

    s16 buzzer = SampleBuzzer();

    // Keep the samples already buffered for this frame and drop the excess
    if (m_buffer_index + 1 >= GDOX_AUDIO_BUFFER_SIZE)
    {
        if (!m_buffer_overflow)
            Error("Audio buffer overflow");

        m_buffer_overflow = true;
        return;
    }

    if (m_channel_scopes)
        CaptureChannels(m_buffer_index >> 1);

    m_fm_buffer[m_buffer_index + 0] = (s16)CLAMP((fm_left * m_fm_gain_left) >> 15, -32768, 32767);
    m_fm_buffer[m_buffer_index + 1] = (s16)CLAMP((fm_right * m_fm_gain_right) >> 15, -32768, 32767);
    m_pcm_buffer[m_buffer_index + 0] = (s16)((m_state.pcm_lowpass_left * m_pcm_gain_left) >> 15);
    m_pcm_buffer[m_buffer_index + 1] = (s16)((m_state.pcm_lowpass_right * m_pcm_gain_right) >> 15);
    m_cdda_buffer[m_buffer_index + 0] = (s16)CLAMP(cd_left, -32768, 32767);
    m_cdda_buffer[m_buffer_index + 1] = (s16)CLAMP(cd_right, -32768, 32767);
    m_buzzer_buffer[m_buffer_index + 0] = buzzer;
    m_buzzer_buffer[m_buffer_index + 1] = buzzer;
    m_buffer_index += 2;
}

// Port 61h bit 1 gates OUT2 and the driver only pulls one way, so the speaker sits at zero with no current through it
// Both are averaged over the PIT ticks of the output sample, so PWM through either survives and high tones don't alias
INLINE s16 Audio::SampleBuzzer()
{
    AccumulateBuzzer(m_pit->GetTick(m_state.clocks));

    u32 ticks = m_state.buzzer_ticks;
    u32 level = m_state.buzzer_level;

    m_state.buzzer_ticks = 0;
    m_state.buzzer_level = 0;

    return ticks != 0 ? (s16)((k_audio_buzzer_level * level) / ticks) : 0;
}

// Level counts half ticks. A square wave past Nyquist can't be reproduced and would fold back as a tone a real speaker
// doesn't make, so it plays as its 50% average
INLINE void Audio::AccumulateBuzzer(u64 tick)
{
    u64 first = m_state.buzzer_tick;

    if (tick <= first)
        return;

    m_state.buzzer_tick = tick;
    m_state.buzzer_ticks += (u32)(tick - first);

    if (!m_pit->IsBuzzerEnabled())
        return;

    if (m_pit->IsBuzzerUltrasonic())
    {
        m_state.buzzer_level += (u32)(tick - first);
        return;
    }

    m_state.buzzer_level += (u32)m_pit->CountBuzzerHighs(first + 1, tick) * 2;
}

INLINE s32 Audio::HighPass(s32& dc, s32 sample)
{
    dc += ((sample * (1 << k_audio_dc_fraction)) - dc) >> k_audio_dc_shift;
    return sample - ((dc + k_audio_dc_round) >> k_audio_dc_fraction);
}

#endif /* AUDIO_INLINE_H */
