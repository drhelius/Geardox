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

#include "ne2000.h"
#include "network_port.h"
#include "../media/crc.h"
#include "../system/pic.h"
#include "../system/scheduler.h"
#include "../common/trace_logger.h"
#include "../common/state_serializer.h"

static const u8 k_ne2000_cr_stp = 0x01;
static const u8 k_ne2000_cr_sta = 0x02;
static const u8 k_ne2000_cr_txp = 0x04;
static const u8 k_ne2000_cr_rd_mask = 0x38;
static const u8 k_ne2000_cr_rd_read = 0x08;
static const u8 k_ne2000_cr_rd_write = 0x10;
static const u8 k_ne2000_cr_rd_send = 0x18;
static const u8 k_ne2000_cr_rd_abort = 0x20;

static const u8 k_ne2000_isr_prx = 0x01;
static const u8 k_ne2000_isr_ptx = 0x02;
static const u8 k_ne2000_isr_rxe = 0x04;
static const u8 k_ne2000_isr_ovw = 0x10;
static const u8 k_ne2000_isr_cnt = 0x20;
static const u8 k_ne2000_isr_rdc = 0x40;
static const u8 k_ne2000_isr_rst = 0x80;
static const u8 k_ne2000_isr_ints = 0x7F;

static const u8 k_ne2000_dcr_wts = 0x01;

static const u8 k_ne2000_tcr_crc = 0x01;
static const u8 k_ne2000_tcr_lb_mask = 0x06;

static const u8 k_ne2000_rcr_ar = 0x02;
static const u8 k_ne2000_rcr_ab = 0x04;
static const u8 k_ne2000_rcr_am = 0x08;
static const u8 k_ne2000_rcr_pro = 0x10;
static const u8 k_ne2000_rcr_mon = 0x20;

static const u8 k_ne2000_rsr_prx = 0x01;
static const u8 k_ne2000_rsr_crc = 0x02;
static const u8 k_ne2000_rsr_mpa = 0x10;
static const u8 k_ne2000_rsr_phy = 0x20;
static const u8 k_ne2000_rsr_dis = 0x40;

static const u8 k_ne2000_tsr_ptx = 0x01;
static const u8 k_ne2000_tsr_loopback[4] = { 0x01, 0x51, 0x41, 0x01 };

static const int k_ne2000_min_frame = 60;
static const int k_ne2000_wire_overhead = 8 + 4 + 12;
static const u32 k_ne2000_wire_bytes_per_second = 1250000;
static const u8 k_ne2000_counter_limit = 0xC0;

static void append_fcs(u8* frame, int size)
{
    write_u32_le(frame + size, CalculateCRC32(0, frame, size));
}

static bool check_fcs(const u8* frame, int size)
{
    return (size >= 4) && (CalculateCRC32(0, frame, size - 4) == read_u32_le(frame + size - 4));
}

static u32 multicast_hash(const u8* address)
{
    u32 crc = 0xFFFFFFFF;

    for (int i = 0; i < 6; i++)
    {
        u8 value = address[i];

        for (int bit = 0; bit < 8; bit++)
        {
            u32 carry = (crc >> 31) ^ ((value >> bit) & 1);
            crc <<= 1;

            if (carry)
                crc = (crc ^ 0x04C11DB6) | carry;
        }
    }

    return (crc >> 26) & 0x3F;
}

NE2000::NE2000()
{
    InitPointer(m_pic);
    InitPointer(m_scheduler);
    InitPointer(m_port);
    m_trace_logger = TraceLogger::GetDisabled();
    m_enabled = false;
    m_base = 0xF000;
    m_data_port = 0xFFFF;
    m_irq_line = k_ne2000_default_irq;
    memset(&m_state, 0, sizeof(m_state));
    memset(m_address, 0, sizeof(m_address));
    memset(m_history, 0, sizeof(m_history));
    memset(&m_counters, 0, sizeof(m_counters));
    memset(m_frame, 0, sizeof(m_frame));
    m_history_count = 0;
    m_history_next = 0;
    m_state.transmit_end = GDOX_NO_EVENT;
    m_state.receive_end = GDOX_NO_EVENT;
}

NE2000::~NE2000()
{
}

void NE2000::Init(PIC* pic, Scheduler* scheduler, NetworkPort* port)
{
    m_pic = pic;
    m_scheduler = scheduler;
    m_port = port;
}

void NE2000::SetTraceLogger(TraceLogger* trace_logger)
{
    m_trace_logger = TraceLogger::Resolve(trace_logger);
}

void NE2000::SetConfig(bool enabled, u16 base, u8 irq)
{
    if (m_enabled && m_state.irq)
        m_pic->SetIRQLine(m_irq_line, false);

    m_enabled = enabled;
    m_base = enabled ? base : 0xF000;
    m_data_port = enabled ? (u16)(base + 0x10) : 0xFFFF;
    m_irq_line = irq;
    m_state.irq = false;
    m_port->SetAttached(enabled);
}

void NE2000::SetAddress(const u8* address)
{
    memcpy(m_address, address, 6);
}

void NE2000::Reset()
{
    memset(m_state.prom, 0, sizeof(m_state.prom));
    memcpy(m_state.prom, m_address, 6);
    m_state.prom[14] = 0x57;
    m_state.prom[15] = 0x57;

    memset(m_state.ram, 0, sizeof(m_state.ram));
    memset(m_state.par, 0, sizeof(m_state.par));
    memset(m_state.mar, 0, sizeof(m_state.mar));
    m_state.pstart = 0;
    m_state.pstop = 0;
    m_state.bnry = 0;
    m_state.curr = 0;
    m_state.tpsr = 0;
    m_state.tbcr = 0;
    m_state.rsar = 0;
    m_state.rbcr = 0;
    m_state.clda = 0;
    m_state.rnpp = 0;
    m_state.lnpp = 0;
    m_state.rcr = 0;
    m_state.rsr = 0;
    m_state.tsr = 0;
    m_state.ncr = 0;
    m_state.cntr[0] = 0;
    m_state.cntr[1] = 0;
    m_state.cntr[2] = 0;
    memset(m_state.fifo, 0, sizeof(m_state.fifo));
    m_state.fifo_read = 0;
    m_state.wire_free = 0;
    m_state.transmit_end = GDOX_NO_EVENT;
    m_state.receive_end = GDOX_NO_EVENT;
    HardReset();
    m_port->Clear();
    m_history_count = 0;
    m_history_next = 0;
    memset(&m_counters, 0, sizeof(m_counters));
}

void NE2000::HardReset()
{
    m_state.cr = k_ne2000_cr_stp | k_ne2000_cr_rd_abort;
    m_state.isr = k_ne2000_isr_rst;
    m_state.imr = 0;
    m_state.dcr = 0x04;
    m_state.tcr = 0;
    m_state.send_packet = false;
    m_state.send_end = 0;
    m_state.transmit_end = GDOX_NO_EVENT;

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_NET, TRACE_NET_RESET)))
        TraceEvent(TRACE_NET_RESET, 0, 0, NULL, 0, 0);

    UpdateIRQ();
    UpdateEvent();
}

bool NE2000::IsEnabled() const
{
    return m_enabled;
}

u16 NE2000::GetBase() const
{
    return m_enabled ? m_base : 0;
}

u8 NE2000::GetIRQ() const
{
    return m_irq_line;
}

u8 NE2000::Read(u16 port, u64 clocks)
{
    u8 offset = (u8)(port - m_base);

    if (offset >= 0x18)
    {
        HardReset();
        return 0xFF;
    }

    if (offset >= 0x10)
        return (u8)TransferData(false, 0, false);

    u8 page = m_state.cr >> 6;
    u8 value = ReadRegister(page, offset, false);

    if ((page == 0) && (offset >= 0x0D))
        m_state.cntr[offset - 0x0D] = 0;
    else if ((page == 0) && (offset == 0x06))
        m_state.fifo_read = (u8)((m_state.fifo_read + 1) & (NE2000_FIFO_SIZE - 1));

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_NET, TRACE_NET_READ)))
        TraceEvent(TRACE_NET_READ, offset, value, NULL, 0, 0);

    UNUSED(clocks);
    return value;
}

u8 NE2000::Peek(u16 port) const
{
    u8 offset = (u8)(port - m_base);

    if (offset >= 0x10)
        return 0xFF;

    return ReadRegister(m_state.cr >> 6, offset, true);
}

void NE2000::Write(u16 port, u8 value, u64 clocks)
{
    u8 offset = (u8)(port - m_base);

    if (offset >= 0x18)
    {
        HardReset();
        return;
    }

    if (offset >= 0x10)
    {
        TransferData(true, value, false);
        return;
    }

    u8 page = m_state.cr >> 6;

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_NET, TRACE_NET_WRITE)))
        TraceEvent(TRACE_NET_WRITE, offset, value, NULL, 0, page);

    if (offset == 0)
        WriteCommand(value, clocks);
    else
        WriteRegister(page, offset, value, clocks);

    UpdateIRQ();
    UpdateEvent();
}

u16 NE2000::ReadData16(u64 clocks)
{
    UNUSED(clocks);
    return TransferData(false, 0, true);
}

void NE2000::WriteData16(u16 value, u64 clocks)
{
    UNUSED(clocks);
    TransferData(true, value, true);
}

u64 NE2000::GetReadStableUntil(u16 port) const
{
    u8 offset = (u8)(port - m_base);

    if (((m_state.cr >> 6) != 0) || ((offset != 0x00) && (offset != 0x07)))
        return 0;

    return MIN(m_state.transmit_end, m_state.receive_end);
}

void NE2000::WriteCommand(u8 value, u64 clocks)
{
    u8 previous = m_state.cr;
    u8 dma = value & k_ne2000_cr_rd_mask;
    m_state.cr = (value & ~k_ne2000_cr_txp) | (previous & k_ne2000_cr_txp);

    if ((value & k_ne2000_cr_stp) && (previous & k_ne2000_cr_sta))
        m_state.cr |= k_ne2000_cr_sta;

    if (value & k_ne2000_cr_stp)
        m_state.isr |= k_ne2000_isr_rst;
    else if (value & k_ne2000_cr_sta)
        m_state.isr &= ~k_ne2000_isr_rst;

    if (dma != 0)
        m_state.send_packet = false;

    if ((dma == k_ne2000_cr_rd_read) || (dma == k_ne2000_cr_rd_write))
    {
        if (unlikely(m_trace_logger->IsEventEnabled(TRACE_NET, TRACE_NET_DMA)))
            TraceEvent(TRACE_NET_DMA, 0, dma, NULL, 0, 0);

        if (m_state.rbcr == 0)
        {
            m_state.isr |= k_ne2000_isr_rdc;
            m_state.cr = (m_state.cr & ~k_ne2000_cr_rd_mask) | k_ne2000_cr_rd_abort;
        }
    }
    else if (dma == k_ne2000_cr_rd_send)
    {
        u16 header = (u16)(m_state.bnry << 8);
        m_state.rsar = header;
        m_state.rbcr = (u16)(ReadMemory((u16)(header + 2)) | (ReadMemory((u16)(header + 3)) << 8));
        m_state.send_end = ReadMemory((u16)(header + 1));
        m_state.send_packet = true;
        m_state.cr = (m_state.cr & ~k_ne2000_cr_rd_mask) | k_ne2000_cr_rd_read;

        if (unlikely(m_trace_logger->IsEventEnabled(TRACE_NET, TRACE_NET_DMA)))
            TraceEvent(TRACE_NET_DMA, 0, dma, NULL, 0, 0);

        if (m_state.rbcr == 0)
            AdvanceRemoteDMA(0);
    }

    if ((value & k_ne2000_cr_txp) && !(value & k_ne2000_cr_stp) && (m_state.cr & k_ne2000_cr_sta) &&
        !(previous & k_ne2000_cr_txp))
        StartTransmit(clocks);
}

void NE2000::WriteRegister(u8 page, u8 reg, u8 value, u64 clocks)
{
    UNUSED(clocks);

    if (page == 0)
    {
        switch (reg)
        {
            case 0x01:
                m_state.pstart = value;
                break;
            case 0x02:
                m_state.pstop = value;
                break;
            case 0x03:
                m_state.bnry = value;
                break;
            case 0x04:
                m_state.tpsr = value;
                break;
            case 0x05:
                m_state.tbcr = (m_state.tbcr & 0xFF00) | value;
                break;
            case 0x06:
                m_state.tbcr = (m_state.tbcr & 0x00FF) | (u16)(value << 8);
                break;
            case 0x07:
                m_state.isr &= ~(value & k_ne2000_isr_ints);
                break;
            case 0x08:
                m_state.rsar = (m_state.rsar & 0xFF00) | value;
                break;
            case 0x09:
                m_state.rsar = (m_state.rsar & 0x00FF) | (u16)(value << 8);
                break;
            case 0x0A:
                m_state.rbcr = (m_state.rbcr & 0xFF00) | value;
                break;
            case 0x0B:
                m_state.rbcr = (m_state.rbcr & 0x00FF) | (u16)(value << 8);
                break;
            case 0x0C:
                m_state.rcr = value & 0x3F;
                m_state.rsr = (m_state.rsr & ~k_ne2000_rsr_dis) | ((value & k_ne2000_rcr_mon) ? k_ne2000_rsr_dis : 0);
                break;
            case 0x0D:
                m_state.tcr = value & 0x1F;
                break;
            case 0x0E:
                m_state.dcr = value & 0x7F;
                break;
            case 0x0F:
                m_state.imr = value & 0x7F;
                break;
            default:
                break;
        }
    }
    else if (page == 1)
    {
        if (reg <= 0x06)
            m_state.par[reg - 1] = value;
        else if (reg == 0x07)
            m_state.curr = value;
        else
            m_state.mar[reg - 0x08] = value;
    }
    else if (page == 2)
    {
        switch (reg)
        {
            case 0x01:
                m_state.clda = (m_state.clda & 0xFF00) | value;
                break;
            case 0x02:
                m_state.clda = (m_state.clda & 0x00FF) | (u16)(value << 8);
                break;
            case 0x03:
                m_state.rnpp = value;
                break;
            case 0x05:
                m_state.lnpp = value;
                break;
            case 0x06:
                m_state.clda = (m_state.clda & 0x00FF) | (u16)(value << 8);
                break;
            case 0x07:
                m_state.clda = (m_state.clda & 0xFF00) | value;
                break;
            default:
                break;
        }
    }
}

u8 NE2000::ReadRegister(u8 page, u8 reg, bool peek) const
{
    UNUSED(peek);

    if (reg == 0x00)
        return m_state.cr;

    switch (page)
    {
        case 0:
            switch (reg)
            {
                case 0x01: return (u8)m_state.clda;
                case 0x02: return (u8)(m_state.clda >> 8);
                case 0x03: return m_state.bnry;
                case 0x04: return m_state.tsr;
                case 0x05: return m_state.ncr;
                case 0x06: return m_state.fifo[m_state.fifo_read];
                case 0x07: return m_state.isr;
                case 0x08: return (u8)m_state.rsar;
                case 0x09: return (u8)(m_state.rsar >> 8);
                case 0x0C: return m_state.rsr;
                case 0x0D: return m_state.cntr[0];
                case 0x0E: return m_state.cntr[1];
                case 0x0F: return m_state.cntr[2];
                default: return 0xFF;
            }
        case 1:
            if (reg <= 0x06)
                return m_state.par[reg - 1];

            if (reg == 0x07)
                return m_state.curr;

            return m_state.mar[reg - 0x08];
        case 2:
            switch (reg)
            {
                case 0x01: return m_state.pstart;
                case 0x02: return m_state.pstop;
                case 0x03: return m_state.rnpp;
                case 0x04: return m_state.tpsr;
                case 0x05: return m_state.lnpp;
                case 0x06: return (u8)(m_state.clda >> 8);
                case 0x07: return (u8)m_state.clda;
                case 0x0C: return m_state.rcr | 0xC0;
                case 0x0D: return m_state.tcr | 0xE0;
                case 0x0E: return m_state.dcr | 0x80;
                case 0x0F: return m_state.imr | 0x80;
                default: return 0xFF;
            }
        default:
            return 0xFF;
    }
}

INLINE u8 NE2000::ReadMemory(u16 address) const
{
    address &= 0x7FFF;

    if (address < NE2000_RAM_START)
        return m_state.prom[(address >> 1) & (NE2000_PROM_SIZE - 1)];

    return m_state.ram[address - NE2000_RAM_START];
}

INLINE u16 NE2000::ReadMemoryWord(u16 address) const
{
    address &= 0x7FFE;

    if (address < NE2000_RAM_START)
        return m_state.prom[(address >> 1) & (NE2000_PROM_SIZE - 1)];

    address -= NE2000_RAM_START;
    return (u16)(m_state.ram[address] | (m_state.ram[address + 1] << 8));
}

INLINE void NE2000::WriteMemory(u16 address, u8 value)
{
    address &= 0x7FFF;

    if (address >= NE2000_RAM_START)
        m_state.ram[address - NE2000_RAM_START] = value;
}

u16 NE2000::TransferData(bool write, u16 value, bool word)
{
    u8 dma = m_state.cr & k_ne2000_cr_rd_mask;
    bool active = (dma == k_ne2000_cr_rd_read) || (dma == k_ne2000_cr_rd_write);

    if (!active || (m_state.rbcr == 0) || (write != (dma == k_ne2000_cr_rd_write)))
        return write ? 0 : (word ? 0xFFFF : 0xFF);

    bool wide = (m_state.dcr & k_ne2000_dcr_wts) != 0;
    u16 result = 0;

    if (wide)
    {
        u16 address = m_state.rsar & 0xFFFE;
        m_state.rsar = address;

        if (write)
        {
            WriteMemory(address, (u8)value);
            WriteMemory((u16)(address + 1), (u8)(value >> 8));
        }
        else
            result = ReadMemoryWord(address);

        AdvanceRemoteDMA(2);
        return result;
    }

    int bytes = word ? 2 : 1;

    for (int i = 0; i < bytes; i++)
    {
        if (m_state.rbcr == 0)
            break;

        if (write)
            WriteMemory(m_state.rsar, (u8)(value >> (i * 8)));
        else
            result |= (u16)(ReadMemory(m_state.rsar) << (i * 8));

        AdvanceRemoteDMA(1);
    }

    return result;
}

void NE2000::AdvanceRemoteDMA(int bytes)
{
    m_state.rsar = (u16)(m_state.rsar + bytes);

    if ((m_state.pstop != 0) && (m_state.rsar == (u16)(m_state.pstop << 8)))
        m_state.rsar = (u16)(m_state.pstart << 8);

    m_state.rbcr = (u16)(m_state.rbcr > bytes ? m_state.rbcr - bytes : 0);

    if (m_state.rbcr != 0)
        return;

    m_state.isr |= k_ne2000_isr_rdc;
    m_state.cr = (m_state.cr & ~k_ne2000_cr_rd_mask) | k_ne2000_cr_rd_abort;

    if (m_state.send_packet)
    {
        m_state.send_packet = false;
        m_state.bnry = (u8)m_state.send_end;
    }

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_NET, TRACE_NET_DMA)))
        TraceEvent(TRACE_NET_DMA, 1, 0, NULL, 0, 0);

    UpdateIRQ();
}

void NE2000::StartTransmit(u64 clocks)
{
    int size = MIN((int)m_state.tbcr, NETWORK_FRAME_MAX);
    m_state.cr |= k_ne2000_cr_txp;
    m_state.tsr = 0;
    m_state.ncr = 0;
    m_state.transmit_end = clocks + GetWireClocks(size);
}

void NE2000::CompleteTransmit(u64 clocks)
{
    m_state.transmit_end = GDOX_NO_EVENT;

    int size = MIN((int)m_state.tbcr, NETWORK_FRAME_MAX);
    u16 address = (u16)(m_state.tpsr << 8);

    for (int i = 0; i < size; i++)
        m_frame[i] = ReadMemory((u16)(address + i));

    u8 mode = (m_state.tcr & k_ne2000_tcr_lb_mask) >> 1;
    bool loopback = mode != 0;

    m_state.cr &= ~k_ne2000_cr_txp;
    m_state.tsr = loopback ? k_ne2000_tsr_loopback[mode] : k_ne2000_tsr_ptx;
    m_state.isr |= k_ne2000_isr_ptx;
    m_counters.sent++;

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_NET, TRACE_NET_TRANSMIT)))
        TraceEvent(TRACE_NET_TRANSMIT, 0, loopback ? 1 : 0, m_frame, size, 0);

    AddHistory(m_frame, size, true, loopback ? 1 : 0, clocks);

    if (loopback)
    {
        bool inhibit = (m_state.tcr & k_ne2000_tcr_crc) != 0;
        bool crc_ok = inhibit && check_fcs(m_frame, size);

        if (!inhibit)
        {
            append_fcs(m_frame, size);
            size += 4;
        }

        ReceiveLoopback(m_frame, size, crc_ok);
    }
    else if (size >= NETWORK_FRAME_MIN)
        m_port->Send(m_frame, size, clocks);

    UpdateIRQ();
}

void NE2000::Poll(u64 clocks)
{
    if (m_enabled && (m_state.receive_end == GDOX_NO_EVENT) && m_port->HasReceived())
    {
        ScheduleReceive(clocks);
        UpdateEvent();
    }
}

void NE2000::ScheduleReceive(u64 clocks)
{
    int size = 0;

    if (!IsValidPointer(m_port->PeekReceived(&size)))
    {
        m_state.receive_end = GDOX_NO_EVENT;
        return;
    }

    m_state.receive_end = MAX(clocks, m_state.wire_free) + GetWireClocks(size);
}

void NE2000::CompleteReceive(u64 clocks)
{
    int size = 0;
    const u8* data = m_port->PeekReceived(&size);
    m_state.receive_end = GDOX_NO_EVENT;

    if (!IsValidPointer(data))
        return;

    memcpy(m_frame, data, size);
    m_port->PopReceived();
    m_state.wire_free = clocks;

    if (m_state.tcr & k_ne2000_tcr_lb_mask)
        m_counters.filtered++;
    else
    {
        append_fcs(m_frame, size);
        Receive(m_frame, size + 4, clocks);
    }

    ScheduleReceive(clocks);
}

bool NE2000::Receive(const u8* data, int size, u64 clocks)
{
    bool multicast = (data[0] & 0x01) != 0;

    if ((m_state.cr & k_ne2000_cr_stp) || !(m_state.cr & k_ne2000_cr_sta))
    {
        m_counters.filtered++;

        if (unlikely(m_trace_logger->IsEventEnabled(TRACE_NET, TRACE_NET_DROP)))
            TraceEvent(TRACE_NET_DROP, 0, 0, data, size, TRACE_NET_DROP_STOPPED);

        return false;
    }

    if (!AcceptsAddress(data))
    {
        m_counters.filtered++;

        if (unlikely(m_trace_logger->IsEventEnabled(TRACE_NET, TRACE_NET_DROP)))
            TraceEvent(TRACE_NET_DROP, 0, 0, data, size, TRACE_NET_DROP_ADDRESS);

        return false;
    }

    if ((size < k_ne2000_min_frame + 4) && !(m_state.rcr & k_ne2000_rcr_ar))
    {
        m_counters.runts++;

        if (unlikely(m_trace_logger->IsEventEnabled(TRACE_NET, TRACE_NET_DROP)))
            TraceEvent(TRACE_NET_DROP, 0, 0, data, size, TRACE_NET_DROP_RUNT);

        return false;
    }

    if (m_state.rcr & k_ne2000_rcr_mon)
    {
        CountMissed();
        m_state.rsr = k_ne2000_rsr_mpa | k_ne2000_rsr_dis | (multicast ? k_ne2000_rsr_phy : 0);
        m_state.isr |= k_ne2000_isr_rxe;

        if (unlikely(m_trace_logger->IsEventEnabled(TRACE_NET, TRACE_NET_DROP)))
            TraceEvent(TRACE_NET_DROP, 0, 0, data, size, TRACE_NET_DROP_MONITOR);

        UpdateIRQ();
        return false;
    }

    int ring = (int)m_state.pstop - (int)m_state.pstart;
    int total = size + 4;
    int pages = (total + 255) >> 8;
    int curr = m_state.curr;
    bool valid = (ring > 0) && (curr >= m_state.pstart) && (curr < m_state.pstop);
    int free = valid ? ((int)m_state.bnry - curr + ring) % ring : 0;

    if (free == 0)
        free = ring;

    if (!valid || (pages >= free))
    {
        m_state.isr |= k_ne2000_isr_ovw | k_ne2000_isr_rst | k_ne2000_isr_rxe;
        m_state.rsr = k_ne2000_rsr_mpa | (multicast ? k_ne2000_rsr_phy : 0);
        CountMissed();
        m_counters.overflows++;

        if (unlikely(m_trace_logger->IsEventEnabled(TRACE_NET, TRACE_NET_DROP)))
            TraceEvent(TRACE_NET_DROP, 0, 0, data, size, TRACE_NET_DROP_OVERFLOW);

        UpdateIRQ();
        return false;
    }

    int next = curr + pages;

    if (next >= m_state.pstop)
        next -= ring;

    u8 status = k_ne2000_rsr_prx | (multicast ? k_ne2000_rsr_phy : 0);
    u16 start = (u16)(curr << 8);
    u16 stop = (u16)(m_state.pstop << 8);
    u16 address = start;

    u8 header[4] = { status, (u8)next, (u8)size, (u8)(size >> 8) };

    for (int i = 0; i < total; i++)
    {
        WriteMemory(address, i < 4 ? header[i] : data[i - 4]);
        address++;

        if (address == stop)
            address = (u16)(m_state.pstart << 8);
    }

    m_state.clda = address;
    m_state.curr = (u8)next;
    m_state.rsr = status;
    m_state.isr |= k_ne2000_isr_prx;
    m_counters.received++;

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_NET, TRACE_NET_RECEIVE)))
        TraceEvent(TRACE_NET_RECEIVE, 0, 0, data, size, 0);

    AddHistory(data, size - 4, false, status, clocks);
    UpdateIRQ();
    return true;
}

void NE2000::ReceiveLoopback(const u8* data, int size, bool crc_ok)
{
    bool multicast = (size > 0) && ((data[0] & 0x01) != 0);
    bool accepted = (size >= 6) && AcceptsAddress(data);
    u8 write = 0;

    for (int i = 0; i < size; i++)
        m_state.fifo[write++ & (NE2000_FIFO_SIZE - 1)] = data[i];

    m_state.fifo[write++ & (NE2000_FIFO_SIZE - 1)] = (u8)size;
    m_state.fifo[write++ & (NE2000_FIFO_SIZE - 1)] = (u8)(size >> 8);
    m_state.fifo[write++ & (NE2000_FIFO_SIZE - 1)] = (u8)(size >> 8);
    m_state.fifo_read = 0;
    m_state.rsr = (crc_ok ? k_ne2000_rsr_prx : k_ne2000_rsr_crc) | (multicast ? k_ne2000_rsr_phy : 0);

    if (!crc_ok && accepted)
        m_state.isr |= k_ne2000_isr_rxe;

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_NET, TRACE_NET_RECEIVE)))
        TraceEvent(TRACE_NET_RECEIVE, 0, 1, data, size, 0);
}

bool NE2000::AcceptsAddress(const u8* destination) const
{
    if (m_state.rcr & k_ne2000_rcr_pro)
        return true;

    if (!(destination[0] & 0x01))
        return memcmp(destination, m_state.par, 6) == 0;

    bool broadcast = (destination[0] & destination[1] & destination[2] & destination[3] & destination[4] &
        destination[5]) == 0xFF;

    if (broadcast)
        return (m_state.rcr & k_ne2000_rcr_ab) != 0;

    if (!(m_state.rcr & k_ne2000_rcr_am))
        return false;

    u32 index = multicast_hash(destination);
    return (m_state.mar[index >> 3] & (1 << (index & 7))) != 0;
}

void NE2000::CountMissed()
{
    if (m_state.cntr[2] < k_ne2000_counter_limit)
        m_state.cntr[2]++;

    if (m_state.cntr[2] & 0x80)
        m_state.isr |= k_ne2000_isr_cnt;
}

void NE2000::HandleEvent(u64 clocks)
{
    while (true)
    {
        u64 next = MIN(m_state.transmit_end, m_state.receive_end);

        if (next > clocks)
            break;

        if (m_state.transmit_end == next)
            CompleteTransmit(next);
        else
            CompleteReceive(next);
    }

    UpdateIRQ();
    UpdateEvent();
}

void NE2000::UpdateIRQ()
{
    bool irq = m_enabled && ((m_state.isr & m_state.imr & k_ne2000_isr_ints) != 0);

    if (irq == m_state.irq)
        return;

    m_state.irq = irq;
    m_pic->SetIRQLine(m_irq_line, irq);

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_NET, TRACE_NET_IRQ)))
        TraceEvent(TRACE_NET_IRQ, 0, irq ? 1 : 0, NULL, 0, 0);
}

void NE2000::UpdateEvent()
{
    if (!m_enabled)
        return;

    m_scheduler->Schedule(SCHEDULER_EVENT_NET, MIN(m_state.transmit_end, m_state.receive_end));
}

u64 NE2000::GetWireClocks(int size) const
{
    u64 bytes = (u64)(MAX(size, k_ne2000_min_frame) + k_ne2000_wire_overhead);
    return (bytes * GDOX_MACHINE_CLOCK_RATE + k_ne2000_wire_bytes_per_second - 1) / k_ne2000_wire_bytes_per_second;
}

void NE2000::AddHistory(const u8* data, int size, bool sent, u8 status, u64 clocks)
{
    NE2000_Frame& frame = m_history[m_history_next];
    frame.clocks = clocks;
    frame.length = (u16)size;
    frame.sent = sent;
    frame.status = status;

    if (size >= 14)
    {
        memcpy(frame.destination, data, 6);
        memcpy(frame.source, data + 6, 6);
        frame.type = (u16)((data[12] << 8) | data[13]);
    }
    else
    {
        memset(frame.destination, 0, 6);
        memset(frame.source, 0, 6);
        frame.type = 0;
    }

    m_history_next = (m_history_next + 1) % NE2000_HISTORY;

    if (m_history_count < NE2000_HISTORY)
        m_history_count++;
}

NE2000::NE2000_State* NE2000::GetState()
{
    return &m_state;
}

const u8* NE2000::GetAddress() const
{
    return m_address;
}

const NE2000::NE2000_Frame* NE2000::GetHistory(int* count, int* next) const
{
    *count = m_history_count;
    *next = m_history_next;
    return m_history;
}

const NE2000::NE2000_Counters* NE2000::GetCounters() const
{
    return &m_counters;
}

void NE2000::TraceEvent(u8 event, u8 reg, u8 value, const u8* frame, int size, u8 reason)
{
    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_NET, event);
    entry->net.reg = reg;
    entry->net.value = value;
    entry->net.page = event == TRACE_NET_WRITE ? reason : (u8)(m_state.cr >> 6);
    entry->net.cr = m_state.cr;
    entry->net.isr = m_state.isr;
    entry->net.imr = m_state.imr;
    entry->net.bnry = m_state.bnry;
    entry->net.curr = m_state.curr;
    entry->net.status = m_state.rsr;
    entry->net.reason = reason;
    entry->net.address = m_state.rsar;
    entry->net.count = m_state.rbcr;
    entry->net.length = (u16)size;
    entry->net.type = 0;

    if (IsValidPointer(frame) && (size >= 14))
    {
        memcpy(entry->net.destination, frame, 6);
        memcpy(entry->net.source, frame + 6, 6);
        entry->net.type = (u16)((frame[12] << 8) | frame[13]);
    }
    else
    {
        memset(entry->net.destination, 0, 6);
        memset(entry->net.source, 0, 6);
    }
}

void NE2000::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
}

void NE2000::LoadState(std::istream& stream, int version)
{
    UNUSED(version);
    StateSerializer serializer(stream);
    Serialize(serializer);
    SanitizeState();

    if (m_enabled)
        m_pic->SetIRQLine(m_irq_line, m_state.irq);

    UpdateEvent();
}

void NE2000::Serialize(StateSerializer& serializer)
{
    if (!m_enabled)
        return;

    G_SERIALIZE(serializer, m_state.cr);
    G_SERIALIZE(serializer, m_state.isr);
    G_SERIALIZE(serializer, m_state.imr);
    G_SERIALIZE(serializer, m_state.dcr);
    G_SERIALIZE(serializer, m_state.tcr);
    G_SERIALIZE(serializer, m_state.tsr);
    G_SERIALIZE(serializer, m_state.rcr);
    G_SERIALIZE(serializer, m_state.rsr);
    G_SERIALIZE(serializer, m_state.pstart);
    G_SERIALIZE(serializer, m_state.pstop);
    G_SERIALIZE(serializer, m_state.bnry);
    G_SERIALIZE(serializer, m_state.curr);
    G_SERIALIZE(serializer, m_state.tpsr);
    G_SERIALIZE(serializer, m_state.ncr);
    G_SERIALIZE(serializer, m_state.rnpp);
    G_SERIALIZE(serializer, m_state.lnpp);
    G_SERIALIZE(serializer, m_state.tbcr);
    G_SERIALIZE(serializer, m_state.rsar);
    G_SERIALIZE(serializer, m_state.rbcr);
    G_SERIALIZE(serializer, m_state.clda);
    G_SERIALIZE(serializer, m_state.send_end);
    G_SERIALIZE_ARRAY(serializer, m_state.cntr, 3);
    G_SERIALIZE_ARRAY(serializer, m_state.par, 6);
    G_SERIALIZE_ARRAY(serializer, m_state.mar, 8);
    G_SERIALIZE(serializer, m_state.irq);
    G_SERIALIZE(serializer, m_state.send_packet);
    G_SERIALIZE(serializer, m_state.transmit_end);
    G_SERIALIZE(serializer, m_state.receive_end);
    G_SERIALIZE(serializer, m_state.wire_free);
    G_SERIALIZE_ARRAY(serializer, m_state.fifo, NE2000_FIFO_SIZE);
    G_SERIALIZE(serializer, m_state.fifo_read);
    G_SERIALIZE_ARRAY(serializer, m_state.prom, NE2000_PROM_SIZE);
    G_SERIALIZE_ARRAY(serializer, m_state.ram, NE2000_RAM_SIZE);
}

void NE2000::SanitizeState()
{
    if (!m_enabled)
    {
        m_state.irq = false;
        m_state.transmit_end = GDOX_NO_EVENT;
        m_state.receive_end = GDOX_NO_EVENT;
    }

    m_state.fifo_read &= NE2000_FIFO_SIZE - 1;
    memcpy(m_address, m_state.prom, 6);
}
