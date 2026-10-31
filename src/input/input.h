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

class Input
{
public:
    struct Input_State
    {
        s32 mouse_x[GDOX_MAX_GAMEPADS];
        s32 mouse_y[GDOX_MAX_GAMEPADS];
        GDOX_GamePad_State gamepads[GDOX_MAX_GAMEPADS];
    };

public:
    Input();
    void Init();
    void SetTraceLogger(TraceLogger* trace_logger);
    void Reset();
    void SetMouseDelta(int port, s32 x, s32 y);
    void ClearMouseInput(int port);
    void SetGamePadState(int port, const GDOX_GamePad_State& state);
    void SetInjectedGamePadState(int port, const GDOX_GamePad_State& state);
    const GDOX_GamePad_State& GetGamePadState(int port) const;
    void SetControllerType(int port, GDOX_Controller_Type type);
    GDOX_Controller_Type GetControllerType(int port) const;
    Input_State* GetState();
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream);

private:
    void UpdateGamePadState(int port);
    void TraceEvent(u8 event, int port, u8 value, u16 previous);
    void Serialize(StateSerializer& serializer);
    void SanitizeState();

private:
    Input_State m_state;
    GDOX_GamePad_State m_physical_gamepads[GDOX_MAX_GAMEPADS];
    GDOX_GamePad_State m_injected_gamepads[GDOX_MAX_GAMEPADS];
    GDOX_Controller_Type m_controller_type[GDOX_MAX_GAMEPADS];
    TraceLogger* m_trace_logger;
};

static const s32 k_input_mouse_limit = 0x3FFFFFFF;

#include "input_inline.h"

#endif /* INPUT_H */
