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

#define EMU_HARD_DISK_IMPORT
#include "emu_hard_disk.h"

#include <string>
#include <SDL3/SDL.h>
#include "emu.h"
#include "config.h"
#include "utils.h"
#include "emu_dos.h"
#include "emu_floppy.h"
#include "drive/dos_disk.h"
#include "drive/fat_image.h"
#include "drive/folder_disk.h"
#include "drive/hard_disk_image.h"
#include "media/game_descriptor.h"
#include "media/media_file.h"
#include "common/idle_flush.h"

static IdleFlush idle_flush;
static bool flush_error = false;
static u32 state_changes = 0;
static bool rewind_changed = false;
static bool game_disks[GDOX_MAX_HARD_DISKS];
static Emu_HardDiskSource prepared_source;
static FolderDisk* prepared_disk = NULL;
static HardDiskImage* prepared_images[GDOX_MAX_HARD_DISKS];
static std::string prepared_working_paths[GDOX_MAX_HARD_DISKS];
static bool prepared_write_protected[GDOX_MAX_HARD_DISKS];
static std::string builtin_error;
static std::string fallback_error;

static bool valid_id(int id);
static std::string get_working_path(const char* path);
static std::string get_builtin_path(int system);
static int get_builtin_system(const char* path);
static bool make_builtin(int system, bool replace);
static bool attach_default(int id, int choice);
static bool is_generated(int id, const char* path);
static bool is_under(const std::string& path, const std::string& folder);

bool emu_hard_disk_is_image(const char* path)
{
    return HardDiskImage::IsImage(path);
}

int emu_hard_disk_get_image_id(const char* path)
{
    if (!emu_hard_disk_is_image(path))
        return -1;

    if (HardDiskImage::GetImageId(path) >= 0)
        return HardDiskImage::GetImageId(path);

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        Emu_HardDiskInfo info;
        emu_hard_disk_get_info(i, &info);

        if (emu_hard_disk_is_cdrom_position(i))
            continue;

        const char* attached = info.pending ? info.pending_path : info.path;

        if ((info.pending ? !info.pending_attached : !info.attached) || info.builtin ||
            (emu_hard_disk_get_default_path() == attached))
            return i;
    }

    return -1;
}

int emu_hard_disk_get_free_id(void)
{
    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        Emu_HardDiskInfo info;
        emu_hard_disk_get_info(i, &info);

        if (emu_hard_disk_is_cdrom_position(i))
            continue;

        if (info.pending ? !info.pending_attached : !info.attached)
            return i;
    }

    return -1;
}

bool emu_hard_disk_is_cdrom_position(int id)
{
    return valid_id(id) && (id == (int)emu_get_core()->GetPendingMachineConfig().cdrom - 1);
}

bool emu_hard_disk_is_source(const char* path)
{
    SDL_PathInfo info;

    if (!IsValidPointer(path) || !SDL_GetPathInfo(path, &info))
        return false;

    if (info.type == SDL_PATHTYPE_DIRECTORY)
        return true;

    return ends_with_no_case(path, ".zip") && !emu_floppy_is_image(path);
}

bool emu_hard_disk_is_source_attached(const char* path)
{
    std::string source = trim_trailing_separators(path);

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        Emu_HardDiskInfo info;
        emu_hard_disk_get_info(i, &info);

        if (source == (info.pending ? info.pending_path : info.path))
            return true;
    }

    return false;
}

void emu_hard_disk_prepare_source(const char* path)
{
    std::string error;
    SafeDelete(prepared_disk);
    strncpy_fit(prepared_source.path, trim_trailing_separators(path).c_str(), sizeof(prepared_source.path));
    prepared_disk = FolderDisk::OpenSource(prepared_source.path, emu_get_save_folder(NULL).c_str(), config_temp_path,
        &error);
    strncpy_fit(prepared_source.error, error.c_str(), sizeof(prepared_source.error));
    prepared_source.left_out = IsValidPointer(prepared_disk) ? prepared_disk->GetLeftOut() : 0;
    prepared_source.ready = IsValidPointer(prepared_disk);
}

const Emu_HardDiskSource* emu_hard_disk_get_prepared_source(void)
{
    return &prepared_source;
}

bool emu_hard_disk_attach_prepared_source(int id, bool write_protected)
{
    UNUSED(write_protected);
    FolderDisk* disk = prepared_disk;
    prepared_disk = NULL;
    prepared_source.ready = false;

    if (!valid_id(id) || !emu_get_core()->AttachHardDisk(id, disk))
        return false;

    game_disks[id] = false;
    config_push_recent_hard_disk(prepared_source.path);
    return true;
}

void emu_hard_disk_prepare_image(int id, const char* path, bool write_protected)
{
    if (!valid_id(id) || !IsValidPointer(path))
        return;

    SafeDelete(prepared_images[id]);
    prepared_working_paths[id] = get_working_path(path);
    prepared_write_protected[id] = write_protected;
    HardDiskImage* image = new HardDiskImage();

    if (image->Open(path, write_protected, prepared_working_paths[id].c_str()))
        prepared_images[id] = image;
    else
        SafeDelete(image);
}

bool emu_hard_disk_attach_prepared_image(int id, bool game)
{
    if (!valid_id(id) || !IsValidPointer(prepared_images[id]))
        return false;

    HardDiskImage* image = prepared_images[id];
    InitPointer(prepared_images[id]);
    std::string path = image->GetPath();

    if (!emu_get_core()->AttachHardDisk(id, image, prepared_write_protected[id], prepared_working_paths[id].c_str()))
        return false;

    game_disks[id] = game;

    if (game)
        return true;

    config_emulator.hard_disk_write_protected[id] = prepared_write_protected[id] ? 1 : 0;

    if (!is_generated(id, path.c_str()))
        config_push_recent_hard_disk(path.c_str());

    return true;
}

int emu_hard_disk_get_default(void)
{
    int choice = config_emulator.hard_disk_default;

    if ((choice == config_DefaultDisk_MSDOS) && (emu_dos_get_msdos_name()[0] == 0))
        return config_DefaultDisk_FreeDOS;

    if ((choice == config_DefaultDisk_Custom) && config_emulator.hard_disk_default_path.empty())
        return config_DefaultDisk_FreeDOS;

    return choice;
}

std::string emu_hard_disk_get_default_path(void)
{
    switch (emu_hard_disk_get_default())
    {
        case config_DefaultDisk_MSDOS:
            return get_builtin_path(GDOX_GAME_SYSTEM_MSDOS);
        case config_DefaultDisk_FreeDOS:
            return get_builtin_path(GDOX_GAME_SYSTEM_FREEDOS);
        case config_DefaultDisk_Custom:
            return config_emulator.hard_disk_default_path;
        default:
            return "";
    }
}

void emu_hard_disk_set_default(int choice, const char* path)
{
    std::string previous = emu_hard_disk_get_default_path();
    int system = get_builtin_system(path);

    if (system >= 0)
        choice = system == GDOX_GAME_SYSTEM_MSDOS ? (int)config_DefaultDisk_MSDOS : (int)config_DefaultDisk_FreeDOS;
    else if (IsValidPointer(path) && (path[0] != 0))
        config_emulator.hard_disk_default_path = path;

    config_emulator.hard_disk_default = CLAMP(choice, (int)config_DefaultDisk_MSDOS, (int)config_DefaultDisk_None);
    emu_hard_disk_follow_default(previous);
}

void emu_hard_disk_follow_default(const std::string& previous)
{
    std::string current = emu_hard_disk_get_default_path();
    int attached = 0;
    int id = -1;

    if (current.empty() || (current == previous))
        return;

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        Emu_HardDiskInfo info;
        emu_hard_disk_get_info(i, &info);

        if (!info.pending_attached)
            continue;

        attached++;
        const char* path = info.pending ? info.pending_path : info.path;

        if ((info.builtin || (previous == path)) && (current != path) && !info.game)
            id = i;
    }

    if ((attached == 1) && (id >= 0))
        attach_default(id, emu_hard_disk_get_default());
}

void emu_hard_disk_boot_default(void)
{
    int choice = emu_hard_disk_get_default();
    int id = emu_hard_disk_get_free_id();

    if ((choice == config_DefaultDisk_None) || (id < 0) || emu_get_core()->HasHardDisks())
        return;

    if (attach_default(id, choice) || (choice == config_DefaultDisk_FreeDOS))
        return;

    fallback_error = choice == config_DefaultDisk_Custom ?
        "Unable to attach " + config_emulator.hard_disk_default_path : builtin_error;
    builtin_error.clear();
    emu_hard_disk_attach_builtin(id, GDOX_GAME_SYSTEM_FREEDOS);
}

bool emu_hard_disk_attach_builtin(int id, int system)
{
    std::string path = get_builtin_path(system);
    u64 size = 0;
    s64 modified = 0;

    if (!valid_id(id) || (!MediaFile::GetFileInfo(path.c_str(), &size, &modified) && !make_builtin(system, false)))
        return false;

    return emu_hard_disk_attach(id, path.c_str(), config_emulator.hard_disk_write_protected[id] != 0);
}

// The old disk only goes once the new one is complete, so a failure leaves it as it was
bool emu_hard_disk_recreate_builtin(int system)
{
    int id = -1;

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        Emu_HardDiskInfo info;
        emu_hard_disk_get_info(i, &info);

        if (info.builtin && (info.builtin_system == system))
            id = i;
    }

    if (id >= 0)
        emu_get_core()->DetachHardDisk(id);

    bool written = make_builtin(system, true);

    if (id >= 0)
        emu_hard_disk_attach_builtin(id, system);

    return written;
}

bool emu_hard_disk_is_builtin_attached(int system)
{
    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        Emu_HardDiskInfo info;
        emu_hard_disk_get_info(i, &info);

        if (info.pending_attached && info.builtin && (info.builtin_system == system))
            return true;
    }

    return false;
}

bool emu_hard_disk_take_builtin_error(std::string* error)
{
    if (builtin_error.empty())
        return false;

    *error = builtin_error;
    builtin_error.clear();
    return true;
}

bool emu_hard_disk_take_fallback_error(std::string* error)
{
    if (fallback_error.empty())
        return false;

    *error = fallback_error;
    fallback_error.clear();
    return true;
}

bool emu_hard_disk_attach(int id, const char* path, bool write_protected)
{
    if (!valid_id(id) || !IsValidPointer(path) ||
        !emu_get_core()->AttachHardDisk(id, path, write_protected, get_working_path(path).c_str()))
        return false;

    config_emulator.hard_disk_write_protected[id] = write_protected ? 1 : 0;
    game_disks[id] = false;

    if (!is_generated(id, path))
        config_push_recent_hard_disk(path);

    return true;
}

bool emu_hard_disk_attach_game(int id, FolderDisk* disk)
{
    if (!valid_id(id) || !emu_get_core()->AttachHardDisk(id, disk))
        return false;

    game_disks[id] = true;
    return true;
}

bool emu_hard_disk_detach(int id)
{
    if (!valid_id(id))
        return false;

    emu_get_core()->DetachHardDisk(id);
    game_disks[id] = false;
    return true;
}

bool emu_hard_disk_create(int id, const char* path, int size_mb, bool formatted, bool write_protected)
{
    if (!valid_id(id) || !IsValidPointer(path) || (size_mb <= 0) || SDL_GetPathInfo(path, NULL))
        return false;

    if (!HardDiskImage::Create(path, (u64)size_mb * 1024 * 1024, formatted))
        return false;

    return emu_hard_disk_attach(id, path, write_protected);
}

bool emu_hard_disk_save(int id, const char* path, std::string* error, std::string* warning)
{
    error->clear();
    warning->clear();
    GeardoxCore* core = emu_get_core();

    if (!valid_id(id) || !IsValidPointer(path) || !core->GetHardDisk(id)->IsOpen())
    {
        *error = "There is no hard disk to save.";
        return false;
    }

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        Emu_HardDiskInfo info;
        emu_hard_disk_get_info(i, &info);
        FolderDisk* folder = core->GetFolderDisk(i);

        if (is_under(path, info.path) || is_under(path, info.pending_path) ||
            is_under(path, get_working_path(info.path)) ||
            (IsValidPointer(folder) && is_under(path, folder->GetDataFolder())))
        {
            *error = "This file or folder is part of a hard disk in use. Save the image somewhere else.";
            return false;
        }
    }

    if (!core->GetHardDisk(id)->Save(path))
    {
        *error = "Check the free space, and that the disk's own files are still there.";
        return false;
    }

    if (!IsValidPointer(core->GetFolderDisk(id)))
        return true;

    FatImage image;
    MediaFile* file = MediaFile::OpenFile(path);

    if (!image.Read(file))
        *warning = image.GetReadError();

    SafeDelete(file);
    return true;
}

bool emu_hard_disk_set_write_protected(int id, bool write_protected)
{
    Emu_HardDiskInfo info;

    if (!emu_hard_disk_get_info(id, &info))
        return false;

    const char* path = info.pending ? info.pending_path : info.path;

    if (path[0] != 0)
        return emu_hard_disk_attach(id, path, write_protected);

    config_emulator.hard_disk_write_protected[id] = write_protected ? 1 : 0;
    return true;
}

bool emu_hard_disk_flush(void)
{
    bool flushed = emu_get_core()->FlushHardDisks();

    if (idle_flush.Done(flushed))
        flush_error = true;

    return flushed;
}

void emu_hard_disk_update(void)
{
    GeardoxCore* core = emu_get_core();
    u32 count = 0;
    u32 unflushed = 0;

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        HardDiskImage* image = core->GetHardDisk(i);
        count += image->GetWriteCount();
        unflushed += image->GetUnflushedBlocks();
    }

    if (idle_flush.Tick(count, unflushed != 0, k_idle_flush_hard_disk_frames))
        emu_hard_disk_flush();
}

bool emu_hard_disk_take_flush_error(void)
{
    bool error = flush_error;
    flush_error = false;
    return error;
}

bool emu_hard_disk_get_info(int id, Emu_HardDiskInfo* info)
{
    if (!IsValidPointer(info))
        return false;

    memset(info, 0, sizeof(*info));

    if (!valid_id(id))
        return false;

    GeardoxCore* core = emu_get_core();
    HardDiskImage* image = core->GetHardDisk(id);
    info->attached = image->IsOpen();
    info->write_protected = image->IsWriteProtected();
    info->blocks = image->GetBlocks();
    info->unflushed = image->GetUnflushedBlocks();
    info->pending = core->IsHardDiskPending(id);
    info->pending_attached = info->pending ? core->GetPendingHardDiskPath(id)[0] != 0 : info->attached;
    info->pending_write_protected = info->pending ? core->IsPendingHardDiskWriteProtected(id) : info->write_protected;
    strncpy_fit(info->path, image->GetPath(), sizeof(info->path));
    strncpy_fit(info->pending_path, core->GetPendingHardDiskPath(id), sizeof(info->pending_path));
    const char* path = info->pending ? info->pending_path : info->path;
    info->builtin_system = get_builtin_system(path);
    info->builtin = info->builtin_system >= 0;
    info->folder = !info->pending && IsValidPointer(core->GetFolderDisk(id));

    if (info->folder)
        strncpy_fit(info->source, info->path, sizeof(info->source));

    info->game = game_disks[id] && (path[0] != 0);
    return true;
}

// A built-in disk only names the content when it's alone, or every game on it would share its save states
const char* emu_hard_disk_get_content_path(void)
{
    const char* path = "";

    for (int i = GDOX_MAX_HARD_DISKS - 1; i >= 0; i--)
    {
        HardDiskImage* image = emu_get_core()->GetHardDisk(i);

        if (image->IsOpen() && ((path[0] == 0) || (get_builtin_system(image->GetPath()) < 0)))
            path = image->GetPath();
    }

    return path;
}

const char* emu_hard_disk_get_content_name(void)
{
    return emu_hard_disk_get_content_path();
}

void emu_hard_disk_state_loaded(bool rewind)
{
    u32 changes = emu_get_core()->GetHardDiskStateChanges();

    if (!rewind || !rewind_changed)
        state_changes |= changes;

    rewind_changed = rewind && (changes != 0);
}

u32 emu_hard_disk_take_state_changes(void)
{
    u32 changes = state_changes;
    state_changes = 0;
    return changes;
}

static bool valid_id(int id)
{
    return (id >= 0) && (id < GDOX_MAX_HARD_DISKS);
}

static std::string get_builtin_path(int system)
{
    char path[GDOX_MAX_PATH];
    join_path(config_root_path, system == GDOX_GAME_SYSTEM_MSDOS ? k_dos_disk_msdos : k_dos_disk_freedos, path,
        sizeof(path));
    return path;
}

static int get_builtin_system(const char* path)
{
    if (!IsValidPointer(path) || (path[0] == 0))
        return -1;

    if (get_builtin_path(GDOX_GAME_SYSTEM_FREEDOS) == path)
        return GDOX_GAME_SYSTEM_FREEDOS;

    return get_builtin_path(GDOX_GAME_SYSTEM_MSDOS) == path ? (int)GDOX_GAME_SYSTEM_MSDOS : -1;
}

static bool make_builtin(int system, bool replace)
{
    std::string path = get_builtin_path(system);
    std::string msdos_zip = system == GDOX_GAME_SYSTEM_MSDOS ? emu_dos_get_msdos_path() : "";

    return DosDisk::WriteBuiltin(path.c_str(), system, msdos_zip.c_str(), config_input.keyboard_layout.c_str(), replace,
        &builtin_error);
}

static bool attach_default(int id, int choice)
{
    if (choice == config_DefaultDisk_Custom)
        return emu_hard_disk_attach(id, config_emulator.hard_disk_default_path.c_str(),
            config_emulator.hard_disk_write_protected[id] != 0);

    return emu_hard_disk_attach_builtin(id, choice == config_DefaultDisk_MSDOS ? (int)GDOX_GAME_SYSTEM_MSDOS :
        (int)GDOX_GAME_SYSTEM_FREEDOS);
}

static bool is_generated(int id, const char* path)
{
    return game_disks[id] || (get_builtin_system(path) >= 0);
}

static bool is_under(const std::string& path, const std::string& folder)
{
    std::string inner = path_forward_slashes(path);
    std::string outer = path_forward_slashes(trim_trailing_separators(folder));

    return !outer.empty() && starts_with_no_case(inner.c_str(), outer.c_str()) &&
        ((inner.size() == outer.size()) || (inner[outer.size()] == '/'));
}

static std::string get_working_path(const char* path)
{
    u32 hash = 0;

    if (!ends_with_no_case(path, ".chd") || !HardDiskImage::GetChdHash(path, &hash))
        return "";

    std::string directory = emu_get_save_folder(path);
    char name[GDOX_MAX_PATH];
    char file_name[GDOX_MAX_PATH];
    char working[GDOX_MAX_PATH];
    get_filename_without_extension(path, name, sizeof(name));
    snprintf(file_name, sizeof(file_name), "%s.%08X.geardox.hdd", name, hash);
    join_path(directory.c_str(), file_name, working, sizeof(working));
    return working;
}

