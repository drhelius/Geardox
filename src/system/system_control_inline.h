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


#ifndef SYSTEM_CONTROL_INLINE_H
#define SYSTEM_CONTROL_INLINE_H

#include "system_control.h"

INLINE bool SystemControl::IsCPUResetPending() const
{
    return m_state.reset_pending;
}

// The gate is open when port 92h or the 8042 ask for it. The CPU fetches its reset vector through it, so a CPU reset
// opens it until one of them is written again
INLINE bool SystemControl::IsA20Enabled() const
{
    return (m_state.port_92 & 0x02) != 0 || m_state.keyboard_a20 || m_state.reset_a20;
}

INLINE bool SystemControl::IsKeyboardA20Enabled() const
{
    return m_state.keyboard_a20;
}

INLINE SystemControl::SystemControl_State* SystemControl::GetState()
{
    return &m_state;
}

#endif /* SYSTEM_CONTROL_INLINE_H */
