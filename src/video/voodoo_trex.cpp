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

#include "voodoo_trex.h"
#include "voodoo_regs.h"
#include "../common/state_serializer.h"

static INLINE u32 expand_argb(u32 a, u32 r, u32 g, u32 b)
{
    return (a << 24) | (r << 16) | (g << 8) | b;
}

static INLINE u32 expand_3(u32 value)
{
    return (value << 5) | (value << 2) | (value >> 1);
}

static INLINE u32 expand_5(u32 value)
{
    return (value << 3) | (value >> 2);
}

VoodooTrex::VoodooTrex()
{
    memset(&m_state, 0, sizeof(m_state));
    memset(&m_texture, 0, sizeof(m_texture));
    memset(m_dirty_pages, 0, sizeof(m_dirty_pages));
    m_texture_dirty = true;
    m_dirty_tracking = false;
    BuildLookups();
}

VoodooTrex::~VoodooTrex()
{
}

void VoodooTrex::Init()
{
    Reset();
}

void VoodooTrex::Reset()
{
    memset(&m_state, 0, sizeof(m_state));

    for (int i = 0; i < 256; i++)
        m_lookup_palette[i] = 0xFF000000;

    UpdateNCC(0);
    UpdateNCC(1);
    m_texture_dirty = true;

    for (int i = 0; i < VOODOO_TREX_PAGES; i++)
        m_dirty_pages[i] = 1;
}

void VoodooTrex::BuildLookups()
{
    for (u32 i = 0; i < 256; i++)
    {
        u32 r = expand_3((i >> 5) & 0x07);
        u32 g = expand_3((i >> 2) & 0x07);
        u32 b = (i & 0x03) * 0x55;
        u32 a = (i >> 4) * 0x11;
        u32 n = (i & 0x0F) * 0x11;

        m_lookup_332[i] = expand_argb(0xFF, r, g, b);
        m_lookup_a8[i] = expand_argb(i, i, i, i);
        m_lookup_i8[i] = expand_argb(0xFF, i, i, i);
        m_lookup_ai44[i] = expand_argb(a, n, n, n);
        m_lookup_zero[i] = 0;
    }

    for (u32 i = 0; i < 65536; i++)
    {
        m_lookup_565[i] = expand_argb(0xFF, expand_5((i >> 11) & 0x1F), (((i >> 5) & 0x3F) << 2) | (((i >> 5) & 0x3F) >> 4),
            expand_5(i & 0x1F));
        m_lookup_1555[i] = expand_argb((i & 0x8000) != 0 ? 0xFF : 0x00, expand_5((i >> 10) & 0x1F),
            expand_5((i >> 5) & 0x1F), expand_5(i & 0x1F));
        m_lookup_4444[i] = expand_argb(((i >> 12) & 0x0F) * 0x11, ((i >> 8) & 0x0F) * 0x11, ((i >> 4) & 0x0F) * 0x11,
            (i & 0x0F) * 0x11);
    }
}

void VoodooTrex::WriteRegister(int reg, u32 value)
{
    switch (reg)
    {
        case k_voodoo_start_s:
            m_state.start_s = (s64)(s32)value << 14;
            break;
        case k_voodoo_start_t:
            m_state.start_t = (s64)(s32)value << 14;
            break;
        case k_voodoo_start_w:
            m_state.start_w = (s64)(s32)value << 2;
            break;
        case k_voodoo_dsdx:
            m_state.dsdx = (s64)(s32)value << 14;
            break;
        case k_voodoo_dtdx:
            m_state.dtdx = (s64)(s32)value << 14;
            break;
        case k_voodoo_dwdx:
            m_state.dwdx = (s64)(s32)value << 2;
            break;
        case k_voodoo_dsdy:
            m_state.dsdy = (s64)(s32)value << 14;
            break;
        case k_voodoo_dtdy:
            m_state.dtdy = (s64)(s32)value << 14;
            break;
        case k_voodoo_dwdy:
            m_state.dwdy = (s64)(s32)value << 2;
            break;
        case k_voodoo_fstart_s:
            m_state.start_s = voodoo_float_to_s64(value, 32);
            break;
        case k_voodoo_fstart_t:
            m_state.start_t = voodoo_float_to_s64(value, 32);
            break;
        case k_voodoo_fstart_w:
            m_state.start_w = voodoo_float_to_s64(value, 32);
            break;
        case k_voodoo_fdsdx:
            m_state.dsdx = voodoo_float_to_s64(value, 32);
            break;
        case k_voodoo_fdtdx:
            m_state.dtdx = voodoo_float_to_s64(value, 32);
            break;
        case k_voodoo_fdwdx:
            m_state.dwdx = voodoo_float_to_s64(value, 32);
            break;
        case k_voodoo_fdsdy:
            m_state.dsdy = voodoo_float_to_s64(value, 32);
            break;
        case k_voodoo_fdtdy:
            m_state.dtdy = voodoo_float_to_s64(value, 32);
            break;
        case k_voodoo_fdwdy:
            m_state.dwdy = voodoo_float_to_s64(value, 32);
            break;
        case k_voodoo_texture_mode:
            m_state.texture_mode = value;
            m_texture_dirty = true;
            break;
        case k_voodoo_tlod:
            m_state.tlod = value & 0x0FFFFFFF;
            m_texture_dirty = true;
            break;
        case k_voodoo_tdetail:
            m_state.tdetail = value & 0x1FFFF;
            m_texture_dirty = true;
            break;
        case k_voodoo_tex_base_addr:
        case k_voodoo_tex_base_addr_1:
        case k_voodoo_tex_base_addr_2:
        case k_voodoo_tex_base_addr_3_8:
            m_state.tex_base[reg - k_voodoo_tex_base_addr] = value & 0x7FFFF;
            m_texture_dirty = true;
            break;
        case k_voodoo_trex_init0:
        case k_voodoo_trex_init1:
            m_state.trex_init[reg - k_voodoo_trex_init0] = value;
            break;
        default:
            if ((reg >= k_voodoo_ncc_table0) && (reg <= k_voodoo_ncc_table_end))
            {
                int table = reg >= k_voodoo_ncc_table1 ? 1 : 0;
                int word = reg - (table != 0 ? k_voodoo_ncc_table1 : k_voodoo_ncc_table0);

                if ((table == 0) && (word >= 4) && ((value & 0x80000000) != 0))
                {
                    u32 index = ((value >> 23) & 0xFE) | (u32)(word & 0x01);
                    m_state.palette[index] = value & 0x00FFFFFF;
                    m_lookup_palette[index] = 0xFF000000 | m_state.palette[index];
                    break;
                }

                m_state.ncc[table][word] = value;
                UpdateNCC(table);
            }
            break;
    }
}

void VoodooTrex::UpdateNCC(int table)
{
    const u32* regs = m_state.ncc[table];

    for (int index = 0; index < 256; index++)
    {
        int y_index = (index >> 4) & 0x0F;
        s32 y = (s32)((regs[y_index >> 2] >> ((y_index & 0x03) * 8)) & 0xFF);
        u32 i = regs[4 + ((index >> 2) & 0x03)];
        u32 q = regs[8 + (index & 0x03)];
        s32 ir = (s32)(((i >> 18) & 0x1FF) ^ 0x100) - 0x100;
        s32 ig = (s32)(((i >> 9) & 0x1FF) ^ 0x100) - 0x100;
        s32 ib = (s32)((i & 0x1FF) ^ 0x100) - 0x100;
        s32 qr = (s32)(((q >> 18) & 0x1FF) ^ 0x100) - 0x100;
        s32 qg = (s32)(((q >> 9) & 0x1FF) ^ 0x100) - 0x100;
        s32 qb = (s32)((q & 0x1FF) ^ 0x100) - 0x100;
        s32 r = CLAMP(y + ir + qr, 0, 255);
        s32 g = CLAMP(y + ig + qg, 0, 255);
        s32 b = CLAMP(y + ib + qb, 0, 255);

        m_lookup_ncc[table][index] = expand_argb(0xFF, (u32)r, (u32)g, (u32)b);
    }
}

void VoodooTrex::UpdateTexture()
{
    u32 mode = m_state.texture_mode;
    u32 tlod = m_state.tlod;
    u32 format = (mode >> 8) & 0x0F;
    u32 aspect = (tlod >> 21) & 0x03;
    int ncc = (mode >> 5) & 0x01;

    m_texture_dirty = false;
    m_texture.texture_mode = mode;
    m_texture.format = format;
    m_texture.lod_min = (s32)((tlod & 0x3F) << 6);
    m_texture.lod_max = (s32)MIN(((tlod >> 6) & 0x3F) << 6, 8U << 8);
    m_texture.lod_bias = (s32)(s8)(((tlod >> 12) & 0x3F) << 2) * 16;
    m_texture.lod_mask = 0x1FF;

    if ((tlod & 0x80000) != 0)
        m_texture.lod_mask = (tlod & 0x40000) != 0 ? 0x0AA : 0x155;

    m_texture.width_mask = 0xFF;
    m_texture.height_mask = 0xFF;

    if ((tlod & 0x100000) != 0)
        m_texture.height_mask >>= aspect;
    else
        m_texture.width_mask >>= aspect;

    switch (format)
    {
        case 0:
        case 8:
            m_texture.lookup = m_lookup_332;
            break;
        case 1:
        case 9:
            m_texture.lookup = m_lookup_ncc[ncc];
            break;
        case 2:
            m_texture.lookup = m_lookup_a8;
            break;
        case 3:
        case 13:
            m_texture.lookup = m_lookup_i8;
            break;
        case 4:
            m_texture.lookup = m_lookup_ai44;
            break;
        case 5:
        case 14:
            m_texture.lookup = m_lookup_palette;
            break;
        case 10:
            m_texture.lookup = m_lookup_565;
            break;
        case 11:
            m_texture.lookup = m_lookup_1555;
            break;
        case 12:
            m_texture.lookup = m_lookup_4444;
            break;
        default:
            m_texture.lookup = m_lookup_zero;
            break;
    }

    u32 shift = format >= 8 ? 1 : 0;
    u32 mask = VOODOO_TREX_MEMORY_SIZE - 1;
    u32 base = m_state.tex_base[0] << 3;

    m_texture.lod_offset[0] = base & mask;

    if ((tlod & 0x1000000) != 0)
    {
        m_texture.lod_offset[1] = (m_state.tex_base[1] << 3) & mask;
        m_texture.lod_offset[2] = (m_state.tex_base[2] << 3) & mask;
        base = m_state.tex_base[3] << 3;
        m_texture.lod_offset[3] = base & mask;
    }
    else
    {
        for (int lod = 1; lod <= 3; lod++)
        {
            if ((m_texture.lod_mask & (1U << (lod - 1))) != 0)
                base += (((m_texture.width_mask >> (lod - 1)) + 1) * ((m_texture.height_mask >> (lod - 1)) + 1)) << shift;

            m_texture.lod_offset[lod] = base & mask;
        }
    }

    for (int lod = 4; lod < VOODOO_TREX_LODS; lod++)
    {
        if ((m_texture.lod_mask & (1U << (lod - 1))) != 0)
        {
            u32 size = ((m_texture.width_mask >> (lod - 1)) + 1) * ((m_texture.height_mask >> (lod - 1)) + 1);
            base += MAX(size, 4U) << shift;
        }

        m_texture.lod_offset[lod] = base & mask;
    }

    m_texture.replace = ((mode & 0x001C3000) == 0x00041000) && ((mode & 0x20600000) == 0x00200000) &&
        ((mode & 0x18000000) != 0);
    m_texture.dynamic_lookup = (format == 1) || (format == 5) || (format == 9) || (format == 14);

    u32 detail = m_state.tdetail;
    m_texture.detail_max = (s32)(detail & 0xFF);
    m_texture.detail_bias = (s32)(s8)(((detail >> 8) & 0x3F) << 2) * 64;
    m_texture.detail_scale = (s32)((detail >> 14) & 0x07);
}

u32 VoodooTrex::DecodeTextureWrite(u32 offset, u32& value, u32& start, bool& wide)
{
    u32 tlod = m_state.tlod;

    if ((tlod & 0x2000000) != 0)
        value = ((value >> 24) & 0xFF) | ((value >> 8) & 0xFF00) | ((value << 8) & 0xFF0000) | (value << 24);

    if ((tlod & 0x4000000) != 0)
        value = (value >> 16) | (value << 16);

    const VoodooTrex_Texture* texture = GetTexture();
    u32 lod = (offset >> 17) & 0x0F;

    if (lod >= VOODOO_TREX_LODS)
        return 0;

    wide = texture->format >= 8;
    u32 t = (offset >> 9) & 0xFF;
    u32 s = wide || ((m_state.texture_mode & 0x80000000) == 0) ? (offset >> 1) & 0xFE : offset & 0xFC;
    u32 width = (texture->width_mask >> lod) + 1;
    u32 index = t * width + s;

    start = texture->lod_offset[lod] + (wide ? index * 2 : index);
    return MIN(wide ? 2U : 4U, width);
}

void VoodooTrex::SetDirtyTracking(bool enabled)
{
    if (enabled && !m_dirty_tracking)
        memset(m_dirty_pages, 1, sizeof(m_dirty_pages));

    m_dirty_tracking = enabled;
}

void VoodooTrex::ClearDirtyPages()
{
    memset(m_dirty_pages, 0, sizeof(m_dirty_pages));
}

void VoodooTrex::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
}

void VoodooTrex::LoadState(std::istream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
    SanitizeState();
}

void VoodooTrex::Serialize(StateSerializer& serializer)
{
    G_SERIALIZE(serializer, m_state.texture_mode);
    G_SERIALIZE(serializer, m_state.tlod);
    G_SERIALIZE(serializer, m_state.tdetail);
    G_SERIALIZE_ARRAY(serializer, m_state.tex_base, 4);
    G_SERIALIZE_ARRAY(serializer, m_state.trex_init, 2);
    G_SERIALIZE_ARRAY(serializer, &m_state.ncc[0][0], 2 * VOODOO_TREX_NCC_WORDS);
    G_SERIALIZE_ARRAY(serializer, m_state.palette, 256);
    G_SERIALIZE(serializer, m_state.start_s);
    G_SERIALIZE(serializer, m_state.start_t);
    G_SERIALIZE(serializer, m_state.start_w);
    G_SERIALIZE(serializer, m_state.dsdx);
    G_SERIALIZE(serializer, m_state.dtdx);
    G_SERIALIZE(serializer, m_state.dwdx);
    G_SERIALIZE(serializer, m_state.dsdy);
    G_SERIALIZE(serializer, m_state.dtdy);
    G_SERIALIZE(serializer, m_state.dwdy);
    G_SERIALIZE_ARRAY(serializer, m_state.memory, VOODOO_TREX_MEMORY_SIZE);
}

void VoodooTrex::SanitizeState()
{
    m_state.tlod &= 0x0FFFFFFF;
    m_state.tdetail &= 0x1FFFF;

    for (int i = 0; i < 4; i++)
        m_state.tex_base[i] &= 0x7FFFF;

    for (int i = 0; i < 256; i++)
    {
        m_state.palette[i] &= 0x00FFFFFF;
        m_lookup_palette[i] = 0xFF000000 | m_state.palette[i];
    }

    UpdateNCC(0);
    UpdateNCC(1);
    m_texture_dirty = true;
}
