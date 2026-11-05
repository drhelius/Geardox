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


#ifndef I8042_INLINE_H
#define I8042_INLINE_H

#include "i8042.h"

INLINE void I8042::HandleEvent(u64 clocks)
{
    Synchronize(clocks);
    UpdateNextEvent();
}

INLINE u8 I8042::Peek(u16 port) const
{
    return port == 0x0064 ? GetStatus() : m_state.output_buffer;
}

// The status moves only at the keyboard event or with a write, port 60h clears the output buffer
INLINE u64 I8042::GetReadStableUntil(u16 port) const
{
    return port == 0x0064 ? GDOX_NO_EVENT : 0;
}

INLINE u8 I8042::GetStatus() const
{
    return (u8)((m_state.output_full ? 0x01 : 0x00) | (m_state.input_full ? 0x02 : 0x00) |
        (m_state.ram[0] & k_i8042_system_flag) | (m_state.input_command ? 0x08 : 0x00) | 0x10 |
        (m_state.output_aux ? 0x20 : 0x00) | (m_state.timeout ? 0x40 : 0x00));
}

// Bits 4 and 5 are the IRQ1 and IRQ12 lines, wired to the output buffer
INLINE u8 I8042::GetOutputPort() const
{
    return (u8)((m_state.output_port & 0xCF) | (m_state.output_full && !m_state.output_aux ? 0x10 : 0x00) |
        (m_state.output_full && m_state.output_aux ? 0x20 : 0x00));
}

INLINE I8042::I8042_State* I8042::GetState()
{
    return &m_state;
}

#endif /* I8042_INLINE_H */
