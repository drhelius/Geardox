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

#include <time.h>
#include <algorithm>
#include <map>
#include <set>
#include "dos_fs.h"
#include "floppy_image.h"
#include "../media/media_file.h"

static const char* const k_dos_fs_clutter[] =
{
    "thumbs.db", "desktop.ini", "__macosx", "$recycle.bin", "system volume information"
};

static const char* const k_dos_fs_devices[] =
{
    "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "LPT1", "LPT2", "LPT3", "CLOCK$"
};

static void split_name(const std::string& name, std::string* base, std::string* extension)
{
    size_t dot = name.find_last_of('.');

    if ((dot == std::string::npos) || (dot == 0))
    {
        *base = name;
        extension->clear();
        return;
    }

    *base = name.substr(0, dot);
    *extension = name.substr(dot + 1);
}

// One underscore a character, not a byte, for anything DOS can't hold
static std::string clean_part(const std::string& text, const DosFs_Name_Policy& policy)
{
    std::string result;

    for (size_t i = 0; i < text.size(); i++)
    {
        u8 byte = (u8)text[i];

        if ((byte == ' ') || (byte == '.') || ((byte & 0xC0) == 0x80))
            continue;

        char c = to_upper_ascii(text[i]);
        result += dos_fs_is_character(c, policy.characters) ? c : '_';
    }

    return result;
}

static bool is_clean_part(const std::string& text, size_t max_size, const DosFs_Name_Policy& policy)
{
    if (text.size() > max_size)
        return false;

    for (size_t i = 0; i < text.size(); i++)
    {
        if (!dos_fs_is_character(to_upper_ascii(text[i]), policy.characters))
            return false;
    }

    return true;
}

static bool is_clean_name(const std::string& name, bool directory, const DosFs_Name_Policy& policy)
{
    std::string base;
    std::string extension;

    if (directory && policy.plain_directories)
        base = name;
    else
        split_name(name, &base, &extension);

    if (base.empty() || !is_clean_part(base, 8, policy) || !is_clean_part(extension, 3, policy))
        return false;

    std::string device = to_upper_ascii(base);

    for (size_t i = 0; i < sizeof(k_dos_fs_devices) / sizeof(k_dos_fs_devices[0]); i++)
    {
        if (device == k_dos_fs_devices[i])
            return false;
    }

    return true;
}

DosFs_Raw_Result dos_fs_load_raw(const u8* data, u32 size, std::vector<u8>* raw)
{
    u32 image_size = 0;
    u8* image = FloppyImage::Load(data, size, &image_size);
    raw->clear();

    if (!IsValidPointer(image))
        return DOS_FS_RAW_UNKNOWN;

    raw->resize(FloppyImage::GetRawSize(image, image_size));
    bool exported = !raw->empty() && FloppyImage::ExportRaw(image, image_size, raw->data(), (u32)raw->size());
    SafeDeleteArray(image);

    if (!exported)
    {
        raw->clear();
        return DOS_FS_RAW_NOT_512;
    }

    return DOS_FS_RAW_OK;
}

bool dos_fs_fat12_open(const u8* raw, u32 size, DosFs_Fat12* fs)
{
    if ((size < k_dos_sector_size) || (read_u16_le(raw + 11) != k_dos_sector_size) || (read_u16_le(raw + 510) != 0xAA55))
        return false;

    u32 cluster_sectors = raw[13];
    u32 reserved = read_u16_le(raw + 14);
    u32 fats = raw[16];
    u32 root_entries = read_u16_le(raw + 17);
    u32 total = read_u16_le(raw + 19) != 0 ? read_u16_le(raw + 19) : read_u32_le(raw + 32);
    u32 fat_sectors = read_u16_le(raw + 22);

    if ((cluster_sectors == 0) || ((cluster_sectors & (cluster_sectors - 1)) != 0) || (reserved == 0) || (fats == 0) ||
        (fats > 2) || (root_entries == 0) || (fat_sectors == 0))
        return false;

    u32 root = (reserved + (fats * fat_sectors)) * k_dos_sector_size;
    u32 data = root + (((root_entries * 32) + k_dos_sector_size - 1) / k_dos_sector_size) * k_dos_sector_size;
    total = MIN(total, size / k_dos_sector_size);

    if ((data >= size) || (total * k_dos_sector_size <= data))
        return false;

    u32 cluster_size = cluster_sectors * k_dos_sector_size;
    u32 clusters = (total * k_dos_sector_size - data) / cluster_size;

    if ((clusters == 0) || (clusters > k_dos_max_fat12_clusters) || ((reserved + fat_sectors) * k_dos_sector_size > size))
        return false;

    fs->raw = raw;
    fs->fat = raw + (reserved * k_dos_sector_size);
    fs->size = size;
    fs->root = root;
    fs->root_entries = root_entries;
    fs->data = data;
    fs->cluster_size = cluster_size;
    fs->clusters = clusters;
    return true;
}

const u8* dos_fs_fat12_next(const DosFs_Fat12& fs, u32* index, std::string* name)
{
    while (*index < fs.root_entries)
    {
        const u8* entry = fs.raw + fs.root + (*index * 32);
        (*index)++;

        if (entry[0] == 0)
        {
            *index = fs.root_entries;
            return NULL;
        }

        if ((entry[0] == 0xE5) || ((entry[11] & 0x0F) == 0x0F) || ((entry[11] & 0x18) != 0))
            continue;

        *name = dos_fs_entry_name(entry);

        if (!name->empty() && ((u8)(*name)[0] == 0x05))
            (*name)[0] = (char)0xE5;

        *name = to_upper_ascii(*name);
        return entry;
    }

    return NULL;
}

bool dos_fs_fat12_read(const DosFs_Fat12& fs, const u8* entry, std::string* data)
{
    u32 file_size = read_u32_le(entry + 28);
    u32 cluster = read_u16_le(entry + 26);
    u32 steps = 0;
    data->clear();

    while ((data->size() < file_size) && (cluster >= 2) && (cluster < fs.clusters + 2) && (steps++ <= fs.clusters))
    {
        u32 offset = fs.data + ((cluster - 2) * fs.cluster_size);
        u32 chunk = MIN(fs.cluster_size, file_size - (u32)data->size());
        data->append((const char*)fs.raw + offset, chunk);
        u32 at = cluster + (cluster / 2);
        u32 value = fs.fat[at] | (fs.fat[at + 1] << 8);
        cluster = (cluster & 1) != 0 ? value >> 4 : value & 0xFFF;
    }

    return data->size() == file_size;
}

std::string dos_fs_entry_name(const u8* short_name)
{
    std::string base((const char*)short_name, 8);
    std::string extension((const char*)short_name + 8, 3);
    base.erase(base.find_last_not_of(' ') + 1);
    extension.erase(extension.find_last_not_of(' ') + 1);
    return extension.empty() ? base : base + "." + extension;
}

bool dos_is_name_character(char c)
{
    return dos_fs_is_character(c, k_dos_name_characters);
}

bool dos_fs_is_character(char c, const char* characters)
{
    return ((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')) || ((c != 0) && (strchr(characters, c) != NULL));
}

bool dos_fs_is_clutter(const std::string& name)
{
    if (name.empty() || (name[0] == '.'))
        return true;

    std::string lower = to_lower_ascii(name);

    for (size_t i = 0; i < sizeof(k_dos_fs_clutter) / sizeof(k_dos_fs_clutter[0]); i++)
    {
        if (lower == k_dos_fs_clutter[i])
            return true;
    }

    return false;
}

std::string dos_fs_pad(const std::string& text, size_t size)
{
    std::string result = text.substr(0, size);
    result.resize(size, ' ');
    return result;
}

void dos_fs_assign_names(const std::vector<std::string>& names, const std::vector<bool>& directories,
    const DosFs_Name_Policy& policy, std::vector<std::string>* short_names)
{
    std::vector<std::pair<std::pair<bool, std::string>, std::pair<std::string, size_t> > > order;
    short_names->assign(names.size(), std::string());

    for (size_t i = 0; i < names.size(); i++)
        order.push_back(std::make_pair(std::make_pair(!is_clean_name(names[i], directories[i], policy),
            to_upper_ascii(names[i])), std::make_pair(names[i], i)));

    std::sort(order.begin(), order.end());
    std::set<std::string> taken;
    std::map<std::string, u32> tails;

    for (size_t i = 0; i < order.size(); i++)
    {
        size_t index = order[i].second.second;
        std::string base;
        std::string extension;

        if (directories[index] && policy.plain_directories)
            base = names[index];
        else
            split_name(names[index], &base, &extension);

        std::string short_base = clean_part(base, policy);
        std::string short_extension = clean_part(extension, policy).substr(0, 3);

        if (short_base.empty() && policy.empty_base)
            short_base = "_";

        std::string candidate = dos_fs_pad(short_base, 8) + dos_fs_pad(short_extension, 3);

        if (!is_clean_name(names[index], directories[index], policy) || (taken.count(candidate) != 0))
        {
            std::string key = short_base + "." + short_extension;
            u32& tail = tails[key];

            do
            {
                char number[16];
                snprintf(number, sizeof(number), "%c%u", policy.tail, ++tail);
                size_t room = 8 - strlen(number);
                candidate = dos_fs_pad(short_base.substr(0, room) + number, 8) + dos_fs_pad(short_extension, 3);
            }
            while (taken.count(candidate) != 0);
        }

        taken.insert(candidate);
        (*short_names)[index] = candidate;
    }
}

bool dos_is_short_name(const std::string& name, bool extension_needed)
{
    size_t dot = name.find('.');
    size_t base = dot == std::string::npos ? name.size() : dot;
    size_t extension = dot == std::string::npos ? 0 : name.size() - dot - 1;

    if ((base == 0) || (base > 8) || (extension > 3) || (extension_needed && (extension == 0)))
        return false;

    for (size_t i = 0; i < name.size(); i++)
    {
        if ((i != dot) && !dos_is_name_character(name[i]))
            return false;
    }

    return true;
}

bool dos_fs_read_deleted(const std::string& folder, std::vector<std::string>* paths)
{
    std::string list = folder;
    append_path_component(list, k_dos_deleted_list);
    std::vector<std::string> lines;
    std::string text;
    u64 size = 0;
    s64 modified = 0;
    paths->clear();

    if (!MediaFile::GetFileInfo(list.c_str(), &size, &modified))
        return true;

    if (!MediaFile::ReadFile(list.c_str(), k_media_file_no_limit, &text, true))
        return false;

    split_string(text, '\n', &lines);

    for (size_t i = 0; i < lines.size(); i++)
    {
        std::string line = lines[i];

        if (!line.empty() && (line[line.size() - 1] == '\r'))
            line.resize(line.size() - 1);

        if (!line.empty())
            paths->push_back(line);
    }

    return true;
}

bool dos_fs_write_deleted(const std::string& folder, const std::vector<std::string>& paths)
{
    std::string list = path_join(folder, k_dos_deleted_list);

    if (paths.empty())
        return !MediaFile::Exists(list.c_str()) || MediaFile::RemoveFile(list.c_str());

    std::string text;

    for (size_t i = 0; i < paths.size(); i++)
        text += paths[i] + "\n";

    return MediaFile::MakeDirectories(folder.c_str()) && MediaFile::WriteFileAtomic(list.c_str(), text.data(), text.size());
}

std::string dos_fs_join(const std::string& base, const std::string& name)
{
    return base.empty() ? name : name.empty() ? base : base + "\\" + name;
}

// SWITCHES=/F came with MS-DOS 6.0, and HIMEM's memory test, with /TESTMEM to skip it, with 6.2
std::string dos_fs_config_header(bool msdos, int version)
{
    return (!msdos || (version >= 600)) ? "SWITCHES=/F\r\nLASTDRIVE=Z\r\n" : "LASTDRIVE=Z\r\n";
}

std::string dos_fs_himem_line(int version)
{
    return version >= 620 ? "DEVICE=C:\\DOS\\HIMEM.SYS /TESTMEM:OFF\r\n" : "DEVICE=C:\\DOS\\HIMEM.SYS\r\n";
}

std::string dos_fs_shell_line(bool msdos)
{
    return msdos ? "SHELL=C:\\COMMAND.COM C:\\ /E:1024 /P\r\n" :
        "SHELL=C:\\FREEDOS\\BIN\\COMMAND.COM C:\\FREEDOS\\BIN /E:1024 /P\r\n";
}

std::string dos_fs_autoexec_header(bool msdos)
{
    std::string out = msdos ? "@ECHO OFF\r\nC:\\GEARDOX\\GDXPAD.COM\r\nSET PATH=C:\\DOS;C:\\GEARDOX\r\n" :
        "@ECHO OFF\r\nC:\\FREEDOS\\BIN\\GDXPAD.COM\r\nSET DOSDIR=C:\\FREEDOS\r\nSET PATH=C:\\FREEDOS\\BIN\r\n";
    return out + "SET TEMP=C:\\TEMP\r\nSET DIRCMD=/OGN\r\n";
}

std::string dos_fs_keyb_line(const std::string& layout, bool msdos)
{
    return "LH KEYB " + layout + (msdos ? ",437,C:\\DOS\\KEYBOARD.SYS\r\n" : ",437,C:\\FREEDOS\\BIN\\KEYBOARD.SYS\r\n");
}

std::string dos_fs_doskey_line()
{
    return "LH C:\\GEARDOX\\DOSKEY.COM -I DOSKEY=C:\\GEARDOX\\DOSKEY.COM $*\r\n";
}

s64 dos_time_to_host(u16 date, u16 time_value)
{
    struct tm local;
    memset(&local, 0, sizeof(local));
    local.tm_year = ((date >> 9) & 0x7F) + 80;
    local.tm_mon = (int)((date >> 5) & 0x0F) - 1;
    local.tm_mday = date & 0x1F;
    local.tm_hour = (time_value >> 11) & 0x1F;
    local.tm_min = (time_value >> 5) & 0x3F;
    local.tm_sec = (time_value & 0x1F) * 2;
    local.tm_isdst = -1;

    if ((local.tm_mon < 0) || (local.tm_mon > 11) || (local.tm_mday == 0))
        return 0;

    return (s64)mktime(&local);
}

void host_time_to_dos(s64 modified, u16* date, u16* time_value)
{
    struct tm local;
    *date = k_dos_default_date;
    *time_value = k_dos_default_time;

    if ((modified == 0) || !get_host_time((time_t)modified, false, &local))
        return;

    int year = CLAMP(local.tm_year + 1900, 1980, 2107);
    *date = (u16)(((year - 1980) << 9) | ((local.tm_mon + 1) << 5) | local.tm_mday);
    *time_value = (u16)((local.tm_hour << 11) | (local.tm_min << 5) | (local.tm_sec / 2));
}
