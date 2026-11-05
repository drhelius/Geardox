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
#include "../common/state_serializer.h"
#include "../common/trace_logger.h"
#include "../system/pic.h"
#include "../system/scheduler.h"

Video::Video()
{
    InitPointer(m_pic);
    InitPointer(m_trace_logger);
    InitPointer(m_pit);
    InitPointer(m_scheduler);
    InitPointer(m_memory);
    InitPointer(m_frame_buffer);
    m_render = false;
    m_frame_ready = false;
    m_next_event_clocks = GDOX_NO_EVENT;
    m_next_event_line = 0;
    m_line_start_clocks = 0;
    m_display_end_clocks = 0;
    m_display_clocks = 0;
    m_frame_width = GDOX_FRAME_BUFFER_WIDTH;
    m_frame_height = GDOX_FRAME_BUFFER_HEIGHT;
    m_display_width = GDOX_FRAME_BUFFER_WIDTH;
    m_display_height = GDOX_FRAME_BUFFER_HEIGHT;
    m_render_width = GDOX_FRAME_BUFFER_WIDTH;
    m_render_height = GDOX_FRAME_BUFFER_HEIGHT;
    m_partial_row = -1;
    m_partial_x = 0;
    m_native_resolution = true;
    m_canvas_h_divider = 1;
    m_canvas_v_divider = 1;
    m_canvas_zoom = 1;
    m_window_start = 0;
    m_window_size = 0;
    m_mapped_size = 0;
    m_chain4 = false;
    m_odd_even_read = false;
    m_odd_even_write = false;
    m_plain_write = false;
    m_write_planes = 0;
    m_set_reset = 0;
    m_set_reset_enable = 0;
    m_bit_mask = 0;

    for (int i = 0; i < 16; i++)
    {
        u8 planes[4];

        for (int plane = 0; plane < 4; plane++)
            planes[plane] = (i & (1 << plane)) != 0 ? 0xFF : 0x00;

        memcpy(&m_plane_masks[i], planes, sizeof(planes));
    }

    for (int i = 0; i < 256; i++)
    {
        m_plane_bits[i] = 0;
        m_pair_bits[i] = 0;

        for (int bit = 0; bit < 8; bit++)
            m_plane_bits[i] |= (u64)((i >> (7 - bit)) & 0x01) << (bit * 8);

        for (int pair = 0; pair < 4; pair++)
            m_pair_bits[i] |= (u32)((i >> (6 - pair * 2)) & 0x03) << (pair * 8);
    }
}

Video::~Video()
{
}

void Video::SetTraceLogger(TraceLogger* trace_logger)
{
    m_trace_logger = trace_logger;
}

void Video::Init(PIC* pic, PIT* pit, Scheduler* scheduler, Memory* memory)
{
    m_pic = pic;
    m_pit = pit;
    m_scheduler = scheduler;
    m_memory = memory;

    Reset();
}

void Video::Reset()
{
    memset(&m_state, 0, sizeof(m_state));
    m_state.vga_enable = 0x01;
    m_state.pel_mask = 0xFF;
    m_frame_ready = false;
    m_frame_width = GDOX_FRAME_BUFFER_WIDTH;
    m_frame_height = GDOX_FRAME_BUFFER_HEIGHT;
    m_display_width = GDOX_FRAME_BUFFER_WIDTH;
    m_display_height = GDOX_FRAME_BUFFER_HEIGHT;
    UpdateColorCaches();
    UpdateMemoryAccess();
    UpdateIRQ();
    StartFrame(m_scheduler->GetClocks());
}

void Video::BeginFrame(u8* frame_buffer, bool render)
{
    m_frame_buffer = frame_buffer;
    m_render = render;
    m_frame_ready = false;
}

void Video::EndFrame()
{
    if (m_frame_ready || m_state.running)
        return;

    m_frame_width = GDOX_FRAME_BUFFER_WIDTH;
    m_frame_height = GDOX_FRAME_BUFFER_HEIGHT;
    m_display_width = GDOX_FRAME_BUFFER_WIDTH;
    m_display_height = GDOX_FRAME_BUFFER_HEIGHT;

    if (!m_render || !IsValidPointer(m_frame_buffer))
        return;

    int pixels = m_frame_width * m_frame_height;
    u32* buffer = (u32*)m_frame_buffer;
    u32 black = ToXRGB(0, 0, 0);

    for (int i = 0; i < pixels; i++)
        buffer[i] = black;
}

void Video::SetFrameSize(int frame_width, int frame_height, int display_width, int display_height)
{
    m_frame_width = frame_width;
    m_frame_height = frame_height;
    m_display_width = display_width;
    m_display_height = display_height;
}

// What a read returns, without the attribute flip-flop reset and the DAC stepping of Read
u8 Video::Peek(u16 port, u64 clocks) const
{
    switch (port)
    {
        case 0x3C0:
            return m_state.attribute_index;
        case 0x3C1:
            return (m_state.attribute_index & 0x1F) < VIDEO_ATTRIBUTE_COUNT ?
                m_state.attribute[m_state.attribute_index & 0x1F] : 0x00;
        case 0x3C2:
            return (m_state.vertical_interrupt ? 0x80 : 0x00) | 0x10;
        case 0x3C3:
            return m_state.vga_enable;
        case 0x3C4:
            return m_state.sequencer_index;
        case 0x3C5:
            return m_state.sequencer_index < VIDEO_SEQUENCER_COUNT ? m_state.sequencer[m_state.sequencer_index] : 0x00;
        case 0x3C6:
            return m_state.pel_mask;
        case 0x3C7:
            return m_state.dac_state;
        case 0x3C8:
            return m_state.dac_write_index;
        case 0x3C9:
            return m_state.dac[m_state.dac_read_index][m_state.dac_component];
        case 0x3CA:
            return m_state.feature;
        case 0x3CC:
            return m_state.misc;
        case 0x3CE:
            return m_state.graphics_index;
        case 0x3CF:
            return m_state.graphics_index < VIDEO_GRAPHICS_COUNT ? m_state.graphics[m_state.graphics_index] : 0x00;
        case 0x3B4:
        case 0x3D4:
            return IsCRTCBlockSelected(port) ? m_state.crtc_index : 0xFF;
        case 0x3B5:
        case 0x3D5:
            if (!IsCRTCBlockSelected(port))
                return 0xFF;

            return m_state.crtc_index < VIDEO_CRTC_COUNT ? m_state.crtc[m_state.crtc_index] : 0x00;
        case 0x3BA:
        case 0x3DA:
            return IsCRTCBlockSelected(port) ? GetSyncStatus(clocks) : 0xFF;
        default:
            return 0xFF;
    }
}

u8 Video::Read(u16 port, u64 clocks)
{
    Synchronize(clocks);

    switch (port)
    {
        case 0x3C9:
            return ReadDAC();
        case 0x3BA:
        case 0x3DA:
            if (IsCRTCBlockSelected(port))
                m_state.attribute_data = false;

            return Peek(port, clocks);
        default:
            return Peek(port, clocks);
    }
}

// Status changes where the display ends on a line and at the events, which end the CPU's polling on their own
u64 Video::GetSyncStableUntil(u64 clocks) const
{
    if (!m_state.running)
        return GDOX_NO_EVENT;

    if (m_state.event_line < m_state.display_lines && clocks < m_display_end_clocks)
        return m_display_end_clocks;

    return m_next_event_clocks;
}

void Video::Write(u16 port, u8 value, u64 clocks)
{
    Synchronize(clocks);

    switch (port)
    {
        case 0x3C0:
            WriteAttribute(value, clocks);
            break;
        case 0x3C2:
            WriteMisc(value);
            break;
        case 0x3C3:
            m_state.vga_enable = value & 0x01;
            break;
        case 0x3C4:
            m_state.sequencer_index = value & 0x07;
            break;
        case 0x3C5:
            WriteSequencer(value, clocks);
            break;
        case 0x3C6:
            if (value != m_state.pel_mask)
            {
                RenderLineToBeam(clocks);
                m_state.pel_mask = value;
                UpdateColorCaches();
            }
            break;
        case 0x3C7:
            m_state.dac_read_index = value;
            m_state.dac_component = 0;
            m_state.dac_state = 0x03;
            break;
        case 0x3C8:
            m_state.dac_write_index = value;
            m_state.dac_component = 0;
            m_state.dac_state = 0x00;
            break;
        case 0x3C9:
            WriteDAC(value, clocks);
            break;
        case 0x3CE:
            m_state.graphics_index = value & 0x0F;
            break;
        case 0x3CF:
            WriteGraphics(value, clocks);
            break;
        case 0x3B4:
        case 0x3D4:
            if (IsCRTCBlockSelected(port))
                m_state.crtc_index = value & 0x1F;
            break;
        case 0x3B5:
        case 0x3D5:
            if (IsCRTCBlockSelected(port))
                WriteCRTC(value, clocks);
            break;
        case 0x3BA:
        case 0x3DA:
            if (IsCRTCBlockSelected(port))
                m_state.feature = value;
            break;
        default:
            break;
    }

    if (unlikely(IsValidPointer(m_trace_logger) && m_trace_logger->IsEnabled(TRACE_VIDEO)))
        TraceWrite(port, value, clocks);
}

// A second write with the flip-flop in data state is data, the index keeps its palette access bit
void Video::WriteAttribute(u8 value, u64 clocks)
{
    if (!m_state.attribute_data)
    {
        if (((m_state.attribute_index ^ value) & 0x20) != 0)
            RenderLineToBeam(clocks);

        m_state.attribute_index = value & 0x3F;
        m_state.attribute_data = true;
        return;
    }

    m_state.attribute_data = false;

    u8 index = m_state.attribute_index & 0x1F;

    if (index >= VIDEO_ATTRIBUTE_COUNT)
        return;

    switch (index)
    {
        case k_video_ar_mode:
            value &= 0xEF;
            break;
        case k_video_ar_overscan:
            break;
        case k_video_ar_plane_enable:
            value &= 0x3F;
            break;
        case k_video_ar_panning:
        case k_video_ar_color_select:
            value &= 0x0F;
            break;
        default:
            value &= 0x3F;
            break;
    }

    if (value == m_state.attribute[index])
        return;

    RenderLineToBeam(clocks);
    m_state.attribute[index] = value;

    if (index != k_video_ar_panning && index != k_video_ar_overscan)
        UpdateAttributeColors();
}

void Video::WriteSequencer(u8 value, u64 clocks)
{
    u8 index = m_state.sequencer_index;

    if (index >= VIDEO_SEQUENCER_COUNT)
        return;

    value &= k_video_sequencer_masks[index];

    if (value == m_state.sequencer[index])
        return;

    RenderLineToBeam(clocks);
    m_state.sequencer[index] = value;

    if (index == k_video_sr_clocking)
        UpdateRunning(clocks);

    UpdateMemoryAccess();
}

void Video::WriteGraphics(u8 value, u64 clocks)
{
    u8 index = m_state.graphics_index;

    if (index >= VIDEO_GRAPHICS_COUNT)
        return;

    value &= k_video_graphics_masks[index];

    if (value == m_state.graphics[index])
        return;

    if (index == k_video_gr_mode || index == k_video_gr_misc)
        RenderLineToBeam(clocks);

    m_state.graphics[index] = value;
    UpdateMemoryAccess();
}

// CR11 bit 7 write-protects CR00-CR07, except the line compare bit of CR07
void Video::WriteCRTC(u8 value, u64 clocks)
{
    u8 index = m_state.crtc_index;

    if (index >= VIDEO_CRTC_COUNT)
        return;

    if ((m_state.crtc[k_video_cr_retrace_end] & 0x80) != 0)
    {
        if (index < k_video_cr_overflow)
            return;

        if (index == k_video_cr_overflow)
            value = (m_state.crtc[index] & ~0x10) | (value & 0x10);
    }

    if (value == m_state.crtc[index])
        return;

    RenderLineToBeam(clocks);
    m_state.crtc[index] = value;

    switch (index)
    {
        case k_video_cr_horizontal_total:
        case k_video_cr_vertical_total:
        case k_video_cr_overflow:
            UpdateRunning(clocks);
            break;
        case k_video_cr_retrace_end:
            if ((value & 0x10) == 0 && m_state.vertical_interrupt)
            {
                m_state.vertical_interrupt = false;

                if (unlikely(IsValidPointer(m_trace_logger) &&
                    m_trace_logger->IsEventEnabled(TRACE_VIDEO, TRACE_VIDEO_VSYNC_CLEAR)))
                    TraceEvent(TRACE_VIDEO_VSYNC_CLEAR, 0, 0, 0, 0, m_state.frame_count, clocks);
            }

            UpdateIRQ();
            break;
        default:
            break;
    }
}

void Video::WriteMisc(u8 value)
{
    if (value == m_state.misc)
        return;

    m_state.misc = value;
    UpdateMemoryAccess();
}

// A color reaches the DAC with its third component
void Video::WriteDAC(u8 value, u64 clocks)
{
    u8 component = m_state.dac_component;
    u8 index = m_state.dac_write_index;

    m_state.dac_cache[component] = value & 0x3F;

    if (component < 2)
    {
        m_state.dac_component = component + 1;
        return;
    }

    m_state.dac_component = 0;
    m_state.dac_write_index = index + 1;

    if (memcmp(m_state.dac[index], m_state.dac_cache, 3) == 0)
        return;

    RenderLineToBeam(clocks);
    memcpy(m_state.dac[index], m_state.dac_cache, 3);
    UpdateDACColor(index);
}

u8 Video::ReadDAC()
{
    u8 value = m_state.dac[m_state.dac_read_index][m_state.dac_component];

    if (++m_state.dac_component == 3)
    {
        m_state.dac_component = 0;
        m_state.dac_read_index++;
    }

    return value;
}

// One event per display line, and the edges of the rest of the frame
void Video::RunNextEvent()
{
    u64 clocks = m_next_event_clocks;
    u32 line = m_next_event_line;

    if (m_state.event_line < m_state.display_lines)
        FinishLine();

    m_state.event_line = line;

    if (line == m_state.retrace_start)
    {
        if ((m_state.crtc[k_video_cr_retrace_end] & 0x10) != 0)
        {
            m_state.vertical_interrupt = true;
            UpdateIRQ();
        }

        if (unlikely(IsValidPointer(m_trace_logger) && m_trace_logger->IsEventEnabled(TRACE_VIDEO, TRACE_VIDEO_VSYNC)))
            TraceEvent(TRACE_VIDEO_VSYNC, 0, 0, 0, 0, m_state.frame_count, clocks);
    }

    if (line == m_state.ready_line)
        CompleteFrame();

    if (line >= m_state.frame_lines)
        StartFrame(clocks);
    else
        UpdateNextEvent();
}

// The address counters run on their own: a change of the offset or the line compare works from the next line
void Video::FinishLine()
{
    u32 line = m_state.event_line;

    RenderRow(line);

    u8 max_scan = m_state.crtc[k_video_cr_max_scan];

    if ((max_scan & 0x80) != 0 && !m_state.row_repeat)
        m_state.row_repeat = true;
    else
    {
        m_state.row_repeat = false;

        if (m_state.row_scan == (u32)(max_scan & 0x1F))
        {
            m_state.row_scan = 0;
            m_state.row_address += (u32)m_state.crtc[k_video_cr_offset] << 1;
        }
        else
            m_state.row_scan = (m_state.row_scan + 1) & 0x1F;
    }

    if (line == GetLineCompare())
    {
        m_state.row_address = 0;
        m_state.row_scan = 0;

        if ((m_state.attribute[k_video_ar_mode] & 0x20) != 0)
            m_state.pan_reset = true;
    }
}

void Video::LatchFrameTiming()
{
    u32 display = ((u32)m_state.crtc[k_video_cr_horizontal_display] + 1) * GetCharacterClockDots();
    u32 retrace = GetVerticalRetraceStart();
    u32 retrace_length = (m_state.crtc[k_video_cr_retrace_end] - retrace) & 0x0F;

    m_state.frame_line_dots = GetLineDots();
    m_state.frame_display_dots = MIN(display, m_state.frame_line_dots);
    m_state.frame_lines = GetTotalLines();
    m_state.frame_dot_clock = GetDotClock();
    m_state.display_lines = MIN(GetVerticalDisplayEnd() + 1, m_state.frame_lines);
    m_state.retrace_start = retrace;
    m_state.retrace_end = MIN(retrace + (retrace_length != 0 ? retrace_length : 16), m_state.frame_lines);
    m_state.ready_line = MIN(MAX(retrace, m_state.display_lines), m_state.frame_lines);
}

void Video::StartFrame(u64 clocks)
{
    m_state.frame_start_clocks = clocks;
    m_state.event_line = 0;

    if (m_state.running)
    {
        LatchFrameTiming();
        m_state.row_address = m_state.start_address;
        m_state.row_scan = m_state.crtc[k_video_cr_preset_row] & 0x1F;
        m_state.row_repeat = false;
        m_state.pan_reset = false;
    }

    m_partial_row = -1;
    m_partial_x = 0;
    UpdateGeometry();
    UpdateNextEvent();
}

// The picture is finished as soon as retrace starts, which is also when the start address latches
void Video::CompleteFrame()
{
    bool blink = GetBlinkPhase();

    m_frame_width = m_render_width;
    m_frame_height = m_render_height;
    m_display_width = m_render_width;
    m_display_height = (m_render_width * 3) / 4;
    m_frame_ready = true;
    m_state.frame_count++;
    m_state.start_address = (GetStartAddress() + ((m_state.crtc[k_video_cr_preset_row] >> 5) & 0x03)) & 0xFFFF;

    if (blink != GetBlinkPhase() && !IsTextMode() && (m_state.attribute[k_video_ar_mode] & 0x08) != 0)
        UpdateAttributeColors();
}

void Video::UpdateRunning(u64 clocks)
{
    bool running = GetLineDots() >= k_video_min_line_dots && GetTotalLines() >= k_video_min_lines;

    if (running == m_state.running)
        return;

    m_state.running = running;
    StartFrame(clocks);
}

void Video::UpdateNextEvent()
{
    if (!m_state.running)
    {
        m_next_event_line = 0;
        m_next_event_clocks = GDOX_NO_EVENT;
        m_scheduler->Schedule(SCHEDULER_EVENT_VIDEO, m_next_event_clocks);
        return;
    }

    u32 line = m_state.event_line;
    u32 next = line < m_state.display_lines ? line + 1 : m_state.frame_lines;

    if (line < m_state.retrace_start)
        next = MIN(next, m_state.retrace_start);

    if (line < m_state.retrace_end)
        next = MIN(next, m_state.retrace_end);

    if (line < m_state.ready_line)
        next = MIN(next, m_state.ready_line);

    m_next_event_line = next;
    m_next_event_clocks = GetLineClocks(next);
    m_line_start_clocks = GetLineClocks(line);
    m_display_end_clocks = m_line_start_clocks + m_display_clocks;
    m_scheduler->Schedule(SCHEDULER_EVENT_VIDEO, m_next_event_clocks);
}

// The canvas is the active area in pixels as the program addresses them, doubled up to dots with native resolution off
void Video::UpdateGeometry()
{
    if (!m_state.running)
    {
        m_render_width = GDOX_FRAME_BUFFER_WIDTH;
        m_render_height = GDOX_FRAME_BUFFER_HEIGHT;
        m_canvas_h_divider = 1;
        m_canvas_v_divider = 1;
        m_canvas_zoom = 1;
        m_display_clocks = 0;
        return;
    }

    u64 display_clocks = (u64)m_state.frame_display_dots * GDOX_MACHINE_CLOCK_RATE;
    m_display_clocks = (u32)((display_clocks + m_state.frame_dot_clock - 1) / m_state.frame_dot_clock);

    u8 clocking = m_state.sequencer[k_video_sr_clocking];
    u8 max_scan = m_state.crtc[k_video_cr_max_scan];
    bool text = IsTextMode();
    bool packed = !text && IsPacked();
    u32 pixel_dots = (((clocking & 0x08) != 0) ? 2 : 1) * (packed ? 2 : 1);
    u32 character_pixels = text ? ((clocking & 0x01) != 0 ? 8 : 9) : (packed ? 4 : 8);
    u32 width = ((u32)m_state.crtc[k_video_cr_horizontal_display] + 1) * character_pixels;
    u32 v_divider = (max_scan & 0x80) != 0 ? 2 : 1;

    if (!text && (m_state.crtc[k_video_cr_mode] & 0x03) == 0x03)
        v_divider *= (u32)(max_scan & 0x1F) + 1;

    if (!m_native_resolution)
    {
        m_canvas_h_divider = 1;
        m_canvas_v_divider = 1;
        m_canvas_zoom = pixel_dots;
    }
    else
    {
        m_canvas_h_divider = pixel_dots;
        m_canvas_v_divider = v_divider;
        m_canvas_zoom = 1;
    }

    u32 height = (m_state.display_lines + m_canvas_v_divider - 1) / m_canvas_v_divider;

    m_render_width = (int)MIN(width * m_canvas_zoom, (u32)GDOX_MAX_FRAME_BUFFER_WIDTH);
    m_render_height = (int)CLAMP(height, 1U, (u32)GDOX_MAX_FRAME_BUFFER_HEIGHT);
}

void Video::UpdateIRQ()
{
    m_pic->SetIRQLine(k_video_irq, m_state.vertical_interrupt && (m_state.crtc[k_video_cr_retrace_end] & 0x20) == 0);
}

void Video::UpdateDACColor(int index)
{
    const u8* color = m_state.dac[index];
    u8 red = (u8)((color[0] << 2) | (color[0] >> 4));
    u8 green = (u8)((color[1] << 2) | (color[1] >> 4));
    u8 blue = (u8)((color[2] << 2) | (color[2] >> 4));

    m_dac_rgb[index] = ToXRGB(red, green, blue);

    if (m_state.pel_mask == 0xFF)
        m_dac_colors[index] = m_dac_rgb[index];
    else
    {
        for (int i = 0; i < VIDEO_DAC_COUNT; i++)
            m_dac_colors[i] = m_dac_rgb[i & m_state.pel_mask];
    }

    UpdateAttributeColors();
}

// Plane enable, palette, color select and graphics blink all come down to one color per 4-bit value
void Video::UpdateAttributeColors()
{
    u8 mode = m_state.attribute[k_video_ar_mode];
    u8 plane_enable = m_state.attribute[k_video_ar_plane_enable] & 0x0F;
    u8 select = m_state.attribute[k_video_ar_color_select];
    bool blink = !IsTextMode() && (mode & 0x08) != 0;
    bool blink_phase = GetBlinkPhase();

    for (int i = 0; i < 16; i++)
    {
        u8 value = i & plane_enable;

        if (blink)
        {
            bool high = !blink_phase || ((i & 0x08) == 0 && (plane_enable & 0x08) != 0);
            value = (value & 0x07) | (high ? 0x08 : 0x00);
        }

        u8 color = m_state.attribute[value];
        u8 index;

        if ((mode & 0x80) != 0)
            index = (color & 0x0F) | ((select & 0x0F) << 4);
        else
            index = (color & 0x3F) | ((select & 0x0C) << 4);

        m_attribute_colors[i] = m_dac_colors[index];
    }
}

void Video::UpdateColorCaches()
{
    for (int i = 0; i < VIDEO_DAC_COUNT; i++)
    {
        const u8* color = m_state.dac[i];
        m_dac_rgb[i] = ToXRGB((u8)((color[0] << 2) | (color[0] >> 4)), (u8)((color[1] << 2) | (color[1] >> 4)),
            (u8)((color[2] << 2) | (color[2] >> 4)));
    }

    for (int i = 0; i < VIDEO_DAC_COUNT; i++)
        m_dac_colors[i] = m_dac_rgb[i & m_state.pel_mask];

    UpdateAttributeColors();
}

// Index writes only select, so the events are the data writes with what the register holds afterwards
void Video::TraceWrite(u16 port, u8 value, u64 clocks)
{
    switch (port)
    {
        case 0x3C0:
            if (!m_state.attribute_data)
                TraceEvent(TRACE_VIDEO_OUTPUT, m_state.attribute_index & 0x1F, value, value, 2, 0, clocks);
            break;
        case 0x3C2:
            TraceEvent(TRACE_VIDEO_OUTPUT, 0, value, value, 3, 0, clocks);
            break;
        case 0x3C5:
            TraceEvent(TRACE_VIDEO_OUTPUT, m_state.sequencer_index, value, value, 0, 0, clocks);
            break;
        case 0x3C9:
        {
            const u8* color = m_state.dac_cache;
            u8 component = (m_state.dac_component + 2) % 3;
            u8 index = m_state.dac_write_index - (component == 2 ? 1 : 0);
            u32 rgb = ((u32)color[0] << 16) | ((u32)color[1] << 8) | color[2];
            TraceEvent(TRACE_VIDEO_PALETTE, index, component, value, 0, rgb, clocks);
            break;
        }
        case 0x3CF:
            TraceEvent(TRACE_VIDEO_OUTPUT, m_state.graphics_index, value, value, 1, 0, clocks);
            break;
        case 0x3B5:
        case 0x3D5:
            if (IsCRTCBlockSelected(port))
                TraceEvent(TRACE_VIDEO_CRTC, m_state.crtc_index, value, value, 0, 0, clocks);
            break;
        default:
            break;
    }
}

void Video::TraceEvent(u8 event, u8 reg, u16 value, u8 raw, u8 bank, u32 param, u64 clocks)
{
    if (!m_trace_logger->IsEventEnabled(TRACE_VIDEO, event))
        return;

    GDOX_Trace_Entry entry = {};
    entry.type = TRACE_VIDEO;
    entry.event = event;
    entry.video.param = param;
    entry.video.value = value;
    entry.video.reg = reg;
    entry.video.raw = raw;
    entry.video.bank = bank;
    entry.video.line = 0xFFFF;

    if (m_state.running)
    {
        entry.video.line = (u16)GetBeamLine(clocks);
        entry.video.dot = (u16)GetBeamDot(clocks);
    }

    m_trace_logger->TraceLog(entry);
}

void Video::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer, GDOX_SAVESTATE_VERSION);
}

void Video::LoadState(std::istream& stream, int version)
{
    StateSerializer serializer(stream);
    Serialize(serializer, version);
    SanitizeState();
}

void Video::Serialize(StateSerializer& serializer, int version)
{
    UNUSED(version);
    G_SERIALIZE_ARRAY(serializer, m_state.vram, VIDEO_VRAM_SIZE);
    G_SERIALIZE_ARRAY(serializer, m_state.sequencer, VIDEO_SEQUENCER_COUNT);
    G_SERIALIZE(serializer, m_state.sequencer_index);
    G_SERIALIZE_ARRAY(serializer, m_state.graphics, VIDEO_GRAPHICS_COUNT);
    G_SERIALIZE(serializer, m_state.graphics_index);
    G_SERIALIZE_ARRAY(serializer, m_state.crtc, VIDEO_CRTC_COUNT);
    G_SERIALIZE(serializer, m_state.crtc_index);
    G_SERIALIZE_ARRAY(serializer, m_state.attribute, VIDEO_ATTRIBUTE_COUNT);
    G_SERIALIZE(serializer, m_state.attribute_index);
    G_SERIALIZE(serializer, m_state.attribute_data);
    G_SERIALIZE(serializer, m_state.misc);
    G_SERIALIZE(serializer, m_state.feature);
    G_SERIALIZE(serializer, m_state.vga_enable);
    G_SERIALIZE_ARRAY(serializer, &m_state.dac[0][0], sizeof(m_state.dac));
    G_SERIALIZE_ARRAY(serializer, m_state.dac_cache, 3);
    G_SERIALIZE(serializer, m_state.dac_read_index);
    G_SERIALIZE(serializer, m_state.dac_write_index);
    G_SERIALIZE(serializer, m_state.dac_component);
    G_SERIALIZE(serializer, m_state.dac_state);
    G_SERIALIZE(serializer, m_state.pel_mask);
    G_SERIALIZE(serializer, m_state.latch);
    G_SERIALIZE(serializer, m_state.vertical_interrupt);
    G_SERIALIZE(serializer, m_state.running);
    G_SERIALIZE(serializer, m_state.frame_start_clocks);
    G_SERIALIZE(serializer, m_state.frame_line_dots);
    G_SERIALIZE(serializer, m_state.frame_display_dots);
    G_SERIALIZE(serializer, m_state.frame_lines);
    G_SERIALIZE(serializer, m_state.frame_dot_clock);
    G_SERIALIZE(serializer, m_state.frame_count);
    G_SERIALIZE(serializer, m_state.display_lines);
    G_SERIALIZE(serializer, m_state.retrace_start);
    G_SERIALIZE(serializer, m_state.retrace_end);
    G_SERIALIZE(serializer, m_state.ready_line);
    G_SERIALIZE(serializer, m_state.event_line);
    G_SERIALIZE(serializer, m_state.start_address);
    G_SERIALIZE(serializer, m_state.row_address);
    G_SERIALIZE(serializer, m_state.row_scan);
    G_SERIALIZE(serializer, m_state.row_repeat);
    G_SERIALIZE(serializer, m_state.pan_reset);
}

void Video::SanitizeState()
{
    m_state.sequencer_index &= 0x07;
    m_state.graphics_index &= 0x0F;
    m_state.crtc_index &= 0x1F;
    m_state.attribute_index &= 0x3F;
    m_state.dac_component = m_state.dac_component % 3;
    m_state.frame_line_dots = MAX(m_state.frame_line_dots, k_video_min_line_dots);
    m_state.frame_lines = MAX(m_state.frame_lines, k_video_min_lines);
    m_state.event_line = MIN(m_state.event_line, m_state.frame_lines - 1);

    if (m_state.frame_dot_clock == 0)
        m_state.frame_dot_clock = GetDotClock();

    // The rows drawn before saving are not part of the state, the rest of the frame redraws them
    m_partial_row = -1;
    m_partial_x = 0;
    UpdateColorCaches();
    UpdateGeometry();
    UpdateNextEvent();
    UpdateIRQ();
    UpdateMemoryAccess();
    UpdateVRAMMapping();
}
