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

#include "video.h"

// Palette writes and the like draw what the beam has passed with the old state first,
// so the row splits where they landed
void Video::RenderLineToBeam(u64 clocks)
{
    u32 line = m_state.event_line;

    if (!m_state.running || !m_render || line >= m_state.display_lines || (line % m_canvas_v_divider) != 0)
        return;

    int row = (int)(line / m_canvas_v_divider);

    if (row >= m_render_height)
        return;

    u64 elapsed = clocks > m_line_start_clocks ? clocks - m_line_start_clocks : 0;
    u32 dot = (u32)((elapsed * m_state.frame_dot_clock) / GDOX_MACHINE_CLOCK_RATE);
    int x = (int)MIN(dot / m_canvas_h_divider, (u32)m_render_width);
    int from = m_partial_row == row ? m_partial_x : 0;

    if (x <= from)
        return;

    RenderRowPart(row, from, x);
    m_partial_row = row;
    m_partial_x = x;
}

void Video::RenderRow(u32 line)
{
    if ((line % m_canvas_v_divider) != 0)
        return;

    int row = (int)(line / m_canvas_v_divider);

    if (row < m_render_height)
        RenderRowPart(row, m_partial_row == row ? m_partial_x : 0, m_render_width);
}

void Video::RenderRowPart(int row, int x, int end)
{
    if (!m_render || !IsValidPointer(m_frame_buffer) || x >= end)
        return;

    u32* destination = (u32*)m_frame_buffer + row * m_render_width;

    if (IsScreenBlank())
    {
        u32 color = !m_cga && (m_state.attribute_index & 0x20) == 0 ? GetOverscanColor() : ToXRGB(0, 0, 0);

        for (int i = x; i < end; i++)
            destination[i] = color;

        return;
    }

    bool cursor = m_cirrus && (m_state.sequencer[k_video_sr_cursor_attributes] & 0x01) != 0 && !IsTextMode();
    m_render_row = row;

    if (m_canvas_zoom == 1)
    {
        RenderPixels(destination, x, end);

        if (cursor)
            RenderCursor(destination, x, end);

        return;
    }

    u32 zoom = m_canvas_zoom;

    RenderPixels(m_line_pixels, (int)(x / zoom), (int)((end + zoom - 1) / zoom));

    if (cursor)
        RenderCursor(m_line_pixels, (int)(x / zoom), (int)((end + zoom - 1) / zoom));

    for (int i = x; i < end; i++)
        destination[i] = m_line_pixels[i / zoom];
}

void Video::RenderPixels(u32* destination, int x, int end)
{
    if (m_cga)
    {
        RenderCGA(destination, x, end);
        return;
    }

    if (m_extended)
    {
        switch (m_extended_format)
        {
            case VIDEO_EXTENDED_GREY:
                RenderExtended<VIDEO_EXTENDED_GREY>(destination, x, end);
                break;
            case VIDEO_EXTENDED_332:
                RenderExtended<VIDEO_EXTENDED_332>(destination, x, end);
                break;
            case VIDEO_EXTENDED_555:
                RenderExtended<VIDEO_EXTENDED_555>(destination, x, end);
                break;
            case VIDEO_EXTENDED_555_MIX:
                RenderExtended<VIDEO_EXTENDED_555_MIX>(destination, x, end);
                break;
            case VIDEO_EXTENDED_565:
                RenderExtended<VIDEO_EXTENDED_565>(destination, x, end);
                break;
            case VIDEO_EXTENDED_888:
                RenderExtended<VIDEO_EXTENDED_888>(destination, x, end);
                break;
            case VIDEO_EXTENDED_BLANK:
                RenderExtended<VIDEO_EXTENDED_BLANK>(destination, x, end);
                break;
            default:
                RenderExtended<VIDEO_EXTENDED_PALETTE>(destination, x, end);
                break;
        }

        return;
    }

    if (IsTextMode())
    {
        RenderText(destination, x, end);
        return;
    }

    bool packed = IsPacked();

    switch (GetShiftFormat())
    {
        case VIDEO_FORMAT_PLANAR:
            if (packed)
                RenderGraphics<VIDEO_FORMAT_PLANAR, true>(destination, x, end);
            else
                RenderGraphics<VIDEO_FORMAT_PLANAR, false>(destination, x, end);
            break;
        case VIDEO_FORMAT_CGA:
            if (packed)
                RenderGraphics<VIDEO_FORMAT_CGA, true>(destination, x, end);
            else
                RenderGraphics<VIDEO_FORMAT_CGA, false>(destination, x, end);
            break;
        default:
            if (packed)
                RenderGraphics<VIDEO_FORMAT_256, true>(destination, x, end);
            else
                RenderGraphics<VIDEO_FORMAT_256, false>(destination, x, end);
            break;
    }
}

// Pixels of the first character the panning skips: 9-dot text counts 8, 0, 1 ... 7 for shifts of 0 to 8,
// and 8-bit pixels take two dots
int Video::GetPixelPan(bool text, bool packed) const
{
    u8 pan = m_state.pan_reset ? 0 : m_state.attribute[k_video_ar_panning] & 0x0F;

    if (text && (m_state.sequencer[k_video_sr_clocking] & 0x01) == 0)
        return pan >= 8 ? 0 : pan + 1;

    if (packed)
        return MIN(pan, (u8)7) >> 1;

    return pan & 0x07;
}

// Plane 0 holds characters, plane 1 attributes and plane 2 the fonts, two of them picked by attribute bit 3
void Video::RenderText(u32* destination, int x, int end)
{
    const u8* vram = m_state.vram;
    bool extended = (m_state.sequencer[k_video_sr_memory_mode] & 0x02) != 0;
    u8 map = extended ? m_state.character_map : m_state.character_map & 0x30;
    u8 mode = m_state.attribute[k_video_ar_mode];
    u8 cursor_start = m_state.crtc[k_video_cr_cursor_start];
    u8 cursor_end = m_state.crtc[k_video_cr_cursor_end] & 0x1F;
    int width = (m_state.sequencer[k_video_sr_clocking] & 0x01) != 0 ? 8 : 9;
    u32 row = m_state.row_scan;
    u32 font_a = ((map >> 2) & 0x03) * 0x4000 + ((map & 0x20) != 0 ? 0x2000 : 0);
    u32 font_b = (map & 0x03) * 0x4000 + ((map & 0x10) != 0 ? 0x2000 : 0);
    u32 underline = m_state.crtc[k_video_cr_underline] & 0x1F;
    u32 cursor = GetCursorAddress();
    bool cursor_on = (cursor_start & 0x20) == 0 && GetCursorPhase() && row >= (u32)(cursor_start & 0x1F) && row <= cursor_end;

    if (m_ega)
    {
        u32 start = cursor_start & 0x1F;
        cursor_on = GetCursorPhase() && (cursor_end > start ? row >= start && row < cursor_end : row >= start || row < cursor_end);
    }
    bool blink = (mode & 0x08) != 0;
    bool blink_phase = GetBlinkPhase();
    bool line_graphics = (mode & 0x04) != 0;
    u32 count_shift = GetCountShift();
    int shifted = x + GetPixelPan(true, false);
    int bit = shifted % width;
    u32 column = (u32)(shifted / width);

    while (x < end)
    {
        u32 address = m_state.row_address + (column >> count_shift);
        u32 base = GetPlaneBase(GetCRTCOffset(address, row));
        u8 character = vram[base];
        u8 attribute = vram[base + 1];
        u32 font = ((attribute & 0x08) != 0 ? font_a : font_b) + (u32)character * 32 + row;
        u32 bits = (u32)vram[GetPlaneBase(font) + 2] << 1;
        u32 foreground = m_attribute_colors[attribute & 0x0F];
        u32 background = m_attribute_colors[blink ? (attribute >> 4) & 0x07 : attribute >> 4];

        bool line_character = line_graphics && (character & 0xE0) == 0xC0;

        if (line_character)
            bits |= (bits >> 1) & 0x01;

        if (blink && (attribute & 0x80) != 0 && blink_phase)
            foreground = background;

        if (row == underline && (attribute & 0x77) == 0x01)
            bits = line_character ? 0x1FF : 0x1FE;

        if (cursor_on && (address & 0xFFFF) == cursor)
        {
            bits = 0x1FF;
            foreground = m_attribute_colors[attribute & 0x0F];
        }

        int count = MIN(width - bit, end - x);

        for (int i = 0; i < count; i++)
            destination[x + i] = ((bits >> (8 - (bit + i))) & 0x01) != 0 ? foreground : background;

        x += count;
        bit = 0;
        column++;
    }
}

// The shift registers take one fetch of the four planes per load and rotate the planes between loads,
// the address moves on every one, two or four character clocks
template<Video::Video_Format format, bool packed>
void Video::RenderGraphics(u32* destination, int x, int end)
{
    const u8* vram = m_state.vram;
    const int pixels = packed ? 4 : 8;
    u8 clocking = m_state.sequencer[k_video_sr_clocking];
    u32 load_mask = (clocking & 0x10) != 0 ? 0x03 : ((clocking & 0x04) != 0 ? 0x01 : 0x00);
    u32 count_shift = GetCountShift();
    u32 row = m_state.row_scan;
    int shifted = x + GetPixelPan(false, packed);
    int skip = shifted % pixels;
    u32 column = (u32)(shifted / pixels);

    while (x < end)
    {
        u32 load = column & ~load_mask;
        u32 rotate = column & load_mask;
        u32 base = GetPlaneBase(GetCRTCOffset(m_state.row_address + (load >> count_shift), row));
        const u8* planes = vram + base;
        u8 plane0 = planes[rotate & 0x03];
        u8 plane1 = planes[(rotate + 1) & 0x03];
        u8 plane2 = planes[(rotate + 2) & 0x03];
        u8 plane3 = planes[(rotate + 3) & 0x03];
        u32 colors[8];

        if (format == VIDEO_FORMAT_256 && packed)
        {
            colors[0] = m_dac_colors[plane0];
            colors[1] = m_dac_colors[plane1];
            colors[2] = m_dac_colors[plane2];
            colors[3] = m_dac_colors[plane3];
        }
        else
        {
            u8 values[8];

            if (format == VIDEO_FORMAT_PLANAR)
            {
                u64 bits = m_plane_bits[plane0] | (m_plane_bits[plane1] << 1) | (m_plane_bits[plane2] << 2) |
                    (m_plane_bits[plane3] << 3);

                for (int i = 0; i < 8; i++)
                    values[i] = (u8)(bits >> (i * 8));
            }
            else if (format == VIDEO_FORMAT_CGA)
            {
                u32 low = m_pair_bits[plane0] | (m_pair_bits[plane2] << 2);
                u32 high = m_pair_bits[plane1] | (m_pair_bits[plane3] << 2);

                for (int i = 0; i < 4; i++)
                {
                    values[i] = (u8)(low >> (i * 8));
                    values[i + 4] = (u8)(high >> (i * 8));
                }
            }
            else
            {
                values[0] = plane0 >> 4;
                values[1] = plane0 & 0x0F;
                values[2] = plane1 >> 4;
                values[3] = plane1 & 0x0F;
                values[4] = plane2 >> 4;
                values[5] = plane2 & 0x0F;
                values[6] = plane3 >> 4;
                values[7] = plane3 & 0x0F;
            }

            if (packed)
            {
                for (int i = 0; i < 4; i++)
                    colors[i] = m_dac_colors[(values[i * 2] << 4) | values[i * 2 + 1]];
            }
            else
            {
                for (int i = 0; i < 8; i++)
                    colors[i] = m_attribute_colors[values[i]];
            }
        }

        int count = MIN(pixels - skip, end - x);

        for (int i = 0; i < count; i++)
            destination[x + i] = colors[skip + i];

        x += count;
        skip = 0;
        column++;
    }
}

// Extended modes read memory in order from four bytes per counter step, pixels little endian (5446 p290-293)
template<Video::Video_Extended_Format format>
void Video::RenderExtended(u32* destination, int x, int end)
{
    const u8* vram = m_state.vram;
    const u32 bytes = format == VIDEO_EXTENDED_888 ? 3 :
        (format == VIDEO_EXTENDED_555 || format == VIDEO_EXTENDED_555_MIX || format == VIDEO_EXTENDED_565) ? 2 : 1;
    u32 mask = m_vram_mask;
    u32 address = ((m_state.row_address & GetCounterMask()) << 2) + (u32)x * bytes;

    for (int i = x; i < end; i++, address += bytes)
    {
        u32 a = address & mask;
        u32 color;

        if (format == VIDEO_EXTENDED_PALETTE)
            color = m_dac_colors[vram[a]];
        else if (format == VIDEO_EXTENDED_GREY)
            color = ToXRGB(vram[a], vram[a], vram[a]);
        else if (format == VIDEO_EXTENDED_332)
        {
            u8 value = vram[a];
            u8 red = (u8)((value & 0xE0) | ((value & 0xE0) >> 3) | ((value & 0xE0) >> 6));
            u8 green = (u8)(((value & 0x1C) << 3) | (value & 0x1C) | ((value & 0x1C) >> 3));
            u8 blue = (u8)((value & 0x03) * 0x55);
            color = ToXRGB(red, green, blue);
        }
        else if (format == VIDEO_EXTENDED_888)
            color = ToXRGB(vram[(a + 2) & mask], vram[(a + 1) & mask], vram[a]);
        else if (format == VIDEO_EXTENDED_BLANK)
            color = ToXRGB(0, 0, 0);
        else
        {
            u8 low = vram[a];
            u8 high = vram[(a + 1) & mask];

            if (format == VIDEO_EXTENDED_555_MIX && (high & 0x80) != 0)
                color = m_dac_colors[low];
            else if (format == VIDEO_EXTENDED_565)
                color = m_direct_high[1][high] | m_direct_low[1][low];
            else
                color = m_direct_high[0][high] | m_direct_low[0][low];
        }

        destination[i] = color;
    }
}
