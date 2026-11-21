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
#include <vector>
#include "glide_import.h"
#include "../media/media_file.h"
#include "../media/media_zip.h"
#include "../media/crc.h"

struct GlideImport_Known
{
    u32 crc;
    const char* name;
};

static const u32 k_glide_max_file_size = 64 * 1024 * 1024;
static const int k_glide_max_depth = 4;
static const char k_glide_overlay[] = "GLIDE2X.OVL";
static const char k_glide_compressed[] = "GLIDE2X.OV_";
static const char k_glide_signature[] = "DOS/4G dynamic link library";
static const char k_glide_module[] = "glide2x";
static const char k_glide_stub[] = "Cannot determine glide port";
static const u8 k_glide_szdd_magic[8] = { 'S', 'Z', 'D', 'D', 0x88, 0xF0, 0x27, 0x33 };

static const GlideImport_Known k_glide_known[] =
{
    { 0xB2268E40, "Driver 2.43 (Orchid Righteous 3D 3.00.00)" },
    { 0xD62F0CC3, "Driver 2.46 (3dfx Voodoo Graphics July 1998)" },
    { 0xA85E0BE1, "Driver 2.48 (3dfx Voodoo Graphics 3.01.00)" }
};

GlideImport::GlideImport()
{
    m_stub = false;
}

GlideImport::~GlideImport()
{
}

const char* GlideImport::GetError() const
{
    return m_error.c_str();
}

const char* GlideImport::GetMessage() const
{
    return m_message.c_str();
}

bool GlideImport::IsOverlay(const std::string& data)
{
    return (data.size() > 64) && (data[0] == 'M') && (data[1] == 'Z') &&
        (data.find(k_glide_signature) != std::string::npos) && (data.find(k_glide_module) != std::string::npos) &&
        !IsStub(data);
}

bool GlideImport::IsStub(const std::string& data)
{
    return data.find(k_glide_stub) != std::string::npos;
}

bool GlideImport::ImportsOverlay(const std::string& data)
{
    return (data.find(k_glide_module) != std::string::npos) && !IsOverlay(data);
}

const char* GlideImport::GetName(const std::string& data)
{
    u32 crc = CalculateCRC32(0, data.data(), data.size());

    for (size_t i = 0; i < sizeof(k_glide_known) / sizeof(k_glide_known[0]); i++)
    {
        if (k_glide_known[i].crc == crc)
            return k_glide_known[i].name;
    }

    return "3dfx Voodoo driver";
}

bool GlideImport::Load(const char* path, std::string* data, std::string* name)
{
    if (!MediaFile::ReadFile(path, k_glide_max_file_size, data) || !IsOverlay(*data))
    {
        data->clear();
        return false;
    }

    if (IsValidPointer(name))
        *name = GetName(*data);

    return true;
}

bool GlideImport::ExpandSZDD(const std::string& data, std::string* out)
{
    if ((data.size() < 14) || (memcmp(data.data(), k_glide_szdd_magic, sizeof(k_glide_szdd_magic)) != 0))
        return false;

    const u8* input = (const u8*)data.data();
    u32 expected = read_u32_le(input + 10);
    u8 window[4096];
    u32 position = 4096 - 16;
    size_t offset = 14;

    if (expected > k_glide_max_file_size)
        return false;

    memset(window, ' ', sizeof(window));
    out->clear();
    out->reserve(expected);

    while ((offset < data.size()) && (out->size() < expected))
    {
        u8 flags = input[offset++];

        for (int bit = 0; (bit < 8) && (offset < data.size()) && (out->size() < expected); bit++)
        {
            if ((flags & (1 << bit)) != 0)
            {
                u8 value = input[offset++];
                out->push_back((char)value);
                window[position] = value;
                position = (position + 1) & 4095;
                continue;
            }

            if (offset + 1 >= data.size())
                return false;

            u32 match = input[offset] | ((u32)(input[offset + 1] & 0xF0) << 4);
            u32 length = (u32)(input[offset + 1] & 0x0F) + 3;
            offset += 2;

            for (u32 i = 0; i < length; i++)
            {
                u8 value = window[(match + i) & 4095];
                out->push_back((char)value);
                window[position] = value;
                position = (position + 1) & 4095;
            }
        }
    }

    return out->size() == expected;
}

void GlideImport::Consider(const std::string& name, const std::string& data)
{
    std::string overlay;

    if (name == k_glide_compressed)
    {
        if (!ExpandSZDD(data, &overlay))
            return;
    }
    else
        overlay = data;

    if (IsStub(overlay))
    {
        m_stub = true;
        return;
    }

    if (m_overlay.empty() && IsOverlay(overlay))
        m_overlay = overlay;
}

bool GlideImport::SearchZip(const u8* data, size_t size, int depth)
{
    MediaZip zip;

    if (!zip.Open(data, size))
        return false;

    for (mz_uint i = 0; m_overlay.empty() && (i < mz_zip_reader_get_num_files(zip.GetArchive())); i++)
    {
        mz_zip_archive_file_stat stat;

        if (!mz_zip_reader_file_stat(zip.GetArchive(), i, &stat) || stat.m_is_directory ||
            (stat.m_uncomp_size > k_glide_max_file_size))
            continue;

        std::string name = to_upper_ascii(path_filename(stat.m_filename));
        bool nested = ends_with_no_case(name, ".zip");

        if (!nested && (name != k_glide_overlay) && (name != k_glide_compressed))
            continue;

        std::string bytes;

        if (!zip.Extract(i, &bytes))
            continue;

        if (nested)
        {
            if (depth < k_glide_max_depth)
                SearchZip((const u8*)bytes.data(), bytes.size(), depth + 1);
        }
        else
            Consider(name, bytes);
    }

    return true;
}

bool GlideImport::SearchFolder(const std::string& path, int depth)
{
    std::vector<std::string> folders;
    std::vector<std::string> files;

    if (!MediaFile::ListDirectory(path.c_str(), folders, files))
        return false;

    std::sort(files.begin(), files.end());
    std::sort(folders.begin(), folders.end());

    for (size_t i = 0; m_overlay.empty() && (i < files.size()); i++)
    {
        std::string name = to_upper_ascii(files[i]);
        std::string data;

        if ((files[i][0] == '.') ||
            (!ends_with_no_case(name, ".zip") && (name != k_glide_overlay) && (name != k_glide_compressed)))
            continue;

        if (!MediaFile::ReadFile(path_join(path, files[i]).c_str(), k_glide_max_file_size, &data))
            continue;

        if (ends_with_no_case(name, ".zip"))
            SearchZip((const u8*)data.data(), data.size(), depth);
        else
            Consider(name, data);
    }

    for (size_t i = 0; m_overlay.empty() && (depth < k_glide_max_depth) && (i < folders.size()); i++)
    {
        if (folders[i][0] != '.')
            SearchFolder(path_join(path, folders[i]), depth + 1);
    }

    return true;
}

GlideImport_Result GlideImport::Import(const char* source, const char* destination)
{
    std::vector<std::string> folders;
    std::vector<std::string> files;
    std::string path(source);
    std::string name = to_upper_ascii(path_filename(path));
    m_overlay.clear();
    m_stub = false;

    if (MediaFile::ListDirectory(source, folders, files))
        SearchFolder(path, 0);
    else
    {
        std::string data;

        if (!MediaFile::ReadFile(path.c_str(), k_glide_max_file_size, &data))
        {
            m_error = "Unable to read " + path;
            return GLIDE_IMPORT_ERROR;
        }

        if (ends_with_no_case(name, ".zip"))
            SearchZip((const u8*)data.data(), data.size(), 0);
        else
            Consider(name == k_glide_compressed ? name : std::string(k_glide_overlay), data);
    }

    if (m_overlay.empty())
    {
        m_error = m_stub ? "Only a stand-in GLIDE2X.OVL was found, it doesn't drive a Voodoo card" :
            "No GLIDE2X.OVL found: choose the 3dfx DOS driver package, its ZIP or the GLIDE2X.OVL file";
        return GLIDE_IMPORT_NOT_FOUND;
    }

    if (!MediaFile::WriteFileAtomic(destination, m_overlay.data(), m_overlay.size()))
    {
        m_error = std::string("Unable to write ") + destination;
        return GLIDE_IMPORT_ERROR;
    }

    m_message = std::string(GetName(m_overlay)) + " imported";
    return GLIDE_IMPORT_DONE;
}
