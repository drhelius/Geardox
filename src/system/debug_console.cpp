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

DebugConsole::DebugConsole()
{
    m_enabled = true;
    m_log = false;
    Reset();
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

void DebugConsole::SetEnabled(bool enabled)
{
    m_enabled = enabled;
}

bool DebugConsole::IsEnabled() const
{
    return m_enabled;
}

void DebugConsole::SetLogOutput(bool log)
{
    m_log = log;
}

bool DebugConsole::Decodes(u16 port) const
{
    return m_enabled && port == k_debug_console_port;
}

u8 DebugConsole::Read(u16 port) const
{
    return Decodes(port) ? k_debug_console_readback : 0xFF;
}

// The oldest text drops out once the buffer is full
void DebugConsole::Write(u16 port, u8 value)
{
    if (!Decodes(port))
        return;

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

u32 DebugConsole::GetLength() const
{
    return m_length;
}

u64 DebugConsole::GetWrittenCount() const
{
    return m_written;
}

void DebugConsole::FlushLine()
{
    m_line[m_line_length] = 0;
    Log("BIOS: %s", m_line);
    m_line_length = 0;
}
