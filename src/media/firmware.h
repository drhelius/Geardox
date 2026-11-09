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

#ifndef FIRMWARE_H
#define FIRMWARE_H

#include "../common/common.h"

class Firmware
{
public:
    Firmware();
    ~Firmware();
    void Init();
    void Load(bool cirrus);
    bool IsReady() const;
    bool IsCirrus() const;
    const GDOX_Firmware_Info& GetInfo(GDOX_Firmware_Type type) const;
    const u8* GetSystemRom() const;
    const u8* GetVGARom() const;

    static const char* GetComponentName(GDOX_Firmware_Type type);

private:
    void SetInfo(GDOX_Firmware_Type type, const char* name, const u8* data, u32 size);

private:
    GDOX_Firmware_Info m_info[GDOX_FIRMWARE_COUNT];
    const u8* m_system_rom;
    const u8* m_vga_rom;
    bool m_ready;
    bool m_cirrus;
};

#include "firmware_inline.h"

#endif /* FIRMWARE_H */
