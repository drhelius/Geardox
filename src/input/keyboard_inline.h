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


#ifndef KEYBOARD_INLINE_H
#define KEYBOARD_INLINE_H

#include "keyboard.h"

INLINE u64 Keyboard::GetNextEventClocks() const
{
    u64 next = MIN(m_state.receive_clocks, m_state.send_clocks);
    return MIN(next, MIN(m_state.self_test_clocks, m_state.repeat_clocks));
}

INLINE bool Keyboard::IsReceiving() const
{
    return m_state.receive_clocks != GDOX_NO_EVENT;
}

INLINE u64 Keyboard::GetReceiveClocks() const
{
    return m_state.receive_clocks;
}

INLINE bool Keyboard::IsKeyPressed(GDOX_Keys key) const
{
    return IsValidKey(key) && m_state.keys[key] != 0;
}

INLINE Keyboard::Keyboard_State* Keyboard::GetState()
{
    return &m_state;
}

#endif /* KEYBOARD_INLINE_H */
