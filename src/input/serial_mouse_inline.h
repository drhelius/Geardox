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


#ifndef SERIAL_MOUSE_INLINE_H
#define SERIAL_MOUSE_INLINE_H

#include "serial_mouse.h"

INLINE u64 SerialMouse::GetNextEventClocks() const
{
    return MIN(MIN(m_state.ident_clocks, m_state.byte_clocks), m_state.poll_clocks);
}

INLINE SerialMouse::SerialMouse_State* SerialMouse::GetState()
{
    return &m_state;
}

#endif /* SERIAL_MOUSE_INLINE_H */
