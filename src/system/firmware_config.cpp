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


#include "firmware_config.h"
#include "../common/state_serializer.h"

static const u16 k_firmware_config_signature = 0x0000;
static const u16 k_firmware_config_id = 0x0001;
static const u16 k_firmware_config_boot_menu = 0x000E;
static const u16 k_firmware_config_file_dir = 0x0019;
static const u16 k_firmware_config_file_first = 0x0020;
static const u8 k_firmware_config_signature_data[4] = { 'Q', 'E', 'M', 'U' };
static const u8 k_firmware_config_id_data[4] = { 0x01, 0x00, 0x00, 0x00 };
static const char* const k_firmware_config_file_names[FIRMWARE_CONFIG_FILE_COUNT] =
{
    "vgaroms/vgabios.bin", "etc/show-boot-menu", "etc/boot-menu-wait", "etc/e820"
};

FirmwareConfig::FirmwareConfig()
{
    m_enabled = false;
    InitPointer(m_vga_bios);
    m_vga_bios_size = 0;
    memset(m_e820, 0, sizeof(m_e820));
    m_e820_size = 0;
    m_state.selector = 0;
    m_state.offset = 0;
    SetBootMenu(false, 0);
}

FirmwareConfig::~FirmwareConfig()
{
}

void FirmwareConfig::Init()
{
    Reset();
}

void FirmwareConfig::Reset()
{
    m_state.selector = 0;
    m_state.offset = 0;
}

void FirmwareConfig::SetVGABIOS(const u8* data, u32 size)
{
    m_vga_bios = data;
    m_vga_bios_size = IsValidPointer(data) ? size : 0;
    BuildDirectory();
}

void FirmwareConfig::SetBootMenu(bool show, u16 wait_ms)
{
    m_show_menu[0] = show ? 1 : 0;
    m_show_menu[1] = 0;
    m_menu_wait[0] = (u8)wait_ms;
    m_menu_wait[1] = (u8)(wait_ms >> 8);
    BuildDirectory();
}

// With a memory hole at 15 MB for a video card's linear frame buffer, SeaBIOS takes its map from etc/e820: RAM below
// 15 MB and from 16 MB up, little endian address, length and type. Without one the file is left out and SeaBIOS reads
// the CMOS as before
void FirmwareConfig::SetMemoryHole(u32 ram_size, bool hole)
{
    static const u32 k_hole_start = 15 * 1024 * 1024;
    static const u32 k_hole_end = 16 * 1024 * 1024;

    memset(m_e820, 0, sizeof(m_e820));
    m_e820_size = 0;

    if (hole)
    {
        u32 starts[FIRMWARE_CONFIG_E820_ENTRIES] = { 0, k_hole_end };
        u32 lengths[FIRMWARE_CONFIG_E820_ENTRIES] = { k_hole_start, ram_size > k_hole_end ? ram_size - k_hole_end : 0 };

        for (int i = 0; i < FIRMWARE_CONFIG_E820_ENTRIES; i++)
        {
            if (lengths[i] == 0)
                continue;

            u8* entry = m_e820 + m_e820_size;

            for (int b = 0; b < 4; b++)
            {
                entry[b] = (u8)(starts[i] >> (b * 8));
                entry[8 + b] = (u8)(lengths[i] >> (b * 8));
            }

            entry[16] = 1;
            m_e820_size += FIRMWARE_CONFIG_E820_ENTRY_SIZE;
        }
    }

    BuildDirectory();
}

u8 FirmwareConfig::Read(u16 port)
{
    u8 value = Peek(port);

    if (port == k_firmware_config_data_port)
        m_state.offset++;

    return value;
}

// Unknown selectors and reads past the end of an item read as zero
u8 FirmwareConfig::Peek(u16 port) const
{
    if (port != k_firmware_config_data_port)
        return 0xFF;

    const u8* data = NULL;
    u32 size = 0;

    if (!GetEntry(m_state.selector, data, size) || m_state.offset >= size)
        return 0x00;

    return data[m_state.offset];
}

// A byte write to the selector port sets the low byte, the data port takes no writes
void FirmwareConfig::Write(u16 port, u8 value)
{
    if (port == k_firmware_config_selector_port)
        WriteSelector(value);
}

void FirmwareConfig::WriteSelector(u16 selector)
{
    m_state.selector = selector;
    m_state.offset = 0;
}

bool FirmwareConfig::GetEntry(u16 selector, const u8*& data, u32& size) const
{
    switch (selector)
    {
        case k_firmware_config_signature:
            data = k_firmware_config_signature_data;
            size = sizeof(k_firmware_config_signature_data);
            return true;
        case k_firmware_config_id:
            data = k_firmware_config_id_data;
            size = sizeof(k_firmware_config_id_data);
            return true;
        case k_firmware_config_boot_menu:
            data = m_show_menu;
            size = sizeof(m_show_menu);
            return true;
        case k_firmware_config_file_dir:
            data = m_directory;
            size = sizeof(m_directory);
            return true;
        case k_firmware_config_file_first:
            data = m_vga_bios;
            size = m_vga_bios_size;
            return IsValidPointer(data);
        case k_firmware_config_file_first + 1:
            data = m_show_menu;
            size = sizeof(m_show_menu);
            return true;
        case k_firmware_config_file_first + 2:
            data = m_menu_wait;
            size = sizeof(m_menu_wait);
            return true;
        case k_firmware_config_file_first + 3:
            data = m_e820;
            size = m_e820_size;
            return m_e820_size != 0;
        default:
            return false;
    }
}

// Big endian count, then size, selector, reserved and a 56-byte name per file; empty files are left out
void FirmwareConfig::BuildDirectory()
{
    u32 sizes[FIRMWARE_CONFIG_FILE_COUNT] = { m_vga_bios_size, sizeof(m_show_menu), sizeof(m_menu_wait), m_e820_size };
    u32 count = 0;

    memset(m_directory, 0, sizeof(m_directory));

    for (int i = 0; i < FIRMWARE_CONFIG_FILE_COUNT; i++)
    {
        if (sizes[i] == 0)
            continue;

        u8* entry = m_directory + 4 + count * FIRMWARE_CONFIG_FILE_ENTRY_SIZE;
        count++;
        u16 selector = (u16)(k_firmware_config_file_first + i);
        entry[0] = (u8)(sizes[i] >> 24);
        entry[1] = (u8)(sizes[i] >> 16);
        entry[2] = (u8)(sizes[i] >> 8);
        entry[3] = (u8)sizes[i];
        entry[4] = (u8)(selector >> 8);
        entry[5] = (u8)selector;
        strncpy_fit((char*)entry + 8, k_firmware_config_file_names[i], FIRMWARE_CONFIG_FILE_ENTRY_SIZE - 8);
    }

    m_directory[3] = (u8)count;
}

void FirmwareConfig::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
}

void FirmwareConfig::LoadState(std::istream& stream, int version)
{
    UNUSED(version);
    StateSerializer serializer(stream);
    Serialize(serializer);
}

void FirmwareConfig::Serialize(StateSerializer& serializer)
{
    G_SERIALIZE(serializer, m_state.selector);
    G_SERIALIZE(serializer, m_state.offset);
}
