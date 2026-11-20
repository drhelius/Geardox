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


#include "debug_console.h"
#include "../common/trace_logger.h"

DebugConsole::DebugConsole()
{
    m_trace_logger = TraceLogger::GetDisabled();
    m_trace_port = 0;
    m_head = 0;
    m_length = 0;
    m_written = 0;
    m_line_length = 0;
    m_enabled = true;
    m_log = false;
}

DebugConsole::~DebugConsole()
{
}

void DebugConsole::Init()
{
    Reset();
}

void DebugConsole::Reset()
{
    m_head = 0;
    m_length = 0;
    m_written = 0;
    m_line_length = 0;
}

// The oldest text drops out once the buffer is full
void DebugConsole::Write(u8 value)
{
    m_buffer[(m_head + m_length) % DEBUG_CONSOLE_SIZE] = (char)value;

    if (m_length < DEBUG_CONSOLE_SIZE)
        m_length++;
    else
        m_head = (m_head + 1) % DEBUG_CONSOLE_SIZE;

    m_written++;

    if (!m_log)
        return;

    if (value == '\n' || m_line_length == DEBUG_CONSOLE_LINE_SIZE - 1)
        FlushLine();

    if (value != '\n' && value != '\r')
        m_line[m_line_length++] = (char)value;
}

// The text from oldest to newest, as a C string
u32 DebugConsole::CopyText(char* buffer, u32 size) const
{
    if (size == 0)
        return 0;

    u32 count = MIN(m_length, size - 1);
    u32 start = (m_head + m_length - count) % DEBUG_CONSOLE_SIZE;

    for (u32 i = 0; i < count; i++)
        buffer[i] = m_buffer[(start + i) % DEBUG_CONSOLE_SIZE];

    buffer[count] = 0;
    return count;
}

u32 DebugConsole::CopySince(u64 written, char* buffer, u32 size) const
{
    if (size == 0)
        return 0;

    u64 oldest = m_written - m_length;
    u64 first = MAX(written, oldest);
    u32 available = first < m_written ? (u32)(m_written - first) : 0;
    u32 count = MIN(available, size - 1);
    u32 start = (m_head + (u32)(first - oldest)) % DEBUG_CONSOLE_SIZE;

    for (u32 i = 0; i < count; i++)
        buffer[i] = m_buffer[(start + i) % DEBUG_CONSOLE_SIZE];

    buffer[count] = 0;
    return count;
}

void DebugConsole::Rewind(u64 written)
{
    if (written <= m_written)
    {
        u64 dropped = m_written - written;
        m_length = dropped >= m_length ? 0 : m_length - (u32)dropped;
    }
    else
    {
        m_head = 0;
        m_length = 0;
    }

    m_written = written;
    m_line_length = 0;
}

void DebugConsole::SetTraceLogger(TraceLogger* trace_logger, u16 port)
{
    m_trace_logger = IsValidPointer(trace_logger) ? trace_logger : TraceLogger::GetDisabled();
    m_trace_port = port;
}

void DebugConsole::FlushLine()
{
    m_line[m_line_length] = 0;

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_SYSTEM, TRACE_SYSTEM_CONSOLE)))
        TraceLine();

    Log("BIOS: %s", m_line);
    m_line_length = 0;
}

void DebugConsole::TraceLine()
{
    for (int start = 0; start == 0 || start < m_line_length; start += 64)
    {
        GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_SYSTEM, TRACE_SYSTEM_CONSOLE);
        entry->console.port = m_trace_port;
        entry->console.length = (u8)MIN(m_line_length - start, 64);
        memcpy(entry->console.text, m_line + start, entry->console.length);
    }
}
