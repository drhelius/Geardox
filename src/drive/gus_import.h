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

#ifndef GUS_IMPORT_H
#define GUS_IMPORT_H

#include <map>
#include <set>
#include <string>
#include <vector>
#include "../common/common.h"

enum GusImport_Result
{
    GUS_IMPORT_ERROR = 0,
    GUS_IMPORT_UNKNOWN,
    GUS_IMPORT_UNSUPPORTED,
    GUS_IMPORT_INCOMPLETE,
    GUS_IMPORT_DONE
};

struct GusImport_Software
{
    std::string name;
    std::string zip;
    int patches;
    u32 crc;
};

struct Gus_Output;
struct Gus_Record;

class GusImport
{
public:
    GusImport();
    ~GusImport();
    bool AddSource(const char* path);
    GusImport_Result Import(const char* zip_path);
    const char* GetName() const;
    const char* GetMessage() const;
    int GetPatchCount() const;
    u32 GetSourceFingerprint() const;
    const char* GetError() const;
    static bool Load(const char* zip_path, GusImport_Software* software, std::string* error);
    static bool ReadSourceFingerprint(const char* zip_path, u32* fingerprint);
    static bool ExpandImplode(const u8* data, size_t size, size_t expected, std::string* out);

private:
    bool AddFolder(const std::string& path, int depth);
    bool AddZipData(const u8* data, size_t size, int depth);
    bool AddImage(const u8* data, u32 size, const std::string& name);
    bool ReadFloppy(const u8* raw, u32 size);
    void AddFile(const std::string& name, const u8* data, size_t size);
    bool IsInstalled(const std::string& path) const;
    bool AddInstalled(const std::string& path, const std::string& prefix, int depth);
    GusImport_Result ImportInstalled(const char* zip_path);
    GusImport_Result ImportDisks(const char* zip_path);
    GusImport_Result Refuse();
    bool ReadRecords(const std::string& volume, u32 start, std::vector<Gus_Record>* records, size_t* end);
    GusImport_Result Finish(const char* zip_path, const char* family, const char* name, const char* title, int patches,
        const std::vector<Gus_Output>& output);
    bool Fail(GusImport_Result result, const char* format, ...);

private:
    std::map<std::string, std::string> m_files;
    std::set<std::string> m_names;
    std::vector<std::pair<std::string, std::string> > m_installed;
    std::string m_name;
    std::string m_message;
    int m_patches;
    GusImport_Result m_result;
    std::string m_error;
};

static const char k_gus_import_file[] = "ultrasnd.zip";
static const char k_gus_import_version[] = "version.txt";
static const char k_gus_import_midi[] = "MIDI/";

#endif /* GUS_IMPORT_H */
