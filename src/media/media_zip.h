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

#ifndef MEDIA_ZIP_H
#define MEDIA_ZIP_H

#include <string>
#include <vector>
#include "../common/common.h"

class MediaFile;

class MediaZip
{
public:
    MediaZip();
    ~MediaZip();
    bool Open(const char* path);
    bool Open(const void* data, size_t size);
    void Close();
    bool IsOpen() const;
    mz_zip_archive* GetArchive();
    bool Extract(mz_uint index, std::string* data);
    bool Extract(mz_uint index, std::vector<u8>* data);
    bool Extract(const char* name, std::string* data);
    bool ExtractTo(mz_uint index, MediaFile* file, u64 offset);
    bool ExtractToFile(mz_uint index, const char* path);
    bool BeginArchive();
    bool AddEntry(const char* name, const void* data, size_t size, int level, const s64* modified = NULL);
    bool FinishArchive();
    bool WriteArchive(const char* path);

private:
    MediaZip(const MediaZip&);
    MediaZip& operator=(const MediaZip&);
    static size_t ReadCallback(void* opaque, mz_uint64 offset, void* buffer, size_t size);
    static size_t WriteCallback(void* opaque, mz_uint64 offset, const void* buffer, size_t size);

private:
    mz_zip_archive m_zip;
    MediaFile* m_file;
    MediaFile* m_output;
    u64 m_output_offset;
    void* m_archive;
    size_t m_archive_size;
    bool m_reading;
    bool m_writing;
};

#endif /* MEDIA_ZIP_H */
