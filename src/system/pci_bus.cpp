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

#include "pci_bus.h"
#include "../common/trace_logger.h"
#include "../common/state_serializer.h"

PciBus::PciBus()
{
    m_trace_logger = TraceLogger::GetDisabled();
    memset(&m_state, 0, sizeof(m_state));
    m_enabled = false;

    for (int i = 0; i < PCI_BUS_DEVICES; i++)
    {
        InitPointer(m_contexts[i]);
        InitPointer(m_reads[i]);
        InitPointer(m_writes[i]);
    }
}

PciBus::~PciBus()
{
}

void PciBus::Init()
{
    Reset();
}

void PciBus::Reset()
{
    memset(&m_state, 0, sizeof(m_state));
}

void PciBus::SetEnabled(bool enabled)
{
    m_enabled = enabled;
}

bool PciBus::IsEnabled() const
{
    return m_enabled;
}

void PciBus::RegisterDevice(int device, void* context, GDOX_Pci_Read_Fn read, GDOX_Pci_Write_Fn write)
{
    if (device < 0 || device >= PCI_BUS_DEVICES)
        return;

    m_contexts[device] = context;
    m_reads[device] = read;
    m_writes[device] = write;
}

bool PciBus::Decodes(u16 port) const
{
    return m_enabled && (port & 0xFFFC) == k_pci_bus_data_port;
}

u32 PciBus::ReadAddress() const
{
    return m_state.address;
}

void PciBus::WriteAddress(u32 value)
{
    m_state.address = value & k_pci_bus_address_mask;
}

int PciBus::Select(u16 port, u8& function, u8& offset) const
{
    u32 address = m_state.address;

    if ((address & k_pci_bus_enable) == 0 || ((address >> 16) & 0xFF) != 0)
        return -1;

    int device = (int)((address >> 11) & 0x1F);

    if (device > k_pci_bus_last_device || !IsValidPointer(m_reads[device]))
        return -1;

    function = (u8)((address >> 8) & 0x07);
    offset = (u8)((address & 0xFC) | (port & 0x03));
    return device;
}

void PciBus::SetTraceLogger(TraceLogger* trace_logger)
{
    m_trace_logger = TraceLogger::Resolve(trace_logger);
}

u8 PciBus::ReadData(u16 port) const
{
    u8 function = 0;
    u8 offset = 0;
    int device = Select(port, function, offset);
    u8 value = device >= 0 ? m_reads[device](m_contexts[device], function, offset) : 0xFF;

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_SYSTEM, TRACE_SYSTEM_PCI)))
        TraceAccess(device, function, offset, value, true);

    return value;
}

void PciBus::TraceAccess(int device, u8 function, u8 offset, u8 value, bool read) const
{
    u32 address = m_state.address;
    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_SYSTEM, TRACE_SYSTEM_PCI);
    entry->flags |= (read ? TRACE_SYSTEM_FLAG_READ : 0) | (device < 0 ? TRACE_SYSTEM_FLAG_NO_DEVICE : 0);
    entry->system.bus = (u8)((address >> 16) & 0xFF);
    entry->system.device = (u8)((address >> 11) & 0x1F);
    entry->system.function = (u8)((address >> 8) & 0x07);
    entry->system.address = device >= 0 ? offset : (u8)(address & 0xFC);
    entry->system.data = value;
    entry->system.raw = 1;
    UNUSED(function);
}

void PciBus::WriteData(u16 port, u8 value)
{
    u8 function = 0;
    u8 offset = 0;
    int device = Select(port, function, offset);

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_SYSTEM, TRACE_SYSTEM_PCI)))
        TraceAccess(device, function, offset, value, false);

    if (device >= 0 && IsValidPointer(m_writes[device]))
        m_writes[device](m_contexts[device], function, offset, value);
}

bool PciBus::HasDevice(int device) const
{
    return device >= 0 && device <= k_pci_bus_last_device && IsValidPointer(m_reads[device]);
}

u8 PciBus::PeekConfig(int device, u8 function, u8 offset) const
{
    return HasDevice(device) ? m_reads[device](m_contexts[device], function & 0x07, offset) : 0xFF;
}

PciBus::PciBus_State* PciBus::GetState()
{
    return &m_state;
}

void PciBus::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
}

void PciBus::LoadState(std::istream& stream, int version)
{
    UNUSED(version);
    StateSerializer serializer(stream);
    Serialize(serializer);
    m_state.address &= k_pci_bus_address_mask;
}

void PciBus::Serialize(StateSerializer& serializer)
{
    G_SERIALIZE(serializer, m_state.address);
}
