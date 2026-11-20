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


#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <iostream>
#include "../common/common.h"

#define KEYBOARD_BUFFER_SIZE 16
#define KEYBOARD_BUFFER_CAPACITY 64
#define KEYBOARD_REPLY_SIZE 4

class StateSerializer;
class TraceLogger;

// MF2 keyboard on the cable of the 8042, which clocks it and owns its scheduler event
class Keyboard
{
public:
    struct Keyboard_State
    {
        u8 buffer[KEYBOARD_BUFFER_CAPACITY];
        u8 buffer_read;
        u8 buffer_count;
        bool overrun;
        u8 reply[KEYBOARD_REPLY_SIZE];
        u8 reply_count;
        u8 last_byte;
        u8 command;
        u8 received;
        bool clock_enabled;
        bool scanning;
        u8 scan_set;
        u8 leds;
        bool num_lock;
        u8 typematic;
        u8 keys[GDOX_KEY_COUNT];
        u8 repeat_key;
        u64 repeat_clocks;
        u64 receive_clocks;
        u64 send_clocks;
        u64 self_test_clocks;
    };

public:
    Keyboard();
    ~Keyboard();
    void Init();
    void SetTraceLogger(TraceLogger* trace_logger);
    void Reset(u64 clocks);
    u64 GetNextEventClocks() const;
    bool HandleEvent(u64 clocks, u8& value);
    void SetClock(bool enabled, u64 clocks);
    void Receive(u8 value, u64 clocks);
    bool IsReceiving() const;
    u64 GetReceiveClocks() const;
    bool KeyPressed(GDOX_Keys key, u64 clocks);
    void KeyReleased(GDOX_Keys key, u64 clocks);
    void ReleaseAllKeys(u64 clocks);
    bool IsKeyPressed(GDOX_Keys key) const;
    u32 GetTypematicDelay() const;
    u32 GetTypematicPeriod() const;
    Keyboard_State* GetState();
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream, int version = GDOX_SAVESTATE_VERSION);

private:
    void ProcessByte(u8 value, u64 clocks);
    void ProcessCommand(u8 value, u64 clocks);
    void SetDefaults();
    void ClearBuffer();
    void QueueReply(u8 value);
    void QueueByte(u8 value);
    void QueueCode(u8 code, bool extended, bool make);
    void QueueKey(GDOX_Keys key, u8 flags, bool make);
    void QueueMake(GDOX_Keys key, u8 flags);
    void QueueBreak(GDOX_Keys key, u8 flags);
    void QueuePause();
    void QueueOverrun();
    void RepeatKey(u64 clocks);
    void UpdateSend(u64 clocks);
    u8 GetMakeFlags(GDOX_Keys key) const;
    bool IsValidKey(GDOX_Keys key) const;
    bool IsNavigationKey(GDOX_Keys key) const;
    bool IsHeld(GDOX_Keys left, GDOX_Keys right) const;
    u8 PopByte();
    NO_INLINE void TraceKey(u8 kind, GDOX_Keys key, u8 count);
    NO_INLINE void TraceReject(GDOX_Keys key, u8 reason);
    NO_INLINE void TraceCommand(u8 value, u8 command);
    void Serialize(StateSerializer& serializer);
    void SanitizeState();

private:
    Keyboard_State m_state;
    TraceLogger* m_trace_logger;
};

static const u64 k_keyboard_byte_clocks = ((u64)GDOX_MACHINE_CLOCK_RATE * 11) / 12000;
static const u64 k_keyboard_self_test_clocks = ((u64)GDOX_MACHINE_CLOCK_RATE * 3) / 10;
static const u64 k_keyboard_power_on_clocks = ((u64)GDOX_MACHINE_CLOCK_RATE * 45) / 100;
static const u8 k_keyboard_typematic_default = 0x2B;
static const u8 k_keyboard_ack = 0xFA;
static const u8 k_keyboard_resend = 0xFE;
static const u8 k_keyboard_self_test_passed = 0xAA;
static const u8 k_keyboard_held = 0x01;
static const u8 k_keyboard_fake_shift = 0x02;
static const u8 k_keyboard_left_shift_up = 0x04;
static const u8 k_keyboard_right_shift_up = 0x08;
static const u8 k_keyboard_sysrq = 0x10;

#include "keyboard_inline.h"

#endif /* KEYBOARD_H */
