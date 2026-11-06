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


#ifndef PIT_H
#define PIT_H

#include <iostream>
#include "../common/common.h"
#include "i8254.h"

class Audio;
class PIC;
class Scheduler;
class StateSerializer;
class TraceLogger;

class PIT
{
public:
    struct PIT_State
    {
        u64 settled_tick;
        bool speaker_gate;
        bool speaker_data;
    };

public:
    PIT();
    ~PIT();
    void Init(PIC* pic, Scheduler* scheduler, Audio* audio);
    void SetTraceLogger(TraceLogger* trace_logger);
    void Reset();
    u8 Read(u16 port, u64 clocks);
    u8 Peek(u16 port, u64 clocks) const;
    void Write(u16 port, u8 value, u64 clocks);
    void Synchronize(u64 clocks);
    void HandleEvent(u64 clocks);
    void SetSpeaker(bool gate, bool data, u64 clocks);
    bool GetChannel2Output(u64 clocks) const;
    bool GetRefreshToggle(u64 clocks) const;
    u64 GetSpeakerStableUntil(u64 clocks) const;
    bool IsBuzzerEnabled() const;
    bool GetBuzzerOutput(u64 tick);
    I8254* GetPIT();
    u64 GetTick(u64 clocks) const;
    u64 GetTickClocks(u64 tick) const;
    PIT_State* GetState();
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream, int version = GDOX_SAVESTATE_VERSION);

private:
    u64 GetRefreshEdges(u64 tick) const;
    void UpdateNextEvent();
    void TraceTimeout();
    void TraceWrite(u16 port, u8 value);
    void TraceSpeaker();
    void Serialize(StateSerializer& serializer);
    void SanitizeState();

private:
    PIC* m_pic;
    Scheduler* m_scheduler;
    Audio* m_audio;
    TraceLogger* m_trace_logger;
    I8254 m_pit;
    PIT_State m_state;
};

// 1,193,182 Hz is a twelfth of the 14.31818 MHz oscillator, 24 machine clocks
static const u64 k_pit_tick_clocks = 24;
static const int k_pit_irq0_channel = 0;
static const int k_pit_refresh_channel = 1;
static const int k_pit_buzzer_channel = 2;
static const u64 k_pit_refresh_ticks = 18;

#include "pit_inline.h"

#endif /* PIT_H */
