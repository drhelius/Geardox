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
#include "video_color.h"
#include "voodoo_regs.h"
#if defined(GDOX_ENABLE_VOODOO_THREAD) && defined(__SWITCH__)
#include <switch.h>
#endif

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

static INLINE u32 make_color(s32 a, s32 r, s32 g, s32 b)
{
    return pack_argb((u32)CLAMP(a, 0, 255), (u32)CLAMP(r, 0, 255), (u32)CLAMP(g, 0, 255), (u32)CLAMP(b, 0, 255));
}

static INLINE u8 dither_rb(u32 value, u32 dither)
{
    return (u8)(((value << 1) - (value >> 4) + (value >> 7) + dither) >> 4);
}

static INLINE u8 dither_g(u32 value, u32 dither)
{
    return (u8)(((value << 2) - (value >> 4) + (value >> 6) + dither) >> 4);
}

static INLINE u32 combine_source(u32 select, u32 iterated, u32 texel, u32 color1)
{
    return select == 0 ? iterated : select == 1 ? texel : select == 2 ? color1 : 0;
}

static INLINE s32 combine_channel(const VoodooRaster::VoodooRaster_Combine& c, s32 other, s32 local, s32 local_alpha,
    s32 other_alpha, s32 texel_alpha)
{
    s32 color = c.zero_other ? 0 : other;

    if (c.sub_local)
        color -= local;

    s32 factor = c.factor == 1 ? local : c.factor == 2 ? other_alpha : c.factor == 3 ? local_alpha :
        c.factor == 4 ? texel_alpha : 0;
    color = (color * ((factor ^ c.factor_invert) + 1)) >> 8;
    color += c.add == 1 ? local : c.add == 2 ? local_alpha : 0;
    return CLAMP(color, 0, 255) ^ c.invert;
}

static INLINE void decode_combine(u32 field, bool alpha, VoodooRaster::VoodooRaster_Combine* c)
{
    u32 add = (field >> 6) & 0x03;
    c->zero_other = (field & 0x01) != 0;
    c->sub_local = (field & 0x02) != 0;
    c->factor = (u8)((field >> 2) & 0x07);
    c->factor = c->factor > 4 ? 0 : c->factor;
    c->factor_invert = (field & 0x20) != 0 ? 0 : 0xFF;
    c->add = alpha ? (add != 0 ? 1 : 0) : (add == 3 ? 0 : (u8)add);
    c->invert = (field & 0x100) != 0 ? 0xFF : 0;
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

INLINE bool VoodooRaster::OwnsLine(s32 y) const
{
    return ((u32)y & m_line_mask) == m_line_owner;
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
    memset(&m_decoded, 0, sizeof(m_decoded));
    m_threaded = false;
    m_line_mask = 0;
    m_line_owner = 0;
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
    m_helper_read = 0;
    m_workers_sleeping = 0;
    m_producer_waiting = false;
    m_stop = false;
    m_lfb_count = 0;
    m_lfb_buffer = 0;
    m_lfb_x = 0;
    m_lfb_y = 0;
    m_lfb_present = 0;
    memset(m_lfb_colors, 0, sizeof(m_lfb_colors));
    memset(m_lfb_depths, 0, sizeof(m_lfb_depths));
    InitPointer(m_helper);
    InitPointer(m_helper_texture);
    InitPointer(m_helper_frame_pages);
    memset(m_helper_counters, 0, sizeof(m_helper_counters));
    memset(m_helper_texture_pages, 0, sizeof(m_helper_texture_pages));
    m_marked = false;
#if defined(__SWITCH__)
    m_host_core = 0;
#endif
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
                m_dither[0][y][x][0][value] = (u8)(value >> 3);
                m_dither[0][y][x][1][value] = (u8)(value >> 2);
                m_dither[1][y][x][0][value] = dither_rb(value, k_voodoo_dither_4x4[y * 4 + x]);
                m_dither[1][y][x][1][value] = dither_g(value, k_voodoo_dither_4x4[y * 4 + x]);
                m_dither[2][y][x][0][value] = dither_rb(value, k_voodoo_dither_2x2[y * 4 + x]);
                m_dither[2][y][x][1][value] = dither_g(value, k_voodoo_dither_2x2[y * 4 + x]);
            }
        }
    }

    m_generic_span = &VoodooRaster::DrawSpan<k_voodoo_mode_runtime, k_voodoo_mode_runtime, k_voodoo_mode_runtime,
        k_voodoo_mode_runtime, k_voodoo_mode_runtime>;
    m_span = m_generic_span;

    for (int i = 0; i < VOODOO_RASTER_SPAN_KEYS; i++)
        m_spans[i] = m_generic_span;

    AddSpan<k_voodoo_texture_off, k_voodoo_combine_decoded, 0, 0, 0>();
    AddSpan<k_voodoo_texture_off, k_voodoo_combine_decoded, 0, 0, 1>();
    AddSpan<k_voodoo_texture_off, k_voodoo_combine_decoded, 0, 1, 0>();
    AddSpan<k_voodoo_texture_off, k_voodoo_combine_decoded, 0, 1, 1>();
    AddSpan<k_voodoo_texture_off, k_voodoo_combine_decoded, 1, 0, 0>();
    AddSpan<k_voodoo_texture_off, k_voodoo_combine_decoded, 1, 0, 1>();
    AddSpan<k_voodoo_texture_off, k_voodoo_combine_decoded, 1, 1, 0>();
    AddSpan<k_voodoo_texture_off, k_voodoo_combine_decoded, 1, 1, 1>();
    AddSpan<k_voodoo_texture_bilinear, k_voodoo_combine_texel, 0, 0, 0>();
    AddSpan<k_voodoo_texture_bilinear, k_voodoo_combine_texel, 0, 1, 0>();
    AddSpan<k_voodoo_texture_bilinear, k_voodoo_combine_texel, 0, 1, 1>();
    AddSpan<k_voodoo_texture_bilinear, k_voodoo_combine_texel, 1, 1, 0>();
    AddSpan<k_voodoo_texture_bilinear, k_voodoo_combine_modulate, 1, 0, 0>();
    AddSpan<k_voodoo_texture_bilinear, k_voodoo_combine_modulate, 1, 0, 1>();
    AddSpan<k_voodoo_texture_bilinear, k_voodoo_combine_modulate, 1, 1, 1>();
    AddSpan<k_voodoo_texture_bilinear, k_voodoo_combine_modulate, 0, 1, 1>();
    AddSpan<k_voodoo_texture_bilinear, k_voodoo_combine_decoded, 0, 0, 0>();
    AddSpan<k_voodoo_texture_bilinear, k_voodoo_combine_decoded, 0, 0, 1>();
    AddSpan<k_voodoo_texture_point, k_voodoo_combine_texel, 1, 0, 0>();
    AddSpan<k_voodoo_texture_point, k_voodoo_combine_texel, 1, 1, 0>();
    AddSpan<k_voodoo_texture_point, k_voodoo_combine_modulate, 1, 0, 0>();
    AddSpan<k_voodoo_texture_point, k_voodoo_combine_modulate, 1, 1, 0>();

    for (u32 i = 0; i < 65536; i++)
    {
        m_rgb565[i] = pack_xrgb(expand_5((i >> 11) & 0x1F), expand_6((i >> 5) & 0x3F), expand_5(i & 0x1F));
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

    FlushLfb();

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
    {
        FlushLfb();
        WaitSpace(VOODOO_RASTER_RING_WORDS);

        if (IsValidPointer(m_helper))
            MergeHelper();
    }
#endif
#if defined(GDOX_VOODOO_CHECK)
    if (IsValidPointer(m_shadow))
        CompareShadow();
#endif
}

void VoodooRaster::Reload()
{
#if defined(GDOX_ENABLE_VOODOO_THREAD)
    if (IsValidPointer(m_helper))
    {
        Sync();
        memcpy(m_helper_texture, m_texture_memory, VOODOO_TREX_MEMORY_SIZE);
        memset(m_helper_counters, 0, sizeof(m_helper_counters));
    }
#endif
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
    {
        Decode(*p, texture);
        return;
    }

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
#else
    Decode(*p, texture);
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
        if ((m_lfb_count > 0) && ((buffer != m_lfb_buffer) || (y != m_lfb_y) || (x != m_lfb_x + (s32)m_lfb_count) ||
            (present != m_lfb_present) || (m_lfb_count == VOODOO_RASTER_LFB_PIXELS)))
            FlushLfb();

        if (m_lfb_count == 0)
        {
            m_lfb_buffer = buffer;
            m_lfb_x = x;
            m_lfb_y = y;
            m_lfb_present = present;
        }

        m_lfb_colors[m_lfb_count] = color;
        m_lfb_depths[m_lfb_count] = depth;
        m_lfb_count++;
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

            Decode(m_job_pipeline, m_job_textured ? &m_job_texture : NULL);
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
            for (u32 i = 0; i < payload[4]; i++)
                RunLfbPixel(m_job_pipeline, payload[0], (s32)payload[1] + (s32)i, (s32)payload[2], payload[5 + i],
                    payload[5 + payload[4] + i], payload[3]);
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
static const int k_voodoo_raster_spins = 2000;

u32* VoodooRaster::BeginJob(u32 type, u32 words)
{
    if ((type != VOODOO_JOB_LFB) && (m_lfb_count > 0))
        FlushLfb();

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

void VoodooRaster::FlushLfb()
{
    u32 count = m_lfb_count;

    if (count == 0)
        return;

    m_lfb_count = 0;

    u32 words = 6 + 2 * count;
    u32* job = BeginJob(VOODOO_JOB_LFB, words);
    job[0] = m_lfb_buffer;
    job[1] = (u32)m_lfb_x;
    job[2] = (u32)m_lfb_y;
    job[3] = m_lfb_present;
    job[4] = count;
    memcpy(job + 5, m_lfb_colors, count * sizeof(u32));
    memcpy(job + 5 + count, m_lfb_depths, count * sizeof(u32));
    EndJob(words);
}

void VoodooRaster::EndJob(u32 words)
{
    m_write += words;
    m_published.store(m_write);

    if (m_workers_sleeping.load() > 0)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_work_condition.notify_all();
    }
}

u64 VoodooRaster::GetReadPosition() const
{
    u64 read = m_read.load();

    if (!IsValidPointer(m_helper))
        return read;

    u64 helper_read = m_helper_read.load();
    return MIN(read, helper_read);
}

void VoodooRaster::WaitSpace(u32 words)
{
    for (int i = 0; i < k_voodoo_raster_spins; i++)
    {
        if (m_write + words - GetReadPosition() <= VOODOO_RASTER_RING_WORDS)
            return;

        std::this_thread::yield();
    }

    std::unique_lock<std::mutex> lock(m_mutex);
    m_producer_waiting.store(true);

    while (m_write + words - GetReadPosition() > VOODOO_RASTER_RING_WORDS)
        m_done_condition.wait(lock);

    m_producer_waiting.store(false);
}

#if defined(__SWITCH__)
static u64 voodoo_raster_core_mask()
{
    u64 mask = 0;

    if (R_FAILED(svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0)))
        return 0;

    return mask;
}

static void voodoo_raster_leave_core(int core, int index)
{
    u64 mask = voodoo_raster_core_mask() & ~(1ULL << core);

    for (int i = 63; i >= 0; i--)
    {
        if ((mask & (1ULL << i)) == 0)
            continue;

        if (index-- > 0)
            continue;

        svcSetThreadCoreMask(CUR_THREAD_HANDLE, i, 1U << i);
        return;
    }
}
#endif

static bool voodoo_raster_helper_available()
{
#if defined(__SWITCH__)
    return __builtin_popcountll(voodoo_raster_core_mask()) >= 3;
#else
    return std::thread::hardware_concurrency() >= 4;
#endif
}

void VoodooRaster::StartWorker()
{
    m_ring = new u32[VOODOO_RASTER_RING_WORDS];
    m_write = 0;
    m_published.store(0);
    m_read.store(0);
    m_helper_read.store(0);
    m_stop.store(false);
    m_threaded = true;
#if defined(__SWITCH__)
    m_host_core = (int)svcGetCurrentProcessorNumber();
#endif

    if (voodoo_raster_helper_available())
        StartHelper();

    m_worker = std::thread(&VoodooRaster::WorkerThread, this, this, &m_read, 0);

    if (IsValidPointer(m_helper))
        m_helper_worker = std::thread(&VoodooRaster::WorkerThread, this, m_helper, &m_helper_read, 1);
}

void VoodooRaster::StopWorker()
{
    Sync();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop.store(true);
        m_work_condition.notify_all();
    }

    if (m_worker.joinable())
        m_worker.join();

    if (m_helper_worker.joinable())
        m_helper_worker.join();

    m_threaded = false;
    StopHelper();
    SafeDeleteArray(m_ring);
}

void VoodooRaster::StartHelper()
{
    u32 pages = (m_frame_buffer_pixels * 2 + 4095) >> 12;
    m_helper = new VoodooRaster();
#if defined(GDOX_VOODOO_CHECK)
    m_helper->m_is_shadow = true;
#endif
    m_helper_texture = new u8[VOODOO_TREX_MEMORY_SIZE];
    m_helper_frame_pages = new u8[pages];
    memcpy(m_helper_texture, m_texture_memory, VOODOO_TREX_MEMORY_SIZE);
    memset(m_helper_frame_pages, 0, pages);
    memset(m_helper_counters, 0, sizeof(m_helper_counters));
    m_helper->Init(m_frame_buffer, m_frame_buffer_pixels, m_helper_texture, m_helper_texture_pages, m_helper_counters,
        m_helper_frame_pages);
    m_helper->m_line_mask = 1;
    m_helper->m_line_owner = 1;
    m_line_mask = 1;
    m_line_owner = 0;
}

void VoodooRaster::StopHelper()
{
    SafeDelete(m_helper);
    SafeDeleteArray(m_helper_texture);
    SafeDeleteArray(m_helper_frame_pages);
    m_line_mask = 0;
    m_line_owner = 0;
}

void VoodooRaster::MergeHelper()
{
    for (int i = 0; i < VOODOO_RASTER_COUNTERS; i++)
    {
        m_counters[i] += m_helper_counters[i];
        m_helper_counters[i] = 0;
    }

    if (!m_helper->m_marked)
        return;

    u32 pages = (m_frame_buffer_pixels * 2 + 4095) >> 12;
    m_helper->m_marked = false;

    for (u32 i = 0; i < pages; i++)
    {
        m_dirty_pages[i] |= m_helper_frame_pages[i];
        m_helper_frame_pages[i] = 0;
    }
}

void VoodooRaster::WorkerThread(VoodooRaster* executor, std::atomic<u64>* position, int index)
{
    u64 read = 0;

#if defined(__SWITCH__)
    voodoo_raster_leave_core(m_host_core, index);
#else
    UNUSED(index);
#endif

    while (true)
    {
        u64 write = m_published.load();

        for (int i = 0; (i < k_voodoo_raster_spins) && (read == write); i++)
        {
            std::this_thread::yield();
            write = m_published.load();
        }

        if (read == write)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_workers_sleeping++;

            while (((write = m_published.load()) == read) && !m_stop.load())
                m_work_condition.wait(lock);

            m_workers_sleeping--;

            if (read == write)
                return;
        }

        while (read != write)
        {
            u32 offset = (u32)(read & (VOODOO_RASTER_RING_WORDS - 1));
            u32 header = m_ring[offset];

            if ((header & 0xFF) != VOODOO_JOB_PAD)
                executor->RunJob(header & 0xFF, &m_ring[offset + 1]);

            read += header >> 8;
            position->store(read);

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
            write_u16_le(m_texture_memory + address, texel);
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
    int kind = (fbz_mode & k_voodoo_fbz_dither) == 0 ? 0 : (fbz_mode & k_voodoo_fbz_dither_2x2) != 0 ? 2 : 1;
    const u8 (*table)[256] = m_dither[kind][y & 3][x & 3];
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

#if defined(GDOX_ENABLE_VOODOO_THREAD)
    m_marked = true;
#endif
}

u32 VoodooRaster::CombineColor(const VoodooRaster_Pipeline& p, u32 iterated, u32 texel, u32 iterz, bool& killed)
{
    const VoodooRaster_Decoded& d = m_decoded;
    u32 other = combine_source(d.other_rgb, iterated, texel, p.color1) & 0x00FFFFFF;

    if (d.chroma && (other == (p.chroma_key & 0x00FFFFFF)))
    {
        m_counters[k_voodoo_counter_chroma_fail]++;
        killed = true;
        return 0;
    }

    other |= combine_source(d.other_alpha, iterated, texel, p.color1) & 0xFF000000;

    if (d.alpha_mask && ((other & 0x01000000) == 0))
    {
        m_counters[k_voodoo_counter_afunc_fail]++;
        killed = true;
        return 0;
    }

    killed = false;

    if (!d.combined)
        return other;

    bool local_color0 = d.local_rgb == 2 ? (texel & 0x80000000) != 0 : d.local_rgb == 1;
    u32 local = (local_color0 ? p.color0 : iterated) & 0x00FFFFFF;

    if (d.local_alpha == 0)
        local |= iterated & 0xFF000000;
    else if (d.local_alpha == 1)
        local |= p.color0 & 0xFF000000;
    else if (d.local_alpha == 2)
        local |= (ClampZ(iterz) >> 8) << 24;

    s32 local_alpha = (s32)color_a(local);
    s32 other_alpha = (s32)color_a(other);
    s32 texel_alpha = (s32)color_a(texel);
    s32 r = combine_channel(d.rgb, (s32)color_r(other), (s32)color_r(local), local_alpha, other_alpha, texel_alpha);
    s32 g = combine_channel(d.rgb, (s32)color_g(other), (s32)color_g(local), local_alpha, other_alpha, texel_alpha);
    s32 b = combine_channel(d.rgb, (s32)color_b(other), (s32)color_b(local), local_alpha, other_alpha, texel_alpha);
    s32 a = combine_channel(d.alpha, other_alpha, local_alpha, local_alpha, other_alpha, texel_alpha);

    return ((u32)a << 24) | ((u32)r << 16) | ((u32)g << 8) | (u32)b;
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

void VoodooRaster::Decode(const VoodooRaster_Pipeline& p, const VoodooTrex::VoodooTrex_Texture* texture)
{
    VoodooRaster_Decoded& d = m_decoded;
    u32 path = p.fbz_color_path;
    u32 fbz_mode = p.fbz_mode;
    u32 fog_mode = p.fog_mode;
    bool fog = (fog_mode & 0x01) != 0;
    bool blend = (p.alpha_mode & k_voodoo_alpha_blend) != 0;
    bool depth = ((fbz_mode & k_voodoo_fbz_depth) != 0) ||
        (((fbz_mode & k_voodoo_fbz_aux_write) != 0) && ((fbz_mode & k_voodoo_fbz_alpha_planes) == 0));
    u32 rgb_field = (path >> 8) & 0x1FF;
    u32 alpha_field = (path >> 17) & 0x1FF;

    d.chroma = (fbz_mode & k_voodoo_fbz_chroma) != 0;
    d.alpha_mask = (fbz_mode & k_voodoo_fbz_alpha_mask) != 0;
    d.combined = (path & 0x03FFFF00) != 0;
    d.wfloat = ((fbz_mode & k_voodoo_fbz_wbuffer) != 0) || (fog && ((fog_mode & 0x38) == 0));
    d.dither = (fbz_mode & k_voodoo_fbz_dither) == 0 ? 0 : (fbz_mode & k_voodoo_fbz_dither_2x2) != 0 ? 2 : 1;
    d.blend_alpha = (((p.alpha_mode >> 8) & 0x0F) == 1) && (((p.alpha_mode >> 12) & 0x0F) == 5);
    d.alpha_planes = (fbz_mode & k_voodoo_fbz_alpha_planes) != 0;
    d.dither_subtract = ((fbz_mode & k_voodoo_fbz_dither_subtract) != 0) && ((fbz_mode & k_voodoo_fbz_dither) != 0);
    d.blend_alpha_source = ((p.alpha_mode >> 16) & 0x0F) == 4 ? 256 : 0;
    d.blend_alpha_dest = ((p.alpha_mode >> 20) & 0x0F) == 4 ? 256 : 0;
    d.dither_matrix = (fbz_mode & k_voodoo_fbz_dither_2x2) != 0 ? k_voodoo_dither_2x2 : k_voodoo_dither_4x4;
    d.other_rgb = (u8)(path & 0x03);
    d.other_alpha = (u8)((path >> 2) & 0x03);
    d.local_rgb = (path & 0x80) != 0 ? 2 : (path & 0x10) != 0 ? 1 : 0;
    d.local_alpha = (u8)((path >> 5) & 0x03);
    decode_combine(rgb_field, false, &d.rgb);
    decode_combine(alpha_field, true, &d.alpha);
    d.alpha_form = alpha_field == 0 ? k_voodoo_alpha_other :
        (alpha_field & 0x13F) == 0x021 && (alpha_field & 0xC0) != 0 ? k_voodoo_alpha_local : k_voodoo_alpha_combined;
    d.iterated_rgb = (d.other_rgb == 0) || (d.combined && (d.local_rgb != 1));
    d.iterated_alpha = (d.other_alpha == 0) || (d.combined && (d.local_alpha == 0)) ||
        (fog && ((fog_mode & 0x38) == 0x08));

    int texture_mode = k_voodoo_texture_off;
    int combine = k_voodoo_combine_decoded;
    bool special = true;

    if (p.texture)
    {
        u32 filter = IsValidPointer(texture) ? texture->texture_mode & 0x06 : 0x02;
        special = IsValidPointer(texture) && !p.send_config && ((filter == 0) || (filter == 0x06));
        texture_mode = filter == 0 ? k_voodoo_texture_point : k_voodoo_texture_bilinear;

        if ((d.other_rgb == 1) && (rgb_field == 0x000))
            combine = k_voodoo_combine_texel;
        else if ((d.other_rgb == 1) && (d.local_rgb == 0) && (rgb_field == 0x024))
            combine = k_voodoo_combine_modulate;
    }

    m_span = special ? m_spans[GetSpanKey(texture_mode, combine, depth, blend, fog)] : m_generic_span;
}

int VoodooRaster::GetSpanKey(int texture_mode, int combine, bool depth, bool blend, bool fog)
{
    return (((texture_mode * 3 + combine) * 2 + (depth ? 1 : 0)) * 2 + (blend ? 1 : 0)) * 2 + (fog ? 1 : 0);
}

template<int texture_mode, int combine, int depth_mode, int blend_mode, int fog_mode>
void VoodooRaster::AddSpan()
{
    m_spans[GetSpanKey(texture_mode, combine, depth_mode != 0, blend_mode != 0, fog_mode != 0)] =
        &VoodooRaster::DrawSpan<texture_mode, combine, depth_mode, blend_mode, fog_mode>;
}

u32 VoodooRaster::BlendAlpha(u32 color, u16 destination, u32 dest_alpha, s32 x, s32 y) const
{
    const VoodooRaster_Decoded& d = m_decoded;
    s32 dr = (s32)expand_5((destination >> 11) & 0x1F);
    s32 dg = (s32)expand_6((destination >> 5) & 0x3F);
    s32 db = (s32)expand_5(destination & 0x1F);
    s32 sa = (s32)color_a(color);
    s32 da = d.alpha_planes ? (s32)(dest_alpha & 0xFF) : 0xFF;
    s32 dither = d.dither_subtract ? (15 - d.dither_matrix[(y & 3) * 4 + (x & 3)]) >> 1 : 0;
    s32 source = sa + 1;
    s32 dest = 0x100 - sa;
    s32 r = ((s32)color_r(color) * source + (dr + dither) * dest) >> 8;
    s32 g = ((s32)color_g(color) * source + (dg + (dither >> 1)) * dest) >> 8;
    s32 b = ((s32)color_b(color) * source + (db + dither) * dest) >> 8;
    s32 alpha = (sa * d.blend_alpha_source + da * d.blend_alpha_dest) >> 8;

    return make_color(alpha, r, g, b);
}

void VoodooRaster::RunTriangle(const VoodooRaster_Pipeline& p, const VoodooTrex::VoodooTrex_Texture* texture,
    const VoodooRaster_Triangle& triangle, const VoodooRaster_Span* spans, int count)
{
#if defined(GDOX_VOODOO_CHECK)
    CheckTriangle(p, texture, triangle, spans, count, m_generic_span, m_span);
#endif

    DrawSpans(p, texture, triangle, spans, count, m_span);
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

template<int texture_mode, int combine, int depth_mode, int blend_mode, int fog_mode>
void VoodooRaster::DrawSpan(const VoodooRaster_Pipeline& p, const VoodooTrex::VoodooTrex_Texture* texture,
    const VoodooRaster_Triangle& triangle, const VoodooRaster_Span& span, u32 stipple)
{
    const VoodooRaster_Decoded& d = m_decoded;
    u32 fbz_mode = p.fbz_mode;
    bool generic = texture_mode == k_voodoo_mode_runtime;
    bool textured = generic ? p.texture : texture_mode != k_voodoo_texture_off;
    bool depth_needed = generic ? true : depth_mode != 0;
    bool blend = generic ? (p.alpha_mode & k_voodoo_alpha_blend) != 0 : blend_mode != 0;
    bool fog = generic ? (p.fog_mode & 0x01) != 0 : fog_mode != 0;
    bool decoded = generic || (combine == k_voodoo_combine_decoded);
    s32 y = span.y;
    s32 x0 = span.x0;
    s32 x1 = span.x1;
    s32 scry = (fbz_mode & k_voodoo_fbz_y_origin) != 0 ? p.y_origin - y : y;

    if (!OwnsLine(scry))
        return;

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
    const u8 (*dither)[2][256] = m_dither[d.dither][scry & 3];
    bool stippled = (fbz_mode & k_voodoo_fbz_stipple) != 0;
    bool wfloat_needed = (depth_needed || fog) && d.wfloat;
    bool iterated_rgb = d.iterated_rgb;
    bool iterated_alpha = d.iterated_alpha;
    u32 chroma_key = p.chroma_key & 0x00FFFFFF;
    u32 pixels_out = 0;
    u32 chroma_fail = 0;
    u32 zfunc_fail = 0;
    u32 afunc_fail = 0;

    for (s32 x = x0; x < x1; x++)
    {
        do
        {
            if (stippled && !StippleTest(fbz_mode, x, scry, stipple))
                break;

            u32 wfloat = 0;
            u32 depth_value = 0;

            if (wfloat_needed)
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
                    zfunc_fail++;
                    break;
                }
            }

            u32 texel = 0;

            if (textured)
            {
                s32 lod = 0;

                if (generic)
                {
                    if (p.send_config)
                        texel = k_voodoo_tmu_config;
                    else
                    {
                        u32 sample = VoodooTrex::FetchTexel(*texture, m_texture_memory, iters, itert, itertw,
                            triangle.lod_base, lod_dither[x & 3], lod);
                        texel = VoodooTrex::Combine(*texture, sample, 0, lod);
                    }
                }
                else
                {
                    u32 sample = VoodooTrex::FetchFiltered<texture_mode == k_voodoo_texture_bilinear>(*texture,
                        m_texture_memory, iters, itert, itertw, triangle.lod_base, lod_dither[x & 3], lod);
                    texel = texture->replace ? sample : VoodooTrex::Combine(*texture, sample, 0, lod);
                }
            }

            u32 iterated = 0;

            if (iterated_rgb)
                iterated = (ClampColor(iter[0]) << 16) | (ClampColor(iter[1]) << 8) | ClampColor(iter[2]);

            if (iterated_alpha)
                iterated |= ClampColor(iter[3]) << 24;

            u32 color;

            if (decoded)
            {
                bool killed = false;
                color = CombineColor(p, iterated, texel, iter[4], killed);

                if (killed)
                    break;
            }
            else
            {
                if (d.chroma && ((texel & 0x00FFFFFF) == chroma_key))
                {
                    chroma_fail++;
                    break;
                }

                s32 other_alpha = (s32)color_a(combine_source(d.other_alpha, iterated, texel, p.color1));

                if (d.alpha_mask && ((other_alpha & 0x01) == 0))
                {
                    afunc_fail++;
                    break;
                }

                s32 alpha = other_alpha;

                if (d.alpha_form != k_voodoo_alpha_other)
                {
                    s32 local_alpha = d.local_alpha == 0 ? (s32)color_a(iterated) : d.local_alpha == 1 ?
                        (s32)color_a(p.color0) : d.local_alpha == 2 ? (s32)(ClampZ(iter[4]) >> 8) : 0;
                    alpha = d.alpha_form == k_voodoo_alpha_local ? local_alpha :
                        combine_channel(d.alpha, other_alpha, local_alpha, local_alpha, other_alpha, (s32)color_a(texel));
                }

                if (combine == k_voodoo_combine_texel)
                    color = ((u32)alpha << 24) | (texel & 0x00FFFFFF);
                else
                    color = ((u32)alpha << 24) | (((color_r(texel) * (color_r(iterated) + 1)) >> 8) << 16) |
                        (((color_g(texel) * (color_g(iterated) + 1)) >> 8) << 8) |
                        ((color_b(texel) * (color_b(iterated) + 1)) >> 8);
            }

            if (((alpha_mode & k_voodoo_alpha_test) != 0) && !Compare(alpha_mode >> 1, color_a(color), alpha_reference))
            {
                afunc_fail++;
                break;
            }

            u32 prefog = color;

            if (fog)
                color = Fog(p, color, wfloat, iter[4], color_a(iterated));

            if (blend)
                color = !generic && d.blend_alpha ? BlendAlpha(color, dest[x], depth[x], x, scry) :
                    Blend(p, color, prefog, dest[x], depth[x], x, scry);

            if ((fbz_mode & k_voodoo_fbz_rgb_write) != 0)
            {
                const u8 (*cell)[256] = dither[x & 3];
                dest[x] = (u16)((cell[0][color_r(color)] << 11) | (cell[1][color_g(color)] << 5) | cell[0][color_b(color)]);
            }

            if ((fbz_mode & k_voodoo_fbz_aux_write) != 0)
                depth[x] = (u16)((fbz_mode & k_voodoo_fbz_alpha_planes) != 0 ? color_a(color) : depth_value);

            pixels_out++;
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

    m_counters[k_voodoo_counter_pixels_out] += pixels_out;
    m_counters[k_voodoo_counter_chroma_fail] += chroma_fail;
    m_counters[k_voodoo_counter_zfunc_fail] += zfunc_fail;
    m_counters[k_voodoo_counter_afunc_fail] += afunc_fail;

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

        if (!OwnsLine(scry))
            continue;

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
    if (!OwnsLine(y))
        return;

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

    if (!OwnsLine(scry))
        return;

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
