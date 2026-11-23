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

#ifndef ISO_IMAGE_H
#define ISO_IMAGE_H

#include <string>
#include <vector>
#include "../common/common.h"
#include "../media/media_file.h"

struct IsoImage_Entry
{
    std::string name;
    std::string source;
    char short_name[11];
    bool directory;
    u64 size;
    s64 modified;
    u32 parent;
    u32 extent;
    u32 length;
    std::vector<u32> children;
};

struct IsoImage_Run
{
    u32 first;
    u32 count;
    int entry;
    u64 offset;
};

static const u32 k_iso_open_sources = 4;

class IsoImage
{
public:
    IsoImage();
    ~IsoImage();
    bool AddFolder(const char* path);
    void SetLabel(const char* label);
    u32 GetFingerprint() const;
    u32 GetLeftOut() const;
    bool Build();
    u32 GetSectors() const;
    void ReadSector(u32 lba, u8* buffer);

private:
    void Clear();
    bool WalkFolder(const std::string& path, u32 directory, int level);
    u32 AddEntry(u32 directory, const std::string& name, bool is_directory);
    void AssignNames(u32 directory);
    void HashEntries(u32 directory, u32* crc) const;
    bool Layout();
    void BuildDirectory(u32 directory, std::vector<u8>* content) const;
    void BuildPathTable(bool big_endian, std::vector<u8>* content) const;
    void WriteDescriptor(u8* sector) const;
    void AddMetadata(u32 first, const u8* data, size_t size);
    MediaFile* OpenSource(u32 index);
    void CloseSources();

private:
    std::vector<IsoImage_Entry> m_entries;
    std::vector<u32> m_order;
    std::vector<u8> m_metadata;
    std::vector<IsoImage_Run> m_runs;
    MediaFile* m_sources[k_iso_open_sources];
    u32 m_source_index[k_iso_open_sources];
    u32 m_next_source;
    char m_label[33];
    u32 m_left_out;
    u32 m_path_size;
    u32 m_path_sectors;
    u32 m_total;
    s64 m_volume_time;
};

class IsoImageFile : public MediaFile
{
public:
    IsoImageFile();
    bool Load(const char* folder, const char* label);
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
    IsoImage m_image;
    bool m_loaded;
    s64 m_position;
};

#endif /* ISO_IMAGE_H */
