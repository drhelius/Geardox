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
#include "fat_image.h"
#include "freedos.h"
#include "../media/media_file.h"

static const u32 k_fat_sector_size = 512;
static const u32 k_fat_sectors_per_track = 63;
static const u32 k_fat_heads = 16;
static const u32 k_fat_cylinder_sectors = k_fat_heads * k_fat_sectors_per_track;
static const u32 k_fat_max_cylinders = 1024;
static const u32 k_fat_max_disk_cylinders = 4096;
static const u32 k_fat_entry_size = 32;
static const u32 k_fat_root_entries = 512;
static const u32 k_fat_min_clusters = 4085;
static const u32 k_fat_max_clusters = 65525;
static const u32 k_fat_max_path = 63;
static const int k_fat_max_depth = 32;
static const size_t k_fat_max_entries = 131072;
static const u64 k_fat_max_file_size = 0xFFFFFFFFULL;
static const u8 k_fat_media = 0xF8;
static const u8 k_fat_attribute_label = 0x08;
static const u8 k_fat_attribute_directory = 0x10;
static const u8 k_fat_attribute_archive = 0x20;
static const u16 k_fat_end_of_chain = 0xFFFF;
static const u16 k_fat_default_date = (45 << 9) | (4 << 5) | 5;
static const u16 k_fat_default_time = 12 << 11;
static const u32 k_fat_copy_size = 1024 * 1024;
static const u32 k_fat_code_offset = 0x3E;
static const u32 k_fat_entry_offset = 0x1BE;
static const char k_fat_oem[] = "GEARDOX ";
static const char k_fat_type[] = "FAT16   ";
static const char k_fat_label[] = "GEARDOX";
static const char k_fat_name_characters[] = "$%'-_@~`!(){}^#&";

// SeaBIOS only names 1024 cylinders of its logical geometry, so a disk just above a head step leaves its end unreachable
static const u32 k_fat_size_gaps[2][2] = { { 1024, 1088 }, { 2048, 2112 } };

static const u32 k_fat_cluster_table[5][2] =
{
    { 262144, 4 }, { 524288, 8 }, { 1048576, 16 }, { 2097152, 32 }, { 4194304, 64 }
};

static const char* const k_fat_clutter[] =
{
    "thumbs.db", "desktop.ini", "__macosx", "$recycle.bin", "system volume information"
};

static const char* const k_fat_devices[] =
{
    "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "LPT1", "LPT2", "LPT3", "CLOCK$"
};

static char to_upper(char c)
{
    return ((c >= 'a') && (c <= 'z')) ? (char)(c - 32) : c;
}

static std::string upper(const std::string& text)
{
    std::string result(text);

    for (size_t i = 0; i < result.size(); i++)
        result[i] = to_upper(result[i]);

    return result;
}

static bool is_name_character(char c)
{
    return ((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')) || ((c != 0) && (strchr(k_fat_name_characters, c) != NULL));
}

static bool is_clutter(const std::string& name)
{
    if (name.empty() || (name[0] == '.'))
        return true;

    std::string lower(name);

    for (size_t i = 0; i < lower.size(); i++)
        lower[i] = ((lower[i] >= 'A') && (lower[i] <= 'Z')) ? (char)(lower[i] + 32) : lower[i];

    for (size_t i = 0; i < sizeof(k_fat_clutter) / sizeof(k_fat_clutter[0]); i++)
    {
        if (lower == k_fat_clutter[i])
            return true;
    }

    return false;
}

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
static std::string clean_part(const std::string& text)
{
    std::string result;

    for (size_t i = 0; i < text.size(); i++)
    {
        u8 byte = (u8)text[i];

        if ((byte == ' ') || (byte == '.') || ((byte & 0xC0) == 0x80))
            continue;

        char c = to_upper(text[i]);
        result += is_name_character(c) ? c : '_';
    }

    return result;
}

static bool is_clean_part(const std::string& text, size_t max_size)
{
    if (text.size() > max_size)
        return false;

    for (size_t i = 0; i < text.size(); i++)
    {
        if (!is_name_character(to_upper(text[i])))
            return false;
    }

    return true;
}

static bool is_clean_name(const std::string& name)
{
    std::string base;
    std::string extension;
    split_name(name, &base, &extension);

    if (base.empty() || !is_clean_part(base, 8) || !is_clean_part(extension, 3))
        return false;

    std::string device = upper(base);

    for (size_t i = 0; i < sizeof(k_fat_devices) / sizeof(k_fat_devices[0]); i++)
    {
        if (device == k_fat_devices[i])
            return false;
    }

    return true;
}

static std::string pad(const std::string& text, size_t size)
{
    std::string result = text.substr(0, size);
    result.resize(size, ' ');
    return result;
}

static std::string pretty_name(const u8* short_name)
{
    std::string base((const char*)short_name, 8);
    std::string extension((const char*)short_name + 8, 3);
    base.erase(base.find_last_not_of(' ') + 1);
    extension.erase(extension.find_last_not_of(' ') + 1);
    return extension.empty() ? base : base + "." + extension;
}

static void get_dos_time(s64 modified, u16* date, u16* time_value)
{
    struct tm local;
    *date = k_fat_default_date;
    *time_value = k_fat_default_time;

    if ((modified == 0) || !get_host_time((time_t)modified, false, &local))
        return;

    int year = CLAMP(local.tm_year + 1900, 1980, 2107);
    *date = (u16)(((year - 1980) << 9) | ((local.tm_mon + 1) << 5) | local.tm_mday);
    *time_value = (u16)((local.tm_hour << 11) | (local.tm_min << 5) | (local.tm_sec / 2));
}

static std::string trim_path(const char* source)
{
    std::string path(source);

    while ((path.size() > 1) && ((path[path.size() - 1] == '/') || (path[path.size() - 1] == '\\')))
        path.resize(path.size() - 1);

    return path;
}

static std::string get_source_name(const char* source)
{
    std::string path = trim_path(source);
    size_t separator = path.find_last_of("/\\");
    std::string name = separator == std::string::npos ? path : path.substr(separator + 1);

    if (ends_with_no_case(name.c_str(), ".zip"))
        name.resize(name.size() - 4);

    return name;
}

static u32 get_logical_heads(u32 total)
{
    if ((total / k_fat_cylinder_sectors) <= k_fat_max_cylinders)
        return k_fat_heads;

    u32 heads = (total / k_fat_sectors_per_track) / k_fat_max_cylinders;

    if (heads > 128)
        return 255;

    if (heads > 64)
        return 128;

    if (heads > 32)
        return 64;

    return heads > 16 ? 32 : 16;
}

static void write_chs(u8* out, u32 lba, u32 heads)
{
    u32 cylinder = lba / (heads * k_fat_sectors_per_track);
    u32 rest = lba % (heads * k_fat_sectors_per_track);
    cylinder = MIN(cylinder, k_fat_max_cylinders - 1);
    out[0] = (u8)(rest / k_fat_sectors_per_track);
    out[1] = (u8)(((rest % k_fat_sectors_per_track) + 1) | ((cylinder >> 8) << 6));
    out[2] = (u8)cylinder;
}

static void write_entry(u8* out, const u8* name, u8 attributes, u16 date, u16 time_value, u32 cluster, u32 size)
{
    memcpy(out, name, 11);
    out[11] = attributes;
    write_u16_le(out + 22, time_value);
    write_u16_le(out + 24, date);
    write_u16_le(out + 26, (u16)cluster);
    write_u32_le(out + 28, size);
}

FatImage::FatImage()
{
    InitPointer(m_zip_file);
    InitPointer(m_output);
    InitPointer(m_boot_sector);
    memset(&m_zip, 0, sizeof(m_zip));
    memset(&m_layout, 0, sizeof(m_layout));
    m_zip_open = false;
    m_zip_offset = 0;
    m_label[0] = 0;
    m_fingerprint = 0;
    m_left_out = 0;
    m_next_cluster = 2;
    Clear();
    SetLabel(k_fat_label);
    Finish();
}

FatImage::~FatImage()
{
    Clear();
}

bool FatImage::AddFolder(const char* path)
{
    Clear();

    if (!IsValidPointer(path) || !WalkFolder(path, 0, 0))
        return false;

    SetLabel(get_source_name(path).c_str());
    Finish();
    return true;
}

bool FatImage::AddZip(const u8* archive, size_t size)
{
    Clear();

    if (!IsValidPointer(archive) || !mz_zip_reader_init_mem(&m_zip, archive, size, 0))
        return false;

    m_zip_open = true;

    if (!ScanZip())
        return false;

    Finish();
    return true;
}

bool FatImage::AddZipFile(const char* path)
{
    Clear();
    m_zip_file = MediaFile::OpenFile(path);

    if (!IsValidPointer(m_zip_file))
        return false;

    m_zip.m_pRead = ReadZipFile;
    m_zip.m_pIO_opaque = m_zip_file;

    if (!mz_zip_reader_init(&m_zip, (mz_uint64)m_zip_file->GetSize(), 0))
        return false;

    m_zip_open = true;

    if (!ScanZip())
        return false;

    SetLabel(get_source_name(path).c_str());
    Finish();
    return true;
}

void FatImage::SetLabel(const char* label)
{
    std::string text(label);
    std::string result;

    for (size_t i = 0; i < text.size(); i++)
    {
        u8 byte = (u8)text[i];

        if ((byte & 0xC0) == 0x80)
            continue;

        char c = to_upper(text[i]);

        if ((c == ' ') && !result.empty())
            result += c;
        else if (c != ' ')
            result += is_name_character(c) ? c : '_';
    }

    memcpy(m_label, pad(result, 11).c_str(), 11);
    m_label[11] = 0;
}

void FatImage::SetBootSector(const u8* sector)
{
    m_boot_sector = sector;
}

u32 FatImage::GetFingerprint() const
{
    return m_fingerprint;
}

u32 FatImage::GetLeftOut() const
{
    return m_left_out;
}

u64 FatImage::GetContentSize() const
{
    u64 size = 0;

    for (size_t i = 0; i < m_entries.size(); i++)
        size += m_entries[i].size;

    return size;
}

u64 FatImage::GetRequiredSize(u64 free_size) const
{
    u32 cylinders = (u32)(free_size / (k_fat_cylinder_sectors * k_fat_sector_size)) + 1;

    for (int i = 0; i < 16; i++)
    {
        for (int gap = 0; gap < 2; gap++)
        {
            if ((cylinders > k_fat_size_gaps[gap][0]) && (cylinders < k_fat_size_gaps[gap][1]))
                cylinders = k_fat_size_gaps[gap][1];
        }

        if (cylinders > k_fat_max_disk_cylinders)
            return 0;

        FatImage_Layout layout;
        u32 total = cylinders * k_fat_cylinder_sectors;

        if (!GetLayout(total, &layout))
        {
            cylinders++;
            continue;
        }

        u64 cluster_size = (u64)layout.cluster_sectors * k_fat_sector_size;
        u64 needed = CountClusters(0, (u32)cluster_size) + ((free_size + cluster_size - 1) / cluster_size);

        if (needed <= layout.clusters)
            return (u64)total * k_fat_sector_size;

        cylinders += (u32)(((needed - layout.clusters) * layout.cluster_sectors) / k_fat_cylinder_sectors) + 1;
    }

    return 0;
}

bool FatImage::Write(const char* path, u64 size, bool replace)
{
    u64 existing_size = 0;
    s64 modified = 0;

    if (!replace && MediaFile::GetFileInfo(path, &existing_size, &modified))
    {
        Error("An existing hard disk image is never replaced: %s", path);
        return false;
    }

    u32 total = (u32)(size / k_fat_sector_size);
    total -= total % k_fat_cylinder_sectors;

    if (!GetLayout(total, &m_layout))
    {
        Error("A FAT16 disk can't be %u sectors: %s", total, path);
        return false;
    }

    u32 cluster_size = m_layout.cluster_sectors * k_fat_sector_size;

    if (CountClusters(0, cluster_size) > m_layout.clusters)
    {
        Error("The files don't fit in a disk of %u sectors: %s", total, path);
        return false;
    }

    std::string temporary = std::string(path) + ".tmp";
    m_output = MediaFile::OpenNewFile(temporary.c_str());

    if (!IsValidPointer(m_output))
    {
        Error("Unable to create the hard disk image %s", temporary.c_str());
        return false;
    }

    m_fat.assign(m_layout.clusters + 2, 0);
    m_fat[0] = (u16)(0xFF00 | k_fat_media);
    m_fat[1] = k_fat_end_of_chain;
    m_next_cluster = 2;
    m_buffer.resize(k_fat_copy_size);

    u8 zero[k_fat_sector_size] = { };
    std::vector<u8> root;
    u8 label_entry[k_fat_entry_size] = { };
    write_entry(label_entry, (const u8*)m_label, k_fat_attribute_label, k_fat_default_date, k_fat_default_time, 0, 0);
    u64 root_offset = (u64)(k_fat_sectors_per_track + 1 + (2 * m_layout.fat_sectors)) * k_fat_sector_size;

    bool ok = m_output->WriteAt((u64)(total - 1) * k_fat_sector_size, zero, k_fat_sector_size);
    ok = ok && PlaceDirectory(0, 0, 0, &root);
    ok = ok && m_output->WriteAt(root_offset, label_entry, k_fat_entry_size);
    ok = ok && (root.empty() || m_output->WriteAt(root_offset + k_fat_entry_size, root.data(), root.size()));
    ok = ok && WriteFat() && WriteBootSector() && WriteMasterBootRecord() && m_output->Flush();
    SafeDelete(m_output);
    std::vector<u8>().swap(m_buffer);
    std::vector<u16>().swap(m_fat);
    ok = ok && MediaFile::RenameFile(temporary.c_str(), path);

    if (!ok)
    {
        MediaFile::RemoveFile(temporary.c_str());
        Error("Unable to write the hard disk image %s", path);
    }

    return ok;
}

bool FatImage::WriteFreeDOS(const char* path, bool replace)
{
    FatImage image;

    if (!image.AddZip(k_freedos_files, k_freedos_files_size))
        return false;

    image.SetBootSector(k_freedos_boot);
    return image.Write(path, k_fat_freedos_size, replace);
}

bool FatImage::ReadFingerprint(const char* path, u32* fingerprint)
{
    MediaFile* file = MediaFile::OpenFile(path);
    u8 sector[k_fat_sector_size];

    if (!IsValidPointer(file))
        return false;

    bool ok = file->ReadAt(0, sector, k_fat_sector_size) && (read_u16_le(sector + 510) == 0xAA55);
    u32 start = ok ? read_u32_le(sector + k_fat_entry_offset + 8) : 0;
    ok = ok && file->ReadAt((u64)start * k_fat_sector_size, sector, k_fat_sector_size) && (read_u16_le(sector + 510) == 0xAA55);
    SafeDelete(file);

    if (ok)
        *fingerprint = read_u32_le(sector + 0x27);

    return ok;
}

void FatImage::GetImageName(const char* source, std::string* name)
{
    std::string path = trim_path(source);
    char suffix[32];
    snprintf(suffix, sizeof(suffix), ".%08X.geardox.hdd", (u32)mz_crc32(0, (const u8*)path.data(), path.size()));
    *name = get_source_name(source) + suffix;
}

void FatImage::Prepare(const char* source, const char* image_path, bool rebuild, FatImage_Report* report)
{
    report->result = FAT_IMAGE_ERROR;
    report->left_out = 0;
    report->content_size = 0;

    FatImage image;

    if (!(ends_with_no_case(source, ".zip") && image.AddZipFile(source)) && !image.AddFolder(source))
        return;

    report->left_out = image.GetLeftOut();
    report->content_size = image.GetContentSize();
    u64 existing_size = 0;
    s64 modified = 0;

    if (!rebuild && MediaFile::GetFileInfo(image_path, &existing_size, &modified))
    {
        u32 fingerprint = 0;
        bool same = ReadFingerprint(image_path, &fingerprint) && (fingerprint == image.GetFingerprint());
        report->result = same ? FAT_IMAGE_SAME : FAT_IMAGE_CHANGED;
        return;
    }

    u64 size = image.GetRequiredSize(k_fat_free_size);

    if (size == 0)
        report->result = FAT_IMAGE_TOO_BIG;
    else if (image.Write(image_path, size, rebuild))
        report->result = FAT_IMAGE_BUILT;
}

void FatImage::Clear()
{
    if (m_zip_open)
        mz_zip_reader_end(&m_zip);

    memset(&m_zip, 0, sizeof(m_zip));
    m_zip_open = false;
    SafeDelete(m_zip_file);
    m_entries.clear();
    m_entries.resize(1);
    m_entries[0].directory = true;
    m_entries[0].size = 0;
    m_entries[0].date = k_fat_default_date;
    m_entries[0].time = k_fat_default_time;
    m_left_out = 0;
}

bool FatImage::WalkFolder(const std::string& path, u32 directory, int depth)
{
    std::vector<std::string> folders;
    std::vector<std::string> files;

    if (!MediaFile::ListDirectory(path.c_str(), folders, files))
        return false;

    for (size_t i = 0; i < files.size(); i++)
    {
        std::string item(path);
        append_path_component(item, files[i].c_str());
        u64 size = 0;
        s64 modified = 0;

        if (is_clutter(files[i]))
            continue;

        if ((m_entries.size() >= k_fat_max_entries) || !MediaFile::GetFileInfo(item.c_str(), &size, &modified))
        {
            m_left_out++;
            continue;
        }

        u32 entry = AddEntry(directory, files[i], false);
        m_entries[entry].source = item;
        m_entries[entry].size = size;
        get_dos_time(modified, &m_entries[entry].date, &m_entries[entry].time);
    }

    for (size_t i = 0; (depth < k_fat_max_depth) && (i < folders.size()); i++)
    {
        std::string item(path);
        append_path_component(item, folders[i].c_str());
        u64 size = 0;
        s64 modified = 0;

        if (is_clutter(folders[i]) || MediaFile::IsFolderLink(item.c_str()))
            continue;

        if (m_entries.size() >= k_fat_max_entries)
        {
            m_left_out++;
            continue;
        }

        u32 entry = AddEntry(directory, folders[i], true);

        if (MediaFile::GetFileInfo(item.c_str(), &size, &modified))
            get_dos_time(modified, &m_entries[entry].date, &m_entries[entry].time);

        if (!WalkFolder(item, entry, depth + 1))
            m_left_out++;
    }

    return true;
}

bool FatImage::ScanZip()
{
    mz_uint count = mz_zip_reader_get_num_files(&m_zip);
    std::vector<std::pair<std::string, mz_uint> > order;

    for (mz_uint i = 0; i < count; i++)
    {
        mz_zip_archive_file_stat stat;

        if (!mz_zip_reader_file_stat(&m_zip, i, &stat))
            return false;

        order.push_back(std::make_pair(upper(stat.m_filename), i));
    }

    std::sort(order.begin(), order.end());
    std::map<std::string, u32> directories;
    directories[""] = 0;

    for (size_t i = 0; i < order.size(); i++)
    {
        mz_zip_archive_file_stat stat;

        if (!mz_zip_reader_file_stat(&m_zip, order[i].second, &stat))
            return false;

        std::string name(stat.m_filename);
        std::replace(name.begin(), name.end(), '\\', '/');
        std::vector<std::string> parts;
        size_t start = 0;
        bool clutter = false;

        while (start <= name.size())
        {
            size_t end = name.find('/', start);
            end = end == std::string::npos ? name.size() : end;
            std::string part = name.substr(start, end - start);
            start = end + 1;

            if (part.empty() || (part == "."))
                continue;

            if (part == "..")
            {
                Error("This ZIP file has an entry outside of it: %s", stat.m_filename);
                return false;
            }

            clutter = clutter || is_clutter(part);
            parts.push_back(part);
        }

        if (parts.empty() || clutter)
            continue;

        if (!stat.m_is_directory && (!stat.m_is_supported || (stat.m_uncomp_size > k_fat_max_file_size)))
        {
            Error("This ZIP file has an encrypted, unsupported or oversized entry: %s", stat.m_filename);
            return false;
        }

        if (m_entries.size() >= k_fat_max_entries)
        {
            m_left_out++;
            continue;
        }

        u16 date = 0;
        u16 time_value = 0;
        get_dos_time((s64)stat.m_time, &date, &time_value);
        size_t folders = stat.m_is_directory ? parts.size() : parts.size() - 1;
        std::string path;
        u32 directory = 0;

        for (size_t part = 0; part < folders; part++)
        {
            path += "/" + parts[part];
            std::map<std::string, u32>::iterator found = directories.find(path);

            if (found != directories.end())
            {
                directory = found->second;
                continue;
            }

            directory = AddEntry(directory, parts[part], true);
            m_entries[directory].date = date;
            m_entries[directory].time = time_value;
            directories[path] = directory;
        }

        if (stat.m_is_directory)
            continue;

        u32 entry = AddEntry(directory, parts[parts.size() - 1], false);
        m_entries[entry].size = stat.m_uncomp_size;
        m_entries[entry].zip_index = order[i].second;
        m_entries[entry].date = date;
        m_entries[entry].time = time_value;
    }

    return true;
}

u32 FatImage::AddEntry(u32 directory, const std::string& name, bool is_directory)
{
    u32 index = (u32)m_entries.size();
    m_entries.resize(index + 1);
    FatImage_Entry& entry = m_entries[index];
    entry.name = name;
    memset(entry.short_name, ' ', sizeof(entry.short_name));
    entry.directory = is_directory;
    entry.size = 0;
    entry.date = k_fat_default_date;
    entry.time = k_fat_default_time;
    entry.zip_index = 0;
    entry.first_cluster = 0;
    m_entries[directory].children.push_back(index);
    return index;
}

void FatImage::Finish()
{
    AssignNames(0);
    LimitPaths(0, "");
    u32 crc = (u32)mz_crc32(0, (const u8*)m_label, 11);
    HashEntries(0, &crc);
    m_fingerprint = crc;
}

void FatImage::AssignNames(u32 directory)
{
    std::vector<u32>& children = m_entries[directory].children;
    std::vector<std::pair<std::pair<bool, std::string>, std::pair<std::string, u32> > > order;

    for (size_t i = 0; i < children.size(); i++)
    {
        const FatImage_Entry& entry = m_entries[children[i]];
        order.push_back(std::make_pair(std::make_pair(!is_clean_name(entry.name), upper(entry.name)),
            std::make_pair(entry.name, children[i])));
    }

    std::sort(order.begin(), order.end());
    std::set<std::string> taken;
    std::map<std::string, u32> tails;

    for (size_t i = 0; i < order.size(); i++)
    {
        FatImage_Entry& entry = m_entries[order[i].second.second];
        std::string base;
        std::string extension;
        split_name(entry.name, &base, &extension);
        std::string short_base = clean_part(base);
        std::string short_extension = clean_part(extension).substr(0, 3);
        std::string candidate = pad(short_base, 8) + pad(short_extension, 3);

        if (!is_clean_name(entry.name) || (taken.count(candidate) != 0))
        {
            std::string key = short_base + "." + short_extension;
            u32& tail = tails[key];

            do
            {
                char number[16];
                snprintf(number, sizeof(number), "~%u", ++tail);
                size_t room = 8 - strlen(number);
                candidate = pad(short_base.substr(0, room) + number, 8) + pad(short_extension, 3);
            }
            while (taken.count(candidate) != 0);
        }

        taken.insert(candidate);
        memcpy(entry.short_name, candidate.data(), sizeof(entry.short_name));

        if (entry.directory)
            AssignNames(order[i].second.second);
    }

    std::vector<std::pair<std::string, u32> > sorted;

    for (size_t i = 0; i < children.size(); i++)
    {
        const FatImage_Entry& entry = m_entries[children[i]];
        sorted.push_back(std::make_pair(std::string((const char*)entry.short_name, 11), children[i]));
    }

    std::sort(sorted.begin(), sorted.end());

    for (size_t i = 0; i < sorted.size(); i++)
        children[i] = sorted[i].second;
}

void FatImage::LimitPaths(u32 directory, const std::string& path)
{
    std::vector<u32> kept;

    for (size_t i = 0; i < m_entries[directory].children.size(); i++)
    {
        u32 index = m_entries[directory].children[i];
        const FatImage_Entry& entry = m_entries[index];
        std::string item = path.empty() ? pretty_name(entry.short_name) : path + "\\" + pretty_name(entry.short_name);

        if (entry.directory && (item.size() > k_fat_max_path))
        {
            m_left_out += 1 + CountEntries(index);
            continue;
        }

        kept.push_back(index);

        if (entry.directory)
            LimitPaths(index, item);
    }

    m_entries[directory].children.swap(kept);
}

u32 FatImage::CountEntries(u32 directory) const
{
    u32 count = 0;

    for (size_t i = 0; i < m_entries[directory].children.size(); i++)
    {
        u32 index = m_entries[directory].children[i];
        count += 1 + (m_entries[index].directory ? CountEntries(index) : 0);
    }

    return count;
}

// A folder's own date moves whenever the host writes in it, so only its name counts
void FatImage::HashEntries(u32 directory, u32* crc) const
{
    for (size_t i = 0; i < m_entries[directory].children.size(); i++)
    {
        const FatImage_Entry& entry = m_entries[m_entries[directory].children[i]];
        u8 data[12] = { };

        if (!entry.directory)
        {
            write_u32_le(data, (u32)entry.size);
            write_u32_le(data + 4, (u32)(entry.size >> 32));
            write_u16_le(data + 8, entry.date);
            write_u16_le(data + 10, entry.time);
        }

        *crc = (u32)mz_crc32(*crc, (const u8*)entry.name.data(), entry.name.size());
        *crc = (u32)mz_crc32(*crc, data, sizeof(data));

        if (entry.directory)
            HashEntries(m_entries[directory].children[i], crc);
    }
}

u64 FatImage::CountClusters(u32 directory, u32 cluster_size) const
{
    u64 count = 0;

    for (size_t i = 0; i < m_entries[directory].children.size(); i++)
    {
        u32 index = m_entries[directory].children[i];
        const FatImage_Entry& entry = m_entries[index];

        if (entry.directory)
        {
            u32 size = (u32)(entry.children.size() + 2) * k_fat_entry_size;
            count += ((size + cluster_size - 1) / cluster_size) + CountClusters(index, cluster_size);
        }
        else
            count += (entry.size + cluster_size - 1) / cluster_size;
    }

    return count;
}

bool FatImage::GetLayout(u32 total, FatImage_Layout* layout) const
{
    u32 heads = get_logical_heads(total);
    u32 reach = MIN(total, k_fat_max_cylinders * heads * k_fat_sectors_per_track);
    reach -= reach % (heads * k_fat_sectors_per_track);

    if (reach <= k_fat_sectors_per_track)
        return false;

    u32 partition = reach - k_fat_sectors_per_track;
    u32 cluster_sectors = 0;

    for (int i = 0; (i < 5) && (cluster_sectors == 0); i++)
    {
        if (partition <= k_fat_cluster_table[i][0])
            cluster_sectors = k_fat_cluster_table[i][1];
    }

    if (cluster_sectors == 0)
        return false;

    u32 root_entries = (u32)m_entries[0].children.size() + 1;
    root_entries = MAX(k_fat_root_entries, (root_entries + 15) & ~15U);
    u32 root_sectors = (root_entries * k_fat_entry_size) / k_fat_sector_size;
    u32 divisor = (256 * cluster_sectors) + 2;
    u32 fat_sectors = (partition - (1 + root_sectors) + divisor - 1) / divisor;
    u32 first_data = 1 + (2 * fat_sectors) + root_sectors;
    u32 clusters = (partition - first_data) / cluster_sectors;

    if ((clusters <= k_fat_min_clusters) || (clusters >= k_fat_max_clusters))
        return false;

    layout->total = total;
    layout->heads = heads;
    layout->partition = partition;
    layout->cluster_sectors = cluster_sectors;
    layout->root_entries = root_entries;
    layout->root_sectors = root_sectors;
    layout->fat_sectors = fat_sectors;
    layout->first_data = first_data;
    layout->clusters = clusters;
    return true;
}

u32 FatImage::Allocate(u32 clusters)
{
    u32 first = m_next_cluster;

    for (u32 i = 0; i < clusters; i++)
        m_fat[first + i] = (i + 1) < clusters ? (u16)(first + i + 1) : k_fat_end_of_chain;

    m_next_cluster += clusters;
    return first;
}

bool FatImage::PlaceDirectory(u32 directory, u32 cluster, u32 parent, std::vector<u8>* content)
{
    u32 cluster_size = m_layout.cluster_sectors * k_fat_sector_size;
    const std::vector<u32> children = m_entries[directory].children;
    const FatImage_Entry& self = m_entries[directory];
    u32 own_entries = cluster != 0 ? 2 : 0;
    content->assign((children.size() + own_entries) * k_fat_entry_size, 0);

    if (cluster != 0)
    {
        u8 dot[11];
        memset(dot, ' ', sizeof(dot));
        dot[0] = '.';
        write_entry(content->data(), dot, k_fat_attribute_directory, self.date, self.time, cluster, 0);
        dot[1] = '.';
        write_entry(content->data() + k_fat_entry_size, dot, k_fat_attribute_directory, self.date, self.time, parent, 0);
    }

    for (size_t i = 0; i < children.size(); i++)
    {
        FatImage_Entry& entry = m_entries[children[i]];

        if (entry.directory)
        {
            u32 size = (u32)(entry.children.size() + 2) * k_fat_entry_size;
            entry.first_cluster = Allocate((size + cluster_size - 1) / cluster_size);
        }
    }

    for (size_t i = 0; i < children.size(); i++)
    {
        FatImage_Entry& entry = m_entries[children[i]];

        if (entry.directory || (entry.size == 0))
            continue;

        entry.first_cluster = Allocate((u32)((entry.size + cluster_size - 1) / cluster_size));

        if (!CopyFile(entry, GetClusterOffset(entry.first_cluster)))
            return false;
    }

    for (size_t i = 0; i < children.size(); i++)
    {
        const FatImage_Entry& entry = m_entries[children[i]];
        write_entry(content->data() + ((i + own_entries) * k_fat_entry_size), entry.short_name,
            entry.directory ? k_fat_attribute_directory : k_fat_attribute_archive, entry.date, entry.time,
            entry.first_cluster, entry.directory ? 0 : (u32)entry.size);
    }

    for (size_t i = 0; i < children.size(); i++)
    {
        const FatImage_Entry& entry = m_entries[children[i]];

        if (!entry.directory)
            continue;

        std::vector<u8> child;

        if (!PlaceDirectory(children[i], entry.first_cluster, cluster, &child) ||
            !m_output->WriteAt(GetClusterOffset(entry.first_cluster), child.data(), child.size()))
            return false;
    }

    return true;
}

bool FatImage::CopyFile(const FatImage_Entry& entry, u64 offset)
{
    if (entry.source.empty())
    {
        m_zip_offset = offset;
        return mz_zip_reader_extract_to_callback(&m_zip, entry.zip_index, WriteZipEntry, this, 0) != 0;
    }

    MediaFile* file = MediaFile::OpenFile(entry.source.c_str());

    if (!IsValidPointer(file))
    {
        Error("Unable to read %s", entry.source.c_str());
        return false;
    }

    u64 left = entry.size;
    bool ok = true;

    while (ok && (left != 0))
    {
        u32 chunk = (u32)MIN(left, (u64)m_buffer.size());
        ok = file->ReadExact(m_buffer.data(), chunk) && m_output->WriteAt(offset, m_buffer.data(), chunk);
        offset += chunk;
        left -= chunk;
    }

    SafeDelete(file);

    if (!ok)
        Error("Unable to copy %s", entry.source.c_str());

    return ok;
}

bool FatImage::WriteFat()
{
    std::vector<u8> table((size_t)m_layout.fat_sectors * k_fat_sector_size, 0);

    for (size_t i = 0; i < m_fat.size(); i++)
        write_u16_le(table.data() + (i * 2), m_fat[i]);

    for (u32 i = 0; i < 2; i++)
    {
        u64 offset = (u64)(k_fat_sectors_per_track + 1 + (i * m_layout.fat_sectors)) * k_fat_sector_size;

        if (!m_output->WriteAt(offset, table.data(), table.size()))
            return false;
    }

    return true;
}

bool FatImage::WriteBootSector()
{
    u8 sector[k_fat_sector_size] = { };
    u32 partition = m_layout.partition;

    if (IsValidPointer(m_boot_sector))
        memcpy(sector, m_boot_sector, sizeof(sector));
    else
    {
        sector[0] = 0xEB;
        sector[1] = k_fat_code_offset - 2;
        sector[2] = 0x90;
        memcpy(sector + 3, k_fat_oem, 8);
        memcpy(sector + k_fat_code_offset, k_freedos_no_system, k_freedos_no_system_size);
        write_u16_le(sector + 510, 0xAA55);
    }

    write_u16_le(sector + 11, k_fat_sector_size);
    sector[13] = (u8)m_layout.cluster_sectors;
    write_u16_le(sector + 14, 1);
    sector[16] = 2;
    write_u16_le(sector + 17, (u16)m_layout.root_entries);
    write_u16_le(sector + 19, partition < 0x10000 ? (u16)partition : 0);
    sector[21] = k_fat_media;
    write_u16_le(sector + 22, (u16)m_layout.fat_sectors);
    write_u16_le(sector + 24, k_fat_sectors_per_track);
    write_u16_le(sector + 26, (u16)m_layout.heads);
    write_u32_le(sector + 28, k_fat_sectors_per_track);
    write_u32_le(sector + 32, partition < 0x10000 ? 0 : partition);
    sector[0x24] = 0x80;
    sector[0x25] = 0;
    sector[0x26] = 0x29;
    write_u32_le(sector + 0x27, m_fingerprint);
    memcpy(sector + 0x2B, m_label, 11);
    memcpy(sector + 0x36, k_fat_type, 8);
    return m_output->WriteAt((u64)k_fat_sectors_per_track * k_fat_sector_size, sector, sizeof(sector));
}

bool FatImage::WriteMasterBootRecord()
{
    u8 sector[k_fat_sector_size];
    memcpy(sector, k_freedos_mbr, sizeof(sector));
    memset(sector + k_fat_entry_offset, 0, 64);
    u8* entry = sector + k_fat_entry_offset;
    entry[0] = 0x80;
    write_chs(entry + 1, k_fat_sectors_per_track, m_layout.heads);
    entry[4] = 0x06;
    write_chs(entry + 5, k_fat_sectors_per_track + m_layout.partition - 1, m_layout.heads);
    write_u32_le(entry + 8, k_fat_sectors_per_track);
    write_u32_le(entry + 12, m_layout.partition);
    write_u16_le(sector + 510, 0xAA55);
    return m_output->WriteAt(0, sector, sizeof(sector));
}

u64 FatImage::GetClusterOffset(u32 cluster) const
{
    u64 sector = k_fat_sectors_per_track + m_layout.first_data + ((u64)(cluster - 2) * m_layout.cluster_sectors);
    return sector * k_fat_sector_size;
}

size_t FatImage::ReadZipFile(void* opaque, mz_uint64 offset, void* buffer, size_t size)
{
    MediaFile* file = (MediaFile*)opaque;
    return file->ReadAt(offset, buffer, size) ? size : 0;
}

size_t FatImage::WriteZipEntry(void* opaque, mz_uint64 offset, const void* buffer, size_t size)
{
    FatImage* image = (FatImage*)opaque;
    return image->m_output->WriteAt(image->m_zip_offset + offset, buffer, size) ? size : 0;
}
