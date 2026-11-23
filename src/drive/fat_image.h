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

#ifndef FAT_IMAGE_H
#define FAT_IMAGE_H

#include <string>
#include <vector>
#include "../common/common.h"
#include "../media/media_zip.h"

class MediaFile;

struct FatImage_Entry
{
    std::string name;
    std::string source;
    std::string host;
    u8 short_name[11];
    bool directory;
    u64 size;
    u16 date;
    u16 time;
    u32 first_cluster;
    u8 attributes;
    u8 layer;
    int memory;
    int base;
    std::vector<u32> children;
};

struct FatImage_Listed
{
    std::string dos_path;
    std::string host_path;
    u32 index;
};

struct FatImage_File
{
    std::string path;
    u8 attributes;
    bool directory;
    u32 size;
    u16 date;
    u16 time;
    u32 first_cluster;
};

struct FatImage_Extent
{
    u32 first;
    u32 last;
    int file;
};

struct FatImage_Rule
{
    std::string source;
    std::string target;
    bool file;
};

struct FatImage_Layout
{
    u32 total;
    u32 heads;
    u32 partition;
    u32 cluster_sectors;
    u32 root_entries;
    u32 root_sectors;
    u32 fat_sectors;
    u32 first_data;
    u32 clusters;
};

struct FatImage_Run
{
    u32 first;
    u32 count;
    int entry;
    u64 offset;
};

struct FatImage_Volume
{
    u32 blocks;
    std::vector<u8> metadata;
    std::vector<FatImage_Run> runs;
};

static const u32 k_fat_open_sources = 8;

class FatImage
{
public:
    FatImage();
    ~FatImage();
    bool AddFolder(const char* path, u8 layer);
    bool AddGame(const char* source, const char* dir, u8 layer, u8 root_layer);
    bool AddOverlay(const char* path, bool game, u8 layer);
    void SetLayer(u8 layer);
    void SetLabel(const char* label);
    void SetBootSector(const u8* sector);
    u32 GetLeftOut() const;
    u64 GetRequiredSize(u64 free_size) const;
    bool Build(u64 size, FatImage_Volume* volume);
    bool Write(const char* path, u64 size, bool replace = false);
    bool ApplyLayout(const std::vector<FatImage_Rule>& rules);
    bool AddFreeDOS(bool hidden, const std::vector<std::string>& reserved);
    bool AddMSDOS(const u8* zip, size_t size, bool hidden, const std::vector<std::string>& reserved,
        const std::vector<std::string>& pinned, bool shsucdx);
    bool AddUltraSound(const u8* zip, size_t size);
    bool AddMemoryFile(const char* path, const std::string& data, u8 attributes, const std::string* base = NULL);
    bool HasFile(const char* path) const;
    bool GetFile(const char* path, std::string* data);
    void LeaveOutMatching(const char* name, bool (*match)(const std::string& data));
    bool PatchFile(const char* path, u64 offset, const std::string& before, const std::string& after);
    void FinishGame(const char* label, const u8* boot_sector);
    const FatImage_Entry& GetEntry(u32 index) const;
    u32 GetBaseIndex() const;
    const std::string& GetBase(u32 index) const;
    void ListEntries(std::vector<FatImage_Listed>* listed) const;
    u32 ReadEntryAt(u32 index, u64 offset, u8* buffer, u32 size);
    bool Read(MediaFile* file);
    static void SortRuns(std::vector<FatImage_Run>* runs);
    void CloseRead();
    const std::vector<FatImage_File>& GetFiles() const;
    void GetSectorMap(std::vector<FatImage_Extent>* extents) const;
    const char* GetReadError() const;
    bool CopyOut(const FatImage_File& file, const char* path);

private:
    void Clear();
    void CloseSources();
    MediaFile* OpenSource(u32 index);
    bool WalkFolder(const std::string& path, u32 directory, int depth);
    u32 AddEntry(u32 directory, const std::string& name, bool is_directory);
    int FindChild(u32 directory, const std::string& name) const;
    bool FindParent(const char* path, u32* directory, std::string* name) const;
    bool ReadEntry(const FatImage_Entry& entry, std::string* data);
    void RemoveChild(u32 directory, u32 index);
    bool WalkOverlay(const std::string& path, u32 directory, int depth);
    void LeaveOutReserved(const std::vector<std::string>& reserved);
    void LeaveOutMatching(u32 directory, const std::string& name, bool (*match)(const std::string& data));
    bool AddFreeDOSFile(MediaZip* zip, const char* name, u32 directory, const char* target);
    bool AddDosTool(u32 directory, const char* name, const u8* data, u32 size);
    u32 AddDirectories(u32 directory, std::string* rest);
    void PinEntries();
    void ApplyDeleted(u32 directory, const std::vector<std::string>& lines);
    void ListDirectory(u32 directory, const std::string& dos_path, const std::string& host_path,
        std::vector<FatImage_Listed>* listed) const;
    bool ReadDirectory(u32 cluster, const std::string& path, int depth, std::vector<u8>* used);
    bool ReadChain(u32 first, std::vector<u32>* chain, std::vector<u8>* used);
    void Finish();
    void AssignNames(u32 directory);
    void LimitPaths(u32 directory, const std::string& path);
    u32 CountEntries(u32 directory) const;
    u64 CountClusters(u32 directory, u32 cluster_size) const;
    bool GetLayout(u32 total, FatImage_Layout* layout) const;
    u32 Allocate(u32 clusters);
    void PlaceDirectory(u32 directory, u32 cluster, u32 parent, FatImage_Volume* volume);
    u32 GetClusterSector(u32 cluster) const;
    void AddMetadata(FatImage_Volume* volume, u32 first, const u8* data, size_t size);
    void WriteFat(FatImage_Volume* volume);
    void WriteBootSector(FatImage_Volume* volume);
    void WriteMasterBootRecord(FatImage_Volume* volume);

private:
    std::vector<FatImage_Entry> m_entries;
    std::vector<u16> m_fat;
    FatImage_Layout m_layout;
    std::vector<std::string> m_pinned;
    u32 m_pinned_count;
    char m_label[12];
    const u8* m_boot_sector;
    u32 m_left_out;
    u32 m_next_cluster;
    u32 m_base;
    u8 m_layer;
    std::vector<std::string> m_memory;
    u32 m_source_index[k_fat_open_sources];
    MediaFile* m_sources[k_fat_open_sources];
    u32 m_next_source;
    MediaFile* m_read;
    u64 m_read_offset;
    std::vector<FatImage_File> m_files;
    std::string m_read_error;
};

static const u8 k_fat_attribute_read_only = 0x01;
static const u8 k_fat_attribute_hidden = 0x02;
static const u8 k_fat_attribute_system = 0x04;

static const u64 k_fat_min_size = 33ULL * 1024 * 1024;
static const u64 k_fat_free_size = 64ULL * 1024 * 1024;
static const u64 k_fat_freedos_size = 1024ULL * 16 * 63 * 512;

#endif /* FAT_IMAGE_H */
