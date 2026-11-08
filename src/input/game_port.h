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


#ifndef GAME_PORT_H
#define GAME_PORT_H

#include <iostream>
#include "../common/common.h"

class Input;
class StateSerializer;
class TraceLogger;

// The game port at 201h: four one-shots fired by a write, four switches, and the device the host joystick plays
class GamePort
{
public:
    struct GamePort_State
    {
        u64 fire_clocks;
        u64 pulse_end[GDOX_JOYSTICK_AXIS_COUNT];
        u8 latched;
    };

public:
    GamePort();
    ~GamePort();
    void Init(Input* input);
    void SetTraceLogger(TraceLogger* trace_logger);
    void Reset();
    bool Decodes(u16 port) const;
    u8 Read(u64 clocks);
    u8 Peek(u64 clocks) const;
    void Write(u64 clocks);
    u64 GetReadStableUntil(u64 clocks) const;
    GamePort_State* GetState();
    u64 GetPulseClocks(s16 axis) const;
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream, int version = GDOX_SAVESTATE_VERSION);

private:
    u8 GetAxes(u64 clocks) const;
    u8 GetSwitches(u8 latched) const;
    u8 GetFlightStickSwitch(u8 latched) const;
    bool IsAxisConnected(GDOX_Joystick_Type type, int axis) const;
    s16 GetAxisPosition(GDOX_Joystick_Type type, int axis) const;
    void Serialize(StateSerializer& serializer);
    void SanitizeState();

private:
    GamePort_State m_state;
    Input* m_input;
    TraceLogger* m_trace_logger;
};

static const u8 k_game_port_none = 0xFF;

#include "game_port_inline.h"

#endif /* GAME_PORT_H */
