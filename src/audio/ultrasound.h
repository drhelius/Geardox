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

#ifndef ULTRASOUND_H
#define ULTRASOUND_H

#include <iostream>
#include "../common/common.h"

#define ULTRASOUND_VOICES 32
#define ULTRASOUND_DRAM_SIZE 0x100000
#define ULTRASOUND_PAGE_SHIFT 12
#define ULTRASOUND_PAGES (ULTRASOUND_DRAM_SIZE >> ULTRASOUND_PAGE_SHIFT)
#define ULTRASOUND_VOLUME_LEVELS 4096

class DMA;
class MidiOut;
class PIC;
class StateSerializer;
class TraceLogger;

class UltraSound
{
public:
    struct UltraSound_Voice
    {
        u32 start;
        u32 end;
        u32 address;
        u16 frequency;
        u16 volume;
        u8 control;
        u8 volume_control;
        u8 ramp_rate;
        u8 ramp_start;
        u8 ramp_end;
        u8 pan;
        u8 ramp_count;
        u32 fetch_key;
        s16 fetch_first;
        s16 fetch_second;
        bool address_pending;
        u32 held_end;
        bool end_held;
    };

    struct UltraSound_State
    {
        UltraSound_Voice voices[ULTRASOUND_VOICES];
        u8 active_voices;
        u8 voice_select;
        u8 register_select;
        u8 data_low;
        u8 reset;
        u8 dma_control;
        u16 dma_address;
        u32 dma_pointer;
        u32 dram_address;
        u8 timer_control;
        u8 timer_count[2];
        u8 timer_preload[2];
        u64 timer_phase[2];
        u8 adlib_select;
        u8 adlib_control;
        u8 adlib_status;
        u8 adlib_data;
        u8 sb_2xc;
        u8 sb_2xe;
        u8 sample_control;
        u8 sample_rate;
        u64 sample_phase;
        u8 sample_units;
        u8 joystick_trim;
        bool timer_irq[2];
        bool dma_tc;
        bool sample_tc;
        u8 voice_irq;
        bool voice_irq_latched;
        u8 mix_control;
        u8 irq_latch;
        u8 dma_latch;
        bool latch_armed;
        u8 register_bank;
        u8 jumper;
        u8 midi_control;
        u8 midi_rx;
        bool midi_rx_full;
        u32 midi_tx_clocks;
        u64 phase;
        s16 left_sample;
        s16 right_sample;
        s16 previous_left_sample;
        s16 previous_right_sample;
        u8 dram[ULTRASOUND_DRAM_SIZE];
    };

public:
    UltraSound();
    ~UltraSound();
    void Init(PIC* pic, DMA* dma, MidiOut* midi_out);
    void SetTraceLogger(TraceLogger* trace_logger);
    void SetEnabled(bool enabled);
    void Reset();
    void Clock(u32 clocks);
    void Sample(s16& left, s16& right) const;
    bool Decodes(u16 port) const;
    u8 Read(u16 port);
    u8 Peek(u16 port) const;
    void Write(u16 port, u8 value);
    void SnoopAdLib(u8 value);
    u64 GetClocksToEvent() const;
    u64 GetReadStableUntil(u16 port, u64 clocks) const;
    int GetDRAMChannel() const;
    int GetADCChannel() const;
    int GetIRQ(int channel) const;
    u32 GetFrameRate() const;
    int GetActiveVoices() const;
    bool IsRunning() const;
    bool IsOutputEnabled() const;
    u8 GetIRQStatus() const;
    u16 PeekRegister(int voice, u8 index) const;
    s16 GetVoiceOutput(int voice) const;
    u8 PeekDRAM(u32 address) const;
    void PokeDRAM(u32 address, u8 value);
    u8* GetDRAM();
    void SetDirtyTracking(bool enabled);
    const u8* GetDirtyPages() const;
    void ClearDirtyPages();
    UltraSound_State* GetState();
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream);

    static bool DRAMReadCallback(void* device, u16& value, bool word);
    static bool DRAMWriteCallback(void* device, u16 value, bool word);
    static void DRAMEndCallback(void* device);
    static bool ADCReadCallback(void* device, u16& value, bool word);
    static bool ADCWriteCallback(void* device, u16 value, bool word);
    static void ADCEndCallback(void* device);

private:
    void ResetGF1();
    void ResetVoice(UltraSound_Voice& voice);
    void Frame();
    void RunVoice(UltraSound_Voice& voice, int index, s32& left, s32& right);
    void FetchVoice(UltraSound_Voice& voice);
    void StepAddress(UltraSound_Voice& voice);
    void HoldEnd(UltraSound_Voice& voice, u32 previous);
    void StepRamp(UltraSound_Voice& voice);
    void LatchVoiceIRQ(int index);
    void ClockTimers(u32 clocks);
    void TickTimer(int timer);
    void ClockSampling(u32 clocks);
    void ClockMIDI(u32 clocks);
    u8 ReadRegister(bool high);
    u16 GetRegister(int voice, u8 index) const;
    void WriteRegister(u8 index, u16 value, bool word);
    void WriteGlobal(u8 index, u16 value);
    void WriteVoice(UltraSound_Voice& voice, u8 index, u16 value);
    void WriteDMAControl(u8 value);
    void WriteSampleControl(u8 value);
    void WriteTimerControl(u8 value);
    void WriteAdLib(u8 value);
    void WriteLatch(u8 value);
    void WriteMIDIControl(u8 value);
    void WriteMIDIData(u8 value);
    u8 ReadMIDIStatus() const;
    u8 ReadVoiceIRQ();
    bool IsWordRegister(u8 index) const;
    bool IsWordChannel(int channel) const;
    bool DecodesMIDI() const;
    u32 GetDMAAddress(u32 pointer) const;
    void AdvanceDMA(bool word);
    u8 InvertDMA(u8 value, u32 pointer) const;
    void MarkDirty(u32 address);
    u64 GetFramePeriod() const;
    u64 GetSamplePeriod() const;
    u64 GetVoiceFrames(const UltraSound_Voice& voice) const;
    u64 GetRampFrames(const UltraSound_Voice& voice) const;
    u64 GetTimerClocks(int timer) const;
    u64 FramesToClocks(u64 frames) const;
    void UpdateDMARequest();
    void UpdateSampleRequest();
    void UpdateIRQ();
    void TraceRegister(u8 index, u16 value);
    void TraceEvent(u8 event, u8 voice, u16 value);
    void Serialize(StateSerializer& serializer);
    void SanitizeState();

private:
    UltraSound_State m_state;
    PIC* m_pic;
    DMA* m_dma;
    MidiOut* m_midi_out;
    TraceLogger* m_trace_logger;
    bool m_enabled;
    int m_asserted_irq[2];
    int m_dram_request;
    int m_adc_request;
    bool m_dirty_tracking;
    u8 m_dirty_pages[ULTRASOUND_PAGES];
    s16 m_voice_output[ULTRASOUND_VOICES];
    u32 m_gain[ULTRASOUND_VOLUME_LEVELS];
};

static const u16 k_ultrasound_port = 0x0240;
static const u32 k_ultrasound_clock_rate = 9878400;
static const u32 k_ultrasound_voice_clocks = 16;
static const int k_ultrasound_min_voices = 14;

#include "ultrasound_inline.h"

#endif /* ULTRASOUND_H */
