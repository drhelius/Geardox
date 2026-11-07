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

#ifndef OPL_INLINE_H
#define OPL_INLINE_H

#include "opl.h"

INLINE void OPL::Clock(u32 cycles)
{
    m_state.elapsed_cycles += cycles;
}

INLINE OPL::OPL_Model OPL::GetModel() const
{
    return m_model;
}

INLINE OPL::OPL_State* OPL::GetState()
{
    return &m_state;
}

INLINE s16 OPL::GetChannelOutput(int channel) const
{
    const u8* outputs = m_state.channels[channel].outputs;
    return (s16)(m_state.signals[outputs[0]] + m_state.signals[outputs[1]] + m_state.signals[outputs[2]] +
        m_state.signals[outputs[3]]);
}

INLINE u8 OPL::GetRegister(u16 address) const
{
    return m_state.registers[(address >> 8) & 0x01][address & 0xFF];
}

INLINE u16 OPL::GetSelectedAddress() const
{
    return m_state.address;
}

INLINE void OPL::SetChannelMute(int channel, bool mute)
{
    m_channel_mask[channel] = mute ? 0x0000 : 0xFFFF;
}

INLINE bool OPL::IsChannelMuted(int channel) const
{
    return m_channel_mask[channel] == 0;
}

#endif /* OPL_INLINE_H */
