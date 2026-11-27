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

#include "net_crc.h"

static u32 crc_table[256];
static bool crc_table_ready = false;

static void build_crc_table()
{
    for (u32 i = 0; i < 256; i++)
    {
        u32 crc = i;

        for (int bit = 0; bit < 8; bit++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320 : crc >> 1;

        crc_table[i] = crc;
    }

    crc_table_ready = true;
}

u32 net_crc32(const u8* data, int size)
{
    if (!crc_table_ready)
        build_crc_table();

    u32 crc = 0xFFFFFFFF;

    for (int i = 0; i < size; i++)
        crc = crc_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);

    return ~crc;
}

void net_crc_append(u8* frame, int size)
{
    u32 crc = net_crc32(frame, size);
    frame[size] = (u8)crc;
    frame[size + 1] = (u8)(crc >> 8);
    frame[size + 2] = (u8)(crc >> 16);
    frame[size + 3] = (u8)(crc >> 24);
}

bool net_crc_check(const u8* frame, int size)
{
    if (size < 4)
        return false;

    u32 crc = net_crc32(frame, size - 4);
    const u8* fcs = frame + size - 4;
    return crc == (u32)(fcs[0] | (fcs[1] << 8) | (fcs[2] << 16) | ((u32)fcs[3] << 24));
}

u32 net_crc_hash(const u8* address)
{
    u32 crc = 0xFFFFFFFF;

    for (int i = 0; i < 6; i++)
    {
        u8 value = address[i];

        for (int bit = 0; bit < 8; bit++)
        {
            u32 carry = (crc >> 31) ^ ((value >> bit) & 1);
            crc <<= 1;

            if (carry)
                crc = (crc ^ 0x04C11DB6) | carry;
        }
    }

    return (crc >> 26) & 0x3F;
}

u16 net_checksum(const u8* data, int size, u32 sum)
{
    for (int i = 0; i + 1 < size; i += 2)
        sum += (u32)((data[i] << 8) | data[i + 1]);

    if (size & 1)
        sum += (u32)(data[size - 1] << 8);

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return (u16)~sum;
}
