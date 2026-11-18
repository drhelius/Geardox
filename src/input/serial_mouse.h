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


#ifndef SERIAL_MOUSE_H
#define SERIAL_MOUSE_H

#include <iostream>
#include "../common/common.h"

#define SERIAL_MOUSE_QUEUE_SIZE 4

class Input;
class StateSerializer;
class TraceLogger;

// Microsoft-compatible two-button mouse, as Logitech's C7-M documents it, on the line of a UART
class SerialMouse
{
public:
    struct SerialMouse_State
    {
        u8 queue[SERIAL_MOUSE_QUEUE_SIZE];
        u8 queue_count;
        u8 buttons;
        bool rts;
        bool ready;
        u64 ident_clocks;
        u64 byte_clocks;
        u64 poll_clocks;
    };

public:
    SerialMouse();
    ~SerialMouse();
    void Init(Input* input);
    void SetTraceLogger(TraceLogger* trace_logger);
    void Reset();
    void SetRTS(bool rts, u64 clocks);
    u64 GetNextEventClocks() const;
    bool HandleEvent(u64 clocks, u8& value);
    SerialMouse_State* GetState();
    void Serialize(StateSerializer& serializer);
    void SanitizeState();

private:
    void Poll(u64 clocks);
    void StartByte(u64 clocks);
    void TraceEvent(const u8* data, u8 count);

private:
    SerialMouse_State m_state;
    Input* m_input;
    TraceLogger* m_trace_logger;
};

static const u8 k_serial_mouse_data_bits = 7;
static const u32 k_serial_mouse_divisor = 96;
static const u64 k_serial_mouse_byte_clocks = ((u64)GDOX_MACHINE_CLOCK_RATE * 10) / 1200;
static const u64 k_serial_mouse_ident_clocks = ((u64)GDOX_MACHINE_CLOCK_RATE * 14) / 1000;

#include "serial_mouse_inline.h"

#endif /* SERIAL_MOUSE_H */
