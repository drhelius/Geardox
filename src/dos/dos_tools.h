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

#ifndef DOS_TOOLS_H
#define DOS_TOOLS_H

#include "../common/types.h"

extern const u16 k_dos_tools_date;
extern const u16 k_dos_tools_time;
extern const u8 k_dos_gdxrun[];
extern const u32 k_dos_gdxrun_size;
extern const u8 k_dos_poweroff[];
extern const u32 k_dos_poweroff_size;
extern const u8 k_dos_gdxidle[];
extern const u32 k_dos_gdxidle_size;
extern const u8 k_dos_gdxpad[];
extern const u32 k_dos_gdxpad_size;
extern const u8 k_dos_no_system[];
extern const u32 k_dos_no_system_size;
extern const u8 k_dos_gdxdbg[];
extern const u32 k_dos_gdxdbg_size;
extern const u8 k_dos_gdxgus[];
extern const u32 k_dos_gdxgus_size;

#endif /* DOS_TOOLS_H */
