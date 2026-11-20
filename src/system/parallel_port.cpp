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

#include "parallel_port.h"
#include "../common/trace_logger.h"
#include "../common/state_serializer.h"

ParallelPort::ParallelPort()
{
    m_trace_logger = TraceLogger::GetDisabled();
    Reset();
}

ParallelPort::~ParallelPort()
{
}

void ParallelPort::SetTraceLogger(TraceLogger* trace_logger)
{
    m_trace_logger = IsValidPointer(trace_logger) ? trace_logger : TraceLogger::GetDisabled();
}

void ParallelPort::Reset()
{
    m_state.data = 0;
    m_state.control = 0;
}

bool ParallelPort::Decodes(u16 port) const
{
    return port >= k_parallel_port_base && port <= k_parallel_port_base + 2;
}

// The data port is a latch. With nothing attached the status lines are pulled up, BUSY inverted to 0,
// and ACK never pulses, so the IRQ never fires. Control bits 7-6 read 1
u8 ParallelPort::Read(u16 port) const
{
    switch (port - k_parallel_port_base)
    {
        case 0:
            return m_state.data;
        case 1:
            return k_parallel_port_status_empty;
        default:
            return (u8)(0xE0 | m_state.control);
    }
}

void ParallelPort::Write(u16 port, u8 value)
{
    switch (port - k_parallel_port_base)
    {
        case 0:
            m_state.data = value;

            if (unlikely(m_trace_logger->IsEventEnabled(TRACE_PARALLEL, TRACE_PARALLEL_DATA)))
                TraceEvent(TRACE_PARALLEL_DATA, port, value, value);

            break;
        case 2:
        {
            u8 previous = m_state.control;
            m_state.control = value & 0x1F;

            if (unlikely(m_trace_logger->IsEnabled(TRACE_PARALLEL)))
                TraceControl(port, value, previous);

            break;
        }
        default:
            break;
    }
}

ParallelPort::ParallelPort_State* ParallelPort::GetState()
{
    return &m_state;
}

void ParallelPort::TraceControl(u16 port, u8 value, u8 previous)
{
    if (m_trace_logger->IsEventEnabled(TRACE_PARALLEL, TRACE_PARALLEL_CONTROL))
        TraceEvent(TRACE_PARALLEL_CONTROL, port, value, previous);

    if (m_trace_logger->IsEventEnabled(TRACE_PARALLEL, TRACE_PARALLEL_STROBE) && (value & 0x01) != 0 &&
        (previous & 0x01) == 0)
        TraceEvent(TRACE_PARALLEL_STROBE, port, m_state.data, previous);
}

void ParallelPort::TraceEvent(u8 event, u16 port, u8 value, u8 previous)
{
    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_PARALLEL, event);
    entry->parallel.port = port;
    entry->parallel.value = value;
    entry->parallel.data = m_state.data;
    entry->parallel.previous = previous;
}

void ParallelPort::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
}

void ParallelPort::LoadState(std::istream& stream, int version)
{
    UNUSED(version);
    StateSerializer serializer(stream);
    Serialize(serializer);
    m_state.control &= 0x1F;
}

void ParallelPort::Serialize(StateSerializer& serializer)
{
    G_SERIALIZE(serializer, m_state.data);
    G_SERIALIZE(serializer, m_state.control);
}
