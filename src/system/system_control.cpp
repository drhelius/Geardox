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

SystemControl::SystemControl()
{
    InitPointer(m_memory);
    InitPointer(m_pic);
    InitPointer(m_pit);
    InitPointer(m_trace_logger);
    memset(&m_state, 0, sizeof(m_state));
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

// A20 is on at power on, the BIOS sets the state it wants
void SystemControl::Reset()
{
    memset(&m_state, 0, sizeof(m_state));
    m_state.nmi_mask = true;
    m_state.port_92 = 0x02;
    UpdateA20();
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
            return GDOX_NO_EVENT;
        default:
            return 0;
    }
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
}

// The coprocessor's ERROR# latches IRQ13 until a write to port F0h
void SystemControl::SetFPUError()
{
    if (m_state.fpu_error)
        return;

    m_state.fpu_error = true;
    m_pic->SetIRQLine(k_system_control_fpu_irq, true);
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
}

void SystemControl::SanitizeState()
{
    m_state.port_61 &= 0x0F;
    m_state.port_92 &= 0x03;
    UpdateA20();
}
