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


#ifndef DEBUG_CONSOLE_H
#define DEBUG_CONSOLE_H

#include "../common/common.h"

#define DEBUG_CONSOLE_SIZE 0x10000
#define DEBUG_CONSOLE_LINE_SIZE 256

class DebugConsole
{
public:
    DebugConsole();
    ~DebugConsole();
    void Init();
    void Reset();
    void SetEnabled(bool enabled);
    bool IsEnabled() const;
    void SetLogOutput(bool log);
    bool Decodes(u16 port) const;
    u8 Read(u16 port) const;
    void Write(u16 port, u8 value);
    u32 CopyText(char* buffer, u32 size) const;
    u32 GetLength() const;
    u64 GetWrittenCount() const;

private:
    void FlushLine();

private:
    char m_buffer[DEBUG_CONSOLE_SIZE];
    u32 m_head;
    u32 m_length;
    u64 m_written;
    char m_line[DEBUG_CONSOLE_LINE_SIZE];
    int m_line_length;
    bool m_enabled;
    bool m_log;
};

// The debug port SeaBIOS writes its log to, which reads back E9h so firmware knows it is there
static const u16 k_debug_console_port = 0x0402;
static const u8 k_debug_console_readback = 0xE9;

#endif /* DEBUG_CONSOLE_H */
