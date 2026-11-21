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

#ifndef VOODOO_RASTER_H
#define VOODOO_RASTER_H

#if defined(GDOX_ENABLE_VOODOO_THREAD)
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#endif

#include "../common/common.h"
#include "voodoo_trex.h"

#define VOODOO_RASTER_FOG_SIZE 64
#define VOODOO_RASTER_MAX_SPANS 4096
#define VOODOO_RASTER_COUNTERS 5
#define VOODOO_RASTER_NO_BUFFER 0xFFFFFFFF
#define VOODOO_RASTER_SCRATCH 4096
#define VOODOO_RASTER_CHECK_PIXELS 0x100000
#define VOODOO_RASTER_RING_WORDS 0x100000

class VoodooRaster
{
public:
    struct VoodooRaster_Pipeline
    {
        u32 fbz_color_path;
        u32 fog_mode;
        u32 alpha_mode;
        u32 fbz_mode;
        u32 lfb_mode;
        u32 color0;
        u32 color1;
        u32 chroma_key;
        u32 fog_color;
        u32 za_color;
        s32 clip_left;
        s32 clip_right;
        s32 clip_top;
        s32 clip_bottom;
        u32 draw_base;
        u32 aux_base;
        u32 row_pixels;
        s32 y_origin;
        bool texture;
        bool send_config;
        u8 fog_blend[VOODOO_RASTER_FOG_SIZE];
        u8 fog_delta[VOODOO_RASTER_FOG_SIZE];
    };

    struct VoodooRaster_Triangle
    {
        s32 ax;
        s32 ay;
        u32 start[5];
        u32 dx[5];
        u32 dy[5];
        s64 start_w;
        s64 dwdx;
        s64 dwdy;
        s64 start_s;
        s64 start_t;
        s64 start_tw;
        s64 dsdx;
        s64 dtdx;
        s64 dtwdx;
        s64 dsdy;
        s64 dtdy;
        s64 dtwdy;
        s32 lod_base;
        u32 stipple;
    };

    struct VoodooRaster_Span
    {
        s32 y;
        s32 x0;
        s32 x1;
    };

public:
    VoodooRaster();
    ~VoodooRaster();
    void Init(u16* frame_buffer, u32 frame_buffer_pixels, u8* texture_memory, u8* texture_pages, u32* counters,
        u8* dirty_pages);
    void SetThreaded(bool threaded);
    bool IsThreaded() const;
    void Sync();
    void Reload();
    void SetState(const VoodooRaster_Pipeline* p, const VoodooTrex::VoodooTrex_Texture* texture);
    void DrawTriangle(const VoodooRaster_Triangle& triangle, const s32* vertex, const VoodooRaster_Span* spans,
        int count);
    void FastFill();
    void WriteLfbPixel(u32 buffer, s32 x, s32 y, u32 color, u32 depth, u32 present);
    void PipelineLfbPixel(u32 buffer, s32 x, s32 y, u32 color, u32 depth, bool visible);
    void WriteTexture(u32 start, u32 value, u32 count, bool wide);
    void ResetCounters();
    static int WalkEdges(const s32* vertex, VoodooRaster_Span* spans, s64& pixels);
    static bool LfbVisible(const VoodooRaster_Pipeline& p, s32 x, s32 y, u32& stipple);

private:
    typedef void (VoodooRaster::*Span_Function)(const VoodooRaster_Pipeline&, const VoodooTrex::VoodooTrex_Texture*,
        const VoodooRaster_Triangle&, const VoodooRaster_Span&, u32);

    template<int texture_mode, int depth_mode, int blend_mode, int fog_mode>
    void DrawSpan(const VoodooRaster_Pipeline& p, const VoodooTrex::VoodooTrex_Texture* texture,
        const VoodooRaster_Triangle& triangle, const VoodooRaster_Span& span, u32 stipple);
    void DrawSpans(const VoodooRaster_Pipeline& p, const VoodooTrex::VoodooTrex_Texture* texture,
        const VoodooRaster_Triangle& triangle, const VoodooRaster_Span* spans, int count, Span_Function span);
    void RunTriangle(const VoodooRaster_Pipeline& p, const VoodooTrex::VoodooTrex_Texture* texture,
        const VoodooRaster_Triangle& triangle, const VoodooRaster_Span* spans, int count);
    void RunFastFill(const VoodooRaster_Pipeline& p);
    void RunLfbPixel(const VoodooRaster_Pipeline& p, u32 buffer, s32 x, s32 y, u32 color, u32 depth, u32 present);
    void RunPipelineLfbPixel(const VoodooRaster_Pipeline& p, u32 buffer, s32 x, s32 y, u32 color, u32 depth,
        bool visible);
    void RunTexture(u32 start, u32 value, u32 count, bool wide);
    void RunJob(u32 type, const u32* payload);
#if defined(GDOX_ENABLE_VOODOO_THREAD)
    u32* BeginJob(u32 type, u32 words);
    void EndJob(u32 words);
    void WaitSpace(u32 words);
    void StartWorker();
    void StopWorker();
    void WorkerThread();
#endif
#if defined(GDOX_VOODOO_CHECK)
    void CompareShadow();
    void CheckTriangle(const VoodooRaster_Pipeline& p, const VoodooTrex::VoodooTrex_Texture* texture,
        const VoodooRaster_Triangle& triangle, const VoodooRaster_Span* spans, int count, Span_Function generic,
        Span_Function specialised);
#endif
    u16* GetRow(u32 base, u32 row_pixels, s32 y, s32& x0, s32& x1);
    void MarkDirty(const u16* start, s32 count);
    static u32 ClampColor(u32 iter);
    static u32 ClampZ(u32 iterz);
    static u32 GetWFloat(s64 iterw);
    static bool StippleTest(u32 fbz_mode, s32 x, s32 y, u32& stipple);
    static bool Compare(u32 function, u32 value, u32 reference);
    u32 CombineColor(const VoodooRaster_Pipeline& p, u32 iterated, u32 texel, u32 iterz, bool fast, bool& killed);
    u16 DitherPixel(u32 fbz_mode, s32 x, s32 y, u32 color) const;
    u32 Fog(const VoodooRaster_Pipeline& p, u32 color, u32 wfloat, u32 iterz, u32 iter_alpha) const;
    u32 Blend(const VoodooRaster_Pipeline& p, u32 color, u32 prefog, u16 destination, u32 dest_alpha, s32 x, s32 y) const;

private:
    u16* m_frame_buffer;
    u32 m_frame_buffer_pixels;
    u8* m_texture_memory;
    u8* m_texture_pages;
    u32* m_counters;
    u8* m_dirty_pages;
    const VoodooRaster_Pipeline* m_pipeline;
    const VoodooTrex::VoodooTrex_Texture* m_texture;
    bool m_threaded;
    VoodooRaster_Pipeline m_job_pipeline;
    VoodooTrex::VoodooTrex_Texture m_job_texture;
    bool m_job_textured;
    u32 m_job_lookup[256];
    VoodooRaster_Span m_job_spans[VOODOO_RASTER_MAX_SPANS];
#if defined(GDOX_ENABLE_VOODOO_THREAD)
    u32* m_ring;
    u64 m_write;
    std::atomic<u64> m_published;
    std::atomic<u64> m_read;
    std::atomic<bool> m_worker_sleeping;
    std::atomic<bool> m_producer_waiting;
    std::atomic<bool> m_stop;
    std::mutex m_mutex;
    std::condition_variable m_work_condition;
    std::condition_variable m_done_condition;
    std::thread m_worker;
#endif
#if defined(GDOX_VOODOO_CHECK)
    VoodooRaster* m_shadow;
    u16* m_shadow_frame_buffer;
    u8* m_shadow_texture;
    u32 m_shadow_counters[VOODOO_RASTER_COUNTERS];
    u8 m_shadow_frame_pages[VOODOO_TREX_PAGES];
    u8 m_shadow_texture_pages[VOODOO_TREX_PAGES];
    bool m_is_shadow;
#endif
    u8 m_dither4[4][4][2][256];
    u8 m_dither2[4][4][2][256];
    u8 m_nodither[2][256];
    u32 m_rgb565[65536];
    u16 m_scratch[VOODOO_RASTER_SCRATCH];
};

static const u8 k_voodoo_dither_4x4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
static const u8 k_voodoo_dither_2x2[16] = { 8, 10, 8, 10, 11, 9, 11, 9, 8, 10, 8, 10, 11, 9, 11, 9 };
static const u32 k_voodoo_tmu_config = 0x11;
static const int k_voodoo_mode_runtime = 2;
static const int k_voodoo_counter_pixels_in = 0;
static const int k_voodoo_counter_chroma_fail = 1;
static const int k_voodoo_counter_zfunc_fail = 2;
static const int k_voodoo_counter_afunc_fail = 3;
static const int k_voodoo_counter_pixels_out = 4;

#endif /* VOODOO_RASTER_H */
