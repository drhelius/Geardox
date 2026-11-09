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

#include "firmware.h"
#include "seabios.h"
#include "crc.h"

Firmware::Firmware()
{
    Init();
}

Firmware::~Firmware()
{
}

void Firmware::Init()
{
    memset(m_info, 0, sizeof(m_info));
    InitPointer(m_system_rom);
    InitPointer(m_vga_rom);
    m_ready = false;
    m_cirrus = false;
}

// SeaBIOS and SeaVGABIOS built from the pinned SeaBIOS commit, compiled in. The VGA BIOS is built for the card:
// SeaVGABIOS for a plain VGA or for the Cirrus Logic GD5428
void Firmware::Load(bool cirrus)
{
    m_system_rom = k_seabios_bios;
    m_vga_rom = cirrus ? k_seabios_vgabios_cirrus : k_seabios_vgabios;
    m_cirrus = cirrus;

    SetInfo(GDOX_FIRMWARE_SYSTEM_BIOS, "SeaBIOS", m_system_rom, k_seabios_bios_size);
    SetInfo(GDOX_FIRMWARE_VGA_BIOS, cirrus ? "SeaVGABIOS CL-GD5428" : "SeaVGABIOS", m_vga_rom,
        cirrus ? k_seabios_vgabios_cirrus_size : k_seabios_vgabios_size);

    m_ready = true;
    Log("Firmware loaded: built-in %s, %s", m_info[GDOX_FIRMWARE_SYSTEM_BIOS].name, m_info[GDOX_FIRMWARE_VGA_BIOS].name);
}

void Firmware::SetInfo(GDOX_Firmware_Type type, const char* name, const u8* data, u32 size)
{
    GDOX_Firmware_Info& info = m_info[type];
    memset(&info, 0, sizeof(info));
    snprintf(info.name, sizeof(info.name), "%s %s", name, k_seabios_version);
    info.size = (int)size;
    info.crc = CalculateCRC32(0, data, size);
    info.loaded = true;
}

const char* Firmware::GetComponentName(GDOX_Firmware_Type type)
{
    switch (type)
    {
        case GDOX_FIRMWARE_SYSTEM_BIOS:
            return "System BIOS";
        case GDOX_FIRMWARE_VGA_BIOS:
            return "VGA BIOS";
        default:
            return "Unknown ROM";
    }
}
