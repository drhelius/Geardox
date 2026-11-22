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

#ifndef VIDEO_COLOR_H
#define VIDEO_COLOR_H

#include "../common/common.h"

INLINE u32 expand_3(u32 value)
{
    return (value << 5) | (value << 2) | (value >> 1);
}

INLINE u32 expand_5(u32 value)
{
    return (value << 3) | (value >> 2);
}

INLINE u32 expand_6(u32 value)
{
    return (value << 2) | (value >> 4);
}

INLINE u32 pack_argb(u32 a, u32 r, u32 g, u32 b)
{
    return (a << 24) | (r << 16) | (g << 8) | b;
}

INLINE u32 pack_xrgb(u32 r, u32 g, u32 b)
{
    return 0xFF000000U | (r << 16) | (g << 8) | b;
}

INLINE u32 color_a(u32 color)
{
    return color >> 24;
}

INLINE u32 color_r(u32 color)
{
    return (color >> 16) & 0xFF;
}

INLINE u32 color_g(u32 color)
{
    return (color >> 8) & 0xFF;
}

INLINE u32 color_b(u32 color)
{
    return color & 0xFF;
}

#endif /* VIDEO_COLOR_H */
