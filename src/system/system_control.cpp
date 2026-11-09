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
#include "../i386/x87.h"

SystemControl::SystemControl()
{
    InitPointer(m_memory);
    InitPointer(m_pic);
    InitPointer(m_pit);
    InitPointer(m_x87);
    InitPointer(m_trace_logger);
    memset(&m_state, 0, sizeof(m_state));
    m_power_on_a20_port_92 = true;
    m_shadow_control = false;
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
    m_trace_logger = trace_logger;
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

// Locked, the option ROMs and the F segment are read-only shadow RAM and the free space between the ROMs and SeaBIOS'
// low zone has nothing there, for UMB managers. Unlocked, C0000h-FFFFFh is plain RAM
void SystemControl::ApplyShadow()
{
    if (!IsValidPointer(m_memory))
        return;

    u32 rom_end = MIN(k_system_control_shadow_start + ((u32)m_state.shadow_rom_end << 12), k_system_control_shadow_bios);
    u32 free_end = MIN(k_system_control_shadow_start + ((u32)m_state.shadow_free_end << 12), k_system_control_shadow_bios);
    bool locked = m_state.shadow_locked && m_shadow_control;
    u8* ram = m_memory->GetMainRAM();
    free_end = MAX(free_end, rom_end);

    if (locked && rom_end > k_system_control_shadow_start)
        m_memory->SetRegionSpan(GDOX_DEBUG_REGION_SHADOW_ROM, k_system_control_shadow_start,
            rom_end - k_system_control_shadow_start, ram + k_system_control_shadow_start, NULL);

    if (locked && free_end > rom_end)
        m_memory->SetRegionSpan(GDOX_DEBUG_REGION_UPPER_MEMORY, rom_end, free_end - rom_end, NULL, NULL);

    m_memory->SetRegionMapped(GDOX_DEBUG_REGION_SHADOW_ROM, locked && rom_end > k_system_control_shadow_start);
    m_memory->SetRegionMapped(GDOX_DEBUG_REGION_UPPER_MEMORY, locked && free_end > rom_end);
    m_memory->SetRegionMapped(GDOX_DEBUG_REGION_SHADOW_BIOS, locked);
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
    UpdateA20();
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
    u8 previous = 0;

    switch (port)
    {
        case 0x0061:
            previous = m_state.port_61;
            m_state.port_61 = value & 0x0F;
            m_pit->SetSpeaker((value & 0x01) != 0, (value & 0x02) != 0, clocks);
            break;
        case 0x0070:
            previous = m_state.nmi_mask ? 0x80 : 0x00;
            m_state.nmi_mask = (value & 0x80) != 0;
            break;
        case 0x0080:
            previous = m_state.post_code;
            m_state.post_code = value;
            break;
        case 0x0092:
            previous = m_state.port_92;
            m_state.port_92 = value & 0x03;
            m_state.reset_a20 = false;
            UpdateA20();

            // The fast reset pulses on a rising bit 0
            if ((value & 0x01) != 0 && (previous & 0x01) == 0)
                RequestCPUReset(k_system_control_reset_fast);

            break;
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
        case 0x0406:
            previous = m_state.shadow_locked ? 1 : 0;
            m_state.shadow_locked = (value & 0x01) != 0;
            ApplyShadow();
            break;
        case 0x0408:
            m_power_off_requested = m_power_off_requested || ((value & 0x01) != 0);
            break;
        default:
            return;
    }

    if (unlikely(IsValidPointer(m_trace_logger) && m_trace_logger->IsEnabled(TRACE_SYSTEM)))
        TraceWrite(port, value, previous);
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

void SystemControl::RequestCPUReset(u8 cause)
{
    m_state.reset_cause |= cause;
    m_state.reset_pending = true;

    if (unlikely(IsValidPointer(m_trace_logger) && m_trace_logger->IsEventEnabled(TRACE_SYSTEM, TRACE_SYSTEM_RESET)))
    {
        GDOX_Trace_Entry entry = {};
        entry.type = TRACE_SYSTEM;
        entry.event = TRACE_SYSTEM_RESET;
        entry.system.flags = cause;
        m_trace_logger->TraceLog(entry);
    }
}

void SystemControl::AcknowledgeCPUReset()
{
    m_state.last_reset_cause = m_state.reset_cause;
    m_state.reset_cause = 0;
    m_state.reset_pending = false;
    m_state.reset_count++;
    m_state.reset_a20 = true;
    UpdateA20();
}

// The coprocessor's ERROR# latches IRQ13 until a write to port F0h
void SystemControl::SetFPUError()
{
    if (m_state.fpu_error)
        return;

    m_state.fpu_error = true;
    m_pic->SetIRQLine(k_system_control_fpu_irq, true);
}

void SystemControl::SetKeyboardA20(bool enabled)
{
    m_state.keyboard_a20 = enabled;
    m_state.reset_a20 = false;
    UpdateA20();
}

void SystemControl::UpdateA20()
{
    m_memory->SetA20(IsA20Enabled());
}

void SystemControl::TraceWrite(u16 port, u8 value, u8 previous)
{
    u8 event = port == 0x0080 ? TRACE_SYSTEM_POST : port == 0x0092 ? TRACE_SYSTEM_MEMORY_MAP : TRACE_SYSTEM_CONTROL;

    if (!m_trace_logger->IsEventEnabled(TRACE_SYSTEM, event))
        return;

    if (event == TRACE_SYSTEM_MEMORY_MAP && ((value ^ previous) & 0x02) == 0)
        return;

    GDOX_Trace_Entry entry = {};
    entry.type = TRACE_SYSTEM;
    entry.event = event;
    entry.system.port = port;
    entry.system.value = value;
    entry.system.address = previous;
    entry.system.flags = (u8)((IsA20Enabled() ? 0x01 : 0x00) | (m_state.nmi_mask ? 0x02 : 0x00));
    m_trace_logger->TraceLog(entry);
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
    G_SERIALIZE(serializer, m_state.shadow_rom_end);
    G_SERIALIZE(serializer, m_state.shadow_free_end);
    G_SERIALIZE(serializer, m_state.shadow_locked);
}

void SystemControl::SanitizeState()
{
    m_state.port_61 &= 0x0F;
    m_state.port_92 &= 0x03;
    UpdateA20();
    ApplyShadow();
}
