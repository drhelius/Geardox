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

#include "mc6845.h"
#include "../common/state_serializer.h"

MC6845::MC6845()
{
    memset(&m_state, 0, sizeof(m_state));
}

MC6845::~MC6845()
{
}

void MC6845::Init()
{
    Reset();
}

void MC6845::Reset()
{
    memset(&m_state, 0, sizeof(m_state));
}

u8 MC6845::ReadData() const
{
    u8 index = m_state.index;

    if (index >= k_mc6845_cursor_high && index < MC6845_REGISTER_COUNT)
        return m_state.registers[index];

    return 0xFF;
}

void MC6845::WriteIndex(u8 value)
{
    m_state.index = value & 0x1F;
}

bool MC6845::IsDataChange(u8 value) const
{
    u8 index = m_state.index;

    if (index >= k_mc6845_pen_high)
        return false;

    return (value & k_mc6845_masks[index]) != m_state.registers[index];
}

void MC6845::WriteData(u8 value)
{
    u8 index = m_state.index;

    if (index >= k_mc6845_pen_high)
        return;

    m_state.registers[index] = value & k_mc6845_masks[index];
}

bool MC6845::IsTimingRegister() const
{
    u8 index = m_state.index;

    return index == k_mc6845_horizontal_total || index == k_mc6845_vertical_total ||
        index == k_mc6845_vertical_adjust || index == k_mc6845_max_scan;
}

void MC6845::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer, GDOX_SAVESTATE_VERSION);
}

void MC6845::LoadState(std::istream& stream, int version)
{
    StateSerializer serializer(stream);
    Serialize(serializer, version);
    SanitizeState();
}

void MC6845::Serialize(StateSerializer& serializer, int version)
{
    UNUSED(version);
    G_SERIALIZE_ARRAY(serializer, m_state.registers, MC6845_REGISTER_COUNT);
    G_SERIALIZE(serializer, m_state.index);
}

void MC6845::SanitizeState()
{
    m_state.index &= 0x1F;

    for (int i = 0; i < MC6845_REGISTER_COUNT; i++)
        m_state.registers[i] &= k_mc6845_masks[i];
}
