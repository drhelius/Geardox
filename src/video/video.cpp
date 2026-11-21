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
#include "mc6845.h"
#include "voodoo.h"
#include "../common/state_serializer.h"
#include "../common/trace_logger.h"
#include "../system/memory.h"
#include "../system/pic.h"
#include "../system/scheduler.h"
#include "../system/machine_profiles.h"

Video::Video()
{
    InitPointer(m_pic);
    m_trace_logger = TraceLogger::GetDisabled();
    memset(&m_trace, 0, sizeof(m_trace));
    memset(&m_trace_blt, 0, sizeof(m_trace_blt));
    InitPointer(m_scheduler);
    InitPointer(m_memory);
    InitPointer(m_frame_buffer);
    InitPointer(m_voodoo);
    m_voodoo_enabled = false;
    m_voodoo_threaded = false;
    m_voodoo_shown = false;
    m_render_requested = false;
    m_render = false;
    m_frame_ready = false;
    m_next_event_clocks = GDOX_NO_EVENT;
    m_next_event_line = 0;
    m_edge_clocks = GDOX_NO_EVENT;
    // Without the CPU's cycles every write runs the lines first
    m_start_cycles = &m_vram_deadline;
    m_vram_deadline = 0;
    m_line_start_clocks = 0;
    m_display_end_clocks = 0;
    m_display_clocks = 0;
    m_frame_width = GDOX_FRAME_BUFFER_WIDTH;
    m_frame_height = GDOX_FRAME_BUFFER_HEIGHT;
    m_display_width = GDOX_FRAME_BUFFER_WIDTH;
    m_display_height = GDOX_FRAME_BUFFER_HEIGHT;
    m_render_width = GDOX_FRAME_BUFFER_WIDTH;
    m_render_height = GDOX_FRAME_BUFFER_HEIGHT;
    m_canvas_width = GDOX_FRAME_BUFFER_WIDTH;
    m_canvas_height = GDOX_FRAME_BUFFER_HEIGHT;
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
    m_odd_even_address = false;
    m_plain_write = false;
    m_write_planes = 0;
    m_set_reset = 0;
    m_set_reset_enable = 0;
    m_bit_mask = 0;
    m_card = GDOX_VIDEO_CARD_VGA;
    m_family = GDOX_VIDEO_FAMILY_VGA;
    m_cirrus = false;
    m_ega = false;
    m_cga = false;
    InitPointer(m_mc6845);
    memset(m_cga_colors, 0, sizeof(m_cga_colors));
    m_cga_output = GDOX_CGA_OUTPUT_AUTO;
    m_cga_composite = false;
    memset(m_composite_line, 0, sizeof(m_composite_line));
    BuildCompositeTables();
    m_local_bus = false;
    m_pci = false;
    m_vram_size = VIDEO_VGA_VRAM_SIZE;
    m_vram_mask = VIDEO_VGA_VRAM_SIZE - 1;
    m_plane_mask = (VIDEO_VGA_VRAM_SIZE >> 2) - 1;
    m_extended = false;
    m_wide_counter = false;
    m_dual_page = false;
    m_bank_low = 0;
    m_bank_high = 0;
    m_mapped_bank = 0;
    m_aperture_base = 0;
    m_aperture_mapped = 0;
    m_extended_format = VIDEO_EXTENDED_PALETTE;
    m_extended_bytes = 1;
    m_window_limit = 0xFFFFFFFF;
    m_address_shift = 0;
    m_cirrus_writes = false;
    m_mmio_low = false;
    m_mmio_top = false;
    m_blt_handlers = false;
    m_blt_frame_used = false;
    m_blt_count = 0;
    m_blt_remaps = 0;
    m_blt_frame_remaps = 0;
    m_render_row = 0;

    for (int i = 0; i < VIDEO_BLT_KIND_COUNT; i++)
    {
        m_blt_kinds[i] = 0;
        m_blt_frame_kinds[i] = 0;
    }

    for (int i = 0; i < 16; i++)
    {
        u8 planes[4];

        for (int plane = 0; plane < 4; plane++)
            planes[plane] = IS_SET_BIT(i, plane) ? 0xFF : 0x00;

        memcpy(&m_plane_masks[i], planes, sizeof(planes));
    }

    // 5:5:5 and 5:6:5 pixels expanded to 8 bits a channel, each byte's share of the color: their bits don't overlap
    for (u32 i = 0; i < 256; i++)
    {
        u32 blue = ((i & 0x1F) << 3) | ((i & 0x1F) >> 2);
        u32 red = (((i >> 2) & 0x1F) << 3) | (((i >> 2) & 0x1F) >> 2);
        u32 red565 = ((i >> 3) << 3) | ((i >> 3) >> 2);

        m_direct_low[0][i] = blue | ((((i >> 5) << 3) | (i >> 7)) << 8);
        m_direct_high[0][i] = 0xFF000000U | (red << 16) | ((((i & 0x03) << 6) | ((i & 0x03) << 1)) << 8);
        m_direct_low[1][i] = blue | (((i >> 5) << 2) << 8);
        m_direct_high[1][i] = 0xFF000000U | (red565 << 16) | ((((i & 0x07) << 5) | ((i & 0x07) >> 1)) << 8);
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
    SafeDelete(m_mc6845);
    SafeDelete(m_voodoo);
}

void Video::SetTraceLogger(TraceLogger* trace_logger)
{
    m_trace_logger = IsValidPointer(trace_logger) ? trace_logger : TraceLogger::GetDisabled();

    if (IsValidPointer(m_voodoo))
        m_voodoo->SetTraceLogger(trace_logger);
}

void Video::Init(PIC* pic, Scheduler* scheduler, Memory* memory)
{
    m_pic = pic;
    m_scheduler = scheduler;
    m_memory = memory;

    if (!IsValidPointer(m_mc6845))
        m_mc6845 = new MC6845();

    m_mc6845->Init();

    if (IsValidPointer(m_voodoo))
        m_voodoo->Init(this, scheduler, memory);

    Reset();
}

void Video::SetVoodoo(bool enabled)
{
    m_voodoo_enabled = enabled;

    if (!enabled)
    {
        SafeDelete(m_voodoo);
        return;
    }

    if (!IsValidPointer(m_voodoo))
    {
        m_voodoo = new Voodoo();
        m_voodoo->SetTraceLogger(m_trace_logger);
        m_voodoo->Init(this, m_scheduler, m_memory);
        m_voodoo->SetThreaded(m_voodoo_threaded);
    }

    m_voodoo->SetEnabled(true);
}

void Video::SetVoodooThreaded(bool threaded)
{
    m_voodoo_threaded = threaded;

    if (IsValidPointer(m_voodoo))
        m_voodoo->SetThreaded(threaded);
}

// A card change takes effect at the next reset
void Video::SetCard(int card)
{
    const GDOX_Video_Card_Info& info = k_video_cards[(card >= 0 && card < GDOX_VIDEO_CARD_COUNT) ? card : GDOX_VIDEO_CARD_VGA];

    m_card = (card >= 0 && card < GDOX_VIDEO_CARD_COUNT) ? card : GDOX_VIDEO_CARD_VGA;
    m_family = info.family;
    m_cirrus = info.family == GDOX_VIDEO_FAMILY_CIRRUS;
    m_ega = info.family == GDOX_VIDEO_FAMILY_EGA;
    m_cga = info.family == GDOX_VIDEO_FAMILY_CGA;
    m_local_bus = info.local_bus;
    m_pci = info.pci;
    m_vram_size = MIN(info.memory_size, (u32)VIDEO_VRAM_SIZE);
    m_vram_mask = m_vram_size - 1;
    m_plane_mask = (m_vram_size >> 2) - 1;
}

void Video::Reset()
{
    memset(&m_state, 0, sizeof(m_state));
    m_state.vga_enable = m_ega || m_cga ? 0x00 : 0x01;
    m_state.pel_mask = 0xFF;
    m_mc6845->Reset();

    if (m_cirrus)
    {
        for (int i = VIDEO_SEQUENCER_COUNT; i < VIDEO_SEQUENCER_EXT_COUNT; i++)
            m_state.sequencer[i] = m_pci ? k_video_cirrus_5446_sequencer_reset[i] : k_video_cirrus_sequencer_reset[i];

        m_state.sequencer[k_video_sr_config] = m_pci ? 0x00 : (m_local_bus ? 0x05 : 0x07) << 3;
        m_state.crtc[k_video_cr_id] = m_pci ? k_video_cirrus_5446_id : k_video_cirrus_id;
    }

    m_extended = false;
    m_wide_counter = false;
    m_dual_page = false;
    m_bank_low = 0;
    m_bank_high = 0;
    m_address_shift = 0;
    m_cirrus_writes = false;
    m_mmio_low = false;
    m_mmio_top = false;
    m_blt_handlers = false;
    m_blt_frame_used = false;
    UpdateExtendedFormat();
    m_state.frame_line_dots = k_video_min_line_dots;
    m_state.frame_lines = k_video_min_lines;
    m_state.frame_dot_clock = GetDotClock();
    m_frame_ready = false;
    m_frame_width = GDOX_FRAME_BUFFER_WIDTH;
    m_frame_height = GDOX_FRAME_BUFFER_HEIGHT;
    m_display_width = GDOX_FRAME_BUFFER_WIDTH;
    m_display_height = GDOX_FRAME_BUFFER_HEIGHT;
    UpdateColorCaches();
    UpdateMemoryAccess();
    UpdateAperture();
    UpdateIRQ();
    m_voodoo_shown = false;

    if (m_voodoo_enabled)
        m_voodoo->Reset();

    StartFrame(m_scheduler->GetClocks());
}

void Video::BeginFrame(u8* frame_buffer, bool render)
{
    m_frame_buffer = frame_buffer;
    m_render_requested = render;
    m_render = render && !m_voodoo_shown;
    m_frame_ready = false;
    m_trace.status = m_trace_logger->IsEventEnabled(TRACE_VIDEO, TRACE_VIDEO_STATUS);

    if (m_blt_handlers && !m_blt_frame_used && !m_state.blt_waiting)
    {
        m_blt_handlers = false;
        m_blt_remaps++;
        UpdateVRAMMapping();
    }

    m_blt_frame_used = false;
    m_blt_frame_remaps = m_blt_remaps;
    m_blt_remaps = 0;

    for (int i = 0; i < VIDEO_BLT_KIND_COUNT; i++)
    {
        m_blt_frame_kinds[i] = m_blt_kinds[i];
        m_blt_kinds[i] = 0;
    }
}

void Video::EndFrame()
{
    if (m_frame_ready || IsRunning())
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
    if (m_cga)
        return PeekCGA(port, clocks);

    if (m_ega)
        return PeekEGA(port, clocks);

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
            return PeekSequencer();
        case 0x3C6:
            return IsUnlocked() && m_state.hidden_dac_reads == 4 ? m_state.hidden_dac : m_state.pel_mask;
        case 0x3C7:
            return m_state.dac_state;
        case 0x3C8:
            return m_state.dac_index;
        case 0x3C9:
            return m_state.dac[m_state.dac_address][m_state.dac_component];
        case 0x3CA:
            return m_state.feature;
        case 0x3CC:
            return m_state.misc;
        case 0x3CE:
            return m_state.graphics_index;
        case 0x3CF:
            return PeekGraphics();
        case 0x3B4:
        case 0x3D4:
            return IsCRTCBlockSelected(port) ? m_state.crtc_index : 0xFF;
        case 0x3B5:
        case 0x3D5:
            if (!IsCRTCBlockSelected(port))
                return 0xFF;

            return PeekCRTC();
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

    if (m_cga)
        return PeekCGA(port, clocks);

    switch (port)
    {
        case 0x3C6:
            if (IsUnlocked() && m_state.hidden_dac_reads < 4)
            {
                m_state.hidden_dac_reads++;
                return m_state.pel_mask;
            }

            return Peek(port, clocks);
        case 0x3C7:
        case 0x3C8:
            m_state.hidden_dac_reads = 0;
            return Peek(port, clocks);
        case 0x3C9:
            m_state.hidden_dac_reads = 0;
            return ReadDAC();
        case 0x3BA:
        case 0x3DA:
            if (IsCRTCBlockSelected(port))
                m_state.attribute_data = false;

            if (unlikely(m_trace.status))
                CountStatusRead(clocks);

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

    if (m_cga)
    {
        WriteCGA(port, value, clocks);

        if (unlikely(m_trace_logger->IsEnabled(TRACE_VIDEO)))
            TraceCGAWrite(port, value, clocks);

        return;
    }

    if (unlikely(m_trace_logger->IsEnabled(TRACE_VIDEO)))
    {
        WriteTraced(port, value, clocks);
        return;
    }

    ApplyWrite(port, value, clocks);
}

void Video::ApplyWrite(u16 port, u8 value, u64 clocks)
{
    switch (port)
    {
        case 0x3C0:
            WriteAttribute(value, clocks);
            break;
        case 0x3C1:
            if (m_ega)
                WriteAttribute(value, clocks);
            break;
        case 0x3C2:
            WriteMisc(value, clocks);
            break;
        case 0x3C3:
            m_state.vga_enable = value & 0x01;
            break;
        case 0x3C4:
            m_state.sequencer_index = m_cirrus ? value : value & (m_ega ? 0x0F : 0x07);
            break;
        case 0x3C5:
            WriteSequencer(value, clocks);
            break;
        case 0x3C6:
            if (IsUnlocked() && m_state.hidden_dac_reads == 4)
            {
                m_state.hidden_dac_reads = 0;
                WriteHiddenDAC(value, clocks);
            }
            else if (value != m_state.pel_mask)
            {
                m_state.hidden_dac_reads = 0;
                RenderLineToBeam(clocks);
                m_state.pel_mask = value;
                UpdateColorCaches();
            }
            else
                m_state.hidden_dac_reads = 0;
            break;
        case 0x3C7:
            m_state.hidden_dac_reads = 0;
            m_state.dac_address = value;
            m_state.dac_index = value + 1;
            m_state.dac_component = 0;
            m_state.dac_state = 0x03;
            break;
        case 0x3C8:
            m_state.hidden_dac_reads = 0;
            m_state.dac_address = value;
            m_state.dac_index = value;
            m_state.dac_component = 0;
            m_state.dac_state = 0x00;
            break;
        case 0x3C9:
            m_state.hidden_dac_reads = 0;
            WriteDAC(value, clocks);
            break;
        case 0x3CA:
        case 0x3CC:
            if (m_ega)
                m_state.ega_position[port == 0x3CA ? 1 : 0] = value & 0x03;
            break;
        case 0x3CE:
            m_state.graphics_index = value & (m_cirrus ? 0x3F : 0x0F);
            break;
        case 0x3CF:
            WriteGraphics(value, clocks);
            break;
        case 0x3B4:
        case 0x3D4:
            if (IsCRTCBlockSelected(port))
                m_state.crtc_index = value & (m_ega ? 0x1F : 0x3F);
            break;
        case 0x3B5:
        case 0x3D5:
            if (IsCRTCBlockSelected(port))
                WriteCRTC(value, clocks);
            break;
        case 0x3BA:
        case 0x3DA:
            if (IsCRTCBlockSelected(port))
                m_state.feature = m_ega ? value & 0x03 : value;
            break;
        default:
            break;
    }
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

    if (index >= (m_ega ? k_video_ar_color_select : VIDEO_ATTRIBUTE_COUNT))
        return;

    if (index < k_video_ar_mode && (m_state.attribute_index & 0x20) != 0)
        return;

    switch (index)
    {
        case k_video_ar_mode:
            value &= m_ega ? 0x0F : 0xEF;
            break;
        case k_video_ar_overscan:
            value &= m_ega ? 0x3F : 0xFF;
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
    u8 index = m_state.sequencer_index & (m_cirrus ? 0x1F : (m_ega ? 0x0F : 0x07));

    if (index >= VIDEO_SEQUENCER_COUNT)
    {
        if (m_cirrus)
            WriteCirrusSequencer(index, value, clocks);

        return;
    }

    if (!m_cirrus || index != k_video_sr_map_mask || (m_state.graphics[k_video_gr_extensions] & 0x04) == 0)
        value &= m_ega ? k_video_ega_sequencer_masks[index] : k_video_sequencer_masks[index];

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
    {
        if (m_cirrus)
            WriteCirrusGraphics(index, value, clocks);

        return;
    }

    u8 mask = m_ega ? k_video_ega_graphics_masks[index] : k_video_graphics_masks[index];

    if (m_cirrus && index <= k_video_gr_enable_set_reset)
        mask = 0xFF;
    else if (m_cirrus && index == k_video_gr_mode && (m_state.graphics[k_video_gr_extensions] & 0x04) != 0)
        mask = 0x7F;

    value &= mask;

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
    {
        if (m_cirrus)
            WriteCirrusCRTC(index, value, clocks);

        return;
    }

    if (m_ega)
        value &= k_video_ega_crtc_masks[index];
    else if ((m_state.crtc[k_video_cr_retrace_end] & 0x80) != 0)
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

                if (unlikely(m_trace_logger->IsEventEnabled(TRACE_VIDEO, TRACE_VIDEO_VSYNC_CLEAR)))
                    m_trace.deferred |= 0x01;
            }

            UpdateIRQ();
            break;
        default:
            break;
    }
}

void Video::WriteMisc(u8 value, u64 clocks)
{
    if (value == m_state.misc)
        return;

    bool colors = m_ega && ((value ^ m_state.misc) & 0x80) != 0;

    if (colors)
        RenderLineToBeam(clocks);

    m_state.misc = value;
    UpdateMemoryAccess();

    if (colors)
        UpdateColorCaches();
}

// A color reaches the DAC with its third component
void Video::WriteDAC(u8 value, u64 clocks)
{
    u8 component = m_state.dac_component;
    u8 index = m_state.dac_address;

    m_state.dac_cache[component] = value & 0x3F;

    if (component < 2)
    {
        m_state.dac_component = component + 1;
        return;
    }

    m_state.dac_component = 0;
    m_state.dac_address = index + 1;
    m_state.dac_index++;

    // SR12 bit 1 turns the DAC's address to the 16 extra entries of the cursor and the border
    if (IsUnlocked() && (m_state.sequencer[k_video_sr_cursor_attributes] & 0x02) != 0)
    {
        memcpy(m_state.dac_extended[index & (VIDEO_DAC_EXT_COUNT - 1)], m_state.dac_cache, 3);
        return;
    }

    if (memcmp(m_state.dac[index], m_state.dac_cache, 3) == 0)
        return;

    RenderLineToBeam(clocks);
    memcpy(m_state.dac[index], m_state.dac_cache, 3);
    UpdateDACColor(index);
}

u8 Video::ReadDAC()
{
    u8 index = m_state.dac_address;
    u8 value = IsUnlocked() && (m_state.sequencer[k_video_sr_cursor_attributes] & 0x02) != 0 ?
        m_state.dac_extended[index & (VIDEO_DAC_EXT_COUNT - 1)][m_state.dac_component] :
        m_state.dac[index][m_state.dac_component];

    if (++m_state.dac_component == 3)
    {
        m_state.dac_component = 0;
        m_state.dac_address++;
        m_state.dac_index++;
    }

    return value;
}

// Past the register file sit the readbacks IBM never documented, all read only
u8 Video::PeekCRTC() const
{
    u8 latches[4];

    switch (m_state.crtc_index)
    {
        case k_video_cr_latch_readback:
            memcpy(latches, &m_state.latch, sizeof(latches));
            return latches[m_state.graphics[k_video_gr_read_map] & 0x03];
        case k_video_cr_toggle_readback:
            return (m_state.attribute_data ? 0x80 : 0x00) | m_state.attribute_index;
        case k_video_cr_index_readback:
            return m_state.attribute_index;
        default:
            break;
    }

    if (m_state.crtc_index < VIDEO_CRTC_COUNT)
        return m_state.crtc[m_state.crtc_index];

    if (!m_cirrus)
        return 0x00;

    switch (m_state.crtc_index)
    {
        case k_video_cr_interlace_end:
        case k_video_cr_misc_control:
        case k_video_cr_extended_display:
        case k_video_cr_overlay:
        case k_video_cr_part_status:
        case k_video_cr_id:
            return m_state.crtc[m_state.crtc_index];
        default:
            return 0xFF;
    }
}

u8 Video::PeekEGA(u16 port, u64 clocks) const
{
    switch (port)
    {
        case 0x3C2:
        {
            u32 shift = 3 - ((m_state.misc >> 2) & 0x03);
            return (m_state.vertical_interrupt ? 0x80 : 0x00) | (((k_video_ega_switches >> shift) & 0x01) << 4);
        }
        case 0x3B5:
        case 0x3D5:
            return IsCRTCBlockSelected(port) ? PeekEGACRTC() : 0xFF;
        case 0x3BA:
        case 0x3DA:
            return IsCRTCBlockSelected(port) ? (u8)(GetSyncStatus(clocks) | GetEGAVideoStatus(clocks)) : 0xFF;
        default:
            return 0xFF;
    }
}

u8 Video::PeekEGACRTC() const
{
    u8 index = m_state.crtc_index;

    if (index >= k_video_cr_start_high && index <= k_video_cr_cursor_low)
        return m_state.crtc[index];

    return (index == k_video_cr_retrace_start || index == k_video_cr_retrace_end) ? 0x00 : 0xFF;
}

u8 Video::GetEGAVideoStatus(u64 clocks) const
{
    u32 line = m_state.event_line;

    if (!m_state.running || line >= m_state.display_lines || clocks >= m_display_end_clocks || IsScreenBlank())
        return 0x00;

    u64 elapsed = clocks > m_line_start_clocks ? clocks - m_line_start_clocks : 0;
    u32 dot = (u32)((elapsed * m_state.frame_dot_clock) / GDOX_MACHINE_CLOCK_RATE);
    u32 pixel_dots = MAX(m_canvas_h_divider * m_canvas_zoom, 1U);
    u8 select = m_state.attribute[k_video_ar_plane_enable];
    u8 color = m_state.attribute[PeekPixelValue((int)(dot / pixel_dots)) & select & 0x0F] & 0x3F;

    switch ((select >> 4) & 0x03)
    {
        case 0:
            return (u8)(((color & 0x04) << 3) | ((color & 0x01) << 4));
        case 1:
            return (u8)(((color & 0x08) << 2) | ((color & 0x02) << 3));
        case 2:
            return (u8)(color & 0x30);
        default:
            return 0x00;
    }
}

u8 Video::PeekPixelValue(int x) const
{
    const u8* vram = m_state.vram;
    u32 row = m_state.row_scan;
    u32 count_shift = GetCountShift();
    u8 clocking = m_state.sequencer[k_video_sr_clocking];

    if (IsTextMode())
    {
        bool extended = (m_state.sequencer[k_video_sr_memory_mode] & 0x02) != 0;
        u8 map = extended ? m_state.character_map : m_state.character_map & 0x30;
        u8 mode = m_state.attribute[k_video_ar_mode];
        int width = (clocking & 0x01) != 0 ? 8 : 9;
        int shifted = x + GetPixelPan(true, false);
        u32 address = m_state.row_address + ((u32)(shifted / width) >> count_shift);
        int bit = shifted % width;
        u32 base = GetPlaneBase(GetCRTCOffset(address, row));
        u8 character = vram[base];
        u8 attribute = vram[base + 1];
        u32 font_a = ((map >> 2) & 0x03) * 0x4000 + ((map & 0x20) != 0 ? 0x2000 : 0);
        u32 font_b = (map & 0x03) * 0x4000 + ((map & 0x10) != 0 ? 0x2000 : 0);
        u32 font = ((attribute & 0x08) != 0 ? font_a : font_b) + (u32)character * 32 + row;
        u32 bits = (u32)vram[GetPlaneBase(font) + 2] << 1;

        if ((mode & 0x04) != 0 && (character & 0xE0) == 0xC0)
            bits |= (bits >> 1) & 0x01;

        if (((bits >> (8 - bit)) & 0x01) != 0)
            return attribute & 0x0F;

        return (mode & 0x08) != 0 ? (attribute >> 4) & 0x07 : attribute >> 4;
    }

    u32 load_mask = (clocking & 0x10) != 0 ? 0x03 : ((clocking & 0x04) != 0 ? 0x01 : 0x00);
    int shifted = x + GetPixelPan(false, false);
    u32 column = (u32)(shifted / 8);
    int skip = shifted % 8;
    u32 load = column & ~load_mask;
    u32 rotate = column & load_mask;
    const u8* planes = vram + GetPlaneBase(GetCRTCOffset(m_state.row_address + (load >> count_shift), row));
    u8 plane0 = planes[rotate & 0x03];
    u8 plane1 = planes[(rotate + 1) & 0x03];
    u8 plane2 = planes[(rotate + 2) & 0x03];
    u8 plane3 = planes[(rotate + 3) & 0x03];

    if (GetShiftFormat() == VIDEO_FORMAT_CGA)
    {
        int shift = 6 - (skip & 0x03) * 2;
        u8 low = skip < 4 ? plane0 : plane1;
        u8 high = skip < 4 ? plane2 : plane3;
        return (u8)(((low >> shift) & 0x03) | (((high >> shift) & 0x03) << 2));
    }

    int shift = 7 - skip;
    return (u8)(((plane0 >> shift) & 0x01) | (((plane1 >> shift) & 0x01) << 1) | (((plane2 >> shift) & 0x01) << 2) |
        (((plane3 >> shift) & 0x01) << 3));
}

// Past SR06 and GR08, a locked Cirrus reads FFh
u8 Video::PeekSequencer() const
{
    u8 index = m_state.sequencer_index & (m_cirrus ? 0x1F : 0x07);

    if (index < VIDEO_SEQUENCER_COUNT)
        return m_state.sequencer[index];

    if (!m_cirrus || index < k_video_sr_unlock)
        return 0x00;

    if (index > k_video_sr_unlock && !IsUnlocked())
        return 0xFF;

    return m_state.sequencer[index];
}

u8 Video::PeekGraphics() const
{
    u8 index = m_state.graphics_index;

    if (m_cirrus && index <= k_video_gr_enable_set_reset && (m_state.graphics[k_video_gr_extensions] & 0x04) == 0)
        return m_state.graphics[index] & 0x0F;

    if (index < VIDEO_GRAPHICS_COUNT)
        return m_state.graphics[index];

    if (!m_cirrus)
        return 0x00;

    return IsUnlocked() ? m_state.graphics[index] : 0xFF;
}

// SR06 takes the key in bits 0-2 and 4, 12h unlocks and reads back 12h, anything else reads 0Fh (CIRRUS.TXT l1488-1492)
void Video::WriteCirrusSequencer(u8 index, u8 value, u64 clocks)
{
    if (index == k_video_sr_unlock)
    {
        m_state.sequencer[index] = (value & 0x17) == k_video_cirrus_unlock ? k_video_cirrus_unlock : 0x0F;
        return;
    }

    if (index < k_video_sr_unlock || !IsUnlocked())
        return;

    if (index == k_video_sr_cursor_x || index == k_video_sr_cursor_y)
    {
        RenderLineToBeam(clocks);
        u16 position = (u16)((value << 3) | (m_state.sequencer_index >> 5));

        if (index == k_video_sr_cursor_x)
            m_state.cursor_x = position;
        else
            m_state.cursor_y = position;
    }

    // The memory clock and bus width straps of SR0F and the bus type of SR17 are read only
    if (index == k_video_sr_dram)
        value = (value & ~0x1BU) | (m_state.sequencer[index] & 0x1B);
    else if (index == k_video_sr_config)
        value = (value & ~0x38U) | (m_state.sequencer[index] & 0x38);

    if (value == m_state.sequencer[index])
        return;

    RenderLineToBeam(clocks);
    m_state.sequencer[index] = value;

    if (index == k_video_sr_extended)
    {
        UpdateExtendedFormat();
        UpdateMemoryAccess();
        UpdateAperture();
    }
    else if (index == k_video_sr_config)
    {
        UpdateMemoryAccess();
        UpdateVRAMMapping();
    }
}

void Video::WriteCirrusGraphics(u8 index, u8 value, u64 clocks)
{
    if (!IsUnlocked())
        return;

    if (index == k_video_gr_blt_status)
    {
        WriteBltControl(value, clocks);
        return;
    }

    if (index >= k_video_gr_blt_width && index <= k_video_gr_blt_extensions)
    {
        m_state.graphics[index] = value;

        if (index == k_video_gr_blt_dst + 2 && m_pci && (m_state.graphics[k_video_gr_blt_status] & 0x80) != 0 &&
            !m_state.blt_waiting)
            StartBlt(clocks);

        return;
    }

    if (value == m_state.graphics[index])
        return;

    RenderLineToBeam(clocks);

    if (index == k_video_gr_extensions && m_pci && (value & 0x04) == 0)
    {
        m_state.graphics[k_video_gr_background] &= 0x0F;
        m_state.graphics[k_video_gr_foreground] &= 0x0F;
    }

    m_state.graphics[index] = value;

    if (index == k_video_gr_offset_0 || index == k_video_gr_offset_1 || index == k_video_gr_extensions)
        UpdateMemoryAccess();

    if (index == k_video_gr_extensions)
        UpdateAperture();
}

// The 5428 has CR19-CR1B, CR25 and CR27 past the VGA's, the last two read only. CR1D is the 5429's, kept and read back
// since SeaVGABIOS reads its start address bit 19, which only the 543x use
void Video::WriteCirrusCRTC(u8 index, u8 value, u64 clocks)
{
    if (!IsUnlocked() || (index != k_video_cr_interlace_end && index != k_video_cr_misc_control &&
        index != k_video_cr_extended_display && index != k_video_cr_overlay))
        return;

    if (value == m_state.crtc[index])
        return;

    RenderLineToBeam(clocks);
    m_state.crtc[index] = value;
    m_wide_counter = (m_state.crtc[k_video_cr_extended_display] & 0x02) != 0;
}

// Reached by the fifth access to 3C6h after four reads; it picks what the DAC does with the pixels it gets
void Video::WriteHiddenDAC(u8 value, u64 clocks)
{
    if (value == m_state.hidden_dac)
        return;

    RenderLineToBeam(clocks);
    m_state.hidden_dac = value;
    UpdateExtendedFormat();
}

// The hidden DAC picks the format (CIRRUS.TXT l1792-1825), SR07 bits 3-1 the bytes a dot clock brings
void Video::UpdateExtendedFormat()
{
    u8 dac = m_state.hidden_dac;

    if ((dac & 0x80) == 0)
        m_extended_format = VIDEO_EXTENDED_PALETTE;
    else if ((dac & 0x40) == 0)
        m_extended_format = (dac & 0x10) != 0 ? VIDEO_EXTENDED_555_MIX : VIDEO_EXTENDED_555;
    else
    {
        switch (dac & 0x0F)
        {
            case 0x00:
                m_extended_format = (dac & 0x10) != 0 ? VIDEO_EXTENDED_555_MIX : VIDEO_EXTENDED_555;
                break;
            case 0x01:
                m_extended_format = VIDEO_EXTENDED_565;
                break;
            case 0x05:
                m_extended_format = VIDEO_EXTENDED_888;
                break;
            case 0x06:
            case 0x07:
                m_extended_format = VIDEO_EXTENDED_BLANK;
                break;
            case 0x08:
                m_extended_format = VIDEO_EXTENDED_GREY;
                break;
            case 0x09:
                m_extended_format = VIDEO_EXTENDED_332;
                break;
            default:
                m_extended_format = VIDEO_EXTENDED_PALETTE;
                break;
        }
    }

    switch (m_extended_format)
    {
        case VIDEO_EXTENDED_555:
        case VIDEO_EXTENDED_555_MIX:
        case VIDEO_EXTENDED_565:
            m_extended_bytes = 2;
            break;
        case VIDEO_EXTENDED_888:
            m_extended_bytes = 3;
            break;
        default:
            m_extended_bytes = 1;
            break;
    }
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

        if (unlikely(m_trace_logger->IsEventEnabled(TRACE_VIDEO, TRACE_VIDEO_VSYNC)))
        {
            GDOX_Trace_Entry* entry = TraceEvent(TRACE_VIDEO_VSYNC, 0, (u16)m_state.frame_lines, 0, 0,
                m_state.frame_count, clocks);
            entry->flags |= (m_state.crtc[k_video_cr_retrace_end] & 0x10) != 0 ? TRACE_VIDEO_FLAG_IRQ : 0;
            entry->video.count = (u16)m_state.frame_line_dots;
            entry->video.param2 = m_state.running ? (u32)(100000.0f / GetFrameTime()) : 0;
        }
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
    if (m_cga)
    {
        FinishCGALine();
        return;
    }

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
            m_state.row_address += GetOffsetRegister() << 1;
        }
        else
            m_state.row_scan = (m_state.row_scan + 1) & 0x1F;
    }

    if (line == GetLineCompare() * GetVerticalScale())
    {
        m_state.row_address = 0;
        m_state.row_scan = 0;
        m_state.row_repeat = false;

        if ((m_state.attribute[k_video_ar_mode] & 0x20) != 0)
            m_state.pan_reset = true;
    }

    // A font change waits for the next whole character row
    if (m_state.row_scan == 0 && !m_state.row_repeat)
        m_state.character_map = m_state.sequencer[k_video_sr_character_map];
}

void Video::LatchFrameTiming()
{
    if (m_cga)
    {
        LatchCGATiming();
        return;
    }

    u32 display = ((u32)m_state.crtc[k_video_cr_horizontal_display] + 1) * GetCharacterClockDots();
    u32 scale = GetVerticalScale();
    u32 retrace = GetVerticalRetraceStart() * scale;
    u32 retrace_length = ((m_state.crtc[k_video_cr_retrace_end] - GetVerticalRetraceStart()) & 0x0F) * scale;

    m_state.frame_line_dots = GetLineDots();
    m_state.frame_display_dots = MIN(display, m_state.frame_line_dots);
    m_state.frame_lines = GetTotalLines() * scale;
    m_state.frame_dot_clock = GetDotClock();
    m_state.display_lines = MIN((GetVerticalDisplayEnd() + 1) * scale, m_state.frame_lines);
    m_state.retrace_start = retrace;
    m_state.retrace_end = MIN(retrace + (retrace_length != 0 ? retrace_length : 16), m_state.frame_lines);
    m_state.ready_line = MIN(MAX(retrace, m_state.display_lines), m_state.frame_lines);
}

void Video::StartFrame(u64 clocks)
{
    m_state.frame_start_clocks = clocks;
    m_state.event_line = 0;

    if (m_state.running && m_cga)
    {
        LatchFrameTiming();
        m_state.start_address = m_mc6845->GetStartAddress();
        m_state.row_address = m_state.start_address;
        m_state.row_scan = 0;
        m_state.row_repeat = false;
        m_state.pan_reset = false;
    }
    else if (m_state.running)
    {
        LatchFrameTiming();
        m_state.row_address = m_state.start_address;
        m_state.row_scan = m_state.crtc[k_video_cr_preset_row] & 0x1F;
        m_state.row_repeat = false;
        m_state.pan_reset = false;
        m_state.character_map = m_state.sequencer[k_video_sr_character_map];
    }

    m_partial_row = -1;
    m_partial_x = 0;
    UpdateGeometry();
    UpdateNextEvent();

    if (unlikely(m_trace_logger->IsEnabled(TRACE_VIDEO)))
        TraceFrameStart(clocks);
}

// The picture is finished as soon as retrace starts, which is also when the start address latches
void Video::CompleteFrame()
{
    bool blink = GetBlinkPhase();

    if (!m_voodoo_shown)
    {
        m_frame_width = m_render_width;
        m_frame_height = m_render_height;
        m_display_width = m_canvas_width;
        m_display_height = m_canvas_height;
        m_frame_ready = true;
    }

    m_state.frame_count++;

    if (m_cga)
        return;

    m_state.start_address = (GetStartAddress() + ((m_state.crtc[k_video_cr_preset_row] >> 5) & 0x03)) & GetCounterMask();

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_VIDEO, TRACE_VIDEO_FRAME)))
        TraceFrameComplete();

    if (blink != GetBlinkPhase() && (m_state.attribute[k_video_ar_mode] & 0x09) == 0x09)
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
        m_edge_clocks = GDOX_NO_EVENT;
        m_vram_deadline = 0;
        ScheduleEvent();
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

    // The CPU stops only where the frame does something it sees: the retrace interrupt, the finished picture and the
    // next frame. The lines between them run when a port, video memory or one of those looks
    u32 edge = m_state.frame_lines;

    if (line < m_state.retrace_start)
        edge = MIN(edge, m_state.retrace_start);

    if (line < m_state.ready_line)
        edge = MIN(edge, m_state.ready_line);

    m_edge_clocks = GetLineClocks(edge);
    m_vram_deadline = 0;
    ScheduleEvent();
}

void Video::ScheduleEvent()
{
    u64 clocks = m_edge_clocks;

    if (m_voodoo_enabled)
        clocks = MIN(clocks, m_voodoo->GetEventClocks());

    m_scheduler->Schedule(SCHEDULER_EVENT_VIDEO, clocks);
}

void Video::UpdateVoodooOutput()
{
    m_voodoo_shown = m_voodoo_enabled && m_voodoo->IsDisplayed();
    m_render = m_render_requested && !m_voodoo_shown;
}

void Video::CompleteVoodooFrame()
{
    m_frame_width = m_voodoo->GetFrameWidth();
    m_frame_height = m_voodoo->GetFrameHeight();
    m_display_width = m_frame_width;
    m_display_height = m_frame_height;

    if (m_render_requested && IsValidPointer(m_frame_buffer))
        m_voodoo->RenderFrame((u32*)m_frame_buffer);

    m_frame_ready = true;
}

void Video::SetStartCycles(const u32* cycles)
{
    m_start_cycles = cycles;
    m_vram_deadline = 0;
}

// The deadline counts CPU cycles from the slice's start
void Video::BeginSlice()
{
    m_vram_deadline = 0;
}

// A write runs the lines the beam has finished first, so they show video memory as their line events did. Until the
// next line ends, measured in the CPU's cycles, writes go straight through
void Video::SynchronizeVRAMWrite()
{
    GDOX_Bus_Access_Context* context = m_memory->GetAccessContext();

    if (IsValidPointer(context) && IsValidPointer(context->synchronize_memory))
        context->synchronize_memory(context->synchronize_context, *context);

    Synchronize(m_scheduler->GetClocks());

    if (IsValidPointer(context) && IsValidPointer(context->synchronize_memory))
        m_vram_deadline = context->synchronized_cycles + m_scheduler->GetCyclesBefore(m_next_event_clocks) + 1;
}

// The canvas is the active area in pixels as the program addresses them, doubled up to dots with native resolution off
void Video::UpdateGeometry()
{
    if (m_state.running && m_cga)
    {
        UpdateCGAGeometry();
        return;
    }

    if (!m_state.running)
    {
        m_render_width = GDOX_FRAME_BUFFER_WIDTH;
        m_render_height = GDOX_FRAME_BUFFER_HEIGHT;
        m_canvas_width = GDOX_FRAME_BUFFER_WIDTH;
        m_canvas_height = GDOX_FRAME_BUFFER_HEIGHT;
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

    // Extended modes give a pixel a dot whatever AR10 says, or two and three dots when the clock runs at 2 and 3 times
    // the pixel rate for 16 and 24 bits; a character clock is 8 pixels either way (CIRRUS.TXT l1497-1516)
    if (m_extended)
    {
        text = false;
        pixel_dots = (((clocking & 0x08) != 0) ? 2 : 1) * GetExtendedClockMultiplier();
        width = ((u32)m_state.crtc[k_video_cr_horizontal_display] + 1) * ((clocking & 0x01) != 0 ? 8 : 9);
    }

    if (!text && (m_state.crtc[k_video_cr_mode] & 0x03) == 0x03)
        v_divider *= (u32)(max_scan & 0x1F) + 1;

    // With native resolution off, a mode whose dots don't fit the frame buffer keeps one pixel a pixel
    if (!m_native_resolution && width * pixel_dots <= (u32)GDOX_MAX_FRAME_BUFFER_WIDTH)
    {
        m_canvas_h_divider = 1;
        m_canvas_v_divider = 1;
        m_canvas_zoom = pixel_dots;
    }
    else
    {
        m_canvas_h_divider = pixel_dots;
        m_canvas_v_divider = m_native_resolution ? v_divider : 1;
        m_canvas_zoom = 1;
    }

    u32 height = (m_state.display_lines + m_canvas_v_divider - 1) / m_canvas_v_divider;

    m_render_width = (int)MIN(width * m_canvas_zoom, (u32)GDOX_MAX_FRAME_BUFFER_WIDTH);
    m_render_height = (int)CLAMP(height, 1U, (u32)GDOX_MAX_FRAME_BUFFER_HEIGHT);

    u32 monitor_dots = pixel_dots / GetExtendedClockMultiplier();
    m_canvas_width = (int)(((u32)m_render_width * monitor_dots) / m_canvas_zoom);
    m_canvas_height = m_render_height * (int)m_canvas_v_divider;

    if (m_ega && !IsEGA350Lines())
        m_canvas_height *= 2;
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
    bool blink = (mode & 0x09) == 0x09;
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
    if (m_cga)
    {
        UpdateCGAColors();
        return;
    }

    if (m_ega)
    {
        UpdateEGAColors();
        return;
    }

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

void Video::UpdateEGAColors()
{
    bool high = IsEGA350Lines();

    for (int i = 0; i < VIDEO_DAC_COUNT; i++)
    {
        u32 color = (u32)i & (VIDEO_EGA_COLOR_COUNT - 1);
        u32 red;
        u32 green;
        u32 blue;

        if (high)
        {
            red = ((color >> 1) & 0x02) | ((color >> 5) & 0x01);
            green = (color & 0x02) | ((color >> 4) & 0x01);
            blue = ((color << 1) & 0x02) | ((color >> 3) & 0x01);
        }
        else
        {
            u32 intensity = (color >> 4) & 0x01;
            red = ((color >> 1) & 0x02) | intensity;
            green = (color & 0x17) == 0x06 ? 0x01 : (color & 0x02) | intensity;
            blue = ((color << 1) & 0x02) | intensity;
        }

        m_dac_rgb[i] = ToXRGB((u8)(red * 0x55), (u8)(green * 0x55), (u8)(blue * 0x55));
        m_dac_colors[i] = m_dac_rgb[i];
    }

    UpdateAttributeColors();
}

u8 Video::GetTraceAdapter() const
{
    if (m_cga)
        return TRACE_VIDEO_ADAPTER_CGA;

    if (m_cirrus)
        return TRACE_VIDEO_ADAPTER_CIRRUS;

    return m_ega ? TRACE_VIDEO_ADAPTER_EGA : TRACE_VIDEO_ADAPTER_VGA;
}

// Index writes only select, so the events are the data writes with what the register holds afterwards
void Video::WriteTraced(u16 port, u8 value, u64 clocks)
{
    bool attribute_data = m_state.attribute_data;
    u8 attribute_index = m_state.attribute_index & 0x1F;
    bool hidden = port == 0x3C6 && IsUnlocked() && m_state.hidden_dac_reads == 4;
    u8 dac_component = m_state.dac_component;
    u8 dac_address = m_state.dac_address;
    u8 sequencer_index = m_state.sequencer_index;
    u8 sr = sequencer_index & (m_cirrus ? 0x1F : (m_ega ? 0x0F : 0x07));
    u8 gr = m_state.graphics_index;
    u8 cr = m_state.crtc_index;

    ApplyWrite(port, value, clocks);

    switch (port)
    {
        case 0x3C0:
        case 0x3C1:
        {
            if (!attribute_data || (port == 0x3C1 && !m_ega))
                break;

            bool stored = attribute_index < VIDEO_ATTRIBUTE_COUNT;
            GDOX_Trace_Entry* entry = TraceEvent(TRACE_VIDEO_OUTPUT, attribute_index,
                stored ? m_state.attribute[attribute_index] : value, value, TRACE_VIDEO_BANK_AR, 0, clocks);

            if (IsValidPointer(entry) && (!stored || m_state.attribute[attribute_index] != value))
                entry->flags |= TRACE_ENTRY_IGNORED;
            break;
        }
        case 0x3C2:
            TraceEvent(TRACE_VIDEO_OUTPUT, 0, m_state.misc, value, TRACE_VIDEO_BANK_MISC, 0, clocks);
            break;
        case 0x3C5:
        {
            bool stored = sr < VIDEO_SEQUENCER_COUNT || (m_cirrus && sr < VIDEO_SEQUENCER_EXT_COUNT);
            u8 kept = stored ? m_state.sequencer[sr] : value;
            GDOX_Trace_Entry* entry = TraceEvent(sr == 0x02 ? TRACE_VIDEO_DATAPATH : TRACE_VIDEO_OUTPUT, sr, kept, value,
                TRACE_VIDEO_BANK_SR, 0, clocks);

            if (!IsValidPointer(entry))
                break;

            if (!stored || kept != value)
                entry->flags |= TRACE_ENTRY_IGNORED;

            if (m_cirrus && (sr == 0x10 || sr == 0x11))
                entry->video.index_bits = sequencer_index >> 5;
            break;
        }
        case 0x3C6:
            FlushPaletteLoad();

            if (hidden)
                TraceEvent(TRACE_VIDEO_DAC, 0, m_state.hidden_dac, value, TRACE_VIDEO_BANK_HIDDEN_DAC,
                    GetExtendedBytesPerPixel(), clocks);
            else
                TraceEvent(TRACE_VIDEO_DAC, 0, m_state.pel_mask, value, TRACE_VIDEO_BANK_PEL_MASK, 0, clocks);
            break;
        case 0x3C7:
        case 0x3C8:
            FlushPaletteLoad();
            TraceEvent(TRACE_VIDEO_DAC, value, value, value, port == 0x3C7 ? TRACE_VIDEO_BANK_DAC_READ :
                TRACE_VIDEO_BANK_DAC_WRITE, 0, clocks);
            break;
        case 0x3C9:
            if (dac_component == 2)
                TraceColor(dac_address, clocks);
            break;
        case 0x3CA:
        case 0x3CC:
            if (m_ega)
                TraceEvent(TRACE_VIDEO_OUTPUT, port == 0x3CA ? 1 : 0, m_state.ega_position[port == 0x3CA ? 1 : 0], value,
                    TRACE_VIDEO_BANK_EGA_POSITION, 0, clocks);
            break;
        case 0x3CF:
        {
            bool stored = gr < VIDEO_GRAPHICS_COUNT || (m_cirrus && gr < VIDEO_GRAPHICS_EXT_COUNT);
            u8 kept = stored ? m_state.graphics[gr] : value;
            u8 event = gr <= 0x04 || gr == 0x08 ? TRACE_VIDEO_DATAPATH :
                m_cirrus && gr >= 0x20 && gr <= 0x39 ? TRACE_VIDEO_BLT_REGISTER : TRACE_VIDEO_OUTPUT;
            u32 bank = m_cirrus && (gr == 0x09 || gr == 0x0A) ? GetBankOffset(gr - 0x09) : 0;
            GDOX_Trace_Entry* entry = TraceEvent(event, gr, kept, value, TRACE_VIDEO_BANK_GR, bank, clocks);

            if (!IsValidPointer(entry))
                break;

            if (!stored || kept != value)
                entry->flags |= TRACE_ENTRY_IGNORED;

            if (m_cirrus && (gr == 0x09 || gr == 0x0A))
                entry->flags |= TRACE_VIDEO_FLAG_BANK;
            break;
        }
        case 0x3B5:
        case 0x3D5:
        {
            if (!IsCRTCBlockSelected(port))
                break;

            bool stored = cr < VIDEO_CRTC_COUNT || (m_cirrus && cr < VIDEO_CRTC_EXT_COUNT);
            u8 kept = stored ? m_state.crtc[cr] : value;
            GDOX_Trace_Entry* entry = TraceEvent(TRACE_VIDEO_CRTC, cr, kept, value, 0, 0, clocks);

            if (IsValidPointer(entry) && (!stored || kept != value))
                entry->flags |= TRACE_ENTRY_IGNORED;

            if (cr == 0x0C || cr == 0x0D)
                m_trace.start_line = m_state.running ? (u16)GetBeamLine(clocks) : 0xFFFF;
            break;
        }
        default:
            break;
    }

    FlushDeferred(clocks);
}

void Video::FlushDeferred(u64 clocks)
{
    if (m_trace.deferred == 0)
        return;

    if ((m_trace.deferred & 0x01) != 0)
        TraceEvent(TRACE_VIDEO_VSYNC_CLEAR, 0, 0, 0, 0, m_state.frame_count, clocks);

    if ((m_trace.deferred & 0x02) != 0)
        m_trace_logger->TraceLog(m_trace_blt);

    m_trace.deferred = 0;
}

GDOX_Trace_Entry* Video::TraceEvent(u8 event, u8 reg, u16 value, u8 raw, u8 bank, u32 param, u64 clocks)
{
    if (!m_trace_logger->IsEventEnabled(TRACE_VIDEO, event))
        return NULL;

    if (event != TRACE_VIDEO_PALETTE && m_trace.load_count != 0)
        FlushPaletteLoad();

    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_VIDEO, event);
    entry->video.param = param;
    entry->video.value = value;
    entry->video.reg = reg;
    entry->video.raw = raw;
    entry->video.bank = bank;
    entry->video.adapter = GetTraceAdapter();
    entry->video.line = 0xFFFF;

    if (m_state.running)
    {
        entry->video.line = (u16)GetBeamLine(clocks);
        entry->video.dot = (u16)GetBeamDot(clocks);
    }

    return entry;
}

void Video::TraceColor(u8 index, u64 clocks)
{
    const u8* color = m_state.dac_cache;
    u32 rgb = ((u32)color[0] << 16) | ((u32)color[1] << 8) | color[2];
    bool extended = IsUnlocked() && (m_state.sequencer[k_video_sr_cursor_attributes] & 0x02) != 0;
    u16 line = m_state.running ? (u16)GetBeamLine(clocks) : 0xFFFF;

    if (m_trace_logger->IsEventEnabled(TRACE_VIDEO, TRACE_VIDEO_PALETTE))
    {
        GDOX_Trace_Entry* entry = TraceEvent(TRACE_VIDEO_PALETTE, index, 0, 0, 0, rgb, clocks);
        entry->flags |= extended ? TRACE_VIDEO_FLAG_EXTENDED_DAC : 0;
    }

    if (!m_trace_logger->IsEventEnabled(TRACE_VIDEO, TRACE_VIDEO_PALETTE_LOAD))
        return;

    if (m_trace.load_count != 0 && (index != (u8)m_trace.load_next || extended != m_trace.load_extended))
        FlushPaletteLoad();

    if (m_trace.load_count == 0)
    {
        m_trace.load_clocks = m_trace_logger->GetClocks();
        m_trace.load_first = index;
        m_trace.load_first_rgb = rgb;
        m_trace.load_first_line = line;
        m_trace.load_extended = extended;
    }

    m_trace.load_last_rgb = rgb;
    m_trace.load_last_line = line;
    m_trace.load_next = (u8)(index + 1);
    m_trace.load_count++;
}

void Video::FlushPaletteLoad()
{
    if (m_trace.load_count == 0)
        return;

    u16 count = m_trace.load_count;
    m_trace.load_count = 0;

    if (!m_trace_logger->IsEventEnabled(TRACE_VIDEO, TRACE_VIDEO_PALETTE_LOAD))
        return;

    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_VIDEO, TRACE_VIDEO_PALETTE_LOAD, m_trace.load_clocks);
    entry->flags |= m_trace.load_extended ? TRACE_VIDEO_FLAG_EXTENDED_DAC : 0;
    entry->video.reg = (u8)m_trace.load_first;
    entry->video.count = count;
    entry->video.param = m_trace.load_first_rgb;
    entry->video.param2 = m_trace.load_last_rgb;
    entry->video.line = m_trace.load_first_line;
    entry->video.last_line = m_trace.load_last_line;
    entry->video.adapter = GetTraceAdapter();
}

void Video::TraceFrameStart(u64 clocks)
{
    if (m_trace.status_reads != 0 && m_trace_logger->IsEventEnabled(TRACE_VIDEO, TRACE_VIDEO_STATUS))
    {
        GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_VIDEO, TRACE_VIDEO_STATUS);
        entry->status.reads = m_trace.status_reads;
        entry->status.retrace = m_trace.status_retrace;
        entry->status.display_off = m_trace.status_off;
        entry->status.frame = m_state.frame_count;
        entry->status.first_line = m_trace.status_first_line;
        entry->status.last_line = m_trace.status_last_line;
    }

    m_trace.status_reads = 0;
    m_trace.status_retrace = 0;
    m_trace.status_off = 0;

    if (!m_trace_logger->IsEventEnabled(TRACE_VIDEO, TRACE_VIDEO_MODE))
        return;

    bool text = m_cga ? (m_state.cga_mode & 0x02) == 0 : IsTextMode();
    u8 format = 0;
    u8 bpp = 0;

    if (m_cga)
        format = text ? 0 : 4;
    else if (!text && IsExtendedMode())
    {
        format = 5;
        bpp = (u8)GetExtendedBytesPerPixel();
    }
    else if (!text)
    {
        Video_Format shift = GetShiftFormat();
        format = shift == VIDEO_FORMAT_PLANAR ? 1 : shift == VIDEO_FORMAT_CGA ? 2 : 3;
        bpp = shift == VIDEO_FORMAT_256 ? 1 : 0;
    }

    u32 refresh = m_state.running ? (u32)(100000.0f / GetFrameTime()) : 0;
    u8 char_width = text && !m_cga ? (u8)GetCharacterClockDots() : 8;
    u8 char_height = text && !m_cga ? (u8)((m_state.crtc[k_video_cr_max_scan] & 0x1F) + 1) : 0;
    u8 misc = m_cga ? m_state.cga_mode : m_state.misc;
    u8 extended = !m_cga && IsExtendedMode() ? (u8)GetExtendedFormat() : 0;
    u32 signature[6] = { (u32)m_canvas_width | ((u32)m_canvas_height << 16),
        (u32)char_width | ((u32)char_height << 8) | ((u32)format << 16) | ((u32)extended << 24),
        (u32)bpp | ((u32)misc << 8) | ((u32)GetCard() << 16) | ((u32)GetTraceAdapter() << 24),
        m_cga ? 0 : GetOffsetRegister(), (refresh + 50) / 100, text ? 1U : 0U };

    if (memcmp(signature, m_trace.mode, sizeof(signature)) == 0)
        return;

    memcpy(m_trace.mode, signature, sizeof(signature));
    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_VIDEO, TRACE_VIDEO_MODE);
    entry->mode.refresh = refresh;
    entry->mode.offset = signature[3];
    entry->mode.start = m_state.start_address;
    entry->mode.width = (u16)m_canvas_width;
    entry->mode.height = (u16)m_canvas_height;
    entry->mode.lines = (u16)m_state.frame_lines;
    entry->mode.char_width = char_width;
    entry->mode.char_height = char_height;
    entry->mode.format = format;
    entry->mode.extended = extended;
    entry->mode.bpp = bpp;
    entry->mode.misc = misc;
    entry->mode.card = (u8)GetCard();
    entry->mode.adapter = GetTraceAdapter();
    UNUSED(clocks);
}

void Video::TraceFrameComplete()
{
    u32 values[4] = { m_state.start_address, GetLineCompare(), GetOffsetRegister(),
        (u32)(m_state.attribute[0x13] & 0x0F) | ((u32)((m_state.crtc[k_video_cr_preset_row] >> 5) & 0x03) << 8) };

    if (memcmp(values, m_trace.frame, sizeof(values)) == 0)
        return;

    memcpy(m_trace.frame, values, sizeof(values));
    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_VIDEO, TRACE_VIDEO_FRAME);
    entry->frame.start = values[0];
    entry->frame.line_compare = values[1];
    entry->frame.offset = values[2];
    entry->frame.frame = m_state.frame_count;
    entry->frame.pel = (u8)(values[3] & 0x0F);
    entry->frame.byte_pan = (u8)(values[3] >> 8);
    entry->frame.line = m_trace.start_line;
}

void Video::CountStatusRead(u64 clocks)
{
    u8 status = GetSyncStatus(clocks);
    u16 line = m_state.running ? (u16)GetBeamLine(clocks) : 0xFFFF;

    if (m_trace.status_reads == 0)
        m_trace.status_first_line = line;

    m_trace.status_last_line = line;
    m_trace.status_reads++;
    m_trace.status_retrace += (status & 0x08) != 0 ? 1 : 0;
    m_trace.status_off += (status & 0x01) != 0 ? 1 : 0;
}

void Video::TraceBlt(u8 event, u64 clocks)
{
    const u8* graphics = m_state.graphics;
    u8 mode = graphics[k_video_gr_blt_mode];
    u8 extensions = m_pci ? graphics[k_video_gr_blt_extensions] : 0x00;
    bool system = (mode & 0x04) != 0 || (!m_pci && (mode & 0x02) != 0);
    GDOX_Trace_Entry* entry = &m_trace_blt;

    if (event == TRACE_VIDEO_BLT_DONE)
        entry = m_trace_logger->Begin(TRACE_VIDEO, event);
    else
    {
        memset(entry, 0, sizeof(GDOX_Trace_Entry));
        entry->type = TRACE_VIDEO;
        entry->event = event;
        m_trace.deferred |= 0x02;
    }

    entry->blt.destination = m_state.blt_dst;
    entry->blt.source = m_state.blt_src;
    entry->blt.colors = (u32)graphics[0x01] | ((u32)graphics[0x11] << 8) | ((u32)graphics[0x00] << 16) |
        ((u32)graphics[0x10] << 24);
    entry->blt.key = (u32)graphics[k_video_gr_blt_key] | ((u32)graphics[k_video_gr_blt_key + 1] << 8) |
        ((u32)graphics[k_video_gr_blt_key_mask] << 16) | ((u32)graphics[k_video_gr_blt_key_mask + 1] << 24);
    entry->blt.width = (u16)GetBltWidth();
    entry->blt.height = (u16)GetBltHeight();
    entry->blt.dst_pitch = (u16)GetBltPitch(k_video_gr_blt_dst_pitch);
    entry->blt.src_pitch = (u16)GetBltPitch(k_video_gr_blt_src_pitch);
    entry->blt.mode = mode;
    entry->blt.rop = graphics[k_video_gr_blt_rop];
    entry->blt.extensions = extensions;
    entry->blt.pixel_bytes = (u8)GetBltPixelBytes();
    entry->blt.kind = system ? VIDEO_BLT_SYSTEM : ((mode & 0x80) != 0 || (extensions & 0x04) != 0) ? VIDEO_BLT_EXPAND :
        (mode & 0x40) != 0 ? VIDEO_BLT_PATTERN : VIDEO_BLT_SCREEN;
    entry->blt.line = m_state.running ? (u16)GetBeamLine(clocks) : 0xFFFF;
    entry->blt.dot = m_state.running ? (u16)GetBeamDot(clocks) : 0;
}

void Video::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer, GDOX_SAVESTATE_VERSION);
    m_mc6845->SaveState(stream);

    if (m_voodoo_enabled)
        m_voodoo->SaveState(stream);
}

void Video::LoadState(std::istream& stream, int version)
{
    StateSerializer serializer(stream);
    Serialize(serializer, version);
    m_mc6845->LoadState(stream, version);

    if (m_voodoo_enabled)
        m_voodoo->LoadState(stream);

    SanitizeState();
}

void Video::Serialize(StateSerializer& serializer, int version)
{
    UNUSED(version);
    G_SERIALIZE_ARRAY(serializer, m_state.vram, m_vram_size);
    G_SERIALIZE_ARRAY(serializer, m_state.sequencer, VIDEO_SEQUENCER_EXT_COUNT);
    G_SERIALIZE(serializer, m_state.sequencer_index);
    G_SERIALIZE_ARRAY(serializer, m_state.graphics, VIDEO_GRAPHICS_EXT_COUNT);
    G_SERIALIZE(serializer, m_state.graphics_index);
    G_SERIALIZE_ARRAY(serializer, m_state.crtc, VIDEO_CRTC_EXT_COUNT);
    G_SERIALIZE(serializer, m_state.crtc_index);
    G_SERIALIZE_ARRAY(serializer, m_state.attribute, VIDEO_ATTRIBUTE_COUNT);
    G_SERIALIZE(serializer, m_state.attribute_index);
    G_SERIALIZE(serializer, m_state.attribute_data);
    G_SERIALIZE(serializer, m_state.misc);
    G_SERIALIZE(serializer, m_state.feature);
    G_SERIALIZE(serializer, m_state.vga_enable);
    G_SERIALIZE_ARRAY(serializer, &m_state.dac[0][0], sizeof(m_state.dac));
    G_SERIALIZE_ARRAY(serializer, m_state.dac_cache, 3);
    G_SERIALIZE(serializer, m_state.dac_address);
    G_SERIALIZE(serializer, m_state.dac_index);
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
    G_SERIALIZE(serializer, m_state.character_map);
    G_SERIALIZE(serializer, m_state.hidden_dac);
    G_SERIALIZE(serializer, m_state.hidden_dac_reads);
    G_SERIALIZE_ARRAY(serializer, &m_state.dac_extended[0][0], sizeof(m_state.dac_extended));
    G_SERIALIZE_ARRAY(serializer, m_state.ega_position, 2);
    G_SERIALIZE(serializer, m_state.cga_mode);
    G_SERIALIZE(serializer, m_state.cga_color);

    if (m_pci)
    {
        G_SERIALIZE(serializer, m_state.pci_command);
        G_SERIALIZE(serializer, m_state.pci_base);
        G_SERIALIZE(serializer, m_state.pci_line);
    }

    if (m_cirrus)
    {
        G_SERIALIZE(serializer, m_state.latch_high);
        G_SERIALIZE(serializer, m_state.cursor_x);
        G_SERIALIZE(serializer, m_state.cursor_y);
        G_SERIALIZE(serializer, m_state.blt_waiting);
        G_SERIALIZE(serializer, m_state.blt_suspended);
        G_SERIALIZE(serializer, m_state.blt_dst);
        G_SERIALIZE(serializer, m_state.blt_src);
        G_SERIALIZE(serializer, m_state.blt_line);
        G_SERIALIZE(serializer, m_state.blt_count);
        G_SERIALIZE(serializer, m_state.blt_fed);
        G_SERIALIZE_ARRAY(serializer, m_state.blt_buffer, VIDEO_BLT_BUFFER_SIZE);
    }
}

u8 Video::ReadConfigCallback(void* device, u8 function, u8 offset)
{
    return ((Video*)device)->ReadConfig(function, offset);
}

void Video::WriteConfigCallback(void* device, u8 function, u8 offset, u8 value)
{
    ((Video*)device)->WriteConfig(function, offset, value);
}

u8 Video::ReadConfig(u8 function, u8 offset) const
{
    if (function != 0)
        return 0xFF;

    switch (offset)
    {
        case 0x00:
            return 0x13;
        case 0x01:
            return 0x10;
        case 0x02:
            return 0xB8;
        case 0x04:
            return m_state.pci_command;
        case 0x07:
            return 0x02;
        case 0x0B:
            return 0x03;
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
            return (u8)(m_state.pci_base >> ((offset - 0x10) * 8));
        case 0x3C:
            return m_state.pci_line;
        default:
            return 0x00;
    }
}

void Video::WriteConfig(u8 function, u8 offset, u8 value)
{
    if (function != 0)
        return;

    switch (offset)
    {
        case 0x04:
            m_state.pci_command = value & 0x23;
            UpdateAperture();
            break;
        case 0x13:
            m_state.pci_base = ((u32)value << 24) & k_video_pci_base_mask;
            UpdateAperture();
            break;
        case 0x3C:
            m_state.pci_line = value;
            break;
        default:
            break;
    }
}

void Video::SanitizeState()
{
    m_state.pci_command &= 0x23;
    m_state.pci_base &= k_video_pci_base_mask;
    m_state.sequencer_index &= m_cirrus ? 0xFF : (m_ega ? 0x0F : 0x07);
    m_state.graphics_index &= m_cirrus ? 0x3F : 0x0F;
    m_state.crtc_index &= m_ega ? 0x1F : 0x3F;
    m_state.ega_position[0] &= 0x03;
    m_state.ega_position[1] &= 0x03;
    m_state.cga_mode &= 0x3F;
    m_state.cga_color &= 0x3F;

    if (m_ega)
    {
        for (int i = 0; i < VIDEO_SEQUENCER_COUNT; i++)
            m_state.sequencer[i] &= k_video_ega_sequencer_masks[i];

        for (int i = 0; i < VIDEO_GRAPHICS_COUNT; i++)
            m_state.graphics[i] &= k_video_ega_graphics_masks[i];

        for (int i = 0; i < VIDEO_CRTC_COUNT; i++)
            m_state.crtc[i] &= k_video_ega_crtc_masks[i];

        m_state.attribute[k_video_ar_mode] &= 0x0F;
        m_state.attribute[k_video_ar_overscan] &= 0x3F;
        m_state.attribute[k_video_ar_color_select] = 0;
    }

    m_state.hidden_dac_reads = MIN(m_state.hidden_dac_reads, (u8)4);
    m_state.cursor_x &= 0x7FF;
    m_state.cursor_y &= 0x7FF;
    m_state.blt_count = MIN(m_state.blt_count, (u32)VIDEO_BLT_BUFFER_SIZE);
    m_state.blt_waiting = m_cirrus && m_state.blt_waiting;
    m_blt_handlers = m_state.blt_waiting;
    m_blt_frame_used = false;
    m_wide_counter = m_cirrus && (m_state.crtc[k_video_cr_extended_display] & 0x02) != 0;
    m_extended = m_cirrus && (m_state.sequencer[k_video_sr_extended] & 0x01) != 0;
    UpdateExtendedFormat();
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
    UpdateAperture();
    UpdateVoodooOutput();
}
