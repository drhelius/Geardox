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

#ifndef IDE_CONTROLLER_INLINE_H
#define IDE_CONTROLLER_INLINE_H

#include "ide_controller.h"

INLINE int IdeController::GetChannel(u16 port)
{
    return (port & 0x80) != 0 ? 0 : 1;
}

INLINE bool IdeController::IsControlPort(u16 port)
{
    return (port & 0x200) != 0;
}

// A channel with no disk isn't there, and 3F7h is the floppy's
INLINE bool IdeController::Decodes(u16 port) const
{
    if ((port & 0xFF78) != 0x0170 && (port & 0xFF7F) != 0x0376)
        return false;

    int channel = GetChannel(port);
    return m_disks[channel * IDE_CHANNEL_DISKS].IsAttached() || m_disks[(channel * IDE_CHANNEL_DISKS) + 1].IsAttached();
}

// Only the data register is 16 bits wide, the device asserts -I/O CS16 for it alone
INLINE bool IdeController::IsDataPort(u16 port) const
{
    return (port == 0x01F0 || port == 0x0170) && Decodes(port);
}

INLINE IdeDisk& IdeController::GetSelected(int channel)
{
    return m_disks[(channel * IDE_CHANNEL_DISKS) + m_state.selected[channel]];
}

INLINE const IdeDisk& IdeController::GetSelected(int channel) const
{
    return m_disks[(channel * IDE_CHANNEL_DISKS) + m_state.selected[channel]];
}

INLINE IdeDisk* IdeController::GetDisk(int id)
{
    return (id >= 0 && id < IDE_DISKS) ? &m_disks[id] : NULL;
}

INLINE IdeController::IdeController_State* IdeController::GetState()
{
    return &m_state;
}

INLINE bool IdeController::IsInterruptOut(int channel) const
{
    return m_irq_out[channel];
}

// Only events and writes change Status, and the read that just happened took the interrupt
INLINE u64 IdeController::GetReadStableUntil(u16 port) const
{
    return IsControlPort(port) || (port & 0x07) == 7 ? GDOX_NO_EVENT : 0;
}

INLINE void IdeController::Synchronize(u64 clocks)
{
    if (clocks >= m_event_clocks)
        Run(clocks);
}

INLINE void IdeController::HandleEvent(u64 clocks)
{
    Synchronize(clocks);
    UpdateNextEvent();
}

#endif /* IDE_CONTROLLER_INLINE_H */
