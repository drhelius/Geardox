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

#ifndef DOS_FS_H
#define DOS_FS_H

#include <string>
#include <vector>
#include "../common/common.h"

enum DosFs_Raw_Result
{
    DOS_FS_RAW_OK = 0,
    DOS_FS_RAW_UNKNOWN,
    DOS_FS_RAW_NOT_512
};

struct DosFs_Name_Policy
{
    const char* characters;
    char tail;
    bool empty_base;
    bool plain_directories;
};

struct DosFs_Fat12
{
    const u8* raw;
    const u8* fat;
    u32 size;
    u32 root;
    u32 root_entries;
    u32 data;
    u32 cluster_size;
    u32 clusters;
};

const u32 k_dos_sector_size = 512;
const u32 k_dos_max_fat12_clusters = 4084;
const u16 k_dos_default_date = (45 << 9) | (4 << 5) | 5;
const u16 k_dos_default_time = 12 << 11;
const char k_dos_name_characters[] = "$%'-_@~`!(){}^#&";
const char k_dos_root_freedos[] = "FREEDOS";
const char k_dos_root_temp[] = "TEMP";
const char k_dos_root_dos[] = "DOS";
const char k_dos_root_geardox[] = "GEARDOX";
const char k_dos_config_sys[] = "CONFIG.SYS";
const char k_dos_autoexec_bat[] = "AUTOEXEC.BAT";
const char k_dos_glide_overlay[] = "GLIDE2X.OVL";
const char k_dos_deleted_list[] = ".deleted";
const DosFs_Name_Policy k_dos_fat_names = { k_dos_name_characters, '~', false, false };
const DosFs_Name_Policy k_dos_iso_names = { "_", '_', true, true };

DosFs_Raw_Result dos_fs_load_raw(const u8* data, u32 size, std::vector<u8>* raw);
bool dos_fs_fat12_open(const u8* raw, u32 size, DosFs_Fat12* fs);
const u8* dos_fs_fat12_next(const DosFs_Fat12& fs, u32* index, std::string* name);
bool dos_fs_fat12_read(const DosFs_Fat12& fs, const u8* entry, std::string* data);
std::string dos_fs_entry_name(const u8* short_name);
bool dos_is_name_character(char c);
bool dos_fs_is_character(char c, const char* characters);
bool dos_fs_is_clutter(const std::string& name);
std::string dos_fs_pad(const std::string& text, size_t size);
void dos_fs_assign_names(const std::vector<std::string>& names, const std::vector<bool>& directories,
    const DosFs_Name_Policy& policy, std::vector<std::string>* short_names);
bool dos_is_short_name(const std::string& name, bool extension_needed);
bool dos_fs_read_deleted(const std::string& folder, std::vector<std::string>* paths);
std::string dos_fs_config_header(bool msdos, int version);
std::string dos_fs_himem_line(int version);
std::string dos_fs_shell_line(bool msdos);
std::string dos_fs_autoexec_header(bool msdos);
std::string dos_fs_keyb_line(const std::string& layout, bool msdos);
s64 dos_time_to_host(u16 date, u16 time_value);
void host_time_to_dos(s64 modified, u16* date, u16* time_value);

#endif /* DOS_FS_H */
