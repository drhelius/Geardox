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

#ifndef NET_CRC_H
#define NET_CRC_H

#include "../common/common.h"

u32 net_crc32(const u8* data, int size);
void net_crc_append(u8* frame, int size);
bool net_crc_check(const u8* frame, int size);
u32 net_crc_hash(const u8* address);
u16 net_checksum(const u8* data, int size, u32 sum = 0);

#endif /* NET_CRC_H */
