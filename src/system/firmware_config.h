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


#ifndef FIRMWARE_CONFIG_H
#define FIRMWARE_CONFIG_H

#include <iostream>
#include "../common/common.h"

class StateSerializer;

#define FIRMWARE_CONFIG_FILE_COUNT 5
#define FIRMWARE_CONFIG_E820_ENTRIES 2
#define FIRMWARE_CONFIG_E820_ENTRY_SIZE 20
#define FIRMWARE_CONFIG_FILE_ENTRY_SIZE 64

class FirmwareConfig
{
public:
    struct FirmwareConfig_State
    {
        u16 selector;
        u32 offset;
    };

public:
    FirmwareConfig();
    ~FirmwareConfig();
    void Init();
    void Reset();
    void SetEnabled(bool enabled);
    bool IsEnabled() const;
    void SetVGABIOS(const u8* data, u32 size, bool cga);
    void SetBootMenu(bool show, u16 wait_ms);
    void SetMemoryHole(u32 ram_size, bool hole);
    void SetGameTarget(int target);
    bool Decodes(u16 port) const;
    u8 Read(u16 port);
    u8 Peek(u16 port) const;
    void Write(u16 port, u8 value);
    void WriteSelector(u16 selector);
    FirmwareConfig_State* GetState();
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream, int version = GDOX_SAVESTATE_VERSION);

private:
    bool GetEntry(u16 selector, const u8*& data, u32& size) const;
    void BuildDirectory();
    void Serialize(StateSerializer& serializer);

private:
    FirmwareConfig_State m_state;
    bool m_enabled;
    const u8* m_vga_bios;
    u32 m_vga_bios_size;
    bool m_cga_bios;
    u8 m_show_menu[2];
    u8 m_menu_wait[2];
    u8 m_e820[FIRMWARE_CONFIG_E820_ENTRIES * FIRMWARE_CONFIG_E820_ENTRY_SIZE];
    u32 m_e820_size;
    u8 m_game_target;
    bool m_game;
    u8 m_directory[4 + FIRMWARE_CONFIG_FILE_COUNT * FIRMWARE_CONFIG_FILE_ENTRY_SIZE];
};

// The fw_cfg I/O interface SeaBIOS reads: the selector at 510h, the data at 511h, no DMA
static const u16 k_firmware_config_selector_port = 0x0510;
static const u16 k_firmware_config_data_port = 0x0511;

#include "firmware_config_inline.h"

#endif /* FIRMWARE_CONFIG_H */
