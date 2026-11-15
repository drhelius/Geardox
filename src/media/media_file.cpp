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

#include "media_file.h"
#include "media_file_native.h"
#include "../common/common.h"

#if defined(__LIBRETRO__)
#include "media_file_libretro.h"
#endif

MediaFile::~MediaFile()
{

}

MediaFile* MediaFile::OpenFile(const char* path, bool writable)
{
#if defined(__LIBRETRO__)
    if (MediaFileLibretro::HasVfsInterface())
    {
        MediaFile* file = new MediaFileLibretro;
        if (file->Open(path, writable))
            return file;

        SafeDelete(file);
        return NULL;
    }

    if (MediaFileLibretro::IsCdRomUriPath(path))
        return NULL;
#endif

    MediaFile* file = new MediaFileNative;
    if (file->Open(path, writable))
        return file;

    SafeDelete(file);
    return NULL;
}

MediaFile* MediaFile::OpenNewFile(const char* path)
{
#if defined(__LIBRETRO__)
    if (MediaFileLibretro::HasVfsInterface())
    {
        MediaFile* file = new MediaFileLibretro;

        if (file->Create(path))
            return file;

        SafeDelete(file);
        return NULL;
    }
#endif

    MediaFile* file = new MediaFileNative;

    if (file->Create(path))
        return file;

    SafeDelete(file);
    return NULL;
}

bool MediaFile::ListDirectory(const char* path, std::vector<std::string>& folders, std::vector<std::string>& files)
{
    folders.clear();
    files.clear();

    if (!IsValidPointer(path) || (path[0] == '\0'))
        return false;

#if defined(__LIBRETRO__)
    if (MediaFileLibretro::HasVfsDirectories())
        return MediaFileLibretro::ListDirectory(path, folders, files);
#endif

    return MediaFileNative::ListDirectory(path, folders, files);
}

bool MediaFile::GetFileInfo(const char* path, u64* size, s64* modified)
{
    if (!IsValidPointer(path) || (path[0] == '\0'))
        return false;

#if defined(__LIBRETRO__)
    if (MediaFileLibretro::HasVfsInterface())
    {
        MediaFile* file = OpenFile(path);

        if (!IsValidPointer(file))
            return false;

        *size = (u64)file->GetSize();
        *modified = 0;
        SafeDelete(file);
        return true;
    }
#endif

    return MediaFileNative::GetFileInfo(path, size, modified);
}

bool MediaFile::IsFolderLink(const char* path)
{
    return IsValidPointer(path) && MediaFileNative::IsFolderLink(path);
}

bool MediaFile::RenameFile(const char* from, const char* to)
{
#if defined(__LIBRETRO__)
    if (MediaFileLibretro::HasVfsInterface())
        return MediaFileLibretro::RenameFile(from, to);
#endif

    return rename_file_utf8(from, to);
}

bool MediaFile::RemoveFile(const char* path)
{
#if defined(__LIBRETRO__)
    if (MediaFileLibretro::HasVfsInterface())
        return MediaFileLibretro::RemoveFile(path);
#endif

    return remove_file_utf8(path);
}

bool MediaFile::MakeDirectory(const char* path)
{
#if defined(__LIBRETRO__)
    if (MediaFileLibretro::HasVfsDirectories())
        return MediaFileLibretro::MakeDirectory(path);
#endif

    return MediaFileNative::MakeDirectory(path);
}

bool MediaFile::DeleteDirectory(const char* path)
{
#if defined(__LIBRETRO__)
    if (MediaFileLibretro::HasVfsInterface())
        return MediaFileLibretro::RemoveFile(path);
#endif

    return MediaFileNative::DeleteDirectory(path);
}

// libretro's VFS can't set a time, so there a file keeps the time it was written
bool MediaFile::SetModified(const char* path, s64 modified)
{
#if defined(__LIBRETRO__)
    if (MediaFileLibretro::HasVfsInterface())
        return false;
#endif

    return MediaFileNative::SetModified(path, modified);
}

void MediaFile::SetVfsInterface(const retro_vfs_interface* iface, unsigned version)
{
#if defined(__LIBRETRO__)
    MediaFileLibretro::SetVfsInterface(iface, version);
#else
    (void)iface;
    (void)version;
#endif
}

bool MediaFile::ReadExact(void* buffer, u64 size)
{
    if ((size > (u64)SIZE_MAX) || (!IsValidPointer(buffer) && (size != 0)))
        return false;

    u8* destination = reinterpret_cast<u8*>(buffer);
    u64 total = 0;

    while (total < size)
    {
        s64 read = Read(destination + total, size - total);
        if ((read <= 0) || ((u64)read > (size - total)))
            return false;

        total += (u64)read;
    }

    return true;
}

bool MediaFile::ReadAt(u64 offset, void* buffer, u64 size)
{
    if (!CanSeek() || (offset > (u64)INT64_MAX))
        return false;

    if (!Seek((s64)offset))
        return false;

    return ReadExact(buffer, size);
}

bool MediaFile::WriteAt(u64 offset, const void* buffer, u64 size)
{
    if (!CanSeek() || (offset > (u64)INT64_MAX) || (size > (u64)INT64_MAX) || !IsValidPointer(buffer))
        return false;

    if (!Seek((s64)offset))
        return false;

    return Write(buffer, size) == (s64)size;
}
