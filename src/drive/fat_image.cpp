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
#include "../dos/dos_tools.h"
#include "../media/media_file.h"
#include "../media/crc.h"

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

static const u32 k_fat_min_cluster_sectors = 4;
static const u32 k_fat_max_cluster_sectors = 64;

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
    InitPointer(m_read);
    m_read_offset = 0;
    memset(&m_zip, 0, sizeof(m_zip));
    memset(&m_system_zip, 0, sizeof(m_system_zip));
    memset(&m_ultrasound_zip, 0, sizeof(m_ultrasound_zip));
    memset(&m_layout, 0, sizeof(m_layout));
    m_zip_open = false;
    m_system_zip_open = false;
    m_ultrasound_zip_open = false;
    m_pinned_count = 0;
    m_zip_offset = 0;
    m_label[0] = 0;
    m_fingerprint = 0;
    m_left_out = 0;
    m_next_cluster = 2;
    m_base = 0;
    Clear();
    SetLabel(k_fat_label);
    Finish();
}

FatImage::~FatImage()
{
    CloseRead();
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
    std::vector<u32> pending(1, 0);

    while (!pending.empty())
    {
        u32 directory = pending.back();
        pending.pop_back();

        for (size_t i = 0; i < m_entries[directory].children.size(); i++)
        {
            u32 child = m_entries[directory].children[i];
            size += m_entries[child].size;

            if (m_entries[child].directory)
                pending.push_back(child);
        }
    }

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
    u64 pinned_size = (u64)m_pinned_count * k_fat_entry_size;
    ok = ok && PlaceDirectory(0, 0, 0, &root);
    ok = ok && ((pinned_size == 0) || m_output->WriteAt(root_offset, root.data(), pinned_size));
    ok = ok && m_output->WriteAt(root_offset + pinned_size, label_entry, k_fat_entry_size);
    ok = ok && ((root.size() == pinned_size) || m_output->WriteAt(root_offset + pinned_size + k_fat_entry_size,
        root.data() + pinned_size, root.size() - pinned_size));
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
    *name = get_source_name(source) + "." + crc_key(trim_path(source)) + ".geardox.hdd";
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

// The game goes in dir on C:, its folders made as the system's. A descriptor in the game's root never reaches its disk
bool FatImage::AddGame(const char* source, const char* dir, const char* prefix)
{
    Clear();

    if (!IsValidPointer(source))
        return false;

    if (IsValidPointer(prefix) && (prefix[0] != 0))
    {
        m_zip_prefix = upper(prefix);
        std::replace(m_zip_prefix.begin(), m_zip_prefix.end(), '\\', '/');

        if (m_zip_prefix[m_zip_prefix.size() - 1] != '/')
            m_zip_prefix += "/";
    }

    std::string path = IsValidPointer(dir) ? dir : "";
    size_t start = 0;

    while (start < path.size())
    {
        size_t end = path.find('\\', start);
        end = end == std::string::npos ? path.size() : end;

        if (end > start)
        {
            m_base = AddEntry(m_base, path.substr(start, end - start), true);
            m_entries[m_base].origin = FAT_IMAGE_ORIGIN_SYSTEM;
        }

        start = end + 1;
    }

    if (ends_with_no_case(source, ".zip"))
    {
        m_zip_file = MediaFile::OpenFile(source);

        if (!IsValidPointer(m_zip_file))
            return false;

        m_zip.m_pRead = ReadZipFile;
        m_zip.m_pIO_opaque = m_zip_file;

        if (!mz_zip_reader_init(&m_zip, (mz_uint64)m_zip_file->GetSize(), 0))
            return false;

        m_zip_open = true;

        if (!ScanZip())
            return false;
    }
    else if (!WalkFolder(trim_path(source), m_base, 0))
        return false;

    for (size_t i = m_entries[m_base].children.size(); i > 0; i--)
    {
        u32 child = m_entries[m_base].children[i - 1];

        if (!m_entries[child].directory && ends_with_no_case(m_entries[child].name.c_str(), ".gdox"))
            RemoveChild(m_base, child);
    }

    return true;
}

bool FatImage::ApplyLayout(const std::vector<FatImage_Rule>& rules)
{
    for (size_t r = 0; r < rules.size(); r++)
    {
        const FatImage_Rule& rule = rules[r];
        u32 parent = m_base;
        int source = (int)m_base;
        std::string host;
        size_t start = 0;

        while ((source >= 0) && (start < rule.source.size()))
        {
            size_t end = rule.source.find('/', start);
            end = end == std::string::npos ? rule.source.size() : end;
            parent = (u32)source;
            source = FindChild(parent, rule.source.substr(start, end - start));

            if (source >= 0)
                host = !m_entries[source].host.empty() ? m_entries[source].host : (host.empty() ?
                    m_entries[source].name : host + "/" + m_entries[source].name);

            start = end + 1;
        }

        if ((source < 0) || ((u32)source == m_base))
            continue;

        std::string target_folder = rule.target;
        std::string target_name;

        if (rule.file)
        {
            size_t slash = rule.target.find_last_of('/');
            target_folder = slash == std::string::npos ? std::string() : rule.target.substr(0, slash);
            target_name = slash == std::string::npos ? rule.target : rule.target.substr(slash + 1);

            if (m_entries[source].directory || target_name.empty())
                continue;
        }
        else if (!m_entries[source].directory)
            continue;

        u32 destination = m_base;
        start = 0;

        while (start < target_folder.size())
        {
            size_t end = target_folder.find('/', start);
            end = end == std::string::npos ? target_folder.size() : end;
            std::string part = target_folder.substr(start, end - start);
            int found = FindChild(destination, part);

            if ((found >= 0) && !m_entries[found].directory)
            {
                RemoveChild(destination, (u32)found);
                found = -1;
            }

            destination = found >= 0 ? (u32)found : AddEntry(destination, part, true);
            start = end + 1;
        }

        if (destination == (u32)source)
            continue;

        RemoveChild(parent, (u32)source);

        if (rule.file)
        {
            int existing = FindChild(destination, target_name);

            if (existing >= 0)
                RemoveChild(destination, (u32)existing);

            if (m_entries[source].host.empty())
                m_entries[source].host = host;

            m_entries[source].name = target_name;
            m_entries[destination].children.push_back((u32)source);
            continue;
        }

        std::vector<u32> children = m_entries[source].children;
        m_entries[source].children.clear();

        for (size_t i = 0; i < children.size(); i++)
        {
            u32 child = children[i];
            int existing = FindChild(destination, m_entries[child].name);

            if (existing >= 0)
                RemoveChild(destination, (u32)existing);

            if (m_entries[child].host.empty())
                m_entries[child].host = host + "/" + m_entries[child].name;

            m_entries[destination].children.push_back(child);
        }
    }

    return true;
}

// Folders and files of the overlay replace the source's with the same name, whatever the case. Its .deleted lists
// what the guest removed, one path from the root a line
bool FatImage::AddOverlay(const char* path)
{
    std::string root = trim_path(path);
    u64 size = 0;
    s64 modified = 0;

    if (!WalkOverlay(root, m_base, 0))
        return false;

    std::string list = root;
    append_path_component(list, ".deleted");

    if (!MediaFile::GetFileInfo(list.c_str(), &size, &modified))
        return true;

    MediaFile* file = MediaFile::OpenFile(list.c_str());

    if (!IsValidPointer(file))
        return false;

    std::string text((size_t)size, '\0');
    bool ok = (size == 0) || file->ReadAt(0, &text[0], size);
    SafeDelete(file);

    if (ok)
        ApplyDeleted(text);

    return ok;
}

bool FatImage::WalkOverlay(const std::string& path, u32 directory, int depth)
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

        if (is_clutter(files[i]) || !MediaFile::GetFileInfo(item.c_str(), &size, &modified))
            continue;

        int found = FindChild(directory, files[i]);

        if ((found >= 0) && m_entries[found].directory)
        {
            RemoveChild(directory, (u32)found);
            found = -1;
        }

        u32 entry = found >= 0 ? (u32)found : AddEntry(directory, files[i], false);
        m_entries[entry].source = item;
        m_entries[entry].size = size;
        m_entries[entry].modified = modified;
        m_entries[entry].memory = -1;
        m_entries[entry].crc = 0;
        m_entries[entry].origin = FAT_IMAGE_ORIGIN_OVERLAY;
        get_dos_time(modified, &m_entries[entry].date, &m_entries[entry].time);
    }

    for (size_t i = 0; (depth < k_fat_max_depth) && (i < folders.size()); i++)
    {
        std::string item(path);
        append_path_component(item, folders[i].c_str());

        if (is_clutter(folders[i]) || MediaFile::IsFolderLink(item.c_str()))
            continue;

        int found = FindChild(directory, folders[i]);

        if ((found >= 0) && !m_entries[found].directory)
        {
            RemoveChild(directory, (u32)found);
            found = -1;
        }

        u32 entry = found >= 0 ? (u32)found : AddEntry(directory, folders[i], true);

        if (!WalkOverlay(item, entry, depth + 1))
            m_left_out++;
    }

    return true;
}

void FatImage::ApplyDeleted(const std::string& list)
{
    size_t start = 0;

    while (start < list.size())
    {
        size_t end = list.find('\n', start);
        end = end == std::string::npos ? list.size() : end;
        std::string line = list.substr(start, end - start);
        start = end + 1;

        if (!line.empty() && (line[line.size() - 1] == '\r'))
            line.resize(line.size() - 1);

        u32 directory = m_base;
        int found = -1;
        size_t part = 0;

        while (part <= line.size())
        {
            size_t next = line.find('/', part);
            next = next == std::string::npos ? line.size() : next;
            std::string name = line.substr(part, next - part);
            part = next + 1;

            if (name.empty())
                continue;

            if (found >= 0)
            {
                if (!m_entries[found].directory)
                {
                    found = -1;
                    break;
                }

                directory = (u32)found;
            }

            found = FindChild(directory, name);

            if (found < 0)
                break;
        }

        if (found > 0)
            RemoveChild(directory, (u32)found);
    }
}

// Laid out as FreeDOS installs itself, with CONFIG.SYS and AUTOEXEC.BAT left to the caller. Hidden, the five entries in
// the root are hidden and KERNEL.SYS is also read only and system
bool FatImage::AddFreeDOS(bool hidden, const std::vector<std::string>& reserved)
{
    u8 hide = hidden ? k_fat_attribute_hidden : 0;
    mz_zip_archive zip;
    LeaveOutReserved(reserved);

    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_mem(&zip, k_freedos_files, k_freedos_files_size, 0))
        return false;

    u32 freedos = AddEntry(0, "FREEDOS", true);
    m_entries[freedos].attributes = k_fat_attribute_directory | hide;
    m_entries[freedos].origin = FAT_IMAGE_ORIGIN_SYSTEM;
    bool ok = true;

    for (mz_uint i = 0; ok && (i < mz_zip_reader_get_num_files(&zip)); i++)
    {
        mz_zip_archive_file_stat stat;

        if (!mz_zip_reader_file_stat(&zip, i, &stat) || stat.m_is_directory)
            continue;

        std::string name(stat.m_filename);
        bool kernel = name == "KERNEL.SYS";

        if (!kernel && (name.compare(0, 8, "FREEDOS/") != 0))
            continue;

        size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);

        if (!IsValidPointer(data))
        {
            ok = false;
            break;
        }

        m_memory.push_back(std::string((const char*)data, size));
        mz_free(data);
        u32 directory = kernel ? 0 : freedos;
        std::string rest = kernel ? name : name.substr(8);
        size_t slash = rest.find('/');

        while (slash != std::string::npos)
        {
            std::string folder = rest.substr(0, slash);
            int found = FindChild(directory, folder);

            if (found < 0)
            {
                found = (int)AddEntry(directory, folder, true);
                m_entries[found].origin = FAT_IMAGE_ORIGIN_SYSTEM;
            }

            directory = (u32)found;
            rest = rest.substr(slash + 1);
            slash = rest.find('/');
        }

        u32 entry = AddEntry(directory, rest, false);
        FatImage_Entry& file = m_entries[entry];
        file.memory = (int)m_memory.size() - 1;
        file.size = size;
        file.crc = stat.m_crc32;
        file.origin = FAT_IMAGE_ORIGIN_SYSTEM;
        get_dos_time((s64)stat.m_time, &file.date, &file.time);

        if (kernel && hidden)
            file.attributes = k_fat_attribute_read_only | k_fat_attribute_hidden | k_fat_attribute_system;
    }

    mz_zip_reader_end(&zip);
    int bin = ok ? FindChild(freedos, "BIN") : -1;
    ok = (bin >= 0) && AddDosTool((u32)bin, "GDXRUN.COM", k_dos_gdxrun, k_dos_gdxrun_size) &&
        AddDosTool((u32)bin, "GDXDBG.COM", k_dos_gdxdbg, k_dos_gdxdbg_size) &&
        AddDosTool((u32)bin, "GDXGUS.COM", k_dos_gdxgus, k_dos_gdxgus_size) &&
        AddDosTool((u32)bin, "GDXPAD.COM", k_dos_gdxpad, k_dos_gdxpad_size) &&
        AddDosTool((u32)bin, "POWEROFF.COM", k_dos_poweroff, k_dos_poweroff_size);

    if (ok)
    {
        u32 temp = AddEntry(0, "TEMP", true);
        m_entries[temp].origin = FAT_IMAGE_ORIGIN_SYSTEM;
        m_entries[temp].attributes = k_fat_attribute_directory | hide;
    }

    return ok;
}

// An imported MS-DOS: its C/ tree from the user's ZIP, read at Write, IO.SYS and MSDOS.SYS ahead of the
// label as its boot sector wants, and Geardox' own tools from the FreeDOS files in \GEARDOX
bool FatImage::AddMSDOS(const u8* zip, size_t size, bool hidden, const std::vector<std::string>& reserved,
    const std::vector<std::string>& pinned, bool shsucdx)
{
    static const char* const k_tools[] = { "CTMOUSE.EXE", "UDVD2.SYS" };
    u8 hide = hidden ? k_fat_attribute_hidden : 0;
    LeaveOutReserved(reserved);

    if (m_system_zip_open)
        mz_zip_reader_end(&m_system_zip);

    memset(&m_system_zip, 0, sizeof(m_system_zip));
    m_system_zip_open = mz_zip_reader_init_mem(&m_system_zip, zip, size, 0) != 0;

    if (!m_system_zip_open)
        return false;

    u32 dos = AddEntry(0, "DOS", true);
    m_entries[dos].attributes = k_fat_attribute_directory | hide;
    m_entries[dos].origin = FAT_IMAGE_ORIGIN_SYSTEM;

    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&m_system_zip); i++)
    {
        mz_zip_archive_file_stat stat;

        if (!mz_zip_reader_file_stat(&m_system_zip, i, &stat) || stat.m_is_directory)
            continue;

        std::string name(stat.m_filename);

        if (name.compare(0, 2, "C/") != 0)
            continue;

        name = name.substr(2);
        u32 directory = 0;

        if (name.compare(0, 4, "DOS/") == 0)
        {
            directory = dos;
            name = name.substr(4);
        }

        if (name.empty() || (name.find('/') != std::string::npos))
            continue;

        bool boot = (directory == 0) && (std::find(pinned.begin(), pinned.end(), upper(name)) != pinned.end());
        u32 entry = AddEntry(directory, name, false);
        FatImage_Entry& file = m_entries[entry];
        file.size = stat.m_uncomp_size;
        file.crc = stat.m_crc32;
        file.zip_index = i;
        file.origin = FAT_IMAGE_ORIGIN_SYSTEM;
        get_dos_time((s64)stat.m_time, &file.date, &file.time);

        if (boot)
            file.attributes = k_fat_attribute_read_only | k_fat_attribute_hidden | k_fat_attribute_system;
        else if (directory == 0)
            file.attributes = k_fat_attribute_archive | hide;
    }

    mz_zip_archive tools;
    memset(&tools, 0, sizeof(tools));

    if (!mz_zip_reader_init_mem(&tools, k_freedos_files, k_freedos_files_size, 0))
        return false;

    u32 geardox = AddEntry(0, "GEARDOX", true);
    m_entries[geardox].attributes = k_fat_attribute_directory | hide;
    m_entries[geardox].origin = FAT_IMAGE_ORIGIN_SYSTEM;
    bool ok = AddFreeDOSFile(&tools, "FREEDOS/COPYING.TXT", geardox, "COPYING.TXT") &&
        AddDosTool(geardox, "GDXRUN.COM", k_dos_gdxrun, k_dos_gdxrun_size) &&
        AddDosTool(geardox, "GDXDBG.COM", k_dos_gdxdbg, k_dos_gdxdbg_size) &&
        AddDosTool(geardox, "GDXGUS.COM", k_dos_gdxgus, k_dos_gdxgus_size) &&
        AddDosTool(geardox, "POWEROFF.COM", k_dos_poweroff, k_dos_poweroff_size) &&
        AddDosTool(geardox, "GDXIDLE.SYS", k_dos_gdxidle, k_dos_gdxidle_size) &&
        AddDosTool(geardox, "GDXPAD.COM", k_dos_gdxpad, k_dos_gdxpad_size);

    for (size_t i = 0; ok && (i < sizeof(k_tools) / sizeof(k_tools[0])); i++)
        ok = AddFreeDOSFile(&tools, (std::string("FREEDOS/BIN/") + k_tools[i]).c_str(), geardox, k_tools[i]);

    if (ok && shsucdx)
        ok = AddFreeDOSFile(&tools, "FREEDOS/BIN/SHSUCDX.COM", geardox, "SHSUCDX.COM");

    mz_zip_reader_end(&tools);

    if (!ok)
        return false;

    u32 temp = AddEntry(0, "TEMP", true);
    m_entries[temp].origin = FAT_IMAGE_ORIGIN_SYSTEM;
    m_entries[temp].attributes = k_fat_attribute_directory | hide;
    m_pinned = pinned;
    return true;
}

bool FatImage::AddFreeDOSFile(mz_zip_archive* zip, const char* name, u32 directory, const char* target)
{
    int index = mz_zip_reader_locate_file(zip, name, NULL, 0);
    mz_zip_archive_file_stat stat;
    size_t size = 0;

    if ((index < 0) || !mz_zip_reader_file_stat(zip, (mz_uint)index, &stat))
        return false;

    void* data = mz_zip_reader_extract_to_heap(zip, (mz_uint)index, &size, 0);

    if (!IsValidPointer(data))
        return false;

    m_memory.push_back(std::string((const char*)data, size));
    mz_free(data);
    u32 entry = AddEntry(directory, target, false);
    m_entries[entry].memory = (int)m_memory.size() - 1;
    m_entries[entry].size = size;
    m_entries[entry].crc = stat.m_crc32;
    m_entries[entry].origin = FAT_IMAGE_ORIGIN_SYSTEM;
    get_dos_time((s64)stat.m_time, &m_entries[entry].date, &m_entries[entry].time);
    return true;
}

bool FatImage::AddUltraSound(const u8* zip, size_t size)
{
    static const char* const k_reserved[] = { "ULTRASND" };
    std::vector<std::string> reserved(k_reserved, k_reserved + 1);
    LeaveOutReserved(reserved);

    if (m_ultrasound_zip_open)
        mz_zip_reader_end(&m_ultrasound_zip);

    memset(&m_ultrasound_zip, 0, sizeof(m_ultrasound_zip));
    m_ultrasound_zip_open = mz_zip_reader_init_mem(&m_ultrasound_zip, zip, size, 0) != 0;

    if (!m_ultrasound_zip_open)
        return false;

    u32 root = AddEntry(0, "ULTRASND", true);
    m_entries[root].origin = FAT_IMAGE_ORIGIN_SYSTEM;
    m_entries[root].ultrasound = true;

    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&m_ultrasound_zip); i++)
    {
        mz_zip_archive_file_stat stat;

        if (!mz_zip_reader_file_stat(&m_ultrasound_zip, i, &stat) || stat.m_is_directory)
            continue;

        std::string rest(stat.m_filename);

        if (rest.empty() || (rest == "version.txt"))
            continue;

        u32 directory = root;
        size_t slash = rest.find('/');

        while (slash != std::string::npos)
        {
            std::string folder = rest.substr(0, slash);
            int found = FindChild(directory, folder);

            if (found < 0)
            {
                found = (int)AddEntry(directory, folder, true);
                m_entries[found].origin = FAT_IMAGE_ORIGIN_SYSTEM;
                m_entries[found].ultrasound = true;
            }

            directory = (u32)found;
            rest = rest.substr(slash + 1);
            slash = rest.find('/');
        }

        u32 entry = AddEntry(directory, rest, false);
        FatImage_Entry& file = m_entries[entry];
        file.size = stat.m_uncomp_size;
        file.crc = stat.m_crc32;
        file.zip_index = i;
        file.origin = FAT_IMAGE_ORIGIN_SYSTEM;
        file.ultrasound = true;
        get_dos_time((s64)stat.m_time, &file.date, &file.time);
    }

    return true;
}

bool FatImage::AddDosTool(u32 directory, const char* name, const u8* data, u32 size)
{
    m_memory.push_back(std::string((const char*)data, size));
    u32 entry = AddEntry(directory, name, false);
    m_entries[entry].memory = (int)m_memory.size() - 1;
    m_entries[entry].size = size;
    m_entries[entry].crc = CalculateCRC32(0, data, size);
    m_entries[entry].origin = FAT_IMAGE_ORIGIN_SYSTEM;
    m_entries[entry].date = k_dos_tools_date;
    m_entries[entry].time = k_dos_tools_time;
    return true;
}

// The names in m_pinned lead the root in their order, so the label is written after them
void FatImage::PinEntries()
{
    std::vector<u32>& children = m_entries[0].children;
    std::vector<u32> pinned;
    m_pinned_count = 0;

    for (size_t p = 0; p < m_pinned.size(); p++)
    {
        for (size_t i = 0; i < children.size(); i++)
        {
            if (upper(m_entries[children[i]].name) == m_pinned[p])
            {
                pinned.push_back(children[i]);
                children.erase(children.begin() + i);
                break;
            }
        }
    }

    children.insert(children.begin(), pinned.begin(), pinned.end());
    m_pinned_count = (u32)pinned.size();
}

void FatImage::LeaveOutReserved(const std::vector<std::string>& reserved)
{
    for (size_t i = m_entries[0].children.size(); i > 0; i--)
    {
        u32 child = m_entries[0].children[i - 1];
        std::string name = upper(m_entries[child].name);

        if (std::find(reserved.begin(), reserved.end(), name) != reserved.end())
        {
            m_left_out += 1 + CountEntries(child);
            RemoveChild(0, child);
        }
    }
}

bool FatImage::HasFile(const char* path) const
{
    u32 directory = 0;
    std::string rest;

    if (!FindParent(path, &directory, &rest))
        return false;

    int found = FindChild(directory, rest);
    return (found >= 0) && !m_entries[found].directory;
}

bool FatImage::GetFile(const char* path, std::string* data)
{
    u32 directory = 0;
    std::string rest;

    if (!FindParent(path, &directory, &rest))
        return false;

    int found = FindChild(directory, rest);
    return (found >= 0) && !m_entries[found].directory && ReadEntry(m_entries[found], data);
}

void FatImage::LeaveOutMatching(const char* name, bool (*match)(const std::string& data))
{
    LeaveOutMatching(0, upper(name), match);
}

void FatImage::LeaveOutMatching(u32 directory, const std::string& name, bool (*match)(const std::string& data))
{
    for (size_t i = m_entries[directory].children.size(); i > 0; i--)
    {
        u32 child = m_entries[directory].children[i - 1];
        FatImage_Entry& entry = m_entries[child];

        if (entry.directory)
        {
            LeaveOutMatching(child, name, match);
            continue;
        }

        std::string data;

        if ((upper(entry.name) == name) && ReadEntry(entry, &data) && match(data))
        {
            m_left_out++;
            RemoveChild(directory, child);
        }
    }
}

bool FatImage::AddMemoryFile(const char* path, const std::string& data, u8 attributes)
{
    u32 directory = 0;
    std::string rest;

    if (!FindParent(path, &directory, &rest))
        return false;

    int existing = FindChild(directory, rest);

    if (existing >= 0)
        RemoveChild(directory, (u32)existing);

    m_memory.push_back(data);
    u32 entry = AddEntry(directory, rest, false);
    m_entries[entry].memory = (int)m_memory.size() - 1;
    m_entries[entry].size = data.size();
    m_entries[entry].crc = CalculateCRC32(0, data.data(), data.size());
    m_entries[entry].attributes = attributes;
    m_entries[entry].origin = FAT_IMAGE_ORIGIN_SYSTEM;
    return true;
}

bool FatImage::PatchFile(const char* path, u64 offset, const std::string& before, const std::string& after)
{
    u32 directory = 0;
    std::string name;

    if (!FindParent(path, &directory, &name))
        return false;

    int found = FindChild(directory, name);

    if ((found < 0) || m_entries[found].directory || (before.size() != after.size()) ||
        (offset + before.size() > m_entries[found].size))
        return false;

    FatImage_Entry& entry = m_entries[found];
    std::string data;

    if (!ReadEntry(entry, &data) || (data.compare((size_t)offset, before.size(), before) != 0))
        return false;

    data.replace((size_t)offset, after.size(), after);
    m_memory.push_back(data);
    entry.memory = (int)m_memory.size() - 1;
    entry.crc = CalculateCRC32(0, data.data(), data.size());
    entry.origin = FAT_IMAGE_ORIGIN_SYSTEM;
    return true;
}

void FatImage::FinishGame(const char* label, u64 free_size, const u8* boot_sector)
{
    SetLabel(label);
    SetBootSector(boot_sector);
    Finish();
    u8 data[8];
    write_u32_le(data, (u32)free_size);
    write_u32_le(data + 4, (u32)(free_size >> 32));
    u32 crc = CalculateCRC32(m_fingerprint, boot_sector, k_fat_sector_size);
    crc = CalculateCRC32(crc, k_freedos_mbr, k_freedos_mbr_size);
    m_fingerprint = CalculateCRC32(crc, data, sizeof(data));
}

const FatImage_Entry& FatImage::GetEntry(u32 index) const
{
    return m_entries[index];
}

void FatImage::ListEntries(std::vector<FatImage_Listed>* listed) const
{
    listed->clear();
    ListDirectory(0, "", "", listed);
}

void FatImage::ListDirectory(u32 directory, const std::string& dos_path, const std::string& host_path,
    std::vector<FatImage_Listed>* listed) const
{
    for (size_t i = 0; i < m_entries[directory].children.size(); i++)
    {
        u32 index = m_entries[directory].children[i];
        const FatImage_Entry& entry = m_entries[index];
        FatImage_Listed item;
        item.dos_path = dos_path.empty() ? pretty_name(entry.short_name) : dos_path + "\\" + pretty_name(entry.short_name);
        item.host_path = !entry.host.empty() ? entry.host : (host_path.empty() ? entry.name : host_path + "/" +
            entry.name);
        item.index = index;
        listed->push_back(item);

        if (entry.directory)
            ListDirectory(index, item.dos_path, index == m_base ? std::string() : item.host_path, listed);
    }
}

int FatImage::FindChild(u32 directory, const std::string& name) const
{
    std::string wanted = upper(name);

    for (size_t i = 0; i < m_entries[directory].children.size(); i++)
    {
        u32 child = m_entries[directory].children[i];

        if (upper(m_entries[child].name) == wanted)
            return (int)child;
    }

    return -1;
}

bool FatImage::FindParent(const char* path, u32* directory, std::string* name) const
{
    std::string rest(path);
    size_t slash = rest.find('/');
    *directory = 0;

    while (slash != std::string::npos)
    {
        int found = FindChild(*directory, rest.substr(0, slash));

        if ((found < 0) || !m_entries[found].directory)
            return false;

        *directory = (u32)found;
        rest = rest.substr(slash + 1);
        slash = rest.find('/');
    }

    *name = rest;
    return true;
}

bool FatImage::ReadEntry(const FatImage_Entry& entry, std::string* data)
{
    if (entry.memory >= 0)
    {
        *data = m_memory[entry.memory];
        return true;
    }

    if (entry.source.empty())
    {
        size_t size = 0;
        mz_zip_archive* zip = entry.ultrasound ? &m_ultrasound_zip :
            entry.origin == FAT_IMAGE_ORIGIN_SYSTEM ? &m_system_zip : &m_zip;
        void* bytes = mz_zip_reader_extract_to_heap(zip, entry.zip_index, &size, 0);

        if (!IsValidPointer(bytes))
            return false;

        data->assign((const char*)bytes, size);
        mz_free(bytes);
        return true;
    }

    MediaFile* file = MediaFile::OpenFile(entry.source.c_str());

    if (!IsValidPointer(file))
        return false;

    data->resize((size_t)entry.size);
    bool ok = data->empty() || file->ReadExact(&(*data)[0], entry.size);
    SafeDelete(file);
    return ok;
}

void FatImage::RemoveChild(u32 directory, u32 index)
{
    std::vector<u32>& children = m_entries[directory].children;
    children.erase(std::remove(children.begin(), children.end(), index), children.end());
}

// The walk write-back trusts: a chain out of range or looping, a cluster in two chains, or a size past its chain and
// the image is refused
bool FatImage::Read(const char* path)
{
    CloseRead();
    m_read = MediaFile::OpenFile(path);
    u8 sector[k_fat_sector_size];

    if (!IsValidPointer(m_read) || !m_read->ReadAt(0, sector, sizeof(sector)) || (read_u16_le(sector + 510) != 0xAA55))
    {
        m_read_error = "The hard disk image has no partition table";
        CloseRead();
        return false;
    }

    u32 start = read_u32_le(sector + k_fat_entry_offset + 8);
    m_read_offset = (u64)start * k_fat_sector_size;

    if (!m_read->ReadAt(m_read_offset, sector, sizeof(sector)) || (read_u16_le(sector + 510) != 0xAA55) ||
        (read_u16_le(sector + 11) != k_fat_sector_size))
    {
        m_read_error = "The partition has no FAT boot sector";
        CloseRead();
        return false;
    }

    u32 cluster_sectors = sector[13];
    u32 reserved = read_u16_le(sector + 14);
    u32 fats = sector[16];
    u32 root_entries = read_u16_le(sector + 17);
    u32 total = read_u16_le(sector + 19) != 0 ? read_u16_le(sector + 19) : read_u32_le(sector + 32);
    u32 fat_sectors = read_u16_le(sector + 22);
    u32 root_sectors = ((root_entries * k_fat_entry_size) + k_fat_sector_size - 1) / k_fat_sector_size;
    u32 first_data = reserved + (fats * fat_sectors) + root_sectors;
    bool power = (cluster_sectors != 0) && ((cluster_sectors & (cluster_sectors - 1)) == 0);
    u32 clusters = (power && (total > first_data)) ? (total - first_data) / cluster_sectors : 0;

    if (!power || (reserved == 0) || (fats == 0) || (fat_sectors == 0) || (clusters <= k_fat_min_clusters) ||
        (clusters >= k_fat_max_clusters) || ((u64)fat_sectors * k_fat_sector_size / 2 < clusters + 2))
    {
        m_read_error = "The partition isn't a FAT16 volume Geardox can read";
        CloseRead();
        return false;
    }

    memset(&m_layout, 0, sizeof(m_layout));
    m_layout.cluster_sectors = cluster_sectors;
    m_layout.root_entries = root_entries;
    m_layout.root_sectors = root_sectors;
    m_layout.fat_sectors = fat_sectors;
    m_layout.first_data = first_data;
    m_layout.clusters = clusters;
    m_layout.partition = reserved;
    std::vector<u8> table((size_t)fat_sectors * k_fat_sector_size);

    if (!m_read->ReadAt(m_read_offset + (u64)reserved * k_fat_sector_size, table.data(), table.size()))
    {
        m_read_error = "Unable to read the FAT";
        CloseRead();
        return false;
    }

    m_fat.resize(clusters + 2);

    for (u32 i = 0; i < clusters + 2; i++)
        m_fat[i] = read_u16_le(table.data() + (i * 2));

    std::vector<u8> used(clusters + 2, 0);

    if (!ReadDirectory(0, "", 0, &used))
    {
        CloseRead();
        return false;
    }

    return true;
}

void FatImage::CloseRead()
{
    SafeDelete(m_read);
    m_files.clear();
    m_fat.clear();
}

const std::vector<FatImage_File>& FatImage::GetFiles() const
{
    return m_files;
}

void FatImage::GetSectorMap(std::vector<FatImage_Extent>* extents) const
{
    extents->clear();

    if (m_fat.empty())
        return;

    u32 partition = (u32)(m_read_offset / k_fat_sector_size);
    u32 root = partition + m_layout.first_data - m_layout.root_sectors;
    FatImage_Extent system[3] =
    {
        { 0, partition - 1, -1 },
        { partition, root - 1, -2 },
        { root, partition + m_layout.first_data - 1, -3 }
    };

    for (int i = 0; i < 3; i++)
    {
        if (partition > 0 && system[i].last >= system[i].first)
            extents->push_back(system[i]);
    }

    for (size_t i = 0; i < m_files.size(); i++)
    {
        u32 cluster = m_files[i].first_cluster;
        u32 guard = 0;
        FatImage_Extent extent = { 0, 0, (int)i };
        bool open = false;

        while (cluster >= 2 && cluster < m_fat.size() && guard++ <= m_layout.clusters)
        {
            u32 first = partition + m_layout.first_data + (cluster - 2) * m_layout.cluster_sectors;

            if (open && first == extent.last + 1)
                extent.last = first + m_layout.cluster_sectors - 1;
            else
            {
                if (open)
                    extents->push_back(extent);

                extent.first = first;
                extent.last = first + m_layout.cluster_sectors - 1;
                open = true;
            }

            cluster = m_fat[cluster];
        }

        if (open)
            extents->push_back(extent);
    }
}

const char* FatImage::GetReadError() const
{
    return m_read_error.c_str();
}

bool FatImage::ReadDirectory(u32 cluster, const std::string& path, int depth, std::vector<u8>* used)
{
    std::vector<u8> data;
    u32 cluster_size = m_layout.cluster_sectors * k_fat_sector_size;

    if (depth > k_fat_max_depth)
    {
        m_read_error = "Directories nest too deep at " + path;
        return false;
    }

    if (cluster == 0)
    {
        data.resize((size_t)m_layout.root_sectors * k_fat_sector_size);
        u64 root = m_read_offset + (u64)(m_layout.partition + (2 * m_layout.fat_sectors)) * k_fat_sector_size;

        if (!m_read->ReadAt(root, data.data(), data.size()))
        {
            m_read_error = "Unable to read the root directory";
            return false;
        }
    }
    else
    {
        std::vector<u32> chain;

        if (!ReadChain(cluster, &chain, used))
        {
            m_read_error += " in directory " + path;
            return false;
        }

        data.resize(chain.size() * cluster_size);

        for (size_t i = 0; i < chain.size(); i++)
        {
            u64 offset = m_read_offset + ((u64)m_layout.first_data + (u64)(chain[i] - 2) * m_layout.cluster_sectors) *
                k_fat_sector_size;

            if (!m_read->ReadAt(offset, data.data() + (i * cluster_size), cluster_size))
            {
                m_read_error = "Unable to read directory " + path;
                return false;
            }
        }
    }

    for (size_t offset = 0; offset + k_fat_entry_size <= data.size(); offset += k_fat_entry_size)
    {
        const u8* entry = data.data() + offset;
        u8 attributes = entry[11];

        if (entry[0] == 0x00)
            break;

        if ((entry[0] == 0xE5) || ((attributes & 0x0F) == 0x0F) || ((attributes & k_fat_attribute_label) != 0) ||
            (entry[0] == '.'))
            continue;

        u8 name[11];
        memcpy(name, entry, sizeof(name));

        if (name[0] == 0x05)
            name[0] = 0xE5;

        FatImage_File file;
        file.path = path.empty() ? pretty_name(name) : path + "\\" + pretty_name(name);
        file.attributes = attributes;
        file.directory = (attributes & k_fat_attribute_directory) != 0;
        file.size = file.directory ? 0 : read_u32_le(entry + 28);
        file.date = read_u16_le(entry + 24);
        file.time = read_u16_le(entry + 22);
        file.first_cluster = read_u16_le(entry + 26);

        if (file.directory)
        {
            if (file.first_cluster < 2)
            {
                m_read_error = "Directory " + file.path + " has no cluster";
                return false;
            }

            m_files.push_back(file);

            if (!ReadDirectory(file.first_cluster, file.path, depth + 1, used))
                return false;

            continue;
        }

        std::vector<u32> chain;

        if ((file.first_cluster != 0) && !ReadChain(file.first_cluster, &chain, used))
        {
            m_read_error += " in " + file.path;
            return false;
        }

        if ((u64)chain.size() * cluster_size < file.size)
        {
            m_read_error = "The size of " + file.path + " is past its clusters";
            return false;
        }

        m_files.push_back(file);
    }

    return true;
}

bool FatImage::ReadChain(u32 first, std::vector<u32>* chain, std::vector<u8>* used)
{
    u32 cluster = first;

    while (true)
    {
        if ((cluster < 2) || (cluster >= m_layout.clusters + 2))
        {
            m_read_error = "A cluster chain leaves the volume";
            return false;
        }

        if (IsValidPointer(used))
        {
            if ((*used)[cluster] != 0)
            {
                m_read_error = "A cluster chain loops or two files share a cluster";
                return false;
            }

            (*used)[cluster] = 1;
        }
        else if (chain->size() > m_layout.clusters)
        {
            m_read_error = "A cluster chain loops";
            return false;
        }

        chain->push_back(cluster);
        u16 next = m_fat[cluster];

        if (next >= 0xFFF8)
            return true;

        if ((next == 0) || (next == 0xFFF7))
        {
            m_read_error = "A cluster chain is cut";
            return false;
        }

        cluster = next;
    }
}

bool FatImage::GetFileCRC(const FatImage_File& file, u32* crc)
{
    return ReadFileData(file, NULL, NULL, crc);
}

bool FatImage::GetFileData(const FatImage_File& file, std::string* data, u32* crc)
{
    data->clear();
    return ReadFileData(file, NULL, data, crc);
}

bool FatImage::CopyOut(const FatImage_File& file, const char* path, u32* crc)
{
    MediaFile* output = MediaFile::OpenNewFile(path);
    u32 value = 0;

    if (!IsValidPointer(output))
        return false;

    bool ok = ReadFileData(file, output, NULL, &value) && output->Flush();
    SafeDelete(output);

    if (IsValidPointer(crc))
        *crc = value;

    return ok;
}

bool FatImage::ReadFileData(const FatImage_File& file, MediaFile* output, std::string* data, u32* crc)
{
    *crc = 0;

    if (!IsValidPointer(m_read) || file.directory)
        return false;

    if (file.size == 0)
        return true;

    std::vector<u32> chain;

    if (!ReadChain(file.first_cluster, &chain, NULL))
        return false;

    u32 cluster_size = m_layout.cluster_sectors * k_fat_sector_size;
    std::vector<u8> buffer(cluster_size);
    u32 left = file.size;

    for (size_t i = 0; (i < chain.size()) && (left != 0); i++)
    {
        u32 chunk = MIN(left, cluster_size);
        u64 offset = m_read_offset + ((u64)m_layout.first_data + (u64)(chain[i] - 2) * m_layout.cluster_sectors) *
            k_fat_sector_size;

        if (!m_read->ReadAt(offset, buffer.data(), chunk))
            return false;

        if (IsValidPointer(output) && !output->WriteAt((u64)file.size - left, buffer.data(), chunk))
            return false;

        if (IsValidPointer(data))
            data->append((const char*)buffer.data(), chunk);

        *crc = CalculateCRC32(*crc, buffer.data(), chunk);
        left -= chunk;
    }

    return left == 0;
}

void FatImage::Clear()
{
    if (m_zip_open)
        mz_zip_reader_end(&m_zip);

    if (m_system_zip_open)
        mz_zip_reader_end(&m_system_zip);

    if (m_ultrasound_zip_open)
        mz_zip_reader_end(&m_ultrasound_zip);

    memset(&m_zip, 0, sizeof(m_zip));
    memset(&m_system_zip, 0, sizeof(m_system_zip));
    memset(&m_ultrasound_zip, 0, sizeof(m_ultrasound_zip));
    m_zip_open = false;
    m_system_zip_open = false;
    m_ultrasound_zip_open = false;
    m_pinned.clear();
    m_pinned_count = 0;
    m_zip_prefix.clear();
    SafeDelete(m_zip_file);
    m_entries.clear();
    m_entries.resize(1);
    m_entries[0].directory = true;
    m_entries[0].size = 0;
    m_entries[0].date = k_fat_default_date;
    m_entries[0].time = k_fat_default_time;
    m_entries[0].attributes = k_fat_attribute_directory;
    m_entries[0].origin = FAT_IMAGE_ORIGIN_SOURCE;
    m_entries[0].memory = -1;
    m_entries[0].crc = 0;
    m_entries[0].modified = 0;
    m_memory.clear();
    m_base = 0;
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
        m_entries[entry].modified = modified;
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
    directories[""] = m_base;

    for (size_t i = 0; i < order.size(); i++)
    {
        mz_zip_archive_file_stat stat;

        if (!mz_zip_reader_file_stat(&m_zip, order[i].second, &stat))
            return false;

        std::string name(stat.m_filename);
        std::replace(name.begin(), name.end(), '\\', '/');

        if (!m_zip_prefix.empty())
        {
            if (upper(name).compare(0, m_zip_prefix.size(), m_zip_prefix) != 0)
                continue;

            name = name.substr(m_zip_prefix.size());
        }

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
        u32 directory = m_base;

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
        m_entries[entry].crc = stat.m_crc32;
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
    entry.attributes = is_directory ? k_fat_attribute_directory : k_fat_attribute_archive;
    entry.origin = FAT_IMAGE_ORIGIN_SOURCE;
    entry.ultrasound = false;
    entry.memory = -1;
    entry.crc = 0;
    entry.modified = 0;
    m_entries[directory].children.push_back(index);
    return index;
}

void FatImage::Finish()
{
    AssignNames(0);
    PinEntries();
    LimitPaths(0, "");
    u32 crc = CalculateCRC32(0, m_label, 11);
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

        *crc = CalculateCRC32(*crc, entry.name.data(), entry.name.size());
        *crc = CalculateCRC32(*crc, data, sizeof(data));

        // Generated and system files change with their contents and attributes, not only their size
        if (entry.origin != FAT_IMAGE_ORIGIN_SOURCE)
        {
            u8 extra[6] = { entry.attributes, entry.origin, 0, 0, 0, 0 };
            write_u32_le(extra + 2, entry.crc);
            *crc = CalculateCRC32(*crc, extra, sizeof(extra));
        }

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

// The largest clusters FAT16 allows, as DOS walks a file's chain a cluster at a time to reach an offset
bool FatImage::GetLayout(u32 total, FatImage_Layout* layout) const
{
    u32 heads = get_logical_heads(total);
    u32 reach = MIN(total, k_fat_max_cylinders * heads * k_fat_sectors_per_track);
    reach -= reach % (heads * k_fat_sectors_per_track);

    if (reach <= k_fat_sectors_per_track)
        return false;

    u32 partition = reach - k_fat_sectors_per_track;
    u32 root_entries = (u32)m_entries[0].children.size() + 1;
    root_entries = MAX(k_fat_root_entries, (root_entries + 15) & ~15U);
    u32 root_sectors = (root_entries * k_fat_entry_size) / k_fat_sector_size;
    u32 cluster_sectors = k_fat_max_cluster_sectors;
    u32 fat_sectors = 0;
    u32 first_data = 0;
    u32 clusters = 0;

    for (; cluster_sectors >= k_fat_min_cluster_sectors; cluster_sectors >>= 1)
    {
        u32 divisor = (256 * cluster_sectors) + 2;
        fat_sectors = (partition - (1 + root_sectors) + divisor - 1) / divisor;
        first_data = 1 + (2 * fat_sectors) + root_sectors;
        clusters = (partition - first_data) / cluster_sectors;

        if (clusters > k_fat_min_clusters)
            break;
    }

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
        write_entry(content->data() + ((i + own_entries) * k_fat_entry_size), entry.short_name, entry.attributes,
            entry.date, entry.time, entry.first_cluster, entry.directory ? 0 : (u32)entry.size);
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

bool FatImage::CopyFile(FatImage_Entry& entry, u64 offset)
{
    if (entry.memory >= 0)
    {
        const std::string& data = m_memory[entry.memory];
        return m_output->WriteAt(offset, data.data(), data.size());
    }

    if (entry.source.empty() && (entry.origin == FAT_IMAGE_ORIGIN_SYSTEM))
    {
        m_zip_offset = offset;
        return mz_zip_reader_extract_to_callback(entry.ultrasound ? &m_ultrasound_zip : &m_system_zip, entry.zip_index,
            WriteZipEntry, this, 0) != 0;
    }

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
    u32 crc = 0;

    while (ok && (left != 0))
    {
        u32 chunk = (u32)MIN(left, (u64)m_buffer.size());
        ok = file->ReadExact(m_buffer.data(), chunk) && m_output->WriteAt(offset, m_buffer.data(), chunk);
        crc = CalculateCRC32(crc, m_buffer.data(), chunk);
        offset += chunk;
        left -= chunk;
    }

    entry.crc = crc;

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
        memcpy(sector + k_fat_code_offset, k_dos_no_system, k_dos_no_system_size);
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
