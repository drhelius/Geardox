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
    m_trace_logger = TraceLogger::GetDisabled();
    m_joystick_type = GDOX_JOYSTICK_GAMEPAD;
    Reset();
}

void Input::Init()
{
    Reset();
}

void Input::SetTraceLogger(TraceLogger* trace_logger)
{
    m_trace_logger = TraceLogger::Resolve(trace_logger);
}

void Input::Reset()
{
    memset(&m_state, 0, sizeof(m_state));
    memset(&m_physical_joystick, 0, sizeof(m_physical_joystick));
    memset(&m_injected_joystick, 0, sizeof(m_injected_joystick));
    m_physical_mouse_buttons = 0;
    m_injected_mouse_buttons = 0;
    m_gamepad_mouse_buttons = 0;
    m_injected_axes = false;
}

void Input::ClearMouseInput()
{
    m_state.mouse_x = 0;
    m_state.mouse_y = 0;
    m_state.mouse_added_x = 0;
    m_state.mouse_added_y = 0;
    m_state.mouse_frame_x = 0;
    m_state.mouse_frame_y = 0;
}

void Input::BeginMouseFrame(u64 clocks)
{
    u64 length = m_state.mouse_frame_start != 0 && clocks > m_state.mouse_frame_start ? clocks - m_state.mouse_frame_start : 0;

    m_state.mouse_frame_clocks = (u32)MIN(length, (u64)k_input_mouse_frame_limit);
    m_state.mouse_frame_start = clocks;
    m_state.mouse_frame_x = m_state.mouse_added_x;
    m_state.mouse_frame_y = m_state.mouse_added_y;
    m_state.mouse_added_x = 0;
    m_state.mouse_added_y = 0;
}

void Input::ApplyDPad(GDOX_Joystick_State& state, bool up, bool down, bool left, bool right, bool flightstick)
{
    if (flightstick)
    {
        state.hat = (u8)((up ? GDOX_JOYSTICK_HAT_UP : 0) | (down ? GDOX_JOYSTICK_HAT_DOWN : 0) |
            (left ? GDOX_JOYSTICK_HAT_LEFT : 0) | (right ? GDOX_JOYSTICK_HAT_RIGHT : 0));
        return;
    }

    if (left != right)
        state.axes[GDOX_JOYSTICK_AXIS_AX] = right ? 32767 : -32768;

    if (up != down)
        state.axes[GDOX_JOYSTICK_AXIS_AY] = down ? 32767 : -32768;
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
    G_SERIALIZE(serializer, m_state.mouse_x);
    G_SERIALIZE(serializer, m_state.mouse_y);
    G_SERIALIZE(serializer, m_state.mouse_added_x);
    G_SERIALIZE(serializer, m_state.mouse_added_y);
    G_SERIALIZE(serializer, m_state.mouse_frame_x);
    G_SERIALIZE(serializer, m_state.mouse_frame_y);
    G_SERIALIZE(serializer, m_state.mouse_frame_start);
    G_SERIALIZE(serializer, m_state.mouse_frame_clocks);
    G_SERIALIZE(serializer, m_state.mouse_buttons);
    G_SERIALIZE_ARRAY(serializer, m_state.joystick.axes, GDOX_JOYSTICK_AXIS_COUNT);
    G_SERIALIZE(serializer, m_state.joystick.buttons);
    G_SERIALIZE(serializer, m_state.joystick.hat);
}

void Input::SanitizeState()
{
    m_state.mouse_x = CLAMP(m_state.mouse_x, -k_input_mouse_limit, k_input_mouse_limit);
    m_state.mouse_y = CLAMP(m_state.mouse_y, -k_input_mouse_limit, k_input_mouse_limit);
    m_state.mouse_frame_x = CLAMP(m_state.mouse_frame_x, -k_input_mouse_limit, k_input_mouse_limit);
    m_state.mouse_frame_y = CLAMP(m_state.mouse_frame_y, -k_input_mouse_limit, k_input_mouse_limit);
    m_state.mouse_added_x = CLAMP(m_state.mouse_added_x, -k_input_mouse_limit, k_input_mouse_limit);
    m_state.mouse_added_y = CLAMP(m_state.mouse_added_y, -k_input_mouse_limit, k_input_mouse_limit);
    m_state.mouse_frame_clocks = MIN(m_state.mouse_frame_clocks, k_input_mouse_frame_limit);
    m_state.mouse_buttons &= GDOX_MOUSE_LEFT | GDOX_MOUSE_RIGHT | GDOX_MOUSE_MIDDLE;
    m_state.joystick.buttons &= 0x0F;
    m_state.joystick.hat &= 0x0F;
}

void Input::UpdateMouseButtons()
{
    u8 previous = m_state.mouse_buttons;
    m_state.mouse_buttons = m_physical_mouse_buttons | m_injected_mouse_buttons | m_gamepad_mouse_buttons;

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_INPUT, TRACE_INPUT_CHANGE)) && previous != m_state.mouse_buttons)
        TraceChange(TRACE_INPUT_DEVICE_MOUSE, m_state.mouse_buttons, previous);
}

// Injected buttons add to the host's, injected axes replace them
void Input::UpdateJoystickState()
{
    u16 previous = (u16)(m_state.joystick.buttons | (m_state.joystick.hat << 4));

    for (int i = 0; i < GDOX_JOYSTICK_AXIS_COUNT; i++)
        m_state.joystick.axes[i] = m_injected_axes ? m_injected_joystick.axes[i] : m_physical_joystick.axes[i];

    m_state.joystick.buttons = (m_physical_joystick.buttons | m_injected_joystick.buttons) & 0x0F;
    m_state.joystick.hat = (m_physical_joystick.hat | m_injected_joystick.hat) & 0x0F;

    u16 current = (u16)(m_state.joystick.buttons | (m_state.joystick.hat << 4));

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_INPUT, TRACE_INPUT_CHANGE)) && previous != current)
        TraceChange(TRACE_INPUT_DEVICE_JOYSTICK, current, previous);
}

void Input::TraceChange(u8 device, u16 buttons, u16 previous)
{
    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_INPUT, TRACE_INPUT_CHANGE);
    entry->input.device = device;
    entry->input.buttons = buttons;
    entry->input.previous = previous;
}
