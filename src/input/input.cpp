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


#include "input.h"
#include "../common/trace_logger.h"
#include "../common/state_serializer.h"

Input::Input()
{
    InitPointer(m_trace_logger);

    for (int i = 0; i < GDOX_MAX_GAMEPADS; i++)
        m_controller_type[i] = GDOX_CONTROLLER_GAMEPAD;

    Reset();
}

void Input::Init()
{
    Reset();
}

void Input::SetTraceLogger(TraceLogger* trace_logger)
{
    m_trace_logger = trace_logger;
}

void Input::Reset()
{
    memset(&m_state, 0, sizeof(m_state));
    memset(m_physical_gamepads, 0, sizeof(m_physical_gamepads));
    memset(m_injected_gamepads, 0, sizeof(m_injected_gamepads));
}

void Input::ClearMouseInput(int port)
{
    if (port < 0 || port >= GDOX_MAX_GAMEPADS)
        return;

    m_state.mouse_x[port] = 0;
    m_state.mouse_y[port] = 0;
}

void Input::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
}

void Input::LoadState(std::istream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
    SanitizeState();
}

void Input::Serialize(StateSerializer& serializer)
{
    G_SERIALIZE_ARRAY(serializer, m_state.mouse_x, GDOX_MAX_GAMEPADS);
    G_SERIALIZE_ARRAY(serializer, m_state.mouse_y, GDOX_MAX_GAMEPADS);
    G_SERIALIZE_ARRAY(serializer, m_state.gamepads, GDOX_MAX_GAMEPADS);
}

void Input::SanitizeState()
{
    for (int i = 0; i < GDOX_MAX_GAMEPADS; i++)
    {
        m_state.mouse_x[i] = CLAMP(m_state.mouse_x[i], -k_input_mouse_limit, k_input_mouse_limit);
        m_state.mouse_y[i] = CLAMP(m_state.mouse_y[i], -k_input_mouse_limit, k_input_mouse_limit);
    }
}

void Input::UpdateGamePadState(int port)
{
    if (port < 0 || port >= GDOX_MAX_GAMEPADS)
        return;

    u16 previous = m_state.gamepads[port].buttons;
    m_state.gamepads[port] = m_physical_gamepads[port];
    m_state.gamepads[port].buttons |= m_injected_gamepads[port].buttons;

    if (unlikely(previous != m_state.gamepads[port].buttons && IsValidPointer(m_trace_logger) &&
        m_trace_logger->IsEventEnabled(TRACE_INPUT, TRACE_INPUT_CHANGE)))
        TraceEvent(TRACE_INPUT_CHANGE, port, 0, previous);
}

void Input::TraceEvent(u8 event, int port, u8 value, u16 previous)
{
    GDOX_Trace_Entry entry = {};
    entry.type = TRACE_INPUT;
    entry.event = event;
    entry.input.buttons = m_state.gamepads[port].buttons;
    entry.input.previous = previous;
    entry.input.port = (u8)port;
    entry.input.value = value;
    entry.input.device = (u8)m_controller_type[port];
    m_trace_logger->TraceLog(entry);
}
