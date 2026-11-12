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
#include "opl.h"
#include "sound_blaster.h"
#include "mpu401.h"
#include "vgm_recorder.h"

#define AUDIO_OPL_CHIPS 2

class CdRomAudio;
class DMA;
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
        s32 pcm_lowpass_left;
        s32 pcm_lowpass_right;
        s32 output_dc_left;
        s32 output_dc_right;
        u64 clocks;
        u64 buzzer_tick;
        u32 buzzer_ticks;
        u32 buzzer_level;
    };

public:
    Audio();
    ~Audio();
    void Init(Scheduler* scheduler, CdRomAudio* cdrom_audio, PIC* pic, PIT* pit, Memory* memory, DMA* dma);
    void SetTraceLogger(TraceLogger* trace_logger);
    void SetCard(int card, int irq, int dma);
    void Reset();
    void Mute(bool mute);
    void SetMasterVolume(float volume);
    void SetFMVolume(float volume);
    void SetPCMVolume(float volume);
    void SetCDDAVolume(float volume);
    void SetCDDAGains(const s32* gains);
    void SetBuzzerVolume(float volume);
    void Synchronize(u64 clocks);
    void SynchronizeBuzzer(u64 clocks);
    void SynchronizeDMA(u64 clocks);
    void HandleEvent(u64 clocks);
    void UpdateEvent();
    bool Decodes(u16 port) const;
    u8 Read(u16 port, u64 clocks);
    u8 Peek(u16 port, u64 clocks) const;
    void Write(u16 port, u8 value, u64 clocks);
    u64 GetReadStableUntil(u16 port) const;
    void EndFrame(s16* sample_buffer, int* sample_count);
    OPL* GetOPL(int chip);
    int GetOPLChips() const;
    SoundBlaster* GetSoundBlaster();
    MPU401* GetMPU401();
    Audio_State* GetState();
    void SetSourceMute(int source, bool mute);
    bool IsSourceMuted(int source) const;
    void EnableChannelScopes(bool enable);
    int GetFrameSamples() const;
    const s16* GetSourceBuffer(int source) const;
    const s16* GetFMChannelBuffer(int channel) const;
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream, int version = GDOX_SAVESTATE_VERSION);
    bool StartVgmRecording(const char* file_path, int clock_rate, const GDOX_VGM_Metadata& metadata);
    void StopVgmRecording();
    bool IsVgmRecording() const;

private:
    void Clock(u32 clocks);
    void ClockSources(u32 clocks);
    void SampleSources();
    s16 SampleBuzzer();
    void AccumulateBuzzer(u64 tick);
    s32 HighPass(s32& dc, s32 sample);
    void CaptureChannels(int index);
    bool IsFMPort(u16 port) const;
    bool IsMPUPort(u16 port) const;
    void WriteFM(u16 port, u8 value);
    void WriteChip(int chip, u8 port, u8 value);
    void UpdatePCMFilter();
    void UpdateGains();
    void TraceFM(int chip, u8 value);
    void WriteVgmInitialState();
    void Serialize(StateSerializer& serializer, int version);
    void SanitizeState();

private:
    OPL* m_opl[AUDIO_OPL_CHIPS];
    SoundBlaster* m_sound_blaster;
    MPU401* m_mpu401;
    CdRomAudio* m_cdrom_audio;
    Scheduler* m_scheduler;
    PIC* m_pic;
    PIT* m_pit;
    Memory* m_memory;
    TraceLogger* m_trace_logger;
    int m_opl_chips;
    bool m_opl3;
    bool m_dsp;
    bool m_mixer;
    bool m_mpu;
    bool m_mute;
    float m_master_volume;
    float m_fm_volume;
    float m_pcm_volume;
    float m_cdda_volume;
    float m_buzzer_volume;
    Audio_State m_state;
    float m_pcm_cutoff;
    u16 m_pcm_lowpass_alpha_q15;
    s32 m_fm_gain_left;
    s32 m_fm_gain_right;
    s32 m_pcm_gain_left;
    s32 m_pcm_gain_right;
    s32 m_cdda_drive_gains[4];
    s32 m_cdda_gains[4];
    s16 m_fm_buffer[GDOX_AUDIO_BUFFER_SIZE];
    s16 m_pcm_buffer[GDOX_AUDIO_BUFFER_SIZE];
    s16 m_cdda_buffer[GDOX_AUDIO_BUFFER_SIZE];
    s16 m_buzzer_buffer[GDOX_AUDIO_BUFFER_SIZE];
    int m_buffer_index;
    bool m_buffer_overflow;
    int m_frame_samples;
    bool m_source_mute[AUDIO_SOURCE_COUNT];
    bool m_channel_scopes;
    s16 m_fm_channel_buffer[OPL_CHANNEL_COUNT][GDOX_AUDIO_BUFFER_SIZE / 2];
    VgmRecorder m_vgm_recorder;
    bool m_vgm_recording_enabled;
};

#include "audio_inline.h"

#endif /* AUDIO_H */
