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

#ifndef PCI_BUS_H
#define PCI_BUS_H

#include <iostream>
#include "../common/common.h"

class StateSerializer;

typedef u8 (*GDOX_Pci_Read_Fn)(void* device, u8 function, u8 offset);
typedef void (*GDOX_Pci_Write_Fn)(void* device, u8 function, u8 offset, u8 value);

#define PCI_BUS_DEVICES 32

class PciBus
{
public:
    struct PciBus_State
    {
        u32 address;
    };

public:
    PciBus();
    ~PciBus();
    void Init();
    void Reset();
    void SetEnabled(bool enabled);
    bool IsEnabled() const;
    void RegisterDevice(int device, void* context, GDOX_Pci_Read_Fn read, GDOX_Pci_Write_Fn write);
    bool Decodes(u16 port) const;
    u32 ReadAddress() const;
    void WriteAddress(u32 value);
    u8 ReadData(u16 port) const;
    bool HasDevice(int device) const;
    u8 PeekConfig(int device, u8 function, u8 offset) const;
    void WriteData(u16 port, u8 value);
    PciBus_State* GetState();
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream, int version = GDOX_SAVESTATE_VERSION);

private:
    int Select(u16 port, u8& function, u8& offset) const;
    void Serialize(StateSerializer& serializer);

private:
    PciBus_State m_state;
    bool m_enabled;
    void* m_contexts[PCI_BUS_DEVICES];
    GDOX_Pci_Read_Fn m_reads[PCI_BUS_DEVICES];
    GDOX_Pci_Write_Fn m_writes[PCI_BUS_DEVICES];
};

static const u16 k_pci_bus_address_port = 0x0CF8;
static const u16 k_pci_bus_data_port = 0x0CFC;
static const u32 k_pci_bus_enable = 0x80000000;
static const u32 k_pci_bus_address_mask = 0x80FFFFFC;
static const int k_pci_bus_last_device = 20;
static const int k_pci_device_host_bridge = 0;
static const int k_pci_device_piix = 7;
static const int k_pci_device_slot = 8;

#endif /* PCI_BUS_H */
