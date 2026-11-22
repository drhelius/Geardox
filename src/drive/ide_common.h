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

#ifndef IDE_COMMON_H
#define IDE_COMMON_H

#include "../common/common.h"

inline void ide_pack_string(u16* words, const char* text, int word_count)
{
    for (int i = 0; i < word_count; i++)
        words[i] = (u16)((text[i * 2] << 8) | text[(i * 2) + 1]);
}

#endif /* IDE_COMMON_H */
