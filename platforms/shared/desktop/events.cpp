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

#include <SDL3/SDL.h>
#include "application.h"
#include "config.h"
#include "emu.h"
#include "gamepad.h"
#include "gui.h"
#include "gui_actions.h"

#define EVENTS_IMPORT
#include "events.h"

static bool input_updated = false;
static u8 mouse_buttons = 0;
static float mouse_remainder_x = 0.0f;
static float mouse_remainder_y = 0.0f;
static bool keyboard_scancode_down[SDL_SCANCODE_COUNT] = { };
static GDOX_Keys keyboard_pressed_keys[SDL_SCANCODE_COUNT];
static int keyboard_key_references[GDOX_KEY_COUNT] = { };

static bool events_check_hotkey(const SDL_Event* event, const config_Hotkey& hotkey, bool allow_repeat);
static bool events_match_hotkey_scancode(const SDL_Event* event, const config_Hotkey& hotkey);
static void input_update(bool check_shortcuts);
static GDOX_Joystick_State input_build_joystick(void);
static GDOX_Keys keyboard_key_from_scancode(SDL_Scancode scancode);
static void keyboard_send_key(SDL_Scancode scancode, bool pressed);
static bool keyboard_captures_event(const SDL_Event* event);

bool events_shortcuts(const SDL_Event* event)
{
    bool keyboard_captured = config_emulator.capture_keyboard && keyboard_captures_event(event);

    if (event->type == SDL_EVENT_KEY_UP)
    {
        if (!keyboard_captured && events_match_hotkey_scancode(event, config_hotkeys[config_HotkeyIndex_Rewind]))
        {
            gui_action_rewind_released();
            return true;
        }

        return false;
    }

    if (event->type != SDL_EVENT_KEY_DOWN)
        return false;

    if (!keyboard_captured && events_check_hotkey(event, config_hotkeys[config_HotkeyIndex_Rewind], false))
    {
        gui_action_rewind_pressed();
        return true;
    }

    if (events_check_hotkey(event, config_hotkeys[config_HotkeyIndex_Quit], false))
    {
        application_trigger_quit();
        return true;
    }

    // While the keyboard is captured every other key belongs to the PC keyboard
    if (keyboard_captured)
    {
        if (events_check_hotkey(event, config_hotkeys[config_HotkeyIndex_Fullscreen], false))
        {
            gui_shortcut(gui_ShortcutFullscreen);
            return true;
        }

        if (events_check_hotkey(event, config_hotkeys[config_HotkeyIndex_CaptureMouse], false))
        {
            gui_shortcut(gui_ShortcutCaptureMouse);
            return true;
        }

        if (events_check_hotkey(event, config_hotkeys[config_HotkeyIndex_CaptureKeyboard], false))
        {
            gui_shortcut(gui_ShortcutCaptureKeyboard);
            return true;
        }

        return false;
    }

    for (int i = 0; i < GUI_HOTKEY_MAP_COUNT; i++)
    {
        const gui_HotkeyMapping& mapping = gui_hotkey_map[i];

        if (events_check_hotkey(event, config_hotkeys[mapping.config_index], mapping.allow_repeat))
        {
            gui_shortcut(mapping.shortcut);
            return true;
        }
    }

    if (event->key.repeat == 0 && event->key.scancode == SDL_SCANCODE_ESCAPE &&
        config_emulator.fullscreen && !config_emulator.always_show_menu && !emu_is_video_recording())
    {
        application_trigger_fullscreen(false);
        return true;
    }

    return false;
}

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
            if (!config_emulator.capture_mouse && !gui_main_window_hovered)
                break;

            float scale = (float)MAX(config_emulator.mouse_sensitivity, 1) / 6.0f;
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
            else if (config_emulator.capture_mouse || gui_main_window_hovered)
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
    memset(keyboard_key_references, 0, sizeof(keyboard_key_references));
    emu_release_all_keys();
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
        gamepad_check_shortcuts(0);
}

// The left stick and the D-pad move stick A, the right stick's throttle is stick B's Y
// The flight stick's D-pad is its hat
static GDOX_Joystick_State input_build_joystick(void)
{
    GDOX_Joystick_State state;
    memset(&state, 0, sizeof(state));
    SDL_Gamepad* gamepad = gamepad_controller[0];

    if (!IsValidPointer(gamepad))
        return state;

    const config_Input_Gamepad& mapping = config_input_gamepad[0];
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

    if (config_input.joystick_type == GDOX_JOYSTICK_FLIGHTSTICK)
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

static bool events_check_hotkey(const SDL_Event* event, const config_Hotkey& hotkey, bool allow_repeat)
{
    if (event->type != SDL_EVENT_KEY_DOWN)
        return false;

    if (!allow_repeat && event->key.repeat != 0)
        return false;

    if (event->key.scancode != hotkey.key)
        return false;

    SDL_Keymod mods = event->key.mod;
    SDL_Keymod expected = hotkey.mod;
    SDL_Keymod normalized = (SDL_Keymod)0;
    SDL_Keymod expected_normalized = (SDL_Keymod)0;

    if (mods & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL))
        normalized = (SDL_Keymod)(normalized | SDL_KMOD_CTRL);

    if (mods & (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT))
        normalized = (SDL_Keymod)(normalized | SDL_KMOD_SHIFT);

    if (mods & (SDL_KMOD_LALT | SDL_KMOD_RALT))
        normalized = (SDL_Keymod)(normalized | SDL_KMOD_ALT);

    if (mods & (SDL_KMOD_LGUI | SDL_KMOD_RGUI))
        normalized = (SDL_Keymod)(normalized | SDL_KMOD_GUI);

    if (expected & (SDL_KMOD_CTRL | SDL_KMOD_LCTRL | SDL_KMOD_RCTRL))
        expected_normalized = (SDL_Keymod)(expected_normalized | SDL_KMOD_CTRL);

    if (expected & (SDL_KMOD_SHIFT | SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT))
        expected_normalized = (SDL_Keymod)(expected_normalized | SDL_KMOD_SHIFT);

    if (expected & (SDL_KMOD_ALT | SDL_KMOD_LALT | SDL_KMOD_RALT))
        expected_normalized = (SDL_Keymod)(expected_normalized | SDL_KMOD_ALT);

    if (expected & (SDL_KMOD_GUI | SDL_KMOD_LGUI | SDL_KMOD_RGUI))
        expected_normalized = (SDL_Keymod)(expected_normalized | SDL_KMOD_GUI);

    return normalized == expected_normalized;
}

static bool events_match_hotkey_scancode(const SDL_Event* event, const config_Hotkey& hotkey)
{
    if (event->type != SDL_EVENT_KEY_UP && event->type != SDL_EVENT_KEY_DOWN)
        return false;

    if (hotkey.key == SDL_SCANCODE_UNKNOWN)
        return false;

    return event->key.scancode == hotkey.key;
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

    if (pressed)
    {
        if (keyboard_scancode_down[scancode])
            return;

        GDOX_Keys key = keyboard_key_from_scancode(scancode);

        if (key == GDOX_KEY_NONE)
            return;

        keyboard_scancode_down[scancode] = true;
        keyboard_pressed_keys[scancode] = key;

        if (keyboard_key_references[key]++ == 0)
            emu_key_pressed(key);
    }
    else
    {
        if (!keyboard_scancode_down[scancode])
            return;

        GDOX_Keys key = keyboard_pressed_keys[scancode];
        keyboard_scancode_down[scancode] = false;

        if (keyboard_key_references[key] > 0 && --keyboard_key_references[key] == 0)
            emu_key_released(key);
    }
}

static GDOX_Keys keyboard_key_from_scancode(SDL_Scancode scancode)
{
    switch (scancode)
    {
        case SDL_SCANCODE_A: return GDOX_KEY_A;
        case SDL_SCANCODE_B: return GDOX_KEY_B;
        case SDL_SCANCODE_C: return GDOX_KEY_C;
        case SDL_SCANCODE_D: return GDOX_KEY_D;
        case SDL_SCANCODE_E: return GDOX_KEY_E;
        case SDL_SCANCODE_F: return GDOX_KEY_F;
        case SDL_SCANCODE_G: return GDOX_KEY_G;
        case SDL_SCANCODE_H: return GDOX_KEY_H;
        case SDL_SCANCODE_I: return GDOX_KEY_I;
        case SDL_SCANCODE_J: return GDOX_KEY_J;
        case SDL_SCANCODE_K: return GDOX_KEY_K;
        case SDL_SCANCODE_L: return GDOX_KEY_L;
        case SDL_SCANCODE_M: return GDOX_KEY_M;
        case SDL_SCANCODE_N: return GDOX_KEY_N;
        case SDL_SCANCODE_O: return GDOX_KEY_O;
        case SDL_SCANCODE_P: return GDOX_KEY_P;
        case SDL_SCANCODE_Q: return GDOX_KEY_Q;
        case SDL_SCANCODE_R: return GDOX_KEY_R;
        case SDL_SCANCODE_S: return GDOX_KEY_S;
        case SDL_SCANCODE_T: return GDOX_KEY_T;
        case SDL_SCANCODE_U: return GDOX_KEY_U;
        case SDL_SCANCODE_V: return GDOX_KEY_V;
        case SDL_SCANCODE_W: return GDOX_KEY_W;
        case SDL_SCANCODE_X: return GDOX_KEY_X;
        case SDL_SCANCODE_Y: return GDOX_KEY_Y;
        case SDL_SCANCODE_Z: return GDOX_KEY_Z;
        case SDL_SCANCODE_1: return GDOX_KEY_1;
        case SDL_SCANCODE_2: return GDOX_KEY_2;
        case SDL_SCANCODE_3: return GDOX_KEY_3;
        case SDL_SCANCODE_4: return GDOX_KEY_4;
        case SDL_SCANCODE_5: return GDOX_KEY_5;
        case SDL_SCANCODE_6: return GDOX_KEY_6;
        case SDL_SCANCODE_7: return GDOX_KEY_7;
        case SDL_SCANCODE_8: return GDOX_KEY_8;
        case SDL_SCANCODE_9: return GDOX_KEY_9;
        case SDL_SCANCODE_0: return GDOX_KEY_0;
        case SDL_SCANCODE_RETURN: return GDOX_KEY_RETURN;
        case SDL_SCANCODE_ESCAPE: return GDOX_KEY_ESCAPE;
        case SDL_SCANCODE_BACKSPACE: return GDOX_KEY_BACKSPACE;
        case SDL_SCANCODE_TAB: return GDOX_KEY_TAB;
        case SDL_SCANCODE_SPACE: return GDOX_KEY_SPACE;
        case SDL_SCANCODE_MINUS: return GDOX_KEY_MINUS;
        case SDL_SCANCODE_EQUALS: return GDOX_KEY_EQUALS;
        case SDL_SCANCODE_LEFTBRACKET: return GDOX_KEY_LEFT_BRACKET;
        case SDL_SCANCODE_RIGHTBRACKET: return GDOX_KEY_RIGHT_BRACKET;
        case SDL_SCANCODE_BACKSLASH: return GDOX_KEY_BACKSLASH;
        case SDL_SCANCODE_NONUSHASH: return GDOX_KEY_BACKSLASH;
        case SDL_SCANCODE_SEMICOLON: return GDOX_KEY_SEMICOLON;
        case SDL_SCANCODE_APOSTROPHE: return GDOX_KEY_APOSTROPHE;
        case SDL_SCANCODE_GRAVE: return GDOX_KEY_GRAVE;
        case SDL_SCANCODE_COMMA: return GDOX_KEY_COMMA;
        case SDL_SCANCODE_PERIOD: return GDOX_KEY_PERIOD;
        case SDL_SCANCODE_SLASH: return GDOX_KEY_SLASH;
        case SDL_SCANCODE_NONUSBACKSLASH: return GDOX_KEY_NON_US_BACKSLASH;
        case SDL_SCANCODE_CAPSLOCK: return GDOX_KEY_CAPS_LOCK;
        case SDL_SCANCODE_F1: return GDOX_KEY_F1;
        case SDL_SCANCODE_F2: return GDOX_KEY_F2;
        case SDL_SCANCODE_F3: return GDOX_KEY_F3;
        case SDL_SCANCODE_F4: return GDOX_KEY_F4;
        case SDL_SCANCODE_F5: return GDOX_KEY_F5;
        case SDL_SCANCODE_F6: return GDOX_KEY_F6;
        case SDL_SCANCODE_F7: return GDOX_KEY_F7;
        case SDL_SCANCODE_F8: return GDOX_KEY_F8;
        case SDL_SCANCODE_F9: return GDOX_KEY_F9;
        case SDL_SCANCODE_F10: return GDOX_KEY_F10;
        case SDL_SCANCODE_F11: return GDOX_KEY_F11;
        case SDL_SCANCODE_F12: return GDOX_KEY_F12;
        case SDL_SCANCODE_PRINTSCREEN: return GDOX_KEY_PRINT_SCREEN;
        case SDL_SCANCODE_SYSREQ: return GDOX_KEY_SYSRQ;
        case SDL_SCANCODE_SCROLLLOCK: return GDOX_KEY_SCROLL_LOCK;
        case SDL_SCANCODE_PAUSE: return GDOX_KEY_PAUSE;
        case SDL_SCANCODE_INSERT: return GDOX_KEY_INSERT;
        case SDL_SCANCODE_HOME: return GDOX_KEY_HOME;
        case SDL_SCANCODE_PAGEUP: return GDOX_KEY_PAGE_UP;
        case SDL_SCANCODE_DELETE: return GDOX_KEY_DELETE;
        case SDL_SCANCODE_END: return GDOX_KEY_END;
        case SDL_SCANCODE_PAGEDOWN: return GDOX_KEY_PAGE_DOWN;
        case SDL_SCANCODE_RIGHT: return GDOX_KEY_RIGHT;
        case SDL_SCANCODE_LEFT: return GDOX_KEY_LEFT;
        case SDL_SCANCODE_DOWN: return GDOX_KEY_DOWN;
        case SDL_SCANCODE_UP: return GDOX_KEY_UP;
        case SDL_SCANCODE_NUMLOCKCLEAR: return GDOX_KEY_NUM_LOCK;
        case SDL_SCANCODE_KP_DIVIDE: return GDOX_KEY_KP_DIVIDE;
        case SDL_SCANCODE_KP_MULTIPLY: return GDOX_KEY_KP_MULTIPLY;
        case SDL_SCANCODE_KP_MINUS: return GDOX_KEY_KP_MINUS;
        case SDL_SCANCODE_KP_PLUS: return GDOX_KEY_KP_PLUS;
        case SDL_SCANCODE_KP_ENTER: return GDOX_KEY_KP_ENTER;
        case SDL_SCANCODE_KP_1: return GDOX_KEY_KP_1;
        case SDL_SCANCODE_KP_2: return GDOX_KEY_KP_2;
        case SDL_SCANCODE_KP_3: return GDOX_KEY_KP_3;
        case SDL_SCANCODE_KP_4: return GDOX_KEY_KP_4;
        case SDL_SCANCODE_KP_5: return GDOX_KEY_KP_5;
        case SDL_SCANCODE_KP_6: return GDOX_KEY_KP_6;
        case SDL_SCANCODE_KP_7: return GDOX_KEY_KP_7;
        case SDL_SCANCODE_KP_8: return GDOX_KEY_KP_8;
        case SDL_SCANCODE_KP_9: return GDOX_KEY_KP_9;
        case SDL_SCANCODE_KP_0: return GDOX_KEY_KP_0;
        case SDL_SCANCODE_KP_PERIOD: return GDOX_KEY_KP_PERIOD;
        case SDL_SCANCODE_LCTRL: return GDOX_KEY_LEFT_CTRL;
        case SDL_SCANCODE_RCTRL: return GDOX_KEY_RIGHT_CTRL;
        case SDL_SCANCODE_LSHIFT: return GDOX_KEY_LEFT_SHIFT;
        case SDL_SCANCODE_RSHIFT: return GDOX_KEY_RIGHT_SHIFT;
        case SDL_SCANCODE_LALT: return GDOX_KEY_LEFT_ALT;
        case SDL_SCANCODE_RALT: return GDOX_KEY_RIGHT_ALT;
        case SDL_SCANCODE_LGUI: return GDOX_KEY_LEFT_GUI;
        case SDL_SCANCODE_RGUI: return GDOX_KEY_RIGHT_GUI;
        case SDL_SCANCODE_APPLICATION: return GDOX_KEY_MENU;
        default: return GDOX_KEY_NONE;
    }
}
