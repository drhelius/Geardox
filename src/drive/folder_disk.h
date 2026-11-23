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

#ifndef FOLDER_DISK_H
#define FOLDER_DISK_H

#include <map>
#include <string>
#include <vector>
#include "../common/common.h"
#include "fat_image.h"
#include "hard_disk_image.h"

class MediaFile;

struct FolderDisk_Layer
{
    std::string folder;
    bool deletions;
    std::vector<std::string> names;
};

struct FolderDisk_Result
{
    bool done;
    bool refused;
    u32 written;
    u32 deleted;
    u32 directories;
    u32 conflicts;
    u32 failed;
    std::vector<std::string> notes;
};

struct FolderDisk_Item
{
    int entry;
    bool directory;
    u32 size;
    u16 date;
    u16 time;
    u32 first_cluster;
    u8 layer;
    std::string host;
    std::string children;
};

struct FolderDisk_Rest
{
    u64 offset;
    u32 skip;
};

class FolderDisk : public HardDiskSource
{
public:
    FolderDisk();
    ~FolderDisk();
    static FolderDisk* OpenSource(const char* source, const char* save_folder, const char* temp_folder,
        std::string* error);
    static std::string GetDataFolder(const char* save_folder, const std::string& name, const std::string& key);
    static bool IsWritable(const std::string& folder);
    static bool HasSession(const std::string& data_folder);
    bool Recover(FatImage* tree, const std::vector<FolderDisk_Layer>& layers, const std::string& data_folder);
    bool Open(FatImage* tree, const std::vector<FolderDisk_Layer>& layers, const std::string& data_folder,
        u64 free_size, const std::string& path);
    void SetTemporary(const std::string& folder);
    bool Close(u64 session, u64 write, bool resumed, FolderDisk_Result* result);
    void Discard();
    void SetQuarantine();
    bool IsQuarantined() const;
    bool GetEndMark(u64* session, u64* write) const;
    const char* GetPath() const;
    const char* GetDataFolder() const;
    std::string GetConflictsFolder() const;
    const FolderDisk_Result& GetRecovery() const;
    const char* GetError() const;
    u32 GetLeftOut() const;
    std::string GetHostPath(const std::string& dos_path) const;
    u32 GetBlocks() const;
    bool Read(u32 lba, u8* buffer, u32 count);
    bool Store(u32 lba, const u8* buffer);
    bool Sync();
    bool ReadVolume(const FatImage_Volume& volume, u32 lba, u8* buffer, u32 count, bool changes);

private:
    bool Fail(const char* format, ...);
    bool TakeLock();
    void ReleaseLock();
    void Clear();
    void AddItems();
    bool LoadLayout(bool strict);
    bool SaveLayout(const FatImage& disk, u64 session, u64 write);
    void GetRest(const FatImage& disk, const std::vector<FatImage_Extent>& extents, bool old, std::vector<FatImage_Extent>* rest);
    bool LoadChanges();
    bool WriteBack(u64 session, u64 write, FolderDisk_Result* result);
    bool Route(const std::string& path, std::map<std::string, FolderDisk_Item>* items, FolderDisk_Item* item) const;
    bool IsTouched(const std::vector<FatImage_Extent>& extents, size_t* next, int file) const;
    std::string GetChangesPath() const;
    std::string GetLayoutPath() const;
    std::string GetLockPath() const;

private:
    FatImage* m_tree;
    std::vector<FolderDisk_Layer> m_layers;
    std::string m_data;
    std::string m_path;
    std::string m_temporary;
    FatImage_Volume m_volume;
    std::map<std::string, FolderDisk_Item> m_items;
    MediaFile* m_changes;
    std::map<u32, u32> m_slots;
    std::map<u32, FolderDisk_Rest> m_rest;
    u64 m_end_session;
    u64 m_end_write;
    bool m_locked;
    bool m_quarantine;
    bool m_resumed;
    FolderDisk_Result m_recovery;
    std::string m_error;
};

#endif /* FOLDER_DISK_H */
