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

#ifndef FIRMWARE_H
#define FIRMWARE_H

#include <string>
#include <vector>
#include "../common/common.h"

class MediaFile;

#define FIRMWARE_SET_COUNT 0
#define FIRMWARE_FILE_NAMES 2

class Firmware
{
public:
    Firmware();
    ~Firmware();
    void Init();
    void Unload();
    bool LoadSource(const GDOX_Firmware_Source& source, bool quiet = false);
    bool IsReady() const;
    bool IsComplete() const;
    const char* GetDirectory() const;
    const GDOX_Firmware_Source& GetSource() const;
    const GDOX_Firmware_Info& GetInfo(GDOX_Firmware_Type type) const;
    const u8* GetSystemRom() const;
    const u8* GetVGARom() const;
    GDOX_Firmware_Style GetStyle() const;

    static void FindSources(const char* default_folder, const std::string* model_folders, GDOX_Firmware_Source* sources);
    static int FindSet(u32 system_crc);
    static GDOX_Firmware_Source_Type GetSourceType(int set, GDOX_Machine_Model model);
    static const char* GetComponentName(GDOX_Firmware_Type type);
    static const char* GetFileName(GDOX_Firmware_Type type);
    static int GetMaxSize(GDOX_Firmware_Type type);
    static bool IsValidSize(GDOX_Firmware_Type type, u32 size);
    static const char* GetSizeText(GDOX_Firmware_Type type);
    static bool IsRequired(GDOX_Firmware_Type type);
    static u16 GetDatabaseFlag(GDOX_Firmware_Type type);
    static bool IsSeaBIOS(const u8* data, u32 size);

private:
    struct Container
    {
        bool zip;
        const char* path;
        mz_zip_archive archive;
        MediaFile* zip_file;
        bool listed;
        std::vector<std::string> files;
    };

    void GatherDatabaseInfo(GDOX_Firmware_Type type, GDOX_Firmware_Info& info);
    bool LoadBuiltin(const GDOX_Firmware_Source& source, bool quiet);
    static void SetBuiltinInfo(GDOX_Firmware_Type type, GDOX_Firmware_Info& info);
    u8* GetWritableData(GDOX_Firmware_Type type);
    static bool IdentifySource(GDOX_Firmware_Source& source);
    static void AddCandidate(std::vector<GDOX_Firmware_Source>& candidates, const char* path, bool zip);
    static bool OpenContainer(const char* path, bool zip, Container& container);
    static void CloseContainer(Container& container);
    static bool ReadComponent(Container& container, GDOX_Firmware_Type type, u8* destination, GDOX_Firmware_Info& info, bool report);
    static bool ReadPart(Container& container, GDOX_Firmware_Type type, int part, u8* destination, std::string* path, bool report, u32* size);
    static bool ReadNamed(Container& container, GDOX_Firmware_Type type, const char* name, u8* destination, std::string* path, bool report, u32* size);
    static std::string FindFile(const Container& container, const char* name);
    static int FindZipEntry(mz_zip_archive& zip, const char* name);
    static int GetPartCount(GDOX_Firmware_Type type);
    static const char* GetPartName(GDOX_Firmware_Type type, int part, int index);
    static bool OpenZip(const char* path, MediaFile** file, mz_zip_archive& zip);
    static size_t ReadZipCallback(void* opaque, mz_uint64 offset, void* buffer, size_t size);

private:
    GDOX_Firmware_Source m_source;
    GDOX_Firmware_Info m_info[GDOX_FIRMWARE_COUNT];
    GDOX_Firmware_Info m_pending_info[GDOX_FIRMWARE_COUNT];
    bool m_ready;
    GDOX_Firmware_Style m_style;
    u8 m_system_rom[GDOX_FIRMWARE_SYSTEM_BIOS_MAX_SIZE];
    u8 m_vga_rom[GDOX_FIRMWARE_VGA_BIOS_MAX_SIZE];
};

static const GDOX_Firmware_Set k_firmware_sets[FIRMWARE_SET_COUNT + 1] =
{
    { NULL, NULL, 0, 0, 0 }
};

#include "firmware_inline.h"

#endif /* FIRMWARE_H */
