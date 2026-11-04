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
#include "../system/scheduler.h"

INLINE void Video::Synchronize(u64 clocks)
{
    while (clocks >= m_next_event_clocks)
        RunNextEvent();
}

INLINE void Video::HandleEvent(u64 clocks)
{
    Synchronize(clocks);
    m_scheduler->Schedule(SCHEDULER_EVENT_VIDEO, m_next_event_clocks);
}

// A 10-bit ISA decode, the caller drops the upper address lines
INLINE bool Video::Decodes(u16 port) const
{
    if (port >= 0x3C0 && port <= 0x3CF)
        return port != 0x3CB && port != 0x3CD;

    return port == 0x3B4 || port == 0x3B5 || port == 0x3BA || port == 0x3D4 || port == 0x3D5 || port == 0x3DA;
}

INLINE bool Video::IsFrameReady() const
{
    return m_frame_ready;
}

INLINE bool Video::IsRunning() const
{
    return m_state.running;
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

// Milliseconds between vertical retraces with the timing latched at the start of the frame
INLINE float Video::GetFrameTime() const
{
    if (!m_state.running)
        return 1000.0f / GDOX_VIDEO_FRAME_RATE;

    double dots = (double)m_state.frame_lines * m_state.frame_line_dots;
    return (float)((dots * 1000.0) / m_state.frame_dot_clock);
}

INLINE void Video::EnableNativeResolution(bool enable)
{
    m_native_resolution = enable;
}

INLINE bool Video::IsNativeResolutionEnabled() const
{
    return m_native_resolution;
}

INLINE u8* Video::GetVRAM()
{
    return m_state.vram;
}

INLINE Video::Video_State* Video::GetState()
{
    return &m_state;
}

// Byte o of plane p lives at (o & FFFCh) | ((o & 3) << 16) | p: the four planes of an offset are one aligned doubleword
// and the chain-4 offset of CPU address A lands on index A, so chain-4 memory is linear for the host
INLINE u32 Video::GetPlaneBase(u32 offset) const
{
    return (offset & 0xFFFC) | ((offset & 0x03) << 16);
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

INLINE u32 Video::GetStartAddress() const
{
    return ((u32)m_state.crtc[k_video_cr_start_high] << 8) | m_state.crtc[k_video_cr_start_low];
}

INLINE u32 Video::GetCursorAddress() const
{
    u32 address = ((u32)m_state.crtc[k_video_cr_cursor_high] << 8) | m_state.crtc[k_video_cr_cursor_low];
    return (address + ((m_state.crtc[k_video_cr_cursor_end] >> 5) & 0x03)) & 0xFFFF;
}

// Plane offset of a CRTC address: byte, word or doubleword rotation, then the CGA and Hercules row substitution
INLINE u32 Video::GetCRTCOffset(u32 address, u32 row) const
{
    u8 mode = m_state.crtc[k_video_cr_mode];
    u32 offset;

    address &= 0xFFFF;

    if ((m_state.crtc[k_video_cr_underline] & 0x40) != 0)
        offset = (address << 2) | ((address >> 14) & 0x03);
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

// Dots of a character clock, doubled when the sequencer halves the dot clock
INLINE u32 Video::GetCharacterClockDots() const
{
    u8 clocking = m_state.sequencer[k_video_sr_clocking];
    return ((clocking & 0x01) != 0 ? 8 : 9) << ((clocking >> 3) & 0x01);
}

INLINE u32 Video::GetLineDots() const
{
    return ((u32)m_state.crtc[k_video_cr_horizontal_total] + 5) * GetCharacterClockDots();
}

INLINE u32 Video::GetTotalLines() const
{
    return GetVerticalTotal() + 2;
}

// Clock selects 2 and 3 are external clocks, which a plain VGA doesn't have
INLINE u32 Video::GetDotClock() const
{
    return (m_state.misc & 0x0C) == 0x04 ? k_video_dot_clock_28 : k_video_dot_clock_25;
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
    if (!m_state.running)
        return 0x01;

    u32 line = m_state.event_line;
    u8 status = 0x01;

    if (line < m_state.display_lines && clocks < m_display_end_clocks)
        status = 0x00;

    if (line >= m_state.retrace_start && line < m_state.retrace_end)
        status |= 0x08;

    return status;
}

INLINE bool Video::IsDirectMapped() const
{
    return m_mapped_size != 0;
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
