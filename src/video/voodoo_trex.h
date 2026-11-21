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

#ifndef VOODOO_TREX_H
#define VOODOO_TREX_H

#include <iostream>
#include "../common/common.h"

#define VOODOO_TREX_MEMORY_SIZE 0x200000
#define VOODOO_TREX_PAGES (VOODOO_TREX_MEMORY_SIZE >> 12)
#define VOODOO_TREX_NCC_WORDS 12
#define VOODOO_TREX_LODS 9

class StateSerializer;

class VoodooTrex
{
public:
    struct VoodooTrex_State
    {
        u32 texture_mode;
        u32 tlod;
        u32 tdetail;
        u32 tex_base[4];
        u32 trex_init[2];
        u32 ncc[2][VOODOO_TREX_NCC_WORDS];
        u32 palette[256];
        s64 start_s;
        s64 start_t;
        s64 start_w;
        s64 dsdx;
        s64 dtdx;
        s64 dwdx;
        s64 dsdy;
        s64 dtdy;
        s64 dwdy;
        u8 memory[VOODOO_TREX_MEMORY_SIZE];
    };

    struct VoodooTrex_Texture
    {
        const u32* lookup;
        u32 texture_mode;
        u32 format;
        u32 lod_offset[VOODOO_TREX_LODS];
        u32 lod_mask;
        u32 width_mask;
        u32 height_mask;
        s32 lod_min;
        s32 lod_max;
        s32 lod_bias;
        s32 detail_max;
        s32 detail_bias;
        s32 detail_scale;
        bool replace;
        bool dynamic_lookup;
    };

public:
    VoodooTrex();
    ~VoodooTrex();
    void Init();
    void Reset();
    void WriteRegister(int reg, u32 value);
    u32 DecodeTextureWrite(u32 offset, u32& value, u32& start, bool& wide);
    const VoodooTrex_Texture* GetTexture();
    u8* GetMemory();
    void SetDirtyTracking(bool enabled);
    u8* GetDirtyPages();
    void ClearDirtyPages();
    VoodooTrex_State* GetState();
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream);

    static s32 GetLodBase(s64 dsdx, s64 dsdy, s64 dtdx, s64 dtdy);
    static s32 Log2(double value, int fraction_bits);
    static u32 FetchTexel(const VoodooTrex_Texture& texture, const u8* memory, double iters, double itert, double iterw,
        s32 lod_base, u32 dither, s32& lod);
    static u32 Combine(const VoodooTrex_Texture& texture, u32 local, u32 other, s32 lod);

private:
    void UpdateTexture();
    void UpdateNCC(int table);
    void BuildLookups();
    void Serialize(StateSerializer& serializer);
    void SanitizeState();

private:
    VoodooTrex_State m_state;
    VoodooTrex_Texture m_texture;
    bool m_texture_dirty;
    bool m_dirty_tracking;
    u8 m_dirty_pages[VOODOO_TREX_PAGES];
    u32 m_lookup_332[256];
    u32 m_lookup_a8[256];
    u32 m_lookup_i8[256];
    u32 m_lookup_ai44[256];
    u32 m_lookup_zero[256];
    u32 m_lookup_ncc[2][256];
    u32 m_lookup_palette[256];
    u32 m_lookup_565[65536];
    u32 m_lookup_1555[65536];
    u32 m_lookup_4444[65536];
};

#include "voodoo_trex_inline.h"

#endif /* VOODOO_TREX_H */
