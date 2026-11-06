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

#ifndef IDE_DISK_INLINE_H
#define IDE_DISK_INLINE_H

#include "ide_disk.h"

INLINE HardDiskImage* IdeDisk::GetImage()
{
    return &m_image;
}

INLINE u8 IdeDisk::GetStatus() const
{
    return m_state.status;
}

INLINE u64 IdeDisk::GetEventClocks() const
{
    return m_state.event_clocks;
}

INLINE bool IdeDisk::IsInterrupting() const
{
    return m_state.interrupt;
}

INLINE void IdeDisk::ClearInterrupt()
{
    m_state.interrupt = false;
}

// SeaBIOS reads two bits per disk from CMOS 39h: 0 is none, 1 is LBA
INLINE u8 IdeDisk::GetTranslation() const
{
    return m_state.cylinders > 1024 ? 1 : 0;
}

INLINE u32 IdeDisk::GetAccessCount() const
{
    return m_access_count;
}

INLINE IdeDisk::IdeDisk_State* IdeDisk::GetState()
{
    return &m_state;
}

#endif /* IDE_DISK_INLINE_H */
