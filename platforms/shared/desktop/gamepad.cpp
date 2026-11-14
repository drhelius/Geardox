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
#include <fstream>
#include "geardox.h"
#include "config.h"
#include "emu.h"
#include "emu_game.h"
#include "gui.h"
#include "gui_notifications.h"
#include "hotkeys.h"
#include "utils.h"

#define GAMEPAD_IMPORT
#include "gamepad.h"

static bool gamepad_shortcut_prev[config_HotkeyIndex_COUNT] = { };

static const SDL_GamepadButton k_gamepad_key_buttons[GDOX_GAMEPAD_INPUT_LEFT_STICK_UP] =
{
    SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
    SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_INVALID,
    SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_BACK,
    SDL_GAMEPAD_BUTTON_START
};

static void notify_gamepad(int player, SDL_Gamepad* controller, bool connected);
static void read_gamepad_state(SDL_Gamepad* controller, GDOX_Gamepad_State* state);
static GDOX_Gamepad_Game get_game_gamepad(int pad, bool* open);

bool gamepad_init(void)
{
    for (int i = 0; i < GDOX_MAX_GAMEPADS; i++)
        InitPointer(gamepad_controller[i]);
    gamepad_load_mappings();
    gamepad_add();
    return true;
}

void gamepad_destroy(void)
{
    for (int i = 0; i < GDOX_MAX_GAMEPADS; i++)
    {
        if (IsValidPointer(gamepad_controller[i]))
        {
            SDL_CloseGamepad(gamepad_controller[i]);
        }
    }
}

void gamepad_load_mappings(void)
{
    std::string db_path;
    char exe_path[1024] = { };
    get_executable_path(exe_path, sizeof(exe_path));

    if (exe_path[0] != '\0')
    {
        db_path = std::string(exe_path) + "/gamecontrollerdb.txt";
    }
    else
    {
        db_path = "gamecontrollerdb.txt";
    }

    std::ifstream file;
    open_ifstream_utf8(file, db_path.c_str(), std::ios::in);
    if (!file.is_open())
    {
        open_ifstream_utf8(file, "gamecontrollerdb.txt", std::ios::in);
    }

    int added_mappings = 0;
    int updated_mappings = 0;
    int line_number = 0;
    char platform_field[64] = { };
    snprintf(platform_field, 64, "platform:%s", SDL_GetPlatform());

    if (file.is_open())
    {
        Debug("Loading gamecontrollerdb.txt file");
        std::string line;
        while (std::getline(file, line))
        {
            line_number++;

            size_t comment = line.find_first_of('#');
            if (comment != std::string::npos)
                line = line.substr(0, comment);

            size_t first = line.find_first_not_of(" \t\r\n");
            if (first == std::string::npos)
                continue;

            size_t last = line.find_last_not_of(" \t\r\n");
            line = line.substr(first, last - first + 1);

            size_t platform_pos = line.find("platform:Mac OS X");
            if (platform_pos != std::string::npos)
                line.replace(platform_pos, 17, "platform:macOS");

            if ((line.find("platform:") != std::string::npos) && (line.find(platform_field) == std::string::npos))
                continue;

            int result = SDL_AddGamepadMapping(line.c_str());

            if (result == 1)
                added_mappings++;
            else if (result == 0)
                updated_mappings++;
            else if (result == -1)
            {
                Error("Unable to load game controller mapping in line %d from gamecontrollerdb.txt", line_number);
                SDL_ERROR("SDL_AddGamepadMapping");
            }
        }
        file.close();
    }
    else
    {
        Error("Game controller database not found (gamecontrollerdb.txt)!!");
        return;
    }

    Log("Added %d new game controller mappings from gamecontrollerdb.txt", added_mappings);
    Log("Updated %d game controller mappings from gamecontrollerdb.txt", updated_mappings);

    gamepad_added_mappings = added_mappings;
    gamepad_updated_mappings = updated_mappings;
}

void gamepad_add(void)
{
    for (int i = 0; i < GDOX_MAX_GAMEPADS; i++)
    {
        if (IsValidPointer(gamepad_controller[i]))
        {
            SDL_Joystick* js = SDL_GetGamepadJoystick(gamepad_controller[i]);

            if (!IsValidPointer(js) || !SDL_JoystickConnected(js))
            {
                SDL_CloseGamepad(gamepad_controller[i]);
                gamepad_controller[i] = NULL;
                Debug("Game controller %d closed when adding a new gamepad", i);
            }
        }
    }

    bool player_connected[GDOX_MAX_GAMEPADS] = { };

    for (int i = 0; i < GDOX_MAX_GAMEPADS; i++)
    {
        player_connected[i] = IsValidPointer(gamepad_controller[i]);
    }

    bool all_slots_filled = true;
    for (int i = 0; i < GDOX_MAX_GAMEPADS; i++)
    {
        if (!player_connected[i])
        {
            all_slots_filled = false;
            break;
        }
    }
    if (all_slots_filled)
        return;

    int gamepad_count = 0;
    SDL_JoystickID* gamepads = SDL_GetGamepads(&gamepad_count);

    for (int i = 0; i < gamepad_count; i++)
    {
        SDL_JoystickID joystick_id = gamepads[i];

        bool already_assigned = false;
        for (int p = 0; p < GDOX_MAX_GAMEPADS; p++)
        {
            if (player_connected[p])
            {
                SDL_JoystickID player_id = SDL_GetJoystickID(SDL_GetGamepadJoystick(gamepad_controller[p]));
                if (player_id == joystick_id)
                {
                    already_assigned = true;
                    break;
                }
            }
        }

        if (already_assigned)
            continue;

        SDL_Gamepad* controller = SDL_OpenGamepad(joystick_id);
        if (!IsValidPointer(controller))
        {
            Log("Warning: Unable to open game controller %d!\n", i);
            Log("SDL Error: SDL_OpenGamepad (%s:%d) - %s", __FILE__, __LINE__, SDL_GetError());
            SDL_ClearError();
            continue;
        }

        bool assigned = false;
        for (int p = 0; p < GDOX_MAX_GAMEPADS; p++)
        {
            if (!player_connected[p])
            {
                gamepad_controller[p] = controller;
                player_connected[p] = true;
                Debug("Game controller %d assigned to Player %d", i, p+1);
                notify_gamepad(p, controller, true);
                assigned = true;
                break;
            }
        }

        if (!assigned)
        {
            SDL_CloseGamepad(controller);
            Debug("Game controller %d detected but all player slots are full", i);
        }

        all_slots_filled = true;
        for (int i = 0; i < GDOX_MAX_GAMEPADS; i++)
        {
            if (!player_connected[i])
            {
                all_slots_filled = false;
                break;
            }
        }
        if (all_slots_filled)
            break;
    }

    SDL_free(gamepads);
}

void gamepad_remove(SDL_JoystickID instance_id)
{
    for (int i = 0; i < GDOX_MAX_GAMEPADS; i++)
    {
        if (gamepad_controller[i] != NULL)
        {
            SDL_JoystickID current_id = SDL_GetJoystickID(SDL_GetGamepadJoystick(gamepad_controller[i]));
            if (current_id == instance_id)
            {
                notify_gamepad(i, gamepad_controller[i], false);
                SDL_CloseGamepad(gamepad_controller[i]);
                gamepad_controller[i] = NULL;
                Debug("Game controller %d disconnected from slot %d", instance_id, i);
            }
        }
    }
}

void gamepad_assign(int slot, SDL_JoystickID instance_id)
{
    if (slot < 0 || slot >= GDOX_MAX_GAMEPADS)
        return;

    if (instance_id == 0)
    {
        if (IsValidPointer(gamepad_controller[slot]))
        {
            SDL_CloseGamepad(gamepad_controller[slot]);
            gamepad_controller[slot] = NULL;
            Debug("Player %d controller set to None", slot + 1);
        }
        return;
    }

    if (IsValidPointer(gamepad_controller[slot]))
    {
        SDL_JoystickID current_id = SDL_GetJoystickID(SDL_GetGamepadJoystick(gamepad_controller[slot]));
        if (current_id == instance_id)
            return;
    }

    if (IsValidPointer(gamepad_controller[slot]))
    {
        SDL_CloseGamepad(gamepad_controller[slot]);
        gamepad_controller[slot] = NULL;
    }

    SDL_Gamepad* controller = SDL_OpenGamepad(instance_id);
    if (!IsValidPointer(controller))
    {
        Log("SDL_OpenGamepad failed for instance_id %d", instance_id);
        Log("SDL Error: SDL_OpenGamepad (%s:%d) - %s", __FILE__, __LINE__, SDL_GetError());
        SDL_ClearError();
        return;
    }

    gamepad_controller[slot] = controller;
    Debug("Game controller %d assigned to Player %d", instance_id, slot + 1);
}

int gamepad_get_detected(Gamepad_Detected_Info* out_list, int max_count)
{
    int gamepad_count = 0;
    SDL_JoystickID* gamepads = SDL_GetGamepads(&gamepad_count);
    int result = 0;

    for (int i = 0; i < gamepad_count && result < max_count; i++)
    {
        Gamepad_Detected_Info info;
        info.id = gamepads[i];
        const char* name = SDL_GetGamepadNameForID(gamepads[i]);
        info.name = name ? name : "Unknown";
        SDL_GUID guid = SDL_GetJoystickGUIDForID(gamepads[i]);
        SDL_GUIDToString(guid, info.guid_str, sizeof(info.guid_str));
        out_list[result] = info;
        result++;
    }

    SDL_free(gamepads);
    return result;
}

void gamepad_check_shortcuts(void)
{
    SDL_Gamepad* sdl_controller = gamepad_controller[0];
    if (!IsValidPointer(sdl_controller))
        return;

    for (int i = 0; i < config_HotkeyIndex_COUNT; i++)
    {
        int button_mapping = config_input_gamepad_shortcuts.gamepad_shortcuts[i];
        if (button_mapping == SDL_GAMEPAD_BUTTON_INVALID)
            continue;

        bool button_pressed = gamepad_get_button(sdl_controller, button_mapping);
        bool was_pressed = gamepad_shortcut_prev[i];
        gamepad_shortcut_prev[i] = button_pressed;

        if (button_pressed && !was_pressed)
            gui_hotkey((config_HotkeyIndex)i, true);
        else if (!button_pressed && was_pressed && ((hotkey_definitions[i].flags & HOTKEY_FLAG_HOLD) != 0))
            gui_hotkey((config_HotkeyIndex)i, false);
    }
}

bool gamepad_get_button(SDL_Gamepad* controller, int mapping)
{
    if (!IsValidPointer(controller))
        return false;

    if (mapping >= 0 && mapping < SDL_GAMEPAD_BUTTON_COUNT)
        return SDL_GetGamepadButton(controller, (SDL_GamepadButton)mapping) != 0;

    if (mapping >= GAMEPAD_VBTN_AXIS_BASE)
    {
        int axis = mapping - GAMEPAD_VBTN_AXIS_BASE;
        if (axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT)
            return false;

        Sint16 value = SDL_GetGamepadAxis(controller, (SDL_GamepadAxis)axis);
        return value > GAMEPAD_VBTN_AXIS_THRESHOLD;
    }

    return false;
}

int gamepad_get_use(int pad)
{
    bool open = false;
    GDOX_Gamepad_Game game = get_game_gamepad(pad, &open);
    int use = (open && (game.use >= 0)) ? game.use : config_input_gamepad_keys[pad].use;

    if (pad == 0)
        return use == GDOX_GAMEPAD_USE_JOYSTICK ? GDOX_GAMEPAD_USE_JOYSTICK : GDOX_GAMEPAD_USE_KEYS;

    return use == GDOX_GAMEPAD_USE_OFF ? GDOX_GAMEPAD_USE_OFF : GDOX_GAMEPAD_USE_KEYS;
}

void gamepad_set_use(int pad, int use)
{
    bool open = false;
    GDOX_Gamepad_Game game = get_game_gamepad(pad, &open);

    if (!open)
    {
        config_input_gamepad_keys[pad].use = use;
        return;
    }

    game.use = use;
    emu_game_set_gamepad(pad, game);
}

int gamepad_get_mouse(int pad)
{
    bool open = false;
    GDOX_Gamepad_Game game = get_game_gamepad(pad, &open);
    int mouse = (open && (game.mouse >= 0)) ? game.mouse : config_input_gamepad_keys[pad].mouse;
    return CLAMP(mouse, 0, GDOX_GAMEPAD_MOUSE_COUNT - 1);
}

void gamepad_set_mouse(int pad, int mouse)
{
    bool open = false;
    GDOX_Gamepad_Game game = get_game_gamepad(pad, &open);

    if (!open)
    {
        config_input_gamepad_keys[pad].mouse = mouse;
        return;
    }

    game.mouse = mouse;
    emu_game_set_gamepad(pad, game);
}

int gamepad_get_target(int pad, int input)
{
    bool open = false;
    GDOX_Gamepad_Game game = get_game_gamepad(pad, &open);
    return (open && (game.targets[input] >= 0)) ? game.targets[input] : config_input_gamepad_keys[pad].targets[input];
}

void gamepad_set_target(int pad, int input, int target)
{
    bool open = false;
    GDOX_Gamepad_Game game = get_game_gamepad(pad, &open);

    if (!open)
    {
        config_input_gamepad_keys[pad].targets[input] = target;
        return;
    }

    game.targets[input] = target;
    emu_game_set_gamepad(pad, game);
}

void gamepad_reset_targets(int pad)
{
    bool open = false;
    GDOX_Gamepad_Game game = get_game_gamepad(pad, &open);

    if (!open)
    {
        GDOX_Gamepad_Mapping defaults;
        GamepadKeys::GetDefaultMapping(&defaults);

        for (int i = 0; i < GDOX_GAMEPAD_INPUT_COUNT; i++)
            config_input_gamepad_keys[pad].targets[i] = defaults.targets[i];

        return;
    }

    const GDOX_Gamepad_Game* base = emu_game_get_base_gamepad(pad);

    for (int i = 0; i < GDOX_GAMEPAD_INPUT_COUNT; i++)
        game.targets[i] = base->targets[i];

    emu_game_set_gamepad(pad, game);
}

void gamepad_update_keys(void)
{
    bool active = !gui_in_use && !gui_dialog_in_use && !emu_key_mapper_is_open() && !emu_is_empty();

    for (int pad = 0; pad < GDOX_MAX_GAMEPADS; pad++)
    {
        GDOX_Gamepad_State state;
        memset(&state, 0, sizeof(state));
        SDL_Gamepad* controller = gamepad_controller[pad];
        bool keys = gamepad_get_use(pad) == GDOX_GAMEPAD_USE_KEYS;

        if (active && keys && IsValidPointer(controller))
            read_gamepad_state(controller, &state);

        GDOX_Gamepad_Mapping mapping;
        mapping.mouse = keys ? gamepad_get_mouse(pad) : (int)GDOX_GAMEPAD_MOUSE_OFF;
        mapping.mouse_speed = config_input_gamepad_keys[pad].mouse_speed;
        mapping.dead_zone = config_input_gamepad_keys[pad].dead_zone;

        for (int i = 0; i < GDOX_GAMEPAD_INPUT_COUNT; i++)
            mapping.targets[i] = gamepad_get_target(pad, i);

        emu_gamepad_update(pad, state, mapping);
    }
}

static void notify_gamepad(int player, SDL_Gamepad* controller, bool connected)
{
    const char* name = SDL_GetGamepadName(controller);
    char detail[256];
    snprintf(detail, sizeof(detail), "Host Gamepad %d: %s", player + 1, IsValidPointer(name) ? name : "Unknown");
    gui_notify(gui_NotificationInfo, ICON_MD_GAMEPAD, connected ? "Gamepad connected" : "Gamepad disconnected", detail);
}

static void read_gamepad_state(SDL_Gamepad* controller, GDOX_Gamepad_State* state)
{
    for (int i = 0; i < GDOX_GAMEPAD_INPUT_LEFT_STICK_UP; i++)
    {
        bool down = false;

        if (i == GDOX_GAMEPAD_INPUT_L2)
            down = SDL_GetGamepadAxis(controller, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > GAMEPAD_VBTN_AXIS_THRESHOLD;
        else if (i == GDOX_GAMEPAD_INPUT_R2)
            down = SDL_GetGamepadAxis(controller, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > GAMEPAD_VBTN_AXIS_THRESHOLD;
        else
            down = SDL_GetGamepadButton(controller, k_gamepad_key_buttons[i]);

        if (down)
            state->buttons |= 1u << i;
    }

    state->axes[GDOX_GAMEPAD_AXIS_LEFT_X] = SDL_GetGamepadAxis(controller, SDL_GAMEPAD_AXIS_LEFTX);
    state->axes[GDOX_GAMEPAD_AXIS_LEFT_Y] = SDL_GetGamepadAxis(controller, SDL_GAMEPAD_AXIS_LEFTY);
    state->axes[GDOX_GAMEPAD_AXIS_RIGHT_X] = SDL_GetGamepadAxis(controller, SDL_GAMEPAD_AXIS_RIGHTX);
    state->axes[GDOX_GAMEPAD_AXIS_RIGHT_Y] = SDL_GetGamepadAxis(controller, SDL_GAMEPAD_AXIS_RIGHTY);
}

static GDOX_Gamepad_Game get_game_gamepad(int pad, bool* open)
{
    GDOX_Gamepad_Game game;
    const GDOX_Gamepad_Game* current = emu_game_get_gamepad(pad);
    *open = IsValidPointer(current);

    if (*open)
        game = *current;
    else
        GamepadKeys::ClearGame(&game);

    return game;
}
