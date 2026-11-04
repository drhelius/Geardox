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

#include <algorithm>
#include <new>
#include <string>
#include "firmware.h"
#include "seabios.h"
#include "media_file.h"
#include "game_db.h"
#include "crc.h"

static const char* const k_firmware_file_names[GDOX_FIRMWARE_COUNT][FIRMWARE_FILE_NAMES] =
{
    { "BIOS.BIN", "BIOS.ROM" },
    { "VGABIOS.BIN", "VGABIOS.ROM" }
};

Firmware::Firmware()
{
    Init();
}

Firmware::~Firmware()
{
}

void Firmware::Init()
{
    Unload();
}

void Firmware::Unload()
{
    memset(&m_source, 0, sizeof(m_source));
    m_source.set = -1;
    memset(m_info, 0, sizeof(m_info));
    memset(m_system_rom, 0xFF, sizeof(m_system_rom));
    memset(m_vga_rom, 0xFF, sizeof(m_vga_rom));
    m_ready = false;
    m_style = GDOX_FIRMWARE_STYLE_SEABIOS;
}

bool Firmware::LoadSource(const GDOX_Firmware_Source& source, bool quiet)
{
    if (source.type == GDOX_FIRMWARE_SOURCE_BUILTIN)
        return LoadBuiltin(source, quiet);

    if (source.path[0] == '\0')
    {
        Error("Invalid firmware folder");
        return false;
    }

    u8* pending_data[GDOX_FIRMWARE_COUNT] = {};
    memset(m_pending_info, 0, sizeof(m_pending_info));
    Container container;
    bool success = OpenContainer(source.path, source.zip, container);

    if (!success)
        Error("Unable to open firmware zip %s", source.path);

    for (int i = 0; success && (i < GDOX_FIRMWARE_COUNT); i++)
    {
        GDOX_Firmware_Type type = (GDOX_Firmware_Type)i;
        pending_data[i] = new (std::nothrow) u8[GetMaxSize(type)];
        bool ok = IsValidPointer(pending_data[i]) &&
            ReadComponent(container, type, pending_data[i], m_pending_info[i], true);

        if (!ok)
        {
            SafeDeleteArray(pending_data[i]);

            if (IsRequired(type))
            {
                Error("Required firmware is missing or invalid: %s in %s", GetFileName(type), source.path);
                success = false;
                break;
            }

            SetBuiltinInfo(type, m_pending_info[i]);
        }
    }

    CloseContainer(container);

    if (!success)
    {
        for (int i = 0; i < GDOX_FIRMWARE_COUNT; i++)
            SafeDeleteArray(pending_data[i]);

        return false;
    }

    for (int i = 0; i < GDOX_FIRMWARE_COUNT; i++)
    {
        GDOX_Firmware_Type type = (GDOX_Firmware_Type)i;
        u8* destination = GetWritableData(type);
        int size = m_pending_info[i].size;

        memset(destination, 0xFF, GetMaxSize(type));

        if (IsValidPointer(pending_data[i]))
            memcpy(destination, pending_data[i], size);
        else
            memcpy(destination, k_seabios_vgabios, size);

        m_info[i] = m_pending_info[i];

        if (!m_info[i].builtin)
            GatherDatabaseInfo(type, m_info[i]);

        SafeDeleteArray(pending_data[i]);

        if (quiet)
            continue;

        if (m_info[i].builtin)
        {
            Log("Optional firmware not found: %s. Using the built-in %s.", GetFileName(type), m_info[i].database_name);
        }
        else if (m_info[i].recognized)
        {
            Log("Firmware loaded: %s. CRC: %08X", m_info[i].database_name, m_info[i].crc);
        }
        else
        {
            Log("Unknown firmware loaded: %s. CRC: %08X", GetFileName(type), m_info[i].crc);
        }
    }

    m_source = source;
    m_ready = true;
    m_style = IsSeaBIOS(m_system_rom, (u32)m_info[GDOX_FIRMWARE_SYSTEM_BIOS].size) ? GDOX_FIRMWARE_STYLE_SEABIOS :
        GDOX_FIRMWARE_STYLE_AT;
    return true;
}

// SeaBIOS and SeaVGABIOS built from the pinned SeaBIOS commit, compiled in
bool Firmware::LoadBuiltin(const GDOX_Firmware_Source& source, bool quiet)
{
    Unload();

    for (int i = 0; i < GDOX_FIRMWARE_COUNT; i++)
    {
        GDOX_Firmware_Type type = (GDOX_Firmware_Type)i;
        SetBuiltinInfo(type, m_info[i]);
        memcpy(GetWritableData(type), type == GDOX_FIRMWARE_SYSTEM_BIOS ? k_seabios_bios :
            k_seabios_vgabios, m_info[i].size);
    }

    if (!quiet)
        Log("Firmware loaded: built-in %s", m_info[GDOX_FIRMWARE_SYSTEM_BIOS].database_name);

    m_source = source;
    m_ready = true;
    m_style = GDOX_FIRMWARE_STYLE_SEABIOS;
    return true;
}

void Firmware::SetBuiltinInfo(GDOX_Firmware_Type type, GDOX_Firmware_Info& info)
{
    bool system = type == GDOX_FIRMWARE_SYSTEM_BIOS;
    const u8* data = system ? k_seabios_bios : k_seabios_vgabios;

    memset(&info, 0, sizeof(info));
    strncpy_fit(info.path, "Built-in", sizeof(info.path));
    snprintf(info.database_name, sizeof(info.database_name), "%s %s", system ? "SeaBIOS" : "SeaVGABIOS",
        k_seabios_version);
    info.size = (int)(system ? k_seabios_bios_size : k_seabios_vgabios_size);
    info.crc = CalculateCRC32(0, data, (u32)info.size);
    info.loaded = true;
    info.recognized = true;
    info.builtin = true;
}

// Every SeaBIOS image carries its version banner
bool Firmware::IsSeaBIOS(const u8* data, u32 size)
{
    static const char k_banner[] = "SeaBIOS (version ";
    u32 length = sizeof(k_banner) - 1;

    for (u32 i = 0; i + length <= size; i++)
    {
        if (data[i] == 'S' && memcmp(data + i, k_banner, length) == 0)
            return true;
    }

    return false;
}

void Firmware::GatherDatabaseInfo(GDOX_Firmware_Type type, GDOX_Firmware_Info& info)
{
    info.recognized = false;
    info.database_name[0] = '\0';
    u16 flag = GetDatabaseFlag(type);

    for (int i = 0; k_game_database[i].title != 0; i++)
    {
        const GDOX_DB_Entry& entry = k_game_database[i];

        if (entry.crc == info.crc && (entry.flags & flag))
        {
            info.recognized = true;
            strncpy_fit(info.database_name, entry.title, sizeof(info.database_name));
            return;
        }
    }
}

u8* Firmware::GetWritableData(GDOX_Firmware_Type type)
{
    switch (type)
    {
        case GDOX_FIRMWARE_SYSTEM_BIOS:
            return m_system_rom;
        case GDOX_FIRMWARE_VGA_BIOS:
            return m_vga_rom;
        default:
            return NULL;
    }
}

const char* Firmware::GetComponentName(GDOX_Firmware_Type type)
{
    switch (type)
    {
        case GDOX_FIRMWARE_SYSTEM_BIOS:
            return "System BIOS";
        case GDOX_FIRMWARE_VGA_BIOS:
            return "VGA BIOS";
        default:
            return "Unknown ROM";
    }
}

const char* Firmware::GetFileName(GDOX_Firmware_Type type)
{
    return ((int)type >= 0) && ((int)type < GDOX_FIRMWARE_COUNT) ? k_firmware_file_names[type][0] : "";
}

int Firmware::GetMaxSize(GDOX_Firmware_Type type)
{
    switch (type)
    {
        case GDOX_FIRMWARE_SYSTEM_BIOS:
            return GDOX_FIRMWARE_SYSTEM_BIOS_MAX_SIZE;
        case GDOX_FIRMWARE_VGA_BIOS:
            return GDOX_FIRMWARE_VGA_BIOS_MAX_SIZE;
        default:
            return 0;
    }
}

// System BIOS images fill the top 64, 128 or 256 KB, a VGA BIOS is an option ROM in 512-byte blocks
bool Firmware::IsValidSize(GDOX_Firmware_Type type, u32 size)
{
    switch (type)
    {
        case GDOX_FIRMWARE_SYSTEM_BIOS:
            return (size == 64 * 1024) || (size == 128 * 1024) || (size == 256 * 1024);
        case GDOX_FIRMWARE_VGA_BIOS:
            return (size >= 2048) && (size <= GDOX_FIRMWARE_VGA_BIOS_MAX_SIZE) && ((size & 0x1FF) == 0);
        default:
            return false;
    }
}

const char* Firmware::GetSizeText(GDOX_Firmware_Type type)
{
    switch (type)
    {
        case GDOX_FIRMWARE_SYSTEM_BIOS:
            return "64, 128 or 256 KB";
        case GDOX_FIRMWARE_VGA_BIOS:
            return "up to 64 KB";
        default:
            return "";
    }
}

bool Firmware::IsRequired(GDOX_Firmware_Type type)
{
    return type == GDOX_FIRMWARE_SYSTEM_BIOS;
}

u16 Firmware::GetDatabaseFlag(GDOX_Firmware_Type type)
{
    switch (type)
    {
        case GDOX_FIRMWARE_SYSTEM_BIOS:
            return GDOX_GAMEDB_FIRMWARE_SYSTEM_BIOS;
        case GDOX_FIRMWARE_VGA_BIOS:
            return GDOX_GAMEDB_FIRMWARE_VGA_BIOS;
        default:
            return GDOX_GAMEDB_FLAG_NONE;
    }
}

void Firmware::FindSources(const char* default_folder, const std::string* model_folders, GDOX_Firmware_Source* sources)
{
    std::vector<GDOX_Firmware_Source> candidates;

    if (IsValidPointer(default_folder) && (default_folder[0] != '\0'))
    {
        std::vector<std::string> folders;
        std::vector<std::string> files;
        MediaFile::ListDirectory(default_folder, folders, files);
        std::sort(folders.begin(), folders.end());
        std::sort(files.begin(), files.end());

        AddCandidate(candidates, default_folder, false);

        for (size_t i = 0; i < folders.size(); i++)
        {
            std::string path(default_folder);
            append_path_component(path, folders[i].c_str());
            AddCandidate(candidates, path.c_str(), false);
        }

        for (size_t i = 0; i < files.size(); i++)
        {
            if (!ends_with_no_case(files[i].c_str(), ".zip"))
                continue;

            std::string path(default_folder);
            append_path_component(path, files[i].c_str());
            AddCandidate(candidates, path.c_str(), true);
        }
    }

    for (int model = 0; model < GDOX_MACHINE_COUNT; model++)
    {
        GDOX_Firmware_Source& source = sources[model];
        memset(&source, 0, sizeof(source));
        source.set = -1;

        if (!model_folders[model].empty())
        {
            strncpy_fit(source.path, model_folders[model].c_str(), sizeof(source.path));
            IdentifySource(source);
            source.type = GDOX_FIRMWARE_SOURCE_MODEL_FOLDER;
            continue;
        }

        for (size_t i = 0; i < candidates.size(); i++)
        {
            GDOX_Firmware_Source_Type type = GetSourceType(candidates[i].set, (GDOX_Machine_Model)model);

            bool preferred = (type == source.type) && (type == GDOX_FIRMWARE_SOURCE_COMPATIBLE_SET) &&
                (candidates[i].set < source.set);

            if ((source.type == GDOX_FIRMWARE_SOURCE_NONE) || (type < source.type) || preferred)
            {
                source = candidates[i];
                source.type = type;
            }
        }

        // Without a BIOS of its own a model runs the built-in SeaBIOS
        if (source.type == GDOX_FIRMWARE_SOURCE_NONE)
        {
            source.type = GDOX_FIRMWARE_SOURCE_BUILTIN;
            source.crc = CalculateCRC32(0, k_seabios_bios, k_seabios_bios_size);
        }
    }
}

GDOX_Firmware_Source_Type Firmware::GetSourceType(int set, GDOX_Machine_Model model)
{
    u32 bit = 1 << model;

    if (set < 0)
        return GDOX_FIRMWARE_SOURCE_UNKNOWN;

    if ((k_firmware_sets[set].models & bit) != 0)
        return GDOX_FIRMWARE_SOURCE_OWN_SET;

    if ((k_firmware_sets[set].compatible_models & bit) != 0)
        return GDOX_FIRMWARE_SOURCE_COMPATIBLE_SET;

    return GDOX_FIRMWARE_SOURCE_OTHER_SET;
}

int Firmware::FindSet(u32 system_crc)
{
    for (int i = 0; i < FIRMWARE_SET_COUNT; i++)
    {
        if (k_firmware_sets[i].system_crc == system_crc)
            return i;
    }

    return -1;
}

bool Firmware::IdentifySource(GDOX_Firmware_Source& source)
{
    source.crc = 0;
    source.set = -1;
    Container container;

    if (!OpenContainer(source.path, source.zip, container))
        return false;

    bool complete = true;

    for (int i = 0; complete && (i < GDOX_FIRMWARE_COUNT); i++)
    {
        GDOX_Firmware_Type type = (GDOX_Firmware_Type)i;

        if (!IsRequired(type))
            continue;

        for (int part = 0; complete && (part < GetPartCount(type)); part++)
            complete = ReadPart(container, type, part, NULL, NULL, false, NULL);
    }

    if (complete)
    {
        u8* system_rom = new (std::nothrow) u8[GDOX_FIRMWARE_SYSTEM_BIOS_MAX_SIZE];
        u32 size = 0;
        complete = IsValidPointer(system_rom) &&
            ReadPart(container, GDOX_FIRMWARE_SYSTEM_BIOS, 0, system_rom, NULL, false, &size);

        if (complete)
            source.crc = CalculateCRC32(0, system_rom, size);

        SafeDeleteArray(system_rom);
    }

    CloseContainer(container);

    if (complete)
        source.set = FindSet(source.crc);

    return complete;
}

void Firmware::AddCandidate(std::vector<GDOX_Firmware_Source>& candidates, const char* path, bool zip)
{
    candidates.emplace_back();
    GDOX_Firmware_Source& source = candidates.back();
    memset(&source, 0, sizeof(source));
    strncpy_fit(source.path, path, sizeof(source.path));
    source.zip = zip;

    if (!IdentifySource(source))
        candidates.pop_back();
}

bool Firmware::OpenZip(const char* path, MediaFile** file, mz_zip_archive& zip)
{
    memset(&zip, 0, sizeof(zip));
    *file = MediaFile::OpenFile(path);

    if (!IsValidPointer(*file) || !(*file)->CanSeek() || ((*file)->GetSize() <= 0))
    {
        SafeDelete(*file);
        return false;
    }

    zip.m_pRead = ReadZipCallback;
    zip.m_pIO_opaque = *file;

    if (!mz_zip_reader_init(&zip, (mz_uint64)(*file)->GetSize(), 0))
    {
        SafeDelete(*file);
        return false;
    }

    return true;
}

bool Firmware::OpenContainer(const char* path, bool zip, Container& container)
{
    container.zip = zip;
    container.path = path;
    InitPointer(container.zip_file);
    container.listed = false;

    if (zip)
        return OpenZip(path, &container.zip_file, container.archive);

    std::vector<std::string> folders;
    container.listed = MediaFile::ListDirectory(path, folders, container.files);
    std::sort(container.files.begin(), container.files.end());
    return true;
}

void Firmware::CloseContainer(Container& container)
{
    if (!IsValidPointer(container.zip_file))
        return;

    mz_zip_reader_end(&container.archive);
    SafeDelete(container.zip_file);
}

bool Firmware::ReadComponent(Container& container, GDOX_Firmware_Type type, u8* destination, GDOX_Firmware_Info& info, bool report)
{
    std::string path;
    u32 size = 0;

    if (!ReadPart(container, type, 0, destination, &path, report, &size))
        return false;

    strncpy_fit(info.path, path.c_str(), sizeof(info.path));
    info.size = (int)size;
    info.crc = CalculateCRC32(0, destination, size);
    info.loaded = true;
    return true;
}

bool Firmware::ReadPart(Container& container, GDOX_Firmware_Type type, int part, u8* destination, std::string* path, bool report, u32* size)
{
    for (int i = 0; IsValidPointer(GetPartName(type, part, i)); i++)
    {
        if (ReadNamed(container, type, GetPartName(type, part, i), destination, path, report, size))
            return true;
    }

    return false;
}

bool Firmware::ReadNamed(Container& container, GDOX_Firmware_Type type, const char* name, u8* destination, std::string* path, bool report, u32* size)
{
    if (container.zip)
    {
        int index = FindZipEntry(container.archive, name);
        mz_zip_archive_file_stat stat;

        if ((index < 0) || !mz_zip_reader_file_stat(&container.archive, (mz_uint)index, &stat))
            return false;

        if ((stat.m_uncomp_size > 0xFFFFFFFFULL) || !IsValidSize(type, (u32)stat.m_uncomp_size))
        {
            if (report)
                Error("Incorrect firmware size for %s in %s: %llu bytes, expected %s", stat.m_filename, container.path,
                    (unsigned long long)stat.m_uncomp_size, GetSizeText(type));
            return false;
        }

        u32 file_size = (u32)stat.m_uncomp_size;

        if (IsValidPointer(destination) &&
            !mz_zip_reader_extract_to_mem(&container.archive, (mz_uint)index, destination, file_size, 0))
        {
            Error("Unable to read firmware %s in %s", stat.m_filename, container.path);
            return false;
        }

        if (IsValidPointer(path))
        {
            *path = container.path;
            append_path_component(*path, stat.m_filename);
        }

        if (IsValidPointer(size))
            *size = file_size;

        return true;
    }

    std::string file_path = FindFile(container, name);
    MediaFile* file = file_path.empty() ? NULL : MediaFile::OpenFile(file_path.c_str());

    if (!IsValidPointer(file))
        return false;

    s64 actual_size = file->GetSize();
    bool ok = (actual_size > 0) && (actual_size <= 0xFFFFFFFFLL) && IsValidSize(type, (u32)actual_size);

    if (!ok && report)
        Error("Incorrect firmware size for %s: %lld bytes, expected %s", file_path.c_str(), (long long)actual_size,
            GetSizeText(type));

    if (ok && IsValidPointer(destination))
    {
        ok = file->ReadAt(0, destination, (u32)actual_size);

        if (!ok)
            Error("Unable to read firmware file %s", file_path.c_str());
    }

    SafeDelete(file);

    if (ok && IsValidPointer(path))
        *path = file_path;

    if (ok && IsValidPointer(size))
        *size = (u32)actual_size;

    return ok;
}

std::string Firmware::FindFile(const Container& container, const char* name)
{
    std::string upper(name);
    std::string lower(name);
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (container.listed)
    {
        for (size_t i = 0; i < container.files.size(); i++)
        {
            if (strings_equal_ignore_case(container.files[i], upper))
            {
                std::string path(container.path);
                append_path_component(path, container.files[i].c_str());
                return path;
            }
        }

        return "";
    }

    for (int i = 0; i < 2; i++)
    {
        std::string path(container.path);
        append_path_component(path, (i == 0) ? upper.c_str() : lower.c_str());
        MediaFile* file = MediaFile::OpenFile(path.c_str());

        if (IsValidPointer(file))
        {
            SafeDelete(file);
            return path;
        }
    }

    return "";
}

int Firmware::FindZipEntry(mz_zip_archive& zip, const char* name)
{
    for (mz_uint file = 0; file < mz_zip_reader_get_num_files(&zip); file++)
    {
        char file_name[MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE];
        mz_zip_reader_get_filename(&zip, file, file_name, sizeof(file_name));

        if ((strlen(file_name) == strlen(name)) && ends_with_no_case(file_name, name))
            return (int)file;
    }

    return -1;
}

int Firmware::GetPartCount(GDOX_Firmware_Type type)
{
    UNUSED(type);
    return 1;
}

const char* Firmware::GetPartName(GDOX_Firmware_Type type, int part, int index)
{
    UNUSED(part);
    return index < FIRMWARE_FILE_NAMES ? k_firmware_file_names[type][index] : NULL;
}

size_t Firmware::ReadZipCallback(void* opaque, mz_uint64 offset, void* buffer, size_t size)
{
    MediaFile* file = (MediaFile*)opaque;
    return file->ReadAt(offset, buffer, size) ? size : 0;
}
