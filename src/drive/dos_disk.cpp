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

#include <string.h>
#include <vector>
#include "dos_disk.h"
#include "fat_image.h"
#include "dos_fs.h"
#include "freedos.h"
#include "../media/game_descriptor.h"

static const u8 k_dos_disk_attributes = 0x20;

static bool is_layout(const char* layout)
{
    for (int i = 1; IsValidPointer(layout) && (i < k_keyboard_layout_count); i++)
    {
        if (strcmp(layout, k_keyboard_layouts[i].id) == 0)
            return true;
    }

    return false;
}

bool DosDisk::Write(const char* path, int system, const DosImport_System* msdos, const char* layout, bool replace)
{
    bool ms = system == GDOX_GAME_SYSTEM_MSDOS;
    std::vector<std::string> reserved;
    FatImage image;

    if (ms && !IsValidPointer(msdos))
        return false;

    if (ms && !image.AddMSDOS((const u8*)msdos->zip.data(), msdos->zip.size(), false, reserved, msdos->pinned,
        !msdos->cd_extension))
        return false;

    if (!ms && !image.AddFreeDOS(false, reserved))
        return false;

    if (!image.AddMemoryFile(k_dos_config_sys, GetConfigSys(system, msdos), k_dos_disk_attributes) ||
        !image.AddMemoryFile(k_dos_autoexec_bat, GetAutoexec(system, msdos, layout), k_dos_disk_attributes))
        return false;

    image.FinishGame(ms ? "MS-DOS" : "GEARDOX", ms ? msdos->boot : k_freedos_boot);
    return image.Write(path, k_fat_freedos_size, replace);
}

bool DosDisk::WriteBuiltin(const char* path, int system, const char* msdos_zip, const char* layout, bool replace,
    std::string* error)
{
    DosImport_System msdos;
    error->clear();

    if (system == GDOX_GAME_SYSTEM_MSDOS)
    {
        if (!DosImport::Load(msdos_zip, &msdos, error))
        {
            if (error->empty())
                *error = "MS-DOS isn't imported";

            return false;
        }
    }

    if (Write(path, system, system == GDOX_GAME_SYSTEM_MSDOS ? &msdos : NULL, layout, replace))
        return true;

    *error = std::string("Unable to make ") + path;
    return false;
}

std::string DosDisk::GetConfigSys(int system, const DosImport_System* msdos)
{
    if ((system != GDOX_GAME_SYSTEM_MSDOS) || !IsValidPointer(msdos))
        return dos_fs_config_header(false, 0) + "BUFFERS=20\r\nFILES=40\r\nDOS=HIGH,UMB\r\nDOSDATA=UMB\r\n"
            "DEVICE=C:\\FREEDOS\\BIN\\JEMMEX.EXE NOEMS\r\nIDLEHALT=1\r\n"
            "DEVICEHIGH=C:\\FREEDOS\\BIN\\UDVD2.SYS /D:FDCD0001\r\n" + dos_fs_shell_line(false);

    std::string out = dos_fs_config_header(true, msdos->version) + "BUFFERS=20\r\nFILES=40\r\nDOS=HIGH,UMB\r\n";
    out += dos_fs_himem_line(msdos->version);
    out += "DEVICE=C:\\DOS\\EMM386.EXE NOEMS\r\nDEVICEHIGH=C:\\GEARDOX\\GDXIDLE.SYS\r\n"
        "DEVICEHIGH=C:\\GEARDOX\\UDVD2.SYS /D:FDCD0001\r\n";
    return out + dos_fs_shell_line(true);
}

std::string DosDisk::GetAutoexec(int system, const DosImport_System* msdos, const char* layout)
{
    bool ms = (system == GDOX_GAME_SYSTEM_MSDOS) && IsValidPointer(msdos);
    std::string out = dos_fs_autoexec_header(ms);
    out += "SET BLASTER=A220 I5 D1 H5 P330\r\n";
    out += "GDXGUS 240,3,3,7,7\r\nIF NOT ERRORLEVEL 1 SET ULTRASND=240,3,3,7,7\r\n";
    out += "IF NOT ERRORLEVEL 1 SET ULTRADIR=C:\\ULTRASND\r\nPROMPT $P$G\r\n";

    if (is_layout(layout))
        out += dos_fs_keyb_line(to_upper_ascii(layout), ms);

    out += "LH CTMOUSE\r\n";
    out += (ms && msdos->cd_extension) ? "LH MSCDEX /D:FDCD0001\r\n" : "LH SHSUCDX /QQ /D:FDCD0001\r\n";
    return out;
}
