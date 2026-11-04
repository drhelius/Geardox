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

#define GUI_DEBUG_INPUT_IMPORT
#include "gui_debug_input.h"

#include "imgui.h"
#include "geardox.h"
#include "input/input.h"
#include "input/i8042.h"
#include "input/keyboard.h"
#include "system/system_control.h"
#include "../config.h"
#include "../emu.h"
#include "../gui.h"
#include "../gui_colors.h"
#include "gui_debug_constants.h"

static void draw_grid_label(const char* label);
static void draw_game_port(Input* input, int port);
static void draw_button(const char* name, u16 mask, u16 buttons, u16 present);
static void draw_flag(const char* name, bool on);

void gui_debug_window_game_ports(void)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::SetNextWindowPos(ImVec2(240, 179), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(219, 300), ImGuiCond_FirstUseEver);
    ImGui::Begin("Game Ports", &config_debug.show_game_ports);

    ImGui::PushFont(gui_default_font);

    Input* input = emu_get_core()->GetInput();

    for (int port = 0; port < GDOX_MAX_GAMEPADS; port++)
        draw_game_port(input, port);

    ImGui::PopFont();

    ImGui::End();
    ImGui::PopStyleVar();
}

void gui_debug_window_keyboard(void)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::SetNextWindowPos(ImVec2(240, 179), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340, 470), ImGuiCond_FirstUseEver);
    ImGui::Begin("Keyboard", &config_debug.show_keyboard);

    ImGui::PushFont(gui_default_font);

    I8042* controller = emu_get_core()->GetI8042();
    I8042::I8042_State* state = controller->GetState();
    Keyboard* keyboard = emu_get_core()->GetKeyboard();
    Keyboard::Keyboard_State* keys = keyboard->GetState();
    SystemControl* system_control = emu_get_core()->GetSystemControl();
    float character = ImGui::CalcTextSize("0").x;
    float space = ImGui::CalcTextSize(" ").x;
    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;
    u8 command = state->ram[0];
    u8 status = controller->GetStatus();

    ImGui::TextColored(cyan, "8042"); ImGui::Separator();

    if (ImGui::BeginTable("##i8042", 2, flags))
    {
        ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 10);
        ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 28);

        ImGui::TableNextRow();
        draw_grid_label("STATUS");
        ImGui::TextColored(white, "$%02X", status); ImGui::SameLine(0.0f, space);
        draw_flag("OBF", (status & 0x01) != 0); ImGui::SameLine(0.0f, space);
        draw_flag("IBF", (status & 0x02) != 0); ImGui::SameLine(0.0f, space);
        draw_flag("SYS", (status & 0x04) != 0); ImGui::SameLine(0.0f, space);
        draw_flag("AUX", (status & 0x20) != 0); ImGui::SameLine(0.0f, space);
        draw_flag("TO", (status & 0x40) != 0);

        ImGui::TableNextRow();
        draw_grid_label("COMMAND");
        ImGui::TextColored(white, "$%02X", command); ImGui::SameLine(0.0f, space);
        draw_flag("IRQ1", (command & k_i8042_keyboard_interrupt) != 0); ImGui::SameLine(0.0f, space);
        draw_flag("IRQ12", (command & k_i8042_aux_interrupt) != 0); ImGui::SameLine(0.0f, space);
        draw_flag("XLAT", (command & k_i8042_translate_enabled) != 0);

        ImGui::TableNextRow();
        draw_grid_label("PORTS");
        draw_flag("KBD", (command & k_i8042_keyboard_disabled) == 0); ImGui::SameLine(0.0f, space);
        draw_flag("AUX", (command & k_i8042_aux_disabled) == 0);

        ImGui::TableNextRow();
        draw_grid_label("OUTPUT");
        ImGui::TextColored(state->output_full ? white : gray, "$%02X", state->output_buffer);
        ImGui::SameLine(0.0f, space);
        ImGui::TextColored(gray, "%s", state->output_aux ? "AUX" : "KBD");

        ImGui::TableNextRow();
        draw_grid_label("PENDING");

        if (state->pending_command != 0)
            ImGui::TextColored(orange, "$%02X", state->pending_command);
        else
            ImGui::TextColored(gray, "--");

        ImGui::TableNextRow();
        draw_grid_label("OUT PORT");
        ImGui::TextColored(white, "$%02X", controller->GetOutputPort());

        ImGui::TableNextRow();
        draw_grid_label("IN PORT");
        ImGui::TextColored(white, "$%02X", k_i8042_input_port);

        ImGui::TableNextRow();
        draw_grid_label("A20");
        draw_flag("GATE", system_control->IsA20Enabled()); ImGui::SameLine(0.0f, space);
        draw_flag("8042", system_control->IsKeyboardA20Enabled()); ImGui::SameLine(0.0f, space);
        draw_flag("92h", (system_control->GetState()->port_92 & 0x02) != 0);

        ImGui::EndTable();
    }

    ImGui::NewLine();
    ImGui::TextColored(cyan, "MF2 KEYBOARD"); ImGui::Separator();

    if (ImGui::BeginTable("##keyboard", 2, flags))
    {
        ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 10);
        ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 28);

        ImGui::TableNextRow();
        draw_grid_label("SCANNING");
        draw_flag(keys->self_test_clocks != GDOX_NO_EVENT ? "SELF TEST" : "ON", keys->scanning);

        ImGui::TableNextRow();
        draw_grid_label("SCAN SET");
        ImGui::TextColored(white, "%u", keys->scan_set);

        ImGui::TableNextRow();
        draw_grid_label("LEDS");
        draw_flag("NUM", (keys->leds & 0x02) != 0); ImGui::SameLine(0.0f, space);
        draw_flag("CAPS", (keys->leds & 0x04) != 0); ImGui::SameLine(0.0f, space);
        draw_flag("SCROLL", (keys->leds & 0x01) != 0);

        ImGui::TableNextRow();
        draw_grid_label("TYPEMATIC");
        ImGui::TextColored(white, "%u ms, %.1f/s", keyboard->GetTypematicDelay(),
            1000000.0f / (float)keyboard->GetTypematicPeriod());

        ImGui::TableNextRow();
        draw_grid_label("REPEAT");
        const char* repeat = keys->repeat_key != GDOX_KEY_NONE ? gui_debug_key_name(keys->repeat_key) : NULL;
        ImGui::TextColored(IsValidPointer(repeat) ? orange : gray, "%s", IsValidPointer(repeat) ? repeat : "--");

        ImGui::TableNextRow();
        draw_grid_label("BUFFER");
        ImGui::TextColored(keys->overrun ? yellow : white, "%u/%u%s", keys->buffer_count, KEYBOARD_BUFFER_SIZE,
            keys->overrun ? " OVERRUN" : "");

        for (int i = 0; i < keys->buffer_count; i++)
        {
            if ((i & 7) == 0)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TableNextColumn();
            }
            else
                ImGui::SameLine(0.0f, space);

            ImGui::TextColored(white, "%02X", keys->buffer[(keys->buffer_read + i) & (KEYBOARD_BUFFER_CAPACITY - 1)]);
        }

        ImGui::TableNextRow();
        draw_grid_label("HELD");
        bool held = false;

        for (int i = GDOX_KEY_NONE + 1; i < GDOX_KEY_COUNT; i++)
        {
            const char* name = keys->keys[i] != 0 ? gui_debug_key_name(i) : NULL;

            if (!IsValidPointer(name))
                continue;

            if (held)
                ImGui::SameLine(0.0f, space);

            ImGui::TextColored(green, "%s", name);
            held = true;
        }

        if (!held)
            ImGui::TextColored(gray, "--");

        ImGui::EndTable();
    }

    ImGui::PopFont();

    ImGui::End();
    ImGui::PopStyleVar();
}

static void draw_game_port(Input* input, int port)
{
    static const char* k_types[3] = { "NONE", "GAMEPAD", "MOUSE" };
    Input::Input_State* state = input->GetState();
    GDOX_Controller_Type type = input->GetControllerType(port);
    bool mouse = type == GDOX_CONTROLLER_MOUSE;
    u16 buttons = state->gamepads[port].buttons;
    float character = ImGui::CalcTextSize("0").x;
    float space = ImGui::CalcTextSize(" ").x;
    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;
    char table_id[32];

    if (port > 0)
        ImGui::NewLine();

    ImGui::TextColored(cyan, "PORT %d", port + 1); ImGui::Separator();
    snprintf(table_id, sizeof(table_id), "##game_port_%d", port);

    if (!ImGui::BeginTable(table_id, 2, flags))
        return;

    ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 8);
    ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 22);

    ImGui::TableNextRow();
    draw_grid_label("TYPE");
    ImGui::TextColored(type == GDOX_CONTROLLER_NONE ? gray : orange, "%s", k_types[type % 3]);

    u16 pad = GDOX_GAMEPAD_UP | GDOX_GAMEPAD_DOWN | GDOX_GAMEPAD_LEFT | GDOX_GAMEPAD_RIGHT | GDOX_GAMEPAD_SELECT |
        GDOX_GAMEPAD_RUN | GDOX_GAMEPAD_A | GDOX_GAMEPAD_B;
    u16 present = 0;

    if (type == GDOX_CONTROLLER_GAMEPAD)
        present = pad;
    else if (mouse)
        present = GDOX_GAMEPAD_A | GDOX_GAMEPAD_B;

    ImGui::TableNextRow();
    draw_grid_label("BUTTONS");
    draw_button("UP", GDOX_GAMEPAD_UP, buttons, present); ImGui::SameLine(0.0f, space);
    draw_button("DOWN", GDOX_GAMEPAD_DOWN, buttons, present); ImGui::SameLine(0.0f, space);
    draw_button("LEFT", GDOX_GAMEPAD_LEFT, buttons, present); ImGui::SameLine(0.0f, space);
    draw_button("RIGHT", GDOX_GAMEPAD_RIGHT, buttons, present);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TableNextColumn();
    draw_button("SEL", GDOX_GAMEPAD_SELECT, buttons, present); ImGui::SameLine(0.0f, space);
    draw_button("RUN", GDOX_GAMEPAD_RUN, buttons, present); ImGui::SameLine(0.0f, space);
    draw_button("A", GDOX_GAMEPAD_A, buttons, present); ImGui::SameLine(0.0f, space);
    draw_button("B", GDOX_GAMEPAD_B, buttons, present);

    ImGui::TableNextRow();
    draw_grid_label("PENDING");

    if (mouse)
    {
        ImGui::TextColored(violet, "X"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(white, "%+4d", state->mouse_x[port]); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(violet, "Y"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(white, "%+4d", state->mouse_y[port]);
    }
    else
        ImGui::TextColored(gray, "--");

    ImGui::EndTable();
}

static void draw_button(const char* name, u16 mask, u16 buttons, u16 present)
{
    bool available = (present & mask) != 0;
    bool pressed = available && (buttons & mask) != 0;

    ImGui::TextColored(pressed ? green : (available ? white : gray), "%s", name);
}

static void draw_flag(const char* name, bool on)
{
    ImGui::TextColored(on ? green : gray, "%s", name);
}

static void draw_grid_label(const char* label)
{
    ImGui::TableNextColumn();
    ImGui::TextColored(violet, "%s", label);
    ImGui::TableNextColumn();
}
