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

#include "dma.h"
#include "../common/trace_logger.h"
#include "memory.h"
#include "scheduler.h"
#include "../common/state_serializer.h"

DMA::DMA()
{
    InitPointer(m_memory);
    InitPointer(m_scheduler);
    InitPointer(m_trace_logger);
    InitPointer(m_debug_callback);
    InitPointer(m_debugger);
    memset(m_endpoints, 0, sizeof(m_endpoints));
    m_unsupported_logged = false;
    memset(&m_state, 0, sizeof(m_state));
    m_state.active_channel = -1;
}

DMA::~DMA()
{
}

void DMA::Init(Memory* memory, Scheduler* scheduler)
{
    m_memory = memory;
    m_scheduler = scheduler;
    Reset();
}

void DMA::Reset()
{
    memset(&m_state, 0, sizeof(m_state));
    m_state.active_channel = -1;
    m_chips[0].Reset();
    m_chips[1].Reset();
    UpdateNextEvent();
}

void DMA::SetTraceLogger(TraceLogger* trace_logger)
{
    m_trace_logger = trace_logger;
}

void DMA::SetDebugCallback(GDOX_DMA_Debug_Fn callback, void* debugger)
{
    m_debug_callback = callback;
    m_debugger = debugger;
}

void DMA::SetEndpoint(int channel, const GDOX_DMA_Endpoint& endpoint)
{
    m_endpoints[channel & 0x07] = endpoint;
}

u8 DMA::Read(u16 port)
{
    if (port <= 0x000F)
        return m_chips[0].Read(port);

    if (port <= 0x008F)
        return m_state.pages[port & 0x0F];

    return m_chips[1].Read((port >> 1) & 0x0F);
}

void DMA::Write(u16 port, u8 value)
{
    int chip = port >= 0x00C0 ? 1 : 0;
    int reg = chip != 0 ? (port >> 1) & 0x0F : port & 0x0F;

    if (port >= 0x0081 && port <= 0x008F)
        m_state.pages[port & 0x0F] = value;
    else
    {
        m_chips[chip].Write(reg, value);

        // Clearing chip 2 also takes channel 4 away from chip 1
        if (reg == k_i8237_reg_master_clear && (chip != 0 || m_state.active_channel < k_dma_cascade_channel))
            m_state.active_channel = -1;

        m_state.stalled = 0;
        UpdateNextEvent();
        CheckUnsupported();
    }

    if (unlikely(IsValidPointer(m_trace_logger) && m_trace_logger->IsEventEnabled(TRACE_DMA, TRACE_DMA_REGISTER)))
        TraceWrite(port, chip, reg, value);
}

// The DREQ line of a channel. Devices never drive channel 4, chip 1 does through the cascade
void DMA::SetRequest(int channel, bool active)
{
    channel &= 0x07;

    I8237& chip = GetChip(channel);
    int index = channel & 0x03;
    u8 bit = (u8)(1 << channel);

    if (active && (chip.GetState()->request_levels & (1 << index)) == 0 && IsValidPointer(m_trace_logger) &&
        m_trace_logger->IsEventEnabled(TRACE_DMA, TRACE_DMA_REQUEST))
        TraceEvent(TRACE_DMA_REQUEST, channel, 0, 0, false);

    chip.SetRequestLine(index, active);

    if (active)
        m_state.stalled &= ~bit;
    else if (m_state.active_channel == channel && chip.GetService(index) != k_i8237_block)
        m_state.active_channel = -1;

    UpdateNextEvent();
}

// Single mode gives the bus back after each unit, a block or demand run keeps it and skips S1 from its second unit
u32 DMA::HandleEvent(u64 clocks)
{
    int channel = GetServiceChannel();

    if (channel < 0)
    {
        m_state.active_channel = -1;
        UpdateNextEvent();
        return 0;
    }

    u32 unit = m_state.active_channel == channel ? k_dma_repeat_clocks : k_dma_unit_clocks;
    bool terminal = false;

    if (!TransferUnit(channel, clocks, terminal))
    {
        // The device holds its request without data, so wait until it asserts it again
        m_state.stalled |= (u8)(1 << channel);
        m_state.active_channel = -1;
        UpdateNextEvent();
        return 0;
    }

    I8237& chip = GetChip(channel);
    int index = channel & 0x03;
    u8 service = chip.GetService(index);
    bool hold = !terminal && (service == k_i8237_block || (service == k_i8237_demand && chip.IsRequesting(index)));

    m_state.active_channel = hold ? (s8)channel : -1;

    if (terminal)
        FinishService(channel);

    m_state.next_clocks = clocks + unit + (hold ? 0 : k_dma_release_clocks);
    UpdateNextEvent();
    return unit;
}

// Cascade channels other than channel 4 would be ISA bus masters, and a stalled channel waits for its device
u8 DMA::GetExcluded(int chip) const
{
    u8 cascade = m_chips[chip].GetCascadeChannels();
    u8 stalled = (u8)(m_state.stalled >> (chip * 4)) & 0x0F;

    return (u8)(stalled | (chip != 0 ? cascade & ~0x01 : cascade));
}

bool DMA::IsHolding(int channel) const
{
    const I8237& chip = GetChip(channel);
    int index = channel & 0x03;

    if (!chip.IsEnabled() || (m_state.stalled & (1 << channel)) != 0)
        return false;

    return chip.GetService(index) == k_i8237_block || chip.IsRequesting(index);
}

// Chip 2 decides first. Channels 0-3 only reach the bus through channel 4, which beats 5-7 when chip 1 asks for it
int DMA::GetServiceChannel() const
{
    if (m_state.active_channel >= 0 && IsHolding(m_state.active_channel))
        return m_state.active_channel;

    int channel = m_chips[1].GetServiceChannel(GetExcluded(1));

    if (channel < 0)
        return -1;

    if (channel != 0)
        return channel + k_dma_cascade_channel;

    if (m_chips[1].GetService(0) != k_i8237_cascade)
        return -1;

    return m_chips[0].GetServiceChannel(GetExcluded(0));
}

// Without a device the bus floats, as with a software request. Verify runs the unit past the device and touches no memory
bool DMA::TransferUnit(int channel, u64 clocks, bool& terminal)
{
    I8237& chip = GetChip(channel);
    const GDOX_DMA_Endpoint& endpoint = m_endpoints[channel];
    int index = channel & 0x03;
    bool word = channel >= k_dma_cascade_channel;
    u8 transfer = chip.GetTransfer(index);
    u32 address = GetPhysicalAddress(channel);
    GDOX_Bus_Access_Context context = { };
    context.clocks = clocks;
    context.origin = GDOX_BUS_ORIGIN_DMA;
    u16 value = 0xFFFF;

    if (transfer == k_i8237_memory_to_io)
    {
        value = m_memory->Read8DMA(address, context);

        if (word)
            value |= (u16)(m_memory->Read8DMA(address + 1, context) << 8);

        if (IsValidPointer(endpoint.write) && !endpoint.write(endpoint.device, value, word))
            return false;
    }
    else
    {
        if (IsValidPointer(endpoint.read) && !endpoint.read(endpoint.device, value, word))
            return false;

        if (transfer == k_i8237_io_to_memory)
        {
            m_memory->Write8DMA(address, (u8)value, context);

            if (word)
                m_memory->Write8DMA(address + 1, (u8)(value >> 8), context);
        }
    }

#if !defined(GDOX_DISABLE_DISASSEMBLER)
    if (IsValidPointer(m_debug_callback) && transfer != k_i8237_verify)
        m_debug_callback(m_debugger, address, word ? 2 : 1, transfer == k_i8237_io_to_memory);
#endif

    terminal = chip.Advance(index);
    return true;
}

// The end callback is the I/O channel's T/C line, a pulse that also comes with auto-init
void DMA::FinishService(int channel)
{
    if (IsValidPointer(m_trace_logger) && m_trace_logger->IsEventEnabled(TRACE_DMA, TRACE_DMA_END))
        TraceEvent(TRACE_DMA_END, channel, 0, 0, true);

    GetChip(channel).Terminate(channel & 0x03);

    if (IsValidPointer(m_endpoints[channel].end))
        m_endpoints[channel].end(m_endpoints[channel].device);
}

// Chip 1's HRQ drives chip 2's DREQ0
void DMA::UpdateCascade()
{
    m_chips[1].SetRequestLine(0, m_chips[0].GetServiceChannel(GetExcluded(0)) >= 0);
}

void DMA::UpdateNextEvent()
{
    UpdateCascade();

    u64 next = GDOX_NO_EVENT;

    if (GetServiceChannel() >= 0)
        next = MAX(m_state.next_clocks, m_scheduler->GetClocks());

    m_scheduler->Schedule(SCHEDULER_EVENT_DMA, next);
}

// Memory to memory, rotating priority and the rest are stored but not run. They are logged, as is a request on chip 1
// that channel 4, the cascade, holds back
void DMA::CheckUnsupported()
{
    if (m_unsupported_logged)
        return;

    for (int i = 0; i < DMA_CHIPS; i++)
    {
        u8 command = m_chips[i].GetState()->command;

        if ((command & ~k_i8237_command_disable) != 0)
        {
            Debug("DMA: unsupported command %02X on chip %d", command, i + 1);
            m_unsupported_logged = true;
            return;
        }
    }

    for (int i = 0; i < DMA_CHANNELS; i++)
    {
        const I8237& chip = GetChip(i);

        if (chip.GetTransfer(i & 0x03) == k_i8237_illegal && chip.GetService(i & 0x03) != k_i8237_cascade)
        {
            Debug("DMA: illegal transfer in mode %02X on channel %d", chip.GetState()->channels[i & 0x03].mode, i);
            m_unsupported_logged = true;
            return;
        }
    }

    if (m_chips[0].GetServiceChannel(GetExcluded(0)) >= 0 && GetServiceChannel() < 0)
    {
        Debug("DMA: channel 4 holds back a request on chip 1, mode %02X, mask %X",
            m_chips[1].GetState()->channels[0].mode, m_chips[1].GetState()->mask);
        m_unsupported_logged = true;
    }
}

// Channel registers and the writes that carry a channel report it, the rest report the chip's first one
void DMA::TraceWrite(u16 port, int chip, int reg, u8 value)
{
    if (port >= 0x0081 && port <= 0x008F)
    {
        for (int i = 0; i < DMA_CHANNELS; i++)
        {
            if (k_dma_channel_pages[i] == (port & 0x0F))
            {
                TraceEvent(TRACE_DMA_REGISTER, i, (u8)(0x10 | (port & 0x0F)), value, false);
                return;
            }
        }

        TraceEvent(TRACE_DMA_REGISTER, 0, (u8)(0x10 | (port & 0x0F)), value, false);
        return;
    }

    int channel = 0;

    if (reg < k_i8237_reg_status)
        channel = reg >> 1;
    else if (reg == k_i8237_reg_request || reg == k_i8237_reg_single_mask || reg == k_i8237_reg_mode)
        channel = value & 0x03;

    TraceEvent(TRACE_DMA_REGISTER, (chip * 4) + channel, (u8)reg, value, false);
}

void DMA::TraceEvent(u8 event, int channel, u8 reg, u8 value, bool terminal)
{
    const I8237::I8237_Channel& state = GetChip(channel).GetState()->channels[channel & 0x03];
    GDOX_Trace_Entry entry = {};
    entry.type = TRACE_DMA;
    entry.event = event;
    entry.dma.address = GetPhysicalAddress(channel);
    entry.dma.count = state.current_count;
    entry.dma.channel = (u8)channel;
    entry.dma.mode = state.mode;
    entry.dma.reg = reg;
    entry.dma.value = value;
    entry.dma.terminal = terminal ? 1 : 0;
    entry.dma.mask = (u8)((m_chips[1].GetState()->mask << 4) | m_chips[0].GetState()->mask);
    m_trace_logger->TraceLog(entry);
}

void DMA::SaveState(std::ostream& stream)
{
    m_chips[0].SaveState(stream);
    m_chips[1].SaveState(stream);

    StateSerializer serializer(stream);
    Serialize(serializer, GDOX_SAVESTATE_VERSION);
}

void DMA::LoadState(std::istream& stream, int version)
{
    m_chips[0].LoadState(stream);
    m_chips[1].LoadState(stream);

    StateSerializer serializer(stream);
    Serialize(serializer, version);
    SanitizeState();
}

void DMA::Serialize(StateSerializer& serializer, int version)
{
    UNUSED(version);
    G_SERIALIZE_ARRAY(serializer, m_state.pages, DMA_PAGES);
    G_SERIALIZE(serializer, m_state.stalled);
    G_SERIALIZE(serializer, m_state.active_channel);
    G_SERIALIZE(serializer, m_state.next_clocks);
}

void DMA::SanitizeState()
{
    if (m_state.active_channel < -1 || m_state.active_channel >= DMA_CHANNELS ||
        m_state.active_channel == k_dma_cascade_channel)
        m_state.active_channel = -1;

    UpdateNextEvent();
}
