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

#ifndef DEV_PORT_INLINE_H
#define DEV_PORT_INLINE_H

#include "dev_port.h"

INLINE bool DevPort::IsConsoleEnabled() const
{
    return m_console_enabled;
}

INLINE bool DevPort::IsControlEnabled() const
{
    return m_control_enabled;
}

INLINE bool DevPort::Decodes(u16 port) const
{
    if (port == k_dev_port_console)
        return m_console_enabled;

    return m_control_enabled && (port == k_dev_port_control || port == k_dev_port_stream);
}

INLINE DebugConsole* DevPort::GetConsole()
{
    return &m_console;
}

INLINE DevPort::DevPort_State* DevPort::GetState()
{
    return &m_state;
}

#endif /* DEV_PORT_INLINE_H */
