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
#include "fat_image.h"
#include "dos_fs.h"
#include "import_common.h"
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
static const u32 k_fat_copy_size = 1024 * 1024;
static const u32 k_fat_code_offset = 0x3E;
static const u32 k_fat_entry_offset = 0x1BE;
static const char k_fat_oem[] = "GEARDOX ";
static const char k_fat_type[] = "FAT16   ";
static const char k_fat_label[] = "GEARDOX";

// SeaBIOS only names 1024 cylinders of its logical geometry, so a disk just above a head step leaves its end unreachable
static const u32 k_fat_size_gaps[2][2] = { { 1024, 1088 }, { 2048, 2112 } };

static const u32 k_fat_min_cluster_sectors = 4;
static const u32 k_fat_max_cluster_sectors = 64;

static std::string get_source_name(const char* source)
{
    return path_strip_extension(path_filename(trim_trailing_separators(source)), ".zip");
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
    InitPointer(m_boot_sector);
    InitPointer(m_read);
    m_read_offset = 0;
    memset(&m_layout, 0, sizeof(m_layout));
    m_pinned_count = 0;
    m_label[0] = 0;
    m_left_out = 0;
    m_next_cluster = 2;
    m_base = 0;
    m_layer = 0;
    m_next_source = 0;

    for (u32 i = 0; i < k_fat_open_sources; i++)
    {
        m_source_index[i] = 0;
        InitPointer(m_sources[i]);
    }

    Clear();
    SetLabel(k_fat_label);
    Finish();
}

FatImage::~FatImage()
{
    CloseRead();
    Clear();
}

bool FatImage::AddFolder(const char* path, u8 layer)
{
    m_layer = layer;
    Clear();

    if (!IsValidPointer(path) || !WalkFolder(trim_trailing_separators(path), 0, 0))
        return false;

    SetLabel(get_source_name(path).c_str());
    Finish();
    return true;
}

void FatImage::SetLayer(u8 layer)
{
    m_layer = layer;
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

        char c = to_upper_ascii(text[i]);

        if ((c == ' ') && !result.empty())
            result += c;
        else if (c != ' ')
            result += dos_is_name_character(c) ? c : '_';
    }

    memcpy(m_label, dos_fs_pad(result, 11).c_str(), 11);
    m_label[11] = 0;
}

void FatImage::SetBootSector(const u8* sector)
{
    m_boot_sector = sector;
}

u32 FatImage::GetLeftOut() const
{
    return m_left_out;
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

static bool sort_runs(const FatImage_Run& a, const FatImage_Run& b)
{
    return a.first < b.first;
}

void FatImage::SortRuns(std::vector<FatImage_Run>* runs)
{
    std::sort(runs->begin(), runs->end(), sort_runs);
}

bool FatImage::Build(u64 size, FatImage_Volume* volume)
{
    u32 total = (u32)(size / k_fat_sector_size);
    total -= total % k_fat_cylinder_sectors;

    if (!GetLayout(total, &m_layout))
    {
        Error("A FAT16 disk can't be %u sectors", total);
        return false;
    }

    if (CountClusters(0, m_layout.cluster_sectors * k_fat_sector_size) > m_layout.clusters)
    {
        Error("The files don't fit in a disk of %u sectors", total);
        return false;
    }

    volume->blocks = total;
    volume->metadata.clear();
    volume->runs.clear();
    m_fat.assign(m_layout.clusters + 2, 0);
    m_fat[0] = (u16)(0xFF00 | k_fat_media);
    m_fat[1] = k_fat_end_of_chain;
    m_next_cluster = 2;
    PlaceDirectory(0, 0, 0, volume);
    WriteFat(volume);
    WriteBootSector(volume);
    WriteMasterBootRecord(volume);
    std::vector<u16>().swap(m_fat);
    SortRuns(&volume->runs);
    return true;
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

    FatImage_Volume volume;

    if (!Build(size, &volume))
        return false;

    std::string temporary = std::string(path) + ".tmp";
    MediaFile* output = MediaFile::OpenNewFile(temporary.c_str());

    if (!IsValidPointer(output))
    {
        Error("Unable to create the hard disk image %s", temporary.c_str());
        return false;
    }

    std::vector<u8> buffer(k_fat_copy_size);
    u8 zero[k_fat_sector_size] = { };
    bool ok = output->WriteAt((u64)(volume.blocks - 1) * k_fat_sector_size, zero, k_fat_sector_size);

    for (size_t i = 0; ok && (i < volume.runs.size()); i++)
    {
        const FatImage_Run& run = volume.runs[i];
        u64 offset = (u64)run.first * k_fat_sector_size;

        if (run.entry < 0)
        {
            ok = output->WriteAt(offset, volume.metadata.data() + run.offset, (u64)run.count * k_fat_sector_size);
            continue;
        }

        u64 left = m_entries[run.entry].size;
        u64 position = 0;

        while (ok && (left != 0))
        {
            u32 chunk = (u32)MIN(left, (u64)buffer.size());
            ok = (ReadEntryAt((u32)run.entry, position, buffer.data(), chunk) == chunk) &&
                output->WriteAt(offset + position, buffer.data(), chunk);
            position += chunk;
            left -= chunk;
        }
    }

    ok = ok && output->Flush();
    SafeDelete(output);
    CloseSources();
    ok = ok && MediaFile::MoveIntoPlace(temporary.c_str(), path);

    if (!ok)
    {
        MediaFile::RemoveFile(temporary.c_str());
        Error("Unable to write the hard disk image %s", path);
    }

    return ok;
}

bool FatImage::AddGame(const char* source, const char* dir, u8 layer, u8 root_layer)
{
    m_layer = root_layer;
    Clear();

    if (!IsValidPointer(source))
        return false;

    std::vector<std::string> parts;
    split_string(IsValidPointer(dir) ? dir : "", '\\', &parts, false);

    for (size_t i = 0; i < parts.size(); i++)
        m_base = AddEntry(m_base, parts[i], true);

    m_entries[m_base].layer = layer;
    m_layer = layer;

    if (!WalkFolder(trim_trailing_separators(source), m_base, 0))
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
        std::vector<std::string> parts;
        split_string(rule.source, '/', &parts);

        for (size_t p = 0; (source >= 0) && (p < parts.size()); p++)
        {
            parent = (u32)source;
            source = FindChild(parent, parts[p]);

            if (source >= 0)
                host = !m_entries[source].host.empty() ? m_entries[source].host : (host.empty() ?
                    m_entries[source].name : host + "/" + m_entries[source].name);
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
        split_string(target_folder, '/', &parts);

        for (size_t p = 0; p < parts.size(); p++)
        {
            int found = FindChild(destination, parts[p]);

            if ((found >= 0) && !m_entries[found].directory)
            {
                RemoveChild(destination, (u32)found);
                found = -1;
            }

            destination = found >= 0 ? (u32)found : AddEntry(destination, parts[p], true);
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

bool FatImage::AddOverlay(const char* path, bool game, u8 layer)
{
    std::string root = trim_trailing_separators(path);
    std::vector<std::string> deleted;
    u32 directory = game ? m_base : 0;
    u8 previous = m_layer;
    m_layer = layer;
    bool ok = WalkOverlay(root, directory, 0) && dos_fs_read_deleted(root, &deleted);
    m_layer = previous;

    if (ok)
        ApplyDeleted(directory, deleted);

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

        if (dos_fs_is_clutter(files[i]) || !MediaFile::GetFileInfo(item.c_str(), &size, &modified))
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
        m_entries[entry].memory = -1;
        m_entries[entry].base = -1;
        m_entries[entry].layer = m_layer;
        host_time_to_dos(modified, &m_entries[entry].date, &m_entries[entry].time);
    }

    for (size_t i = 0; (depth < k_fat_max_depth) && (i < folders.size()); i++)
    {
        std::string item(path);
        append_path_component(item, folders[i].c_str());

        if (dos_fs_is_clutter(folders[i]) || MediaFile::IsFolderLink(item.c_str()))
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

void FatImage::ApplyDeleted(u32 root, const std::vector<std::string>& lines)
{
    std::vector<std::string> names;

    for (size_t l = 0; l < lines.size(); l++)
    {
        const std::string& line = lines[l];
        u32 directory = root;
        int found = -1;
        split_string(line, '/', &names, false);

        for (size_t n = 0; n < names.size(); n++)
        {
            const std::string& name = names[n];

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

bool FatImage::AddFreeDOS(bool hidden, const std::vector<std::string>& reserved)
{
    u8 hide = hidden ? k_fat_attribute_hidden : 0;
    MediaZip zip;
    LeaveOutReserved(reserved);

    if (!zip.Open(k_freedos_files, k_freedos_files_size))
        return false;

    u32 freedos = AddEntry(0, k_dos_root_freedos, true);
    m_entries[freedos].attributes = k_fat_attribute_directory | hide;
    bool ok = true;

    for (mz_uint i = 0; ok && (i < mz_zip_reader_get_num_files(zip.GetArchive())); i++)
    {
        mz_zip_archive_file_stat stat;

        if (!mz_zip_reader_file_stat(zip.GetArchive(), i, &stat) || stat.m_is_directory)
            continue;

        std::string name(stat.m_filename);
        bool kernel = name == "KERNEL.SYS";

        if (!kernel && (name.compare(0, 8, "FREEDOS/") != 0))
            continue;

        m_memory.push_back(std::string());

        if (!zip.Extract(i, &m_memory.back()))
        {
            ok = false;
            break;
        }

        std::string rest = kernel ? name : name.substr(8);
        u32 directory = AddDirectories(kernel ? 0 : freedos, &rest);
        FatImage_Entry& file = m_entries[AddEntry(directory, rest, false)];
        file.memory = (int)m_memory.size() - 1;
        file.size = m_memory.back().size();
        host_time_to_dos((s64)stat.m_time, &file.date, &file.time);

        if (kernel && hidden)
            file.attributes = k_fat_attribute_read_only | k_fat_attribute_hidden | k_fat_attribute_system;
    }

    zip.Close();
    int bin = ok ? FindChild(freedos, "BIN") : -1;
    ok = (bin >= 0) && AddDosTool((u32)bin, "GDXRUN.COM", k_dos_gdxrun, k_dos_gdxrun_size) &&
        AddDosTool((u32)bin, "GDXDBG.COM", k_dos_gdxdbg, k_dos_gdxdbg_size) &&
        AddDosTool((u32)bin, "GDXGUS.COM", k_dos_gdxgus, k_dos_gdxgus_size) &&
        AddDosTool((u32)bin, "GDXPAD.COM", k_dos_gdxpad, k_dos_gdxpad_size) &&
        AddDosTool((u32)bin, "POWEROFF.COM", k_dos_poweroff, k_dos_poweroff_size);

    if (ok)
        m_entries[AddEntry(0, k_dos_root_temp, true)].attributes = k_fat_attribute_directory | hide;

    return ok;
}

bool FatImage::AddMSDOS(const u8* zip, size_t size, bool hidden, const std::vector<std::string>& reserved,
    const std::vector<std::string>& pinned, bool shsucdx)
{
    static const char* const k_tools[] = { "CTMOUSE.EXE", "UDVD2.SYS" };
    u8 hide = hidden ? k_fat_attribute_hidden : 0;
    LeaveOutReserved(reserved);
    MediaZip system;

    if (!system.Open(zip, size))
        return false;

    u32 dos = AddEntry(0, k_dos_root_dos, true);
    m_entries[dos].attributes = k_fat_attribute_directory | hide;

    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(system.GetArchive()); i++)
    {
        mz_zip_archive_file_stat stat;

        if (!mz_zip_reader_file_stat(system.GetArchive(), i, &stat) || stat.m_is_directory)
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

        m_memory.push_back(std::string());

        if (!system.Extract(i, &m_memory.back()))
            return false;

        bool boot = (directory == 0) && (std::find(pinned.begin(), pinned.end(), to_upper_ascii(name)) != pinned.end());
        FatImage_Entry& file = m_entries[AddEntry(directory, name, false)];
        file.memory = (int)m_memory.size() - 1;
        file.size = m_memory.back().size();
        host_time_to_dos((s64)stat.m_time, &file.date, &file.time);

        if (boot)
            file.attributes = k_fat_attribute_read_only | k_fat_attribute_hidden | k_fat_attribute_system;
        else if (directory == 0)
            file.attributes = k_fat_attribute_archive | hide;
    }

    system.Close();
    MediaZip tools;

    if (!tools.Open(k_freedos_files, k_freedos_files_size))
        return false;

    u32 geardox = AddEntry(0, k_dos_root_geardox, true);
    m_entries[geardox].attributes = k_fat_attribute_directory | hide;
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

    tools.Close();

    if (!ok)
        return false;

    m_entries[AddEntry(0, k_dos_root_temp, true)].attributes = k_fat_attribute_directory | hide;
    m_pinned = pinned;
    return true;
}

bool FatImage::AddFreeDOSFile(MediaZip* zip, const char* name, u32 directory, const char* target)
{
    int index = mz_zip_reader_locate_file(zip->GetArchive(), name, NULL, 0);
    mz_zip_archive_file_stat stat;

    if ((index < 0) || !mz_zip_reader_file_stat(zip->GetArchive(), (mz_uint)index, &stat))
        return false;

    m_memory.push_back(std::string());

    if (!zip->Extract((mz_uint)index, &m_memory.back()))
        return false;

    FatImage_Entry& entry = m_entries[AddEntry(directory, target, false)];
    entry.memory = (int)m_memory.size() - 1;
    entry.size = m_memory.back().size();
    host_time_to_dos((s64)stat.m_time, &entry.date, &entry.time);
    return true;
}

bool FatImage::AddUltraSound(const u8* zip, size_t size)
{
    static const char* const k_reserved[] = { "ULTRASND" };
    std::vector<std::string> reserved(k_reserved, k_reserved + 1);
    LeaveOutReserved(reserved);
    MediaZip patches;

    if (!patches.Open(zip, size))
        return false;

    u32 root = AddEntry(0, "ULTRASND", true);

    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(patches.GetArchive()); i++)
    {
        mz_zip_archive_file_stat stat;

        if (!mz_zip_reader_file_stat(patches.GetArchive(), i, &stat) || stat.m_is_directory)
            continue;

        std::string rest(stat.m_filename);

        if (rest.empty() || (rest == k_import_version))
            continue;

        m_memory.push_back(std::string());

        if (!patches.Extract(i, &m_memory.back()))
            return false;

        u32 directory = AddDirectories(root, &rest);
        FatImage_Entry& file = m_entries[AddEntry(directory, rest, false)];
        file.memory = (int)m_memory.size() - 1;
        file.size = m_memory.back().size();
        host_time_to_dos((s64)stat.m_time, &file.date, &file.time);
    }

    return true;
}

bool FatImage::AddDosTool(u32 directory, const char* name, const u8* data, u32 size)
{
    m_memory.push_back(std::string((const char*)data, size));
    FatImage_Entry& entry = m_entries[AddEntry(directory, name, false)];
    entry.memory = (int)m_memory.size() - 1;
    entry.size = size;
    entry.date = k_dos_tools_date;
    entry.time = k_dos_tools_time;
    return true;
}

u32 FatImage::AddDirectories(u32 directory, std::string* rest)
{
    size_t slash = rest->find('/');

    while (slash != std::string::npos)
    {
        int found = FindChild(directory, rest->substr(0, slash));
        directory = found >= 0 ? (u32)found : AddEntry(directory, rest->substr(0, slash), true);
        *rest = rest->substr(slash + 1);
        slash = rest->find('/');
    }

    return directory;
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
            if (to_upper_ascii(m_entries[children[i]].name) == m_pinned[p])
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
        std::string name = to_upper_ascii(m_entries[child].name);

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
    LeaveOutMatching(0, to_upper_ascii(name), match);
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

        if ((to_upper_ascii(entry.name) == name) && ReadEntry(entry, &data) && match(data))
            RemoveChild(directory, child);
    }
}

bool FatImage::AddMemoryFile(const char* path, const std::string& data, u8 attributes, const std::string* base)
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
    m_entries[entry].attributes = attributes;

    if (IsValidPointer(base))
    {
        m_memory.push_back(*base);
        m_entries[entry].base = (int)m_memory.size() - 1;
    }

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
    entry.source.clear();
    return true;
}

void FatImage::FinishGame(const char* label, const u8* boot_sector)
{
    SetLabel(label);
    SetBootSector(boot_sector);
    Finish();
}

const FatImage_Entry& FatImage::GetEntry(u32 index) const
{
    return m_entries[index];
}

u32 FatImage::GetBaseIndex() const
{
    return m_base;
}

const std::string& FatImage::GetBase(u32 index) const
{
    static const std::string k_none;
    return m_entries[index].base >= 0 ? m_memory[m_entries[index].base] : k_none;
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
        std::string name = dos_fs_entry_name(entry.short_name);
        item.dos_path = dos_fs_join(dos_path, name);
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
    std::string wanted = to_upper_ascii(name);

    for (size_t i = 0; i < m_entries[directory].children.size(); i++)
    {
        u32 child = m_entries[directory].children[i];

        if (to_upper_ascii(m_entries[child].name) == wanted)
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

    MediaFile* file = MediaFile::OpenFile(entry.source.c_str());

    if (!IsValidPointer(file))
        return false;

    data->resize((size_t)entry.size);
    bool ok = data->empty() || file->ReadExact(&(*data)[0], entry.size);
    SafeDelete(file);
    return ok;
}

u32 FatImage::ReadEntryAt(u32 index, u64 offset, u8* buffer, u32 size)
{
    const FatImage_Entry& entry = m_entries[index];

    if (offset >= entry.size)
        return 0;

    u32 count = (u32)MIN((u64)size, entry.size - offset);

    if (entry.memory >= 0)
    {
        memcpy(buffer, m_memory[entry.memory].data() + offset, count);
        return count;
    }

    MediaFile* file = OpenSource(index);
    return (IsValidPointer(file) && file->ReadAt(offset, buffer, count)) ? count : 0;
}

MediaFile* FatImage::OpenSource(u32 index)
{
    for (u32 i = 0; i < k_fat_open_sources; i++)
    {
        if (IsValidPointer(m_sources[i]) && (m_source_index[i] == index))
            return m_sources[i];
    }

    u32 slot = m_next_source;
    m_next_source = (m_next_source + 1) % k_fat_open_sources;
    SafeDelete(m_sources[slot]);
    m_sources[slot] = MediaFile::OpenFile(m_entries[index].source.c_str());
    m_source_index[slot] = index;
    return m_sources[slot];
}

void FatImage::CloseSources()
{
    for (u32 i = 0; i < k_fat_open_sources; i++)
        SafeDelete(m_sources[i]);
}

void FatImage::RemoveChild(u32 directory, u32 index)
{
    std::vector<u32>& children = m_entries[directory].children;
    children.erase(std::remove(children.begin(), children.end(), index), children.end());
}

bool FatImage::Read(MediaFile* file)
{
    CloseRead();
    m_read = file;
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
    InitPointer(m_read);
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
        file.path = dos_fs_join(path, dos_fs_entry_name(name));
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

bool FatImage::CopyOut(const FatImage_File& file, const char* path)
{
    std::vector<u32> chain;

    if (!IsValidPointer(m_read) || file.directory || ((file.size != 0) && !ReadChain(file.first_cluster, &chain, NULL)))
        return false;

    MediaFile* output = MediaFile::OpenNewFile(path);

    if (!IsValidPointer(output))
        return false;

    u32 cluster_size = m_layout.cluster_sectors * k_fat_sector_size;
    std::vector<u8> buffer(cluster_size);
    u32 left = file.size;
    bool ok = true;

    for (size_t i = 0; ok && (i < chain.size()) && (left != 0); i++)
    {
        u32 chunk = MIN(left, cluster_size);
        u64 offset = m_read_offset + ((u64)m_layout.first_data + (u64)(chain[i] - 2) * m_layout.cluster_sectors) *
            k_fat_sector_size;
        ok = m_read->ReadAt(offset, buffer.data(), chunk) && output->WriteAt((u64)file.size - left, buffer.data(), chunk);
        left -= chunk;
    }

    ok = ok && (left == 0) && output->Flush();
    SafeDelete(output);
    return ok;
}

void FatImage::Clear()
{
    CloseSources();
    m_pinned.clear();
    m_pinned_count = 0;
    m_entries.clear();
    m_entries.resize(1);
    m_entries[0].directory = true;
    m_entries[0].size = 0;
    m_entries[0].date = k_dos_default_date;
    m_entries[0].time = k_dos_default_time;
    m_entries[0].first_cluster = 0;
    m_entries[0].attributes = k_fat_attribute_directory;
    m_entries[0].layer = m_layer;
    m_entries[0].memory = -1;
    m_entries[0].base = -1;
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

        if (dos_fs_is_clutter(files[i]))
            continue;

        if ((m_entries.size() >= k_fat_max_entries) || !MediaFile::GetFileInfo(item.c_str(), &size, &modified) ||
            (size > k_fat_max_file_size))
        {
            m_left_out++;
            continue;
        }

        u32 entry = AddEntry(directory, files[i], false);
        m_entries[entry].source = item;
        m_entries[entry].size = size;
        host_time_to_dos(modified, &m_entries[entry].date, &m_entries[entry].time);
    }

    for (size_t i = 0; (depth < k_fat_max_depth) && (i < folders.size()); i++)
    {
        std::string item(path);
        append_path_component(item, folders[i].c_str());
        u64 size = 0;
        s64 modified = 0;

        if (dos_fs_is_clutter(folders[i]) || MediaFile::IsFolderLink(item.c_str()))
            continue;

        if (m_entries.size() >= k_fat_max_entries)
        {
            m_left_out++;
            continue;
        }

        u32 entry = AddEntry(directory, folders[i], true);

        if (MediaFile::GetFileInfo(item.c_str(), &size, &modified))
            host_time_to_dos(modified, &m_entries[entry].date, &m_entries[entry].time);

        if (!WalkFolder(item, entry, depth + 1))
            m_left_out++;
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
    entry.date = k_dos_default_date;
    entry.time = k_dos_default_time;
    entry.first_cluster = 0;
    entry.attributes = is_directory ? k_fat_attribute_directory : k_fat_attribute_archive;
    entry.layer = m_layer;
    entry.memory = -1;
    entry.base = -1;
    m_entries[directory].children.push_back(index);
    return index;
}

void FatImage::Finish()
{
    AssignNames(0);
    PinEntries();
    LimitPaths(0, "");
}

void FatImage::AssignNames(u32 directory)
{
    std::vector<u32>& children = m_entries[directory].children;
    std::vector<std::string> names;
    std::vector<bool> folders;
    std::vector<std::string> short_names;

    for (size_t i = 0; i < children.size(); i++)
    {
        names.push_back(m_entries[children[i]].name);
        folders.push_back(m_entries[children[i]].directory);
    }

    dos_fs_assign_names(names, folders, k_dos_fat_names, &short_names);
    std::vector<std::pair<std::string, u32> > sorted;

    for (size_t i = 0; i < children.size(); i++)
    {
        FatImage_Entry& entry = m_entries[children[i]];
        memcpy(entry.short_name, short_names[i].data(), sizeof(entry.short_name));
        sorted.push_back(std::make_pair(short_names[i], children[i]));

        if (entry.directory)
            AssignNames(children[i]);
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
        std::string name = dos_fs_entry_name(entry.short_name);
        std::string item = dos_fs_join(path, name);

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

void FatImage::PlaceDirectory(u32 directory, u32 cluster, u32 parent, FatImage_Volume* volume)
{
    u32 cluster_size = m_layout.cluster_sectors * k_fat_sector_size;
    const std::vector<u32> children = m_entries[directory].children;
    const FatImage_Entry& self = m_entries[directory];
    bool root = cluster == 0;
    size_t used = (children.size() + (root ? 1 : 2)) * k_fat_entry_size;
    std::vector<u8> content(root ? (size_t)m_layout.root_sectors * k_fat_sector_size :
        ((used + cluster_size - 1) / cluster_size) * cluster_size, 0);

    if (root)
        write_entry(content.data() + ((size_t)m_pinned_count * k_fat_entry_size), (const u8*)m_label, k_fat_attribute_label,
            k_dos_default_date, k_dos_default_time, 0, 0);
    else
    {
        u8 dot[11];
        memset(dot, ' ', sizeof(dot));
        dot[0] = '.';
        write_entry(content.data(), dot, k_fat_attribute_directory, self.date, self.time, cluster, 0);
        dot[1] = '.';
        write_entry(content.data() + k_fat_entry_size, dot, k_fat_attribute_directory, self.date, self.time, parent, 0);
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
        FatImage_Run run = { GetClusterSector(entry.first_cluster),
            (u32)((entry.size + k_fat_sector_size - 1) / k_fat_sector_size), (int)children[i], 0 };
        volume->runs.push_back(run);
    }

    for (size_t i = 0; i < children.size(); i++)
    {
        const FatImage_Entry& entry = m_entries[children[i]];
        size_t slot = root ? (i < m_pinned_count ? i : i + 1) : i + 2;
        write_entry(content.data() + (slot * k_fat_entry_size), entry.short_name, entry.attributes, entry.date,
            entry.time, entry.first_cluster, entry.directory ? 0 : (u32)entry.size);
    }

    AddMetadata(volume, root ? k_fat_sectors_per_track + 1 + (2 * m_layout.fat_sectors) : GetClusterSector(cluster),
        content.data(), content.size());

    for (size_t i = 0; i < children.size(); i++)
    {
        const FatImage_Entry& entry = m_entries[children[i]];

        if (entry.directory)
            PlaceDirectory(children[i], entry.first_cluster, cluster, volume);
    }
}

u32 FatImage::GetClusterSector(u32 cluster) const
{
    return k_fat_sectors_per_track + m_layout.first_data + ((cluster - 2) * m_layout.cluster_sectors);
}

void FatImage::AddMetadata(FatImage_Volume* volume, u32 first, const u8* data, size_t size)
{
    FatImage_Run run = { first, (u32)(size / k_fat_sector_size), -1, (u64)volume->metadata.size() };
    volume->metadata.insert(volume->metadata.end(), data, data + size);
    volume->runs.push_back(run);
}

void FatImage::WriteFat(FatImage_Volume* volume)
{
    std::vector<u8> table((size_t)m_layout.fat_sectors * k_fat_sector_size, 0);

    for (size_t i = 0; i < m_fat.size(); i++)
        write_u16_le(table.data() + (i * 2), m_fat[i]);

    for (u32 i = 0; i < 2; i++)
        AddMetadata(volume, k_fat_sectors_per_track + 1 + (i * m_layout.fat_sectors), table.data(), table.size());
}

void FatImage::WriteBootSector(FatImage_Volume* volume)
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
    write_u32_le(sector + 0x27, CalculateCRC32(partition, m_label, 11));
    memcpy(sector + 0x2B, m_label, 11);
    memcpy(sector + 0x36, k_fat_type, 8);
    AddMetadata(volume, k_fat_sectors_per_track, sector, sizeof(sector));
}

void FatImage::WriteMasterBootRecord(FatImage_Volume* volume)
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
    AddMetadata(volume, 0, sector, sizeof(sector));
}
