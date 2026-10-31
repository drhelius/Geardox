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

class Memory;
class PIC;
class TraceLogger;
class PIT;
class Scheduler;
class StateSerializer;

class Video
{
public:
    struct Video_State
    {
        bool running;
        u64 frame_start_clocks;
        u32 frame_count;
    };

public:
    Video();
    ~Video();
    void Init(PIC* pic, PIT* pit, Scheduler* scheduler, Memory* memory);
    void SetTraceLogger(TraceLogger* trace_logger);
    void Reset();
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
    Video_State* GetState();
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream, int version = GDOX_SAVESTATE_VERSION);

private:
    void RunNextEvent();
    void StartFrame(u64 clocks);
    void CompleteFrame(u64 clocks);
    void UpdateNextEvent();
    u32 ToXRGB(u8 red, u8 green, u8 blue) const;
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
    int m_frame_width;
    int m_frame_height;
    int m_display_width;
    int m_display_height;
    bool m_native_resolution;
};

#include "video_inline.h"

#endif /* VIDEO_H */
