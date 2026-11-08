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


#ifndef PS2_MOUSE_H
#define PS2_MOUSE_H

#include <iostream>
#include "../common/common.h"

#define PS2_MOUSE_REPLY_SIZE 8
#define PS2_MOUSE_GROUP_SIZE 4

class Input;
class StateSerializer;
class TraceLogger;

// IBM's two-button mouse on the auxiliary port of the 8042, which clocks it and owns its scheduler event
class PS2Mouse
{
public:
    struct PS2Mouse_State
    {
        u8 reply[PS2_MOUSE_REPLY_SIZE];
        u8 reply_count;
        u8 last[PS2_MOUSE_GROUP_SIZE];
        u8 last_count;
        u8 command;
        u8 received;
        u8 errors;
        bool clock_enabled;
        bool enabled;
        bool remote;
        bool wrap;
        bool wrap_remote;
        bool scaling;
        bool resetting;
        u8 resolution;
        u8 rate;
        u8 buttons;
        s32 x;
        s32 y;
        s32 remainder_x;
        s32 remainder_y;
        u64 receive_clocks;
        u64 send_clocks;
        u64 sample_clocks;
    };

public:
    PS2Mouse();
    ~PS2Mouse();
    void Init(Input* input);
    void SetTraceLogger(TraceLogger* trace_logger);
    void Reset(u64 clocks);
    u64 GetNextEventClocks() const;
    bool HandleEvent(u64 clocks, u8& value);
    void SetClock(bool enabled, u64 clocks);
    void Receive(u8 value, u64 clocks);
    bool IsReceiving() const;
    u64 GetReceiveClocks() const;
    PS2Mouse_State* GetState();
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream, int version = GDOX_SAVESTATE_VERSION);

private:
    void ProcessByte(u8 value, u64 clocks);
    bool ProcessParameter(u8 command, u8 value);
    bool ProcessCommand(u8 value, u64 clocks);
    void SetDefaults();
    void Sample(u64 clocks);
    void ClearCounts();
    void QueueGroup(const u8* bytes, u8 count, bool remember);
    void QueueReply(u8 value);
    void QueuePacket(bool scaled);
    void QueueStatus();
    void Reject();
    s32 Scale(s32 value) const;
    void UpdateSend(u64 clocks);
    u8 PopByte();
    void TraceEvent(u8 event, const u8* data, u8 count);
    void Serialize(StateSerializer& serializer);
    void SanitizeState();

private:
    PS2Mouse_State m_state;
    Input* m_input;
    TraceLogger* m_trace_logger;
};

static const u8 k_ps2_mouse_ack = 0xFA;
static const u8 k_ps2_mouse_resend = 0xFE;
static const u8 k_ps2_mouse_error = 0xFC;
static const u8 k_ps2_mouse_self_test_passed = 0xAA;
static const u8 k_ps2_mouse_id = 0x00;
static const u8 k_ps2_mouse_default_rate = 100;
static const u8 k_ps2_mouse_default_resolution = 2;
static const s32 k_ps2_mouse_count_limit = 0x10000;

#include "ps2_mouse_inline.h"

#endif /* PS2_MOUSE_H */
