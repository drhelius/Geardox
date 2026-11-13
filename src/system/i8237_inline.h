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

#ifndef I8237_INLINE_H
#define I8237_INLINE_H

#include "i8237.h"

INLINE void I8237::SetRequestLine(int channel, bool active)
{
    u8 bit = (u8)(1 << channel);

    if (active)
        m_state.request_levels |= bit;
    else
        m_state.request_levels &= ~bit;
}

INLINE bool I8237::IsEnabled() const
{
    return (m_state.command & k_i8237_command_disable) == 0;
}

// The mask only gates the DREQ lines, a software request gets through it
INLINE bool I8237::IsRequesting(int channel) const
{
    return IS_SET_BIT((m_state.request_levels & ~m_state.mask) | m_state.software_requests, channel);
}

INLINE bool I8237::IsMasked(int channel) const
{
    return IS_SET_BIT(m_state.mask, channel);
}

INLINE u8 I8237::GetTransfer(int channel) const
{
    return (m_state.channels[channel].mode >> 2) & 0x03;
}

INLINE u8 I8237::GetService(int channel) const
{
    return (m_state.channels[channel].mode >> 6) & 0x03;
}

INLINE I8237::I8237_State* I8237::GetState()
{
    return &m_state;
}

INLINE const I8237::I8237_State* I8237::GetState() const
{
    return &m_state;
}

#endif /* I8237_INLINE_H */
