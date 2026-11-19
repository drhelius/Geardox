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

#define EMU_DOS_IMPORT
#include "emu_dos.h"

#include "config.h"
#include "utils.h"
#include "emu_hard_disk.h"
#include "media/game_descriptor.h"
#include "media/media_file.h"

static std::string msdos_name;
static bool msdos_checked = false;
static std::string ultrasnd_name;
static bool ultrasnd_checked = false;

std::string emu_dos_get_msdos_path(void)
{
    char path[GDOX_MAX_PATH];
    join_path(config_root_path, k_dos_import_file, path, sizeof(path));
    return path;
}

const char* emu_dos_get_msdos_name(void)
{
    if (!msdos_checked)
    {
        DosImport_System system;
        std::string error;
        std::string path = emu_dos_get_msdos_path();
        u64 size = 0;
        s64 modified = 0;
        msdos_checked = true;
        msdos_name.clear();

        if (MediaFile::GetFileInfo(path.c_str(), &size, &modified))
        {
            if (DosImport::Load(path.c_str(), &system, &error))
                msdos_name = system.name;
            else
                Log("%s", error.c_str());
        }
    }

    return msdos_name.c_str();
}

bool emu_dos_load_msdos(DosImport_System* system, std::string* error)
{
    return DosImport::Load(emu_dos_get_msdos_path().c_str(), system, error);
}

bool emu_dos_import_msdos(const char* path, std::string* message)
{
    std::string previous = emu_hard_disk_get_default_path();
    DosImport import;
    message->clear();

    if (!import.AddSource(path) || (import.Import(emu_dos_get_msdos_path().c_str()) != DOS_IMPORT_DONE))
    {
        *message = import.GetError();
        return false;
    }

    msdos_checked = false;
    *message = import.GetName();
    emu_hard_disk_follow_default(previous);
    return true;
}

bool emu_dos_remove_msdos(void)
{
    std::string previous = emu_hard_disk_get_default_path();
    bool removed = MediaFile::RemoveFile(emu_dos_get_msdos_path().c_str());
    msdos_checked = false;
    emu_hard_disk_follow_default(previous);
    return removed;
}

std::string emu_dos_get_ultrasnd_path(void)
{
    char path[GDOX_MAX_PATH];
    join_path(config_root_path, k_gus_import_file, path, sizeof(path));
    return path;
}

const char* emu_dos_get_ultrasnd_name(void)
{
    if (!ultrasnd_checked)
    {
        GusImport_Software software;
        std::string error;
        std::string path = emu_dos_get_ultrasnd_path();
        u64 size = 0;
        s64 modified = 0;
        ultrasnd_checked = true;
        ultrasnd_name.clear();

        if (MediaFile::GetFileInfo(path.c_str(), &size, &modified))
        {
            if (GusImport::Load(path.c_str(), &software, &error))
                ultrasnd_name = software.name;
            else
                Log("%s", error.c_str());
        }
    }

    return ultrasnd_name.c_str();
}

bool emu_dos_import_ultrasnd(const char* path, std::string* message)
{
    GusImport import;
    message->clear();

    if (!import.AddSource(path) || (import.Import(emu_dos_get_ultrasnd_path().c_str()) != GUS_IMPORT_DONE))
    {
        *message = import.GetError();
        return false;
    }

    ultrasnd_checked = false;
    *message = import.GetMessage();
    return true;
}

bool emu_dos_remove_ultrasnd(void)
{
    bool removed = MediaFile::RemoveFile(emu_dos_get_ultrasnd_path().c_str());
    ultrasnd_checked = false;
    return removed;
}
