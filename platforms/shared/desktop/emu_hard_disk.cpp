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
#include "emu_floppy.h"
#include "drive/hard_disk_image.h"
#include "media/media_file.h"

static const int k_idle_flush_frames = 120;
static u32 write_count = 0;
static int idle_frames = 0;
static bool flush_failed = false;
static bool flush_error = false;
static u32 state_changes = 0;
static bool rewind_changed = false;
static std::string source_paths[GDOX_MAX_HARD_DISKS];
static std::string source_images[GDOX_MAX_HARD_DISKS];
static Emu_HardDiskSource prepared_source;
static const char k_freedos_image[] = "FreeDOS.hdd";

static bool valid_id(int id);
static std::string get_working_path(const char* path);
static std::string get_freedos_path(void);
static std::string get_source_image_path(const char* path);
static bool is_generated(int id, const char* path);

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

        if ((info.pending ? !info.pending_attached : !info.attached) || info.builtin)
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
    std::string image = get_source_image_path(path);

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        Emu_HardDiskInfo info;
        emu_hard_disk_get_info(i, &info);

        if (image == (info.pending ? info.pending_path : info.path))
            return true;
    }

    return false;
}

// Touches nothing of the emulator's, so the loading thread can run it
void emu_hard_disk_prepare_source(const char* path, bool rebuild)
{
    strncpy_fit(prepared_source.path, path, sizeof(prepared_source.path));
    size_t size = strlen(prepared_source.path);

    while ((size > 1) && ((prepared_source.path[size - 1] == '/') || (prepared_source.path[size - 1] == '\\')))
        prepared_source.path[--size] = 0;

    strncpy_fit(prepared_source.image, get_source_image_path(path).c_str(), sizeof(prepared_source.image));
    FatImage::Prepare(prepared_source.path, prepared_source.image, rebuild, &prepared_source.report);
}

const Emu_HardDiskSource* emu_hard_disk_get_prepared_source(void)
{
    return &prepared_source;
}

bool emu_hard_disk_attach_prepared_source(int id, bool write_protected)
{
    if (!valid_id(id))
        return false;

    source_paths[id] = prepared_source.path;
    source_images[id] = prepared_source.image;

    if (!emu_hard_disk_attach(id, prepared_source.image, write_protected))
    {
        source_paths[id].clear();
        source_images[id].clear();
        return false;
    }

    config_push_recent_hard_disk(prepared_source.path);
    return true;
}

void emu_hard_disk_boot_freedos(void)
{
    if (config_emulator.hard_disk_freedos && !emu_get_core()->HasHardDisks())
        emu_hard_disk_attach_freedos(0);
}

bool emu_hard_disk_attach_freedos(int id)
{
    std::string path = get_freedos_path();

    u64 size = 0;
    s64 modified = 0;

    if (!valid_id(id) || (!MediaFile::GetFileInfo(path.c_str(), &size, &modified) && !FatImage::WriteFreeDOS(path.c_str())))
        return false;

    return emu_hard_disk_attach(id, path.c_str(), config_emulator.hard_disk_write_protected[id] != 0);
}

// The old disk only goes once the new one is complete, so a failure leaves it as it was
bool emu_hard_disk_recreate_freedos(void)
{
    int id = -1;

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        Emu_HardDiskInfo info;
        emu_hard_disk_get_info(i, &info);

        if (info.builtin)
            id = i;
    }

    if (id >= 0)
        emu_get_core()->DetachHardDisk(id);

    bool written = FatImage::WriteFreeDOS(get_freedos_path().c_str(), true);

    if (id >= 0)
        emu_hard_disk_attach_freedos(id);

    return written;
}

bool emu_hard_disk_attach(int id, const char* path, bool write_protected)
{
    if (!valid_id(id) || !IsValidPointer(path) ||
        !emu_get_core()->AttachHardDisk(id, path, write_protected, get_working_path(path).c_str()))
        return false;

    config_emulator.hard_disk_write_protected[id] = write_protected ? 1 : 0;

    if (!is_generated(id, path))
        config_push_recent_hard_disk(path);

    return true;
}

bool emu_hard_disk_detach(int id)
{
    if (!valid_id(id))
        return false;

    emu_get_core()->DetachHardDisk(id);
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

    if (!flushed && !flush_failed)
        flush_error = true;

    flush_failed = !flushed;
    idle_frames = 0;
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

    if (count != write_count)
    {
        write_count = count;
        idle_frames = 0;
        return;
    }

    if ((unflushed != 0) && (++idle_frames >= k_idle_flush_frames))
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
    info->builtin = (path[0] != 0) && (get_freedos_path() == path);

    if ((path[0] != 0) && (source_images[id] == path))
        strncpy_fit(info->source, source_paths[id].c_str(), sizeof(info->source));

    return true;
}

// FreeDOS only names the content when it's alone, or every game on it would share its save states
const char* emu_hard_disk_get_content_path(void)
{
    const char* path = "";

    for (int i = GDOX_MAX_HARD_DISKS - 1; i >= 0; i--)
    {
        HardDiskImage* image = emu_get_core()->GetHardDisk(i);

        if (image->IsOpen() && ((path[0] == 0) || (get_freedos_path() != image->GetPath())))
            path = image->GetPath();
    }

    return path;
}

const char* emu_hard_disk_get_content_name(void)
{
    const char* path = emu_hard_disk_get_content_path();

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        if ((path[0] != 0) && (source_images[i] == path))
            return source_paths[i].c_str();
    }

    return path;
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

static std::string get_freedos_path(void)
{
    char path[GDOX_MAX_PATH];
    join_path(config_root_path, k_freedos_image, path, sizeof(path));
    return path;
}

// "Same as ROM" means the config folder here: a folder is only read, so nothing goes next to it
static std::string get_source_image_path(const char* path)
{
    bool custom = (Directory_Location)config_emulator.savefiles_dir_option == Directory_Location_Custom;
    std::string name;
    char image[GDOX_MAX_PATH];
    FatImage::GetImageName(path, &name);
    join_path(custom ? config_emulator.savefiles_path.c_str() : config_root_path, name.c_str(), image, sizeof(image));
    return image;
}

static bool is_generated(int id, const char* path)
{
    return (source_images[id] == path) || (get_freedos_path() == path);
}

static std::string get_working_path(const char* path)
{
    u32 hash = 0;

    if (!ends_with_no_case(path, ".chd") || !HardDiskImage::GetChdHash(path, &hash))
        return "";

    char directory[GDOX_MAX_PATH];

    switch ((Directory_Location)config_emulator.savefiles_dir_option)
    {
        case Directory_Location_ROM:
            get_directory(path, directory, sizeof(directory));
            break;
        case Directory_Location_Custom:
            strncpy_fit(directory, config_emulator.savefiles_path.c_str(), sizeof(directory));
            break;
        default:
            strncpy_fit(directory, config_root_path, sizeof(directory));
            break;
    }

    char name[GDOX_MAX_PATH];
    char file_name[GDOX_MAX_PATH];
    char working[GDOX_MAX_PATH];
    get_filename_without_extension(path, name, sizeof(name));
    snprintf(file_name, sizeof(file_name), "%s.%08X.geardox.hdd", name, hash);
    join_path(directory, file_name, working, sizeof(working));
    return working;
}

