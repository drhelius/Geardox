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

#include "voodoo.h"
#include "voodoo_dac.h"
#include "voodoo_trex.h"
#include "video.h"
#include "../system/memory.h"
#include "../system/scheduler.h"
#include "../common/state_serializer.h"
#include "../common/trace_logger.h"

static const int k_voodoo_param_index[5] = { 0, 1, 2, 4, 3 };

static INLINE u32 swap_bytes(u32 value)
{
    return ((value >> 24) & 0xFF) | ((value >> 8) & 0xFF00) | ((value << 8) & 0xFF0000) | (value << 24);
}

static INLINE u32 expand_5(u32 value)
{
    return (value << 3) | (value >> 2);
}

static INLINE u32 expand_6(u32 value)
{
    return (value << 2) | (value >> 4);
}

Voodoo::Voodoo()
{
    InitPointer(m_video);
    InitPointer(m_scheduler);
    InitPointer(m_memory);
    m_trace_logger = TraceLogger::GetDisabled();
    m_dac = new VoodooDac();
    m_trex = new VoodooTrex();
    m_raster = new VoodooRaster();
    m_enabled = false;
    m_thread_requested = false;
    m_pipeline_dirty = true;
    m_event_clocks = GDOX_NO_EVENT;
    m_fbi_ratio = 0;
    m_fbi_clock = 0;
    m_dirty_tracking = false;
    memset(&m_state, 0, sizeof(m_state));
    memset(&m_video_timing, 0, sizeof(m_video_timing));
    memset(&m_pipeline, 0, sizeof(m_pipeline));
    memset(m_dirty_pages, 0, sizeof(m_dirty_pages));
    memset(m_clut_colors, 0, sizeof(m_clut_colors));

    for (int reg = 0; reg < VOODOO_REGISTERS; reg++)
    {
        u8 flags = 0;
        bool param = ((reg >= k_voodoo_start_r) && (reg <= k_voodoo_dwdy)) ||
            ((reg >= k_voodoo_fstart_r) && (reg <= k_voodoo_fdwdy));

        if ((reg <= k_voodoo_color1) || ((reg >= k_voodoo_fog_table) && (reg <= k_voodoo_fog_table_end)))
            flags = k_voodoo_reg_fbi | k_voodoo_reg_fifo;
        else if ((reg >= k_voodoo_texture_mode) && (reg <= k_voodoo_ncc_table_end))
            flags = k_voodoo_reg_trex | k_voodoo_reg_fifo;

        if (param && ((reg & 0x07) >= 5))
            flags = k_voodoo_reg_trex | k_voodoo_reg_fifo | ((reg & 0x07) == 7 ? k_voodoo_reg_fbi : 0);

        m_reg_flags[reg] = flags;
        m_alt_map[reg] = (u8)reg;
    }

    for (int i = 0; i < 24; i++)
    {
        int param = i / 3;
        int kind = i % 3;
        m_alt_map[k_voodoo_start_r + i] = (u8)(k_voodoo_start_r + kind * 8 + param);
        m_alt_map[k_voodoo_fstart_r + i] = (u8)(k_voodoo_fstart_r + kind * 8 + param);
    }
}

Voodoo::~Voodoo()
{
    SafeDelete(m_raster);
    SafeDelete(m_trex);
    SafeDelete(m_dac);
}

void Voodoo::Init(Video* video, Scheduler* scheduler, Memory* memory)
{
    m_video = video;
    m_scheduler = scheduler;
    m_memory = memory;
    m_dac->Init();
    m_trex->Init();
    m_raster->Init(m_state.fb, VOODOO_FB_PIXELS, m_trex->GetMemory(), m_trex->GetDirtyPages(), m_state.counters,
        m_dirty_pages);
}

void Voodoo::SetTraceLogger(TraceLogger* trace_logger)
{
    m_trace_logger = IsValidPointer(trace_logger) ? trace_logger : TraceLogger::GetDisabled();
}

void Voodoo::SetEnabled(bool enabled)
{
    m_enabled = enabled;

    if (!enabled)
        m_event_clocks = GDOX_NO_EVENT;

    UpdateThread();
}

void Voodoo::SetThreaded(bool threaded)
{
    m_thread_requested = threaded;
    UpdateThread();
}

bool Voodoo::IsThreaded() const
{
    return m_raster->IsThreaded();
}

void Voodoo::UpdateThread()
{
    bool threaded = m_enabled && m_thread_requested;

    if (threaded == m_raster->IsThreaded())
        return;

    m_raster->SetThreaded(threaded);
    m_pipeline_dirty = true;
}

void Voodoo::SyncRender() const
{
    m_raster->Sync();
}

void Voodoo::Reset()
{
    m_raster->Sync();
    memset(&m_state, 0, sizeof(m_state));
    m_state.pci_base = k_voodoo_base_mask;
    m_state.pci_line = 0x05;
    m_state.front_buffer = 0;
    m_state.back_buffer = 1;
    m_state.busy_clocks = m_scheduler->GetClocks();
    m_state.frame_start_clocks = m_state.busy_clocks;
    m_state.regs[k_voodoo_fbi_init0] = 0x00000410;
    m_state.regs[k_voodoo_fbi_init1] = 0x00201102;
    m_state.regs[k_voodoo_fbi_init2] = 0x80000040;
    m_state.regs[k_voodoo_fbi_init3] = 0x001E4000 | k_voodoo_init3_strap_values;
    m_state.regs[k_voodoo_fbi_init4] = 0x00000001;

    for (int i = 0; i < 32; i++)
        m_state.clut[i] = (u32)(((i << 3) | (i >> 2)) * 0x010101);

    m_state.clut[32] = 0x00FFFFFF;
    m_dac->Reset();
    m_trex->Reset();
    m_raster->Reload();
    memset(m_dirty_pages, 1, sizeof(m_dirty_pages));
    m_pipeline_dirty = true;
    UpdateFbiClock();
    UpdateClutColors();
    UpdateVideo(m_state.busy_clocks);
}

u64 Voodoo::GetAccessClocks()
{
    GDOX_Bus_Access_Context* context = m_memory->GetAccessContext();

    if (IsValidPointer(context) && IsValidPointer(context->synchronize_memory))
        context->synchronize_memory(context->synchronize_context, *context);

    return m_scheduler->GetClocks();
}

void Voodoo::Synchronize(u64 clocks)
{
    while (m_event_clocks <= clocks)
    {
        u64 retrace = m_event_clocks;
        RunQueue(retrace);
        Retrace(retrace);
    }

    RunQueue(clocks);
}

void Voodoo::HandleEvent(u64 clocks)
{
    Synchronize(clocks);
}

void Voodoo::Drain()
{
    while ((m_state.queue_count > 0) || m_state.swap_waiting)
    {
        if (m_state.swap_waiting)
        {
            m_state.swap_waiting = false;
            ApplySwap();
            continue;
        }

        Voodoo_Entry entry = m_queue[m_state.queue_read];
        m_state.queue_read = (m_state.queue_read + 1) % VOODOO_QUEUE_SIZE;
        m_state.queue_count--;
        RunEntry(entry.address, entry.data);
    }

    m_raster->Sync();
}

void Voodoo::RunQueue(u64 clocks)
{
    while ((m_state.queue_count > 0) && !m_state.swap_waiting && (m_state.busy_clocks <= clocks))
    {
        Voodoo_Entry entry = m_queue[m_state.queue_read];
        m_state.queue_read = (m_state.queue_read + 1) % VOODOO_QUEUE_SIZE;
        m_state.queue_count--;
        RunEntry(entry.address, entry.data);
    }
}

u32 Voodoo::GetQueueCapacity() const
{
    u32 init0 = m_state.regs[k_voodoo_fbi_init0];

    if ((init0 & k_voodoo_init0_mem_fifo) == 0)
        return VOODOO_PCI_FIFO_SIZE;

    u32 init4 = m_state.regs[k_voodoo_fbi_init4];
    u32 first = (init4 >> 8) & 0x3FF;
    u32 last = (init4 >> 18) & 0x3FF;
    u32 rows = last >= first ? last - first + 1 : 0;

    return VOODOO_PCI_FIFO_SIZE + MIN(rows * 512, (u32)(VOODOO_QUEUE_SIZE - VOODOO_PCI_FIFO_SIZE));
}

void Voodoo::Push(u32 address, u32 data, u64 clocks)
{
    if ((m_state.queue_count == 0) && !m_state.swap_waiting && (m_state.busy_clocks <= clocks))
    {
        m_state.busy_clocks = clocks;
        m_state.busy_fraction = 0;
        RunEntry(address, data);
        return;
    }

    if (m_state.queue_count >= GetQueueCapacity())
    {
        if (m_state.swap_waiting)
        {
            m_state.swap_waiting = false;
            ApplySwap();
        }

        if (m_state.queue_count > 0)
        {
            Voodoo_Entry entry = m_queue[m_state.queue_read];

            if (unlikely(m_trace_logger->IsEventEnabled(TRACE_VOODOO, TRACE_VOODOO_OVERFLOW)))
                TraceEvent(TRACE_VOODOO_OVERFLOW, 0, entry.address & 0x00FFFFFF, entry.data, 0, 0);

            m_state.queue_read = (m_state.queue_read + 1) % VOODOO_QUEUE_SIZE;
            m_state.queue_count--;
            RunEntry(entry.address, entry.data);
        }
    }

    u32 write = (m_state.queue_read + m_state.queue_count) % VOODOO_QUEUE_SIZE;
    m_queue[write].address = address;
    m_queue[write].data = data;
    m_state.queue_count++;
}

void Voodoo::AddBusy(u32 fbi_clocks)
{
    u64 total = (u64)fbi_clocks * m_fbi_ratio + m_state.busy_fraction;
    m_state.busy_clocks += total >> 32;
    m_state.busy_fraction = (u32)total;
}

void Voodoo::UpdateFbiClock()
{
    m_fbi_clock = MAX(m_dac->GetGraphicsClock(), 1000000U);
    m_fbi_ratio = ((u64)GDOX_MACHINE_CLOCK_RATE << 32) / m_fbi_clock;
}

u32 Voodoo::GetFbiClock() const
{
    return m_fbi_clock;
}

u32 Voodoo::GetBufferRows() const
{
    u32 row_pixels = GetRowPixels();
    return row_pixels == 0 ? 0 : MIN(((m_state.regs[k_voodoo_fbi_init2] >> 11) & 0x1FF) * 2048 / row_pixels, 1024U);
}

void Voodoo::RenderBuffer(int buffer, u32* destination, u32 width, u32 height) const
{
    m_raster->Sync();
    u32 base = GetBufferBase(buffer);
    u32 row_pixels = GetRowPixels();

    for (u32 y = 0; y < height; y++)
    {
        for (u32 x = 0; x < width; x++)
        {
            u64 pixel = (u64)base + (u64)y * row_pixels + x;
            u16 value = pixel < VOODOO_FB_PIXELS ? m_state.fb[pixel] : 0;

            if (buffer == 2)
                destination[y * width + x] = 0xFF000000 | ((u32)(value >> 8) * 0x010101);
            else
                destination[y * width + x] = m_clut_colors[value];
        }
    }
}

void Voodoo::RunEntry(u32 address, u32 data)
{
    u32 offset = address & 0x00FFFFFF;

    if (offset < k_voodoo_lfb_base)
    {
        AddBusy(1);
        WriteFifoRegister(offset, data);
    }
    else if (offset < k_voodoo_texture_base)
    {
        AddBusy(1);
        WriteLFB(offset - k_voodoo_lfb_base, data, (address >> 24) & 0x03);
    }
    else
    {
        AddBusy(1);

        if (((offset >> 21) & 0x03) != 0)
            return;

        u32 start = 0;
        bool wide = false;
        u32 count = m_trex->DecodeTextureWrite(offset & 0x1FFFFF, data, start, wide);

        if (count > 0)
            m_raster->WriteTexture(start, data, count, wide);
    }
}

void Voodoo::WriteFifoRegister(u32 address, u32 value)
{
    int reg = (int)((address >> 2) & 0xFF);
    bool high = (address & 0x200000) != 0;

    if (high && ((m_state.regs[k_voodoo_fbi_init3] & k_voodoo_init3_remap) != 0))
        reg = m_alt_map[reg];

    if (((address & 0x100000) != 0) && ((m_state.regs[k_voodoo_fbi_init0] & k_voodoo_init0_swizzle) != 0))
        value = swap_bytes(value);

    u32 chip = (address >> 10) & 0x0F;

    if (chip == 0)
        chip = 0x0F;

    u8 flags = m_reg_flags[reg];

    if (((flags & k_voodoo_reg_trex) != 0) && ((chip & 0x02) != 0))
    {
        m_trex->WriteRegister(reg, value);

        if (reg >= k_voodoo_texture_mode)
            m_pipeline_dirty = true;
    }

    if (((flags & k_voodoo_reg_fbi) != 0) && ((chip & 0x01) != 0))
        WriteFbiRegister(reg, value);
}

void Voodoo::WriteFbiRegister(int reg, u32 value)
{
    Voodoo_Params& params = m_state.params;

    if ((reg >= k_voodoo_vertex_ax) && (reg <= k_voodoo_vertex_cy))
    {
        params.vertex[reg - k_voodoo_vertex_ax] = (s32)(s16)value;
        return;
    }

    if ((reg >= k_voodoo_fvertex_ax) && (reg <= k_voodoo_fvertex_cy))
    {
        params.vertex[reg - k_voodoo_fvertex_ax] = (s32)(s16)voodoo_float_to_s32(value, 4);
        return;
    }

    bool fixed = (reg >= k_voodoo_start_r) && (reg <= k_voodoo_dwdy);

    if (fixed || ((reg >= k_voodoo_fstart_r) && (reg <= k_voodoo_fdwdy)))
    {
        int base = fixed ? k_voodoo_start_r : k_voodoo_fstart_r;
        int kind = (reg - base) >> 3;
        int param = (reg - base) & 0x07;

        if (param == 7)
        {
            s64 w = fixed ? (s64)(s32)value << 2 : voodoo_float_to_s64(value, 32);

            if (kind == 0)
                params.start_w = w;
            else if (kind == 1)
                params.dwdx = w;
            else
                params.dwdy = w;

            return;
        }

        if (param >= 5)
            return;

        int index = k_voodoo_param_index[param];
        u32 fixed_value;

        if (!fixed)
            fixed_value = (u32)voodoo_float_to_s32(value, 12);
        else if (index == 4)
            fixed_value = value;
        else
            fixed_value = (u32)((s32)(value << 8) >> 8);

        if (kind == 0)
            params.start[index] = fixed_value;
        else if (kind == 1)
            params.dx[index] = fixed_value;
        else
            params.dy[index] = fixed_value;

        return;
    }

    switch (reg)
    {
        case k_voodoo_triangle_cmd:
        case k_voodoo_ftriangle_cmd:
            Triangle(value & 0x80000000);
            return;
        case k_voodoo_nop_cmd:
            if (unlikely(m_trace_logger->IsEventEnabled(TRACE_VOODOO, TRACE_VOODOO_COMMAND)))
                TraceEvent(TRACE_VOODOO_COMMAND, TRACE_VOODOO_NOP, value, 0, 0, 0);

            if ((value & 0x01) != 0)
                m_raster->ResetCounters();
            return;
        case k_voodoo_fastfill_cmd:
            FastFill();
            return;
        case k_voodoo_swapbuffer_cmd:
            if (unlikely(m_trace_logger->IsEventEnabled(TRACE_VOODOO, TRACE_VOODOO_COMMAND)))
                TraceEvent(TRACE_VOODOO_COMMAND, TRACE_VOODOO_SWAP, value, 0, 0, m_state.front_buffer);

            Swap(value);
            return;
        case k_voodoo_status:
            return;
        case k_voodoo_fbz_color_path:
            value &= 0x0FFFFFFF;
            break;
        case k_voodoo_fog_mode:
            value &= 0x3F;
            break;
        case k_voodoo_fbz_mode:
            value &= 0x1FFFFF;
            break;
        case k_voodoo_lfb_mode:
            value &= 0x1FFFF;
            break;
        default:
            break;
    }

    m_state.regs[reg] = value;
    m_pipeline_dirty = true;
}

void Voodoo::UpdatePipeline()
{
    const u32* regs = m_state.regs;
    VoodooRaster::VoodooRaster_Pipeline& p = m_pipeline;
    u32 init2 = regs[k_voodoo_fbi_init2];
    u32 fbz_mode = regs[k_voodoo_fbz_mode];

    m_pipeline_dirty = false;
    p.fbz_color_path = regs[k_voodoo_fbz_color_path];
    p.fog_mode = regs[k_voodoo_fog_mode];
    p.alpha_mode = regs[k_voodoo_alpha_mode];
    p.fbz_mode = fbz_mode;
    p.lfb_mode = regs[k_voodoo_lfb_mode];
    p.color0 = regs[k_voodoo_color0];
    p.color1 = regs[k_voodoo_color1];
    p.chroma_key = regs[k_voodoo_chroma_key];
    p.fog_color = regs[k_voodoo_fog_color];
    p.za_color = regs[k_voodoo_za_color];
    p.clip_left = (s32)((regs[k_voodoo_clip_left_right] >> 16) & 0x3FF);
    p.clip_right = (s32)(regs[k_voodoo_clip_left_right] & 0x3FF);
    p.clip_top = (s32)((regs[k_voodoo_clip_low_y_high_y] >> 16) & 0x3FF);
    p.clip_bottom = (s32)(regs[k_voodoo_clip_low_y_high_y] & 0x3FF);
    p.row_pixels = GetRowPixels();
    p.y_origin = (s32)((regs[k_voodoo_fbi_init3] >> 22) & 0x3FF);
    p.texture = ((p.fbz_color_path & k_voodoo_fbz_cp_texture) != 0) &&
        ((regs[k_voodoo_fbi_init3] & k_voodoo_init3_no_texture) == 0);
    p.send_config = (m_trex->GetState()->trex_init[1] & 0x40000) != 0;

    u32 draw = (fbz_mode >> 14) & 0x03;
    p.draw_base = draw == 0 ? GetBufferBase(m_state.front_buffer) :
        draw == 1 ? GetBufferBase(m_state.back_buffer) : VOODOO_RASTER_NO_BUFFER;
    p.aux_base = (init2 & k_voodoo_init2_triple) != 0 ? VOODOO_RASTER_NO_BUFFER : GetBufferBase(2);

    for (int i = 0; i < 32; i++)
    {
        u32 word = regs[k_voodoo_fog_table + i];
        p.fog_delta[i * 2] = (u8)word;
        p.fog_blend[i * 2] = (u8)(word >> 8);
        p.fog_delta[i * 2 + 1] = (u8)(word >> 16);
        p.fog_blend[i * 2 + 1] = (u8)(word >> 24);
    }

    m_raster->SetState(&m_pipeline, p.texture ? m_trex->GetTexture() : NULL);
}

u32 Voodoo::GetBufferBase(int buffer) const
{
    u32 pages = (m_state.regs[k_voodoo_fbi_init2] >> 11) & 0x1FF;
    return (u32)buffer * pages * 2048;
}

void Voodoo::Triangle(u32 sign)
{
    Voodoo_Params& params = m_state.params;
    const s32* v = params.vertex;
    s64 area = (s64)(v[0] - v[2]) * (v[3] - v[5]) - (s64)(v[2] - v[4]) * (v[1] - v[3]);
    u32 path = m_state.regs[k_voodoo_fbz_color_path];
    bool subpixel = (path & k_voodoo_fbz_cp_param_adjust) != 0;

    if ((area == 0) || ((area < 0) != (sign != 0)))
    {
        if (unlikely(m_trace_logger->IsEventEnabled(TRACE_VOODOO, TRACE_VOODOO_CULLED)))
            TraceEvent(TRACE_VOODOO_CULLED, TRACE_VOODOO_TRIANGLE, sign, area != 0 ? 1 : 0, 0, 0);

        AddBusy(subpixel ? 16 : 7);
        return;
    }

    VoodooTrex::VoodooTrex_State* trex = m_trex->GetState();

    if (subpixel)
    {
        s64 dx = 8 - (v[0] & 15);
        s64 dy = 8 - (v[1] & 15);

        for (int i = 0; i < 5; i++)
            params.start[i] += (u32)((dy * (s32)params.dy[i] + dx * (s32)params.dx[i]) >> 4);

        params.start_w += (dy * params.dwdy + dx * params.dwdx) >> 4;
        trex->start_s += (dy * trex->dsdy + dx * trex->dsdx) >> 4;
        trex->start_t += (dy * trex->dtdy + dx * trex->dtdx) >> 4;
        trex->start_w += (dy * trex->dwdy + dx * trex->dwdx) >> 4;
    }

    if (m_pipeline_dirty)
        UpdatePipeline();

    VoodooRaster::VoodooRaster_Triangle triangle;
    triangle.ax = v[0];
    triangle.ay = v[1];

    for (int i = 0; i < 5; i++)
    {
        triangle.start[i] = params.start[i];
        triangle.dx[i] = params.dx[i];
        triangle.dy[i] = params.dy[i];
    }

    triangle.start_w = params.start_w;
    triangle.dwdx = params.dwdx;
    triangle.dwdy = params.dwdy;
    triangle.start_s = trex->start_s;
    triangle.start_t = trex->start_t;
    triangle.start_tw = trex->start_w;
    triangle.dsdx = trex->dsdx;
    triangle.dtdx = trex->dtdx;
    triangle.dtwdx = trex->dwdx;
    triangle.dsdy = trex->dsdy;
    triangle.dtdy = trex->dtdy;
    triangle.dtwdy = trex->dwdy;
    triangle.lod_base = m_pipeline.texture ? VoodooTrex::GetLodBase(trex->dsdx, trex->dsdy, trex->dtdx, trex->dtdy) : 0;
    triangle.stipple = m_state.regs[k_voodoo_stipple];

    s64 pixels = 0;
    int count = VoodooRaster::WalkEdges(v, m_spans, pixels);

    m_raster->DrawTriangle(triangle, v, m_spans, count);

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_VOODOO, TRACE_VOODOO_COMMAND)))
        TraceEvent(TRACE_VOODOO_COMMAND, TRACE_VOODOO_TRIANGLE, sign, 0, (u32)MIN(pixels, (s64)0xFFFFFFFF),
            (u8)((m_state.regs[k_voodoo_fbz_mode] >> 14) & 0x03));

    if ((m_state.regs[k_voodoo_fbz_mode] & k_voodoo_fbz_stipple_pattern) == 0)
    {
        u32 amount = (u32)(pixels & 31);
        u32 stipple = m_state.regs[k_voodoo_stipple];

        if (amount != 0)
            m_state.regs[k_voodoo_stipple] = (stipple << amount) | (stipple >> (32 - amount));
    }

    u32 fbz_mode = m_state.regs[k_voodoo_fbz_mode];
    bool rmw = ((fbz_mode & k_voodoo_fbz_depth) != 0) || ((m_pipeline.alpha_mode & k_voodoo_alpha_blend) != 0);
    u64 cost = k_voodoo_triangle_setup + (((u64)pixels * (rmw ? k_voodoo_pixel_rate_rmw : k_voodoo_pixel_rate)) >> 8);
    u32 floor = m_pipeline.texture ? k_voodoo_triangle_floor_texture : subpixel ? k_voodoo_triangle_floor_subpixel : 0;

    AddBusy((u32)MIN(MAX(cost, (u64)floor), (u64)0x7FFFFFFF));
}

void Voodoo::FastFill()
{
    if (m_pipeline_dirty)
        UpdatePipeline();

    m_raster->FastFill();

    s32 lines = MAX(m_pipeline.clip_bottom - m_pipeline.clip_top, 0);
    s32 width = MAX(m_pipeline.clip_right - m_pipeline.clip_left, 0);

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_VOODOO, TRACE_VOODOO_COMMAND)))
        TraceEvent(TRACE_VOODOO_COMMAND, TRACE_VOODOO_FAST_FILL, 0, 0, (u32)lines * (u32)width,
            (u8)((m_state.regs[k_voodoo_fbz_mode] >> 14) & 0x03));

    AddBusy((u32)lines * k_voodoo_fastfill_line + (((u32)lines * (u32)width) >> 1));
}

void Voodoo::Swap(u32 value)
{
    u64 clocks = m_state.busy_clocks;

    if ((value & 0x01) == 0)
    {
        ApplySwap();
        return;
    }

    u32 interval = (value >> 1) & 0xFF;

    if (IsInRetrace(clocks) && (m_state.retrace_count > interval))
    {
        ApplySwap();
        return;
    }

    m_state.swap_waiting = true;
    m_state.swap_command = value;
}

void Voodoo::ApplySwap()
{
    if ((m_state.regs[k_voodoo_fbi_init2] & k_voodoo_init2_triple) != 0)
    {
        m_state.front_buffer = (u8)((m_state.front_buffer + 1) % 3);
        m_state.back_buffer = (u8)((m_state.front_buffer + 1) % 3);
    }
    else
    {
        u8 front = m_state.front_buffer;
        m_state.front_buffer = m_state.back_buffer;
        m_state.back_buffer = front;
    }

    if (m_state.swaps_pending > 0)
        m_state.swaps_pending--;

    m_state.retrace_count = 0;
    m_pipeline_dirty = true;
}

void Voodoo::Retrace(u64 clocks)
{
    m_state.frame_fraction = (u32)(m_video_timing.frame_fixed + m_state.frame_fraction);
    m_state.frame_start_clocks = clocks;
    m_state.frame_count++;

    if (IsDisplayed())
        m_video->CompleteVoodooFrame();

    if (m_state.retrace_count < 0xFF)
        m_state.retrace_count++;

    if (m_state.swap_waiting && (m_state.retrace_count > ((m_state.swap_command >> 1) & 0xFF)))
    {
        m_state.swap_waiting = false;
        ApplySwap();

        if (m_state.busy_clocks < clocks)
        {
            m_state.busy_clocks = clocks;
            m_state.busy_fraction = 0;
        }
    }

    ScheduleRetrace();
}

void Voodoo::ScheduleRetrace()
{
    if (!m_video_timing.running || !m_enabled)
    {
        m_event_clocks = GDOX_NO_EVENT;
        return;
    }

    m_event_clocks = m_state.frame_start_clocks + ((m_video_timing.frame_fixed + m_state.frame_fraction) >> 32);
}

u32 Voodoo::GetLine(u64 clocks) const
{
    if (!m_video_timing.running || (clocks < m_state.frame_start_clocks))
        return 0xFFFFFFFF;

    u64 elapsed = ((clocks - m_state.frame_start_clocks) % MAX(m_video_timing.frame_fixed >> 32, (u64)1)) << 32;
    elapsed = elapsed >= m_state.frame_fraction ? elapsed - m_state.frame_fraction : 0;
    return (u32)(elapsed / m_video_timing.line_fixed);
}

bool Voodoo::IsInRetrace(u64 clocks) const
{
    return GetLine(clocks) < m_video_timing.v_sync;
}

u32 Voodoo::GetVRetrace(u64 clocks) const
{
    u32 line = GetLine(clocks);

    if ((line == 0xFFFFFFFF) || (line < m_video_timing.v_sync))
        return 0;

    return (line - m_video_timing.v_sync) & 0xFFF;
}

void Voodoo::UpdateVideo(u64 clocks)
{
    const u32* regs = m_state.regs;
    Voodoo_Video& video = m_video_timing;
    bool was_running = video.running;
    u32 old_frame = (u32)(video.frame_fixed >> 32);
    u32 h_sync = regs[k_voodoo_h_sync];
    u32 v_sync = regs[k_voodoo_v_sync];
    u32 dimensions = regs[k_voodoo_video_dimensions];
    u32 divider = ((regs[k_voodoo_fbi_init1] >> 20) & 0x03) == 1 ? 2 : 1;

    video.clock = m_dac->GetVideoClock() / divider;
    video.h_total = (h_sync & 0xFF) + 1 + ((h_sync >> 16) & 0x3FF) + 1;
    video.v_total = (v_sync & 0xFFF) + ((v_sync >> 16) & 0xFFF);
    video.v_sync = v_sync & 0xFFF;
    video.width = MIN((dimensions & 0x3FF) + 1, (u32)GDOX_MAX_FRAME_BUFFER_WIDTH);
    video.height = MIN((dimensions >> 16) & 0x3FF, (u32)GDOX_MAX_FRAME_BUFFER_HEIGHT);
    video.running = ((regs[k_voodoo_fbi_init1] & k_voodoo_init1_video_reset) == 0) && (video.clock >= 1000000) &&
        (h_sync != 0) && (video.v_total > video.v_sync) && (video.height > 0) && ((dimensions & 0x3FF) != 0);

    if (video.running)
    {
        u64 line = (u64)video.h_total * GDOX_MACHINE_CLOCK_RATE;
        video.line_fixed = ((line / video.clock) << 32) + (((line % video.clock) << 32) / video.clock);
        video.frame_fixed = video.line_fixed * video.v_total;
    }

    if ((video.running != was_running) || (video.running && ((u32)(video.frame_fixed >> 32) != old_frame)))
    {
        m_state.frame_start_clocks = clocks;
        m_state.frame_fraction = 0;
        ScheduleRetrace();

        if (IsValidPointer(m_video))
            m_video->ScheduleEvent();
    }

    UpdateOutput();
}

void Voodoo::UpdateOutput()
{
    if (IsValidPointer(m_video))
        m_video->UpdateVoodooOutput();
}

int Voodoo::GetFrameWidth() const
{
    return (int)MAX(m_video_timing.width, 1U);
}

int Voodoo::GetFrameHeight() const
{
    return (int)MAX(m_video_timing.height, 1U);
}

float Voodoo::GetFrameTime() const
{
    if (!m_video_timing.running)
        return 1000.0f / GDOX_VIDEO_FRAME_RATE;

    double clocks = (double)m_video_timing.frame_fixed / 4294967296.0;
    return (float)(clocks * 1000.0 / GDOX_MACHINE_CLOCK_RATE);
}

void Voodoo::RenderFrame(u32* destination)
{
    m_raster->Sync();
    int width = GetFrameWidth();
    int height = GetFrameHeight();
    bool blank = (m_state.regs[k_voodoo_fbi_init1] & k_voodoo_init1_blank) != 0;
    u32 base = GetBufferBase(m_state.front_buffer);
    u32 row_pixels = GetRowPixels();

    for (int y = 0; y < height; y++)
    {
        u32* line = destination + y * width;
        u64 row = (u64)base + (u64)y * row_pixels;

        for (int x = 0; x < width; x++)
        {
            u64 pixel = row + (u64)x;
            line[x] = blank || (pixel >= VOODOO_FB_PIXELS) ? 0xFF000000 : m_clut_colors[m_state.fb[pixel]];
        }
    }
}

void Voodoo::UpdateClutColors()
{
    u8 red[32];
    u8 green[64];
    u8 blue[32];

    for (u32 i = 0; i < 64; i++)
    {
        u32 c = i < 32 ? expand_5(i) : 0;
        u32 g = expand_6(i);
        u32 index = c >> 3;
        u32 fraction = c & 7;
        u32 g_index = g >> 3;
        u32 g_fraction = g & 7;

        if (i < 32)
        {
            red[i] = (u8)(((((m_state.clut[index] >> 16) & 0xFF) * (8 - fraction)) +
                (((m_state.clut[index + 1] >> 16) & 0xFF) * fraction)) >> 3);
            blue[i] = (u8)((((m_state.clut[index] & 0xFF) * (8 - fraction)) + ((m_state.clut[index + 1] & 0xFF) * fraction)) >> 3);
        }

        green[i] = (u8)(((((m_state.clut[g_index] >> 8) & 0xFF) * (8 - g_fraction)) +
            (((m_state.clut[g_index + 1] >> 8) & 0xFF) * g_fraction)) >> 3);
    }

    for (u32 pixel = 0; pixel < 65536; pixel++)
        m_clut_colors[pixel] = 0xFF000000 | ((u32)red[pixel >> 11] << 16) | ((u32)green[(pixel >> 5) & 0x3F] << 8) |
            blue[pixel & 0x1F];
}

void Voodoo::WriteClut(u32 value)
{
    if ((m_state.regs[k_voodoo_fbi_init1] & k_voodoo_init1_video_reset) != 0)
        return;

    u32 index = (value >> 24) & 0x3F;

    if (index >= VOODOO_CLUT_SIZE)
        return;

    m_state.clut[index] = value & 0x00FFFFFF;
    UpdateClutColors();
}

void Voodoo::WriteDac(u32 value)
{
    int reg = (int)((value >> 8) & 0x07);

    if ((value & 0x800) != 0)
        m_state.dac_read = m_dac->Read(reg);
    else
        m_dac->Write(reg, (u8)value);

    UpdateFbiClock();
}

void Voodoo::ResetGraphics()
{
    m_state.queue_count = 0;
    m_state.queue_read = 0;
    m_state.swaps_pending = 0;
    m_state.swap_waiting = false;
    m_state.front_buffer = 0;
    m_state.back_buffer = 1;
    m_pipeline_dirty = true;
}

void Voodoo::WriteInit(int reg, u32 value, u64 clocks)
{
    u32* regs = m_state.regs;

    switch (reg)
    {
        case k_voodoo_fbi_init0:
        case k_voodoo_fbi_init1:
        case k_voodoo_fbi_init2:
        case k_voodoo_fbi_init3:
        case k_voodoo_fbi_init4:
        {
            if ((m_state.init_enable & k_voodoo_enable_init_writes) == 0)
                return;

            u32 old = regs[reg];

            if (reg == k_voodoo_fbi_init1)
                value &= ~0x00000005U;
            else if (reg == k_voodoo_fbi_init3)
                value = (value & ~k_voodoo_init3_straps) | k_voodoo_init3_strap_values;

            regs[reg] = value;
            m_pipeline_dirty = true;

            if ((reg == k_voodoo_fbi_init0) && ((value & ~old & (k_voodoo_init0_graphics_reset | k_voodoo_init0_fifo_reset)) != 0))
            {
                if ((value & k_voodoo_init0_graphics_reset) != 0)
                    ResetGraphics();
                else
                {
                    m_state.queue_count = 0;
                    m_state.queue_read = 0;
                }
            }

            if (reg == k_voodoo_fbi_init1)
                UpdateVideo(clocks);
            else if (reg == k_voodoo_fbi_init0)
                UpdateOutput();
            return;
        }
        case k_voodoo_back_porch:
        case k_voodoo_video_dimensions:
        case k_voodoo_h_sync:
        case k_voodoo_v_sync:
            regs[reg] = value;
            UpdateVideo(clocks);
            return;
        case k_voodoo_clut_data:
            WriteClut(value);
            return;
        case k_voodoo_dac_data:
            WriteDac(value);
            UpdateVideo(clocks);
            return;
        case k_voodoo_max_rgb_delta:
            regs[reg] = value & 0x00FFFFFF;
            return;
        default:
            return;
    }
}

void Voodoo::WriteRegister(u32 offset, u32 value, u64 clocks)
{
    int reg = (int)((offset >> 2) & 0xFF);

    if ((reg >= k_voodoo_fbi_init4) && (reg <= k_voodoo_max_rgb_delta))
    {
        u32 chip = (offset >> 10) & 0x0F;

        if ((chip == 0) || ((chip & 0x01) != 0))
            WriteInit(reg, value, clocks);
        return;
    }

    if ((m_reg_flags[reg] & k_voodoo_reg_fifo) == 0)
        return;

    if ((m_state.init_enable & k_voodoo_enable_fifo_writes) == 0)
        return;

    if (reg == k_voodoo_swapbuffer_cmd)
    {
        u32 chip = (offset >> 10) & 0x0F;

        if (((chip == 0) || ((chip & 0x01) != 0)) && (m_state.swaps_pending < 7))
            m_state.swaps_pending++;
    }

    Push(offset, value, clocks);
}

u32 Voodoo::GetStatus(u64 clocks) const
{
    u32 status = 0;
    u32 count = m_state.queue_count;
    bool memory_fifo = (m_state.regs[k_voodoo_fbi_init0] & k_voodoo_init0_mem_fifo) != 0;

    if (memory_fifo)
        status |= k_voodoo_status_fifo_free | ((0xFFFF - MIN(count, 0xFFFFU)) << 12);
    else
        status |= MIN(VOODOO_PCI_FIFO_SIZE - MIN(count, (u32)VOODOO_PCI_FIFO_SIZE), 0x3FU) | (0xFFFFU << 12);

    if (!IsInRetrace(clocks))
        status |= k_voodoo_status_vretrace;

    if ((count > 0) || m_state.swap_waiting || (m_state.busy_clocks > clocks))
        status |= k_voodoo_status_fbi_busy | k_voodoo_status_trex_busy | k_voodoo_status_busy;

    status |= (u32)m_state.front_buffer << 10;
    status |= (u32)MIN(m_state.swaps_pending, (u8)7) << 28;
    return status;
}

u32 Voodoo::PeekRegister(int reg) const
{
    switch (reg)
    {
        case k_voodoo_fbi_pixels_in:
        case k_voodoo_fbi_chroma_fail:
        case k_voodoo_fbi_zfunc_fail:
        case k_voodoo_fbi_afunc_fail:
        case k_voodoo_fbi_pixels_out:
            return m_state.counters[reg - k_voodoo_fbi_pixels_in] & 0x00FFFFFF;
        case k_voodoo_fbi_init2:
            return (m_state.init_enable & k_voodoo_enable_dac_read) != 0 ? m_state.dac_read : m_state.regs[reg];
        case k_voodoo_fbi_init3:
            return (m_state.init_enable & k_voodoo_enable_dac_read) != 0 ? 0 : m_state.regs[reg];
        default:
            return m_state.regs[reg];
    }
}

u32 Voodoo::ReadRegister(u32 offset, u64 clocks)
{
    int reg = (int)((offset >> 2) & 0xFF);

    switch (reg)
    {
        case k_voodoo_status:
            return GetStatus(clocks);
        case k_voodoo_v_retrace:
            return GetVRetrace(clocks);
        case k_voodoo_fbi_pixels_in:
        case k_voodoo_fbi_chroma_fail:
        case k_voodoo_fbi_zfunc_fail:
        case k_voodoo_fbi_afunc_fail:
        case k_voodoo_fbi_pixels_out:
            Drain();
            return PeekRegister(reg);
        default:
            return PeekRegister(reg);
    }
}

void Voodoo::WriteLFB(u32 offset, u32 value, u32 mask)
{
    if (m_pipeline_dirty)
        UpdatePipeline();

    u32 lfb_mode = m_state.regs[k_voodoo_lfb_mode];
    u32 format = lfb_mode & 0x0F;
    bool wide = (format == 4) || (format == 5) || (format == 12) || (format == 13) || (format == 14);

    if ((format == 3) || ((format >= 6) && (format <= 11)))
        return;

    if ((lfb_mode & k_voodoo_lfb_byte_swizzle) != 0)
    {
        value = swap_bytes(value);
        mask = ((mask & 1) << 1) | ((mask >> 1) & 1);
    }

    if (((lfb_mode & k_voodoo_lfb_word_swap) != 0) && (format != 4) && (format != 5))
    {
        value = (value >> 16) | (value << 16);
        mask = ((mask & 1) << 1) | ((mask >> 1) & 1);
    }

    u32 lanes = (lfb_mode >> 9) & 0x03;
    u32 za = m_state.regs[k_voodoo_za_color];
    u32 default_alpha = za >> 24;
    u32 colors[2];
    u32 depths[2] = { za & 0xFFFF, za & 0xFFFF };
    u32 present[2] = { 0, 0 };
    s32 x;
    s32 y;
    int pixels;

    if (wide)
    {
        x = (s32)((offset >> 2) & 0x3FF);
        y = (s32)((offset >> 12) & 0x3FF);
        pixels = 1;
    }
    else
    {
        x = (s32)((offset >> 1) & 0x3FE);
        y = (s32)((offset >> 11) & 0x3FF);
        pixels = 2;
    }

    for (int i = 0; i < pixels; i++)
    {
        u32 data = wide ? value : (value >> (i * 16)) & 0xFFFF;
        u32 a = default_alpha;
        u32 r = 0;
        u32 g = 0;
        u32 b = 0;
        u32 color_bits = data & 0xFFFF;
        u32 flags = 0;

        if (!wide && ((mask & (1U << i)) == 0))
            continue;

        switch (format)
        {
            case 0:
            case 12:
                r = expand_5((color_bits >> 11) & 0x1F);
                g = expand_6((color_bits >> 5) & 0x3F);
                b = expand_5(color_bits & 0x1F);
                flags = 0x01;
                break;
            case 1:
            case 13:
                if (lanes < 2)
                {
                    r = expand_5((color_bits >> 10) & 0x1F);
                    g = expand_5((color_bits >> 5) & 0x1F);
                    b = expand_5(color_bits & 0x1F);
                }
                else
                {
                    r = expand_5((color_bits >> 11) & 0x1F);
                    g = expand_5((color_bits >> 6) & 0x1F);
                    b = expand_5((color_bits >> 1) & 0x1F);
                }
                flags = 0x01;
                break;
            case 2:
            case 14:
                if (lanes < 2)
                {
                    a = (color_bits & 0x8000) != 0 ? 0xFF : 0x00;
                    r = expand_5((color_bits >> 10) & 0x1F);
                    g = expand_5((color_bits >> 5) & 0x1F);
                    b = expand_5(color_bits & 0x1F);
                }
                else
                {
                    a = (color_bits & 0x0001) != 0 ? 0xFF : 0x00;
                    r = expand_5((color_bits >> 11) & 0x1F);
                    g = expand_5((color_bits >> 6) & 0x1F);
                    b = expand_5((color_bits >> 1) & 0x1F);
                }
                flags = 0x03;
                break;
            case 4:
                if (lanes < 2)
                {
                    r = (data >> 16) & 0xFF;
                    g = (data >> 8) & 0xFF;
                    b = data & 0xFF;
                }
                else
                {
                    r = data >> 24;
                    g = (data >> 16) & 0xFF;
                    b = (data >> 8) & 0xFF;
                }
                flags = 0x01;
                break;
            case 5:
                if (lanes < 2)
                {
                    a = data >> 24;
                    r = (data >> 16) & 0xFF;
                    g = (data >> 8) & 0xFF;
                    b = data & 0xFF;
                }
                else
                {
                    r = data >> 24;
                    g = (data >> 16) & 0xFF;
                    b = (data >> 8) & 0xFF;
                    a = data & 0xFF;
                }
                flags = 0x03;
                break;
            default:
                depths[i] = data & 0xFFFF;
                flags = 0x04;
                break;
        }

        if ((lanes & 0x01) != 0)
        {
            u32 t = r;
            r = b;
            b = t;
        }

        if ((format >= 12) && (format <= 14))
        {
            depths[i] = (value >> 16) & 0xFFFF;

            if ((mask & 0x02) != 0)
                flags |= 0x04;

            if ((mask & 0x01) == 0)
                flags &= ~0x03U;
        }

        if (format == 15)
        {
            u32 color1 = m_state.regs[k_voodoo_color1];
            r = (color1 >> 16) & 0xFF;
            g = (color1 >> 8) & 0xFF;
            b = color1 & 0xFF;
        }

        colors[i] = (a << 24) | (r << 16) | (g << 8) | b;
        present[i] = flags;
    }

    u32 select = (lfb_mode >> 4) & 0x03;

    if (select > 1)
        return;

    u32 buffer = GetBufferBase(select == 0 ? m_state.front_buffer : m_state.back_buffer);

    for (int i = 0; i < pixels; i++)
    {
        if (present[i] == 0)
            continue;

        if ((lfb_mode & k_voodoo_lfb_pipeline) != 0)
        {
            u32 stipple = m_state.regs[k_voodoo_stipple];
            bool visible = VoodooRaster::LfbVisible(m_pipeline, x + i, y, stipple);
            m_state.regs[k_voodoo_stipple] = stipple;
            m_raster->PipelineLfbPixel(buffer, x + i, y, colors[i], depths[i], visible);
        }
        else
        {
            s32 scry = (lfb_mode & k_voodoo_lfb_y_origin) != 0 ? m_pipeline.y_origin - y : y;
            m_raster->WriteLfbPixel(buffer, x + i, scry, colors[i], depths[i], present[i]);
        }
    }
}

u32 Voodoo::ReadLFB(u32 offset)
{
    if ((m_state.regs[k_voodoo_fbi_init1] & k_voodoo_init1_lfb_reads) == 0)
        return 0xFFFFFFFF;

    Drain();

    u32 lfb_mode = m_state.regs[k_voodoo_lfb_mode];
    u32 select = (lfb_mode >> 6) & 0x03;

    if (select == 3)
        return 0xFFFFFFFF;

    u32 base = select == 0 ? GetBufferBase(m_state.front_buffer) : select == 1 ? GetBufferBase(m_state.back_buffer) :
        ((m_state.regs[k_voodoo_fbi_init2] & k_voodoo_init2_triple) != 0 ? VOODOO_RASTER_NO_BUFFER : GetBufferBase(2));
    s32 x = (s32)((offset >> 1) & 0x3FE);
    s32 y = (s32)((offset >> 11) & 0x3FF);

    if ((lfb_mode & k_voodoo_lfb_y_origin) != 0)
        y = (s32)((m_state.regs[k_voodoo_fbi_init3] >> 22) & 0x3FF) - y;

    u32 row_pixels = GetRowPixels();
    u32 result = 0;

    for (int i = 0; i < 2; i++)
    {
        u32 pixel = 0xFFFF;

        if ((base != VOODOO_RASTER_NO_BUFFER) && (y >= 0))
        {
            u64 address = (u64)base + (u64)(u32)y * row_pixels + (u64)(x + i);

            if (address < VOODOO_FB_PIXELS)
                pixel = m_state.fb[address];
        }

        if ((select != 2) && ((lfb_mode & 0x200) != 0))
            pixel = (pixel & 0x07E0) | ((pixel >> 11) & 0x1F) | ((pixel & 0x1F) << 11);

        result |= pixel << (i * 16);
    }

    if ((lfb_mode & k_voodoo_lfb_read_word_swap) != 0)
        result = (result >> 16) | (result << 16);

    if ((lfb_mode & k_voodoo_lfb_read_byte_swizzle) != 0)
        result = swap_bytes(result);

    return result;
}

u32 Voodoo::Read32(u32 offset)
{
    u64 clocks = GetAccessClocks();
    Synchronize(clocks);
    offset &= k_voodoo_window_size - 1;

    if (offset < k_voodoo_lfb_base)
        return ReadRegister(offset, clocks);

    if (offset >= k_voodoo_texture_base)
        return 0xFFFFFFFF;

    u32 queued = m_state.queue_count;
    u32 value = ReadLFB(offset - k_voodoo_lfb_base);

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_VOODOO, TRACE_VOODOO_LFB_READ)))
    {
        GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_VOODOO, TRACE_VOODOO_LFB_READ);
        entry->voodoo.value = offset - k_voodoo_lfb_base;
        entry->voodoo.data = value;
        entry->voodoo.queued = queued;
    }

    return value;
}

u32 Voodoo::Peek32(u32 offset) const
{
    m_raster->Sync();
    offset &= k_voodoo_window_size - 1;

    if (offset < k_voodoo_lfb_base)
        return PeekRegister((int)((offset >> 2) & 0xFF));

    return 0xFFFFFFFF;
}

void Voodoo::Write32(u32 offset, u32 value)
{
    u64 clocks = GetAccessClocks();
    Synchronize(clocks);
    offset &= k_voodoo_window_size - 1;

    if (offset < k_voodoo_lfb_base)
    {
        WriteRegister(offset & ~0x03U, value, clocks);
        return;
    }

    if ((m_state.init_enable & k_voodoo_enable_fifo_writes) == 0)
        return;

    if (offset < k_voodoo_texture_base)
        Push((offset & ~0x03U) | (0x03U << 24), value, clocks);
    else
        Push(offset & ~0x03U, value, clocks);
}

void Voodoo::Write16(u32 offset, u16 value)
{
    offset &= k_voodoo_window_size - 1;

    if ((offset < k_voodoo_lfb_base) || (offset >= k_voodoo_texture_base))
        return;

    u64 clocks = GetAccessClocks();
    Synchronize(clocks);

    if ((m_state.init_enable & k_voodoo_enable_fifo_writes) == 0)
        return;

    bool high = (offset & 0x02) != 0;
    Push((offset & ~0x03U) | ((high ? 0x02U : 0x01U) << 24), high ? (u32)value << 16 : value, clocks);
}

u8 Voodoo::ReadConfig(u8 function, u8 offset)
{
    if (function != 0)
        return 0xFF;

    switch (offset)
    {
        case 0x00:
            return 0x1A;
        case 0x01:
            return 0x12;
        case 0x02:
            return 0x01;
        case 0x04:
            return m_state.pci_command;
        case 0x08:
            return 0x02;
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
            return (u8)(m_state.pci_base >> ((offset - 0x10) * 8));
        case 0x3C:
            return m_state.pci_line;
        case 0x3D:
            return 0x01;
        case 0x40:
        case 0x41:
        case 0x42:
        case 0x43:
            return (u8)(m_state.init_enable >> ((offset - 0x40) * 8));
        case 0x4C:
        case 0x4D:
        case 0x4E:
        case 0x4F:
        {
            u64 clocks = m_scheduler->GetClocks();
            Synchronize(clocks);
            return (u8)(GetStatus(clocks) >> ((offset - 0x4C) * 8));
        }
        default:
            return 0x00;
    }
}

void Voodoo::WriteConfig(u8 function, u8 offset, u8 value)
{
    if (function != 0)
        return;

    switch (offset)
    {
        case 0x04:
            m_state.pci_command = value & 0x02;
            UpdateWindow();
            break;
        case 0x13:
            m_state.pci_base = (u32)value << 24;
            UpdateWindow();
            break;
        case 0x3C:
            m_state.pci_line = value;
            break;
        case 0x40:
        case 0x41:
        case 0x42:
        case 0x43:
        {
            u32 shift = (u32)(offset - 0x40) * 8;
            m_state.init_enable = (m_state.init_enable & ~(0xFFU << shift)) | ((u32)value << shift);
            break;
        }
        default:
            break;
    }
}

void Voodoo::UpdateWindow()
{
    if (!m_enabled || !IsValidPointer(m_memory))
        return;

    bool mapped = ((m_state.pci_command & 0x02) != 0) && (m_state.pci_base != 0);

    if (mapped)
        m_memory->SetRegionSpan(GDOX_DEBUG_REGION_VOODOO, m_state.pci_base, k_voodoo_window_size, NULL, NULL);

    m_memory->SetRegionMapped(GDOX_DEBUG_REGION_VOODOO, mapped);
}

u8 Voodoo::ReadConfigCallback(void* device, u8 function, u8 offset)
{
    return ((Voodoo*)device)->ReadConfig(function, offset);
}

void Voodoo::WriteConfigCallback(void* device, u8 function, u8 offset, u8 value)
{
    ((Voodoo*)device)->WriteConfig(function, offset, value);
}

u8 Voodoo::ReadCallback(void* device, u32 offset)
{
    return (u8)(((Voodoo*)device)->Read32(offset & ~0x03U) >> ((offset & 0x03) * 8));
}

u8 Voodoo::PeekCallback(void* device, u32 offset)
{
    return (u8)(((Voodoo*)device)->Peek32(offset & ~0x03U) >> ((offset & 0x03) * 8));
}

void Voodoo::WriteCallback(void* device, u32 offset, u8 value)
{
    UNUSED(device);
    UNUSED(offset);
    UNUSED(value);
}

u16 Voodoo::Read16Callback(void* device, u32 offset)
{
    return (u16)(((Voodoo*)device)->Read32(offset & ~0x03U) >> ((offset & 0x02) * 8));
}

u32 Voodoo::Read32Callback(void* device, u32 offset)
{
    return ((Voodoo*)device)->Read32(offset);
}

void Voodoo::Write16Callback(void* device, u32 offset, u16 value)
{
    ((Voodoo*)device)->Write16(offset, value);
}

void Voodoo::Write32Callback(void* device, u32 offset, u32 value)
{
    ((Voodoo*)device)->Write32(offset, value);
}

void Voodoo::TraceEvent(u8 event, u8 command, u32 value, u32 data, u32 pixels, u8 buffer)
{
    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_VOODOO, event);
    entry->voodoo.command = command;
    entry->voodoo.value = value;
    entry->voodoo.data = data;
    entry->voodoo.pixels = pixels;
    entry->voodoo.queued = m_state.queue_count;
    entry->voodoo.buffer = buffer;
}

void Voodoo::SetDirtyTracking(bool enabled)
{
    m_raster->Sync();

    if (enabled && !m_dirty_tracking)
        memset(m_dirty_pages, 1, sizeof(m_dirty_pages));

    m_dirty_tracking = enabled;
    m_trex->SetDirtyTracking(enabled);
}

void Voodoo::ClearDirtyPages()
{
    m_raster->Sync();
    memset(m_dirty_pages, 0, sizeof(m_dirty_pages));
    m_trex->ClearDirtyPages();
}

void Voodoo::SaveState(std::ostream& stream)
{
    m_raster->Sync();
    StateSerializer serializer(stream);
    Serialize(serializer);
    m_dac->SaveState(stream);
    m_trex->SaveState(stream);
}

void Voodoo::LoadState(std::istream& stream)
{
    m_raster->Sync();
    StateSerializer serializer(stream);
    Serialize(serializer);
    m_dac->LoadState(stream);
    m_trex->LoadState(stream);
    m_raster->Reload();
    SanitizeState();
}

void Voodoo::Serialize(StateSerializer& serializer)
{
    Voodoo_Params& params = m_state.params;
    G_SERIALIZE_ARRAY(serializer, m_state.regs, VOODOO_REGISTERS);
    G_SERIALIZE_ARRAY(serializer, params.vertex, 6);
    G_SERIALIZE_ARRAY(serializer, params.start, 5);
    G_SERIALIZE_ARRAY(serializer, params.dx, 5);
    G_SERIALIZE_ARRAY(serializer, params.dy, 5);
    G_SERIALIZE(serializer, params.start_w);
    G_SERIALIZE(serializer, params.dwdx);
    G_SERIALIZE(serializer, params.dwdy);
    G_SERIALIZE(serializer, m_state.init_enable);
    G_SERIALIZE(serializer, m_state.pci_base);
    G_SERIALIZE(serializer, m_state.pci_command);
    G_SERIALIZE(serializer, m_state.pci_line);
    G_SERIALIZE(serializer, m_state.dac_read);
    G_SERIALIZE(serializer, m_state.swaps_pending);
    G_SERIALIZE(serializer, m_state.swap_waiting);
    G_SERIALIZE(serializer, m_state.front_buffer);
    G_SERIALIZE(serializer, m_state.back_buffer);
    G_SERIALIZE(serializer, m_state.retrace_count);
    G_SERIALIZE(serializer, m_state.swap_command);
    G_SERIALIZE(serializer, m_state.queue_read);
    G_SERIALIZE(serializer, m_state.queue_count);
    G_SERIALIZE(serializer, m_state.busy_clocks);
    G_SERIALIZE(serializer, m_state.busy_fraction);
    G_SERIALIZE(serializer, m_state.frame_start_clocks);
    G_SERIALIZE(serializer, m_state.frame_fraction);
    G_SERIALIZE(serializer, m_state.frame_count);
    G_SERIALIZE_ARRAY(serializer, m_state.counters, VOODOO_COUNTERS);
    G_SERIALIZE_ARRAY(serializer, m_state.clut, VOODOO_CLUT_SIZE);
    G_SERIALIZE_ARRAY(serializer, m_state.fb, VOODOO_FB_PIXELS);

    if (serializer.IsLoading())
        m_state.queue_count = MIN(m_state.queue_count, (u32)VOODOO_QUEUE_SIZE);

    for (u32 i = 0; i < m_state.queue_count; i++)
    {
        Voodoo_Entry& entry = m_queue[(m_state.queue_read + i) % VOODOO_QUEUE_SIZE];
        G_SERIALIZE(serializer, entry.address);
        G_SERIALIZE(serializer, entry.data);
    }
}

void Voodoo::SanitizeState()
{
    m_state.queue_read %= VOODOO_QUEUE_SIZE;
    m_state.front_buffer = (u8)(m_state.front_buffer % 3);
    m_state.back_buffer = (u8)(m_state.back_buffer % 3);
    m_state.swaps_pending = MIN(m_state.swaps_pending, (u8)7);
    m_state.regs[k_voodoo_fbi_init3] = (m_state.regs[k_voodoo_fbi_init3] & ~k_voodoo_init3_straps) |
        k_voodoo_init3_strap_values;

    for (int i = 0; i < VOODOO_CLUT_SIZE; i++)
        m_state.clut[i] &= 0x00FFFFFF;

    m_pipeline_dirty = true;
    memset(m_dirty_pages, 1, sizeof(m_dirty_pages));
    UpdateFbiClock();
    UpdateClutColors();

    u64 start = m_state.frame_start_clocks;
    u32 fraction = m_state.frame_fraction;
    m_video_timing.running = false;
    UpdateVideo(start);
    m_state.frame_start_clocks = start;
    m_state.frame_fraction = fraction;
    ScheduleRetrace();
    UpdateWindow();
}
