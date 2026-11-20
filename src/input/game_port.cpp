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


#include "game_port.h"
#include "input.h"
#include "../common/trace_logger.h"
#include "../common/state_serializer.h"

// FlightStick Pro: the hat and four buttons as one code on bits 7-4, highest priority first
static const u8 k_game_port_flightstick_codes[8] = { 0x00, 0x40, 0x80, 0xC0, 0xE0, 0xD0, 0xB0, 0x70 };

GamePort::GamePort()
{
    InitPointer(m_input);
    m_trace_logger = TraceLogger::GetDisabled();
    Reset();
}

GamePort::~GamePort()
{
}

void GamePort::Init(Input* input)
{
    m_input = input;
    Reset();
}

void GamePort::SetTraceLogger(TraceLogger* trace_logger)
{
    m_trace_logger = IsValidPointer(trace_logger) ? trace_logger : TraceLogger::GetDisabled();
}

// The one-shots are idle, so their bits read 0 until the first fire
void GamePort::Reset()
{
    m_state.fire_clocks = 0;
    m_state.latched = k_game_port_none;

    for (int i = 0; i < GDOX_JOYSTICK_AXIS_COUNT; i++)
        m_state.pulse_end[i] = 0;
}

u8 GamePort::Read(u64 clocks)
{
    if (IsValidPointer(m_input) && m_input->GetJoystickType() == GDOX_JOYSTICK_FLIGHTSTICK)
        m_state.latched = GetFlightStickSwitch(m_state.latched);
    else
        m_state.latched = k_game_port_none;

    return (u8)(GetSwitches(m_state.latched) | GetAxes(clocks));
}

u8 GamePort::Peek(u64 clocks) const
{
    u8 latched = k_game_port_none;

    if (IsValidPointer(m_input) && m_input->GetJoystickType() == GDOX_JOYSTICK_FLIGHTSTICK)
        latched = GetFlightStickSwitch(m_state.latched);

    return (u8)(GetSwitches(latched) | GetAxes(clocks));
}

// Any value fires all four, each pulse set by its resistance now. An unplugged input never times out
void GamePort::Write(u64 clocks)
{
    GDOX_Joystick_Type type = IsValidPointer(m_input) ? m_input->GetJoystickType() : GDOX_JOYSTICK_NONE;

    m_state.fire_clocks = clocks;

    for (int i = 0; i < GDOX_JOYSTICK_AXIS_COUNT; i++)
    {
        m_state.pulse_end[i] = IsAxisConnected(type, i) ? clocks + GetPulseClocks(GetAxisPosition(type, i)) : GDOX_NO_EVENT;
    }

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_INPUT, TRACE_INPUT_JOYSTICK_FIRE)))
    {
        GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_INPUT, TRACE_INPUT_JOYSTICK_FIRE);
        entry->input.device = TRACE_INPUT_DEVICE_JOYSTICK;
        entry->input.count = GDOX_JOYSTICK_AXIS_COUNT;
        entry->input.switches = (u8)GetSwitches(m_state.latched);

        for (int i = 0; i < GDOX_JOYSTICK_AXIS_COUNT; i++)
        {
            u64 pulse = m_state.pulse_end[i] == GDOX_NO_EVENT ? 0 : m_state.pulse_end[i] - clocks;
            entry->input.data[i] = (u8)MIN((pulse * 1000000 / GDOX_MACHINE_CLOCK_RATE) / 5, (u64)255);
            entry->input.positions[i] = IsAxisConnected(type, i) ? GetAxisPosition(type, i) : 0;
        }
    }
}

// The axis bits change when a pulse ends, the switches only between frames
u64 GamePort::GetReadStableUntil(u64 clocks) const
{
    u64 next = GDOX_NO_EVENT;

    for (int i = 0; i < GDOX_JOYSTICK_AXIS_COUNT; i++)
    {
        if (m_state.pulse_end[i] > clocks)
            next = MIN(next, m_state.pulse_end[i]);
    }

    return next;
}

// IBM: 24.2 us plus 0.011 us per ohm, over 0 to 100 kohm
u64 GamePort::GetPulseClocks(s16 axis) const
{
    u64 ohms = ((u64)((s32)axis + 32768) * 100000) / 65535;
    u64 nanoseconds = 24200 + 11 * ohms;

    return (nanoseconds * GDOX_MACHINE_CLOCK_RATE) / 1000000000;
}

u8 GamePort::GetAxes(u64 clocks) const
{
    u8 value = 0;

    for (int i = 0; i < GDOX_JOYSTICK_AXIS_COUNT; i++)
    {
        if (clocks < m_state.pulse_end[i])
            value |= (u8)(1 << i);
    }

    return value;
}

// Bits 7-4 read 0 while a switch is closed
u8 GamePort::GetSwitches(u8 latched) const
{
    if (!IsValidPointer(m_input))
        return 0xF0;

    GDOX_Joystick_Type type = m_input->GetJoystickType();
    u8 buttons = m_input->GetJoystickState().buttons;

    switch (type)
    {
        case GDOX_JOYSTICK_STANDARD:
            return (u8)(0xF0 & ~((buttons & 0x03) << 4));
        case GDOX_JOYSTICK_GAMEPAD:
            return (u8)(0xF0 & ~((buttons & 0x0F) << 4));
        case GDOX_JOYSTICK_FLIGHTSTICK:
            return latched == k_game_port_none ? 0xF0 : k_game_port_flightstick_codes[latched];
        default:
            return 0xF0;
    }
}

// A switch held keeps the port until it's let go, then the highest of the rest takes it
u8 GamePort::GetFlightStickSwitch(u8 latched) const
{
    const GDOX_Joystick_State& state = m_input->GetJoystickState();
    bool pressed[8] =
    {
        (state.hat & GDOX_JOYSTICK_HAT_UP) != 0, (state.hat & GDOX_JOYSTICK_HAT_RIGHT) != 0,
        (state.hat & GDOX_JOYSTICK_HAT_DOWN) != 0, (state.hat & GDOX_JOYSTICK_HAT_LEFT) != 0,
        (state.buttons & GDOX_JOYSTICK_BUTTON_1) != 0, (state.buttons & GDOX_JOYSTICK_BUTTON_2) != 0,
        (state.buttons & GDOX_JOYSTICK_BUTTON_3) != 0, (state.buttons & GDOX_JOYSTICK_BUTTON_4) != 0
    };

    if (latched < 8 && pressed[latched])
        return latched;

    for (u8 i = 0; i < 8; i++)
    {
        if (pressed[i])
            return i;
    }

    return k_game_port_none;
}

// The Gravis pad's three resistances, the FlightStick Pro's three pots with B-X open
bool GamePort::IsAxisConnected(GDOX_Joystick_Type type, int axis) const
{
    switch (type)
    {
        case GDOX_JOYSTICK_STANDARD:
        case GDOX_JOYSTICK_GAMEPAD:
            return axis <= GDOX_JOYSTICK_AXIS_AY;
        case GDOX_JOYSTICK_FLIGHTSTICK:
            return axis != GDOX_JOYSTICK_AXIS_BX;
        default:
            return false;
    }
}

s16 GamePort::GetAxisPosition(GDOX_Joystick_Type type, int axis) const
{
    s16 position = m_input->GetJoystickState().axes[axis];

    if (type != GDOX_JOYSTICK_GAMEPAD)
        return position;

    return position < -16384 ? -32768 : (position > 16384 ? 32767 : 0);
}

void GamePort::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
}

void GamePort::LoadState(std::istream& stream, int version)
{
    UNUSED(version);
    StateSerializer serializer(stream);
    Serialize(serializer);
    SanitizeState();
}

void GamePort::Serialize(StateSerializer& serializer)
{
    G_SERIALIZE(serializer, m_state.fire_clocks);
    G_SERIALIZE_ARRAY(serializer, m_state.pulse_end, GDOX_JOYSTICK_AXIS_COUNT);
    G_SERIALIZE(serializer, m_state.latched);
}

void GamePort::SanitizeState()
{
    if (m_state.latched >= 8)
        m_state.latched = k_game_port_none;
}
