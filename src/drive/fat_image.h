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

class MediaFile;

enum FatImage_Result
{
    FAT_IMAGE_ERROR = 0,
    FAT_IMAGE_TOO_BIG,
    FAT_IMAGE_BUILT,
    FAT_IMAGE_SAME,
    FAT_IMAGE_CHANGED
};

struct FatImage_Report
{
    FatImage_Result result;
    u32 left_out;
    u64 content_size;
};

enum FatImage_Origin
{
    FAT_IMAGE_ORIGIN_SOURCE = 0,
    FAT_IMAGE_ORIGIN_OVERLAY,
    FAT_IMAGE_ORIGIN_SYSTEM
};

struct FatImage_Entry
{
    std::string name;
    std::string source;
    u8 short_name[11];
    bool directory;
    u64 size;
    u16 date;
    u16 time;
    u32 zip_index;
    u32 first_cluster;
    u8 attributes;
    u8 origin;
    int memory;
    u32 crc;
    s64 modified;
    std::vector<u32> children;
};

// An entry as written: its DOS path and its path on the host, both from the root, backslashes and slashes
struct FatImage_Listed
{
    std::string dos_path;
    std::string host_path;
    u32 index;
};

// A file or directory found by Read, with its DOS path from the root
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

class FatImage
{
public:
    FatImage();
    ~FatImage();
    bool AddFolder(const char* path);
    bool AddZip(const u8* archive, size_t size);
    bool AddZipFile(const char* path);
    void SetLabel(const char* label);
    void SetBootSector(const u8* sector);
    u32 GetFingerprint() const;
    u32 GetLeftOut() const;
    u64 GetContentSize() const;
    u64 GetRequiredSize(u64 free_size) const;
    bool Write(const char* path, u64 size, bool replace = false);
    static bool WriteFreeDOS(const char* path, bool replace = false);
    static bool ReadFingerprint(const char* path, u32* fingerprint);
    static void GetImageName(const char* source, std::string* name);
    static void Prepare(const char* source, const char* image_path, bool rebuild, FatImage_Report* report);
    bool AddGame(const char* source, const char* dir = NULL);
    bool AddOverlay(const char* path);
    bool AddFreeDOS(bool hidden, const std::vector<std::string>& reserved);
    bool AddMSDOS(const u8* zip, size_t size, bool hidden, const std::vector<std::string>& reserved,
        const std::vector<std::string>& pinned, bool shsucdx);
    bool AddMemoryFile(const char* path, const std::string& data, u8 attributes);
    void FinishGame(const char* label, u64 free_size, const u8* boot_sector);
    const FatImage_Entry& GetEntry(u32 index) const;
    void ListEntries(std::vector<FatImage_Listed>* listed) const;
    bool Read(const char* path);
    void CloseRead();
    const std::vector<FatImage_File>& GetFiles() const;
    const char* GetReadError() const;
    bool GetFileCRC(const FatImage_File& file, u32* crc);
    bool GetFileData(const FatImage_File& file, std::string* data, u32* crc);
    bool CopyOut(const FatImage_File& file, const char* path, u32* crc = NULL);

private:
    void Clear();
    bool WalkFolder(const std::string& path, u32 directory, int depth);
    bool ScanZip();
    u32 AddEntry(u32 directory, const std::string& name, bool is_directory);
    int FindChild(u32 directory, const std::string& name) const;
    void RemoveChild(u32 directory, u32 index);
    bool WalkOverlay(const std::string& path, u32 directory, int depth);
    void LeaveOutReserved(const std::vector<std::string>& reserved);
    bool AddFreeDOSFile(mz_zip_archive* zip, const char* name, u32 directory, const char* target);
    void PinEntries();
    void ApplyDeleted(const std::string& list);
    void ListDirectory(u32 directory, const std::string& dos_path, const std::string& host_path,
        std::vector<FatImage_Listed>* listed) const;
    bool ReadDirectory(u32 cluster, const std::string& path, int depth, std::vector<u8>* used);
    bool ReadChain(u32 first, std::vector<u32>* chain, std::vector<u8>* used);
    bool ReadFileData(const FatImage_File& file, MediaFile* output, std::string* data, u32* crc);
    void Finish();
    void AssignNames(u32 directory);
    void LimitPaths(u32 directory, const std::string& path);
    u32 CountEntries(u32 directory) const;
    void HashEntries(u32 directory, u32* crc) const;
    u64 CountClusters(u32 directory, u32 cluster_size) const;
    bool GetLayout(u32 total, FatImage_Layout* layout) const;
    u32 Allocate(u32 clusters);
    bool PlaceDirectory(u32 directory, u32 cluster, u32 parent, std::vector<u8>* content);
    bool CopyFile(FatImage_Entry& entry, u64 offset);
    bool WriteFat();
    bool WriteBootSector();
    bool WriteMasterBootRecord();
    u64 GetClusterOffset(u32 cluster) const;
    static size_t ReadZipFile(void* opaque, mz_uint64 offset, void* buffer, size_t size);
    static size_t WriteZipEntry(void* opaque, mz_uint64 offset, const void* buffer, size_t size);

private:
    std::vector<FatImage_Entry> m_entries;
    std::vector<u16> m_fat;
    std::vector<u8> m_buffer;
    FatImage_Layout m_layout;
    mz_zip_archive m_zip;
    bool m_zip_open;
    mz_zip_archive m_system_zip;
    bool m_system_zip_open;
    std::vector<std::string> m_pinned;
    u32 m_pinned_count;
    MediaFile* m_zip_file;
    MediaFile* m_output;
    u64 m_zip_offset;
    char m_label[12];
    const u8* m_boot_sector;
    u32 m_fingerprint;
    u32 m_left_out;
    u32 m_next_cluster;
    u32 m_base;
    std::vector<std::string> m_memory;
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
