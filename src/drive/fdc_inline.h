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

#ifndef FDC_INLINE_H
#define FDC_INLINE_H

#include "fdc.h"

INLINE FloppyDisk* FDC::GetDisk(int drive)
{
    return (drive >= 0 && drive < FDC_DRIVES) ? &m_disks[drive] : NULL;
}

INLINE FDC::FDC_State* FDC::GetState()
{
    return &m_state;
}

INLINE u32 FDC::GetAccessCount(int drive) const
{
    return (drive >= 0 && drive < FDC_DRIVES) ? m_access_count[drive] : 0;
}

INLINE u32 FDC::GetWriteCount(int drive) const
{
    return (drive >= 0 && drive < FDC_DRIVES) ? m_write_count[drive] : 0;
}

#endif /* FDC_INLINE_H */
