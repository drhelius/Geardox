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

INLINE void Input::SetMouseDelta(s32 x, s32 y)
{
    m_state.mouse_x = (s32)CLAMP((s64)m_state.mouse_x + x, -k_input_mouse_limit, k_input_mouse_limit);
    m_state.mouse_y = (s32)CLAMP((s64)m_state.mouse_y + y, -k_input_mouse_limit, k_input_mouse_limit);
    m_state.mouse_added_x = (s32)CLAMP((s64)m_state.mouse_added_x + x, -k_input_mouse_limit, k_input_mouse_limit);
    m_state.mouse_added_y = (s32)CLAMP((s64)m_state.mouse_added_y + y, -k_input_mouse_limit, k_input_mouse_limit);
}

// The motion added for a frame is spread over the reports in it, older motion is due at once. Up to limit counts each
// way, the rest stays for the next report
INLINE void Input::TakeMouseDelta(u64 clocks, s32 limit, s32& x, s32& y)
{
    u64 elapsed = clocks > m_state.mouse_frame_start ? clocks - m_state.mouse_frame_start : 0;
    x = TakeMouseAxis(m_state.mouse_x, m_state.mouse_frame_x, elapsed, limit);
    y = TakeMouseAxis(m_state.mouse_y, m_state.mouse_frame_y, elapsed, limit);
}

INLINE s32 Input::TakeMouseAxis(s32& pending, s32 frame, u64 elapsed, s32 limit)
{
    s32 due = pending;

    if (m_state.mouse_frame_clocks != 0 && elapsed < m_state.mouse_frame_clocks)
    {
        s64 later = (s64)frame - (((s64)frame * (s64)elapsed) / (s64)m_state.mouse_frame_clocks);
        s64 now = (s64)pending - later;
        due = pending > 0 ? (s32)CLAMP(now, (s64)0, (s64)pending) : (s32)CLAMP(now, (s64)pending, (s64)0);
    }

    due = CLAMP(due, -limit, limit);
    pending -= due;
    return due;
}

INLINE void Input::SetMouseButtons(u8 buttons)
{
    m_physical_mouse_buttons = buttons;
    UpdateMouseButtons();
}

INLINE void Input::SetInjectedMouseButtons(u8 buttons)
{
    m_injected_mouse_buttons = buttons;
    UpdateMouseButtons();
}

INLINE void Input::SetGamepadMouseButtons(u8 buttons)
{
    m_gamepad_mouse_buttons = buttons;
    UpdateMouseButtons();
}

INLINE u8 Input::GetMouseButtons() const
{
    return m_state.mouse_buttons;
}

INLINE void Input::SetJoystickState(const GDOX_Joystick_State& state)
{
    m_physical_joystick = state;
    UpdateJoystickState();
}

INLINE void Input::SetInjectedJoystickState(const GDOX_Joystick_State& state, bool axes)
{
    m_injected_joystick = state;
    m_injected_axes = axes;
    UpdateJoystickState();
}

INLINE const GDOX_Joystick_State& Input::GetJoystickState() const
{
    return m_state.joystick;
}

INLINE void Input::SetJoystickType(GDOX_Joystick_Type type)
{
    m_joystick_type = type;
}

INLINE GDOX_Joystick_Type Input::GetJoystickType() const
{
    return m_joystick_type;
}

INLINE Input::Input_State* Input::GetState()
{
    return &m_state;
}
