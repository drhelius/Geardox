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
#include "../config.h"
#include "../emu.h"
#include "../gui.h"
#include "../gui_colors.h"

static void draw_grid_label(const char* label);
static void draw_game_port(Input* input, int port);
static void draw_button(const char* name, u16 mask, u16 buttons, u16 present);

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

static void draw_grid_label(const char* label)
{
    ImGui::TableNextColumn();
    ImGui::TextColored(violet, "%s", label);
    ImGui::TableNextColumn();
}
