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
#include <SDL3/SDL.h>
#include "application.h"
#include "config.h"
#include "emu.h"
#include "gamepad.h"
#include "gui.h"
#include "game_focus.h"

#define EVENTS_IMPORT
#include "events.h"

static bool input_updated = false;
static u8 mouse_buttons = 0;
static float mouse_remainder_x = 0.0f;
static float mouse_remainder_y = 0.0f;
static bool keyboard_scancode_down[SDL_SCANCODE_COUNT] = { };

static void input_update(bool check_shortcuts);
static GDOX_Joystick_State input_build_joystick(void);
static void keyboard_send_key(SDL_Scancode scancode, bool pressed);
static bool keyboard_captures_event(const SDL_Event* event);

void events_emu(const SDL_Event* event, bool shortcut_consumed)
{
    if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP)
    {
        SDL_Scancode scancode = event->key.scancode;

        // Keys already held by the machine are released even when a menu has just taken focus
        if (event->type == SDL_EVENT_KEY_UP)
            keyboard_send_key(scancode, false);
        else if (keyboard_captures_event(event) && !shortcut_consumed && event->key.repeat == 0)
            keyboard_send_key(scancode, true);
    }

    if (gui_in_use && event->type != SDL_EVENT_MOUSE_BUTTON_UP)
        return;

    switch (event->type)
    {
        case SDL_EVENT_MOUSE_MOTION:
        {
            if ((!game_focus_pointer_captured() && !gui_main_window_hovered) || emu_key_mapper_is_open())
                break;

            float scale = powf(2.0f, (float)(CLAMP(config_emulator.mouse_sensitivity, 1, 15) - 7) / 4.0f);
            mouse_remainder_x += event->motion.xrel * scale;
            mouse_remainder_y += event->motion.yrel * scale;
            int x = (int)mouse_remainder_x;
            int y = (int)mouse_remainder_y;
            mouse_remainder_x -= (float)x;
            mouse_remainder_y -= (float)y;
            emu_set_mouse_delta(x, y);
            break;
        }

        // Clicks outside the screen, like those on debugger windows, stay with the GUI
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            u8 button = 0;

            if (event->button.button == SDL_BUTTON_LEFT)
                button = GDOX_MOUSE_LEFT;
            else if (event->button.button == SDL_BUTTON_RIGHT)
                button = GDOX_MOUSE_RIGHT;
            else if (event->button.button == SDL_BUTTON_MIDDLE)
                button = GDOX_MOUSE_MIDDLE;

            if (event->type == SDL_EVENT_MOUSE_BUTTON_UP)
                mouse_buttons &= (u8)~button;
            else if ((game_focus_pointer_captured() || (gui_main_window_hovered && (gui_main_window_focused || !config_debug.debug))) &&
                !emu_key_mapper_is_open())
                mouse_buttons |= button;

            input_update(true);
            input_updated = true;
            break;
        }

        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        {
            input_update(true);
            input_updated = true;
            break;
        }

        default:
            break;
    }
}

void events_emu(void)
{
    if (input_updated || gui_in_use)
        return;

    SDL_PumpEvents();
    input_update(true);
    input_updated = true;
}

void events_sync_input(void)
{
    events_release_keyboard();
    events_release_mouse();
    SDL_PumpEvents();
    input_update(false);
}

void events_release_keyboard(void)
{
    memset(keyboard_scancode_down, 0, sizeof(keyboard_scancode_down));
    emu_release_all_host_keys();
}

void events_release_mouse(void)
{
    mouse_buttons = 0;
    mouse_remainder_x = 0.0f;
    mouse_remainder_y = 0.0f;
    emu_set_mouse_buttons(0);
    emu_clear_mouse();
}

bool events_is_keyboard_active(void)
{
    return gui_main_window_focused && !gui_in_use && !gui_dialog_in_use && !emu_is_empty();
}

bool events_is_keyboard_captured(void)
{
    return config_debug.debug && events_is_keyboard_active();
}

void events_reset_input(void)
{
    input_updated = false;
}

bool events_input_updated(void)
{
    return input_updated;
}

static void input_update(bool check_shortcuts)
{
    emu_set_joystick_state(input_build_joystick());
    emu_set_mouse_buttons(mouse_buttons);

    if (check_shortcuts)
        gamepad_check_shortcuts();
}

// The left stick and the D-pad move stick A, the right stick's throttle is stick B's Y
// The flight stick's D-pad is its hat
static GDOX_Joystick_State input_build_joystick(void)
{
    GDOX_Joystick_State state;
    memset(&state, 0, sizeof(state));
    SDL_Gamepad* gamepad = gamepad_controller[0];

    if (!IsValidPointer(gamepad) || (gamepad_get_use(0) != GDOX_GAMEPAD_USE_JOYSTICK))
        return state;

    const config_Input_Gamepad& mapping = config_input_gamepad;
    int x = SDL_GetGamepadAxis(gamepad, (SDL_GamepadAxis)mapping.gamepad_x_axis);
    int y = SDL_GetGamepadAxis(gamepad, (SDL_GamepadAxis)mapping.gamepad_y_axis);
    int dead_zone = (mapping.gamepad_dead_zone * 32767) / 100;

    if (mapping.gamepad_invert_x_axis)
        x = -x - 1;

    if (mapping.gamepad_invert_y_axis)
        y = -y - 1;

    if ((x * x) + (y * y) < dead_zone * dead_zone)
    {
        x = 0;
        y = 0;
    }

    bool up = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP);
    bool down = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    bool left = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    bool right = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);

    if (emu_get_joystick_type() == GDOX_JOYSTICK_FLIGHTSTICK)
    {
        state.hat = (u8)((up ? GDOX_JOYSTICK_HAT_UP : 0) | (down ? GDOX_JOYSTICK_HAT_DOWN : 0) |
            (left ? GDOX_JOYSTICK_HAT_LEFT : 0) | (right ? GDOX_JOYSTICK_HAT_RIGHT : 0));
    }
    else
    {
        if (left != right)
            x = left ? -32768 : 32767;

        if (up != down)
            y = up ? -32768 : 32767;
    }

    state.axes[GDOX_JOYSTICK_AXIS_AX] = (s16)CLAMP(x, -32768, 32767);
    state.axes[GDOX_JOYSTICK_AXIS_AY] = (s16)CLAMP(y, -32768, 32767);
    state.axes[GDOX_JOYSTICK_AXIS_BY] = SDL_GetGamepadAxis(gamepad, (SDL_GamepadAxis)mapping.gamepad_throttle_axis);

    for (int i = 0; i < 4; i++)
    {
        if (gamepad_get_button(gamepad, mapping.gamepad_buttons[i]))
            state.buttons |= (u8)(1 << i);
    }

    return state;
}

static bool keyboard_captures_event(const SDL_Event* event)
{
    if (event->type != SDL_EVENT_KEY_DOWN && event->type != SDL_EVENT_KEY_UP)
        return false;

    if (!events_is_keyboard_active())
        return false;

    return (gui_main_window_sdl_window_id == 0) || (event->key.windowID == gui_main_window_sdl_window_id);
}

static void keyboard_send_key(SDL_Scancode scancode, bool pressed)
{
    if (scancode <= SDL_SCANCODE_UNKNOWN || scancode >= SDL_SCANCODE_COUNT)
        return;

    if (keyboard_scancode_down[scancode] == pressed)
        return;

    keyboard_scancode_down[scancode] = pressed;

    if (pressed)
        emu_host_key_pressed((int)scancode);
    else
        emu_host_key_released((int)scancode);
}
