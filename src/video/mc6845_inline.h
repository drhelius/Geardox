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

#ifndef MC6845_INLINE_H
#define MC6845_INLINE_H

#include "mc6845.h"

INLINE u8 MC6845::GetIndex() const
{
    return m_state.index;
}

INLINE u8 MC6845::GetRegister(int index) const
{
    return (index >= 0 && index < MC6845_REGISTER_COUNT) ? m_state.registers[index] : 0x00;
}

INLINE u32 MC6845::GetHorizontalTotal() const
{
    return (u32)m_state.registers[k_mc6845_horizontal_total] + 1;
}

INLINE u32 MC6845::GetHorizontalDisplayed() const
{
    return m_state.registers[k_mc6845_horizontal_displayed];
}

INLINE u32 MC6845::GetHorizontalSyncPosition() const
{
    return m_state.registers[k_mc6845_horizontal_sync];
}

INLINE u32 MC6845::GetRowLines() const
{
    return (u32)m_state.registers[k_mc6845_max_scan] + 1;
}

INLINE u32 MC6845::GetTotalLines() const
{
    return ((u32)m_state.registers[k_mc6845_vertical_total] + 1) * GetRowLines() +
        m_state.registers[k_mc6845_vertical_adjust];
}

INLINE u32 MC6845::GetDisplayedLines() const
{
    return (u32)m_state.registers[k_mc6845_vertical_displayed] * GetRowLines();
}

INLINE u32 MC6845::GetVerticalSyncLine() const
{
    return (u32)m_state.registers[k_mc6845_vertical_sync] * GetRowLines();
}

INLINE u32 MC6845::GetStartAddress() const
{
    return ((u32)m_state.registers[k_mc6845_start_high] << 8) | m_state.registers[k_mc6845_start_low];
}

INLINE u32 MC6845::GetCursorAddress() const
{
    return ((u32)m_state.registers[k_mc6845_cursor_high] << 8) | m_state.registers[k_mc6845_cursor_low];
}

INLINE u32 MC6845::GetCursorStart() const
{
    return m_state.registers[k_mc6845_cursor_start] & 0x1F;
}

INLINE u32 MC6845::GetCursorEnd() const
{
    return m_state.registers[k_mc6845_cursor_end] & 0x1F;
}

INLINE bool MC6845::IsCursorShown() const
{
    return (m_state.registers[k_mc6845_cursor_start] & 0x60) != 0x20;
}

INLINE MC6845::MC6845_State* MC6845::GetState()
{
    return &m_state;
}

#endif /* MC6845_INLINE_H */
