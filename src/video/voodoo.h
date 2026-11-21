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

#ifndef VOODOO_H
#define VOODOO_H

#include <iostream>
#include "../common/common.h"
#include "voodoo_regs.h"
#include "voodoo_raster.h"

#define VOODOO_FB_SIZE 0x200000
#define VOODOO_FB_PIXELS (VOODOO_FB_SIZE / 2)
#define VOODOO_FB_PAGES (VOODOO_FB_SIZE >> 12)
#define VOODOO_PCI_FIFO_SIZE 64
#define VOODOO_QUEUE_SIZE (VOODOO_PCI_FIFO_SIZE + 0x10000)
#define VOODOO_CLUT_SIZE 33
#define VOODOO_COUNTERS 5

class Memory;
class Scheduler;
class StateSerializer;
class TraceLogger;
class Video;
class VoodooDac;
class VoodooTrex;

class Voodoo
{
public:
    struct Voodoo_Entry
    {
        u32 address;
        u32 data;
    };

    struct Voodoo_Params
    {
        s32 vertex[6];
        u32 start[5];
        u32 dx[5];
        u32 dy[5];
        s64 start_w;
        s64 dwdx;
        s64 dwdy;
    };

    struct Voodoo_State
    {
        u32 regs[VOODOO_REGISTERS];
        Voodoo_Params params;
        u32 init_enable;
        u32 pci_base;
        u8 pci_command;
        u8 pci_line;
        u8 dac_read;
        u8 swaps_pending;
        bool swap_waiting;
        u8 front_buffer;
        u8 back_buffer;
        u8 retrace_count;
        u32 swap_command;
        u32 queue_read;
        u32 queue_count;
        u64 busy_clocks;
        u32 busy_fraction;
        u64 frame_start_clocks;
        u32 frame_fraction;
        u32 frame_count;
        u32 counters[VOODOO_COUNTERS];
        u32 clut[VOODOO_CLUT_SIZE];
        u16 fb[VOODOO_FB_PIXELS];
    };

    struct Voodoo_Video
    {
        bool running;
        u32 clock;
        u32 h_total;
        u32 v_total;
        u32 v_sync;
        u32 width;
        u32 height;
        u64 line_fixed;
        u64 frame_fixed;
    };

public:
    Voodoo();
    ~Voodoo();
    void Init(Video* video, Scheduler* scheduler, Memory* memory);
    void SetTraceLogger(TraceLogger* trace_logger);
    void SetEnabled(bool enabled);
    bool IsEnabled() const;
    void SetThreaded(bool threaded);
    bool IsThreaded() const;
    void SyncRender() const;
    void Reset();
    void Synchronize(u64 clocks);
    void HandleEvent(u64 clocks);
    u64 GetEventClocks() const;
    void Drain();
    bool IsDisplayed() const;
    void RenderFrame(u32* destination);
    int GetFrameWidth() const;
    int GetFrameHeight() const;
    float GetFrameTime() const;
    const Voodoo_Video* GetVideo() const;
    void UpdateWindow();
    u16* GetFrameBuffer();
    VoodooDac* GetDac();
    VoodooTrex* GetTrex();
    u32 GetBufferBase(int buffer) const;
    u32 GetRowPixels() const;
    u32 GetQueueCount() const;
    u64 GetBusyClocks() const;
    u32 GetStatus(u64 clocks) const;
    u32 GetBufferRows() const;
    u32 GetFbiClock() const;
    void RenderBuffer(int buffer, u32* destination, u32 width, u32 height) const;
    void SetDirtyTracking(bool enabled);
    const u8* GetDirtyPages() const;
    void ClearDirtyPages();
    Voodoo_State* GetState();
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream);

    static u8 ReadConfigCallback(void* device, u8 function, u8 offset);
    static void WriteConfigCallback(void* device, u8 function, u8 offset, u8 value);
    static u8 ReadCallback(void* device, u32 offset);
    static u8 PeekCallback(void* device, u32 offset);
    static void WriteCallback(void* device, u32 offset, u8 value);
    static u16 Read16Callback(void* device, u32 offset);
    static u32 Read32Callback(void* device, u32 offset);
    static void Write16Callback(void* device, u32 offset, u16 value);
    static void Write32Callback(void* device, u32 offset, u32 value);
    u8 ReadConfig(u8 function, u8 offset);
    void WriteConfig(u8 function, u8 offset, u8 value);
    u32 Read32(u32 offset);
    u32 Peek32(u32 offset) const;
    void Write32(u32 offset, u32 value);
    void Write16(u32 offset, u16 value);

private:
    u64 GetAccessClocks();
    void Push(u32 address, u32 data, u64 clocks);
    void RunQueue(u64 clocks);
    void RunEntry(u32 address, u32 data);
    void AddBusy(u32 fbi_clocks);
    u32 GetQueueCapacity() const;
    u32 ReadRegister(u32 offset, u64 clocks);
    u32 PeekRegister(int reg) const;
    void WriteRegister(u32 offset, u32 value, u64 clocks);
    void WriteInit(int reg, u32 value, u64 clocks);
    void WriteFifoRegister(u32 address, u32 value);
    void WriteFbiRegister(int reg, u32 value);
    void Triangle(u32 sign);
    void FastFill();
    void Swap(u32 value);
    void ApplySwap();
    void Retrace(u64 clocks);
    u32 ReadLFB(u32 offset);
    void WriteLFB(u32 offset, u32 value, u32 mask);
    void WriteClut(u32 value);
    void WriteDac(u32 value);
    void UpdatePipeline();
    void UpdateVideo(u64 clocks);
    void UpdateOutput();
    void UpdateClutColors();
    void UpdateFbiClock();
    void ScheduleRetrace();
    u32 GetLine(u64 clocks) const;
    bool IsInRetrace(u64 clocks) const;
    u32 GetVRetrace(u64 clocks) const;
    void ResetGraphics();
    void UpdateThread();
    void TraceEvent(u8 event, u8 command, u32 value, u32 data, u32 pixels, u8 buffer);
    void Serialize(StateSerializer& serializer);
    void SanitizeState();

private:
    Voodoo_State m_state;
    Voodoo_Video m_video_timing;
    VoodooRaster::VoodooRaster_Pipeline m_pipeline;
    Video* m_video;
    Scheduler* m_scheduler;
    Memory* m_memory;
    TraceLogger* m_trace_logger;
    VoodooDac* m_dac;
    VoodooTrex* m_trex;
    VoodooRaster* m_raster;
    bool m_enabled;
    bool m_thread_requested;
    bool m_pipeline_dirty;
    u64 m_event_clocks;
    u64 m_fbi_ratio;
    u32 m_fbi_clock;
    u8 m_reg_flags[VOODOO_REGISTERS];
    u8 m_alt_map[VOODOO_REGISTERS];
    u32 m_clut_colors[65536];
    bool m_dirty_tracking;
    u8 m_dirty_pages[VOODOO_FB_PAGES];
    VoodooRaster::VoodooRaster_Span m_spans[VOODOO_RASTER_MAX_SPANS];
    Voodoo_Entry m_queue[VOODOO_QUEUE_SIZE];
};

static const u32 k_voodoo_window_size = 0x1000000;
static const u32 k_voodoo_lfb_base = 0x400000;
static const u32 k_voodoo_texture_base = 0x800000;
static const u32 k_voodoo_base_mask = 0xFF000000;
static const u32 k_voodoo_init3_straps = 0x00001F00;
static const u32 k_voodoo_init3_strap_values = 0x00000200;
static const u32 k_voodoo_triangle_setup = 16;
static const u32 k_voodoo_triangle_floor_subpixel = 40;
static const u32 k_voodoo_triangle_floor_texture = 60;
static const u32 k_voodoo_pixel_rate = 297;
static const u32 k_voodoo_pixel_rate_rmw = 340;
static const u32 k_voodoo_fastfill_line = 39;

#include "voodoo_inline.h"

#endif /* VOODOO_H */
