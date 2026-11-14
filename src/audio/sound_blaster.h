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

#ifndef SOUND_BLASTER_H
#define SOUND_BLASTER_H

#include <iostream>
#include "../common/common.h"

#define SOUND_BLASTER_OUTPUT_SIZE 64
#define SOUND_BLASTER_MIXER_SIZE 256
#define SOUND_BLASTER_LATCH_SIZE 4
#define SOUND_BLASTER_AHEAD_SIZE 256
#define SOUND_BLASTER_OPERANDS 4
#define SOUND_BLASTER_ASP_REGISTERS 256

class DMA;
class MidiOut;
class PIC;
class StateSerializer;
class TraceLogger;

class SoundBlaster
{
public:
    enum SoundBlaster_Format
    {
        SOUND_BLASTER_FORMAT_PCM8 = 0,
        SOUND_BLASTER_FORMAT_PCM16,
        SOUND_BLASTER_FORMAT_ADPCM2,
        SOUND_BLASTER_FORMAT_ADPCM3,
        SOUND_BLASTER_FORMAT_ADPCM4,
        SOUND_BLASTER_FORMAT_SILENCE
    };

    enum SoundBlaster_Source
    {
        SOUND_BLASTER_SOURCE_VOICE = 0,
        SOUND_BLASTER_SOURCE_FM,
        SOUND_BLASTER_SOURCE_CD,
        SOUND_BLASTER_SOURCE_COUNT
    };

    struct SoundBlaster_Transfer
    {
        u32 length;
        u32 remaining;
        u32 next_length;
        u8 format;
        bool active;
        bool paused;
        bool auto_init;
        bool input;
        bool high_speed;
        bool stereo;
        bool is_signed;
        bool exit_pending;
        bool next_pending;
    };

    struct SoundBlaster_State
    {
        bool resetting;
        bool reset_pending;
        u64 ready_clocks;
        bool midi_uart;
        u8 command;
        u8 operands[SOUND_BLASTER_OPERANDS];
        u8 operand_count;
        u8 operands_needed;
        u8 output[SOUND_BLASTER_OUTPUT_SIZE];
        u8 output_head;
        u8 output_count;
        u8 last_output;
        u8 write_status_reads;
        u8 test_register;
        u8 asp_registers[SOUND_BLASTER_ASP_REGISTERS];
        u8 e2_value;
        u8 e2_count;
        bool e2_pending;

        SoundBlaster_Transfer transfer;
        u32 block_size;
        u32 rate_numerator;
        u32 rate_denominator;
        u64 phase;
        u8 latch[SOUND_BLASTER_LATCH_SIZE];
        u8 latch_count;
        u8 transfers_needed;
        u8 ahead[SOUND_BLASTER_AHEAD_SIZE];
        u16 ahead_size;
        u16 ahead_read;
        bool requesting;
        s16 left_sample;
        s16 right_sample;
        s16 previous_left_sample;
        s16 previous_right_sample;
        bool speaker;

        u8 adpcm_reference;
        u8 adpcm_step;
        u8 adpcm_byte;
        u8 adpcm_codes;
        bool adpcm_seed;

        bool irq_8;
        bool irq_16;
        bool irq_mpu;
        int irq_line;
        int dma_8;
        int dma_16;

        u8 mixer_index;
        u8 mixer[SOUND_BLASTER_MIXER_SIZE];
    };

public:
    SoundBlaster();
    ~SoundBlaster();
    void Init(PIC* pic, DMA* dma, MidiOut* midi_out);
    void SetTraceLogger(TraceLogger* trace_logger);
    void SetCard(int card, int irq, int dma);
    void Reset(u64 clocks);
    void Clock(u32 clocks);
    void Sample(s16& left, s16& right) const;
    u8 Read(u16 port, u64 clocks);
    u8 Peek(u16 port, u64 clocks) const;
    void Write(u16 port, u8 value, u64 clocks);
    void SetMPUInterrupt(bool active);
    u64 GetClocksToEvent() const;
    u64 GetReadStableUntil(u16 port) const;
    float GetFilterCutoff() const;
    s32 GetGain(int source, bool right) const;
    bool HasDSP() const;
    bool HasMixer() const;
    bool IsRunning() const;
    bool IsSpeakerOn() const;
    bool IsStereo() const;
    bool IsReadingAhead() const;
    u32 GetSampleRate() const;
    u8 GetVersionMajor() const;
    u8 GetVersionMinor() const;
    u8 GetInterruptStatus() const;
    SoundBlaster_State* GetState();
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream);

    static bool DMAReadCallback(void* device, u16& value, bool word);
    static bool DMAWriteCallback(void* device, u16 value, bool word);
    static void DMAChangeCallback(void* device);

private:
    void ResetDSP();
    void ResetMixer();
    void Tick();
    void PlayFrame();
    void PlayADPCM();
    void ConsumeUnits(u32 units);
    void EndBlock();
    void StopTransfer();
    void RequestFrame();
    void ReadAhead(u32 units);
    bool TakeAhead(u32 units);
    void DropAhead();
    u32 GetFramesAhead() const;
    u32 GetUnitBytes() const;
    void SetRequest(bool active);
    int GetChannel() const;
    bool IsWordChannel() const;
    u32 GetFrameUnits() const;
    u64 GetPeriod() const;
    bool IsProStereo() const;
    u8 DecodeADPCM(u8 code, int bits);

    void UpdateReset(u64 clocks);
    void AddOutput(u8 value);
    u8 ReadData();
    void WriteDSP(u8 value);
    int GetOperands(u8 command) const;
    void Execute();
    void ExecuteTransfer(u8 command);
    void StartTransfer(u8 format, bool auto_init, bool input, u32 length, bool high_speed, bool stereo, bool is_signed,
        bool seed);
    void SetTimeConstant(u8 value);
    void SetRate(u32 rate);
    void SetDirect(u8 value);
    void SetSpeaker(bool on);
    void SetPaused(bool paused, bool word);
    void WriteMIDI(u8 value);

    u8 ReadMixer() const;
    void WriteMixer(u8 value);
    void WriteCompatibleVolume(u8 left, u8 value);
    void UpdateResources();
    void UpdateGains();
    s32 GetLevel(u8 reg, bool right) const;
    void UpdateIRQ();
    void TraceCommand();
    void TraceTransfer();
    void TraceEvent(u8 event, u8 value);
    void TraceMixer(u8 value);
    void Serialize(StateSerializer& serializer);
    void SanitizeState();

private:
    SoundBlaster_State m_state;
    PIC* m_pic;
    DMA* m_dma;
    MidiOut* m_midi_out;
    TraceLogger* m_trace_logger;
    u8 m_version_major;
    u8 m_version_minor;
    int m_mixer_type;
    int m_asserted_line;
    s32 m_gain[SOUND_BLASTER_SOURCE_COUNT][2];
};

static const int k_sound_blaster_mixer_none = 0;
static const int k_sound_blaster_mixer_ct1345 = 1;
static const int k_sound_blaster_mixer_ct1745 = 2;
static const u16 k_sound_blaster_port = 0x0220;
static const u32 k_sound_blaster_default_rate = 22050;
static const u8 k_sound_blaster_stereo_switch = 0x02;
static const u8 k_sound_blaster_output_filter = 0x20;

#include "sound_blaster_inline.h"

#endif /* SOUND_BLASTER_H */
