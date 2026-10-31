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

INLINE float Video::GetFrameTime() const
{
    return 1000.0f / GDOX_VIDEO_FRAME_RATE;
}

INLINE void Video::EnableNativeResolution(bool enable)
{
    m_native_resolution = enable;
}

INLINE bool Video::IsNativeResolutionEnabled() const
{
    return m_native_resolution;
}

INLINE Video::Video_State* Video::GetState()
{
    return &m_state;
}

INLINE u32 Video::ToXRGB(u8 red, u8 green, u8 blue) const
{
    return 0xFF000000U | ((u32)red << 16) | ((u32)green << 8) | blue;
}

#endif /* VIDEO_INLINE_H */
