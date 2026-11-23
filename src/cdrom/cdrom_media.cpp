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

#include <ctype.h>
#include "cdrom_media.h"
#include "../drive/iso_image.h"
#include "cdrom_cuebin_image.h"
#include "cdrom_chd_image.h"
#include "../media/media_file.h"
#if defined(GDOX_ENABLE_PHYSICAL_CDROM)
#include "cdrom_physical_image.h"
#endif

CdRomMedia::CdRomMedia()
{
    InitPointer(m_current_image);
    InitPointer(m_cue_bin_image);
    InitPointer(m_chd_image);
#if defined(GDOX_ENABLE_PHYSICAL_CDROM)
    InitPointer(m_physical_image);
#endif
}

CdRomMedia::~CdRomMedia()
{
    SafeDelete(m_cue_bin_image);
    SafeDelete(m_chd_image);
#if defined(GDOX_ENABLE_PHYSICAL_CDROM)
    SafeDelete(m_physical_image);
#endif
}

void CdRomMedia::Init()
{
    m_cue_bin_image = new CdRomCueBinImage();
    m_cue_bin_image->Init();

    m_chd_image = new CdRomChdImage();
    m_chd_image->Init();

#if defined(GDOX_ENABLE_PHYSICAL_CDROM)
    m_physical_image = new CdRomPhysicalImage();
    m_physical_image->Init();
#endif

    Reset();
}

void CdRomMedia::Reset()
{
    InitPointer(m_current_image);

    m_cue_bin_image->Reset();
    m_chd_image->Reset();
#if defined(GDOX_ENABLE_PHYSICAL_CDROM)
    m_physical_image->Reset();
#endif
}

bool CdRomMedia::LoadFromFile(const char* path, bool preload)
{
    if (MediaFile::IsDirectory(path))
        return LoadFromFolder(path, NULL, preload);

    if (ends_with_no_case(path, ".chd"))
        return LoadChdFromFile(path, preload);

    if (ends_with_no_case(path, ".iso"))
        return LoadIsoFromFile(path, preload);

    if (ends_with_no_case(path, ".cue"))
        return LoadCueFromFile(path, preload);

    return LoadProbedFromFile(path, ProbeFile(path), preload);
}

bool CdRomMedia::LoadFromFolder(const char* path, const char* label, bool preload)
{
    m_folder = trim_trailing_separators(path);
    m_folder_label = IsValidPointer(label) ? label : "";
    std::string cue = "FILE \"" + path_filename(m_folder) + ".iso\" BINARY\nTRACK 01 MODE1/2048\nINDEX 01 00:00:00\n";
    Log("Loading a CD from the folder %s...", m_folder.c_str());

    if (!m_cue_bin_image->LoadFromCueData(m_folder.c_str(), (const u8*)cue.c_str(), cue.size(), preload, ResolveFolder,
        this))
        return false;

    m_current_image = m_cue_bin_image;
    return true;
}

MediaFile* CdRomMedia::ResolveFolder(const char* reference, char* resolved_path, size_t resolved_size, void* user_data)
{
    UNUSED(reference);
    CdRomMedia* media = (CdRomMedia*)user_data;
    strncpy_fit(resolved_path, media->m_folder.c_str(), resolved_size);
    IsoImageFile* file = new IsoImageFile();

    if (!file->Load(media->m_folder.c_str(), media->m_folder_label.c_str()))
        SafeDelete(file);

    return file;
}

bool CdRomMedia::LoadCueFromFile(const char* path, bool preload)
{
    bool cdrom_uri_path = IsCdRomUri(path);
    GDOX_CdRomCueBinLoadOptions options = cdrom_uri_path ?
        GDOX_CdRomCueBinStreamingLoadOptions() : GDOX_CdRomCueBinDefaultLoadOptions();

    m_cue_bin_image->SetLoadOptions(options);

    if (m_cue_bin_image->LoadFromFile(path, preload))
    {
        m_current_image = m_cue_bin_image;
        return true;
    }
    else
    {
        Error("Failed to load CUE file from %s", path);
        Reset();
        return false;
    }
}

bool CdRomMedia::LoadChdFromFile(const char* path, bool preload)
{
    if (m_chd_image->LoadFromFile(path, preload))
    {
        m_current_image = m_chd_image;
        return true;
    }
    else
    {
        Error("Failed to load CHD file from %s", path);
        Reset();
        return false;
    }
}

bool CdRomMedia::LoadIsoFromFile(const char* path, bool preload)
{
    m_cue_bin_image->SetLoadOptions(GDOX_CdRomCueBinDefaultLoadOptions());

    if (m_cue_bin_image->LoadFromIsoFile(path, preload))
    {
        m_current_image = m_cue_bin_image;
        return true;
    }
    else
    {
        Error("Failed to load ISO file from %s", path);
        Reset();
        return false;
    }
}

bool CdRomMedia::LoadRawFromFile(const char* path, bool mode2, bool preload)
{
    m_cue_bin_image->SetLoadOptions(GDOX_CdRomCueBinDefaultLoadOptions());

    if (m_cue_bin_image->LoadFromRawFile(path, mode2, preload))
    {
        m_current_image = m_cue_bin_image;
        return true;
    }
    else
    {
        Error("Failed to load raw CD image from %s", path);
        Reset();
        return false;
    }
}

bool CdRomMedia::LoadProbedFromFile(const char* path, GDOX_CdRomFileKind kind, bool preload)
{
    switch (kind)
    {
        case GDOX_CDROM_FILE_ISO:
            return LoadIsoFromFile(path, preload);
        case GDOX_CDROM_FILE_RAW_MODE1:
            return LoadRawFromFile(path, false, preload);
        case GDOX_CDROM_FILE_RAW_MODE2:
            return LoadRawFromFile(path, true, preload);
        default:
            return LoadCueFromFile(path, preload);
    }
}

static bool LineStartsWithWord(const char* line, const char* word)
{
    while ((*line == ' ') || (*line == '\t'))
        line++;

    size_t length = strlen(word);

    for (size_t i = 0; i < length; i++)
    {
        if (to_upper_ascii(line[i]) != word[i])
            return false;
    }

    return (line[length] == ' ') || (line[length] == '\t');
}

static bool IsCueText(const u8* data, size_t size)
{
    if (memchr(data, 0, size) != NULL)
        return false;

    std::string text((const char*)data, size);
    bool file = false;
    bool track = false;
    size_t position = 0;

    while (position < text.size())
    {
        size_t end = text.find_first_of("\r\n", position);

        if (end == std::string::npos)
            end = text.size();

        std::string line = text.substr(position, end - position);
        position = end + 1;

        if (LineStartsWithWord(line.c_str(), "FILE"))
            file = true;
        else if (LineStartsWithWord(line.c_str(), "TRACK"))
            track = true;
    }

    return file && track;
}

static bool HasVolumeDescriptor(MediaFile* file, u64 offset)
{
    u8 descriptor[6];

    return file->ReadAt(offset, descriptor, sizeof(descriptor)) && (descriptor[0] == 0x01) &&
        (memcmp(descriptor + 1, "CD001", 5) == 0);
}

GDOX_CdRomFileKind CdRomMedia::ProbeFile(const char* path)
{
    MediaFile* file = MediaFile::OpenFile(path);

    if (!IsValidPointer(file))
        return GDOX_CDROM_FILE_UNKNOWN;

    GDOX_CdRomFileKind kind = GDOX_CDROM_FILE_UNKNOWN;
    s64 size = file->GetSize();

    if ((size > 0) && (size <= 65536))
    {
        u8* text = new u8[(size_t)size];

        if (file->ReadAt(0, text, (u64)size) && IsCueText(text, (size_t)size))
            kind = GDOX_CDROM_FILE_CUE;

        SafeDeleteArray(text);
    }

    if (kind == GDOX_CDROM_FILE_UNKNOWN)
    {
        if (HasVolumeDescriptor(file, 16 * 2048))
            kind = GDOX_CDROM_FILE_ISO;
        else if (HasVolumeDescriptor(file, (16 * 2352) + 16))
            kind = GDOX_CDROM_FILE_RAW_MODE1;
        else if (HasVolumeDescriptor(file, (16 * 2352) + 24))
            kind = GDOX_CDROM_FILE_RAW_MODE2;
    }

    SafeDelete(file);
    return kind;
}

#if defined(GDOX_ENABLE_PHYSICAL_CDROM)
bool CdRomMedia::LoadPhysicalDrive(const char* device_id, bool preload)
{
    if (m_physical_image->LoadFromDevice(device_id, preload))
    {
        m_current_image = m_physical_image;
        return true;
    }
    else
    {
        Error("Failed to load physical CD-ROM from %s", device_id);
        Reset();
        return false;
    }
}

bool CdRomMedia::HasPhysicalDriveError()
{
    return IsValidPointer(m_physical_image) && m_physical_image->HasDiscError();
}
#endif

bool CdRomMedia::ReadSector(u32 lba, u8* buffer)
{
    if (IsValidPointer(m_current_image))
    {
        return m_current_image->ReadSector(lba, buffer);
    }
    else
    {
        Error("ReadSector failed - Current image is NULL");
        return false;
    }
}

bool CdRomMedia::ReadRawSector2352(u32 lba, u8* buffer)
{
    if (IsValidPointer(m_current_image))
    {
        return m_current_image->ReadRawSector2352(lba, buffer);
    }
    else
    {
        Error("ReadRawSector2352 failed - Current image is NULL");
        return false;
    }
}

bool CdRomMedia::ReadSamples(u32 lba, u32 offset, s16* buffer, u32 count)
{
    if (IsValidPointer(m_current_image))
    {
        return m_current_image->ReadSamples(lba, offset, buffer, count);
    }
    else
    {
        Error("ReadBytes failed - Current image is NULL");
        return false;
    }
}

bool CdRomMedia::PreloadTrack(u32 track_number, u32 lba)
{
    if (IsValidPointer(m_current_image))
    {
        return m_current_image->PreloadTrack(track_number, lba);
    }
    else
    {
        Error("PreloadTrack failed - Current image is NULL");
        return false;
    }
}

s32 CdRomMedia::FindTrackFromLBA(u32 lba, bool include_lead_in)
{
    if (IsValidPointer(m_current_image))
        return m_current_image->FindTrackFromLBA(lba, include_lead_in);

    return -1;
}
