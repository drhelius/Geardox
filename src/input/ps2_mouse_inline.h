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


#ifndef PS2_MOUSE_INLINE_H
#define PS2_MOUSE_INLINE_H

#include "ps2_mouse.h"

INLINE u64 PS2Mouse::GetNextEventClocks() const
{
    return MIN(MIN(m_state.receive_clocks, m_state.send_clocks), m_state.sample_clocks);
}

INLINE bool PS2Mouse::IsReceiving() const
{
    return m_state.receive_clocks != GDOX_NO_EVENT;
}

INLINE u64 PS2Mouse::GetReceiveClocks() const
{
    return m_state.receive_clocks;
}

INLINE PS2Mouse::PS2Mouse_State* PS2Mouse::GetState()
{
    return &m_state;
}

#endif /* PS2_MOUSE_INLINE_H */
