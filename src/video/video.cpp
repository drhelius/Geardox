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
#include "../system/scheduler.h"
#include "../common/state_serializer.h"

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
    m_frame_width = GDOX_FRAME_BUFFER_WIDTH;
    m_frame_height = GDOX_FRAME_BUFFER_HEIGHT;
    m_display_width = GDOX_FRAME_BUFFER_WIDTH;
    m_display_height = GDOX_FRAME_BUFFER_HEIGHT;
    m_native_resolution = true;
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

void Video::Reset()
{
    memset(&m_state, 0, sizeof(m_state));
    m_frame_ready = false;
    m_frame_width = GDOX_FRAME_BUFFER_WIDTH;
    m_frame_height = GDOX_FRAME_BUFFER_HEIGHT;
    m_display_width = GDOX_FRAME_BUFFER_WIDTH;
    m_display_height = GDOX_FRAME_BUFFER_HEIGHT;
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

void Video::RunNextEvent()
{
    CompleteFrame(m_next_event_clocks);
}

void Video::StartFrame(u64 clocks)
{
    m_state.frame_start_clocks = clocks;
    UpdateNextEvent();
}

void Video::CompleteFrame(u64 clocks)
{
    if (m_state.running)
        m_frame_ready = true;

    m_state.frame_count++;
    StartFrame(clocks);
}

void Video::UpdateNextEvent()
{
    m_next_event_clocks = GDOX_NO_EVENT;
    m_scheduler->Schedule(SCHEDULER_EVENT_VIDEO, m_next_event_clocks);
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
    G_SERIALIZE(serializer, m_state.running);
    G_SERIALIZE(serializer, m_state.frame_start_clocks);
    G_SERIALIZE(serializer, m_state.frame_count);
}

void Video::SanitizeState()
{
    UpdateNextEvent();
}
