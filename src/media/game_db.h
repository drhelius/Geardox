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

#ifndef GAME_DB_H
#define GAME_DB_H

#include "../common/common.h"

struct GDOX_DB_Entry
{
    u32 crc;
    const char* title;
    u16 flags;
    u8 model;
    u8 ram_mb;
    u8 cpu_mhz;
    u8 port_1;
    u8 port_2;
    u32 fallback_key;
};

#define GDOX_GAMEDB_FLAG_NONE                0x0000
#define GDOX_GAMEDB_HDD                      0x0001
#define GDOX_GAMEDB_CPU_486                  0x0002
#define GDOX_GAMEDB_FPU                      0x0004
#define GDOX_GAMEDB_FLOPPY_BOOT              0x0010
#define GDOX_GAMEDB_FLOPPY_REQUIRED          0x0020
#define GDOX_GAMEDB_FLOPPY_OPTIONAL          0x0040
#define GDOX_GAMEDB_FLOPPY_PROTECTED         0x0080
#define GDOX_GAMEDB_FLOPPY_MASK              0x00F0
#define GDOX_GAMEDB_FIRMWARE_SYSTEM_BIOS     0x0100
#define GDOX_GAMEDB_FIRMWARE_VGA_BIOS        0x0200
#define GDOX_GAMEDB_FIRMWARE_MASK            0x0300

#define GDOX_GAMEDB_PORT_DEFAULT             0
#define GDOX_GAMEDB_PORT_NONE                1
#define GDOX_GAMEDB_PORT_PAD                 2
#define GDOX_GAMEDB_PORT_MOUSE               3

const GDOX_DB_Entry k_game_database[] =
{
    { 0, 0, GDOX_GAMEDB_FLAG_NONE, 0, 0, 0, 0, 0, 0 }
};

const GDOX_DB_Entry k_floppy_database[] =
{
    { 0, 0, GDOX_GAMEDB_FLAG_NONE, 0, 0, 0, 0, 0, 0 }
};

#endif /* GAME_DB_H */
