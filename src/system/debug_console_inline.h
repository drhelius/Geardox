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


#ifndef DEBUG_CONSOLE_INLINE_H
#define DEBUG_CONSOLE_INLINE_H

#include "debug_console.h"

INLINE void DebugConsole::SetEnabled(bool enabled)
{
    m_enabled = enabled;
}

INLINE bool DebugConsole::IsEnabled() const
{
    return m_enabled;
}

INLINE void DebugConsole::SetLogOutput(bool log)
{
    m_log = log;
}

INLINE bool DebugConsole::Decodes(u16 port) const
{
    return m_enabled && port == k_debug_console_port;
}

INLINE u8 DebugConsole::Read() const
{
    return k_debug_console_readback;
}

INLINE u32 DebugConsole::GetLength() const
{
    return m_length;
}

INLINE u64 DebugConsole::GetWrittenCount() const
{
    return m_written;
}

#endif /* DEBUG_CONSOLE_INLINE_H */
