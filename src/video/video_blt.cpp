/*
 * Geardox - MS-DOS PC/AT Emulator
 * Copyright (C) 2026  Ignacio Sanchez
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/
 *
 */

#include "video.h"
#include "../common/trace_logger.h"
#include "../system/scheduler.h"

static const s8 k_video_mmio_registers[0x1E] =
{
    0x00, 0x10, 0x12, 0x14, 0x01, 0x11, 0x13, 0x15, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
    0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, -1, 0x32, 0x33, 0x34, 0x35
};

static INLINE u8 BltRop(u8 rop, u8 s, u8 d)
{
    switch (rop)
    {
        case 0x00: return 0x00;
        case 0x05: return s & d;
        case 0x06: return d;
        case 0x09: return s & (u8)~d;
        case 0x0B: return (u8)~d;
        case 0x0D: return s;
        case 0x0E: return 0xFF;
        case 0x50: return (u8)~s & d;
        case 0x59: return s ^ d;
        case 0x6D: return s | d;
        case 0x90: return (u8)~(s | d);
        case 0x95: return (u8)~(s ^ d);
        case 0xAD: return s | (u8)~d;
        case 0xD0: return (u8)~s;
        case 0xD6: return (u8)~s | d;
        case 0xDA: return (u8)~(s & d);
        default: return d;
    }
}

template <u8 k_rop>
static void BltSpan(u8* destination, const u8* source, u32 count)
{
    for (u32 i = 0; i < count; i++)
        destination[i] = BltRop(k_rop, source[i], destination[i]);
}

static void BltSpanRop(u8 rop, u8* destination, const u8* source, u32 count)
{
    switch (rop)
    {
        case 0x00: memset(destination, 0x00, count); break;
        case 0x05: BltSpan<0x05>(destination, source, count); break;
        case 0x06: break;
        case 0x09: BltSpan<0x09>(destination, source, count); break;
        case 0x0B: BltSpan<0x0B>(destination, source, count); break;
        case 0x0D: memcpy(destination, source, count); break;
        case 0x0E: memset(destination, 0xFF, count); break;
        case 0x50: BltSpan<0x50>(destination, source, count); break;
        case 0x59: BltSpan<0x59>(destination, source, count); break;
        case 0x6D: BltSpan<0x6D>(destination, source, count); break;
        case 0x90: BltSpan<0x90>(destination, source, count); break;
        case 0x95: BltSpan<0x95>(destination, source, count); break;
        case 0xAD: BltSpan<0xAD>(destination, source, count); break;
        case 0xD0: BltSpan<0xD0>(destination, source, count); break;
        case 0xD6: BltSpan<0xD6>(destination, source, count); break;
        case 0xDA: BltSpan<0xDA>(destination, source, count); break;
        default: break;
    }
}

u32 Video::GetBltWidth() const
{
    const u8* graphics = m_state.graphics;
    return (((u32)graphics[k_video_gr_blt_width + 1] << 8 | graphics[k_video_gr_blt_width]) & (m_pci ? 0x1FFF : 0x7FF)) + 1;
}

u32 Video::GetBltHeight() const
{
    const u8* graphics = m_state.graphics;
    return (((u32)graphics[k_video_gr_blt_height + 1] << 8 | graphics[k_video_gr_blt_height]) & (m_pci ? 0x7FF : 0x3FF)) + 1;
}

u32 Video::GetBltPitch(int index) const
{
    const u8* graphics = m_state.graphics;
    return ((u32)graphics[index + 1] << 8 | graphics[index]) & (m_pci ? 0x1FFF : 0xFFF);
}

u32 Video::GetBltAddress(int index) const
{
    const u8* graphics = m_state.graphics;
    return ((u32)graphics[index + 2] << 16 | (u32)graphics[index + 1] << 8 | graphics[index]) & (m_pci ? 0x3FFFFF : 0x1FFFFF);
}

u32 Video::GetBltPixelBytes() const
{
    u8 mode = m_state.graphics[k_video_gr_blt_mode];
    return m_pci ? ((mode >> 4) & 0x03) + 1 : ((mode & 0x10) != 0 ? 2 : 1);
}

u32 Video::GetBltNeed() const
{
    u32 width = GetBltWidth();

    if ((m_state.graphics[k_video_gr_blt_mode] & 0x80) == 0)
        return (width + 3) & ~3U;

    u32 bytes = ((width / GetBltPixelBytes()) + 7) / 8;

    if (m_pci && (m_state.graphics[k_video_gr_blt_extensions] & 0x01) != 0)
    {
        u32 skip = (m_state.graphics[k_video_gr_blt_clip] >> 5) & 0x03;
        bytes = (skip + bytes + 3) & ~3U;
    }

    return bytes;
}

void Video::WriteBltControl(u8 value, u64 clocks)
{
    u8 keep = m_pci ? 0xE0 : 0x00;
    u8 status = m_state.graphics[k_video_gr_blt_status] & 0x0B;

    if ((value & 0x04) != 0)
    {
        ResetBlt();
        m_state.graphics[k_video_gr_blt_status] = value & keep;
        return;
    }

    m_state.graphics[k_video_gr_blt_status] = (value & keep) | status;

    if ((value & 0x02) != 0)
    {
        if (!m_state.blt_waiting)
            StartBlt(clocks);
        else if (m_state.blt_suspended)
        {
            m_state.blt_suspended = false;
            m_state.graphics[k_video_gr_blt_status] |= 0x0B;
        }
    }
    else if (m_state.blt_waiting && !m_state.blt_suspended)
    {
        m_state.blt_suspended = true;
        m_state.graphics[k_video_gr_blt_status] = (m_state.graphics[k_video_gr_blt_status] & ~0x03) | 0x08;
    }
}

void Video::StartBlt(u64 clocks)
{
    RenderLineToBeam(clocks);
    m_blt_count++;

    u8 mode = m_state.graphics[k_video_gr_blt_mode];
    u8 extensions = m_pci ? m_state.graphics[k_video_gr_blt_extensions] : 0x00;
    bool system = (mode & 0x04) != 0 || (!m_pci && (mode & 0x02) != 0);
    m_state.blt_dst = GetBltAddress(k_video_gr_blt_dst);
    m_state.blt_src = GetBltAddress(k_video_gr_blt_src);
    m_state.blt_line = 0;
    m_state.blt_count = 0;
    m_state.blt_fed = 0;
    m_state.blt_suspended = false;

    if (system)
        m_blt_kinds[VIDEO_BLT_SYSTEM]++;
    else if ((mode & 0x80) != 0 || (extensions & 0x04) != 0)
        m_blt_kinds[VIDEO_BLT_EXPAND]++;
    else if ((mode & 0x40) != 0)
        m_blt_kinds[VIDEO_BLT_PATTERN]++;
    else
        m_blt_kinds[VIDEO_BLT_SCREEN]++;

    if (unlikely(IsValidPointer(m_trace_logger) && m_trace_logger->IsEventEnabled(TRACE_VIDEO, TRACE_VIDEO_BLT)))
    {
        GDOX_Trace_Entry entry = {};
        entry.type = TRACE_VIDEO;
        entry.event = TRACE_VIDEO_BLT;
        entry.blt.destination = m_state.blt_dst;
        entry.blt.source = m_state.blt_src;
        entry.blt.width = (u16)GetBltWidth();
        entry.blt.height = (u16)GetBltHeight();
        entry.blt.mode = mode;
        entry.blt.rop = m_state.graphics[k_video_gr_blt_rop];
        entry.blt.extensions = extensions;
        m_trace_logger->TraceLog(entry);
    }

    if (system)
    {
        m_state.blt_waiting = true;
        m_state.graphics[k_video_gr_blt_status] |= 0x0B;
        UpdateBltMapping(true);
        return;
    }

    u32 height = GetBltHeight();

    while (m_state.blt_line < height)
        RunBltLine(NULL);

    FinishBlt();
}

void Video::FinishBlt()
{
    u8* graphics = m_state.graphics;
    graphics[k_video_gr_blt_dst] = (u8)m_state.blt_dst;
    graphics[k_video_gr_blt_dst + 1] = (u8)(m_state.blt_dst >> 8);
    graphics[k_video_gr_blt_dst + 2] = (u8)((m_state.blt_dst >> 16) & (m_pci ? 0x3F : 0x1F));
    graphics[k_video_gr_blt_src] = (u8)m_state.blt_src;
    graphics[k_video_gr_blt_src + 1] = (u8)(m_state.blt_src >> 8);
    graphics[k_video_gr_blt_src + 2] = (u8)((m_state.blt_src >> 16) & (m_pci ? 0x3F : 0x1F));
    graphics[k_video_gr_blt_status] &= (u8)~0x0B;
    m_state.blt_waiting = false;
    m_state.blt_suspended = false;
    m_state.blt_count = 0;
}

void Video::ResetBlt()
{
    m_state.blt_waiting = false;
    m_state.blt_suspended = false;
    m_state.blt_count = 0;
    m_state.blt_fed = 0;
}

bool Video::TakesBltAccess(u32 offset, bool linear, bool read) const
{
    u8 mode = m_state.graphics[k_video_gr_blt_mode];

    if (m_state.blt_suspended || (read ? (m_pci || (mode & 0x02) == 0) : (mode & 0x04) == 0))
        return false;

    if (m_pci)
    {
        u8 control = m_state.graphics[k_video_gr_blt_status];

        if ((control & 0x20) != 0)
            return false;

        if ((control & 0x40) != 0)
            return !linear && offset >= 0x1C000;
    }

    return linear || (offset - m_window_start) < m_window_size;
}

void Video::FeedBlt(u8 value)
{
    u32 height = GetBltHeight();

    if (m_state.blt_line < height)
    {
        m_state.blt_buffer[m_state.blt_count++] = value;

        if (m_state.blt_count >= GetBltNeed())
        {
            RunBltLine(m_state.blt_buffer);
            m_state.blt_count = 0;
        }
    }

    m_state.blt_fed++;

    if (m_state.blt_line >= height && (m_state.blt_fed & 0x03) == 0)
        FinishBlt();
}

u8 Video::ReadBlt()
{
    u32 height = GetBltHeight();

    if (m_state.blt_count == 0)
    {
        if (m_state.blt_line >= height)
        {
            FinishBlt();
            return 0xFF;
        }

        u32 width = GetBltWidth();
        u32 low = (m_state.graphics[k_video_gr_blt_mode] & 0x01) != 0 ? m_state.blt_src - (width - 1) : m_state.blt_src;
        u8 rop = m_state.graphics[k_video_gr_blt_rop];
        u32 padded = (width + 3) & ~3U;

        for (u32 i = 0; i < width; i++)
            m_state.blt_buffer[i] = BltRop(rop, m_state.vram[(low + i) & m_vram_mask], 0x00);

        memset(m_state.blt_buffer + width, 0, padded - width);
        u32 pitch = GetBltPitch(k_video_gr_blt_src_pitch);
        m_state.blt_src += (m_state.graphics[k_video_gr_blt_mode] & 0x01) != 0 ? (u32)-(s32)pitch : pitch;
        m_state.blt_line++;
        m_state.blt_count = padded;
        m_state.blt_fed = 0;
    }

    u8 value = m_state.blt_buffer[m_state.blt_fed++ % VIDEO_BLT_BUFFER_SIZE];

    if (m_state.blt_fed >= m_state.blt_count)
    {
        m_state.blt_count = 0;

        if (m_state.blt_line >= height)
            FinishBlt();
    }

    return value;
}

void Video::RunBltLine(const u8* system)
{
    u8 mode = m_state.graphics[k_video_gr_blt_mode];
    bool expand = (mode & 0x80) != 0 || (m_pci && (m_state.graphics[k_video_gr_blt_extensions] & 0x04) != 0);
    bool backward = (mode & 0x01) != 0;
    u32 width = GetBltWidth();
    bool masked = false;

    if (expand)
        masked = BuildBltExpansion(m_blt_source, m_blt_enable, width, system);
    else if ((mode & 0x40) != 0)
        masked = BuildBltPattern(m_blt_source, m_blt_enable, width);
    else if (system != NULL)
    {
        u32 skip = m_pci ? (m_state.graphics[k_video_gr_blt_clip] >> 5) & 0x03 : 0;
        memcpy(m_blt_source, system, width);

        if (skip != 0)
        {
            memset(m_blt_enable, 1, width);
            memset(m_blt_enable, 0, MIN(skip, width));
            masked = true;
        }
    }
    else
    {
        u32 low = backward ? m_state.blt_src - (width - 1) : m_state.blt_src;

        if (((low & m_vram_mask) + width) <= m_vram_size)
            memcpy(m_blt_source, m_state.vram + (low & m_vram_mask), width);
        else
        {
            for (u32 i = 0; i < width; i++)
                m_blt_source[i] = m_state.vram[(low + i) & m_vram_mask];
        }
    }

    ApplyBltLine(m_blt_source, m_blt_enable, width, masked);

    u32 pitch = GetBltPitch(k_video_gr_blt_dst_pitch);
    m_state.blt_dst += backward ? (u32)-(s32)pitch : pitch;

    if (expand && (mode & 0x40) == 0 && system == NULL)
        m_state.blt_src += ((width / GetBltPixelBytes()) + 7) / 8;
    else if (!expand && (mode & 0x40) == 0 && system == NULL)
    {
        u32 src_pitch = GetBltPitch(k_video_gr_blt_src_pitch);
        m_state.blt_src += backward ? (u32)-(s32)src_pitch : src_pitch;
    }

    m_state.blt_line++;
}

bool Video::BuildBltExpansion(u8* source, u8* enable, u32 width, const u8* system)
{
    const u8* graphics = m_state.graphics;
    u8 mode = graphics[k_video_gr_blt_mode];
    u8 extensions = m_pci ? graphics[k_video_gr_blt_extensions] : 0x00;
    u32 bytes = GetBltPixelBytes();
    u32 pixels = width / bytes;
    u8 foreground[4] = { graphics[0x01], graphics[0x11], graphics[0x13], graphics[0x15] };
    u8 background[4] = { graphics[0x00], graphics[0x10], graphics[0x12], graphics[0x14] };
    bool transparent = m_pci && (mode & 0x08) != 0;
    bool invert = (extensions & 0x02) != 0;
    bool solid = (extensions & 0x04) != 0;
    bool pattern = (mode & 0x40) != 0;
    u32 clip = m_pci ? graphics[k_video_gr_blt_clip] & (bytes == 3 ? 0x1F : 0x07) : 0;
    bool clip_background = (extensions & 0x08) != 0;
    u32 skip = (m_pci && system != NULL) ? (graphics[k_video_gr_blt_clip] >> 5) & 0x03 : 0;
    u32 base = m_state.blt_src;
    u32 row = 0;

    if (pattern)
    {
        u32 preset = m_pci ? base & 0x07 : 0;
        base = m_pci ? base & ~0x07U : base;
        row = m_state.vram[(base + ((preset + m_state.blt_line) & 0x07)) & m_vram_mask];
    }

    for (u32 p = 0; p < pixels; p++)
    {
        u8 bit = (u8)(0x80 >> (p & 0x07));
        bool one;

        if (solid)
            one = true;
        else if (pattern)
            one = (row & bit) != 0;
        else if (system != NULL)
            one = (system[skip + (p >> 3)] & bit) != 0;
        else
            one = (m_state.vram[(base + (p >> 3)) & m_vram_mask] & bit) != 0;

        if (invert)
            one = !one;

        const u8* color = one ? foreground : background;
        bool write = one || !transparent;

        if (bytes != 3 && p < clip && (!clip_background || !one))
            write = false;

        for (u32 b = 0; b < bytes; b++)
        {
            u32 index = (p * bytes) + b;
            source[index] = color[b];
            enable[index] = (write && (bytes != 3 || index >= clip || (clip_background && one))) ? 1 : 0;
        }
    }

    for (u32 i = pixels * bytes; i < width; i++)
    {
        source[i] = 0;
        enable[i] = 0;
    }

    return transparent || clip != 0 || (pixels * bytes) != width;
}

bool Video::BuildBltPattern(u8* source, u8* enable, u32 width)
{
    u32 bytes = GetBltPixelBytes();
    u32 used = 8 * bytes;
    u32 row_bytes = bytes == 3 ? 32 : used;
    u32 base = m_state.blt_src;
    u32 row = m_state.blt_line;
    u32 clip = 0;

    if (m_pci)
    {
        row += base & 0x07;
        base &= ~((row_bytes * 8) - 1);
        clip = m_state.graphics[k_video_gr_blt_clip] & (bytes == 3 ? 0x1F : 0x07);
        clip = bytes == 3 ? clip : clip * bytes;
    }

    base += (row & 0x07) * row_bytes;

    for (u32 i = 0; i < width; i++)
        source[i] = m_state.vram[(base + (i % used)) & m_vram_mask];

    if (clip == 0)
        return false;

    memset(enable, 1, width);
    memset(enable, 0, MIN(clip, width));
    return true;
}

void Video::ApplyBltLine(const u8* source, const u8* enable, u32 width, bool masked)
{
    const u8* graphics = m_state.graphics;
    u8 mode = graphics[k_video_gr_blt_mode];
    u8 rop = graphics[k_video_gr_blt_rop];
    bool expand = (mode & 0x80) != 0;
    bool compare = (mode & 0x08) != 0 && (!m_pci || !expand);
    u32 low = (mode & 0x01) != 0 ? m_state.blt_dst - (width - 1) : m_state.blt_dst;
    u32 start = low & m_vram_mask;
    u8* vram = m_state.vram;

    if (!masked && !compare && (start + width) <= m_vram_size)
    {
        BltSpanRop(rop, vram + start, source, width);
        return;
    }

    u32 bytes = compare ? (m_pci ? (GetBltPixelBytes() == 1 ? 1 : 2) : ((mode & 0x10) != 0 ? 2 : 1)) : 1;
    u8 key[2] = { graphics[k_video_gr_blt_key], graphics[k_video_gr_blt_key + 1] };
    u8 key_mask[2] = { m_pci ? (u8)0 : graphics[k_video_gr_blt_key_mask],
        m_pci ? (u8)0 : graphics[k_video_gr_blt_key_mask + 1] };

    for (u32 i = 0; i < width; i += bytes)
    {
        u8 result[2];
        u32 count = MIN(bytes, width - i);
        bool equal = compare;

        for (u32 b = 0; b < count; b++)
        {
            u32 address = (low + i + b) & m_vram_mask;
            result[b] = BltRop(rop, source[i + b], vram[address]);
            equal = equal && ((result[b] ^ key[b]) & (u8)~key_mask[b]) == 0;
        }

        if (equal)
            continue;

        for (u32 b = 0; b < count; b++)
        {
            if (!masked || enable[i + b] != 0)
                vram[(low + i + b) & m_vram_mask] = result[b];
        }
    }
}

void Video::UpdateBltMapping(bool system)
{
    if (!system)
        return;

    m_blt_frame_used = true;

    if (!m_blt_handlers)
    {
        m_blt_handlers = true;
        m_blt_remaps++;
        UpdateVRAMMapping();
    }
}

u8 Video::ReadMMIO(u32 offset) const
{
    return offset == 0x40 ? m_state.graphics[k_video_gr_blt_status] : 0xFF;
}

void Video::WriteMMIO(u32 offset, u8 value)
{
    u64 clocks = m_scheduler->GetClocks();

    if (offset == 0x40)
    {
        WriteBltControl(value, clocks);
        return;
    }

    if (offset >= sizeof(k_video_mmio_registers) || k_video_mmio_registers[offset] < 0)
        return;

    u8 index = (u8)k_video_mmio_registers[offset];

    if (index < VIDEO_GRAPHICS_COUNT)
    {
        u8 saved = m_state.graphics_index;
        m_state.graphics_index = index;
        WriteGraphics(value, clocks);
        m_state.graphics_index = saved;
    }
    else
        WriteCirrusGraphics(index, value, clocks);
}

u32 Video::GetCursorSize() const
{
    return (m_state.sequencer[k_video_sr_cursor_attributes] & 0x04) != 0 ? 64 : 32;
}

u32 Video::GetCursorPatternAddress() const
{
    u8 pattern = m_state.sequencer[k_video_sr_cursor_pattern] & (GetCursorSize() == 64 ? 0x3C : 0x3F);
    return m_vram_size - 0x4000 + (pattern * 256);
}

u32 Video::GetCursorColor(int index) const
{
    const u8* c = m_state.dac_extended[index == 0 ? 0 : 15];
    return ToXRGB((u8)((c[0] << 2) | (c[0] >> 4)), (u8)((c[1] << 2) | (c[1] >> 4)), (u8)((c[2] << 2) | (c[2] >> 4)));
}

void Video::RenderCursor(u32* destination, int x, int end)
{
    int size = (int)GetCursorSize();
    int line = m_render_row - (int)m_state.cursor_y;

    if (line < 0 || line >= size)
        return;

    int left = (int)m_state.cursor_x;
    int from = MAX(x, left);
    int to = MIN(end, left + size);

    if (from >= to)
        return;

    u32 base = GetCursorPatternAddress();
    u32 plane0 = size == 64 ? base + (line * 16) : base + (line * 4);
    u32 plane1 = size == 64 ? plane0 + 8 : plane0 + 128;
    u32 color0 = GetCursorColor(0);
    u32 color1 = GetCursorColor(1);

    for (int i = from; i < to; i++)
    {
        int pixel = i - left;
        u8 bit = (u8)(0x80 >> (pixel & 0x07));
        bool b0 = (m_state.vram[(plane0 + (pixel >> 3)) & m_vram_mask] & bit) != 0;
        bool b1 = (m_state.vram[(plane1 + (pixel >> 3)) & m_vram_mask] & bit) != 0;

        if (b0 && b1)
            destination[i] = color1;
        else if (b1)
            destination[i] = color0;
        else if (b0)
            destination[i] ^= 0x00FFFFFF;
    }
}
