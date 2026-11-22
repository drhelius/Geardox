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


#include "system_control.h"
#include "memory.h"
#include "pic.h"
#include "pit.h"
#include "../common/state_serializer.h"
#include "../common/trace_logger.h"
#include "../i386/x87.h"

SystemControl::SystemControl()
{
    InitPointer(m_memory);
    InitPointer(m_pic);
    InitPointer(m_pit);
    InitPointer(m_x87);
    m_trace_logger = TraceLogger::GetDisabled();
    m_trace_read_only = 0;
    m_trace_open = 0;
    memset(&m_state, 0, sizeof(m_state));
    m_power_on_a20_port_92 = true;
    m_shadow_control = false;
    m_page_control = false;
    m_power_off_requested = false;
}

SystemControl::~SystemControl()
{
}

void SystemControl::Init(Memory* memory, PIC* pic, PIT* pit)
{
    m_memory = memory;
    m_pic = pic;
    m_pit = pit;
    Reset();
}

void SystemControl::SetTraceLogger(TraceLogger* trace_logger)
{
    m_trace_logger = TraceLogger::Resolve(trace_logger);
}

void SystemControl::SetFPU(X87* fpu)
{
    m_x87 = fpu;
}

// The shadow register SeaBIOS programs at the end of POST, as it would a chipset's. Other BIOSes get a fixed map
void SystemControl::SetShadowControl(bool enabled)
{
    m_shadow_control = enabled;
}

void SystemControl::SetPageControl(bool enabled)
{
    m_page_control = enabled;
}

// Locked, the option ROMs and the F segment are read-only shadow RAM and the free space between the ROMs and SeaBIOS'
// low zone has nothing there, for UMB managers. Unlocked, C0000h-FFFFFh is plain RAM
void SystemControl::LockShadow()
{
    u32 rom_end = MIN((u32)m_state.shadow_rom_end, k_system_control_bios_page);
    u32 rom_start = MIN((u32)m_state.shadow_rom_start, rom_end);
    u32 free_end = MAX(MIN((u32)m_state.shadow_free_end, k_system_control_bios_page), rom_end);
    bool locked = m_state.shadow_locked && m_shadow_control;

    for (u32 page = 0; page < k_system_control_upper_pages; page++)
    {
        u8 access = k_system_control_page_ram;

        if (locked && (page >= k_system_control_bios_page || (page >= rom_start && page < rom_end)))
            access = k_system_control_page_read;
        else if (locked && (page < rom_start || (page >= rom_end && page < free_end)))
            access = k_system_control_page_open;

        m_state.upper_pages[page] = access;
    }
}

void SystemControl::ApplyShadow()
{
    if (!IsValidPointer(m_memory))
        return;

    u64 read_only = 0;
    u64 open = 0;

    for (u32 page = 0; page < k_system_control_upper_pages; page++)
    {
        u8 access = m_state.upper_pages[page];

        if (access == k_system_control_page_read || (access == k_system_control_page_open && page >= k_system_control_bios_page))
            read_only |= 1ULL << page;
        else if (access == k_system_control_page_open)
            open |= 1ULL << page;
    }

    u64 rom_pages = read_only & ((1ULL << k_system_control_bios_page) - 1);
    u64 bios_pages = read_only >> k_system_control_bios_page;

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_SYSTEM, TRACE_SYSTEM_MEMORY_MAP)) &&
        (read_only != m_trace_read_only || open != m_trace_open))
        TraceShadow(read_only, open);

    m_memory->SetRegionPages(GDOX_DEBUG_REGION_SHADOW_ROM, rom_pages);
    m_memory->SetRegionPages(GDOX_DEBUG_REGION_SHADOW_BIOS, bios_pages);
    m_memory->SetRegionPages(GDOX_DEBUG_REGION_UPPER_MEMORY, open);
    m_memory->SetRegionMapped(GDOX_DEBUG_REGION_SHADOW_ROM, rom_pages != 0);
    m_memory->SetRegionMapped(GDOX_DEBUG_REGION_SHADOW_BIOS, bios_pages != 0);
    m_memory->SetRegionMapped(GDOX_DEBUG_REGION_UPPER_MEMORY, open != 0);
}

u8 SystemControl::GetUpperPage(u32 page) const
{
    return page < k_system_control_upper_pages ? m_state.upper_pages[page] : k_system_control_page_ram;
}

void SystemControl::SetUpperPages(u32 first, u32 count, u8 access)
{
    for (u32 page = first; page < first + count && page < k_system_control_upper_pages; page++)
        m_state.upper_pages[page] = access & k_system_control_page_ram;

    ApplyShadow();
}

// SeaBIOS gates A20 through port 92h only and other BIOSes through the 8042. As the two OR together,
// the one the BIOS drives holds the gate open at power on and the other starts low
void SystemControl::SetPowerOnA20(bool port_92)
{
    m_power_on_a20_port_92 = port_92;
}

void SystemControl::Reset()
{
    memset(&m_state, 0, sizeof(m_state));
    m_state.nmi_mask = true;
    m_state.port_92 = m_power_on_a20_port_92 ? 0x02 : 0x00;
    m_state.keyboard_a20 = !m_power_on_a20_port_92;
    m_power_off_requested = false;
    UpdateA20(TRACE_A20_POWER_ON);
    LockShadow();
    ApplyShadow();
}

bool SystemControl::Decodes(u16 port) const
{
    switch (port)
    {
        case 0x0061:
        case 0x0070:
        case 0x0080:
        case 0x0092:
        case 0x00F0:
        case 0x00F1:
            return true;
        case 0x0404:
        case 0x0405:
        case 0x0406:
        case 0x0407:
        case 0x0408:
            return m_shadow_control;
        default:
            return false;
    }
}

u8 SystemControl::Read(u16 port, u64 clocks)
{
    return Peek(port, clocks);
}

u8 SystemControl::Peek(u16 port, u64 clocks) const
{
    switch (port)
    {
        case 0x0061:
            return (m_state.port_61 & 0x0F) | (m_pit->GetRefreshToggle(clocks) ? 0x10 : 0x00) |
                (m_pit->GetChannel2Output(clocks) ? 0x20 : 0x00);
        case 0x0080:
            return m_state.post_code;
        case 0x0092:
            return m_state.port_92;
        case 0x0404:
        case 0x0405:
        case 0x0406:
        case 0x0407:
            return m_shadow_control ? (u8)(k_system_control_shadow_id >> ((port - 0x0404) * 8)) : 0xFF;
        default:
            return 0xFF;
    }
}

void SystemControl::Write(u16 port, u8 value, u64 clocks)
{
    if (unlikely(m_trace_logger->IsEnabled(TRACE_SYSTEM)))
    {
        WriteTraced(port, value, clocks);
        return;
    }

    ApplyWrite(port, value, clocks);
}

void SystemControl::WriteTraced(u16 port, u8 value, u64 clocks)
{
    u8 previous = 0;
    u8 event = TRACE_SYSTEM_CONTROL;

    switch (port)
    {
        case 0x0061:
            previous = m_state.port_61;
            break;
        case 0x0070:
            previous = m_state.nmi_mask ? 0x80 : 0x00;
            break;
        case 0x0080:
            previous = m_state.post_code;
            event = TRACE_SYSTEM_POST;
            break;
        case 0x0406:
            previous = m_state.shadow_locked ? 1 : 0;
            break;
        default:
            break;
    }

    ApplyWrite(port, value, clocks);

    if (!m_trace_logger->IsEventEnabled(TRACE_SYSTEM, event) || port == 0x0092 || port == 0x00F1 ||
        (port == 0x0070 && ((value ^ previous) & 0x80) == 0) || (port == 0x0080 && value == previous))
        return;

    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_SYSTEM, event);
    entry->system.port = port;
    entry->system.value = value;
    entry->system.previous = previous;
    entry->system.flags = (u8)((IsA20Enabled() ? 0x01 : 0x00) | (m_state.nmi_mask ? 0x02 : 0x00));
}

void SystemControl::ApplyWrite(u16 port, u8 value, u64 clocks)
{
    switch (port)
    {
        case 0x0061:
            m_state.port_61 = value & 0x0F;
            m_pit->SetSpeaker((value & 0x01) != 0, (value & 0x02) != 0, clocks);
            break;
        case 0x0070:
            m_state.nmi_mask = (value & 0x80) != 0;
            break;
        case 0x0080:
            m_state.post_code = value;
            break;
        case 0x0092:
        {
            u8 previous = m_state.port_92;
            m_state.port_92 = value & 0x03;
            m_state.reset_a20 = false;
            UpdateA20(TRACE_A20_PORT_92);

            // The fast reset pulses on a rising bit 0
            if ((value & 0x01) != 0 && (previous & 0x01) == 0)
                RequestCPUReset(k_system_control_reset_fast, 0x0092);

            break;
        }
        case 0x00F0:
            if (m_state.fpu_error)
            {
                m_state.fpu_error = false;
                m_pic->SetIRQLine(k_system_control_fpu_irq, false);
            }
            break;
        case 0x00F1:
            if (IsValidPointer(m_x87))
                m_x87->Reset();
            break;
        case 0x0404:
            m_state.shadow_rom_end = value;
            break;
        case 0x0405:
            m_state.shadow_free_end = value;
            break;
        case 0x0407:
            m_state.shadow_rom_start = value;
            break;
        case 0x0406:
            m_state.shadow_locked = (value & 0x01) != 0;
            LockShadow();
            ApplyShadow();
            break;
        case 0x0408:
            m_power_off_requested = m_power_off_requested || ((value & 0x01) != 0);
            break;
        default:
            break;
    }
}

// Port 61h moves with the refresh toggle and OUT2, the other ports only with writes
u64 SystemControl::GetReadStableUntil(u16 port, u64 clocks) const
{
    switch (port)
    {
        case 0x0061:
            return m_pit->GetSpeakerStableUntil(clocks);
        case 0x0080:
        case 0x0092:
        case 0x0404:
        case 0x0405:
        case 0x0406:
        case 0x0407:
        case 0x0408:
            return GDOX_NO_EVENT;
        default:
            return 0;
    }
}

// SeaBIOS' APM Set Power State, off for the whole system, writes 1 here before it halts: an edge for the frontend,
// not machine state
bool SystemControl::IsPowerOffRequested() const
{
    return m_power_off_requested;
}

void SystemControl::AcknowledgePowerOff()
{
    m_power_off_requested = false;
}

void SystemControl::RequestCPUReset(u8 cause, u16 port)
{
    m_state.reset_cause |= cause;
    m_state.reset_pending = true;

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_SYSTEM, TRACE_SYSTEM_RESET)))
        TraceReset(cause, port, false);
}

void SystemControl::AcknowledgeCPUReset()
{
    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_SYSTEM, TRACE_SYSTEM_RESET)))
        TraceReset(m_state.reset_cause, 0, true);

    m_state.last_reset_cause = m_state.reset_cause;
    m_state.reset_cause = 0;
    m_state.reset_pending = false;
    m_state.reset_count++;
    m_state.reset_a20 = true;
    UpdateA20(TRACE_A20_RESET);
}

// The coprocessor's ERROR# latches IRQ13 until a write to port F0h
void SystemControl::SetFPUError()
{
    if (m_state.fpu_error)
        return;

    m_state.fpu_error = true;
    m_pic->SetIRQLine(k_system_control_fpu_irq, true);

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_SYSTEM, TRACE_SYSTEM_CONTROL)))
    {
        GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_SYSTEM, TRACE_SYSTEM_CONTROL);
        entry->system.port = 0x00F0;
        entry->system.kind = 1;
    }
}

void SystemControl::SetKeyboardA20(bool enabled)
{
    m_state.keyboard_a20 = enabled;
    m_state.reset_a20 = false;
    UpdateA20(TRACE_A20_KEYBOARD);
}

void SystemControl::UpdateA20(u8 source)
{
    bool enabled = IsA20Enabled();

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_SYSTEM, TRACE_SYSTEM_MEMORY_MAP)) &&
        enabled != m_memory->IsA20Enabled())
        TraceA20(enabled, source);

    m_memory->SetA20(enabled);
}

void SystemControl::TraceA20(bool enabled, u8 source)
{
    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_SYSTEM, TRACE_SYSTEM_MEMORY_MAP);
    entry->system.kind = TRACE_SYSTEM_MAP_A20;
    entry->system.source = source;
    entry->system.value = enabled ? 1 : 0;
    entry->system.flags = (u8)((m_state.port_92 & 0x02) != 0 ? 0x01 : 0x00) | (m_state.keyboard_a20 ? 0x02 : 0x00) |
        (m_state.reset_a20 ? 0x04 : 0x00);
    entry->system.port = source == TRACE_A20_PORT_92 ? 0x0092 : source == TRACE_A20_KEYBOARD ? 0x0064 : 0;
}

void SystemControl::TraceShadow(u64 read_only, u64 open)
{
    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_SYSTEM, TRACE_SYSTEM_MEMORY_MAP);
    entry->system.kind = TRACE_SYSTEM_MAP_SHADOW;
    entry->system.read_only_pages = read_only;
    entry->system.open_pages = open;
    entry->system.value = m_state.shadow_locked ? 1 : 0;
    m_trace_read_only = read_only;
    m_trace_open = open;
}

void SystemControl::TraceReset(u8 cause, u16 port, bool performed)
{
    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_SYSTEM, TRACE_SYSTEM_RESET);
    entry->flags |= performed ? TRACE_SYSTEM_FLAG_PERFORMED : 0;
    entry->system.flags = cause;
    entry->system.port = port;
    entry->system.value = (u8)m_state.reset_count;
}

void SystemControl::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
}

void SystemControl::LoadState(std::istream& stream, int version)
{
    UNUSED(version);
    StateSerializer serializer(stream);
    Serialize(serializer);
    SanitizeState();
}

void SystemControl::Serialize(StateSerializer& serializer)
{
    G_SERIALIZE(serializer, m_state.reset_cause);
    G_SERIALIZE(serializer, m_state.reset_pending);
    G_SERIALIZE(serializer, m_state.reset_count);
    G_SERIALIZE(serializer, m_state.last_reset_cause);
    G_SERIALIZE(serializer, m_state.nmi_mask);
    G_SERIALIZE(serializer, m_state.port_61);
    G_SERIALIZE(serializer, m_state.port_92);
    G_SERIALIZE(serializer, m_state.post_code);
    G_SERIALIZE(serializer, m_state.fpu_error);
    G_SERIALIZE(serializer, m_state.keyboard_a20);
    G_SERIALIZE(serializer, m_state.reset_a20);
    G_SERIALIZE(serializer, m_state.shadow_rom_start);
    G_SERIALIZE(serializer, m_state.shadow_rom_end);
    G_SERIALIZE(serializer, m_state.shadow_free_end);
    G_SERIALIZE(serializer, m_state.shadow_locked);

    if (m_page_control)
        G_SERIALIZE_ARRAY(serializer, m_state.upper_pages, k_system_control_upper_pages);
}

void SystemControl::SanitizeState()
{
    m_state.port_61 &= 0x0F;
    m_state.port_92 &= 0x03;
    UpdateA20(TRACE_A20_STATE_LOAD);

    if (!m_page_control)
        LockShadow();

    for (u32 page = 0; page < k_system_control_upper_pages; page++)
        m_state.upper_pages[page] &= k_system_control_page_ram;

    ApplyShadow();
}
