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

#ifndef VOODOO_TREX_INLINE_H
#define VOODOO_TREX_INLINE_H

#include "voodoo_trex.h"

INLINE u8* VoodooTrex::GetMemory()
{
    return m_state.memory;
}

INLINE VoodooTrex::VoodooTrex_State* VoodooTrex::GetState()
{
    return &m_state;
}

INLINE u8* VoodooTrex::GetDirtyPages()
{
    return m_dirty_pages;
}

INLINE const VoodooTrex::VoodooTrex_Texture* VoodooTrex::GetTexture()
{
    if (m_texture_dirty)
        UpdateTexture();

    return &m_texture;
}

INLINE s32 VoodooTrex::Log2(double value, int fraction_bits)
{
    static const u8 k_log2_fraction[128] =
    {
        0, 2, 5, 8, 11, 14, 16, 19, 22, 25, 27, 30, 33, 35, 38, 40,
        43, 46, 48, 51, 53, 56, 58, 61, 63, 65, 68, 70, 73, 75, 77, 80,
        82, 84, 87, 89, 91, 93, 96, 98, 100, 102, 104, 106, 109, 111, 113, 115,
        117, 119, 121, 123, 125, 127, 129, 132, 134, 136, 138, 140, 141, 143, 145, 147,
        149, 151, 153, 155, 157, 159, 161, 162, 164, 166, 168, 170, 172, 173, 175, 177,
        179, 181, 182, 184, 186, 188, 189, 191, 193, 194, 196, 198, 200, 201, 203, 205,
        206, 208, 209, 211, 213, 214, 216, 218, 219, 221, 222, 224, 225, 227, 229, 230,
        232, 233, 235, 236, 238, 239, 241, 242, 244, 245, 247, 248, 250, 251, 253, 254
    };

    if (value < 0.0)
        return 0;

    u64 bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    u32 top = (u32)(bits >> 45);
    s32 exponent = (s32)(top >> 7) - 1023 - fraction_bits;

    return (s32)((u32)exponent << 8) | k_log2_fraction[top & 127];
}

INLINE s32 VoodooTrex::GetLodBase(s64 dsdx, s64 dsdy, s64 dtdx, s64 dtdy)
{
    double sx = (double)dsdx;
    double sy = (double)dsdy;
    double tx = (double)dtdx;
    double ty = (double)dtdy;
    double texdx = sx * sx + tx * tx;
    double texdy = sy * sy + ty * ty;

    return Log2(MAX(texdx, texdy), 64) / 2;
}

INLINE u32 VoodooTrex::FetchTexel(const VoodooTrex_Texture& texture, const u8* memory, double iters, double itert,
    double iterw, s32 lod_base, u32 dither, s32& lod)
{
    u32 mode = texture.texture_mode;
    s32 s;
    s32 t;
    lod = lod_base;

    if ((mode & 0x01) != 0)
    {
        double recip = 256.0 / iterw;
        s = (s32)(iters * recip);
        t = (s32)(itert * recip);
        lod -= Log2(iterw, 32);
    }
    else
    {
        s = (s32)(iters * (1.0 / (1 << 24)));
        t = (s32)(itert * (1.0 / (1 << 24)));
    }

    if (((mode & 0x08) != 0) && (iterw < 0.0))
    {
        s = 0;
        t = 0;
    }

    lod += texture.lod_bias;

    if ((mode & 0x10) != 0)
        lod += (s32)(dither << 4);

    lod = CLAMP(lod, texture.lod_min, texture.lod_max);

    s32 ilod = lod >> 8;
    ilod += (s32)((~texture.lod_mask >> ilod) & 1);
    ilod = MIN(ilod, VOODOO_TREX_LODS - 1);

    u32 base = texture.lod_offset[ilod];
    s32 smax = (s32)(texture.width_mask >> ilod);
    s32 tmax = (s32)(texture.height_mask >> ilod);
    const u32* lookup = texture.lookup;
    bool wide = texture.format >= 8;
    bool split = wide && (texture.format != 10) && (texture.format != 11) && (texture.format != 12);
    bool magnify = lod == texture.lod_min;

    if ((magnify && ((mode & 0x04) == 0)) || (!magnify && ((mode & 0x02) == 0)))
    {
        s >>= ilod + 8;
        t >>= ilod + 8;

        if ((mode & 0x40) != 0)
            s = CLAMP(s, 0, smax);

        if ((mode & 0x80) != 0)
            t = CLAMP(t, 0, tmax);

        u32 index = (u32)((t & tmax) * (smax + 1) + (s & smax));

        if (!wide)
            return lookup[memory[(base + index) & (VOODOO_TREX_MEMORY_SIZE - 1)]];

        u32 address = (base + index * 2) & (VOODOO_TREX_MEMORY_SIZE - 2);
        u32 texel = memory[address] | ((u32)memory[address + 1] << 8);

        if (!split)
            return lookup[texel];

        return (lookup[texel & 0xFF] & 0x00FFFFFF) | ((texel & 0xFF00) << 16);
    }

    s >>= ilod;
    t >>= ilod;
    s -= 0x80;
    t -= 0x80;

    u32 sfrac = (u32)s & 0xF0;
    u32 tfrac = (u32)t & 0xF0;
    s >>= 8;
    t >>= 8;
    s32 s1 = s + 1;
    s32 t1 = t + 1;

    if ((mode & 0x40) != 0)
    {
        if (s < 0)
        {
            s = 0;
            s1 = 0;
        }
        else if (s >= smax)
        {
            s = smax;
            s1 = smax;
        }
    }

    if ((mode & 0x80) != 0)
    {
        if (t < 0)
        {
            t = 0;
            t1 = 0;
        }
        else if (t >= tmax)
        {
            t = tmax;
            t1 = tmax;
        }
    }

    s &= smax;
    s1 &= smax;
    t = (t & tmax) * (smax + 1);
    t1 = (t1 & tmax) * (smax + 1);

    u32 texels[4];
    u32 indexes[4] = { (u32)(t + s), (u32)(t + s1), (u32)(t1 + s), (u32)(t1 + s1) };

    for (int i = 0; i < 4; i++)
    {
        if (!wide)
            texels[i] = lookup[memory[(base + indexes[i]) & (VOODOO_TREX_MEMORY_SIZE - 1)]];
        else
        {
            u32 address = (base + indexes[i] * 2) & (VOODOO_TREX_MEMORY_SIZE - 2);
            u32 texel = memory[address] | ((u32)memory[address + 1] << 8);
            texels[i] = !split ? lookup[texel] : (lookup[texel & 0xFF] & 0x00FFFFFF) | ((texel & 0xFF00) << 16);
        }
    }

    u32 rb0 = (texels[0] & 0x00FF00FF) + ((((texels[1] & 0x00FF00FF) - (texels[0] & 0x00FF00FF)) * sfrac) >> 8);
    u32 rb1 = (texels[2] & 0x00FF00FF) + ((((texels[3] & 0x00FF00FF) - (texels[2] & 0x00FF00FF)) * sfrac) >> 8);
    u32 ag0 = ((texels[0] >> 8) & 0x00FF00FF) +
        (((((texels[1] >> 8) & 0x00FF00FF) - ((texels[0] >> 8) & 0x00FF00FF)) * sfrac) >> 8);
    u32 ag1 = ((texels[2] >> 8) & 0x00FF00FF) +
        (((((texels[3] >> 8) & 0x00FF00FF) - ((texels[2] >> 8) & 0x00FF00FF)) * sfrac) >> 8);
    rb0 = (rb0 & 0x00FF00FF) + ((((rb1 & 0x00FF00FF) - (rb0 & 0x00FF00FF)) * tfrac) >> 8);
    ag0 = (ag0 & 0x00FF00FF) + ((((ag1 & 0x00FF00FF) - (ag0 & 0x00FF00FF)) * tfrac) >> 8);

    return ((ag0 << 8) & 0xFF00FF00) | (rb0 & 0x00FF00FF);
}

INLINE u32 VoodooTrex::Combine(const VoodooTrex_Texture& texture, u32 local, u32 other, s32 lod)
{
    u32 mode = texture.texture_mode;
    s32 local_c[4] = { (s32)(local & 0xFF), (s32)((local >> 8) & 0xFF), (s32)((local >> 16) & 0xFF), (s32)(local >> 24) };
    s32 other_c[4] = { (s32)(other & 0xFF), (s32)((other >> 8) & 0xFF), (s32)((other >> 16) & 0xFF), (s32)(other >> 24) };
    s32 detail = 0;

    if (texture.detail_bias > lod)
        detail = MIN((s32)(u8)(((texture.detail_bias - lod) << texture.detail_scale) >> 8), texture.detail_max);

    u32 result = 0;

    for (int i = 0; i < 4; i++)
    {
        bool alpha = i == 3;
        u32 shift = alpha ? 9 : 0;
        s32 color = ((mode >> (12 + shift)) & 0x01) != 0 ? 0 : other_c[i];

        if (((mode >> (13 + shift)) & 0x01) != 0)
            color -= local_c[i];

        s32 factor;

        switch ((mode >> (14 + shift)) & 0x07)
        {
            case 1:
                factor = local_c[i];
                break;
            case 2:
                factor = other_c[3];
                break;
            case 3:
                factor = local_c[3];
                break;
            case 4:
                factor = detail;
                break;
            case 5:
                factor = lod & 0xFF;
                break;
            default:
                factor = 0;
                break;
        }

        if (((mode >> (17 + shift)) & 0x01) == 0)
            factor ^= 0xFF;

        color = (color * (factor + 1)) >> 8;

        u32 add = (mode >> (18 + shift)) & 0x03;

        if (alpha)
            color += add != 0 ? local_c[3] : 0;
        else if (add == 1)
            color += local_c[i];
        else if (add == 2)
            color += local_c[3];

        color = CLAMP(color, 0, 255);

        if (((mode >> (20 + shift)) & 0x01) != 0)
            color ^= 0xFF;

        result |= (u32)color << (i * 8);
    }

    return result;
}

#endif /* VOODOO_TREX_INLINE_H */
