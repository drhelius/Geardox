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

#ifndef EMU_HARD_DISK_H
#define EMU_HARD_DISK_H

#include <string>
#include <vector>
#include "geardox.h"

#ifdef EMU_HARD_DISK_IMPORT
    #define EXTERN
#else
    #define EXTERN extern
#endif

struct Emu_HardDiskInfo
{
    bool attached;
    bool write_protected;
    bool pending;
    bool pending_attached;
    bool pending_write_protected;
    u32 blocks;
    bool builtin;
    int builtin_system;
    bool game;
    bool folder;
    u32 unflushed;
    char path[GDOX_MAX_PATH];
    char pending_path[GDOX_MAX_PATH];
    char source[GDOX_MAX_PATH];
};

struct Emu_HardDiskSource
{
    char path[GDOX_MAX_PATH];
    char error[GDOX_MAX_PATH];
    const char* recovery;
    bool recovery_refused;
    u32 left_out;
    bool ready;
};

class FolderDisk;
struct FolderDisk_Result;

EXTERN bool emu_hard_disk_is_image(const char* path);
EXTERN int emu_hard_disk_get_image_id(const char* path);
EXTERN int emu_hard_disk_get_free_id(void);
EXTERN bool emu_hard_disk_is_cdrom_position(int id);
EXTERN bool emu_hard_disk_is_source(const char* path);
EXTERN bool emu_hard_disk_is_source_attached(const char* path);
EXTERN void emu_hard_disk_prepare_source(const char* path);
EXTERN const Emu_HardDiskSource* emu_hard_disk_get_prepared_source(void);
EXTERN bool emu_hard_disk_attach_prepared_source(int id, bool write_protected);
EXTERN void emu_hard_disk_prepare_image(int id, const char* path, bool write_protected, const char* working_path = NULL);
EXTERN bool emu_hard_disk_attach_prepared_image(int id, bool game);
EXTERN int emu_hard_disk_get_default(void);
EXTERN std::string emu_hard_disk_get_default_path(void);
EXTERN void emu_hard_disk_set_default(int choice, const char* path);
EXTERN void emu_hard_disk_follow_default(const std::string& previous);
EXTERN void emu_hard_disk_boot_default(void);
EXTERN bool emu_hard_disk_attach_builtin(int id, int system);
EXTERN bool emu_hard_disk_recreate_builtin(int system);
EXTERN bool emu_hard_disk_is_builtin_attached(int system);
EXTERN bool emu_hard_disk_take_builtin_error(std::string* error);
EXTERN bool emu_hard_disk_take_fallback_error(std::string* error);
EXTERN bool emu_hard_disk_attach(int id, const char* path, bool write_protected);
EXTERN bool emu_hard_disk_attach_game(int id, FolderDisk* disk);
EXTERN bool emu_hard_disk_detach(int id);
EXTERN bool emu_hard_disk_create(int id, const char* path, int size_mb, bool formatted, bool write_protected);
EXTERN bool emu_hard_disk_save(int id, const char* path, std::string* error, std::string* warning);
EXTERN bool emu_hard_disk_set_write_protected(int id, bool write_protected);
EXTERN bool emu_hard_disk_flush(void);
EXTERN void emu_hard_disk_update(void);
EXTERN bool emu_hard_disk_take_flush_error(void);
EXTERN bool emu_hard_disk_get_info(int id, Emu_HardDiskInfo* info);
EXTERN const char* emu_hard_disk_get_content_path(void);
EXTERN const char* emu_hard_disk_get_content_name(void);
EXTERN void emu_hard_disk_state_loaded(bool rewind);
EXTERN u32 emu_hard_disk_take_state_changes(void);
EXTERN void emu_hard_disk_take_write_backs(std::vector<FolderDisk_Result>* results);

#undef EMU_HARD_DISK_IMPORT
#undef EXTERN
#endif /* EMU_HARD_DISK_H */
