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
#include "../common/trace_logger.h"

#define VIDEO_VRAM_SIZE 0x200000
#define VIDEO_VGA_VRAM_SIZE 0x40000
#define VIDEO_PLANE_SIZE 0x10000
#define VIDEO_PLANE_COUNT 4
#define VIDEO_SEQUENCER_COUNT 5
#define VIDEO_GRAPHICS_COUNT 9
#define VIDEO_CRTC_COUNT 25
#define VIDEO_ATTRIBUTE_COUNT 21
#define VIDEO_DAC_COUNT 256
#define VIDEO_SEQUENCER_EXT_COUNT 32
#define VIDEO_GRAPHICS_EXT_COUNT 64
#define VIDEO_CRTC_EXT_COUNT 64
#define VIDEO_DAC_EXT_COUNT 16
#define VIDEO_CGA_VRAM_SIZE 0x4000
#define VIDEO_EGA_COLOR_COUNT 64
#define VIDEO_COMPOSITE_TAPS 5
#define VIDEO_BLT_BUFFER_SIZE 0x2010

class MC6845;
class Voodoo;
class Memory;
class PIC;
class TraceLogger;
struct GDOX_Trace_Entry;
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

    enum Video_Blt_Kind
    {
        VIDEO_BLT_SCREEN = 0,
        VIDEO_BLT_PATTERN,
        VIDEO_BLT_EXPAND,
        VIDEO_BLT_SYSTEM,
        VIDEO_BLT_KIND_COUNT
    };

    enum Video_Extended_Format
    {
        VIDEO_EXTENDED_PALETTE = 0,
        VIDEO_EXTENDED_GREY,
        VIDEO_EXTENDED_332,
        VIDEO_EXTENDED_555,
        VIDEO_EXTENDED_555_MIX,
        VIDEO_EXTENDED_565,
        VIDEO_EXTENDED_888,
        VIDEO_EXTENDED_BLANK
    };

    struct Video_State
    {
        u8 vram[VIDEO_VRAM_SIZE];
        u8 sequencer[VIDEO_SEQUENCER_EXT_COUNT];
        u8 sequencer_index;
        u8 graphics[VIDEO_GRAPHICS_EXT_COUNT];
        u8 graphics_index;
        u8 crtc[VIDEO_CRTC_EXT_COUNT];
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
        u8 hidden_dac;
        u8 hidden_dac_reads;
        u8 dac_extended[VIDEO_DAC_EXT_COUNT][3];
        u8 ega_position[2];
        u8 cga_mode;
        u8 cga_color;
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
        u8 pci_command;
        u32 pci_base;
        u8 pci_line;
        u32 latch_high;
        u16 cursor_x;
        u16 cursor_y;
        bool blt_waiting;
        bool blt_suspended;
        u32 blt_dst;
        u32 blt_src;
        u32 blt_line;
        u32 blt_count;
        u32 blt_fed;
        u8 blt_buffer[VIDEO_BLT_BUFFER_SIZE];
    };

public:
    Video();
    ~Video();
    void Init(PIC* pic, Scheduler* scheduler, Memory* memory);
    void SetTraceLogger(TraceLogger* trace_logger);
    void SetCard(int card);
    void SetVoodoo(bool enabled);
    void SetVoodooThreaded(bool threaded);
    bool HasVoodoo() const;
    bool IsVoodooShown() const;
    Voodoo* GetVoodoo();
    void ScheduleEvent();
    void UpdateVoodooOutput();
    void CompleteVoodooFrame();
    void Reset();
    bool Decodes(u16 port) const;
    u8 Read(u16 port, u64 clocks);
    u8 Peek(u16 port, u64 clocks) const;
    void Write(u16 port, u8 value, u64 clocks);
    u64 GetSyncStableUntil(u64 clocks) const;
    void Synchronize(u64 clocks);
    void HandleEvent(u64 clocks);
    void SetStartCycles(const u32* cycles);
    void BeginSlice();
    void BeginFrame(u8* frame_buffer, bool render);
    void EndFrame();
    bool IsFrameReady() const;
    bool IsRunning() const;
    int GetFrameWidth() const;
    int GetFrameHeight() const;
    int GetDisplayWidth() const;
    int GetDisplayHeight() const;
    u32 GetCanvasHDivider() const;
    u32 GetCGABorderColor() const;
    void SetFrameSize(int frame_width, int frame_height, int display_width, int display_height);
    float GetFrameTime() const;
    void EnableNativeResolution(bool enable);
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
    u32 GetVerticalScale() const;
    u32 GetVerticalDisplayEnd() const;
    u32 GetVerticalRetraceStart() const;
    u32 GetLineCompare() const;
    u32 GetStartAddress() const;
    u32 GetCursorAddress() const;
    u32 GetCRTCOffset(u32 address, u32 row) const;
    u32 GetCharacterClockDots() const;
    u32 GetExtendedClockMultiplier() const;
    u32 GetBeamLine(u64 clocks) const;
    u32 GetBeamDot(u64 clocks) const;
    u8 GetSyncStatus(u64 clocks) const;
    bool IsDirectMapped() const;
    void UpdateVRAMMapping();
    int GetCard() const;
    int GetFamily() const;
    bool IsCirrus() const;
    bool IsEGA() const;
    bool IsCGA() const;
    MC6845* GetMC6845();
    bool IsEGA350Lines() const;
    u8 GetEGASwitches() const;
    u32 GetCGAColor(int index) const;
    bool IsCGAGraphics() const;
    bool IsCGAHighResolution() const;
    void SetCGAOutput(int output);
    int GetCGAOutput() const;
    bool IsCGAComposite() const;
    bool IsUnlocked() const;
    u32 GetVRAMSize() const;
    u32 GetApertureSize() const;
    bool IsExtendedMode() const;
    Video_Extended_Format GetExtendedFormat() const;
    u32 GetExtendedBytesPerPixel() const;
    u32 GetVideoClock(int index) const;
    u32 GetCurrentDotClock() const;
    u32 GetBankOffset(int bank) const;
    bool IsDualPage() const;
    u32 GetApertureBase() const;
    u32 GetOffsetRegister() const;
    u32 GetCounterMask() const;
    bool IsBltWaiting() const;
    u32 GetBltCount() const;
    u32 GetBltFrameCount(int kind) const;
    u32 GetBltFrameRemaps() const;
    bool IsBltMapped() const;
    u32 GetBltPixelBytes() const;
    u32 GetBltWidth() const;
    u32 GetBltHeight() const;
    u32 GetBltPitch(int index) const;
    u32 GetBltAddress(int index) const;
    u32 GetCursorSize() const;
    u32 GetCursorPatternAddress() const;
    u32 GetCursorColor(int index) const;
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
    static u8 ReadConfigCallback(void* device, u8 function, u8 offset);
    static void WriteConfigCallback(void* device, u8 function, u8 offset, u8 value);
    u8 ReadConfig(u8 function, u8 offset) const;
    void WriteConfig(u8 function, u8 offset, u8 value);
    static u8 ReadLinearCallback(void* device, u32 offset);
    static u8 PeekLinearCallback(void* device, u32 offset);
    static void WriteLinearCallback(void* device, u32 offset, u8 value);
    static u16 ReadLinear16Callback(void* device, u32 offset);
    static u32 ReadLinear32Callback(void* device, u32 offset);
    static void WriteLinear16Callback(void* device, u32 offset, u16 value);
    static void WriteLinear32Callback(void* device, u32 offset, u32 value);
    static u8 PeekPlane0Callback(void* device, u32 offset);
    static u8 PeekPlane1Callback(void* device, u32 offset);
    static u8 PeekPlane2Callback(void* device, u32 offset);
    static u8 PeekPlane3Callback(void* device, u32 offset);
    static void PokePlane0Callback(void* device, u32 offset, u8 value);
    static void PokePlane1Callback(void* device, u32 offset, u8 value);
    static void PokePlane2Callback(void* device, u32 offset, u8 value);
    static void PokePlane3Callback(void* device, u32 offset, u8 value);
    static u8 ReadCGACallback(void* device, u32 offset);
    static u8 PeekCGACallback(void* device, u32 offset);
    static void WriteCGACallback(void* device, u32 offset, u8 value);
    static u16 ReadCGA16Callback(void* device, u32 offset);
    static u32 ReadCGA32Callback(void* device, u32 offset);
    static void WriteCGA16Callback(void* device, u32 offset, u16 value);
    static void WriteCGA32Callback(void* device, u32 offset, u32 value);

private:
    void WriteAttribute(u8 value, u64 clocks);
    void WriteSequencer(u8 value, u64 clocks);
    void WriteGraphics(u8 value, u64 clocks);
    void WriteCRTC(u8 value, u64 clocks);
    void WriteMisc(u8 value, u64 clocks);
    void WriteDAC(u8 value, u64 clocks);
    u8 ReadDAC();
    u8 PeekSequencer() const;
    u8 PeekGraphics() const;
    u8 PeekCRTC() const;
    u8 PeekEGA(u16 port, u64 clocks) const;
    u8 PeekEGACRTC() const;
    u8 GetEGAVideoStatus(u64 clocks) const;
    u8 PeekPixelValue(int x) const;
    u8 PeekCGA(u16 port, u64 clocks) const;
    void WriteCGA(u16 port, u8 value, u64 clocks);
    void WriteCGAMode(u8 value, u64 clocks);
    void WriteCGAColor(u8 value, u64 clocks);
    void LatchCGATiming();
    void FinishCGALine();
    void UpdateCGAGeometry();
    void UpdateCGAColors();
    void BuildCompositeTables();
    void UpdateEGAColors();
    u32 GetCGACharacterDots() const;
    void RenderCGA(u32* destination, int x, int end);
    void RenderCGAPixels(u32* destination, int x, int end, const u32* colors);
    void RenderCGAComposite(u32* destination, int x, int end);
    void RenderCGAText(u32* destination, int x, int end, const u32* colors);
    template<bool high>
    void RenderCGAGraphics(u32* destination, int x, int end, const u32* colors);
    NO_INLINE void TraceCGAWrite(u16 port, u8 value, u64 clocks);
    void WriteCirrusSequencer(u8 index, u8 value, u64 clocks);
    void WriteCirrusGraphics(u8 index, u8 value, u64 clocks);
    void WriteCirrusCRTC(u8 index, u8 value, u64 clocks);
    void WriteHiddenDAC(u8 value, u64 clocks);
    void WriteBltControl(u8 value, u64 clocks);
    void StartBlt(u64 clocks);
    void FinishBlt();
    void ResetBlt();
    void RunBltLine(const u8* system);
    bool BuildBltExpansion(u8* source, u8* enable, u32 width, const u8* system);
    bool BuildBltPattern(u8* source, u8* enable, u32 width);
    void ApplyBltLine(const u8* source, const u8* enable, u32 width, bool masked);
    u32 GetBltNeed() const;
    void FeedBlt(u8 value);
    u8 ReadBlt();
    bool TakesBltAccess(u32 offset, bool linear, bool read) const;
    u8 ReadMMIO(u32 offset) const;
    void WriteMMIO(u32 offset, u8 value);
    void UpdateBltMapping(bool system);
    bool WriteCirrusMemory(u32 address, u8 value);
    u32 GetShiftedWindowAddress(u32 address) const;
    bool IsLinearSpecial() const;
    u32 GetLinearOffset(u32 offset) const;
    void RenderCursor(u32* destination, int x, int end);
    void UpdateExtendedFormat();
    void UpdateAperture();
    void RunNextEvent();
    void SynchronizeVRAM();
    void SynchronizeVRAMWrite();
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
    u32 GetWindowAddress(u32 address) const;
    u8 ReadMemory(u32 address);
    u8 PeekMemory(u32 address) const;
    void WriteMemory(u32 address, u8 value);
    u32 GetApertureMappedSize() const;
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
    template<Video_Extended_Format format>
    void RenderExtended(u32* destination, int x, int end);
    int GetPixelPan(bool text, bool packed) const;
    void UpdateDACColor(int index);
    void UpdateAttributeColors();
    void UpdateColorCaches();
    bool GetBlinkPhase() const;
    bool GetCursorPhase() const;
    u32 ToXRGB(u8 red, u8 green, u8 blue) const;
    void ApplyWrite(u16 port, u8 value, u64 clocks);
    NO_INLINE void WriteTraced(u16 port, u8 value, u64 clocks);
    NO_INLINE GDOX_Trace_Entry* TraceEvent(u8 event, u8 reg, u16 value, u8 raw, u8 bank, u32 param, u64 clocks);
    NO_INLINE void TraceColor(u8 index, u64 clocks);
    NO_INLINE void FlushPaletteLoad();
    NO_INLINE void TraceFrameStart(u64 clocks);
    NO_INLINE void TraceFrameComplete();
    NO_INLINE void CountStatusRead(u64 clocks);
    NO_INLINE void TraceBlt(u8 event, u64 clocks);
    void FlushDeferred(u64 clocks);
    u8 GetTraceAdapter() const;
    void Serialize(StateSerializer& serializer, int version);
    void SanitizeState();

private:
    struct Video_Trace
    {
        u64 load_clocks;
        u32 load_first_rgb;
        u32 load_last_rgb;
        u32 mode[6];
        u32 frame[4];
        u32 status_reads;
        u32 status_retrace;
        u32 status_off;
        u16 load_first;
        u16 load_count;
        u16 load_first_line;
        u16 load_last_line;
        u16 load_next;
        u16 status_first_line;
        u16 status_last_line;
        u16 start_line;
        u8 deferred;
        bool load_extended;
        bool status;
    };

private:
    Video_State m_state;
    PIC* m_pic;
    TraceLogger* m_trace_logger;
    Video_Trace m_trace;
    GDOX_Trace_Entry m_trace_blt;
    Scheduler* m_scheduler;
    Memory* m_memory;
    u8* m_frame_buffer;
    Voodoo* m_voodoo;
    bool m_voodoo_enabled;
    bool m_voodoo_threaded;
    bool m_voodoo_shown;
    bool m_render_requested;
    bool m_render;
    bool m_frame_ready;
    u64 m_next_event_clocks;
    u32 m_next_event_line;
    u64 m_edge_clocks;
    const u32* m_start_cycles;
    u32 m_vram_deadline;
    u64 m_line_start_clocks;
    u64 m_display_end_clocks;
    u32 m_display_clocks;
    int m_frame_width;
    int m_frame_height;
    int m_display_width;
    int m_display_height;
    int m_render_width;
    int m_render_height;
    int m_canvas_width;
    int m_canvas_height;
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
    u32 m_direct_low[2][256];
    u32 m_direct_high[2][256];
    int m_card;
    int m_family;
    bool m_cirrus;
    bool m_ega;
    bool m_cga;
    MC6845* m_mc6845;
    u32 m_cga_colors[16];
    int m_cga_output;
    bool m_cga_composite;
    u64 m_composite_pairs[2][2][4][256];
    u64 m_composite_single[2][4][16];
    u8 m_composite_clamp[1 << 15];
    u8 m_composite_line[GDOX_MAX_FRAME_BUFFER_WIDTH + VIDEO_COMPOSITE_TAPS * 2];
    bool m_local_bus;
    bool m_pci;
    u32 m_vram_size;
    u32 m_vram_mask;
    u32 m_plane_mask;
    bool m_extended;
    bool m_wide_counter;
    bool m_dual_page;
    u32 m_bank_low;
    u32 m_bank_high;
    u32 m_mapped_bank;
    u32 m_aperture_base;
    u32 m_aperture_mapped;
    Video_Extended_Format m_extended_format;
    u32 m_extended_bytes;
    u32 m_window_limit;
    u32 m_address_shift;
    bool m_cirrus_writes;
    bool m_mmio_low;
    bool m_mmio_top;
    bool m_blt_handlers;
    bool m_blt_frame_used;
    u32 m_blt_count;
    u32 m_blt_kinds[VIDEO_BLT_KIND_COUNT];
    u32 m_blt_frame_kinds[VIDEO_BLT_KIND_COUNT];
    u32 m_blt_remaps;
    u32 m_blt_frame_remaps;
    int m_render_row;
    u8 m_blt_source[VIDEO_BLT_BUFFER_SIZE];
    u8 m_blt_enable[VIDEO_BLT_BUFFER_SIZE];
};

static const u32 k_video_dot_clock_25 = 25175000;
static const u32 k_video_dot_clock_28 = 28322000;
static const u32 k_video_reference_clock = 14318180;
static const u32 k_video_dot_clock_14 = 14318180;
static const u32 k_video_dot_clock_16 = 16257000;
static const u32 k_video_cga_base = 0xB8000;
static const u32 k_video_cga_window_size = 0x8000;
static const u32 k_video_cga_vsync_lines = 16;
static const u8 k_video_ega_switches = 0x09;
static const u32 k_video_aperture_base = 0x00E00000;
static const u8 k_video_cirrus_id = 0x98;
static const u8 k_video_cirrus_5446_id = 0xB8;
static const u32 k_video_pci_base_mask = 0xFF000000;
static const u8 k_video_cirrus_unlock = 0x12;
static const u32 k_video_min_line_dots = 64;
static const u32 k_video_min_lines = 16;
static const u32 k_video_max_window_size = 0x20000;
static const int k_video_irq = 2;
static const u32 k_video_cursor_blink_shift = 3;
static const u32 k_video_text_blink_shift = 4;

static const u8 k_video_sequencer_masks[VIDEO_SEQUENCER_COUNT] = { 0x03, 0x3D, 0x0F, 0x3F, 0x0E };
static const u8 k_video_graphics_masks[VIDEO_GRAPHICS_COUNT] = { 0x0F, 0x0F, 0x0F, 0x1F, 0x03, 0x7B, 0x0F, 0x0F, 0xFF };

static const u8 k_video_ega_sequencer_masks[VIDEO_SEQUENCER_COUNT] = { 0x03, 0x0F, 0x0F, 0x0F, 0x07 };
static const u8 k_video_ega_graphics_masks[VIDEO_GRAPHICS_COUNT] = { 0x0F, 0x0F, 0x0F, 0x1F, 0x07, 0x3F, 0x0F, 0x0F, 0xFF };
static const u8 k_video_ega_crtc_masks[VIDEO_CRTC_COUNT] =
{
    0xFF, 0xFF, 0xFF, 0x7F, 0xFF, 0xFF, 0xFF, 0x1F, 0x1F, 0x1F, 0x1F, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x3F, 0xFF, 0xFF, 0x1F, 0xFF, 0x1F, 0xFF, 0xFF
};

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

static const int k_video_sr_unlock = 0x06;
static const int k_video_sr_extended = 0x07;
static const int k_video_sr_vclk_numerator = 0x0B;
static const int k_video_sr_dram = 0x0F;
static const int k_video_sr_cursor_attributes = 0x12;
static const int k_video_sr_config = 0x17;
static const int k_video_sr_vclk_denominator = 0x1B;
static const int k_video_gr_offset_0 = 0x09;
static const int k_video_gr_offset_1 = 0x0A;
static const int k_video_gr_extensions = 0x0B;
static const int k_video_gr_background = 0x00;
static const int k_video_gr_foreground = 0x01;
static const int k_video_gr_background_high = 0x10;
static const int k_video_gr_foreground_high = 0x11;
static const int k_video_gr_blt_width = 0x20;
static const int k_video_gr_blt_height = 0x22;
static const int k_video_gr_blt_dst_pitch = 0x24;
static const int k_video_gr_blt_src_pitch = 0x26;
static const int k_video_gr_blt_dst = 0x28;
static const int k_video_gr_blt_src = 0x2C;
static const int k_video_gr_blt_clip = 0x2F;
static const int k_video_gr_blt_mode = 0x30;
static const int k_video_gr_blt_status = 0x31;
static const int k_video_gr_blt_rop = 0x32;
static const int k_video_gr_blt_extensions = 0x33;
static const int k_video_gr_blt_key = 0x34;
static const int k_video_gr_blt_key_mask = 0x38;
static const int k_video_sr_cursor_x = 0x10;
static const int k_video_sr_cursor_y = 0x11;
static const int k_video_sr_cursor_pattern = 0x13;
static const int k_video_cr_interlace_end = 0x19;
static const int k_video_cr_misc_control = 0x1A;
static const int k_video_cr_extended_display = 0x1B;
static const int k_video_cr_overlay = 0x1D;
static const int k_video_cr_part_status = 0x25;
static const int k_video_cr_id = 0x27;

// The 542x's reset values for the four video clocks (542X p398); the 542x memory clock readback and bus width straps:
// 50.11 MHz and a 32-bit DRAM bus with 2 MB (CIRRUS.TXT l1583-1593, l35-40)
static const u8 k_video_cirrus_sequencer_reset[VIDEO_SEQUENCER_EXT_COUNT] =
{
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x4A, 0x5B, 0x45, 0x7E, 0x18,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2B, 0x2F, 0x30, 0x33, 0x1C
};

static const u8 k_video_cirrus_5446_sequencer_reset[VIDEO_SEQUENCER_EXT_COUNT] =
{
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x66, 0x5B, 0x45, 0x7E, 0x18,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3B, 0x2F, 0x30, 0x33, 0x1C
};

static const int k_video_ar_mode = 0x10;
static const int k_video_ar_overscan = 0x11;
static const int k_video_ar_plane_enable = 0x12;
static const int k_video_ar_panning = 0x13;
static const int k_video_ar_color_select = 0x14;

#include "video_inline.h"

#endif /* VIDEO_H */
