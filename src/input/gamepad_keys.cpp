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

#include <math.h>
#include "gamepad_keys.h"
#include "input.h"
#include "key_map.h"

static const int k_gamepad_keys_stick_threshold = 16384;
static const float k_gamepad_keys_speed_scale = 8.0f;
static const int k_gamepad_keys_dpad_ramp = 30;

static const GDOX_Gamepad_Info k_gamepad_mouse_targets[3] =
{
    { "Left Click", "mouse_left" },
    { "Right Click", "mouse_right" },
    { "Middle Click", "mouse_middle" }
};

static const int k_gamepad_default_targets[GDOX_GAMEPAD_INPUT_COUNT] =
{
    GDOX_GAMEPAD_TARGET_MOUSE_LEFT, GDOX_GAMEPAD_TARGET_MOUSE_RIGHT, GDOX_KEY_SPACE, GDOX_KEY_LEFT_CTRL,
    GDOX_KEY_UP, GDOX_KEY_DOWN, GDOX_KEY_LEFT, GDOX_KEY_RIGHT,
    GDOX_KEY_LEFT_ALT, GDOX_KEY_LEFT_SHIFT, GDOX_GAMEPAD_TARGET_NONE, GDOX_GAMEPAD_TARGET_NONE,
    GDOX_GAMEPAD_TARGET_NONE, GDOX_GAMEPAD_TARGET_NONE, GDOX_KEY_ESCAPE, GDOX_KEY_RETURN,
    GDOX_GAMEPAD_TARGET_NONE, GDOX_GAMEPAD_TARGET_NONE, GDOX_GAMEPAD_TARGET_NONE, GDOX_GAMEPAD_TARGET_NONE,
    GDOX_GAMEPAD_TARGET_NONE, GDOX_GAMEPAD_TARGET_NONE, GDOX_GAMEPAD_TARGET_NONE, GDOX_GAMEPAD_TARGET_NONE
};

static bool is_valid_target(int target)
{
    return ((target > GDOX_KEY_NONE) && (target < GDOX_KEY_COUNT)) ||
        ((target >= GDOX_GAMEPAD_TARGET_MOUSE_LEFT) && (target < GDOX_GAMEPAD_TARGET_END));
}

static int find_info(const GDOX_Gamepad_Info* infos, int count, const char* id)
{
    for (int i = 0; IsValidPointer(id) && (i < count); i++)
    {
        if (strcmp(id, infos[i].id) == 0)
            return i;
    }

    return -1;
}

GamepadKeys::GamepadKeys()
{
    InitPointer(m_input);
    InitPointer(m_callback);
    InitPointer(m_context);
    memset(m_held, 0, sizeof(m_held));
    memset(m_references, 0, sizeof(m_references));
    memset(m_mouse_references, 0, sizeof(m_mouse_references));
    memset(m_remainder, 0, sizeof(m_remainder));
    memset(m_dpad_frames, 0, sizeof(m_dpad_frames));
}

void GamepadKeys::Init(Input* input, GDOX_Gamepad_Key_Fn callback, void* context)
{
    m_input = input;
    m_callback = callback;
    m_context = context;
    ReleaseAll();
}

void GamepadKeys::Update(int pad, const GDOX_Gamepad_State& state, const GDOX_Gamepad_Mapping& mapping)
{
    if ((pad < 0) || (pad >= GDOX_MAX_GAMEPADS))
        return;

    for (int i = 0; i < GDOX_GAMEPAD_INPUT_COUNT; i++)
    {
        bool down = false;

        if (i < GDOX_GAMEPAD_INPUT_LEFT_STICK_UP)
            down = (state.buttons & (1u << i)) != 0;
        else
        {
            int stick = i - GDOX_GAMEPAD_INPUT_LEFT_STICK_UP;
            int direction = stick & 3;
            int value = state.axes[((stick >> 2) * 2) + (direction < 2 ? 1 : 0)];
            down = (direction & 1) == 0 ? (value < -k_gamepad_keys_stick_threshold) :
                (value > k_gamepad_keys_stick_threshold);
        }

        int target = mapping.targets[i];

        if (!down || MovesMouse(mapping.mouse, i) || !is_valid_target(target))
            target = GDOX_GAMEPAD_TARGET_NONE;

        SetTarget(pad, i, target);
    }

    MoveMouse(pad, state, mapping);
    UpdateMouseButtons();
}

void GamepadKeys::Release(int pad)
{
    if ((pad < 0) || (pad >= GDOX_MAX_GAMEPADS))
        return;

    for (int i = 0; i < GDOX_GAMEPAD_INPUT_COUNT; i++)
        SetTarget(pad, i, GDOX_GAMEPAD_TARGET_NONE);

    m_remainder[pad][0] = 0.0f;
    m_remainder[pad][1] = 0.0f;
    m_dpad_frames[pad] = 0;
    UpdateMouseButtons();
}

void GamepadKeys::ReleaseAll()
{
    for (int i = GDOX_KEY_NONE + 1; i < GDOX_KEY_COUNT; i++)
    {
        if (m_references[i] == 0)
            continue;

        m_references[i] = 0;

        if (IsValidPointer(m_callback))
            m_callback(m_context, (GDOX_Keys)i, false);
    }

    memset(m_held, 0, sizeof(m_held));
    memset(m_mouse_references, 0, sizeof(m_mouse_references));
    memset(m_remainder, 0, sizeof(m_remainder));
    memset(m_dpad_frames, 0, sizeof(m_dpad_frames));
    UpdateMouseButtons();
}

void GamepadKeys::GetDefaultMapping(GDOX_Gamepad_Mapping* mapping)
{
    mapping->mouse = GDOX_GAMEPAD_MOUSE_LEFT_STICK;
    mapping->mouse_speed = k_gamepad_mouse_speed_default;
    mapping->dead_zone = k_gamepad_dead_zone_default;

    for (int i = 0; i < GDOX_GAMEPAD_INPUT_COUNT; i++)
        mapping->targets[i] = k_gamepad_default_targets[i];
}

void GamepadKeys::ClearGame(GDOX_Gamepad_Game* game)
{
    game->use = -1;
    game->mouse = -1;

    for (int i = 0; i < GDOX_GAMEPAD_INPUT_COUNT; i++)
        game->targets[i] = -1;
}

bool GamepadKeys::MovesMouse(int mouse, int input)
{
    switch (mouse)
    {
        case GDOX_GAMEPAD_MOUSE_LEFT_STICK:
            return (input >= GDOX_GAMEPAD_INPUT_LEFT_STICK_UP) && (input <= GDOX_GAMEPAD_INPUT_LEFT_STICK_RIGHT);
        case GDOX_GAMEPAD_MOUSE_RIGHT_STICK:
            return (input >= GDOX_GAMEPAD_INPUT_RIGHT_STICK_UP) && (input <= GDOX_GAMEPAD_INPUT_RIGHT_STICK_RIGHT);
        case GDOX_GAMEPAD_MOUSE_DPAD:
            return (input >= GDOX_GAMEPAD_INPUT_DPAD_UP) && (input <= GDOX_GAMEPAD_INPUT_DPAD_RIGHT);
        default:
            return false;
    }
}

int GamepadKeys::FindInput(const char* id)
{
    return find_info(k_gamepad_inputs, GDOX_GAMEPAD_INPUT_COUNT, id);
}

int GamepadKeys::FindUse(const char* id)
{
    return find_info(k_gamepad_uses, GDOX_GAMEPAD_USE_COUNT, id);
}

int GamepadKeys::FindMouse(const char* id)
{
    return find_info(k_gamepad_mice, GDOX_GAMEPAD_MOUSE_COUNT, id);
}

int GamepadKeys::FindTarget(const char* name)
{
    if (!IsValidPointer(name))
        return -1;

    if (strcmp(name, "none") == 0)
        return GDOX_GAMEPAD_TARGET_NONE;

    int mouse = find_info(k_gamepad_mouse_targets, 3, name);

    if (mouse >= 0)
        return GDOX_GAMEPAD_TARGET_MOUSE_LEFT + mouse;

    int key = KeyMap::FindKey(name);
    return key > GDOX_KEY_NONE ? key : -1;
}

const char* GamepadKeys::GetTargetName(int target)
{
    if ((target >= GDOX_GAMEPAD_TARGET_MOUSE_LEFT) && (target < GDOX_GAMEPAD_TARGET_END))
        return k_gamepad_mouse_targets[target - GDOX_GAMEPAD_TARGET_MOUSE_LEFT].id;

    const char* name = is_valid_target(target) ? KeyMap::GetKeyName((GDOX_Keys)target) : NULL;
    return IsValidPointer(name) ? name : "none";
}

const char* GamepadKeys::GetTargetLabel(int target)
{
    if ((target >= GDOX_GAMEPAD_TARGET_MOUSE_LEFT) && (target < GDOX_GAMEPAD_TARGET_END))
        return k_gamepad_mouse_targets[target - GDOX_GAMEPAD_TARGET_MOUSE_LEFT].name;

    const char* label = is_valid_target(target) ? KeyMap::GetKeyLabel((GDOX_Keys)target) : NULL;
    return IsValidPointer(label) ? label : "";
}

void GamepadKeys::SetTarget(int pad, int input, int target)
{
    int& held = m_held[pad][input];

    if (held == target)
        return;

    if (held != GDOX_GAMEPAD_TARGET_NONE)
        ReleaseTarget(held);

    held = target;

    if (target != GDOX_GAMEPAD_TARGET_NONE)
        PressTarget(target);
}

void GamepadKeys::PressTarget(int target)
{
    if (target >= GDOX_GAMEPAD_TARGET_MOUSE_LEFT)
    {
        m_mouse_references[target - GDOX_GAMEPAD_TARGET_MOUSE_LEFT]++;
        return;
    }

    if ((m_references[target]++ == 0) && IsValidPointer(m_callback))
        m_callback(m_context, (GDOX_Keys)target, true);
}

void GamepadKeys::ReleaseTarget(int target)
{
    if (target >= GDOX_GAMEPAD_TARGET_MOUSE_LEFT)
    {
        u8& references = m_mouse_references[target - GDOX_GAMEPAD_TARGET_MOUSE_LEFT];

        if (references > 0)
            references--;

        return;
    }

    if ((m_references[target] > 0) && (--m_references[target] == 0) && IsValidPointer(m_callback))
        m_callback(m_context, (GDOX_Keys)target, false);
}

void GamepadKeys::MoveMouse(int pad, const GDOX_Gamepad_State& state, const GDOX_Gamepad_Mapping& mapping)
{
    float max_speed = (float)CLAMP(mapping.mouse_speed, 1, 15) * k_gamepad_keys_speed_scale;
    float x = 0.0f;
    float y = 0.0f;

    if ((mapping.mouse == GDOX_GAMEPAD_MOUSE_LEFT_STICK) || (mapping.mouse == GDOX_GAMEPAD_MOUSE_RIGHT_STICK))
    {
        int axis = mapping.mouse == GDOX_GAMEPAD_MOUSE_LEFT_STICK ? GDOX_GAMEPAD_AXIS_LEFT_X : GDOX_GAMEPAD_AXIS_RIGHT_X;
        float fx = (float)state.axes[axis] / 32767.0f;
        float fy = (float)state.axes[axis + 1] / 32767.0f;
        float magnitude = sqrtf((fx * fx) + (fy * fy));
        float dead_zone = (float)CLAMP(mapping.dead_zone, 0, 90) / 100.0f;
        m_dpad_frames[pad] = 0;

        if (magnitude > dead_zone)
        {
            float amount = (MIN(magnitude, 1.0f) - dead_zone) / (1.0f - dead_zone);
            float speed = (amount * amount * max_speed) / magnitude;
            x = fx * speed;
            y = fy * speed;
        }
    }
    else if (mapping.mouse == GDOX_GAMEPAD_MOUSE_DPAD)
    {
        int dx = ((state.buttons >> GDOX_GAMEPAD_INPUT_DPAD_RIGHT) & 1) - ((state.buttons >> GDOX_GAMEPAD_INPUT_DPAD_LEFT) & 1);
        int dy = ((state.buttons >> GDOX_GAMEPAD_INPUT_DPAD_DOWN) & 1) - ((state.buttons >> GDOX_GAMEPAD_INPUT_DPAD_UP) & 1);

        if ((dx != 0) || (dy != 0))
        {
            m_dpad_frames[pad] = MIN(m_dpad_frames[pad] + 1, k_gamepad_keys_dpad_ramp);
            float ramp = (float)m_dpad_frames[pad] / (float)k_gamepad_keys_dpad_ramp;
            float speed = max_speed * (0.1f + (0.9f * ramp)) * (((dx != 0) && (dy != 0)) ? 0.7071f : 1.0f);
            x = (float)dx * speed;
            y = (float)dy * speed;
        }
        else
            m_dpad_frames[pad] = 0;
    }
    else
    {
        m_remainder[pad][0] = 0.0f;
        m_remainder[pad][1] = 0.0f;
        m_dpad_frames[pad] = 0;
        return;
    }

    m_remainder[pad][0] += x;
    m_remainder[pad][1] += y;
    int delta_x = (int)m_remainder[pad][0];
    int delta_y = (int)m_remainder[pad][1];
    m_remainder[pad][0] -= (float)delta_x;
    m_remainder[pad][1] -= (float)delta_y;

    if (((delta_x != 0) || (delta_y != 0)) && IsValidPointer(m_input))
        m_input->SetMouseDelta(delta_x, delta_y);
}

void GamepadKeys::UpdateMouseButtons()
{
    if (!IsValidPointer(m_input))
        return;

    u8 buttons = (u8)((m_mouse_references[0] > 0 ? GDOX_MOUSE_LEFT : 0) | (m_mouse_references[1] > 0 ? GDOX_MOUSE_RIGHT : 0) |
        (m_mouse_references[2] > 0 ? GDOX_MOUSE_MIDDLE : 0));
    m_input->SetGamepadMouseButtons(buttons);
}
