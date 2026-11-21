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

#include "voodoo_raster.h"
#include "voodoo_regs.h"

enum VoodooRaster_Job_Type
{
    VOODOO_JOB_PAD,
    VOODOO_JOB_STATE,
    VOODOO_JOB_TRIANGLE,
    VOODOO_JOB_FAST_FILL,
    VOODOO_JOB_LFB,
    VOODOO_JOB_LFB_PIPELINE,
    VOODOO_JOB_TEXTURE,
    VOODOO_JOB_COUNTERS
};

static const u32 k_voodoo_pipeline_words = (sizeof(VoodooRaster::VoodooRaster_Pipeline) + 3) / 4;
static const u32 k_voodoo_texture_words = (sizeof(VoodooTrex::VoodooTrex_Texture) + 3) / 4;
static const u32 k_voodoo_triangle_words = (sizeof(VoodooRaster::VoodooRaster_Triangle) + 3) / 4;
static const u32 k_voodoo_state_lookup = 0x02;

static INLINE u32 color_a(u32 color)
{
    return color >> 24;
}

static INLINE u32 color_r(u32 color)
{
    return (color >> 16) & 0xFF;
}

static INLINE u32 color_g(u32 color)
{
    return (color >> 8) & 0xFF;
}

static INLINE u32 color_b(u32 color)
{
    return color & 0xFF;
}

static INLINE u32 make_color(s32 a, s32 r, s32 g, s32 b)
{
    return ((u32)CLAMP(a, 0, 255) << 24) | ((u32)CLAMP(r, 0, 255) << 16) | ((u32)CLAMP(g, 0, 255) << 8) |
        (u32)CLAMP(b, 0, 255);
}

static INLINE u8 dither_rb(u32 value, u32 dither)
{
    return (u8)(((value << 1) - (value >> 4) + (value >> 7) + dither) >> 4);
}

static INLINE u8 dither_g(u32 value, u32 dither)
{
    return (u8)(((value << 2) - (value >> 4) + (value >> 6) + dither) >> 4);
}

static INLINE s64 ceil_div(s64 numerator, s64 denominator)
{
    s64 quotient = numerator / denominator;

    if (((numerator % denominator) != 0) && (numerator > 0))
        quotient++;

    return quotient;
}

static INLINE s32 edge_ceil(s32 px, s32 py, s32 qx, s32 qy, s32 yc)
{
    s64 dy = qy - py;
    s64 numerator = (s64)(px - 8) * dy + (s64)(yc - py) * (qx - px);
    return (s32)ceil_div(numerator, dy * 16);
}

VoodooRaster::VoodooRaster()
{
    InitPointer(m_frame_buffer);
    InitPointer(m_texture_memory);
    InitPointer(m_texture_pages);
    InitPointer(m_counters);
    InitPointer(m_dirty_pages);
    InitPointer(m_pipeline);
    InitPointer(m_texture);
    m_frame_buffer_pixels = 0;
    m_threaded = false;
    m_job_textured = false;
    memset(&m_job_pipeline, 0, sizeof(m_job_pipeline));
    memset(&m_job_texture, 0, sizeof(m_job_texture));
    memset(m_job_lookup, 0, sizeof(m_job_lookup));
    memset(m_scratch, 0, sizeof(m_scratch));
#if defined(GDOX_ENABLE_VOODOO_THREAD)
    InitPointer(m_ring);
    m_write = 0;
    m_published = 0;
    m_read = 0;
    m_worker_sleeping = false;
    m_producer_waiting = false;
    m_stop = false;
#endif
#if defined(GDOX_VOODOO_CHECK)
    InitPointer(m_shadow);
    InitPointer(m_shadow_frame_buffer);
    InitPointer(m_shadow_texture);
    m_is_shadow = false;
#endif

    for (int y = 0; y < 4; y++)
    {
        for (int x = 0; x < 4; x++)
        {
            for (u32 value = 0; value < 256; value++)
            {
                m_dither4[y][x][0][value] = dither_rb(value, k_voodoo_dither_4x4[y * 4 + x]);
                m_dither4[y][x][1][value] = dither_g(value, k_voodoo_dither_4x4[y * 4 + x]);
                m_dither2[y][x][0][value] = dither_rb(value, k_voodoo_dither_2x2[y * 4 + x]);
                m_dither2[y][x][1][value] = dither_g(value, k_voodoo_dither_2x2[y * 4 + x]);
            }
        }
    }

    for (u32 value = 0; value < 256; value++)
    {
        m_nodither[0][value] = (u8)(value >> 3);
        m_nodither[1][value] = (u8)(value >> 2);
    }

    for (u32 i = 0; i < 65536; i++)
    {
        u32 r = (i >> 11) & 0x1F;
        u32 g = (i >> 5) & 0x3F;
        u32 b = i & 0x1F;
        m_rgb565[i] = 0xFF000000 | (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
    }
}

VoodooRaster::~VoodooRaster()
{
    SetThreaded(false);
#if defined(GDOX_VOODOO_CHECK)
    SafeDelete(m_shadow);
    SafeDeleteArray(m_shadow_frame_buffer);
    SafeDeleteArray(m_shadow_texture);
#endif
}

void VoodooRaster::Init(u16* frame_buffer, u32 frame_buffer_pixels, u8* texture_memory, u8* texture_pages, u32* counters,
    u8* dirty_pages)
{
    m_frame_buffer = frame_buffer;
    m_frame_buffer_pixels = frame_buffer_pixels;
    m_texture_memory = texture_memory;
    m_texture_pages = texture_pages;
    m_counters = counters;
    m_dirty_pages = dirty_pages;
#if defined(GDOX_VOODOO_CHECK)
    if (m_is_shadow || IsValidPointer(m_shadow))
        return;

    m_shadow = new VoodooRaster();
    m_shadow->m_is_shadow = true;
    m_shadow_frame_buffer = new u16[frame_buffer_pixels];
    m_shadow_texture = new u8[VOODOO_TREX_MEMORY_SIZE];
    m_shadow->Init(m_shadow_frame_buffer, frame_buffer_pixels, m_shadow_texture, m_shadow_texture_pages,
        m_shadow_counters, m_shadow_frame_pages);
    m_shadow->SetThreaded(true);
    Reload();
#endif
}

void VoodooRaster::SetThreaded(bool threaded)
{
#if defined(GDOX_VOODOO_CHECK)
    if (!m_is_shadow)
        return;
#endif
#if defined(GDOX_ENABLE_VOODOO_THREAD)
    if (threaded == m_threaded)
        return;

    if (threaded)
        StartWorker();
    else
        StopWorker();
#else
    UNUSED(threaded);
#endif
}

bool VoodooRaster::IsThreaded() const
{
    return m_threaded;
}

void VoodooRaster::Sync()
{
#if defined(GDOX_ENABLE_VOODOO_THREAD)
    if (m_threaded)
        WaitSpace(VOODOO_RASTER_RING_WORDS);
#endif
#if defined(GDOX_VOODOO_CHECK)
    if (IsValidPointer(m_shadow))
        CompareShadow();
#endif
}

void VoodooRaster::Reload()
{
#if defined(GDOX_VOODOO_CHECK)
    if (!IsValidPointer(m_shadow))
        return;

    m_shadow->Sync();
    memcpy(m_shadow_frame_buffer, m_frame_buffer, m_frame_buffer_pixels * 2);
    memcpy(m_shadow_texture, m_texture_memory, VOODOO_TREX_MEMORY_SIZE);
    memcpy(m_shadow_counters, m_counters, sizeof(m_shadow_counters));
#endif
}

void VoodooRaster::SetState(const VoodooRaster_Pipeline* p, const VoodooTrex::VoodooTrex_Texture* texture)
{
    m_pipeline = p;
    m_texture = texture;
#if defined(GDOX_VOODOO_CHECK)
    if (IsValidPointer(m_shadow))
        m_shadow->SetState(p, texture);
#endif
#if defined(GDOX_ENABLE_VOODOO_THREAD)
    if (!m_threaded)
        return;

    bool lookup = IsValidPointer(texture) && texture->dynamic_lookup;
    u32 words = 2 + k_voodoo_pipeline_words + k_voodoo_texture_words + (lookup ? 256 : 0);
    u32* job = BeginJob(VOODOO_JOB_STATE, words);
    job[0] = (IsValidPointer(texture) ? 0x01 : 0) | (lookup ? k_voodoo_state_lookup : 0);
    memcpy(job + 1, p, sizeof(VoodooRaster_Pipeline));

    if (IsValidPointer(texture))
        memcpy(job + 1 + k_voodoo_pipeline_words, texture, sizeof(VoodooTrex::VoodooTrex_Texture));

    if (lookup)
        memcpy(job + 1 + k_voodoo_pipeline_words + k_voodoo_texture_words, texture->lookup, 256 * 4);

    EndJob(words);
#endif
}

void VoodooRaster::DrawTriangle(const VoodooRaster_Triangle& triangle, const s32* vertex, const VoodooRaster_Span* spans,
    int count)
{
#if defined(GDOX_VOODOO_CHECK)
    if (IsValidPointer(m_shadow))
        m_shadow->DrawTriangle(triangle, vertex, spans, count);
#endif
#if defined(GDOX_ENABLE_VOODOO_THREAD)
    if (m_threaded)
    {
        u32 words = 1 + k_voodoo_triangle_words + 6;
        u32* job = BeginJob(VOODOO_JOB_TRIANGLE, words);
        memcpy(job, &triangle, sizeof(VoodooRaster_Triangle));
        memcpy(job + k_voodoo_triangle_words, vertex, 6 * sizeof(s32));
        EndJob(words);
        return;
    }
#endif
    RunTriangle(*m_pipeline, m_texture, triangle, spans, count);
}

void VoodooRaster::FastFill()
{
#if defined(GDOX_VOODOO_CHECK)
    if (IsValidPointer(m_shadow))
        m_shadow->FastFill();
#endif
#if defined(GDOX_ENABLE_VOODOO_THREAD)
    if (m_threaded)
    {
        BeginJob(VOODOO_JOB_FAST_FILL, 1);
        EndJob(1);
        return;
    }
#endif
    RunFastFill(*m_pipeline);
}

void VoodooRaster::WriteLfbPixel(u32 buffer, s32 x, s32 y, u32 color, u32 depth, u32 present)
{
#if defined(GDOX_VOODOO_CHECK)
    if (IsValidPointer(m_shadow))
        m_shadow->WriteLfbPixel(buffer, x, y, color, depth, present);
#endif
#if defined(GDOX_ENABLE_VOODOO_THREAD)
    if (m_threaded)
    {
        u32* job = BeginJob(VOODOO_JOB_LFB, 7);
        job[0] = buffer;
        job[1] = (u32)x;
        job[2] = (u32)y;
        job[3] = color;
        job[4] = depth;
        job[5] = present;
        EndJob(7);
        return;
    }
#endif
    RunLfbPixel(*m_pipeline, buffer, x, y, color, depth, present);
}

void VoodooRaster::PipelineLfbPixel(u32 buffer, s32 x, s32 y, u32 color, u32 depth, bool visible)
{
#if defined(GDOX_VOODOO_CHECK)
    if (IsValidPointer(m_shadow))
        m_shadow->PipelineLfbPixel(buffer, x, y, color, depth, visible);
#endif
#if defined(GDOX_ENABLE_VOODOO_THREAD)
    if (m_threaded)
    {
        u32* job = BeginJob(VOODOO_JOB_LFB_PIPELINE, 7);
        job[0] = buffer;
        job[1] = (u32)x;
        job[2] = (u32)y;
        job[3] = color;
        job[4] = depth;
        job[5] = visible ? 1 : 0;
        EndJob(7);
        return;
    }
#endif
    RunPipelineLfbPixel(*m_pipeline, buffer, x, y, color, depth, visible);
}

void VoodooRaster::WriteTexture(u32 start, u32 value, u32 count, bool wide)
{
#if defined(GDOX_VOODOO_CHECK)
    if (IsValidPointer(m_shadow))
        m_shadow->WriteTexture(start, value, count, wide);
#endif
#if defined(GDOX_ENABLE_VOODOO_THREAD)
    if (m_threaded)
    {
        u32* job = BeginJob(VOODOO_JOB_TEXTURE, 4);
        job[0] = start;
        job[1] = value;
        job[2] = count | (wide ? 0x100 : 0);
        EndJob(4);
        return;
    }
#endif
    RunTexture(start, value, count, wide);
}

void VoodooRaster::ResetCounters()
{
#if defined(GDOX_VOODOO_CHECK)
    if (IsValidPointer(m_shadow))
        m_shadow->ResetCounters();
#endif
#if defined(GDOX_ENABLE_VOODOO_THREAD)
    if (m_threaded)
    {
        BeginJob(VOODOO_JOB_COUNTERS, 1);
        EndJob(1);
        return;
    }
#endif
    memset(m_counters, 0, VOODOO_RASTER_COUNTERS * sizeof(u32));
}

void VoodooRaster::RunJob(u32 type, const u32* payload)
{
    switch (type)
    {
        case VOODOO_JOB_STATE:
        {
            m_job_textured = (payload[0] & 0x01) != 0;
            memcpy(&m_job_pipeline, payload + 1, sizeof(VoodooRaster_Pipeline));

            if (m_job_textured)
                memcpy(&m_job_texture, payload + 1 + k_voodoo_pipeline_words, sizeof(VoodooTrex::VoodooTrex_Texture));

            if ((payload[0] & k_voodoo_state_lookup) != 0)
            {
                memcpy(m_job_lookup, payload + 1 + k_voodoo_pipeline_words + k_voodoo_texture_words, sizeof(m_job_lookup));
                m_job_texture.lookup = m_job_lookup;
            }
            break;
        }
        case VOODOO_JOB_TRIANGLE:
        {
            VoodooRaster_Triangle triangle;
            s32 vertex[6];
            s64 pixels = 0;
            memcpy(&triangle, payload, sizeof(triangle));
            memcpy(vertex, payload + k_voodoo_triangle_words, sizeof(vertex));
            int count = WalkEdges(vertex, m_job_spans, pixels);
            RunTriangle(m_job_pipeline, m_job_textured ? &m_job_texture : NULL, triangle, m_job_spans, count);
            break;
        }
        case VOODOO_JOB_FAST_FILL:
            RunFastFill(m_job_pipeline);
            break;
        case VOODOO_JOB_LFB:
            RunLfbPixel(m_job_pipeline, payload[0], (s32)payload[1], (s32)payload[2], payload[3], payload[4], payload[5]);
            break;
        case VOODOO_JOB_LFB_PIPELINE:
            RunPipelineLfbPixel(m_job_pipeline, payload[0], (s32)payload[1], (s32)payload[2], payload[3], payload[4],
                payload[5] != 0);
            break;
        case VOODOO_JOB_TEXTURE:
            RunTexture(payload[0], payload[1], payload[2] & 0xFF, (payload[2] & 0x100) != 0);
            break;
        case VOODOO_JOB_COUNTERS:
            memset(m_counters, 0, VOODOO_RASTER_COUNTERS * sizeof(u32));
            break;
        default:
            break;
    }
}

#if defined(GDOX_ENABLE_VOODOO_THREAD)
u32* VoodooRaster::BeginJob(u32 type, u32 words)
{
    u32 offset = (u32)(m_write & (VOODOO_RASTER_RING_WORDS - 1));
    u32 tail = VOODOO_RASTER_RING_WORDS - offset;

    if (tail < words)
    {
        WaitSpace(tail + words);
        m_ring[offset] = (tail << 8) | VOODOO_JOB_PAD;
        m_write += tail;
        offset = 0;
    }
    else
        WaitSpace(words);

    m_ring[offset] = (words << 8) | type;
    return &m_ring[offset + 1];
}

void VoodooRaster::EndJob(u32 words)
{
    m_write += words;
    m_published.store(m_write);

    if (m_worker_sleeping.load())
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_work_condition.notify_one();
    }
}

void VoodooRaster::WaitSpace(u32 words)
{
    if (m_write + words - m_read.load() <= VOODOO_RASTER_RING_WORDS)
        return;

    std::unique_lock<std::mutex> lock(m_mutex);
    m_producer_waiting.store(true);

    while (m_write + words - m_read.load() > VOODOO_RASTER_RING_WORDS)
        m_done_condition.wait(lock);

    m_producer_waiting.store(false);
}

void VoodooRaster::StartWorker()
{
    m_ring = new u32[VOODOO_RASTER_RING_WORDS];
    m_write = 0;
    m_published.store(0);
    m_read.store(0);
    m_stop.store(false);
    m_threaded = true;
    m_worker = std::thread(&VoodooRaster::WorkerThread, this);
}

void VoodooRaster::StopWorker()
{
    Sync();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop.store(true);
        m_work_condition.notify_one();
    }

    if (m_worker.joinable())
        m_worker.join();

    m_threaded = false;
    SafeDeleteArray(m_ring);
}

void VoodooRaster::WorkerThread()
{
    u64 read = 0;

    while (true)
    {
        u64 write = m_published.load();

        if (read == write)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_worker_sleeping.store(true);

            while (((write = m_published.load()) == read) && !m_stop.load())
                m_work_condition.wait(lock);

            m_worker_sleeping.store(false);

            if (read == write)
                return;
        }

        while (read != write)
        {
            u32 offset = (u32)(read & (VOODOO_RASTER_RING_WORDS - 1));
            u32 header = m_ring[offset];

            if ((header & 0xFF) != VOODOO_JOB_PAD)
                RunJob(header & 0xFF, &m_ring[offset + 1]);

            read += header >> 8;
            m_read.store(read);

            if (m_producer_waiting.load())
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_done_condition.notify_one();
            }
        }
    }
}
#endif

int VoodooRaster::WalkEdges(const s32* vertex, VoodooRaster_Span* spans, s64& pixels)
{
    s32 x[3] = { vertex[0], vertex[2], vertex[4] };
    s32 y[3] = { vertex[1], vertex[3], vertex[5] };
    int order[3] = { 0, 1, 2 };

    if (y[order[1]] < y[order[0]])
    {
        int t = order[0];
        order[0] = order[1];
        order[1] = t;
    }

    if (y[order[2]] < y[order[1]])
    {
        int t = order[1];
        order[1] = order[2];
        order[2] = t;
    }

    if (y[order[1]] < y[order[0]])
    {
        int t = order[0];
        order[0] = order[1];
        order[1] = t;
    }

    s32 x0 = x[order[0]];
    s32 y0 = y[order[0]];
    s32 x1 = x[order[1]];
    s32 y1 = y[order[1]];
    s32 x2 = x[order[2]];
    s32 y2 = y[order[2]];

    pixels = 0;

    if (y2 == y0)
        return 0;

    s32 first = (s32)ceil_div(y0 - 8, 16);
    s32 last = (s32)ceil_div(y2 - 8, 16);
    int count = 0;

    for (s32 row = first; (row < last) && (count < VOODOO_RASTER_MAX_SPANS); row++)
    {
        s32 yc = row * 16 + 8;
        s32 major = edge_ceil(x0, y0, x2, y2, yc);
        s32 minor = yc < y1 ? edge_ceil(x0, y0, x1, y1, yc) : edge_ceil(x1, y1, x2, y2, yc);
        s32 left = MIN(major, minor);
        s32 right = MAX(major, minor);

        if (left >= right)
            continue;

        spans[count].y = row;
        spans[count].x0 = left;
        spans[count].x1 = right;
        pixels += right - left;
        count++;
    }

    return count;
}

bool VoodooRaster::LfbVisible(const VoodooRaster_Pipeline& p, s32 x, s32 y, u32& stipple)
{
    u32 fbz_mode = p.fbz_mode;
    s32 scry = (fbz_mode & k_voodoo_fbz_y_origin) != 0 ? p.y_origin - y : y;

    if (((fbz_mode & k_voodoo_fbz_clip) != 0) &&
        ((x < p.clip_left) || (x >= p.clip_right) || (scry < p.clip_top) || (scry >= p.clip_bottom)))
        return false;

    return StippleTest(fbz_mode, x, scry, stipple);
}

void VoodooRaster::RunTexture(u32 start, u32 value, u32 count, bool wide)
{
    for (u32 i = 0; i < count; i++)
    {
        if (wide)
        {
            u32 address = (start + i * 2) & (VOODOO_TREX_MEMORY_SIZE - 2);
            u16 texel = (u16)(value >> (i * 16));
            m_texture_memory[address] = (u8)texel;
            m_texture_memory[address + 1] = (u8)(texel >> 8);
            m_texture_pages[address >> 12] = 1;
        }
        else
        {
            u32 address = (start + i) & (VOODOO_TREX_MEMORY_SIZE - 1);
            m_texture_memory[address] = (u8)(value >> (i * 8));
            m_texture_pages[address >> 12] = 1;
        }
    }
}

u32 VoodooRaster::ClampColor(u32 iter)
{
    u32 value = (iter >> 12) & 0xFFF;

    if (value == 0xFFF)
        return 0;

    if (value == 0x100)
        return 0xFF;

    return value & 0xFF;
}

u32 VoodooRaster::ClampZ(u32 iterz)
{
    u32 value = iterz >> 12;

    if (value == 0xFFFFF)
        return 0;

    if (value == 0x10000)
        return 0xFFFF;

    return value & 0xFFFF;
}

u32 VoodooRaster::GetWFloat(s64 iterw)
{
    if ((iterw < 0) || (iterw >= ((s64)1 << 32)))
        return 0x0000;

    u32 value = (u32)iterw;

    if ((value & 0xFFFF0000) == 0)
        return 0xFFFF;

    u32 exponent = l_zero32(value);
    return ((exponent << 12) | ((value >> (19 - exponent)) ^ 0x1FFF)) + 1;
}

bool VoodooRaster::StippleTest(u32 fbz_mode, s32 x, s32 y, u32& stipple)
{
    if ((fbz_mode & k_voodoo_fbz_stipple_pattern) == 0)
    {
        u32 bit = stipple >> 31;
        stipple = (stipple << 1) | bit;
        return ((fbz_mode & k_voodoo_fbz_stipple) == 0) || (bit != 0);
    }

    if ((fbz_mode & k_voodoo_fbz_stipple) == 0)
        return true;

    return ((stipple >> (((y & 3) << 3) | (~x & 7))) & 1) != 0;
}

bool VoodooRaster::Compare(u32 function, u32 value, u32 reference)
{
    switch (function & 0x07)
    {
        case 0:
            return false;
        case 1:
            return value < reference;
        case 2:
            return value == reference;
        case 3:
            return value <= reference;
        case 4:
            return value > reference;
        case 5:
            return value != reference;
        case 6:
            return value >= reference;
        default:
            return true;
    }
}

u16 VoodooRaster::DitherPixel(u32 fbz_mode, s32 x, s32 y, u32 color) const
{
    if ((fbz_mode & k_voodoo_fbz_dither) == 0)
        return (u16)((m_nodither[0][color_r(color)] << 11) | (m_nodither[1][color_g(color)] << 5) |
            m_nodither[0][color_b(color)]);

    const u8 (*table)[256] = (fbz_mode & k_voodoo_fbz_dither_2x2) != 0 ? m_dither2[y & 3][x & 3] : m_dither4[y & 3][x & 3];
    return (u16)((table[0][color_r(color)] << 11) | (table[1][color_g(color)] << 5) | table[0][color_b(color)]);
}

u16* VoodooRaster::GetRow(u32 base, u32 row_pixels, s32 y, s32& x0, s32& x1)
{
    if ((base == VOODOO_RASTER_NO_BUFFER) || (y < 0))
        return NULL;

    u64 row = (u64)base + (u64)(u32)y * row_pixels;

    if (row >= m_frame_buffer_pixels)
        return NULL;

    x0 = MAX(x0, 0);
    x1 = (s32)MIN((u64)(u32)MAX(x1, 0), m_frame_buffer_pixels - row);
    return m_frame_buffer + row;
}

void VoodooRaster::MarkDirty(const u16* start, s32 count)
{
    if ((count <= 0) || (start < m_frame_buffer) || (start >= m_frame_buffer + m_frame_buffer_pixels))
        return;

    u32 first = (u32)(start - m_frame_buffer) * 2;
    u32 last = first + (u32)count * 2 - 1;

    for (u32 page = first >> 12; page <= (last >> 12); page++)
        m_dirty_pages[page] = 1;
}

u32 VoodooRaster::CombineColor(const VoodooRaster_Pipeline& p, u32 iterated, u32 texel, u32 iterz, bool fast, bool& killed)
{
    u32 path = p.fbz_color_path;
    u32 other;

    switch (path & 0x03)
    {
        case 0:
            other = iterated & 0x00FFFFFF;
            break;
        case 1:
            other = texel & 0x00FFFFFF;
            break;
        case 2:
            other = p.color1 & 0x00FFFFFF;
            break;
        default:
            other = 0;
            break;
    }

    if (((p.fbz_mode & k_voodoo_fbz_chroma) != 0) && (other == (p.chroma_key & 0x00FFFFFF)))
    {
        m_counters[k_voodoo_counter_chroma_fail]++;
        killed = true;
        return 0;
    }

    switch ((path >> 2) & 0x03)
    {
        case 0:
            other |= iterated & 0xFF000000;
            break;
        case 1:
            other |= texel & 0xFF000000;
            break;
        case 2:
            other |= p.color1 & 0xFF000000;
            break;
        default:
            break;
    }

    if (((p.fbz_mode & k_voodoo_fbz_alpha_mask) != 0) && ((other & 0x01000000) == 0))
    {
        m_counters[k_voodoo_counter_afunc_fail]++;
        killed = true;
        return 0;
    }

    killed = false;

    if (fast && ((path & 0x03FFFF00) == 0))
        return other;

    bool local_color0 = (path & 0x80) != 0 ? (texel & 0x80000000) != 0 : (path & 0x10) != 0;
    u32 local = local_color0 ? p.color0 & 0x00FFFFFF : iterated & 0x00FFFFFF;

    switch ((path >> 5) & 0x03)
    {
        case 0:
            local |= iterated & 0xFF000000;
            break;
        case 1:
            local |= p.color0 & 0xFF000000;
            break;
        case 2:
            local |= (ClampZ(iterz) >> 8) << 24;
            break;
        default:
            break;
    }

    s32 local_c[4] = { (s32)color_b(local), (s32)color_g(local), (s32)color_r(local), (s32)color_a(local) };
    s32 other_c[4] = { (s32)color_b(other), (s32)color_g(other), (s32)color_r(other), (s32)color_a(other) };
    s32 texel_alpha = (s32)color_a(texel);
    s32 result[4];

    for (int i = 0; i < 4; i++)
    {
        bool alpha = i == 3;
        u32 shift = alpha ? 9 : 0;
        s32 color = ((path >> (8 + shift)) & 0x01) != 0 ? 0 : other_c[i];

        if (((path >> (9 + shift)) & 0x01) != 0)
            color -= local_c[i];

        s32 factor;

        switch ((path >> (10 + shift)) & 0x07)
        {
            case 1:
                factor = alpha ? local_c[3] : local_c[i];
                break;
            case 2:
                factor = other_c[3];
                break;
            case 3:
                factor = local_c[3];
                break;
            case 4:
                factor = texel_alpha;
                break;
            default:
                factor = 0;
                break;
        }

        if (((path >> (13 + shift)) & 0x01) == 0)
            factor ^= 0xFF;

        color = (color * (factor + 1)) >> 8;

        u32 add = (path >> (14 + shift)) & 0x03;

        if (alpha)
            color += add != 0 ? local_c[3] : 0;
        else if (add == 1)
            color += local_c[i];
        else if (add == 2)
            color += local_c[3];

        color = CLAMP(color, 0, 255);

        if (((path >> (16 + shift)) & 0x01) != 0)
            color ^= 0xFF;

        result[i] = color;
    }

    return ((u32)result[3] << 24) | ((u32)result[2] << 16) | ((u32)result[1] << 8) | (u32)result[0];
}

u32 VoodooRaster::Fog(const VoodooRaster_Pipeline& p, u32 color, u32 wfloat, u32 iterz, u32 iter_alpha) const
{
    u32 mode = p.fog_mode;
    s32 in[3] = { (s32)color_r(color), (s32)color_g(color), (s32)color_b(color) };
    s32 fog[3] = { (s32)color_r(p.fog_color), (s32)color_g(p.fog_color), (s32)color_b(p.fog_color) };
    s32 out[3];

    if ((mode & 0x20) != 0)
    {
        for (int i = 0; i < 3; i++)
            out[i] = ((mode & 0x04) != 0 ? 0 : in[i]) + fog[i];
    }
    else
    {
        s32 blend;

        if ((mode & 0x10) != 0)
            blend = (s32)(ClampZ(iterz) >> 8);
        else if ((mode & 0x08) != 0)
            blend = (s32)iter_alpha;
        else
        {
            s32 depth = (s32)wfloat;

            if ((p.fbz_mode & k_voodoo_fbz_depth_bias) != 0)
                depth = CLAMP(depth + (s32)(s16)p.za_color, 0, 0xFFFF);

            u32 index = (u32)depth >> 10;
            blend = p.fog_blend[index] + (s32)((p.fog_delta[index] * (((u32)depth >> 2) & 0xFF)) >> 10);
        }

        blend++;

        for (int i = 0; i < 3; i++)
        {
            s32 source = (mode & 0x04) != 0 ? 0 : in[i];
            s32 target = (mode & 0x02) != 0 ? 0 : fog[i];
            out[i] = source + (((target - source) * blend) >> 8);
        }
    }

    return make_color((s32)color_a(color), out[0], out[1], out[2]);
}

u32 VoodooRaster::Blend(const VoodooRaster_Pipeline& p, u32 color, u32 prefog, u16 destination, u32 dest_alpha, s32 x,
    s32 y) const
{
    u32 mode = p.alpha_mode;
    u32 dst = m_rgb565[destination];
    s32 sa = (s32)color_a(color);
    s32 da = (p.fbz_mode & k_voodoo_fbz_alpha_planes) != 0 ? (s32)(dest_alpha & 0xFF) : 0xFF;
    s32 src_c[3] = { (s32)color_r(color), (s32)color_g(color), (s32)color_b(color) };
    s32 dst_c[3] = { (s32)color_r(dst), (s32)color_g(dst), (s32)color_b(dst) };
    s32 pre_c[3] = { (s32)color_r(prefog), (s32)color_g(prefog), (s32)color_b(prefog) };

    if (((p.fbz_mode & k_voodoo_fbz_dither_subtract) != 0) && ((p.fbz_mode & k_voodoo_fbz_dither) != 0))
    {
        const u8* matrix = (p.fbz_mode & k_voodoo_fbz_dither_2x2) != 0 ? k_voodoo_dither_2x2 : k_voodoo_dither_4x4;
        s32 dither = (15 - matrix[(y & 3) * 4 + (x & 3)]) >> 1;
        dst_c[0] += dither;
        dst_c[1] += dither >> 1;
        dst_c[2] += dither;
    }

    s32 out[3];
    u32 src_factor = (mode >> 8) & 0x0F;
    u32 dst_factor = (mode >> 12) & 0x0F;

    for (int i = 0; i < 3; i++)
    {
        s32 s;
        s32 d;

        switch (src_factor)
        {
            case 1:
                s = sa + 1;
                break;
            case 2:
                s = dst_c[i] + 1;
                break;
            case 3:
                s = da + 1;
                break;
            case 4:
                s = 256;
                break;
            case 5:
                s = 0x100 - sa;
                break;
            case 6:
                s = 0x100 - dst_c[i];
                break;
            case 7:
                s = 0x100 - da;
                break;
            case 15:
                s = MIN(sa, 0x100 - da) + 1;
                break;
            default:
                s = 0;
                break;
        }

        switch (dst_factor)
        {
            case 1:
                d = sa + 1;
                break;
            case 2:
                d = src_c[i] + 1;
                break;
            case 3:
                d = da + 1;
                break;
            case 4:
                d = 256;
                break;
            case 5:
                d = 0x100 - sa;
                break;
            case 6:
                d = 0x100 - src_c[i];
                break;
            case 7:
                d = 0x100 - da;
                break;
            case 15:
                d = pre_c[i] + 1;
                break;
            default:
                d = 0;
                break;
        }

        out[i] = (src_c[i] * s + dst_c[i] * d) >> 8;
    }

    s32 src_alpha = ((mode >> 16) & 0x0F) == 4 ? 256 : 0;
    s32 dst_alpha = ((mode >> 20) & 0x0F) == 4 ? 256 : 0;
    s32 alpha = (sa * src_alpha + da * dst_alpha) >> 8;

    return make_color(alpha, out[0], out[1], out[2]);
}

void VoodooRaster::RunTriangle(const VoodooRaster_Pipeline& p, const VoodooTrex::VoodooTrex_Texture* texture,
    const VoodooRaster_Triangle& triangle, const VoodooRaster_Span* spans, int count)
{
    static const Span_Function k_spans[17] =
    {
        &VoodooRaster::DrawSpan<0, 0, 0, 0>, &VoodooRaster::DrawSpan<1, 0, 0, 0>,
        &VoodooRaster::DrawSpan<0, 1, 0, 0>, &VoodooRaster::DrawSpan<1, 1, 0, 0>,
        &VoodooRaster::DrawSpan<0, 0, 1, 0>, &VoodooRaster::DrawSpan<1, 0, 1, 0>,
        &VoodooRaster::DrawSpan<0, 1, 1, 0>, &VoodooRaster::DrawSpan<1, 1, 1, 0>,
        &VoodooRaster::DrawSpan<0, 0, 0, 1>, &VoodooRaster::DrawSpan<1, 0, 0, 1>,
        &VoodooRaster::DrawSpan<0, 1, 0, 1>, &VoodooRaster::DrawSpan<1, 1, 0, 1>,
        &VoodooRaster::DrawSpan<0, 0, 1, 1>, &VoodooRaster::DrawSpan<1, 0, 1, 1>,
        &VoodooRaster::DrawSpan<0, 1, 1, 1>, &VoodooRaster::DrawSpan<1, 1, 1, 1>,
        &VoodooRaster::DrawSpan<k_voodoo_mode_runtime, k_voodoo_mode_runtime, k_voodoo_mode_runtime, k_voodoo_mode_runtime>
    };

    u32 fbz_mode = p.fbz_mode;
    bool depth = ((fbz_mode & k_voodoo_fbz_depth) != 0) ||
        (((fbz_mode & k_voodoo_fbz_aux_write) != 0) && ((fbz_mode & k_voodoo_fbz_alpha_planes) == 0));
    int key = (p.texture ? 1 : 0) | (depth ? 2 : 0) | ((p.alpha_mode & k_voodoo_alpha_blend) != 0 ? 4 : 0) |
        ((p.fog_mode & 0x01) != 0 ? 8 : 0);
#if defined(GDOX_VOODOO_CHECK)
    CheckTriangle(p, texture, triangle, spans, count, k_spans[16], k_spans[key]);
#endif

    DrawSpans(p, texture, triangle, spans, count, k_spans[key]);
}

void VoodooRaster::DrawSpans(const VoodooRaster_Pipeline& p, const VoodooTrex::VoodooTrex_Texture* texture,
    const VoodooRaster_Triangle& triangle, const VoodooRaster_Span* spans, int count, Span_Function span)
{
    u32 stipple = triangle.stipple;
    bool rotate = (p.fbz_mode & k_voodoo_fbz_stipple_pattern) == 0;

    for (int i = 0; i < count; i++)
    {
        (this->*span)(p, texture, triangle, spans[i], stipple);

        if (rotate)
        {
            u32 amount = (u32)(spans[i].x1 - spans[i].x0) & 31;

            if (amount != 0)
                stipple = (stipple << amount) | (stipple >> (32 - amount));
        }
    }
}

template<int texture_mode, int depth_mode, int blend_mode, int fog_mode>
void VoodooRaster::DrawSpan(const VoodooRaster_Pipeline& p, const VoodooTrex::VoodooTrex_Texture* texture,
    const VoodooRaster_Triangle& triangle, const VoodooRaster_Span& span, u32 stipple)
{
    u32 fbz_mode = p.fbz_mode;
    bool generic = texture_mode == k_voodoo_mode_runtime;
    bool textured = generic ? p.texture : texture_mode != 0;
    bool depth_needed = generic ? true : depth_mode != 0;
    bool blend = generic ? (p.alpha_mode & k_voodoo_alpha_blend) != 0 : blend_mode != 0;
    bool fog = generic ? (p.fog_mode & 0x01) != 0 : fog_mode != 0;
    s32 y = span.y;
    s32 x0 = span.x0;
    s32 x1 = span.x1;
    s32 scry = (fbz_mode & k_voodoo_fbz_y_origin) != 0 ? p.y_origin - y : y;

    m_counters[k_voodoo_counter_pixels_in] += (u32)(x1 - x0);

    if ((fbz_mode & k_voodoo_fbz_clip) != 0)
    {
        if ((scry < p.clip_top) || (scry >= p.clip_bottom))
            return;

        x0 = MAX(x0, p.clip_left);
        x1 = MIN(x1, p.clip_right);
    }

    u16* dest = GetRow(p.draw_base, p.row_pixels, scry, x0, x1);

    if (!IsValidPointer(dest) || (x0 >= x1))
        return;

    u16* depth = NULL;

    if (p.aux_base != VOODOO_RASTER_NO_BUFFER)
    {
        s32 ax0 = x0;
        s32 ax1 = x1;
        depth = GetRow(p.aux_base, p.row_pixels, scry, ax0, ax1);

        if (IsValidPointer(depth) && (ax1 < x1))
            x1 = ax1;
    }

    if (!IsValidPointer(depth))
    {
        x1 = MIN(x1, x0 + VOODOO_RASTER_SCRATCH);
        depth = m_scratch - x0;
    }

    if (x0 >= x1)
        return;

    if ((fbz_mode & k_voodoo_fbz_stipple_pattern) == 0)
    {
        u32 skip = (u32)(x0 - span.x0) & 31;

        if (skip != 0)
            stipple = (stipple << skip) | (stipple >> (32 - skip));
    }

    s32 dx = x0 - (triangle.ax >> 4);
    s32 dy = y - (triangle.ay >> 4);
    u32 iter[5];

    for (int i = 0; i < 5; i++)
        iter[i] = triangle.start[i] + (u32)dy * triangle.dy[i] + (u32)dx * triangle.dx[i];

    s64 iterw = triangle.start_w + (s64)dy * triangle.dwdy + (s64)dx * triangle.dwdx;
    double iters = 0.0;
    double itert = 0.0;
    double itertw = 0.0;
    double delta_s = (double)triangle.dsdx;
    double delta_t = (double)triangle.dtdx;
    double delta_tw = (double)triangle.dtwdx;

    if (textured)
    {
        iters = (double)(triangle.start_s + (s64)dy * triangle.dsdy + (s64)dx * triangle.dsdx);
        itert = (double)(triangle.start_t + (s64)dy * triangle.dtdy + (s64)dx * triangle.dtdx);
        itertw = (double)(triangle.start_tw + (s64)dy * triangle.dtwdy + (s64)dx * triangle.dtwdx);
    }

    u32 alpha_mode = p.alpha_mode;
    u32 alpha_reference = alpha_mode >> 24;
    const u8* lod_dither = &k_voodoo_dither_4x4[(scry & 3) * 4];

    for (s32 x = x0; x < x1; x++)
    {
        do
        {
            if (!StippleTest(fbz_mode, x, scry, stipple))
                break;

            u32 wfloat = 0;
            u32 depth_value = 0;

            if (depth_needed || fog)
                wfloat = GetWFloat(iterw);

            if (depth_needed)
            {
                depth_value = (fbz_mode & k_voodoo_fbz_wbuffer) != 0 ? wfloat : ClampZ(iter[4]);

                if ((fbz_mode & k_voodoo_fbz_depth_bias) != 0)
                    depth_value = (u32)CLAMP((s32)depth_value + (s32)(s16)p.za_color, 0, 0xFFFF);
            }

            if (depth_needed && ((fbz_mode & k_voodoo_fbz_depth) != 0))
            {
                u32 source = (fbz_mode & k_voodoo_fbz_depth_source) != 0 ? p.za_color & 0xFFFF : depth_value;

                if (!Compare(fbz_mode >> 5, source & 0xFFFF, depth[x]))
                {
                    m_counters[k_voodoo_counter_zfunc_fail]++;
                    break;
                }
            }

            u32 texel = 0;

            if (textured)
            {
                if (p.send_config)
                    texel = k_voodoo_tmu_config;
                else
                {
                    s32 lod = 0;
                    u32 sample = VoodooTrex::FetchTexel(*texture, m_texture_memory, iters, itert, itertw,
                        triangle.lod_base, lod_dither[x & 3], lod);
                    texel = !generic && texture->replace ? sample : VoodooTrex::Combine(*texture, sample, 0, lod);
                }
            }

            u32 iterated = (ClampColor(iter[3]) << 24) | (ClampColor(iter[0]) << 16) | (ClampColor(iter[1]) << 8) |
                ClampColor(iter[2]);
            bool killed = false;
            u32 color = CombineColor(p, iterated, texel, iter[4], !generic, killed);

            if (killed)
                break;

            if (((alpha_mode & k_voodoo_alpha_test) != 0) && !Compare(alpha_mode >> 1, color_a(color), alpha_reference))
            {
                m_counters[k_voodoo_counter_afunc_fail]++;
                break;
            }

            u32 prefog = color;

            if (fog)
                color = Fog(p, color, wfloat, iter[4], color_a(iterated));

            if (blend)
                color = Blend(p, color, prefog, dest[x], depth[x], x, scry);

            if ((fbz_mode & k_voodoo_fbz_rgb_write) != 0)
                dest[x] = DitherPixel(fbz_mode, x, scry, color);

            if ((fbz_mode & k_voodoo_fbz_aux_write) != 0)
                depth[x] = (u16)((fbz_mode & k_voodoo_fbz_alpha_planes) != 0 ? color_a(color) : depth_value);

            m_counters[k_voodoo_counter_pixels_out]++;
        } while (0);

        for (int i = 0; i < 5; i++)
            iter[i] += triangle.dx[i];

        iterw += triangle.dwdx;

        if (textured)
        {
            iters += delta_s;
            itert += delta_t;
            itertw += delta_tw;
        }
    }

    if ((fbz_mode & k_voodoo_fbz_rgb_write) != 0)
        MarkDirty(dest + x0, x1 - x0);

    if (((fbz_mode & k_voodoo_fbz_aux_write) != 0) && (depth + x0 != m_scratch))
        MarkDirty(depth + x0, x1 - x0);
}

void VoodooRaster::RunFastFill(const VoodooRaster_Pipeline& p)
{
    u32 fbz_mode = p.fbz_mode;
    u16 depth_value = (u16)p.za_color;

    for (s32 y = p.clip_top; y < p.clip_bottom; y++)
    {
        s32 scry = (fbz_mode & k_voodoo_fbz_y_origin) != 0 ? p.y_origin - y : y;

        if ((fbz_mode & k_voodoo_fbz_rgb_write) != 0)
        {
            s32 x0 = p.clip_left;
            s32 x1 = p.clip_right;
            u16* dest = GetRow(p.draw_base, p.row_pixels, scry, x0, x1);

            if (IsValidPointer(dest) && (x0 < x1))
            {
                u16 pattern[4];

                for (int i = 0; i < 4; i++)
                    pattern[i] = DitherPixel(fbz_mode, i, scry, p.color1);

                for (s32 x = x0; x < x1; x++)
                    dest[x] = pattern[x & 3];

                MarkDirty(dest + x0, x1 - x0);
                m_counters[k_voodoo_counter_pixels_out] += (u32)(x1 - x0);
            }
        }

        if ((fbz_mode & k_voodoo_fbz_aux_write) != 0)
        {
            s32 x0 = p.clip_left;
            s32 x1 = p.clip_right;
            u16* depth = GetRow(p.aux_base, p.row_pixels, scry, x0, x1);

            if (IsValidPointer(depth) && (x0 < x1))
            {
                for (s32 x = x0; x < x1; x++)
                    depth[x] = depth_value;

                MarkDirty(depth + x0, x1 - x0);
            }
        }
    }
}

void VoodooRaster::RunLfbPixel(const VoodooRaster_Pipeline& p, u32 buffer, s32 x, s32 y, u32 color, u32 depth,
    u32 present)
{
    s32 x0 = x;
    s32 x1 = x + 1;
    u16* dest = GetRow(buffer, p.row_pixels, y, x0, x1);

    if (!IsValidPointer(dest) || (x0 >= x1))
        return;

    if ((present & 0x01) != 0)
    {
        dest[x] = DitherPixel(p.fbz_mode, x, y, color);
        MarkDirty(dest + x, 1);
    }

    x0 = x;
    x1 = x + 1;
    u16* aux = GetRow(p.aux_base, p.row_pixels, y, x0, x1);

    if (IsValidPointer(aux) && (x0 < x1))
    {
        if ((p.fbz_mode & k_voodoo_fbz_alpha_planes) != 0)
        {
            if ((present & 0x02) != 0)
            {
                aux[x] = (u16)color_a(color);
                MarkDirty(aux + x, 1);
            }
        }
        else if ((present & 0x04) != 0)
        {
            aux[x] = (u16)depth;
            MarkDirty(aux + x, 1);
        }
    }

    m_counters[k_voodoo_counter_pixels_out]++;
}

void VoodooRaster::RunPipelineLfbPixel(const VoodooRaster_Pipeline& p, u32 buffer, s32 x, s32 y, u32 color, u32 depth,
    bool visible)
{
    u32 fbz_mode = p.fbz_mode;
    s32 scry = (fbz_mode & k_voodoo_fbz_y_origin) != 0 ? p.y_origin - y : y;

    m_counters[k_voodoo_counter_pixels_in]++;

    if (!visible)
        return;

    s32 x0 = x;
    s32 x1 = x + 1;
    u16* dest = GetRow(buffer, p.row_pixels, scry, x0, x1);

    if (!IsValidPointer(dest) || (x0 >= x1))
        return;

    x0 = x;
    x1 = x + 1;
    u16* aux = GetRow(p.aux_base, p.row_pixels, scry, x0, x1);
    u16 dummy = 0;
    u16* depth_pixel = IsValidPointer(aux) && (x0 < x1) ? &aux[x] : &dummy;
    u32 depth_value = depth & 0xFFFF;

    if ((fbz_mode & k_voodoo_fbz_depth) != 0)
    {
        u32 source = (fbz_mode & k_voodoo_fbz_depth_source) != 0 ? p.za_color & 0xFFFF : depth_value;

        if (!Compare(fbz_mode >> 5, source, *depth_pixel))
        {
            m_counters[k_voodoo_counter_zfunc_fail]++;
            return;
        }
    }

    if (((fbz_mode & k_voodoo_fbz_chroma) != 0) && ((color & 0x00FFFFFF) == (p.chroma_key & 0x00FFFFFF)))
    {
        m_counters[k_voodoo_counter_chroma_fail]++;
        return;
    }

    if (((fbz_mode & k_voodoo_fbz_alpha_mask) != 0) && ((color & 0x01000000) == 0))
    {
        m_counters[k_voodoo_counter_afunc_fail]++;
        return;
    }

    u32 alpha_mode = p.alpha_mode;

    if (((alpha_mode & k_voodoo_alpha_test) != 0) && !Compare(alpha_mode >> 1, color_a(color), alpha_mode >> 24))
    {
        m_counters[k_voodoo_counter_afunc_fail]++;
        return;
    }

    u32 prefog = color;

    if ((p.fog_mode & 0x01) != 0)
    {
        u32 w = (p.lfb_mode & k_voodoo_lfb_w_select) != 0 ? p.za_color & 0xFFFF : depth_value;
        color = Fog(p, color, GetWFloat((s64)w << 16), depth_value << 12, color_a(color));
    }

    if ((alpha_mode & k_voodoo_alpha_blend) != 0)
        color = Blend(p, color, prefog, dest[x], *depth_pixel, x, scry);

    if ((fbz_mode & k_voodoo_fbz_rgb_write) != 0)
    {
        dest[x] = DitherPixel(fbz_mode, x, scry, color);
        MarkDirty(dest + x, 1);
    }

    if (((fbz_mode & k_voodoo_fbz_aux_write) != 0) && (depth_pixel != &dummy))
    {
        *depth_pixel = (u16)((fbz_mode & k_voodoo_fbz_alpha_planes) != 0 ? color_a(color) : depth_value);
        MarkDirty(depth_pixel, 1);
    }

    m_counters[k_voodoo_counter_pixels_out]++;
}

#if defined(GDOX_VOODOO_CHECK)
void VoodooRaster::CheckTriangle(const VoodooRaster_Pipeline& p, const VoodooTrex::VoodooTrex_Texture* texture,
    const VoodooRaster_Triangle& triangle, const VoodooRaster_Span* spans, int count, Span_Function generic,
    Span_Function specialised)
{
    static u16 before[VOODOO_RASTER_CHECK_PIXELS];
    static u16 reference[VOODOO_RASTER_CHECK_PIXELS];
    u32 counters[VOODOO_RASTER_COUNTERS];
    u32 pixels = MIN(m_frame_buffer_pixels, (u32)VOODOO_RASTER_CHECK_PIXELS);

    if (m_is_shadow || (generic == specialised))
        return;

    memcpy(before, m_frame_buffer, pixels * 2);
    memcpy(counters, m_counters, sizeof(counters));
    DrawSpans(p, texture, triangle, spans, count, generic);
    memcpy(reference, m_frame_buffer, pixels * 2);

    u32 reference_counters[VOODOO_RASTER_COUNTERS];
    memcpy(reference_counters, m_counters, sizeof(reference_counters));
    memcpy(m_frame_buffer, before, pixels * 2);
    memcpy(m_counters, counters, sizeof(counters));
    DrawSpans(p, texture, triangle, spans, count, specialised);

    if ((memcmp(reference, m_frame_buffer, pixels * 2) != 0) ||
        (memcmp(reference_counters, m_counters, sizeof(reference_counters)) != 0))
        Log("Voodoo check: specialised span differs, fbzColorPath %07X fbzMode %06X alphaMode %08X fogMode %02X",
            p.fbz_color_path, p.fbz_mode, p.alpha_mode, p.fog_mode);

    memcpy(m_frame_buffer, before, pixels * 2);
    memcpy(m_counters, counters, sizeof(counters));
}
#endif

#if defined(GDOX_VOODOO_CHECK)
void VoodooRaster::CompareShadow()
{
    m_shadow->Sync();

    if (memcmp(m_shadow_frame_buffer, m_frame_buffer, m_frame_buffer_pixels * 2) != 0)
        Log("Voodoo check: the render thread's frame buffer differs");

    if (memcmp(m_shadow_texture, m_texture_memory, VOODOO_TREX_MEMORY_SIZE) != 0)
        Log("Voodoo check: the render thread's texture memory differs");

    if (memcmp(m_shadow_counters, m_counters, sizeof(m_shadow_counters)) != 0)
        Log("Voodoo check: the render thread's counters differ");

    Reload();
}
#endif
