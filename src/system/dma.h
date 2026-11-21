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

#ifndef DMA_H
#define DMA_H

#include <iostream>
#include "../common/common.h"
#include "i8237.h"

#define DMA_CHANNELS 8
#define DMA_CHIPS 2
#define DMA_PAGES 16

class Memory;
class Scheduler;
class TraceLogger;
struct GDOX_Trace_Entry;
class StateSerializer;

typedef bool (*GDOX_DMA_Read_Fn)(void* device, u16& value, bool word);
typedef bool (*GDOX_DMA_Write_Fn)(void* device, u16 value, bool word);
typedef void (*GDOX_DMA_End_Fn)(void* device);
typedef void (*GDOX_DMA_Change_Fn)(void* device);
typedef void (*GDOX_DMA_Debug_Fn)(void* debugger, u32 address, u32 size, bool write);

struct GDOX_DMA_Endpoint
{
    void* device;
    GDOX_DMA_Read_Fn read;
    GDOX_DMA_Write_Fn write;
    GDOX_DMA_End_Fn end;
    GDOX_DMA_Change_Fn change;
};

class DMA
{
public:
    struct DMA_State
    {
        u8 pages[DMA_PAGES];
        u8 stalled;
        s8 active_channel;
        u64 next_clocks;
        u32 ahead_clocks;
        u32 ahead_owed;
        u32 ahead_frame_clocks;
        u32 ahead_frame_cost;
        u64 ahead_next;
    };

public:
    DMA();
    ~DMA();
    void Init(Memory* memory, Scheduler* scheduler);
    void SetTraceLogger(TraceLogger* trace_logger);
    void SetDebugCallback(GDOX_DMA_Debug_Fn callback, void* debugger);
    void Reset();
    bool Decodes(u16 port) const;
    u8 Read(u16 port);
    u8 Peek(u16 port) const;
    void Write(u16 port, u8 value);
    u64 GetReadStableUntil(u16 port) const;
    void SetEndpoint(int channel, const GDOX_DMA_Endpoint& endpoint);
    void SetRequest(int channel, bool active);
    u32 HandleEvent(u64 clocks);
    u32 ReadAhead(int channel, u32 offset, u32 units, u32 frame_units, u32 frame_clocks, u8* buffer);
    void Advance(int channel, u32 units);
    bool IsAheadPending() const;
    bool IsAheadDue(u64 clocks) const;
    u32 GetAheadClocks(u64 clocks) const;
    u32 TakeAheadClocks(u64 clocks, u64 limit);
    I8237* GetController(int index);
    u32 GetPhysicalAddress(int channel) const;
    DMA_State* GetState();
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream, int version = GDOX_SAVESTATE_VERSION);

private:
    I8237& GetChip(int channel);
    const I8237& GetChip(int channel) const;
    u8 GetExcluded(int chip) const;
    bool IsHolding(int channel) const;
    bool IsServiceable(int channel) const;
    int GetServiceChannel() const;
    bool TransferUnit(int channel, u64 clocks, bool& terminal);
    void FinishService(int channel);
    void UpdateCascade();
    void NotifyChange(u16 port, int chip, int reg, u8 value);
    void UpdateNextEvent();
    void CheckUnsupported();
    NO_INLINE void TraceWrite(u16 port, int chip, int reg, u8 value);
    NO_INLINE void TraceUnsupported(int chip, int reg, u8 value);
    GDOX_Trace_Entry* TraceEvent(u8 event, int channel, u8 reg, u8 value, bool terminal);
    void Serialize(StateSerializer& serializer, int version);
    void SanitizeState();

private:
    Memory* m_memory;
    Scheduler* m_scheduler;
    I8237 m_chips[DMA_CHIPS];
    GDOX_DMA_Endpoint m_endpoints[DMA_CHANNELS];
    bool m_unsupported_logged;
    TraceLogger* m_trace_logger;
    GDOX_DMA_Debug_Fn m_debug_callback;
    void* m_debugger;
    DMA_State m_state;
};

// Channel 4 is chip 2's input from chip 1, so it has no page of its own: its slot is the AT's refresh register
static const u8 k_dma_channel_pages[DMA_CHANNELS] = { 0x07, 0x03, 0x01, 0x02, 0x0F, 0x0B, 0x09, 0x0A };
static const int k_dma_cascade_channel = 4;

// A transfer takes 5 clocks of 240 ns at the 8.33 MHz ISA clock, or 4 when a run skips S1. The release has no
// source, it is about 4 BCLKs for the CPU to take the bus and hand it back
static const u32 k_dma_unit_clocks = 34;
static const u32 k_dma_repeat_clocks = 27;
static const u32 k_dma_release_clocks = 14;

#include "dma_inline.h"

#endif /* DMA_H */
