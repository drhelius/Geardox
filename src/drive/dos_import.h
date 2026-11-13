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

#ifndef DOS_IMPORT_H
#define DOS_IMPORT_H

#include <map>
#include <string>
#include <vector>
#include "../common/common.h"

enum DosImport_Result
{
    DOS_IMPORT_ERROR = 0,
    DOS_IMPORT_UNKNOWN,
    DOS_IMPORT_INCOMPLETE,
    DOS_IMPORT_DONE
};

struct DosImport_File
{
    std::string data;
    u16 date;
    u16 time;
    int image;
};

// An imported system: its name, what goes in C:'s root, the ZIP's bytes for FatImage, Disk 1's boot sector
struct DosImport_System
{
    std::string name;
    int version;
    std::vector<std::string> pinned;
    std::vector<std::string> root;
    bool cd_extension;
    bool cache;
    std::string zip;
    u8 boot[512];
    u32 crc;
};

class DosImport
{
public:
    DosImport();
    ~DosImport();
    bool AddSource(const char* path);
    DosImport_Result Import(const char* zip_path);
    const char* GetName() const;
    u32 GetSourceFingerprint() const;
    int GetImageCount() const;
    const char* GetError() const;
    static bool Load(const char* zip_path, DosImport_System* system, std::string* error);
    static bool ReadSourceFingerprint(const char* zip_path, u32* fingerprint);
    static bool ExpandKWAJ(const u8* data, size_t size, std::string* out);
    static bool ExpandSZDD(const u8* data, size_t size, std::string* out);

private:
    bool AddFolder(const std::string& path);
    bool AddZip(const std::string& path);
    bool AddImageFile(const std::string& path);
    bool AddImage(const u8* data, u32 size, const std::string& name);
    bool ReadFloppy(const u8* raw, u32 size, int image);
    bool Fail(DosImport_Result result, const char* format, ...);

private:
    std::map<std::string, DosImport_File> m_files;
    std::vector<std::vector<u8> > m_boot;
    std::vector<std::pair<std::string, u32> > m_images;
    std::string m_name;
    DosImport_Result m_result;
    std::string m_error;
};

static const char k_dos_import_file[] = "msdos.zip";
static const char k_dos_import_version[] = "version.txt";
static const char k_dos_import_boot[] = "boot.bin";
static const char k_dos_import_root[] = "C/";

#endif /* DOS_IMPORT_H */
