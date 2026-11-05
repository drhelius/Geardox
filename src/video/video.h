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

#ifndef VIDEO_H
#define VIDEO_H

#include <iostream>
#include "../common/common.h"

#define VIDEO_VRAM_SIZE 0x40000
#define VIDEO_PLANE_SIZE 0x10000
#define VIDEO_PLANE_COUNT 4
#define VIDEO_SEQUENCER_COUNT 5
#define VIDEO_GRAPHICS_COUNT 9
#define VIDEO_CRTC_COUNT 25
#define VIDEO_ATTRIBUTE_COUNT 21
#define VIDEO_DAC_COUNT 256

class Memory;
class PIC;
class TraceLogger;
class PIT;
class Scheduler;
class StateSerializer;

class Video
{
public:
    enum Video_Format
    {
        VIDEO_FORMAT_PLANAR = 0,
        VIDEO_FORMAT_CGA,
        VIDEO_FORMAT_256
    };

    struct Video_State
    {
        u8 vram[VIDEO_VRAM_SIZE];
        u8 sequencer[VIDEO_SEQUENCER_COUNT];
        u8 sequencer_index;
        u8 graphics[VIDEO_GRAPHICS_COUNT];
        u8 graphics_index;
        u8 crtc[VIDEO_CRTC_COUNT];
        u8 crtc_index;
        u8 attribute[VIDEO_ATTRIBUTE_COUNT];
        u8 attribute_index;
        bool attribute_data;
        u8 misc;
        u8 feature;
        u8 vga_enable;
        u8 dac[VIDEO_DAC_COUNT][3];
        u8 dac_cache[3];
        u8 dac_address;
        u8 dac_index;
        u8 dac_component;
        u8 dac_state;
        u8 pel_mask;
        u32 latch;
        bool vertical_interrupt;
        bool running;
        u64 frame_start_clocks;
        u32 frame_line_dots;
        u32 frame_display_dots;
        u32 frame_lines;
        u32 frame_dot_clock;
        u32 frame_count;
        u32 display_lines;
        u32 retrace_start;
        u32 retrace_end;
        u32 ready_line;
        u32 event_line;
        u32 start_address;
        u32 row_address;
        u32 row_scan;
        bool row_repeat;
        bool pan_reset;
        u8 character_map;
    };

public:
    Video();
    ~Video();
    void Init(PIC* pic, PIT* pit, Scheduler* scheduler, Memory* memory);
    void SetTraceLogger(TraceLogger* trace_logger);
    void Reset();
    bool Decodes(u16 port) const;
    u8 Read(u16 port, u64 clocks);
    u8 Peek(u16 port, u64 clocks) const;
    void Write(u16 port, u8 value, u64 clocks);
    u64 GetSyncStableUntil(u64 clocks) const;
    void Synchronize(u64 clocks);
    void HandleEvent(u64 clocks);
    void BeginFrame(u8* frame_buffer, bool render);
    void EndFrame();
    bool IsFrameReady() const;
    bool IsRunning() const;
    int GetFrameWidth() const;
    int GetFrameHeight() const;
    int GetDisplayWidth() const;
    int GetDisplayHeight() const;
    void SetFrameSize(int frame_width, int frame_height, int display_width, int display_height);
    float GetFrameTime() const;
    void EnableNativeResolution(bool enable);
    bool IsNativeResolutionEnabled() const;
    u8* GetVRAM();
    u8 PeekPlane(int plane, u32 offset) const;
    void PokePlane(int plane, u32 offset, u8 value);
    bool IsTextMode() const;
    bool IsCRTCBlockSelected(u16 port) const;
    Video_Format GetShiftFormat() const;
    bool IsPacked() const;
    bool IsScreenBlank() const;
    u32 GetDACColor(int index) const;
    u32 GetAttributeColor(int index) const;
    u32 GetOverscanColor() const;
    u32 GetVerticalTotal() const;
    u32 GetVerticalDisplayEnd() const;
    u32 GetVerticalRetraceStart() const;
    u32 GetLineCompare() const;
    u32 GetStartAddress() const;
    u32 GetCursorAddress() const;
    u32 GetCRTCOffset(u32 address, u32 row) const;
    u32 GetCharacterClockDots() const;
    u32 GetBeamLine(u64 clocks) const;
    u32 GetBeamDot(u64 clocks) const;
    u8 GetSyncStatus(u64 clocks) const;
    bool IsDirectMapped() const;
    void UpdateVRAMMapping();
    Video_State* GetState();
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream, int version = GDOX_SAVESTATE_VERSION);

    static u8 ReadWindowCallback(void* device, u32 offset);
    static u8 PeekWindowCallback(void* device, u32 offset);
    static void WriteWindowCallback(void* device, u32 offset, u8 value);
    static u16 ReadWindow16Callback(void* device, u32 offset);
    static u32 ReadWindow32Callback(void* device, u32 offset);
    static void WriteWindow16Callback(void* device, u32 offset, u16 value);
    static void WriteWindow32Callback(void* device, u32 offset, u32 value);
    static u8 PeekPlane0Callback(void* device, u32 offset);
    static u8 PeekPlane1Callback(void* device, u32 offset);
    static u8 PeekPlane2Callback(void* device, u32 offset);
    static u8 PeekPlane3Callback(void* device, u32 offset);
    static void PokePlane0Callback(void* device, u32 offset, u8 value);
    static void PokePlane1Callback(void* device, u32 offset, u8 value);
    static void PokePlane2Callback(void* device, u32 offset, u8 value);
    static void PokePlane3Callback(void* device, u32 offset, u8 value);

private:
    void WriteAttribute(u8 value, u64 clocks);
    void WriteSequencer(u8 value, u64 clocks);
    void WriteGraphics(u8 value, u64 clocks);
    void WriteCRTC(u8 value, u64 clocks);
    void WriteMisc(u8 value);
    void WriteDAC(u8 value, u64 clocks);
    u8 ReadDAC();
    u8 PeekCRTC() const;
    void RunNextEvent();
    void LatchFrameTiming();
    void StartFrame(u64 clocks);
    void CompleteFrame();
    void FinishLine();
    void UpdateRunning(u64 clocks);
    void UpdateNextEvent();
    void UpdateGeometry();
    void UpdateIRQ();
    u64 GetLineClocks(u32 line) const;
    u32 GetLineDots() const;
    u32 GetTotalLines() const;
    u32 GetDotClock() const;
    u8 ReadVRAM(u32 offset);
    u8 PeekVRAM(u32 offset) const;
    void WriteVRAM(u32 offset, u8 value);
    u32 GetPlaneBase(u32 offset) const;
    u32 GetHostOffset(u32 address) const;
    u32 GetCountShift() const;
    u32 GetWriteData(u8 value) const;
    u8 CompareLatches() const;
    void UpdateMemoryAccess();
    u32 GetDirectWriteSize() const;
    void RenderLineToBeam(u64 clocks);
    void RenderRow(u32 line);
    void RenderRowPart(int row, int x, int end);
    void RenderPixels(u32* destination, int x, int end);
    void RenderText(u32* destination, int x, int end);
    template<Video_Format format, bool packed>
    void RenderGraphics(u32* destination, int x, int end);
    int GetPixelPan(bool text, bool packed) const;
    void UpdateDACColor(int index);
    void UpdateAttributeColors();
    void UpdateColorCaches();
    bool GetBlinkPhase() const;
    bool GetCursorPhase() const;
    u32 ToXRGB(u8 red, u8 green, u8 blue) const;
    void TraceWrite(u16 port, u8 value, u64 clocks);
    void TraceEvent(u8 event, u8 reg, u16 value, u8 raw, u8 bank, u32 param, u64 clocks);
    void Serialize(StateSerializer& serializer, int version);
    void SanitizeState();

private:
    Video_State m_state;
    PIC* m_pic;
    TraceLogger* m_trace_logger;
    PIT* m_pit;
    Scheduler* m_scheduler;
    Memory* m_memory;
    u8* m_frame_buffer;
    bool m_render;
    bool m_frame_ready;
    u64 m_next_event_clocks;
    u32 m_next_event_line;
    u64 m_line_start_clocks;
    u64 m_display_end_clocks;
    u32 m_display_clocks;
    int m_frame_width;
    int m_frame_height;
    int m_display_width;
    int m_display_height;
    int m_render_width;
    int m_render_height;
    int m_partial_row;
    int m_partial_x;
    bool m_native_resolution;
    u32 m_canvas_h_divider;
    u32 m_canvas_v_divider;
    u32 m_canvas_zoom;
    u32 m_window_start;
    u32 m_window_size;
    u32 m_mapped_size;
    bool m_chain4;
    bool m_odd_even_read;
    bool m_odd_even_write;
    bool m_odd_even_address;
    bool m_plain_write;
    u32 m_write_planes;
    u32 m_set_reset;
    u32 m_set_reset_enable;
    u32 m_bit_mask;
    u32 m_plane_masks[16];
    u64 m_plane_bits[256];
    u32 m_pair_bits[256];
    u32 m_dac_rgb[VIDEO_DAC_COUNT];
    u32 m_dac_colors[VIDEO_DAC_COUNT];
    u32 m_attribute_colors[16];
    u32 m_line_pixels[GDOX_MAX_FRAME_BUFFER_WIDTH + 16];
};

static const u32 k_video_dot_clock_25 = 25175000;
static const u32 k_video_dot_clock_28 = 28322000;
static const u32 k_video_min_line_dots = 64;
static const u32 k_video_min_lines = 16;
static const u32 k_video_max_window_size = 0x20000;
static const int k_video_irq = 2;
static const u32 k_video_cursor_blink_shift = 3;
static const u32 k_video_text_blink_shift = 4;

static const u8 k_video_sequencer_masks[VIDEO_SEQUENCER_COUNT] = { 0x03, 0x3D, 0x0F, 0x3F, 0x0E };
static const u8 k_video_graphics_masks[VIDEO_GRAPHICS_COUNT] = { 0x0F, 0x0F, 0x0F, 0x1F, 0x03, 0x7B, 0x0F, 0x0F, 0xFF };

static const int k_video_sr_reset = 0x00;
static const int k_video_sr_clocking = 0x01;
static const int k_video_sr_map_mask = 0x02;
static const int k_video_sr_character_map = 0x03;
static const int k_video_sr_memory_mode = 0x04;

static const int k_video_gr_set_reset = 0x00;
static const int k_video_gr_enable_set_reset = 0x01;
static const int k_video_gr_color_compare = 0x02;
static const int k_video_gr_rotate = 0x03;
static const int k_video_gr_read_map = 0x04;
static const int k_video_gr_mode = 0x05;
static const int k_video_gr_misc = 0x06;
static const int k_video_gr_color_dont_care = 0x07;
static const int k_video_gr_bit_mask = 0x08;

static const int k_video_cr_horizontal_total = 0x00;
static const int k_video_cr_horizontal_display = 0x01;
static const int k_video_cr_vertical_total = 0x06;
static const int k_video_cr_overflow = 0x07;
static const int k_video_cr_preset_row = 0x08;
static const int k_video_cr_max_scan = 0x09;
static const int k_video_cr_cursor_start = 0x0A;
static const int k_video_cr_cursor_end = 0x0B;
static const int k_video_cr_start_high = 0x0C;
static const int k_video_cr_start_low = 0x0D;
static const int k_video_cr_cursor_high = 0x0E;
static const int k_video_cr_cursor_low = 0x0F;
static const int k_video_cr_retrace_start = 0x10;
static const int k_video_cr_retrace_end = 0x11;
static const int k_video_cr_display_end = 0x12;
static const int k_video_cr_offset = 0x13;
static const int k_video_cr_underline = 0x14;
static const int k_video_cr_mode = 0x17;
static const int k_video_cr_line_compare = 0x18;
static const int k_video_cr_latch_readback = 0x22;
static const int k_video_cr_toggle_readback = 0x24;
static const int k_video_cr_index_readback = 0x26;

static const int k_video_ar_mode = 0x10;
static const int k_video_ar_overscan = 0x11;
static const int k_video_ar_plane_enable = 0x12;
static const int k_video_ar_panning = 0x13;
static const int k_video_ar_color_select = 0x14;

#include "video_inline.h"

#endif /* VIDEO_H */
