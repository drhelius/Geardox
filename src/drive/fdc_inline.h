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

// 3F6h is the fixed disk's
INLINE bool FDC::Decodes(u16 port) const
{
    return port >= 0x03F0 && port <= 0x03F7 && port != 0x03F6;
}

INLINE FloppyDisk* FDC::GetDisk(int drive)
{
    return (drive >= 0 && drive < FDC_DRIVES) ? &m_disks[drive] : NULL;
}

INLINE I82077* FDC::GetController()
{
    return &m_chip;
}

INLINE FDC::FDC_State* FDC::GetState()
{
    return &m_state;
}

INLINE u8 FDC::GetDriveType(int drive) const
{
    return (drive >= 0 && drive < FDC_DRIVES) ? m_drive_types[drive] : (u8)GDOX_FLOPPY_NONE;
}

INLINE u32 FDC::GetAccessCount(int drive) const
{
    return (drive >= 0 && drive < FDC_DRIVES) ? m_access_count[drive] : 0;
}

INLINE u32 FDC::GetWriteCount(int drive) const
{
    return (drive >= 0 && drive < FDC_DRIVES) ? m_write_count[drive] : 0;
}

// Only events and writes change the main status and the change line
INLINE u64 FDC::GetReadStableUntil(u16 port) const
{
    return port == 0x03F4 || port == 0x03F7 ? GDOX_NO_EVENT : 0;
}

// The DOR picks the drive, and a stopped motor gates its select line
INLINE int FDC::GetSelectedDrive() const
{
    u8 dor = m_chip.GetDOR();
    int drive = dor & 0x03;

    if ((dor & (0x10 << drive)) == 0 || drive >= m_internal_drives || drive >= FDC_DRIVES ||
        m_drive_types[drive] == GDOX_FLOPPY_NONE)
        return -1;

    return drive;
}

INLINE bool FDC::HasIndex() const
{
    int drive = GetSelectedDrive();
    return drive >= 0 && m_disks[drive].IsInserted();
}

INLINE bool FDC::IsTrack0() const
{
    int drive = GetSelectedDrive();
    return drive >= 0 && m_state.cylinders[drive] == 0;
}

INLINE bool FDC::IsWriteProtected() const
{
    int drive = GetSelectedDrive();
    return drive >= 0 && m_disks[drive].IsWriteProtected();
}

INLINE u32 FDC::GetRPM(int drive) const
{
    return k_fdc_drives[m_drive_types[drive]].rpm;
}

// Bytes pass at the data rate, FM at half of it
INLINE u64 FDC::GetByteClocks(u32 count, u32 rate, bool mfm) const
{
    return ((u64)count * GDOX_MACHINE_CLOCK_RATE * (mfm ? 8 : 16)) / ((u64)rate * 1000);
}

#endif /* FDC_INLINE_H */
