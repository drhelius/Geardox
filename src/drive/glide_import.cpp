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
    { 0xD62F0CC3, "Driver 2.46 (3Dfx Voodoo Graphics July 1998)" },
    { 0xA85E0BE1, "Driver 2.48 (3Dfx Voodoo Graphics 3.01.00)" }
};

static std::string upper(const std::string& text)
{
    std::string result(text);

    for (size_t i = 0; i < result.size(); i++)
        result[i] = ((result[i] >= 'a') && (result[i] <= 'z')) ? (char)(result[i] - 32) : result[i];

    return result;
}

static std::string join(const std::string& directory, const std::string& name)
{
    if (directory.empty())
        return name;

    char last = directory[directory.size() - 1];
    return ((last == '/') || (last == '\\')) ? directory + name : directory + "/" + name;
}

static std::string file_of(const std::string& path)
{
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

static bool ends_with(const std::string& text, const char* suffix)
{
    size_t length = strlen(suffix);
    return (text.size() >= length) && (text.compare(text.size() - length, length, suffix) == 0);
}

static bool read_file(const std::string& path, std::string* data)
{
    u64 size = 0;
    s64 modified = 0;
    data->clear();

    if (!MediaFile::GetFileInfo(path.c_str(), &size, &modified) || (size == 0) || (size > k_glide_max_file_size))
        return false;

    MediaFile* file = MediaFile::OpenFile(path.c_str());

    if (!IsValidPointer(file))
        return false;

    data->resize((size_t)size);
    bool ok = file->ReadAt(0, &(*data)[0], size);
    SafeDelete(file);
    return ok;
}

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
    u32 crc = (u32)mz_crc32(0, (const u8*)data.data(), data.size());

    for (size_t i = 0; i < sizeof(k_glide_known) / sizeof(k_glide_known[0]); i++)
    {
        if (k_glide_known[i].crc == crc)
            return k_glide_known[i].name;
    }

    return "3Dfx Voodoo driver";
}

bool GlideImport::Load(const char* path, std::string* data, std::string* name)
{
    if (!read_file(path, data) || !IsOverlay(*data))
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
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_mem(&zip, data, size, 0))
        return false;

    for (mz_uint i = 0; m_overlay.empty() && (i < mz_zip_reader_get_num_files(&zip)); i++)
    {
        mz_zip_archive_file_stat stat;

        if (!mz_zip_reader_file_stat(&zip, i, &stat) || stat.m_is_directory || (stat.m_uncomp_size > k_glide_max_file_size))
            continue;

        std::string name = upper(file_of(stat.m_filename));
        bool nested = ends_with(name, ".ZIP");

        if (!nested && (name != k_glide_overlay) && (name != k_glide_compressed))
            continue;

        size_t extracted = 0;
        void* bytes = mz_zip_reader_extract_to_heap(&zip, i, &extracted, 0);

        if (!IsValidPointer(bytes))
            continue;

        if (nested)
        {
            if (depth < k_glide_max_depth)
                SearchZip((const u8*)bytes, extracted, depth + 1);
        }
        else
            Consider(name, std::string((const char*)bytes, extracted));

        mz_free(bytes);
    }

    mz_zip_reader_end(&zip);
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
        std::string name = upper(files[i]);
        std::string data;

        if ((files[i][0] == '.') || (!ends_with(name, ".ZIP") && (name != k_glide_overlay) && (name != k_glide_compressed)))
            continue;

        if (!read_file(join(path, files[i]), &data))
            continue;

        if (ends_with(name, ".ZIP"))
            SearchZip((const u8*)data.data(), data.size(), depth);
        else
            Consider(name, data);
    }

    for (size_t i = 0; m_overlay.empty() && (depth < k_glide_max_depth) && (i < folders.size()); i++)
    {
        if (folders[i][0] != '.')
            SearchFolder(join(path, folders[i]), depth + 1);
    }

    return true;
}

GlideImport_Result GlideImport::Import(const char* source, const char* destination)
{
    std::vector<std::string> folders;
    std::vector<std::string> files;
    std::string path(source);
    std::string name = upper(file_of(path));
    m_overlay.clear();
    m_stub = false;

    if (MediaFile::ListDirectory(source, folders, files))
        SearchFolder(path, 0);
    else
    {
        std::string data;

        if (!read_file(path, &data))
        {
            m_error = "Unable to read " + path;
            return GLIDE_IMPORT_ERROR;
        }

        if (ends_with(name, ".ZIP"))
            SearchZip((const u8*)data.data(), data.size(), 0);
        else
            Consider(name == k_glide_compressed ? name : std::string(k_glide_overlay), data);
    }

    if (m_overlay.empty())
    {
        m_error = m_stub ? "Only a stand-in GLIDE2X.OVL was found, it doesn't drive a Voodoo card" :
            "No GLIDE2X.OVL found: choose the 3Dfx DOS driver package, its ZIP or the GLIDE2X.OVL file";
        return GLIDE_IMPORT_NOT_FOUND;
    }

    MediaFile* file = MediaFile::OpenNewFile(destination);

    if (!IsValidPointer(file) || (file->Write(m_overlay.data(), m_overlay.size()) != (s64)m_overlay.size()) || !file->Flush())
    {
        SafeDelete(file);
        MediaFile::RemoveFile(destination);
        m_error = std::string("Unable to write ") + destination;
        return GLIDE_IMPORT_ERROR;
    }

    SafeDelete(file);
    m_message = std::string(GetName(m_overlay)) + " imported";
    return GLIDE_IMPORT_DONE;
}
