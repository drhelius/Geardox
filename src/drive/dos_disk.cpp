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

static std::string upper(const std::string& text)
{
    std::string result(text);

    for (size_t i = 0; i < result.size(); i++)
        result[i] = ((result[i] >= 'a') && (result[i] <= 'z')) ? (char)(result[i] - 32) : result[i];

    return result;
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

    if (!image.AddMemoryFile("CONFIG.SYS", GetConfigSys(system, msdos), k_dos_disk_attributes) ||
        !image.AddMemoryFile("AUTOEXEC.BAT", GetAutoexec(system, msdos, layout), k_dos_disk_attributes))
        return false;

    image.FinishGame(ms ? "MS-DOS" : "GEARDOX", 0, ms ? msdos->boot : k_freedos_boot);
    return image.Write(path, k_fat_freedos_size, replace);
}

std::string DosDisk::GetConfigSys(int system, const DosImport_System* msdos)
{
    if ((system != GDOX_GAME_SYSTEM_MSDOS) || !IsValidPointer(msdos))
        return "SWITCHES=/F\r\nLASTDRIVE=Z\r\nBUFFERS=20\r\nFILES=40\r\nDOS=HIGH,UMB\r\nDOSDATA=UMB\r\n"
            "DEVICE=C:\\FREEDOS\\BIN\\JEMMEX.EXE NOEMS\r\nIDLEHALT=1\r\n"
            "DEVICEHIGH=C:\\FREEDOS\\BIN\\UDVD2.SYS /D:FDCD0001\r\n"
            "SHELL=C:\\FREEDOS\\BIN\\COMMAND.COM C:\\FREEDOS\\BIN /E:1024 /P\r\n";

    std::string out = msdos->version >= 600 ? "SWITCHES=/F\r\n" : "";
    out += "LASTDRIVE=Z\r\nBUFFERS=20\r\nFILES=40\r\nDOS=HIGH,UMB\r\n";
    out += msdos->version >= 620 ? "DEVICE=C:\\DOS\\HIMEM.SYS /TESTMEM:OFF\r\n" : "DEVICE=C:\\DOS\\HIMEM.SYS\r\n";
    out += "DEVICE=C:\\DOS\\EMM386.EXE NOEMS\r\nDEVICEHIGH=C:\\GEARDOX\\GDXIDLE.SYS\r\n"
        "DEVICEHIGH=C:\\GEARDOX\\UDVD2.SYS /D:FDCD0001\r\nSHELL=C:\\COMMAND.COM C:\\ /E:1024 /P\r\n";
    return out;
}

std::string DosDisk::GetAutoexec(int system, const DosImport_System* msdos, const char* layout)
{
    bool ms = (system == GDOX_GAME_SYSTEM_MSDOS) && IsValidPointer(msdos);
    std::string out = ms ? "@ECHO OFF\r\nSET PATH=C:\\DOS;C:\\GEARDOX\r\n" :
        "@ECHO OFF\r\nSET DOSDIR=C:\\FREEDOS\r\nSET PATH=C:\\FREEDOS\\BIN\r\n";
    out += "SET TEMP=C:\\TEMP\r\nSET DIRCMD=/OGN\r\nSET BLASTER=A220 I5 D1 H5 P330\r\nPROMPT $P$G\r\n";

    if (is_layout(layout))
        out += "LH KEYB " + upper(layout) + (ms ? ",437,C:\\DOS\\KEYBOARD.SYS\r\n" :
            ",437,C:\\FREEDOS\\BIN\\KEYBOARD.SYS\r\n");

    out += "LH CTMOUSE\r\n";
    out += (ms && msdos->cd_extension) ? "LH MSCDEX /D:FDCD0001\r\n" : "LH SHSUCDX /QQ /D:FDCD0001\r\n";
    return out;
}
