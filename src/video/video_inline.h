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

#ifndef VIDEO_INLINE_H
#define VIDEO_INLINE_H

#include "video.h"
#include "mc6845.h"
#include "voodoo.h"
#include "../system/scheduler.h"

INLINE void Video::Synchronize(u64 clocks)
{
    while (clocks >= m_next_event_clocks)
        RunNextEvent();
}

INLINE void Video::HandleEvent(u64 clocks)
{
    Synchronize(clocks);

    if (m_voodoo_enabled)
        m_voodoo->HandleEvent(clocks);

    ScheduleEvent();
}

INLINE void Video::SynchronizeVRAM()
{
    if (unlikely(*m_start_cycles >= m_vram_deadline))
        SynchronizeVRAMWrite();
}

// A 10-bit ISA decode, the caller drops the upper address lines
INLINE bool Video::Decodes(u16 port) const
{
    if (m_cga)
        return port >= 0x3D0 && port <= 0x3DC;

    if (port >= 0x3C0 && port <= 0x3CF)
    {
        if (m_ega)
            return port <= 0x3C2 || port == 0x3C4 || port == 0x3C5 || port == 0x3CA || port == 0x3CC || port == 0x3CE ||
                port == 0x3CF;

        return port != 0x3CB && port != 0x3CD;
    }

    return port == 0x3B4 || port == 0x3B5 || port == 0x3BA || port == 0x3D4 || port == 0x3D5 || port == 0x3DA;
}

INLINE bool Video::IsFrameReady() const
{
    return m_frame_ready;
}

INLINE bool Video::IsRunning() const
{
    return m_state.running || m_voodoo_shown;
}

INLINE bool Video::HasVoodoo() const
{
    return m_voodoo_enabled;
}

INLINE bool Video::IsVoodooShown() const
{
    return m_voodoo_shown;
}

INLINE Voodoo* Video::GetVoodoo()
{
    return m_voodoo;
}

INLINE int Video::GetFrameWidth() const
{
    return m_frame_width;
}

INLINE int Video::GetFrameHeight() const
{
    return m_frame_height;
}

INLINE int Video::GetDisplayWidth() const
{
    return m_display_width;
}

INLINE int Video::GetDisplayHeight() const
{
    return m_display_height;
}

INLINE u32 Video::GetCanvasHDivider() const
{
    return m_canvas_h_divider;
}

INLINE u32 Video::GetCGABorderColor() const
{
    return m_cga_colors[m_state.cga_color & 0x0F];
}

// Milliseconds between vertical retraces with the timing latched at the start of the frame
INLINE float Video::GetFrameTime() const
{
    if (m_voodoo_shown)
        return m_voodoo->GetFrameTime();

    if (!m_state.running)
        return 1000.0f / GDOX_VIDEO_FRAME_RATE;

    double dots = (double)m_state.frame_lines * m_state.frame_line_dots;
    return (float)((dots * 1000.0) / m_state.frame_dot_clock);
}

INLINE void Video::EnableNativeResolution(bool enable)
{
    m_native_resolution = enable;
}

INLINE u8* Video::GetVRAM()
{
    return m_state.vram;
}

INLINE Video::Video_State* Video::GetState()
{
    return &m_state;
}

// Byte o of plane p lives at (o & FFFCh) | ((o & 3) << 16) | p in each 256 KB: the four planes of an offset are one
// aligned doubleword and the chain-4 offset of CPU address A lands on index A, so chain-4 memory is linear for the host
INLINE u32 Video::GetPlaneBase(u32 offset) const
{
    offset &= m_plane_mask;
    return ((offset & ~0xFFFFU) << 2) | (offset & 0xFFFC) | ((offset & 0x03) << 16);
}

// GR06 bit 1 puts address bit 16 in the place of bit 0, which is what the CRTC's word mode rotation undoes
INLINE u32 Video::GetHostOffset(u32 address) const
{
    return m_odd_even_address ? (address & ~0x10001U) | ((address >> 16) & 0x01) : address;
}

// The Cirrus banks: one offset for the whole window, or one per 32 KB half
INLINE u32 Video::GetWindowAddress(u32 address) const
{
    return address + ((m_dual_page && (address & 0x8000) != 0) ? m_bank_high : m_bank_low);
}

INLINE u8 Video::PeekPlane(int plane, u32 offset) const
{
    return m_state.vram[GetPlaneBase(offset) + (plane & 0x03)];
}

INLINE void Video::PokePlane(int plane, u32 offset, u8 value)
{
    m_state.vram[GetPlaneBase(offset) + (plane & 0x03)] = value;
}

INLINE bool Video::IsTextMode() const
{
    return (m_state.graphics[k_video_gr_misc] & 0x01) == 0 && (m_state.attribute[k_video_ar_mode] & 0x01) == 0;
}

INLINE bool Video::IsCRTCBlockSelected(u16 port) const
{
    return (port & 0x3F0) == ((m_state.misc & 0x01) != 0 ? 0x3D0 : 0x3B0);
}

INLINE Video::Video_Format Video::GetShiftFormat() const
{
    u8 mode = m_state.graphics[k_video_gr_mode];

    if ((mode & 0x40) != 0)
        return VIDEO_FORMAT_256;

    return (mode & 0x20) != 0 ? VIDEO_FORMAT_CGA : VIDEO_FORMAT_PLANAR;
}

INLINE bool Video::IsPacked() const
{
    return (m_state.attribute[k_video_ar_mode] & 0x40) != 0;
}

// The attribute index with bit 5 clear hands the palette to the CPU and the screen shows the overscan color
INLINE bool Video::IsScreenBlank() const
{
    if (m_cga)
        return (m_state.cga_mode & 0x08) == 0;

    return (m_state.sequencer[k_video_sr_clocking] & 0x20) != 0 ||
        (m_state.sequencer[k_video_sr_reset] & 0x03) != 0x03 ||
        (m_state.crtc[k_video_cr_mode] & 0x80) == 0 ||
        (m_state.attribute_index & 0x20) == 0;
}

INLINE u32 Video::GetDACColor(int index) const
{
    return m_dac_rgb[index & 0xFF];
}

INLINE u32 Video::GetAttributeColor(int index) const
{
    return m_attribute_colors[index & 0x0F];
}

INLINE u32 Video::GetOverscanColor() const
{
    return m_dac_colors[m_state.attribute[k_video_ar_overscan]];
}

INLINE u32 Video::GetVerticalTotal() const
{
    u8 overflow = m_state.crtc[k_video_cr_overflow];
    return m_state.crtc[k_video_cr_vertical_total] | ((overflow & 0x01) << 8) | ((overflow & 0x20) << 4);
}

INLINE u32 Video::GetVerticalScale() const
{
    return !m_ega && (m_state.crtc[k_video_cr_mode] & 0x04) != 0 ? 2 : 1;
}

INLINE u32 Video::GetVerticalDisplayEnd() const
{
    u8 overflow = m_state.crtc[k_video_cr_overflow];
    return m_state.crtc[k_video_cr_display_end] | ((overflow & 0x02) << 7) | ((overflow & 0x40) << 3);
}

INLINE u32 Video::GetVerticalRetraceStart() const
{
    u8 overflow = m_state.crtc[k_video_cr_overflow];
    return m_state.crtc[k_video_cr_retrace_start] | ((overflow & 0x04) << 6) | ((overflow & 0x80) << 2);
}

INLINE u32 Video::GetLineCompare() const
{
    return m_state.crtc[k_video_cr_line_compare] | ((m_state.crtc[k_video_cr_overflow] & 0x10) << 4) |
        ((m_state.crtc[k_video_cr_max_scan] & 0x40) << 3);
}

// CR1B adds bit 16 in bit 0 and bits 17-18 in bits 2-3
INLINE u32 Video::GetStartAddress() const
{
    u32 address = ((u32)m_state.crtc[k_video_cr_start_high] << 8) | m_state.crtc[k_video_cr_start_low];

    if (!m_cirrus)
        return address;

    u8 extended = m_state.crtc[k_video_cr_extended_display];
    return address | ((u32)(extended & 0x01) << 16) | ((u32)(extended & 0x0C) << 15);
}

// CR1B bit 4 is the offset's ninth bit
INLINE u32 Video::GetOffsetRegister() const
{
    u32 offset = m_state.crtc[k_video_cr_offset];
    return m_cirrus ? offset | ((u32)(m_state.crtc[k_video_cr_extended_display] & 0x10) << 4) : offset;
}

// The address counter wraps at 16 bits, or at 19 with CR1B bit 1
INLINE u32 Video::GetCounterMask() const
{
    return m_wide_counter ? 0x7FFFF : 0xFFFF;
}

INLINE u32 Video::GetCursorAddress() const
{
    u32 address = ((u32)m_state.crtc[k_video_cr_cursor_high] << 8) | m_state.crtc[k_video_cr_cursor_low];
    return (address + ((m_state.crtc[k_video_cr_cursor_end] >> 5) & 0x03)) & 0xFFFF;
}

// The address counter moves on every one, two or four character clocks
INLINE u32 Video::GetCountShift() const
{
    if ((m_state.crtc[k_video_cr_mode] & 0x08) != 0)
        return 1;

    return (m_state.crtc[k_video_cr_underline] & 0x20) != 0 ? 2 : 0;
}

// Plane offset of a CRTC address: byte, word or doubleword rotation, then the CGA and Hercules row substitution
INLINE u32 Video::GetCRTCOffset(u32 address, u32 row) const
{
    u8 mode = m_state.crtc[k_video_cr_mode];
    u32 offset;

    // With the wide counter doubleword mode takes DRAM address bits 1-0 from counter bits 14-15 (5446 p267)
    if (m_wide_counter)
    {
        address &= 0x7FFFF;

        if ((m_state.crtc[k_video_cr_underline] & 0x40) != 0)
            offset = ((address & 0x3FFF) << 2) | ((address >> 14) & 0x03) | (address & ~0xFFFFU);
        else if ((mode & 0x40) != 0)
            offset = address;
        else
            offset = (address << 1) | ((address >> ((mode & 0x20) != 0 ? 15 : 13)) & 0x01);

        if ((mode & 0x01) == 0)
            offset = (offset & ~0x2000U) | ((row & 0x01) << 13);

        if ((mode & 0x02) == 0)
            offset = (offset & ~0x4000U) | ((row & 0x02) << 13);

        return offset & m_plane_mask;
    }

    address &= 0xFFFF;

    if ((m_state.crtc[k_video_cr_underline] & 0x40) != 0)
        offset = address << 2;
    else if ((mode & 0x40) != 0)
        offset = address;
    else
        offset = (address << 1) | ((address >> ((mode & 0x20) != 0 ? 15 : 13)) & 0x01);

    if ((mode & 0x01) == 0)
        offset = (offset & ~0x2000U) | ((row & 0x01) << 13);

    if ((mode & 0x02) == 0)
        offset = (offset & ~0x4000U) | ((row & 0x02) << 13);

    return offset & 0xFFFF;
}

// Dots of a character clock, doubled when the sequencer halves the dot clock. Cirrus clock/2 and clock/3 run the clock
// at 2 and 3 times the pixel rate with the timing still in units of 8 pixels (CIRRUS.TXT l1497-1507); the 5446 runs
// every path at the pixel rate (5446 p227)
INLINE u32 Video::GetCharacterClockDots() const
{
    u8 clocking = m_state.sequencer[k_video_sr_clocking];
    return (((clocking & 0x01) != 0 ? 8 : 9) * GetExtendedClockMultiplier()) << ((clocking >> 3) & 0x01);
}

INLINE u32 Video::GetExtendedClockMultiplier() const
{
    if (!m_extended || m_pci)
        return 1;

    u32 path = (m_state.sequencer[k_video_sr_extended] >> 1) & 0x07;
    return path == 1 ? 2 : (path == 2 ? 3 : 1);
}

INLINE u32 Video::GetLineDots() const
{
    if (m_cga)
        return m_mc6845->GetHorizontalTotal() * GetCGACharacterDots();

    return ((u32)m_state.crtc[k_video_cr_horizontal_total] + (m_ega ? 2 : 5)) * GetCharacterClockDots();
}

INLINE u32 Video::GetTotalLines() const
{
    if (m_cga)
        return m_mc6845->GetTotalLines();

    return GetVerticalTotal() + 2;
}

INLINE u32 Video::GetCGACharacterDots() const
{
    return (m_state.cga_mode & 0x01) != 0 ? 8 : 16;
}

// Clock selects 2 and 3 are external clocks, which a plain VGA doesn't have; the Cirrus has four synthesizers
INLINE u32 Video::GetDotClock() const
{
    if (m_cga)
        return k_video_dot_clock_14;

    if (m_ega)
        return (m_state.misc & 0x0C) == 0x04 ? k_video_dot_clock_16 : k_video_dot_clock_14;

    if (m_cirrus)
        return GetVideoClock((m_state.misc >> 2) & 0x03);

    return (m_state.misc & 0x0C) == 0x04 ? k_video_dot_clock_28 : k_video_dot_clock_25;
}

// 14.31818 MHz x N / D, halved by the post-scalar: N in SR0B-SR0E bits 6-0, D in SR1B-SR1E bits 5-1 (CIRRUS.TXT l1750-1778)
INLINE u32 Video::GetVideoClock(int index) const
{
    u32 numerator = m_state.sequencer[k_video_sr_vclk_numerator + (index & 0x03)] & 0x7F;
    u8 denominator = m_state.sequencer[k_video_sr_vclk_denominator + (index & 0x03)];
    u32 divider = ((denominator >> 1) & 0x1F) << (denominator & 0x01);

    if (numerator == 0 || divider == 0)
        return k_video_dot_clock_25;

    return (u32)(((u64)k_video_reference_clock * numerator) / divider);
}

INLINE u32 Video::GetCurrentDotClock() const
{
    return GetDotClock();
}

// Lines count from the first line of the display, rounded up to the machine clock that reaches them
INLINE u64 Video::GetLineClocks(u32 line) const
{
    u64 dots = (u64)line * m_state.frame_line_dots;
    u64 rate = m_state.frame_dot_clock;
    return m_state.frame_start_clocks + (dots * GDOX_MACHINE_CLOCK_RATE + rate - 1) / rate;
}

INLINE u32 Video::GetBeamLine(u64 clocks) const
{
    if (!m_state.running || clocks < m_state.frame_start_clocks)
        return 0;

    u64 dots = ((clocks - m_state.frame_start_clocks) * m_state.frame_dot_clock) / GDOX_MACHINE_CLOCK_RATE;
    return (u32)(dots / m_state.frame_line_dots);
}

INLINE u32 Video::GetBeamDot(u64 clocks) const
{
    if (!m_state.running || clocks < m_state.frame_start_clocks)
        return 0;

    u64 dots = ((clocks - m_state.frame_start_clocks) * m_state.frame_dot_clock) / GDOX_MACHINE_CLOCK_RATE;
    return (u32)(dots % m_state.frame_line_dots);
}

// Bit 0 is display enable off, bit 3 vertical retrace. Machine time is at or past the last event when it is read,
// so the line comes from it, and every edge but the end of the display on a line has an event of its own
INLINE u8 Video::GetSyncStatus(u64 clocks) const
{
    u8 pen = (m_ega || m_cga) ? 0x04 : 0x00;

    if (!m_state.running)
        return 0x01 | pen;

    u32 line = m_state.event_line;
    u8 status = 0x01 | pen;

    if (line < m_state.display_lines && clocks < m_display_end_clocks)
        status = 0x00;

    if (line >= m_state.retrace_start && line < m_state.retrace_end)
        status |= 0x08;

    return status;
}

INLINE u32 Video::GetLinearOffset(u32 offset) const
{
    static const u32 k_swaps[4] = { 0, 1, 3, 0 };
    return ((offset & 0x3FFFFF) ^ k_swaps[(offset >> 22) & 0x03]) & m_vram_mask;
}

INLINE bool Video::IsLinearSpecial() const
{
    return m_mmio_top || m_state.blt_waiting || m_address_shift != 0;
}

INLINE bool Video::IsBltWaiting() const
{
    return m_state.blt_waiting;
}

INLINE u32 Video::GetBltCount() const
{
    return m_blt_count;
}

INLINE u32 Video::GetBltFrameCount(int kind) const
{
    return m_blt_frame_kinds[kind];
}

INLINE u32 Video::GetBltFrameRemaps() const
{
    return m_blt_frame_remaps;
}

INLINE bool Video::IsBltMapped() const
{
    return m_blt_handlers;
}

INLINE bool Video::IsDirectMapped() const
{
    return m_mapped_size != 0;
}

INLINE int Video::GetCard() const
{
    return m_card;
}

INLINE int Video::GetFamily() const
{
    return m_family;
}

INLINE bool Video::IsCirrus() const
{
    return m_cirrus;
}

INLINE bool Video::IsEGA() const
{
    return m_ega;
}

INLINE bool Video::IsCGA() const
{
    return m_cga;
}

INLINE MC6845* Video::GetMC6845()
{
    return m_mc6845;
}

INLINE bool Video::IsEGA350Lines() const
{
    return (m_state.misc & 0x80) != 0;
}

INLINE u8 Video::GetEGASwitches() const
{
    return k_video_ega_switches;
}

INLINE u32 Video::GetCGAColor(int index) const
{
    return m_cga_colors[index & 0x0F];
}

INLINE bool Video::IsCGAGraphics() const
{
    return (m_state.cga_mode & 0x02) != 0;
}

INLINE bool Video::IsCGAHighResolution() const
{
    return (m_state.cga_mode & 0x10) != 0;
}

INLINE int Video::GetCGAOutput() const
{
    return m_cga_output;
}

INLINE bool Video::IsCGAComposite() const
{
    return m_cga && m_cga_composite;
}

// SR06 holds 12h with the extensions unlocked
INLINE bool Video::IsUnlocked() const
{
    return m_cirrus && m_state.sequencer[k_video_sr_unlock] == k_video_cirrus_unlock;
}

INLINE u32 Video::GetVRAMSize() const
{
    return m_vram_size;
}

INLINE bool Video::IsExtendedMode() const
{
    return m_extended;
}

INLINE Video::Video_Extended_Format Video::GetExtendedFormat() const
{
    return m_extended_format;
}

INLINE u32 Video::GetExtendedBytesPerPixel() const
{
    return m_extended_bytes;
}

INLINE u32 Video::GetBankOffset(int bank) const
{
    return bank == 0 ? m_bank_low : (m_dual_page ? m_bank_high + 0x8000 : m_bank_low);
}

INLINE bool Video::IsDualPage() const
{
    return m_dual_page;
}

INLINE u32 Video::GetApertureBase() const
{
    return m_aperture_base;
}

INLINE bool Video::GetBlinkPhase() const
{
    return ((m_state.frame_count >> k_video_text_blink_shift) & 0x01) != 0;
}

INLINE bool Video::GetCursorPhase() const
{
    return ((m_state.frame_count >> k_video_cursor_blink_shift) & 0x01) == 0;
}

INLINE u32 Video::ToXRGB(u8 red, u8 green, u8 blue) const
{
    return 0xFF000000U | ((u32)red << 16) | ((u32)green << 8) | blue;
}

#endif /* VIDEO_INLINE_H */
