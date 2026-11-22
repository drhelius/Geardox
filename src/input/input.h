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


#ifndef INPUT_H
#define INPUT_H

#include <iostream>
#include "../common/common.h"

class StateSerializer;
class TraceLogger;

// The host's mouse and joystick, which the mice and the game port take from
class Input
{
public:
    struct Input_State
    {
        s32 mouse_x;
        s32 mouse_y;
        s32 mouse_added_x;
        s32 mouse_added_y;
        s32 mouse_frame_x;
        s32 mouse_frame_y;
        u64 mouse_frame_start;
        u32 mouse_frame_clocks;
        u8 mouse_buttons;
        GDOX_Joystick_State joystick;
    };

public:
    Input();
    void Init();
    void SetTraceLogger(TraceLogger* trace_logger);
    void Reset();
    void SetMouseDelta(s32 x, s32 y);
    void BeginMouseFrame(u64 clocks);
    void TakeMouseDelta(u64 clocks, s32 limit, s32& x, s32& y);
    void ClearMouseInput();
    void SetMouseButtons(u8 buttons);
    void SetInjectedMouseButtons(u8 buttons);
    void SetGamepadMouseButtons(u8 buttons);
    u8 GetMouseButtons() const;
    void SetJoystickState(const GDOX_Joystick_State& state);
    void SetInjectedJoystickState(const GDOX_Joystick_State& state, bool axes);
    const GDOX_Joystick_State& GetJoystickState() const;
    void SetJoystickType(GDOX_Joystick_Type type);
    GDOX_Joystick_Type GetJoystickType() const;
    Input_State* GetState();
    static void ApplyDPad(GDOX_Joystick_State& state, bool up, bool down, bool left, bool right, bool flightstick);
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream);

private:
    s32 TakeMouseAxis(s32& pending, s32 frame, u64 elapsed, s32 limit);
    void UpdateMouseButtons();
    void UpdateJoystickState();
    NO_INLINE void TraceChange(u8 device, u16 buttons, u16 previous);
    void Serialize(StateSerializer& serializer);
    void SanitizeState();

private:
    Input_State m_state;
    u8 m_physical_mouse_buttons;
    u8 m_injected_mouse_buttons;
    u8 m_gamepad_mouse_buttons;
    GDOX_Joystick_State m_physical_joystick;
    GDOX_Joystick_State m_injected_joystick;
    bool m_injected_axes;
    GDOX_Joystick_Type m_joystick_type;
    TraceLogger* m_trace_logger;
};

static const s32 k_input_mouse_limit = 0x3FFFFFFF;
static const u32 k_input_mouse_frame_limit = GDOX_MACHINE_CLOCK_RATE / 10;

#include "input_inline.h"

#endif /* INPUT_H */
