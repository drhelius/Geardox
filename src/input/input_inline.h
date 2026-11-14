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
}

// Up to limit counts each way, the rest stays for the next report
INLINE void Input::TakeMouseDelta(s32 limit, s32& x, s32& y)
{
    x = CLAMP(m_state.mouse_x, -limit, limit);
    y = CLAMP(m_state.mouse_y, -limit, limit);
    m_state.mouse_x -= x;
    m_state.mouse_y -= y;
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
