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

#include <stdarg.h>
#include <algorithm>
#include <set>
#include "folder_disk.h"
#include "dos_fs.h"
#include "../media/media_file.h"
#include "../media/media_zip.h"
#include "../media/crc.h"
#include "../common/process_utils.h"

static const u32 k_folder_disk_sector = 512;
static const u32 k_folder_disk_slot = 4 + k_folder_disk_sector;
static const u32 k_folder_disk_layout_magic = 0x4C584447;
static const u32 k_folder_disk_layout_version = 2;
static const u32 k_folder_disk_layout_header = 36;
static const char k_folder_disk_folder[] = "Disk";
static const char k_folder_disk_changes[] = "Changes";
static const char k_folder_disk_layout[] = "Layout";
static const char k_folder_disk_lock[] = "Lock";
static const char k_folder_disk_saves[] = "Saves";
static const char k_folder_disk_conflicts[] = "Conflicts";
static const char k_folder_disk_base[] = ".base";

struct FolderDisk_Write
{
    size_t file;
    FolderDisk_Item item;
    std::string target;
};

class FolderDiskView : public MediaFile
{
public:
    FolderDiskView(FolderDisk* disk, const FatImage_Volume* volume, bool changes);
    bool Open(const char* path, bool writable);
    bool Create(const char* path);
    void Close();
    bool IsOpen() const;
    bool IsValid() const;
    bool CanSeek() const;
    s64 GetSize();
    s64 Tell();
    bool Seek(s64 offset);
    s64 Read(void* buffer, u64 size);
    s64 Write(const void* buffer, u64 size);
    bool Flush();

private:
    FolderDisk* m_disk;
    const FatImage_Volume* m_volume;
    bool m_changes;
    s64 m_position;
};

FolderDiskView::FolderDiskView(FolderDisk* disk, const FatImage_Volume* volume, bool changes)
{
    m_disk = disk;
    m_volume = volume;
    m_changes = changes;
    m_position = 0;
}

bool FolderDiskView::Open(const char* path, bool writable)
{
    UNUSED(path);
    UNUSED(writable);
    return false;
}

bool FolderDiskView::Create(const char* path)
{
    UNUSED(path);
    return false;
}

void FolderDiskView::Close()
{
}

bool FolderDiskView::IsOpen() const
{
    return true;
}

bool FolderDiskView::IsValid() const
{
    return true;
}

bool FolderDiskView::CanSeek() const
{
    return true;
}

s64 FolderDiskView::GetSize()
{
    return (s64)m_volume->blocks * k_folder_disk_sector;
}

s64 FolderDiskView::Tell()
{
    return m_position;
}

bool FolderDiskView::Seek(s64 offset)
{
    m_position = offset;
    return offset >= 0;
}

s64 FolderDiskView::Read(void* buffer, u64 size)
{
    u64 end = (u64)m_volume->blocks * k_folder_disk_sector;

    if ((m_position < 0) || ((u64)m_position >= end))
        return 0;

    size = MIN(size, end - (u64)m_position);
    u8* out = (u8*)buffer;
    u8 sector[k_folder_disk_sector];
    u64 done = 0;

    while (done < size)
    {
        u64 offset = (u64)m_position + done;
        u32 lba = (u32)(offset / k_folder_disk_sector);
        u32 skip = (u32)(offset % k_folder_disk_sector);

        if ((skip == 0) && ((size - done) >= k_folder_disk_sector))
        {
            u32 count = (u32)MIN((size - done) / k_folder_disk_sector, (u64)256);

            if (!m_disk->ReadVolume(*m_volume, lba, out + done, count, m_changes))
                return -1;

            done += (u64)count * k_folder_disk_sector;
            continue;
        }

        if (!m_disk->ReadVolume(*m_volume, lba, sector, 1, m_changes))
            return -1;

        u32 chunk = (u32)MIN((u64)(k_folder_disk_sector - skip), size - done);
        memcpy(out + done, sector + skip, chunk);
        done += chunk;
    }

    m_position += (s64)size;
    return (s64)size;
}

s64 FolderDiskView::Write(const void* buffer, u64 size)
{
    UNUSED(buffer);
    UNUSED(size);
    return -1;
}

bool FolderDiskView::Flush()
{
    return true;
}

static bool unlist(std::vector<std::string>* list, const std::string& path)
{
    bool found = false;

    for (size_t i = list->size(); i > 0; i--)
    {
        if (strings_equal_ignore_case((*list)[i - 1], path))
        {
            list->erase(list->begin() + (i - 1));
            found = true;
        }
    }

    return found;
}

static FatImage* build_folder_tree(const std::string& base, const std::string& saves, const std::string& label)
{
    FatImage* tree = new FatImage();

    if (!tree->AddFolder(base.c_str(), 0) ||
        (!saves.empty() && MediaFile::IsDirectory(saves.c_str()) && !tree->AddOverlay(saves.c_str(), false, 0)))
    {
        SafeDelete(tree);
        return NULL;
    }

    tree->FinishGame(label.c_str(), NULL);
    return tree;
}

FolderDisk::FolderDisk()
{
    InitPointer(m_tree);
    InitPointer(m_changes);
    m_end_session = 0;
    m_end_write = 0;
    m_locked = false;
    m_quarantine = false;
    m_resumed = true;
    m_volume.blocks = 0;
    m_recovery = FolderDisk_Result();
}

FolderDisk::~FolderDisk()
{
    Clear();
}

FolderDisk* FolderDisk::OpenSource(const char* source, const char* save_folder, const char* temp_folder,
    std::string* error)
{
    std::string path = trim_trailing_separators(source);
    bool zip = ends_with_no_case(path.c_str(), ".zip");
    std::string name = path_strip_extension(path_filename(path), ".zip");
    std::string key = crc_key(path + "|disk");
    std::string data = GetDataFolder(save_folder, name, key);
    std::string base = path;
    FolderDisk* disk = new FolderDisk();

    if (zip)
    {
        base = path_join(temp_folder, safe_file_name(name, "Disk") + "." + key);
        MediaFile::RemoveTree(base.c_str());
        MediaZip archive;

        if (!archive.Open(path.c_str()) || !archive.ExtractAll(base.c_str()))
        {
            *error = "Unable to unpack " + path;
            MediaFile::RemoveTree(base.c_str());
            SafeDelete(disk);
            return NULL;
        }
    }

    bool overlay = zip || !IsWritable(path);
    std::string saves = overlay ? path_join(data, k_folder_disk_saves) : std::string();
    std::vector<FolderDisk_Layer> layers(1);
    layers[0].folder = overlay ? saves : path;
    layers[0].deletions = overlay;
    bool ok = true;

    if (HasSession(data))
    {
        FatImage* tree = build_folder_tree(base, saves, name);
        ok = IsValidPointer(tree) && disk->Recover(tree, layers, data);
    }

    FatImage* tree = ok ? build_folder_tree(base, saves, name) : NULL;
    ok = ok && IsValidPointer(tree) && disk->Open(tree, layers, data, k_fat_free_size, path);

    if (!ok)
    {
        *error = disk->GetError()[0] != 0 ? std::string(disk->GetError()) : "Unable to read " + path;

        if (zip)
            MediaFile::RemoveTree(base.c_str());

        SafeDelete(disk);
        return NULL;
    }

    if (zip)
        disk->SetTemporary(base);

    return disk;
}

std::string FolderDisk::GetDataFolder(const char* save_folder, const std::string& name, const std::string& key)
{
    return path_join(save_folder, safe_file_name(name, "Disk") + "." + key);
}

bool FolderDisk::IsWritable(const std::string& folder)
{
    std::string probe = path_join(folder, ".geardox_write_test");
    MediaFile* file = MediaFile::OpenNewFile(probe.c_str());
    bool writable = IsValidPointer(file) && file->WriteAt(0, "x", 1) && file->Flush();
    SafeDelete(file);
    MediaFile::RemoveFile(probe.c_str());
    return writable;
}

bool FolderDisk::HasSession(const std::string& data_folder)
{
    std::string path = path_join(path_join(data_folder, k_folder_disk_folder), k_folder_disk_changes);
    return MediaFile::Exists(path.c_str());
}

bool FolderDisk::Recover(FatImage* tree, const std::vector<FolderDisk_Layer>& layers, const std::string& data_folder)
{
    Clear();
    m_tree = tree;
    m_layers = layers;
    m_data = data_folder;

    if (!TakeLock())
    {
        Fail("This disk is open in another Geardox: %s", GetLockPath().c_str());
        Clear();
        return false;
    }

    Log("The last session of %s didn't end, writing it back", m_data.c_str());
    FolderDisk_Result recovery = FolderDisk_Result();
    AddItems();
    bool ok = LoadLayout(false) && LoadChanges();

    if (!ok)
    {
        recovery.refused = true;
        recovery.notes.push_back("The last session's layout can't be read: " + GetLayoutPath());
    }
    else
        ok = WriteBack(0, 0, &recovery);

    SafeDelete(m_changes);
    std::string changes = GetChangesPath();

    if (recovery.refused)
    {
        MediaFile::RemoveFile((changes + ".refused").c_str());
        MediaFile::RenameFile(changes.c_str(), (changes + ".refused").c_str());
        Log("The last session of %s doesn't add up, its changes are kept as %s.refused", m_data.c_str(), changes.c_str());
    }
    else if (ok)
        MediaFile::RemoveFile(changes.c_str());
    else
        Fail("The last session's changes can't all be written back: %s",
            recovery.notes.empty() ? changes.c_str() : recovery.notes.back().c_str());

    std::string error = m_error;
    Clear();
    m_recovery = recovery;
    m_error = error;
    return ok || recovery.refused;
}

bool FolderDisk::Open(FatImage* tree, const std::vector<FolderDisk_Layer>& layers, const std::string& data_folder,
    u64 free_size, const std::string& path)
{
    FolderDisk_Result recovery = m_recovery;
    Clear();
    m_recovery = recovery;
    m_tree = tree;
    m_layers = layers;
    m_data = data_folder;
    m_path = path;

    if (!MediaFile::MakeDirectories(path_join(m_data, k_folder_disk_folder).c_str()))
        return Fail("Unable to make the folder %s", path_join(m_data, k_folder_disk_folder).c_str());

    if (!TakeLock())
        return Fail("This disk is open in another Geardox: %s", GetLockPath().c_str());

    if (HasSession(m_data))
        return Fail("The last session's changes weren't written back: %s", GetChangesPath().c_str());

    AddItems();

    if (LoadLayout(true))
        return true;

    u64 size = m_tree->GetRequiredSize(free_size);

    if (size == 0)
        return Fail("The files don't fit in a FAT16 disk of 2015 MB: %s", m_path.c_str());

    if (!m_tree->Build(size, &m_volume))
        return Fail("Unable to lay out the disk of %s", m_path.c_str());

    AddItems();
    m_end_session = 0;
    m_end_write = 0;
    FolderDiskView view(this, &m_volume, false);
    FatImage disk;

    if (!disk.Read(&view) || !SaveLayout(disk, 0, 0))
        return Fail("Unable to write %s", GetLayoutPath().c_str());

    return true;
}

void FolderDisk::SetTemporary(const std::string& folder)
{
    m_temporary = folder;
}

bool FolderDisk::Close(u64 session, u64 write, bool resumed, FolderDisk_Result* result)
{
    *result = FolderDisk_Result();
    m_resumed = resumed;

    if (!IsValidPointer(m_tree))
        return true;

    bool changed = IsValidPointer(m_changes) && !m_slots.empty();
    bool ok = true;

    if (changed || m_quarantine)
        ok = WriteBack(session, write, result);
    else
    {
        FolderDiskView view(this, &m_volume, false);
        FatImage parser;
        bool same = (session == m_end_session) && (write == m_end_write);
        ok = same || !parser.Read(&view) || SaveLayout(parser, session, write);
        result->done = ok;
    }

    SafeDelete(m_changes);
    std::string changes = GetChangesPath();

    if (result->refused)
    {
        MediaFile::RemoveFile((changes + ".refused").c_str());
        MediaFile::RenameFile(changes.c_str(), (changes + ".refused").c_str());
    }
    else if (ok)
        MediaFile::RemoveFile(changes.c_str());
    else
        Log("The changes of %s stay in %s, the next launch writes them back", m_path.c_str(), changes.c_str());

    Clear();
    return ok;
}

void FolderDisk::Discard()
{
    Clear();
}

void FolderDisk::SetQuarantine()
{
    if (m_quarantine)
        return;

    m_quarantine = true;
    Log("A state %s can't go back to was loaded, its changes go to %s", m_path.c_str(), GetConflictsFolder().c_str());
}

bool FolderDisk::IsQuarantined() const
{
    return m_quarantine;
}

bool FolderDisk::GetEndMark(u64* session, u64* write) const
{
    *session = m_end_session;
    *write = m_end_write;
    return m_end_session != 0;
}

const char* FolderDisk::GetPath() const
{
    return m_path.c_str();
}

const char* FolderDisk::GetDataFolder() const
{
    return m_data.c_str();
}

std::string FolderDisk::GetConflictsFolder() const
{
    return path_join(m_data, k_folder_disk_conflicts);
}

const FolderDisk_Result& FolderDisk::GetRecovery() const
{
    return m_recovery;
}

const char* FolderDisk::GetError() const
{
    return m_error.c_str();
}

u32 FolderDisk::GetLeftOut() const
{
    return IsValidPointer(m_tree) ? m_tree->GetLeftOut() : 0;
}

std::string FolderDisk::GetHostPath(const std::string& dos_path) const
{
    std::map<std::string, FolderDisk_Item>::const_iterator found = m_items.find(to_upper_ascii(dos_path));

    if ((found == m_items.end()) || (found->second.layer >= m_layers.size()))
        return std::string();

    if ((found->second.entry >= 0) && !m_tree->GetEntry((u32)found->second.entry).source.empty())
        return m_tree->GetEntry((u32)found->second.entry).source;

    return path_join(m_layers[found->second.layer].folder, found->second.host);
}

u32 FolderDisk::GetBlocks() const
{
    return m_volume.blocks;
}

bool FolderDisk::Read(u32 lba, u8* buffer, u32 count)
{
    if (!IsValidPointer(m_tree) || ((u64)lba + count > m_volume.blocks))
        return false;

    return ReadVolume(m_volume, lba, buffer, count, true);
}

bool FolderDisk::Store(u32 lba, const u8* buffer)
{
    if (!IsValidPointer(m_changes))
    {
        m_changes = MediaFile::OpenNewFile(GetChangesPath().c_str());

        if (!IsValidPointer(m_changes))
            return false;
    }

    std::map<u32, u32>::iterator found = m_slots.find(lba);

    if (found != m_slots.end())
        return m_changes->WriteAt((u64)found->second * k_folder_disk_slot + 4, buffer, k_folder_disk_sector);

    u32 slot = (u32)m_slots.size();
    u8 record[k_folder_disk_slot];
    write_u32_le(record, lba);
    memcpy(record + 4, buffer, k_folder_disk_sector);

    if (!m_changes->WriteAt((u64)slot * k_folder_disk_slot, record, sizeof(record)))
        return false;

    m_slots[lba] = slot;
    return true;
}

bool FolderDisk::Sync()
{
    return !IsValidPointer(m_changes) || m_changes->Flush();
}

bool FolderDisk::ReadVolume(const FatImage_Volume& volume, u32 lba, u8* buffer, u32 count, bool changes)
{
    bool ok = true;
    memset(buffer, 0, (size_t)count * k_folder_disk_sector);
    const std::vector<FatImage_Run>& runs = volume.runs;
    u32 end = lba + count;
    size_t low = 0;
    size_t high = runs.size();

    while (low < high)
    {
        size_t middle = (low + high) / 2;

        if (runs[middle].first + runs[middle].count <= lba)
            low = middle + 1;
        else
            high = middle;
    }

    for (size_t i = low; (i < runs.size()) && (runs[i].first < end); i++)
    {
        const FatImage_Run& run = runs[i];
        u32 first = MAX(lba, run.first);
        u32 last = MIN(end, run.first + run.count);

        if (first >= last)
            continue;

        u8* out = buffer + ((size_t)(first - lba) * k_folder_disk_sector);
        u64 offset = run.offset + ((u64)(first - run.first) * k_folder_disk_sector);
        u32 size = (last - first) * k_folder_disk_sector;

        if (run.entry < 0)
            memcpy(out, volume.metadata.data() + offset, size);
        else if (IsValidPointer(m_tree) && !m_tree->ReadEntryAt((u32)run.entry, offset, out, size))
        {
            for (u32 j = first; j < last; j++)
            {
                u32 skip = (j - first) * k_folder_disk_sector;

                if (!changes || (m_slots.find(j) == m_slots.end()))
                    ok = m_tree->ReadEntryAt((u32)run.entry, offset + skip, out + skip, k_folder_disk_sector) && ok;
            }
        }
    }

    if (!changes)
        return ok;

    for (std::map<u32, FolderDisk_Rest>::const_iterator it = m_rest.lower_bound(lba); (it != m_rest.end()) &&
        (it->first < end); ++it)
    {
        memcpy(buffer + ((size_t)(it->first - lba) * k_folder_disk_sector) + it->second.skip,
            m_volume.metadata.data() + it->second.offset + it->second.skip, k_folder_disk_sector - it->second.skip);
    }

    if (!IsValidPointer(m_changes))
        return ok;

    for (std::map<u32, u32>::const_iterator it = m_slots.lower_bound(lba); (it != m_slots.end()) && (it->first < end); ++it)
    {
        ok = m_changes->ReadAt(((u64)it->second * k_folder_disk_slot) + 4,
            buffer + ((size_t)(it->first - lba) * k_folder_disk_sector), k_folder_disk_sector) && ok;
    }

    return ok;
}

bool FolderDisk::Fail(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    m_error = vformat_string(format, args);
    va_end(args);
    Log("%s", m_error.c_str());
    return false;
}

bool FolderDisk::TakeLock()
{
    std::string path = GetLockPath();
    std::string text;
    unsigned long self = get_process_id();

    if (MediaFile::ReadFile(path.c_str(), k_media_file_no_limit, &text, true))
    {
        unsigned long owner = strtoul(text.c_str(), NULL, 10);

        if ((owner != self) && is_process_running(owner))
            return false;
    }

    std::string pid = format_string("%lu\n", self);
    m_locked = MediaFile::MakeDirectories(path_directory(path).c_str()) &&
        MediaFile::WriteFileAtomic(path.c_str(), pid.data(), pid.size());
    return m_locked;
}

void FolderDisk::ReleaseLock()
{
    if (m_locked)
        MediaFile::RemoveFile(GetLockPath().c_str());

    m_locked = false;
}

void FolderDisk::Clear()
{
    SafeDelete(m_changes);
    m_slots.clear();
    m_rest.clear();
    ReleaseLock();

    if (!m_temporary.empty())
        MediaFile::RemoveTree(m_temporary.c_str());

    m_temporary.clear();
    SafeDelete(m_tree);
    m_layers.clear();
    m_data.clear();
    m_path.clear();
    m_volume = FatImage_Volume();
    m_volume.blocks = 0;
    m_items.clear();
    m_end_session = 0;
    m_end_write = 0;
    m_quarantine = false;
    m_resumed = true;
    m_recovery = FolderDisk_Result();
    m_error.clear();
}

void FolderDisk::AddItems()
{
    std::vector<FatImage_Listed> listed;
    m_tree->ListEntries(&listed);
    m_items.clear();
    FolderDisk_Item root = FolderDisk_Item();
    root.entry = 0;
    root.directory = true;
    root.layer = m_tree->GetEntry(0).layer;
    m_items[""] = root;

    for (size_t i = 0; i < listed.size(); i++)
    {
        const FatImage_Entry& entry = m_tree->GetEntry(listed[i].index);
        bool base = listed[i].index == m_tree->GetBaseIndex();
        FolderDisk_Item item = FolderDisk_Item();
        item.entry = (int)listed[i].index;
        item.directory = entry.directory;
        item.size = (u32)entry.size;
        item.date = entry.date;
        item.time = entry.time;
        item.first_cluster = entry.first_cluster;
        item.layer = entry.layer;
        item.host = base ? std::string() : listed[i].host_path;
        item.children = item.host;
        m_items[to_upper_ascii(listed[i].dos_path)] = item;
    }
}

bool FolderDisk::LoadLayout(bool strict)
{
    std::string data;

    if (!MediaFile::ReadFile(GetLayoutPath().c_str(), k_media_file_no_limit, &data) ||
        (data.size() < k_folder_disk_layout_header))
        return false;

    const u8* header = (const u8*)data.data();

    if ((read_u32_le(header) != k_folder_disk_layout_magic) || (read_u32_le(header + 4) != k_folder_disk_layout_version))
        return false;

    u32 blocks = read_u32_le(header + 8);
    mz_ulong raw_size = read_u32_le(header + 12);
    u32 packed_size = read_u32_le(header + 16);
    u64 session = read_u64_le(header + 20);
    u64 write = read_u64_le(header + 28);

    if (packed_size > data.size() - k_folder_disk_layout_header)
        return false;

    std::vector<u8> raw((size_t)raw_size);

    if ((mz_uncompress(raw.data(), &raw_size, header + k_folder_disk_layout_header, packed_size) != MZ_OK) ||
        (raw_size != raw.size()))
        return false;

    FatImage_Volume volume;
    volume.blocks = blocks;
    std::map<u32, FolderDisk_Rest> rest;
    bool metadata = true;
    size_t offset = 0;

    while (offset + 8 <= raw.size())
    {
        u32 lba = read_u32_le(raw.data() + offset);
        u32 count = read_u32_le(raw.data() + offset + 4);
        size_t size = (size_t)count * k_folder_disk_sector;
        offset += 8;

        if ((size > raw.size() - offset) || ((u64)lba + count > blocks))
            return false;

        if (count == 0)
            metadata = false;

        if (metadata)
        {
            FatImage_Run run = { lba, count, -1, (u64)volume.metadata.size() };
            volume.runs.push_back(run);
        }

        for (u32 i = 0; !metadata && (i < count); i++)
        {
            FolderDisk_Rest sector = { volume.metadata.size() + ((u64)i * k_folder_disk_sector), 0 };
            rest[lba + i] = sector;
        }

        volume.metadata.insert(volume.metadata.end(), raw.begin() + offset, raw.begin() + offset + size);
        offset += size;
    }

    FatImage::SortRuns(&volume.runs);
    FolderDiskView view(this, &volume, false);
    FatImage parser;

    if (!parser.Read(&view))
        return false;

    const std::vector<FatImage_File>& files = parser.GetFiles();

    if (strict && (files.size() + 1 != m_items.size()))
        return false;

    std::map<std::string, FolderDisk_Item> items = m_items;

    for (size_t i = 0; i < files.size(); i++)
    {
        std::map<std::string, FolderDisk_Item>::iterator found = items.find(files[i].path);
        bool known = (found != items.end()) && (found->second.directory == files[i].directory) &&
            (files[i].directory || (found->second.size == files[i].size));

        if (strict && !known)
            return false;

        if (found == items.end())
        {
            FolderDisk_Item item = FolderDisk_Item();

            if (!Route(files[i].path, &items, &item))
                continue;

            found = items.insert(std::make_pair(files[i].path, item)).first;
        }

        found->second.directory = files[i].directory;
        found->second.size = files[i].size;
        found->second.date = files[i].date;
        found->second.time = files[i].time;
        found->second.first_cluster = files[i].first_cluster;
    }

    std::vector<FatImage_Extent> extents;
    parser.GetSectorMap(&extents, true);
    int file = -1;
    u64 consumed = 0;

    for (size_t i = 0; i < extents.size(); i++)
    {
        const FatImage_Extent& extent = extents[i];

        if ((extent.file < 0) || files[extent.file].directory)
            continue;

        if (extent.file != file)
        {
            file = extent.file;
            consumed = 0;
        }

        std::map<std::string, FolderDisk_Item>::const_iterator found = items.find(files[extent.file].path);
        u32 count = extent.last - extent.first + 1;

        if ((found != items.end()) && (found->second.entry > 0))
        {
            FatImage_Run run = { extent.first, count, found->second.entry, consumed };
            volume.runs.push_back(run);
        }

        u64 length = files[extent.file].size;
        std::map<u32, FolderDisk_Rest>::iterator tail = length > consumed ?
            rest.find(extent.first + (u32)((length - consumed) / k_folder_disk_sector)) : rest.end();

        if ((tail != rest.end()) && (tail->first <= extent.last))
            tail->second.skip = (u32)(length % k_folder_disk_sector);

        consumed += (u64)count * k_folder_disk_sector;
    }

    FatImage::SortRuns(&volume.runs);
    m_volume = volume;
    m_rest = rest;
    m_items = items;
    m_end_session = session;
    m_end_write = write;
    return true;
}

bool FolderDisk::SaveLayout(const FatImage& disk, u64 session, u64 write)
{
    std::vector<FatImage_Extent> extents;
    std::vector<FatImage_Extent> kept;
    std::vector<FatImage_Extent> rest;
    std::vector<u8> raw;
    disk.GetSectorMap(&extents, true);

    if (session != 0)
        GetRest(disk, extents, m_resumed || ((session == m_end_session) && (write == m_end_write)), &rest);

    for (size_t i = 0; i < extents.size(); i++)
    {
        if ((extents[i].file < 0) || disk.GetFiles()[extents[i].file].directory)
            kept.push_back(extents[i]);
    }

    for (size_t i = 0; i < kept.size() + rest.size(); i++)
    {
        bool metadata = i < kept.size();
        const FatImage_Extent& extent = metadata ? kept[i] : rest[i - kept.size()];
        u32 count = extent.last - extent.first + 1;

        if (i == kept.size())
            raw.resize(raw.size() + 8, 0);

        size_t offset = raw.size();
        raw.resize(offset + 8 + ((size_t)count * k_folder_disk_sector));
        write_u32_le(raw.data() + offset, extent.first);
        write_u32_le(raw.data() + offset + 4, count);

        if (!ReadVolume(m_volume, extent.first, raw.data() + offset + 8, count, true) && metadata)
            return false;
    }

    mz_ulong packed_size = mz_compressBound((mz_ulong)raw.size());
    std::vector<u8> out(k_folder_disk_layout_header + packed_size);

    if (mz_compress2(out.data() + k_folder_disk_layout_header, &packed_size, raw.data(), (mz_ulong)raw.size(),
        MZ_DEFAULT_LEVEL) != MZ_OK)
        return false;

    write_u32_le(out.data(), k_folder_disk_layout_magic);
    write_u32_le(out.data() + 4, k_folder_disk_layout_version);
    write_u32_le(out.data() + 8, m_volume.blocks);
    write_u32_le(out.data() + 12, (u32)raw.size());
    write_u32_le(out.data() + 16, (u32)packed_size);
    write_u64_le(out.data() + 20, session);
    write_u64_le(out.data() + 28, write);
    out.resize(k_folder_disk_layout_header + packed_size);
    return MediaFile::WriteFileAtomic(GetLayoutPath().c_str(), out.data(), out.size());
}

void FolderDisk::GetRest(const FatImage& disk, const std::vector<FatImage_Extent>& extents, bool old,
    std::vector<FatImage_Extent>* rest)
{
    const std::vector<FatImage_File>& files = disk.GetFiles();
    std::vector<FatImage_Run> owners;
    u64 consumed = 0;

    for (size_t i = 0; i < extents.size(); i++)
    {
        u32 count = extents[i].last - extents[i].first + 1;

        if ((i == 0) || (extents[i].file != extents[i - 1].file))
            consumed = 0;

        FatImage_Run owner = { extents[i].first, count, extents[i].file, consumed };
        owners.push_back(owner);
        consumed += (u64)count * k_folder_disk_sector;
    }

    FatImage::SortRuns(&owners);
    std::map<u32, u32>::const_iterator slot = m_slots.begin();
    std::map<u32, FolderDisk_Rest>::const_iterator kept = old ? m_rest.begin() : m_rest.end();
    size_t next = 0;

    while ((slot != m_slots.end()) || (kept != m_rest.end()))
    {
        bool changed = (slot != m_slots.end()) && ((kept == m_rest.end()) || (slot->first <= kept->first));
        u32 lba = changed ? slot->first : kept->first;
        u32 skip = 0;

        if ((kept != m_rest.end()) && (kept->first == lba))
            ++kept;

        if (changed)
            ++slot;

        while ((next < owners.size()) && (owners[next].first + owners[next].count <= lba))
            next++;

        if ((next < owners.size()) && (owners[next].first <= lba))
        {
            const FatImage_Run& owner = owners[next];

            if ((owner.entry < 0) || files[owner.entry].directory)
                continue;

            u64 position = owner.offset + ((u64)(lba - owner.first) * k_folder_disk_sector);
            u64 size = files[owner.entry].size;

            if (position + k_folder_disk_sector <= size)
                continue;

            skip = position < size ? (u32)(size - position) : 0;
        }

        u8 sector[k_folder_disk_sector];
        ReadVolume(m_volume, lba, sector, 1, true);

        while ((skip < k_folder_disk_sector) && (sector[skip] == 0))
            skip++;

        if (skip == k_folder_disk_sector)
            continue;

        if (!rest->empty() && (rest->back().last + 1 == lba))
            rest->back().last++;
        else
        {
            FatImage_Extent extent = { lba, lba, -1 };
            rest->push_back(extent);
        }
    }
}

bool FolderDisk::LoadChanges()
{
    m_changes = MediaFile::OpenFile(GetChangesPath().c_str(), true);

    if (!IsValidPointer(m_changes))
        return false;

    s64 size = m_changes->GetSize();
    u32 slots = size > 0 ? (u32)(size / k_folder_disk_slot) : 0;
    u8 record[4];

    for (u32 slot = 0; slot < slots; slot++)
    {
        if (!m_changes->ReadAt((u64)slot * k_folder_disk_slot, record, sizeof(record)))
            return false;

        u32 lba = read_u32_le(record);

        if (lba < m_volume.blocks)
            m_slots[lba] = slot;
    }

    return true;
}

bool FolderDisk::WriteBack(u64 session, u64 write, FolderDisk_Result* result)
{
    *result = FolderDisk_Result();
    FolderDiskView view(this, &m_volume, true);
    FatImage parser;

    if (!parser.Read(&view))
    {
        result->refused = true;
        result->notes.push_back(std::string("The disk doesn't add up: ") + parser.GetReadError());
        return false;
    }

    const std::vector<FatImage_File>& files = parser.GetFiles();
    std::vector<FatImage_Extent> extents;
    parser.GetSectorMap(&extents, true);
    std::map<std::string, FolderDisk_Item> items = m_items;
    std::vector<std::vector<std::string> > deleted(m_layers.size());
    std::vector<bool> lists(m_layers.size(), false);
    std::set<std::string> present;
    std::vector<FolderDisk_Write> writes;
    size_t next = 0;

    for (size_t l = 0; l < m_layers.size(); l++)
        dos_fs_read_deleted(m_layers[l].folder, &deleted[l]);

    for (size_t i = 0; i < files.size(); i++)
    {
        const FatImage_File& file = files[i];
        present.insert(file.path);
        std::map<std::string, FolderDisk_Item>::iterator found = items.find(file.path);
        bool known = (found != items.end()) && (found->second.directory == file.directory);
        bool touched = IsTouched(extents, &next, (int)i);
        FolderDisk_Item item = known ? found->second : FolderDisk_Item();

        if (file.directory)
        {
            if (known)
                continue;

            if (!Route(file.path, &items, &item))
            {
                result->failed++;
                result->notes.push_back("No folder to write " + file.path + " to");
                continue;
            }

            item.directory = true;
            items[file.path] = item;

            if (m_quarantine)
                continue;

            if (MediaFile::MakeDirectories(path_join(m_layers[item.layer].folder, item.host).c_str()))
            {
                result->directories++;
                lists[item.layer] = unlist(&deleted[item.layer], item.host) || lists[item.layer];
            }
            else
            {
                result->failed++;
                result->notes.push_back("Unable to make " + path_join(m_layers[item.layer].folder, item.host));
            }

            continue;
        }

        if (known && !touched && (item.size == file.size) && (item.date == file.date) && (item.time == file.time) &&
            (item.first_cluster == file.first_cluster))
            continue;

        if (!known && !Route(file.path, &items, &item))
        {
            result->failed++;
            result->notes.push_back("No folder to write " + file.path + " to");
            continue;
        }

        FolderDisk_Write copy;
        copy.file = i;
        copy.item = item;
        copy.target = m_quarantine ? path_join(GetConflictsFolder(), path_forward_slashes(file.path)) :
            path_join(m_layers[item.layer].folder, item.host);
        writes.push_back(copy);

        if (!MediaFile::MakeDirectories(path_directory(copy.target).c_str()) ||
            !parser.CopyOut(file, (copy.target + ".tmp").c_str()))
        {
            result->failed++;
            result->notes.push_back("Unable to write " + copy.target);
        }
    }

    m_tree->CloseSources();

    if (result->failed != 0)
    {
        for (size_t i = 0; i < writes.size(); i++)
            MediaFile::RemoveFile((writes[i].target + ".tmp").c_str());

        return false;
    }

    for (size_t i = 0; i < writes.size(); i++)
    {
        const FatImage_File& file = files[writes[i].file];
        const FolderDisk_Item& item = writes[i].item;
        const std::string& target = writes[i].target;
        std::string temporary = target + ".tmp";

        if (!MediaFile::MoveIntoPlace(temporary.c_str(), target.c_str()))
        {
            MediaFile::RemoveFile(temporary.c_str());
            result->failed++;
            result->notes.push_back("Unable to write " + target);
            continue;
        }

        MediaFile::SetModified(target.c_str(), dos_time_to_host(file.date, file.time));

        if (m_quarantine)
        {
            result->conflicts++;
            continue;
        }

        result->written++;
        lists[item.layer] = unlist(&deleted[item.layer], item.host) || lists[item.layer];

        if ((item.entry > 0) && !m_tree->GetBase((u32)item.entry).empty())
        {
            const std::string& text = m_tree->GetBase((u32)item.entry);
            std::string base = path_join(path_join(m_layers[item.layer].folder, k_folder_disk_base), item.host);

            if (!MediaFile::MakeDirectories(path_directory(base).c_str()) ||
                !MediaFile::WriteFileAtomic(base.c_str(), text.data(), text.size()))
            {
                result->failed++;
                result->notes.push_back("Unable to write " + base);
            }
        }
    }

    if (result->failed != 0)
        return false;

    for (std::map<std::string, FolderDisk_Item>::reverse_iterator it = m_items.rbegin(); !m_quarantine &&
        (it != m_items.rend()); ++it)
    {
        const FolderDisk_Item& item = it->second;

        if (it->first.empty() || item.host.empty() || (present.count(it->first) != 0))
            continue;

        std::string target = path_join(m_layers[item.layer].folder, item.host);

        if (item.directory)
        {
            MediaFile::DeleteDirectory(target.c_str());
            result->directories++;
        }
        else
        {
            MediaFile::RemoveFile(target.c_str());
            result->deleted++;
        }

        if (m_layers[item.layer].deletions)
        {
            unlist(&deleted[item.layer], item.host);
            deleted[item.layer].push_back(item.host);
            lists[item.layer] = true;
        }
    }

    for (size_t l = 0; l < m_layers.size(); l++)
    {
        if (lists[l] && !dos_fs_write_deleted(m_layers[l].folder, deleted[l]))
        {
            result->failed++;
            result->notes.push_back("Unable to write " + path_join(m_layers[l].folder, k_dos_deleted_list));
        }
    }

    if (result->failed != 0)
        return false;

    if (!m_quarantine && !SaveLayout(parser, session, write))
    {
        result->failed++;
        result->notes.push_back("Unable to write " + GetLayoutPath());
        return false;
    }

    result->done = true;
    return true;
}

bool FolderDisk::Route(const std::string& path, std::map<std::string, FolderDisk_Item>* items, FolderDisk_Item* item)
    const
{
    size_t slash = path.find_last_of('\\');
    std::string parent = slash == std::string::npos ? std::string() : path.substr(0, slash);
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    std::map<std::string, FolderDisk_Item>::const_iterator found = items->find(parent);

    if (found == items->end())
        return false;

    *item = FolderDisk_Item();
    item->entry = -1;
    item->layer = found->second.layer;

    for (size_t l = 0; parent.empty() && (l < m_layers.size()); l++)
    {
        if (std::find(m_layers[l].names.begin(), m_layers[l].names.end(), name) != m_layers[l].names.end())
            item->layer = (u8)l;
    }

    item->host = found->second.children.empty() ? name : found->second.children + "/" + name;
    item->children = item->host;
    return item->layer < m_layers.size();
}

bool FolderDisk::IsTouched(const std::vector<FatImage_Extent>& extents, size_t* next, int file) const
{
    bool touched = false;

    while ((*next < extents.size()) && (extents[*next].file < file))
        (*next)++;

    for (; (*next < extents.size()) && (extents[*next].file == file); (*next)++)
    {
        std::map<u32, u32>::const_iterator it = m_slots.lower_bound(extents[*next].first);
        touched = touched || ((it != m_slots.end()) && (it->first <= extents[*next].last));
    }

    return touched;
}

std::string FolderDisk::GetChangesPath() const
{
    return path_join(path_join(m_data, k_folder_disk_folder), k_folder_disk_changes);
}

std::string FolderDisk::GetLayoutPath() const
{
    return path_join(path_join(m_data, k_folder_disk_folder), k_folder_disk_layout);
}

std::string FolderDisk::GetLockPath() const
{
    return path_join(path_join(m_data, k_folder_disk_folder), k_folder_disk_lock);
}
