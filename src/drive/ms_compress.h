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

#ifndef MS_COMPRESS_H
#define MS_COMPRESS_H

#include <string>
#include "../common/common.h"

bool ms_is_kwaj(const u8* data, size_t size);
bool ms_is_szdd(const u8* data, size_t size);
bool ms_expand_kwaj(const u8* data, size_t size, u32 max_size, std::string* out);
bool ms_expand_szdd(const u8* data, size_t size, u32 max_size, std::string* out);

#endif /* MS_COMPRESS_H */
