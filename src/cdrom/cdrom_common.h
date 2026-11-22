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

#ifndef CDROM_COMMON_H
#define CDROM_COMMON_H

#include "../common/common.h"

static const u32 k_cdrom_track_type_size[5] = { 2352, 2048, 2352, 2352, 2336 };
static const u32 k_cdrom_track_type_data_offset[5] = { 16, 0, 16, 24, 8 };
static const char* const k_cdrom_track_type_name[5] = { "AUDIO", "MODE1/2048", "MODE1/2352", "MODE2/2352", "MODE2/2336" };

static const u32 k_cdrom_lead_in = 150;
static const u8 k_cdrom_control_pre_emphasis = 0x01;
static const u8 k_cdrom_control_copy_permitted = 0x02;
static const u8 k_cdrom_control_data = 0x04;
static const u8 k_cdrom_control_four_channels = 0x08;

enum GDOX_CdRomTrackType
{
    GDOX_CDROM_AUDIO_TRACK,
    GDOX_CDROM_DATA_TRACK_MODE1_2048,
    GDOX_CDROM_DATA_TRACK_MODE1_2352,
    GDOX_CDROM_DATA_TRACK_MODE2_2352,
    GDOX_CDROM_DATA_TRACK_MODE2_2336
};

INLINE u32 TrackTypeSectorSize(GDOX_CdRomTrackType type)
{
    return k_cdrom_track_type_size[type];
}

INLINE u32 TrackTypeDataOffset(GDOX_CdRomTrackType type)
{
    return k_cdrom_track_type_data_offset[type];
}

INLINE bool TrackTypeIsMode2(GDOX_CdRomTrackType type)
{
    return (type == GDOX_CDROM_DATA_TRACK_MODE2_2352) || (type == GDOX_CDROM_DATA_TRACK_MODE2_2336);
}

INLINE const char* TrackTypeName(GDOX_CdRomTrackType type)
{
    return k_cdrom_track_type_name[type];
}

struct GDOX_CdRomMSF
{
    u8 minutes;
    u8 seconds;
    u8 frames;
};

INLINE void LbaToMsf(u32 lba, GDOX_CdRomMSF* msf)
{
    msf->minutes = (u8)(lba / 75 / 60);
    msf->seconds = (u8)(lba / 75 % 60);
    msf->frames = (u8)(lba % 75);
}

INLINE u32 MsfToLba(const GDOX_CdRomMSF* msf)
{
    return (msf->minutes * 60 + msf->seconds) * 75 + msf->frames;
}

INLINE u8 DecToBcd(u8 val)
{
    return ((val / 10) << 4) | (val % 10);
}

INLINE void SwapSamples16(s16* buffer, u32 count)
{
    for (u32 i = 0; i < count; i++)
    {
        u16 u = (u16)buffer[i];
        buffer[i] = (s16)((u >> 8) | (u << 8));
    }
}

inline bool IsCdRomUri(const char* path)
{
    return IsValidPointer(path) && (strncmp(path, "cdrom://", 8) == 0);
}

#endif /* CDROM_COMMON_H */