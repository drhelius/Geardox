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

#include <math.h>
#include "video.h"
#include "mc6845.h"
#include "../common/trace_logger.h"
#include "../osd/osd_font.h"

static const u8* const k_video_cga_font = k_osd_font_8;

static const u32 k_video_cga_indices[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

static const int k_video_cga_chroma_phases[8] = { -1, 180, 315, 270, 90, 135, 0, -1 };
static const double k_video_composite_weights[VIDEO_COMPOSITE_TAPS] = { 0.125, 0.25, 0.25, 0.25, 0.125 };
static const int k_video_composite_shift = 6;
static const s32 k_video_composite_bias = 1 << 18;
static const u32 k_video_composite_clamp_mask = (1 << 15) - 1;

static u64 PackComposite(const s32* a, const s32* b)
{
    u64 red = (u64)(a[0] + b[0] + k_video_composite_bias);
    u64 green = (u64)(a[1] + b[1] + k_video_composite_bias);
    u64 blue = (u64)(a[2] + b[2] + k_video_composite_bias);

    return red | (green << 21) | (blue << 42);
}

u8 Video::ReadCGACallback(void* device, u32 offset)
{
    return ((Video*)device)->m_state.vram[offset & (VIDEO_CGA_VRAM_SIZE - 1)];
}

u8 Video::PeekCGACallback(void* device, u32 offset)
{
    return ((Video*)device)->m_state.vram[offset & (VIDEO_CGA_VRAM_SIZE - 1)];
}

void Video::WriteCGACallback(void* device, u32 offset, u8 value)
{
    Video* video = (Video*)device;
    video->SynchronizeVRAM();
    video->m_state.vram[offset & (VIDEO_CGA_VRAM_SIZE - 1)] = value;
}

u16 Video::ReadCGA16Callback(void* device, u32 offset)
{
    const u8* vram = ((Video*)device)->m_state.vram;
    u32 mask = VIDEO_CGA_VRAM_SIZE - 1;
    return (u16)(vram[offset & mask] | (vram[(offset + 1) & mask] << 8));
}

u32 Video::ReadCGA32Callback(void* device, u32 offset)
{
    const u8* vram = ((Video*)device)->m_state.vram;
    u32 mask = VIDEO_CGA_VRAM_SIZE - 1;
    return (u32)vram[offset & mask] | ((u32)vram[(offset + 1) & mask] << 8) | ((u32)vram[(offset + 2) & mask] << 16) |
        ((u32)vram[(offset + 3) & mask] << 24);
}

void Video::WriteCGA16Callback(void* device, u32 offset, u16 value)
{
    Video* video = (Video*)device;
    u32 mask = VIDEO_CGA_VRAM_SIZE - 1;
    video->SynchronizeVRAM();
    video->m_state.vram[offset & mask] = (u8)value;
    video->m_state.vram[(offset + 1) & mask] = (u8)(value >> 8);
}

void Video::WriteCGA32Callback(void* device, u32 offset, u32 value)
{
    Video* video = (Video*)device;
    u32 mask = VIDEO_CGA_VRAM_SIZE - 1;
    video->SynchronizeVRAM();
    video->m_state.vram[offset & mask] = (u8)value;
    video->m_state.vram[(offset + 1) & mask] = (u8)(value >> 8);
    video->m_state.vram[(offset + 2) & mask] = (u8)(value >> 16);
    video->m_state.vram[(offset + 3) & mask] = (u8)(value >> 24);
}

void Video::SetCGAOutput(int output)
{
    m_cga_output = (output >= 0 && output < GDOX_CGA_OUTPUT_COUNT) ? output : GDOX_CGA_OUTPUT_AUTO;
}

void Video::BuildCompositeTables()
{
    const double pi = 3.14159265358979323846;
    const double scale = 255.0 * (1 << k_video_composite_shift);
    const s32 zero[3] = { 0, 0, 0 };
    s32 table[2][VIDEO_COMPOSITE_TAPS][4][16][3];

    for (int c = 0; c < 16; c++)
    {
        for (int p = 0; p < 4; p++)
        {
            double chroma = (c & 0x07) == 0x07 ? 1.0 : 0.0;
            int phase = k_video_cga_chroma_phases[c & 0x07];

            if (phase >= 0)
            {
                int high = 0;

                for (int step = 0; step < 90; step++)
                    high += cos((p * 90.0 - 45.0 + step + 0.5 - phase) * pi / 180.0) > 0.0 ? 1 : 0;

                chroma = high / 90.0;
            }

            double level = 0.72 * chroma + 0.28 * ((c >> 3) & 0x01);
            double u = -2.0 * level * cos(p * pi / 2.0);
            double v = 2.0 * level * sin(p * pi / 2.0);

            for (int k = 0; k < VIDEO_COMPOSITE_TAPS; k++)
            {
                double w = k_video_composite_weights[k];

                for (int burst = 0; burst < 2; burst++)
                {
                    double y = level * w;
                    double cu = burst ? u * w : 0.0;
                    double cv = burst ? v * w : 0.0;
                    s32* entry = table[burst][k][p][c];

                    entry[0] = (s32)lround((y + 1.140 * cv) * scale);
                    entry[1] = (s32)lround((y - 0.395 * cu - 0.581 * cv) * scale);
                    entry[2] = (s32)lround((y + 2.032 * cu) * scale);
                }
            }
        }
    }

    for (int burst = 0; burst < 2; burst++)
    {
        for (int p = 0; p < 4; p++)
        {
            for (int i = 0; i < 256; i++)
            {
                for (int pair = 0; pair < 2; pair++)
                    m_composite_pairs[burst][pair][p][i] = PackComposite(table[burst][pair * 2][p][i >> 4],
                        table[burst][pair * 2 + 1][(p + 1) & 0x03][i & 0x0F]);
            }

            for (int c = 0; c < 16; c++)
                m_composite_single[burst][p][c] = PackComposite(table[burst][4][p][c], zero);
        }
    }

    s32 base = (3 * k_video_composite_bias) >> k_video_composite_shift;

    for (int i = 0; i <= (int)k_video_composite_clamp_mask; i++)
        m_composite_clamp[i] = (u8)CLAMP(i - base, 0, 255);
}

u8 Video::PeekCGA(u16 port, u64 clocks) const
{
    if (port < 0x3D8)
        return (port & 0x01) != 0 ? m_mc6845->ReadData() : 0xFF;

    return port == 0x3DA ? GetSyncStatus(clocks) : 0xFF;
}

void Video::WriteCGA(u16 port, u8 value, u64 clocks)
{
    if (port < 0x3D8)
    {
        if ((port & 0x01) == 0)
        {
            m_mc6845->WriteIndex(value);
            return;
        }

        if (!m_mc6845->IsDataChange(value))
            return;

        RenderLineToBeam(clocks);
        m_mc6845->WriteData(value);

        if (m_mc6845->IsTimingRegister())
            UpdateRunning(clocks);

        return;
    }

    switch (port)
    {
        case 0x3D8:
            WriteCGAMode(value, clocks);
            break;
        case 0x3D9:
            WriteCGAColor(value, clocks);
            break;
        default:
            break;
    }
}

void Video::WriteCGAMode(u8 value, u64 clocks)
{
    value &= 0x3F;

    if (value == m_state.cga_mode)
        return;

    RenderLineToBeam(clocks);
    bool clock = ((value ^ m_state.cga_mode) & 0x01) != 0;
    m_state.cga_mode = value;

    if (clock)
        UpdateRunning(clocks);
}

void Video::WriteCGAColor(u8 value, u64 clocks)
{
    value &= 0x3F;

    if (value == m_state.cga_color)
        return;

    RenderLineToBeam(clocks);
    m_state.cga_color = value;
}

void Video::LatchCGATiming()
{
    u32 retrace = m_mc6845->GetVerticalSyncLine();

    m_state.frame_line_dots = GetLineDots();
    m_state.frame_display_dots = MIN(m_mc6845->GetHorizontalDisplayed() * GetCGACharacterDots(), m_state.frame_line_dots);
    m_state.frame_lines = GetTotalLines();
    m_state.frame_dot_clock = GetDotClock();
    m_state.display_lines = MIN(m_mc6845->GetDisplayedLines(), m_state.frame_lines);
    m_state.retrace_start = retrace;
    m_state.retrace_end = MIN(retrace + k_video_cga_vsync_lines, m_state.frame_lines);
    m_state.ready_line = MIN(MAX(retrace, m_state.display_lines), m_state.frame_lines);
}

void Video::FinishCGALine()
{
    RenderRow(m_state.event_line);

    if (m_state.row_scan >= m_mc6845->GetRegister(k_mc6845_max_scan))
    {
        m_state.row_scan = 0;
        m_state.row_address = (m_state.row_address + m_mc6845->GetHorizontalDisplayed()) & 0x3FFF;
    }
    else
        m_state.row_scan = (m_state.row_scan + 1) & 0x1F;
}

void Video::UpdateCGAGeometry()
{
    u8 mode = m_state.cga_mode;
    u32 dots = GetCGACharacterDots();

    m_cga_composite = m_cga_output == GDOX_CGA_OUTPUT_COMPOSITE ||
        (m_cga_output == GDOX_CGA_OUTPUT_AUTO && (mode & 0x16) == 0x12);

    bool fine = m_cga_composite || dots == 8 || (mode & 0x12) == 0x12;
    u32 pixel_dots = fine ? 1 : 2;
    u32 width = (m_mc6845->GetHorizontalDisplayed() * dots) / pixel_dots;
    u64 display_clocks = (u64)m_state.frame_display_dots * GDOX_MACHINE_CLOCK_RATE;

    m_display_clocks = (u32)((display_clocks + m_state.frame_dot_clock - 1) / m_state.frame_dot_clock);
    width = MAX(width, 1U);

    if (!m_native_resolution && width * pixel_dots <= (u32)GDOX_MAX_FRAME_BUFFER_WIDTH)
    {
        m_canvas_h_divider = 1;
        m_canvas_zoom = pixel_dots;
    }
    else
    {
        m_canvas_h_divider = pixel_dots;
        m_canvas_zoom = 1;
    }

    m_canvas_v_divider = 1;
    m_render_width = (int)MIN(width * m_canvas_zoom, (u32)GDOX_MAX_FRAME_BUFFER_WIDTH);
    m_render_height = (int)CLAMP(m_state.display_lines, 1U, (u32)GDOX_MAX_FRAME_BUFFER_HEIGHT);
    m_canvas_width = (int)(((u32)m_render_width * pixel_dots) / m_canvas_zoom);
    m_canvas_height = m_render_height * 2;
}

void Video::UpdateCGAColors()
{
    for (int i = 0; i < 16; i++)
    {
        u32 intensity = (i >> 3) & 0x01;
        u32 red = ((i >> 1) & 0x02) | intensity;
        u32 green = i == 0x06 ? 0x01 : (i & 0x02) | intensity;
        u32 blue = ((i << 1) & 0x02) | intensity;

        m_cga_colors[i] = ToXRGB((u8)(red * 0x55), (u8)(green * 0x55), (u8)(blue * 0x55));
    }
}

void Video::RenderCGA(u32* destination, int x, int end)
{
    if (m_cga_composite)
        RenderCGAComposite(destination, x, end);
    else
        RenderCGAPixels(destination, x, end, m_cga_colors);
}

void Video::RenderCGAPixels(u32* destination, int x, int end, const u32* colors)
{
    u8 mode = m_state.cga_mode;

    if ((mode & 0x02) == 0)
        RenderCGAText(destination, x, end, colors);
    else if ((mode & 0x10) != 0)
        RenderCGAGraphics<true>(destination, x, end, colors);
    else
        RenderCGAGraphics<false>(destination, x, end, colors);
}

void Video::RenderCGAComposite(u32* destination, int x, int end)
{
    const int half = VIDEO_COMPOSITE_TAPS / 2;
    u8 mode = m_state.cga_mode;
    int pixel_dots = (GetCGACharacterDots() == 8 || (mode & 0x12) == 0x12) ? 1 : 2;
    int first = MAX(x - half, 0);
    int last = MIN(end + half, m_render_width);
    u8* line = m_composite_line + half;

    RenderCGAPixels(m_line_pixels, first / pixel_dots, (last + pixel_dots - 1) / pixel_dots, k_video_cga_indices);

    for (int h = x - half; h < first; h++)
        line[h] = 0;

    for (int h = first; h < last; h++)
        line[h] = (u8)m_line_pixels[h / pixel_dots];

    for (int h = last; h < end + half; h++)
        line[h] = 0;

    int burst = (mode & 0x04) == 0 ? 1 : 0;
    const u64 (*pairs)[4][256] = m_composite_pairs[burst];
    const u64 (*single)[16] = m_composite_single[burst];
    const u8* clamp = m_composite_clamp;
    const int shift = k_video_composite_shift;
    u32 c0 = line[x - 2];
    u32 c1 = line[x - 1];
    u32 c2 = line[x];
    u32 c3 = line[x + 1];

    for (int h = x; h < end; h++)
    {
        u32 c4 = line[h + 2];
        int phase = h & 0x03;
        u64 sum = pairs[0][phase ^ 0x02][(c0 << 4) | c1] + pairs[1][phase][(c2 << 4) | c3] + single[phase ^ 0x02][c4];
        u8 red = clamp[(u32)(sum >> shift) & k_video_composite_clamp_mask];
        u8 green = clamp[(u32)(sum >> (21 + shift)) & k_video_composite_clamp_mask];
        u8 blue = clamp[(u32)(sum >> (42 + shift)) & k_video_composite_clamp_mask];

        destination[h] = ToXRGB(red, green, blue);
        c0 = c1;
        c1 = c2;
        c2 = c3;
        c3 = c4;
    }
}

void Video::RenderCGAText(u32* destination, int x, int end, const u32* colors)
{
    const u8* vram = m_state.vram;
    u8 mode = m_state.cga_mode;
    u32 row = m_state.row_scan;
    u32 glyph_row = row & 0x07;
    bool blink = (mode & 0x20) != 0;
    bool blink_phase = GetBlinkPhase();
    u32 cursor = m_mc6845->GetCursorAddress();
    u32 cursor_start = m_mc6845->GetCursorStart();
    u32 cursor_end = m_mc6845->GetCursorEnd();
    bool cursor_row = cursor_start <= cursor_end ? (row >= cursor_start && row <= cursor_end) :
        (row >= cursor_start || row <= cursor_end);
    bool cursor_on = m_mc6845->IsCursorShown() && GetCursorPhase() && cursor_row;
    int bit = x & 0x07;
    u32 column = (u32)x >> 3;

    while (x < end)
    {
        u32 address = (m_state.row_address + column) & 0x3FFF;
        u32 offset = (address << 1) & (VIDEO_CGA_VRAM_SIZE - 1);
        u8 character = vram[offset];
        u8 attribute = vram[offset + 1];
        u32 bits = k_video_cga_font[(u32)character * 8 + glyph_row];
        u32 foreground = colors[attribute & 0x0F];
        u32 background = colors[blink ? (attribute >> 4) & 0x07 : attribute >> 4];

        if (blink && (attribute & 0x80) != 0 && blink_phase)
            foreground = background;

        if (cursor_on && address == (cursor & 0x3FFF))
        {
            bits = 0xFF;
            foreground = colors[attribute & 0x0F];
        }

        int count = MIN(8 - bit, end - x);

        for (int i = 0; i < count; i++)
            destination[x + i] = ((bits >> (7 - (bit + i))) & 0x01) != 0 ? foreground : background;

        x += count;
        bit = 0;
        column++;
    }
}

template<bool high>
void Video::RenderCGAGraphics(u32* destination, int x, int end, const u32* colors)
{
    const u8* vram = m_state.vram;
    u8 color = m_state.cga_color;
    const int pixels = high ? 16 : 8;
    u32 bank = (m_state.row_scan & 0x01) << 13;
    u32 palette[4];

    if (high)
    {
        palette[0] = colors[0];
        palette[1] = colors[color & 0x0F];
    }
    else
    {
        u32 intensity = (color & 0x10) != 0 ? 0x08 : 0x00;
        palette[0] = colors[color & 0x0F];

        if ((m_state.cga_mode & 0x04) != 0)
        {
            palette[1] = colors[0x03 | intensity];
            palette[2] = colors[0x04 | intensity];
            palette[3] = colors[0x07 | intensity];
        }
        else
        {
            u32 base = (color & 0x20) != 0 ? 0x03 : 0x02;
            palette[1] = colors[base | intensity];
            palette[2] = colors[(base + 2) | intensity];
            palette[3] = colors[(base + 4) | intensity];
        }
    }

    int skip = x % pixels;
    u32 column = (u32)(x / pixels);

    while (x < end)
    {
        u32 offset = ((((m_state.row_address + column) << 1) & 0x1FFF) | bank) & (VIDEO_CGA_VRAM_SIZE - 1);
        u32 data = ((u32)vram[offset] << 8) | vram[(offset + 1) & (VIDEO_CGA_VRAM_SIZE - 1)];
        int count = MIN(pixels - skip, end - x);

        for (int i = 0; i < count; i++)
        {
            int pixel = skip + i;

            if (high)
                destination[x + i] = palette[(data >> (15 - pixel)) & 0x01];
            else
                destination[x + i] = palette[(data >> (14 - pixel * 2)) & 0x03];
        }

        x += count;
        skip = 0;
        column++;
    }
}

void Video::TraceCGAWrite(u16 port, u8 value, u64 clocks)
{
    if (port < 0x3D8)
    {
        if ((port & 0x01) != 0)
            TraceEvent(TRACE_VIDEO_CRTC, m_mc6845->GetIndex(), value, value, 0, 0, clocks);

        return;
    }

    if (port == 0x3D8)
        TraceEvent(TRACE_VIDEO_OUTPUT, 0, m_state.cga_mode, value, TRACE_VIDEO_BANK_CGA, 0, clocks);
    else if (port == 0x3D9)
        TraceEvent(TRACE_VIDEO_OUTPUT, 1, m_state.cga_color, value, TRACE_VIDEO_BANK_CGA, 0, clocks);
}
