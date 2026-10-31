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

#ifndef AUDIO_H
#define AUDIO_H

#include <iostream>
#include "../common/common.h"
#include "vgm_recorder.h"

class CdRomAudio;
class Memory;
class PIC;
class PIT;
class Scheduler;
class StateSerializer;
class TraceLogger;

class Audio
{
public:
    enum Audio_Source
    {
        AUDIO_SOURCE_FM = 0,
        AUDIO_SOURCE_PCM,
        AUDIO_SOURCE_CDDA,
        AUDIO_SOURCE_BUZZER,
        AUDIO_SOURCE_COUNT
    };

    struct Audio_State
    {
        u64 sample_clock_counter;
        s32 output_dc_left;
        s32 output_dc_right;
        u64 clocks;
    };

public:
    Audio();
    ~Audio();
    void Init(Scheduler* scheduler, CdRomAudio* cdrom_audio, PIC* pic, PIT* pit, Memory* memory);
    void SetTraceLogger(TraceLogger* trace_logger);
    void Reset();
    void Mute(bool mute);
    void SetMasterVolume(float volume);
    void SetFMVolume(float volume);
    void SetPCMVolume(float volume);
    void SetCDDAVolume(float volume);
    void SetBuzzerVolume(float volume);
    void Synchronize(u64 clocks);
    void HandleEvent(u64 clocks);
    void EndFrame(s16* sample_buffer, int* sample_count);
    Audio_State* GetState();
    void SetSourceMute(int source, bool mute);
    bool IsSourceMuted(int source) const;
    int GetFrameSamples() const;
    const s16* GetSourceBuffer(int source) const;
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream, int version = GDOX_SAVESTATE_VERSION);
    bool StartVgmRecording(const char* file_path, int clock_rate, const GDOX_VGM_Metadata& metadata);
    void StopVgmRecording();
    bool IsVgmRecording() const;

private:
    void Clock(u32 clocks);
    void SampleSources();
    s16 SampleBuzzer();
    s32 HighPass(s32& dc, s32 sample);
    void Serialize(StateSerializer& serializer, int version);
    void SanitizeState();

private:
    CdRomAudio* m_cdrom_audio;
    Scheduler* m_scheduler;
    PIC* m_pic;
    PIT* m_pit;
    Memory* m_memory;
    TraceLogger* m_trace_logger;
    bool m_mute;
    float m_master_volume;
    float m_fm_volume;
    float m_pcm_volume;
    float m_cdda_volume;
    float m_buzzer_volume;
    Audio_State m_state;
    s16 m_fm_buffer[GDOX_AUDIO_BUFFER_SIZE];
    s16 m_pcm_buffer[GDOX_AUDIO_BUFFER_SIZE];
    s16 m_cdda_buffer[GDOX_AUDIO_BUFFER_SIZE];
    s16 m_buzzer_buffer[GDOX_AUDIO_BUFFER_SIZE];
    int m_buffer_index;
    bool m_buffer_overflow;
    int m_frame_samples;
    bool m_source_mute[AUDIO_SOURCE_COUNT];
    VgmRecorder m_vgm_recorder;
    bool m_vgm_recording_enabled;
};

#include "audio_inline.h"

#endif /* AUDIO_H */
