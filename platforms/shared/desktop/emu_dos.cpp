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

static void follow_builtin(int previous);

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
    int previous = emu_dos_get_builtin();
    DosImport import;
    message->clear();

    if (!import.AddSource(path) || (import.Import(emu_dos_get_msdos_path().c_str()) != DOS_IMPORT_DONE))
    {
        *message = import.GetError();
        return false;
    }

    msdos_checked = false;
    *message = import.GetName();
    follow_builtin(previous);
    return true;
}

bool emu_dos_remove_msdos(void)
{
    int previous = emu_dos_get_builtin();
    bool removed = MediaFile::RemoveFile(emu_dos_get_msdos_path().c_str());
    msdos_checked = false;
    follow_builtin(previous);
    return removed;
}

int emu_dos_get_builtin(void)
{
    if (config_emulator.hard_disk_builtin == config_BuiltinDOS_None)
        return -1;

    if ((config_emulator.hard_disk_builtin == config_BuiltinDOS_MSDOS) && (emu_dos_get_msdos_name()[0] != 0))
        return GDOX_GAME_SYSTEM_MSDOS;

    return GDOX_GAME_SYSTEM_FREEDOS;
}

void emu_dos_set_builtin(int choice)
{
    int previous = emu_dos_get_builtin();
    config_emulator.hard_disk_builtin = CLAMP(choice, (int)config_BuiltinDOS_MSDOS, (int)config_BuiltinDOS_None);
    follow_builtin(previous);
}

static void follow_builtin(int previous)
{
    int current = emu_dos_get_builtin();

    if ((current >= 0) && (current != previous))
        emu_hard_disk_swap_builtin(current);
}
