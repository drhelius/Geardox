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


#ifndef MC146818_H
#define MC146818_H

#include <iostream>
#include "../common/common.h"

class PIC;
class Scheduler;
class StateSerializer;
class TraceLogger;

class MC146818
{
public:
    struct MC146818_State
    {
        u8 ram[GDOX_CMOS_SIZE];
        u8 index;
        u8 seconds;
        u8 minutes;
        u8 hours;
        u8 weekday;
        u8 day;
        u8 month;
        u8 year;
        u8 century;
        bool irq;
        u64 divider_clocks;
        u64 update_clocks;
        u64 update_end_clocks;
        u64 periodic_clocks;
    };

public:
    MC146818();
    ~MC146818();
    void Init(PIC* pic, Scheduler* scheduler);
    void SetTraceLogger(TraceLogger* trace_logger);
    void SetStartTime(s64 start_time);
    void Reset(u64 clocks);
    bool Decodes(u16 port) const;
    u8 Read(u16 port, u64 clocks);
    u8 Peek(u16 port, u64 clocks) const;
    void Write(u16 port, u8 value, u64 clocks);
    u64 GetReadStableUntil(u16 port, u64 clocks) const;
    void Synchronize(u64 clocks);
    void HandleEvent(u64 clocks);
    u8 GetIndex() const;
    u8* GetCMOS();
    u8 ReadRAM(u8 index) const;
    void WriteRAM(u8 index, u8 value);
    void ResetCMOS();
    bool LoadCMOSFile(const u8* data, size_t size);
    void SaveCMOSFile(u8* data) const;
    MC146818_State* GetState();
    bool IsDividerRunning() const;
    bool IsCounting() const;
    bool IsUpdating(u64 clocks) const;
    u32 GetPeriodTicks() const;
    u8 GetStatusC() const;
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream, int version = GDOX_SAVESTATE_VERSION);

private:
    void Tick();
    bool MatchesAlarm(u8 alarm, u8 value) const;
    void CheckAlarm();
    u64 GetTickClocks(u64 ticks) const;
    u64 GetNextPeriodic(u64 clocks) const;
    u64 GetNextChange() const;
    void StartDivider(u64 clocks);
    void UpdatePeriodic(u64 clocks);
    void UpdateIRQ();
    void UpdateNextEvent();
    void UpdateClockRegisters();
    u8 Encode(u8 value) const;
    u8 Decode(u8 value) const;
    u8 EncodeHours(u8 hours) const;
    u8 DecodeHours(u8 value) const;
    void WriteRegister(u8 index, u8 value, u64 clocks);
    NO_INLINE void TraceEvent(u8 event, u8 value, u8 raw);
    void Serialize(StateSerializer& serializer);
    void SanitizeState();

private:
    MC146818_State m_state;
    PIC* m_pic;
    Scheduler* m_scheduler;
    TraceLogger* m_trace_logger;
    s64 m_start_time;
};

static const u8 k_mc146818_seconds = 0x00;
static const u8 k_mc146818_seconds_alarm = 0x01;
static const u8 k_mc146818_minutes = 0x02;
static const u8 k_mc146818_minutes_alarm = 0x03;
static const u8 k_mc146818_hours = 0x04;
static const u8 k_mc146818_hours_alarm = 0x05;
static const u8 k_mc146818_weekday = 0x06;
static const u8 k_mc146818_day = 0x07;
static const u8 k_mc146818_month = 0x08;
static const u8 k_mc146818_year = 0x09;
static const u8 k_mc146818_status_a = 0x0A;
static const u8 k_mc146818_status_b = 0x0B;
static const u8 k_mc146818_status_c = 0x0C;
static const u8 k_mc146818_status_d = 0x0D;
static const u8 k_mc146818_ram_start = 0x0E;
static const u8 k_mc146818_century = 0x32;
static const u8 k_mc146818_a_uip = 0x80;
static const u8 k_mc146818_a_divider = 0x70;
static const u8 k_mc146818_a_divider_normal = 0x20;
static const u8 k_mc146818_a_rate = 0x0F;
static const u8 k_mc146818_b_set = 0x80;
static const u8 k_mc146818_b_periodic = 0x40;
static const u8 k_mc146818_b_alarm = 0x20;
static const u8 k_mc146818_b_update = 0x10;
static const u8 k_mc146818_b_square_wave = 0x08;
static const u8 k_mc146818_b_binary = 0x04;
static const u8 k_mc146818_b_24_hour = 0x02;
static const u8 k_mc146818_b_reset_mask = k_mc146818_b_periodic | k_mc146818_b_alarm | k_mc146818_b_update |
    k_mc146818_b_square_wave;
static const u8 k_mc146818_c_irq = 0x80;
static const u8 k_mc146818_c_periodic = 0x40;
static const u8 k_mc146818_c_alarm = 0x20;
static const u8 k_mc146818_c_update = 0x10;
static const u8 k_mc146818_c_flags = 0x70;
static const u8 k_mc146818_d_vrt = 0x80;
static const u8 k_mc146818_alarm_any = 0xC0;
static const int k_mc146818_irq = 8;
static const u32 k_mc146818_chain_rate = 32768;
static const u64 k_mc146818_second_clocks = GDOX_MACHINE_CLOCK_RATE;
// Update in progress rises 244 us before the time changes and falls with update ended 1984 us after it
static const u64 k_mc146818_uip_clocks = ((u64)GDOX_MACHINE_CLOCK_RATE * 244) / 1000000;
static const u64 k_mc146818_update_clocks = ((u64)GDOX_MACHINE_CLOCK_RATE * 1984) / 1000000;

#include "mc146818_inline.h"

#endif /* MC146818_H */
