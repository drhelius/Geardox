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
#include "media_zip.h"
#include "media_file.h"

MediaZip::MediaZip()
{
    memset(&m_zip, 0, sizeof(m_zip));
    InitPointer(m_file);
    InitPointer(m_output);
    InitPointer(m_archive);
    m_output_offset = 0;
    m_archive_size = 0;
    m_reading = false;
    m_writing = false;
}

MediaZip::~MediaZip()
{
    Close();
}

bool MediaZip::Open(const char* path)
{
    Close();
    m_file = MediaFile::OpenFile(path);

    if (!IsValidPointer(m_file))
        return false;

    m_zip.m_pRead = ReadCallback;
    m_zip.m_pIO_opaque = m_file;

    if (!mz_zip_reader_init(&m_zip, (mz_uint64)m_file->GetSize(), 0))
    {
        Close();
        return false;
    }

    m_reading = true;
    return true;
}

bool MediaZip::Open(const void* data, size_t size)
{
    Close();

    if (!IsValidPointer(data) || !mz_zip_reader_init_mem(&m_zip, data, size, 0))
    {
        Close();
        return false;
    }

    m_reading = true;
    return true;
}

void MediaZip::Close()
{
    if (m_reading)
        mz_zip_reader_end(&m_zip);

    if (m_writing)
        mz_zip_writer_end(&m_zip);

    if (IsValidPointer(m_archive))
        mz_free(m_archive);

    SafeDelete(m_file);
    memset(&m_zip, 0, sizeof(m_zip));
    InitPointer(m_output);
    InitPointer(m_archive);
    m_output_offset = 0;
    m_archive_size = 0;
    m_reading = false;
    m_writing = false;
}

bool MediaZip::IsOpen() const
{
    return m_reading;
}

mz_zip_archive* MediaZip::GetArchive()
{
    return &m_zip;
}

bool MediaZip::Extract(mz_uint index, std::string* data)
{
    size_t size = 0;
    void* bytes = m_reading ? mz_zip_reader_extract_to_heap(&m_zip, index, &size, 0) : NULL;
    data->clear();

    if (!IsValidPointer(bytes))
        return false;

    data->assign((const char*)bytes, size);
    mz_free(bytes);
    return true;
}

bool MediaZip::Extract(mz_uint index, std::vector<u8>* data)
{
    size_t size = 0;
    void* bytes = m_reading ? mz_zip_reader_extract_to_heap(&m_zip, index, &size, 0) : NULL;
    data->clear();

    if (!IsValidPointer(bytes))
        return false;

    data->assign((const u8*)bytes, (const u8*)bytes + size);
    mz_free(bytes);
    return true;
}

bool MediaZip::Extract(const char* name, std::string* data)
{
    size_t size = 0;
    void* bytes = m_reading ? mz_zip_reader_extract_file_to_heap(&m_zip, name, &size, 0) : NULL;
    data->clear();

    if (!IsValidPointer(bytes))
        return false;

    data->assign((const char*)bytes, size);
    mz_free(bytes);
    return true;
}

bool MediaZip::ExtractTo(mz_uint index, MediaFile* file, u64 offset)
{
    if (!m_reading || !IsValidPointer(file))
        return false;

    m_output = file;
    m_output_offset = offset;
    bool ok = mz_zip_reader_extract_to_callback(&m_zip, index, WriteCallback, this, 0) != 0;
    InitPointer(m_output);
    return ok;
}

bool MediaZip::ExtractToFile(mz_uint index, const char* path)
{
    std::string temporary = std::string(path) + ".tmp";
    MediaFile* file = MediaFile::OpenNewFile(temporary.c_str());
    bool ok = IsValidPointer(file) && ExtractTo(index, file, 0) && file->Flush();
    SafeDelete(file);
    ok = ok && MediaFile::MoveIntoPlace(temporary.c_str(), path);

    if (!ok)
        MediaFile::RemoveFile(temporary.c_str());

    return ok;
}

bool MediaZip::ExtractAll(const char* folder)
{
    if (!m_reading || !MediaFile::MakeDirectories(folder))
        return false;

    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&m_zip); i++)
    {
        mz_zip_archive_file_stat stat;

        if (!mz_zip_reader_file_stat(&m_zip, i, &stat))
            return false;

        std::string name = path_forward_slashes(stat.m_filename);

        if (!is_safe_zip_entry_name(name.c_str()))
        {
            Log("Skipping an unsafe ZIP entry: %s", stat.m_filename);
            continue;
        }

        std::string path = path_join(folder, name);

        if (stat.m_is_directory)
        {
            if (!MediaFile::MakeDirectories(trim_trailing_separators(path).c_str()))
                return false;

            continue;
        }

        if (!MediaFile::MakeDirectories(path_directory(path).c_str()) || !ExtractToFile(i, path.c_str()))
            return false;

        MediaFile::SetModified(path.c_str(), (s64)stat.m_time);
    }

    return true;
}

bool MediaZip::BeginArchive()
{
    Close();
    m_writing = mz_zip_writer_init_heap(&m_zip, 0, 4 * 1024 * 1024) != 0;
    return m_writing;
}

bool MediaZip::AddEntry(const char* name, const void* data, size_t size, int level, const s64* modified)
{
    if (!m_writing)
        return false;

    MZ_TIME_T time = IsValidPointer(modified) ? (MZ_TIME_T)*modified : 0;
    return mz_zip_writer_add_mem_ex_v2(&m_zip, name, data, size, NULL, 0, (mz_uint)level, 0, 0,
        IsValidPointer(modified) ? &time : NULL, NULL, 0, NULL, 0) != 0;
}

bool MediaZip::FinishArchive()
{
    return m_writing && mz_zip_writer_finalize_heap_archive(&m_zip, &m_archive, &m_archive_size);
}

bool MediaZip::WriteArchive(const char* path)
{
    return m_writing && IsValidPointer(m_archive) && MediaFile::WriteFileAtomic(path, m_archive, m_archive_size);
}

size_t MediaZip::ReadCallback(void* opaque, mz_uint64 offset, void* buffer, size_t size)
{
    MediaFile* file = (MediaFile*)opaque;
    return file->ReadAt(offset, buffer, size) ? size : 0;
}

size_t MediaZip::WriteCallback(void* opaque, mz_uint64 offset, const void* buffer, size_t size)
{
    MediaZip* zip = (MediaZip*)opaque;
    return zip->m_output->WriteAt(zip->m_output_offset + offset, buffer, size) ? size : 0;
}
