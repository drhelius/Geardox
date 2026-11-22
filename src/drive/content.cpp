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

#include "content.h"
#include "floppy_set.h"
#include "hard_disk_image.h"
#include "../media/media_file.h"

GDOX_Content Content::Classify(const char* path)
{
    if (ends_with_no_case(path, ".m3u"))
        return GDOX_CONTENT_PLAYLIST;

    if (MediaFile::IsDirectory(path))
        return GDOX_CONTENT_HARD_DISK_SOURCE;

    if (HardDiskImage::IsImage(path))
        return GDOX_CONTENT_HARD_DISK;

    if (FloppySet::IsFloppyFile(path))
        return GDOX_CONTENT_FLOPPY;

    if (ends_with_no_case(path, ".zip") && MediaFile::Exists(path))
        return GDOX_CONTENT_HARD_DISK_SOURCE;

    return GDOX_CONTENT_MEDIA;
}
