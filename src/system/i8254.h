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

#ifndef I8254_H
#define I8254_H

#include <iostream>
#include "../common/common.h"

#define I8254_COUNTER_COUNT 3

class StateSerializer;

class I8254
{
public:
    struct I8254_Counter
    {
        u16 reload;
        u16 count_latch;
        u8 write_latch;
        u8 access;
        u8 mode;
        bool bcd;
        bool programmed;
        bool counting;
        bool count_latched;
        bool read_high;
        bool write_high;
        bool latch_high;
        u64 load;
        u32 count;
        u32 phase;
        bool reload_pending;
        u64 pending_load;
        u32 pending_count;
        u32 pending_phase;
        bool gate;
        u64 hold;
        u64 null_until;
        u8 status_latch;
        bool status_latched;
    };

    struct I8254_State
    {
        I8254_Counter counters[I8254_COUNTER_COUNT];
    };

public:
    I8254();
    ~I8254();
    void Init();
    void Reset();
    u8 ReadCounter(int index, u64 tick);
    u8 PeekCounter(int index, u64 tick) const;
    u16 PeekCount(int index, u64 tick) const;
    void WriteCounter(int index, u8 value, u64 tick);
    void WriteControl(u8 value, u64 tick);
    void SetGate(int index, bool gate, u64 tick);
    bool GetOutput(int index, u64 tick);
    bool PeekOutput(int index, u64 tick) const;
    u64 CountHighs(int index, u64 first, u64 last);
    u64 GetNextRisingEdge(int index, u64 tick) const;
    I8254_State* GetState();
    const I8254_State* GetState() const;
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream, int version = GDOX_SAVESTATE_VERSION);

private:
    void LoadCount(I8254_Counter& counter, u16 raw, u64 tick);
    void ReadBack(u8 value, u64 tick);
    u64 GetGateTick(const I8254_Counter& counter, u64 tick) const;
    void CommitReload(I8254_Counter& counter, u64 tick) const;
    bool GetCounterOutput(I8254_Counter& counter, u64 tick) const;
    bool GetLoadedOutput(const I8254_Counter& counter, u64 tick) const;
    u64 CountLoadedHighs(const I8254_Counter& counter, u64 first, u64 last) const;
    u16 GetCount(I8254_Counter& counter, u64 tick) const;
    u32 GetEffectiveCount(u16 raw, bool bcd) const;
    u16 EncodeBCD(u32 value) const;
    u64 GetEdgeAfter(u8 mode, u64 load, u32 count, u32 phase, u64 tick) const;
    void Serialize(StateSerializer& serializer, int version);
    void SanitizeState();

private:
    I8254_State m_state;
};

static const u64 k_i8254_no_edge = 0xFFFFFFFFFFFFFFFFULL;

#include "i8254_inline.h"

#endif /* I8254_H */
