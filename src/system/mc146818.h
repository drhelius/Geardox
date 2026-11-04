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
        u64 update_clocks;
    };

public:
    MC146818();
    ~MC146818();
    void Init();
    void SetTraceLogger(TraceLogger* trace_logger);
    void SetStartTime(s64 start_time);
    void Reset(u64 clocks);
    bool Decodes(u16 port) const;
    u8 Read(u16 port, u64 clocks);
    u8 Peek(u16 port, u64 clocks) const;
    void Write(u16 port, u8 value, u64 clocks);
    u64 GetReadStableUntil(u16 port, u64 clocks) const;
    void Synchronize(u64 clocks);
    u8 GetIndex() const;
    u8* GetCMOS();
    u8 ReadRAM(u8 index) const;
    void WriteRAM(u8 index, u8 value);
    void ResetCMOS();
    bool LoadCMOSFile(const u8* data, size_t size);
    void SaveCMOSFile(u8* data) const;
    MC146818_State* GetState();
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream, int version = GDOX_SAVESTATE_VERSION);

private:
    bool IsCounting() const;
    void Tick();
    void UpdateClockRegisters();
    u8 Encode(u8 value) const;
    u8 Decode(u8 value) const;
    u8 EncodeHours(u8 hours) const;
    u8 DecodeHours(u8 value) const;
    void WriteRegister(u8 index, u8 value, u64 clocks);
    void TraceEvent(u8 event, u8 value);
    void Serialize(StateSerializer& serializer);
    void SanitizeState();

private:
    MC146818_State m_state;
    TraceLogger* m_trace_logger;
    s64 m_start_time;
};

static const u8 k_mc146818_seconds = 0x00;
static const u8 k_mc146818_minutes = 0x02;
static const u8 k_mc146818_hours = 0x04;
static const u8 k_mc146818_weekday = 0x06;
static const u8 k_mc146818_day = 0x07;
static const u8 k_mc146818_month = 0x08;
static const u8 k_mc146818_year = 0x09;
static const u8 k_mc146818_status_a = 0x0A;
static const u8 k_mc146818_status_b = 0x0B;
static const u8 k_mc146818_status_c = 0x0C;
static const u8 k_mc146818_status_d = 0x0D;
static const u8 k_mc146818_century = 0x32;
static const u8 k_mc146818_a_uip = 0x80;
static const u8 k_mc146818_a_divider = 0x70;
static const u8 k_mc146818_a_divider_normal = 0x20;
static const u8 k_mc146818_b_set = 0x80;
static const u8 k_mc146818_b_binary = 0x04;
static const u8 k_mc146818_b_24_hour = 0x02;
static const u8 k_mc146818_d_vrt = 0x80;
static const u64 k_mc146818_second_clocks = GDOX_MACHINE_CLOCK_RATE;
// Update in progress rises 244 us before each update
static const u64 k_mc146818_uip_clocks = ((u64)GDOX_MACHINE_CLOCK_RATE * 244) / 1000000;

#endif /* MC146818_H */
