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

#ifndef GAMEPAD_KEYS_H
#define GAMEPAD_KEYS_H

#include "../common/common.h"

class Input;

enum GDOX_Gamepad_Input
{
    GDOX_GAMEPAD_INPUT_A = 0,
    GDOX_GAMEPAD_INPUT_B,
    GDOX_GAMEPAD_INPUT_X,
    GDOX_GAMEPAD_INPUT_Y,
    GDOX_GAMEPAD_INPUT_DPAD_UP,
    GDOX_GAMEPAD_INPUT_DPAD_DOWN,
    GDOX_GAMEPAD_INPUT_DPAD_LEFT,
    GDOX_GAMEPAD_INPUT_DPAD_RIGHT,
    GDOX_GAMEPAD_INPUT_L1,
    GDOX_GAMEPAD_INPUT_R1,
    GDOX_GAMEPAD_INPUT_L2,
    GDOX_GAMEPAD_INPUT_R2,
    GDOX_GAMEPAD_INPUT_L3,
    GDOX_GAMEPAD_INPUT_R3,
    GDOX_GAMEPAD_INPUT_BACK,
    GDOX_GAMEPAD_INPUT_START,
    GDOX_GAMEPAD_INPUT_LEFT_STICK_UP,
    GDOX_GAMEPAD_INPUT_LEFT_STICK_DOWN,
    GDOX_GAMEPAD_INPUT_LEFT_STICK_LEFT,
    GDOX_GAMEPAD_INPUT_LEFT_STICK_RIGHT,
    GDOX_GAMEPAD_INPUT_RIGHT_STICK_UP,
    GDOX_GAMEPAD_INPUT_RIGHT_STICK_DOWN,
    GDOX_GAMEPAD_INPUT_RIGHT_STICK_LEFT,
    GDOX_GAMEPAD_INPUT_RIGHT_STICK_RIGHT,
    GDOX_GAMEPAD_INPUT_COUNT
};

enum GDOX_Gamepad_Axis
{
    GDOX_GAMEPAD_AXIS_LEFT_X = 0,
    GDOX_GAMEPAD_AXIS_LEFT_Y,
    GDOX_GAMEPAD_AXIS_RIGHT_X,
    GDOX_GAMEPAD_AXIS_RIGHT_Y,
    GDOX_GAMEPAD_AXIS_COUNT
};

enum GDOX_Gamepad_Use
{
    GDOX_GAMEPAD_USE_KEYS = 0,
    GDOX_GAMEPAD_USE_JOYSTICK,
    GDOX_GAMEPAD_USE_OFF,
    GDOX_GAMEPAD_USE_COUNT
};

enum GDOX_Gamepad_Mouse
{
    GDOX_GAMEPAD_MOUSE_OFF = 0,
    GDOX_GAMEPAD_MOUSE_LEFT_STICK,
    GDOX_GAMEPAD_MOUSE_RIGHT_STICK,
    GDOX_GAMEPAD_MOUSE_DPAD,
    GDOX_GAMEPAD_MOUSE_COUNT
};

enum GDOX_Gamepad_Target
{
    GDOX_GAMEPAD_TARGET_NONE = 0,
    GDOX_GAMEPAD_TARGET_MOUSE_LEFT = 0x100,
    GDOX_GAMEPAD_TARGET_MOUSE_RIGHT,
    GDOX_GAMEPAD_TARGET_MOUSE_MIDDLE,
    GDOX_GAMEPAD_TARGET_END
};

struct GDOX_Gamepad_State
{
    u32 buttons;
    s16 axes[GDOX_GAMEPAD_AXIS_COUNT];
};

struct GDOX_Gamepad_Mapping
{
    int mouse;
    int mouse_speed;
    int dead_zone;
    int targets[GDOX_GAMEPAD_INPUT_COUNT];
};

struct GDOX_Gamepad_Game
{
    int use;
    int mouse;
    int targets[GDOX_GAMEPAD_INPUT_COUNT];
};

struct GDOX_Gamepad_Info
{
    const char* name;
    const char* id;
};

typedef void (*GDOX_Gamepad_Key_Fn)(void* context, GDOX_Keys key, bool pressed);

const GDOX_Gamepad_Info k_gamepad_inputs[GDOX_GAMEPAD_INPUT_COUNT] =
{
    { "A", "a" },
    { "B", "b" },
    { "X", "x" },
    { "Y", "y" },
    { "D-Pad Up", "dpad_up" },
    { "D-Pad Down", "dpad_down" },
    { "D-Pad Left", "dpad_left" },
    { "D-Pad Right", "dpad_right" },
    { "L1", "l1" },
    { "R1", "r1" },
    { "L2", "l2" },
    { "R2", "r2" },
    { "L3", "l3" },
    { "R3", "r3" },
    { "Back", "back" },
    { "Start", "start" },
    { "Left Stick Up", "left_stick_up" },
    { "Left Stick Down", "left_stick_down" },
    { "Left Stick Left", "left_stick_left" },
    { "Left Stick Right", "left_stick_right" },
    { "Right Stick Up", "right_stick_up" },
    { "Right Stick Down", "right_stick_down" },
    { "Right Stick Left", "right_stick_left" },
    { "Right Stick Right", "right_stick_right" }
};

const GDOX_Gamepad_Info k_gamepad_uses[GDOX_GAMEPAD_USE_COUNT] =
{
    { "Keyboard and Mouse", "keys" },
    { "Joystick", "joystick" },
    { "Off", "off" }
};

const GDOX_Gamepad_Info k_gamepad_mice[GDOX_GAMEPAD_MOUSE_COUNT] =
{
    { "Off", "off" },
    { "Left Stick", "left_stick" },
    { "Right Stick", "right_stick" },
    { "D-Pad", "dpad" }
};

const int k_gamepad_mouse_speed_default = 5;
const int k_gamepad_dead_zone_default = 15;

class GamepadKeys
{
public:
    GamepadKeys();
    void Init(Input* input, GDOX_Gamepad_Key_Fn callback, void* context);
    void Update(int pad, const GDOX_Gamepad_State& state, const GDOX_Gamepad_Mapping& mapping);
    void Release(int pad);
    void ReleaseAll();
    static void GetDefaultMapping(GDOX_Gamepad_Mapping* mapping);
    static void ClearGame(GDOX_Gamepad_Game* game);
    static bool MovesMouse(int mouse, int input);
    static int FindInput(const char* id);
    static int FindUse(const char* id);
    static int FindMouse(const char* id);
    static int FindTarget(const char* name);
    static const char* GetTargetName(int target);
    static const char* GetTargetLabel(int target);

private:
    void SetTarget(int pad, int input, int target);
    void PressTarget(int target);
    void ReleaseTarget(int target);
    void MoveMouse(int pad, const GDOX_Gamepad_State& state, const GDOX_Gamepad_Mapping& mapping);
    void UpdateMouseButtons();

private:
    Input* m_input;
    GDOX_Gamepad_Key_Fn m_callback;
    void* m_context;
    int m_held[GDOX_MAX_GAMEPADS][GDOX_GAMEPAD_INPUT_COUNT];
    u8 m_references[GDOX_KEY_COUNT];
    u8 m_mouse_references[3];
    float m_remainder[GDOX_MAX_GAMEPADS][2];
    int m_dpad_frames[GDOX_MAX_GAMEPADS];
};

#endif /* GAMEPAD_KEYS_H */
