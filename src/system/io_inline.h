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

#ifndef IO_INLINE_H
#define IO_INLINE_H

#include "io.h"
#include "firmware_config.h"
#include "pci_bus.h"
#include "../drive/ide_controller.h"

// The IDE data port answers with -I/O CS16, every other port is 8 bits wide
INLINE u16 IO::Read16(u16 port, GDOX_Bus_Access_Context& context)
{
    if (m_ide->IsDataPort(port & k_io_isa_mask))
        return ReadIDEData(port & k_io_isa_mask, context);

    u16 value = Read8(port, context);
    value |= (u16)Read8((u16)(port + 1), context) << 8;
    return value;
}

INLINE u32 IO::Read32(u16 port, GDOX_Bus_Access_Context& context)
{
    if (port == k_pci_bus_address_port && m_pci_bus->IsEnabled())
        return m_pci_bus->ReadAddress();

    u32 value = Read16(port, context);
    value |= (u32)Read16((u16)(port + 2), context) << 16;
    return value;
}

// The firmware config selector is a 16-bit register, as the IDE data port
INLINE void IO::Write16(u16 port, u16 value, GDOX_Bus_Access_Context& context)
{
    if (port == k_firmware_config_selector_port && m_firmware_config->Decodes(port))
    {
        m_firmware_config->WriteSelector(value);
        return;
    }

    if (m_ide->IsDataPort(port & k_io_isa_mask))
    {
        WriteIDEData(port & k_io_isa_mask, value, context);
        return;
    }

    Write8(port, (u8)value, context);
    Write8((u16)(port + 1), (u8)(value >> 8), context);
}

INLINE void IO::Write32(u16 port, u32 value, GDOX_Bus_Access_Context& context)
{
    if (port == k_pci_bus_address_port && m_pci_bus->IsEnabled())
    {
        m_pci_bus->WriteAddress(value);
        return;
    }

    Write16(port, (u16)value, context);
    Write16((u16)(port + 2), (u16)(value >> 16), context);
}

#endif /* IO_INLINE_H */
