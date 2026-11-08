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

#ifndef IDE_CDROM_INLINE_H
#define IDE_CDROM_INLINE_H

#include "ide_cdrom.h"

INLINE bool IdeCdRom::IsAttached() const
{
    return m_fitted;
}

INLINE int IdeCdRom::GetId() const
{
    return m_id;
}

INLINE int IdeCdRom::GetSpeed() const
{
    return m_speed;
}

INLINE u8 IdeCdRom::GetStatus() const
{
    return m_state.status;
}

INLINE u64 IdeCdRom::GetEventClocks() const
{
    return MIN(m_state.event_clocks, m_state.seek_clocks);
}

INLINE bool IdeCdRom::IsInterrupting() const
{
    return m_state.interrupt;
}

INLINE void IdeCdRom::ClearInterrupt()
{
    m_state.interrupt = false;
}

INLINE bool IdeCdRom::IsTrayOpen() const
{
    return m_state.tray_open;
}

INLINE u32 IdeCdRom::GetHeadLBA() const
{
    return m_state.head_lba;
}

INLINE u32 IdeCdRom::GetAccessCount() const
{
    return m_access_count;
}

INLINE bool IdeCdRom::IsBusy() const
{
    return m_state.phase != IDE_CDROM_PHASE_IDLE;
}

INLINE IdeCdRom::IdeCdRom_State* IdeCdRom::GetState()
{
    return &m_state;
}

#endif /* IDE_CDROM_INLINE_H */
