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

#ifndef DOS_DISK_H
#define DOS_DISK_H

#include <string>
#include "../common/common.h"
#include "dos_import.h"

class DosDisk
{
public:
    static bool Write(const char* path, int system, const DosImport_System* msdos, const char* layout, bool replace);
    static std::string GetConfigSys(int system, const DosImport_System* msdos);
    static std::string GetAutoexec(int system, const DosImport_System* msdos, const char* layout);
};

static const char k_dos_disk_freedos[] = "FreeDOS.hdd";
static const char k_dos_disk_msdos[] = "MS-DOS.hdd";

#endif /* DOS_DISK_H */
