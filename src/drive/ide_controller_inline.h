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

// A channel with no device isn't there, and 3F7h is the floppy's
INLINE bool IdeController::Decodes(u16 port) const
{
    if ((port & 0xFF78) != 0x0170 && (port & 0xFF7F) != 0x0376)
        return false;

    int channel = GetChannel(port);
    return IsDeviceAttached(channel * IDE_CHANNEL_DISKS) || IsDeviceAttached((channel * IDE_CHANNEL_DISKS) + 1);
}

// Only the data register is 16 bits wide, the device asserts -I/O CS16 for it alone
INLINE bool IdeController::IsDataPort(u16 port) const
{
    return (port == 0x01F0 || port == 0x0170) && Decodes(port);
}

INLINE int IdeController::GetSelected(int channel) const
{
    return (channel * IDE_CHANNEL_DISKS) + m_state.selected[channel];
}

// The CD-ROM takes its position, and a disk image left there isn't seen
INLINE bool IdeController::IsDeviceAttached(int slot) const
{
    return slot == m_cdrom_slot ? m_cdrom.IsAttached() : m_disks[slot].IsAttached();
}

INLINE u8 IdeController::GetDeviceStatus(int slot) const
{
    return slot == m_cdrom_slot ? m_cdrom.GetStatus() : m_disks[slot].GetStatus();
}

INLINE u8 IdeController::ReadDeviceRegister(int slot, int offset) const
{
    return slot == m_cdrom_slot ? m_cdrom.ReadRegister(offset) : m_disks[slot].ReadRegister(offset);
}

INLINE void IdeController::WriteDeviceRegister(int slot, int offset, u8 value)
{
    if (slot == m_cdrom_slot)
        m_cdrom.WriteRegister(offset, value);
    else
        m_disks[slot].WriteRegister(offset, value);
}

INLINE void IdeController::ExecuteDevice(int slot, u8 command, u8 features, u64 clocks)
{
    if (slot == m_cdrom_slot)
        m_cdrom.Execute(command, features, clocks);
    else
        m_disks[slot].Execute(command, features, clocks);
}

INLINE bool IdeController::ReadDeviceData(int slot, u16& value, u64 clocks)
{
    return slot == m_cdrom_slot ? m_cdrom.ReadData(value, clocks) : m_disks[slot].ReadData(value, clocks);
}

INLINE bool IdeController::WriteDeviceData(int slot, u16 value, u64 clocks)
{
    return slot == m_cdrom_slot ? m_cdrom.WriteData(value, clocks) : m_disks[slot].WriteData(value, clocks);
}

INLINE void IdeController::HoldDeviceReset(int slot)
{
    if (slot == m_cdrom_slot)
        m_cdrom.HoldReset();
    else
        m_disks[slot].HoldReset();
}

INLINE void IdeController::ReleaseDeviceReset(int slot, u64 clocks)
{
    if (slot == m_cdrom_slot)
        m_cdrom.ReleaseReset(clocks);
    else
        m_disks[slot].ReleaseReset(clocks);
}

INLINE u64 IdeController::GetDeviceEventClocks(int slot) const
{
    return slot == m_cdrom_slot ? m_cdrom.GetEventClocks() : m_disks[slot].GetEventClocks();
}

INLINE void IdeController::RunDeviceEvent(int slot)
{
    if (slot == m_cdrom_slot)
        m_cdrom.RunEvent();
    else
        m_disks[slot].RunEvent();
}

INLINE bool IdeController::IsDeviceInterrupting(int slot) const
{
    return slot == m_cdrom_slot ? m_cdrom.IsInterrupting() : m_disks[slot].IsInterrupting();
}

INLINE void IdeController::ClearDeviceInterrupt(int slot)
{
    if (slot == m_cdrom_slot)
        m_cdrom.ClearInterrupt();
    else
        m_disks[slot].ClearInterrupt();
}

INLINE IdeDisk* IdeController::GetDisk(int id)
{
    return (id >= 0 && id < IDE_DISKS) ? &m_disks[id] : NULL;
}

INLINE IdeCdRom* IdeController::GetCdRom()
{
    return &m_cdrom;
}

INLINE int IdeController::GetCdRomSlot() const
{
    return m_cdrom_slot;
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
