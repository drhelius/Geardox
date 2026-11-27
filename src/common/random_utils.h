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

#ifndef RANDOM_UTILS_H
#define RANDOM_UTILS_H

#include <time.h>
#include "types.h"

inline u32 random_u32()
{
    static u32 state = 0;

    if (state == 0)
    {
        u32 seed = (u32)time(NULL) ^ (u32)clock() ^ (u32)(size_t)&state ^ 0x9E3779B9U;
        seed ^= seed >> 16;
        seed *= 0x7FEB352DU;
        seed ^= seed >> 15;
        seed *= 0x846CA68BU;
        seed ^= seed >> 16;
        state = seed != 0 ? seed : 0x5A17C3E9U;
    }

    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

#endif /* RANDOM_UTILS_H */
