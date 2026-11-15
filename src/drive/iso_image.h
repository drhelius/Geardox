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

class MediaFile;

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

class IsoImage
{
public:
    IsoImage();
    ~IsoImage();
    bool AddFolder(const char* path);
    void SetLabel(const char* label);
    u32 GetFingerprint() const;
    u32 GetLeftOut() const;
    bool Write(const char* path);
    static bool ReadFingerprint(const char* path, u32* fingerprint);

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
    bool WriteDirectories();
    bool CopyFiles();

private:
    std::vector<IsoImage_Entry> m_entries;
    std::vector<u32> m_order;
    std::vector<u8> m_buffer;
    MediaFile* m_output;
    char m_label[33];
    u32 m_left_out;
    u32 m_path_size;
    u32 m_path_sectors;
    u32 m_total;
    s64 m_volume_time;
};

#endif /* ISO_IMAGE_H */
