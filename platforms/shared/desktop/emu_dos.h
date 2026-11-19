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

#ifndef EMU_DOS_H
#define EMU_DOS_H

#include <string>
#include "geardox.h"
#include "drive/dos_import.h"
#include "drive/gus_import.h"

#ifdef EMU_DOS_IMPORT
    #define EXTERN
#else
    #define EXTERN extern
#endif

EXTERN std::string emu_dos_get_msdos_path(void);
EXTERN const char* emu_dos_get_msdos_name(void);
EXTERN bool emu_dos_load_msdos(DosImport_System* system, std::string* error);
EXTERN bool emu_dos_import_msdos(const char* path, std::string* message);
EXTERN bool emu_dos_remove_msdos(void);
EXTERN std::string emu_dos_get_ultrasnd_path(void);
EXTERN const char* emu_dos_get_ultrasnd_name(void);
EXTERN bool emu_dos_import_ultrasnd(const char* path, std::string* message);
EXTERN bool emu_dos_remove_ultrasnd(void);

#undef EMU_DOS_IMPORT
#undef EXTERN
#endif /* EMU_DOS_H */
