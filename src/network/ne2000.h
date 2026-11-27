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

#ifndef NE2000_H
#define NE2000_H

#include <iostream>
#include "../common/common.h"
#include "network_port.h"

#define NE2000_PORTS 0x20
#define NE2000_RAM_SIZE 0x4000
#define NE2000_RAM_START 0x4000
#define NE2000_PROM_SIZE 16
#define NE2000_FIFO_SIZE 8
#define NE2000_HISTORY 16

class NetworkPort;
class PIC;
class Scheduler;
class StateSerializer;
class TraceLogger;

class NE2000
{
public:
    struct NE2000_State
    {
        u8 cr;
        u8 isr;
        u8 imr;
        u8 dcr;
        u8 tcr;
        u8 tsr;
        u8 rcr;
        u8 rsr;
        u8 pstart;
        u8 pstop;
        u8 bnry;
        u8 curr;
        u8 tpsr;
        u8 ncr;
        u8 rnpp;
        u8 lnpp;
        u16 tbcr;
        u16 rsar;
        u16 rbcr;
        u16 clda;
        u16 send_end;
        u8 cntr[3];
        u8 par[6];
        u8 mar[8];
        bool irq;
        bool send_packet;
        u64 transmit_end;
        u64 receive_end;
        u64 wire_free;
        u8 fifo[NE2000_FIFO_SIZE];
        u8 fifo_read;
        u8 prom[NE2000_PROM_SIZE];
        u8 ram[NE2000_RAM_SIZE];
    };

    struct NE2000_Frame
    {
        u64 clocks;
        u16 length;
        u16 type;
        u8 destination[6];
        u8 source[6];
        bool sent;
        u8 status;
    };

    struct NE2000_Counters
    {
        u32 sent;
        u32 received;
        u32 filtered;
        u32 overflows;
        u32 runts;
    };

public:
    NE2000();
    ~NE2000();
    void Init(PIC* pic, Scheduler* scheduler, NetworkPort* port);
    void SetTraceLogger(TraceLogger* trace_logger);
    void SetConfig(bool enabled, u16 base, u8 irq);
    void SetAddress(const u8* address);
    void Reset();
    bool IsEnabled() const;
    u16 GetBase() const;
    u8 GetIRQ() const;
    bool Decodes(u16 port) const;
    bool IsDataPort(u16 port) const;
    u8 Read(u16 port, u64 clocks);
    u8 Peek(u16 port) const;
    void Write(u16 port, u8 value, u64 clocks);
    u16 ReadData16(u64 clocks);
    void WriteData16(u16 value, u64 clocks);
    u64 GetReadStableUntil(u16 port) const;
    void Poll(u64 clocks);
    void HandleEvent(u64 clocks);
    NE2000_State* GetState();
    const u8* GetAddress() const;
    const NE2000_Frame* GetHistory(int* count, int* next) const;
    const NE2000_Counters* GetCounters() const;
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream, int version = GDOX_SAVESTATE_VERSION);

private:
    void HardReset();
    void WriteCommand(u8 value, u64 clocks);
    void WriteRegister(u8 page, u8 reg, u8 value, u64 clocks);
    u8 ReadRegister(u8 page, u8 reg, bool peek) const;
    u8 ReadMemory(u16 address) const;
    u16 ReadMemoryWord(u16 address) const;
    void WriteMemory(u16 address, u8 value);
    u16 TransferData(bool write, u16 value, bool word);
    void AdvanceRemoteDMA(int bytes);
    void StartTransmit(u64 clocks);
    void CompleteTransmit(u64 clocks);
    void ScheduleReceive(u64 clocks);
    void CompleteReceive(u64 clocks);
    bool Receive(const u8* data, int size, u64 clocks);
    void ReceiveLoopback(const u8* data, int size, bool crc_ok);
    bool AcceptsAddress(const u8* destination) const;
    void CountMissed();
    void UpdateIRQ();
    void UpdateEvent();
    u64 GetWireClocks(int size) const;
    void AddHistory(const u8* data, int size, bool sent, u8 status, u64 clocks);
    NO_INLINE void TraceEvent(u8 event, u8 reg, u8 value, const u8* frame, int size, u8 reason);
    void Serialize(StateSerializer& serializer);
    void SanitizeState();

private:
    NE2000_State m_state;
    PIC* m_pic;
    Scheduler* m_scheduler;
    NetworkPort* m_port;
    TraceLogger* m_trace_logger;
    bool m_enabled;
    u16 m_base;
    u16 m_data_port;
    u8 m_irq_line;
    u8 m_address[6];
    NE2000_Frame m_history[NE2000_HISTORY];
    int m_history_count;
    int m_history_next;
    NE2000_Counters m_counters;
    u8 m_frame[NETWORK_FRAME_MAX + 4];
};

static const u16 k_ne2000_bases[4] = { 0x300, 0x320, 0x340, 0x360 };
static const u8 k_ne2000_irqs[8] = { 3, 4, 5, 9, 10, 11, 12, 15 };
static const u16 k_ne2000_default_base = 0x300;
static const u8 k_ne2000_default_irq = 10;

static inline int find_network_base(int base)
{
    for (int i = 0; i < 4; i++)
    {
        if (k_ne2000_bases[i] == base)
            return i;
    }

    return -1;
}

static inline int find_network_irq(int irq)
{
    for (int i = 0; i < 8; i++)
    {
        if (k_ne2000_irqs[i] == irq)
            return i;
    }

    return -1;
}

#include "ne2000_inline.h"

#endif /* NE2000_H */
