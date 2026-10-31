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

#include "audio.h"
#include "../cdrom/cdrom_audio.h"
#include "../system/scheduler.h"
#include "../common/state_serializer.h"

Audio::Audio()
{
    InitPointer(m_cdrom_audio);
    InitPointer(m_scheduler);
    InitPointer(m_pic);
    InitPointer(m_pit);
    InitPointer(m_memory);
    InitPointer(m_trace_logger);
    m_mute = false;
    m_master_volume = 1.0f;
    m_fm_volume = 1.0f;
    m_pcm_volume = 1.0f;
    m_cdda_volume = 1.0f;
    m_buzzer_volume = 1.0f;
    m_buffer_index = 0;
    m_buffer_overflow = false;
    m_frame_samples = 0;
    m_vgm_recording_enabled = false;

    for (int i = 0; i < AUDIO_SOURCE_COUNT; i++)
        m_source_mute[i] = false;

    memset(m_fm_buffer, 0, sizeof(m_fm_buffer));
    memset(m_pcm_buffer, 0, sizeof(m_pcm_buffer));
    memset(m_cdda_buffer, 0, sizeof(m_cdda_buffer));
    memset(m_buzzer_buffer, 0, sizeof(m_buzzer_buffer));
}

Audio::~Audio()
{
}

void Audio::Init(Scheduler* scheduler, CdRomAudio* cdrom_audio, PIC* pic, PIT* pit, Memory* memory)
{
    m_scheduler = scheduler;
    m_cdrom_audio = cdrom_audio;
    m_pic = pic;
    m_pit = pit;
    m_memory = memory;
    Reset();
}

void Audio::SetTraceLogger(TraceLogger* trace_logger)
{
    m_trace_logger = trace_logger;
}

void Audio::Reset()
{
    m_state.sample_clock_counter = 0;
    m_state.output_dc_left = 0;
    m_state.output_dc_right = 0;
    m_state.clocks = m_scheduler->GetClocks();
    m_buffer_index = 0;
    m_buffer_overflow = false;
}

void Audio::EndFrame(s16* sample_buffer, int* sample_count)
{
    int samples = m_buffer_index;

    m_buffer_index = 0;
    m_buffer_overflow = false;
    m_frame_samples = samples;

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

    s32 dc_left = m_state.output_dc_left;
    s32 dc_right = m_state.output_dc_right;

    // Relative FM/PCM/CD-DA levels are unmeasured: 
    // FM enters at its full DAC sum, PCM at half scale, CD-DA at full scale
    if ((m_master_volume == 1.0f) && (fm_volume == 1.0f) && (pcm_volume == 1.0f) && (cdda_volume == 1.0f) &&
        (buzzer_volume == 1.0f))
    {
        for (int i = 0; i < samples; i += 2)
        {
            s32 left = (s32)m_fm_buffer[i] + (m_pcm_buffer[i] >> 1) + m_cdda_buffer[i] + m_buzzer_buffer[i];
            s32 right = (s32)m_fm_buffer[i + 1] + (m_pcm_buffer[i + 1] >> 1) + m_cdda_buffer[i + 1] + m_buzzer_buffer[i + 1];
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
            float left = (float)m_fm_buffer[i] * fm_volume + (float)(m_pcm_buffer[i] >> 1) * pcm_volume +
                (float)m_cdda_buffer[i] * cdda_volume + (float)m_buzzer_buffer[i] * buzzer_volume;
            float right = (float)m_fm_buffer[i + 1] * fm_volume + (float)(m_pcm_buffer[i + 1] >> 1) * pcm_volume +
                (float)m_cdda_buffer[i + 1] * cdda_volume + (float)m_buzzer_buffer[i + 1] * buzzer_volume;
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
}

void Audio::LoadState(std::istream& stream, int version)
{
    StateSerializer serializer(stream);
    Serialize(serializer, version);
    SanitizeState();
}

bool Audio::StartVgmRecording(const char* file_path, int clock_rate, const GDOX_VGM_Metadata& metadata)
{
    if (m_vgm_recording_enabled)
        return false;

    m_vgm_recorder.Start(file_path, clock_rate, metadata);
    m_vgm_recording_enabled = m_vgm_recorder.IsRecording();
    return m_vgm_recording_enabled;
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
    G_SERIALIZE(serializer, m_state.output_dc_left);
    G_SERIALIZE(serializer, m_state.output_dc_right);
}

void Audio::SanitizeState()
{
    m_state.sample_clock_counter %= GDOX_MACHINE_CLOCK_RATE;
    m_state.output_dc_left = CLAMP(m_state.output_dc_left, -k_audio_dc_limit, k_audio_dc_limit);
    m_state.output_dc_right = CLAMP(m_state.output_dc_right, -k_audio_dc_limit, k_audio_dc_limit);
}
