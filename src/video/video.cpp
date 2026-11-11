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
#include "../system/machine_profiles.h"

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
    m_odd_even_address = false;
    m_plain_write = false;
    m_write_planes = 0;
    m_set_reset = 0;
    m_set_reset_enable = 0;
    m_bit_mask = 0;
    m_card = GDOX_VIDEO_CARD_VGA;
    m_cirrus = false;
    m_local_bus = false;
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

    for (int i = 0; i < 16; i++)
    {
        u8 planes[4];

        for (int plane = 0; plane < 4; plane++)
            planes[plane] = (i & (1 << plane)) != 0 ? 0xFF : 0x00;

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

// A card change takes effect at the next reset
void Video::SetCard(int card)
{
    const GDOX_Video_Card_Info& info = k_video_cards[(card >= 0 && card < GDOX_VIDEO_CARD_COUNT) ? card : 0];

    m_card = (card >= 0 && card < GDOX_VIDEO_CARD_COUNT) ? card : GDOX_VIDEO_CARD_VGA;
    m_cirrus = info.cirrus;
    m_local_bus = info.local_bus;
    m_vram_size = MIN(info.memory_size, (u32)VIDEO_VRAM_SIZE);
    m_vram_mask = m_vram_size - 1;
    m_plane_mask = (m_vram_size >> 2) - 1;
}

void Video::Reset()
{
    memset(&m_state, 0, sizeof(m_state));
    m_state.vga_enable = 0x01;
    m_state.pel_mask = 0xFF;

    if (m_cirrus)
    {
        for (int i = VIDEO_SEQUENCER_COUNT; i < VIDEO_SEQUENCER_EXT_COUNT; i++)
            m_state.sequencer[i] = k_video_cirrus_sequencer_reset[i];

        m_state.sequencer[k_video_sr_config] = (m_local_bus ? 0x05 : 0x07) << 3;
        m_state.crtc[k_video_cr_id] = k_video_cirrus_id;
    }

    m_extended = false;
    m_wide_counter = false;
    m_dual_page = false;
    m_bank_low = 0;
    m_bank_high = 0;
    UpdateExtendedFormat();
    m_frame_ready = false;
    m_frame_width = GDOX_FRAME_BUFFER_WIDTH;
    m_frame_height = GDOX_FRAME_BUFFER_HEIGHT;
    m_display_width = GDOX_FRAME_BUFFER_WIDTH;
    m_display_height = GDOX_FRAME_BUFFER_HEIGHT;
    UpdateColorCaches();
    UpdateMemoryAccess();
    UpdateAperture();
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
            m_state.sequencer_index = m_cirrus ? value : value & 0x07;
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
        case 0x3CE:
            m_state.graphics_index = value & (m_cirrus ? 0x3F : 0x0F);
            break;
        case 0x3CF:
            WriteGraphics(value, clocks);
            break;
        case 0x3B4:
        case 0x3D4:
            if (IsCRTCBlockSelected(port))
                m_state.crtc_index = value & 0x3F;
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

    if (index < k_video_ar_mode && (m_state.attribute_index & 0x20) != 0)
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
    u8 index = m_state.sequencer_index & (m_cirrus ? 0x1F : 0x07);

    if (index >= VIDEO_SEQUENCER_COUNT)
    {
        if (m_cirrus)
            WriteCirrusSequencer(index, value, clocks);

        return;
    }

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
    {
        if (m_cirrus)
            WriteCirrusGraphics(index, value, clocks);

        return;
    }

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
    {
        if (m_cirrus)
            WriteCirrusCRTC(index, value, clocks);

        return;
    }

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
    u8 value = m_state.dac[m_state.dac_address][m_state.dac_component];

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
}

// The BitBLT engine isn't emulated: a blit ends as soon as it starts, never busy (R14-D10)
void Video::WriteCirrusGraphics(u8 index, u8 value, u64 clocks)
{
    if (!IsUnlocked())
        return;

    if (index == k_video_gr_blt_status)
        value &= ~0x0BU;

    if (value == m_state.graphics[index])
        return;

    RenderLineToBeam(clocks);
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
            m_state.row_address += GetOffsetRegister() << 1;
        }
        else
            m_state.row_scan = (m_state.row_scan + 1) & 0x1F;
    }

    if (line == GetLineCompare())
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
        m_state.character_map = m_state.sequencer[k_video_sr_character_map];
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
    m_state.start_address = (GetStartAddress() + ((m_state.crtc[k_video_cr_preset_row] >> 5) & 0x03)) & GetCounterMask();

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
            u8 index = m_state.dac_address - (component == 2 ? 1 : 0);
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
}

void Video::SanitizeState()
{
    m_state.sequencer_index &= m_cirrus ? 0xFF : 0x07;
    m_state.graphics_index &= m_cirrus ? 0x3F : 0x0F;
    m_state.crtc_index &= 0x3F;
    m_state.hidden_dac_reads = MIN(m_state.hidden_dac_reads, (u8)4);
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
}
