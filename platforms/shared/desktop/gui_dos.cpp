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

#define GUI_DOS_IMPORT
#include "gui_dos.h"

#include <string>
#include "imgui.h"
#include "fonts/IconsMaterialDesign.h"
#include "config.h"
#include "emu.h"
#include "emu_dos.h"
#include "emu_game.h"
#include "gui.h"
#include "gui_filedialogs.h"
#include "gui_game.h"
#include "gui_hard_disks.h"
#include "gui_menus.h"
#include "gui_notifications.h"
#include "media/game_descriptor.h"

static void menu_game(bool loading);
static void menu_keyboard_layout(void);
static void menu_recreate(bool loading);
static void menu_import(bool loading);

void gui_dos_menu(void)
{
    if (!ImGui::BeginMenu("DOS"))
        return;

    gui_in_use = true;
    bool loading = gui_is_rom_loading() || emu_is_media_loading();
    menu_game(loading);
    menu_keyboard_layout();
    ImGui::Separator();
    menu_recreate(loading);
    ImGui::Separator();
    menu_import(loading);
    ImGui::EndMenu();
}

void gui_dos_dialog_import_msdos(const char* path)
{
    std::string message;

    if (emu_dos_import_msdos(path, &message))
        gui_notify(gui_NotificationSuccess, ICON_MD_VIDEOGAME_ASSET, (message + " imported").c_str(),
            "Games and the built-in disk boot it from the next game or reset.");
    else
        gui_notify(gui_NotificationError, NULL, "MS-DOS not imported", message.c_str());
}

void gui_dos_dialog_import_ultrasnd(const char* path)
{
    std::string message;

    if (emu_dos_import_ultrasnd(path, &message))
        gui_notify(gui_NotificationSuccess, ICON_MD_VIDEOGAME_ASSET, message.c_str(),
            "Games with the UltraSound on load its patches from the next game or reset.");
    else
        gui_notify(gui_NotificationError, NULL, "UltraSound software not imported", message.c_str());
}

void gui_dos_remove_ultrasnd(void)
{
    if (emu_dos_remove_ultrasnd())
        gui_notify(gui_NotificationInfo, ICON_MD_VIDEOGAME_ASSET, "UltraSound software removed",
            "Games with the UltraSound have no patches from the next game or reset. Your disks aren't touched.");
    else
        gui_notify(gui_NotificationError, NULL, "Unable to remove the UltraSound software",
            emu_dos_get_ultrasnd_path().c_str());
}

static void menu_game(bool loading)
{
    bool open = emu_game_is_open();
    int source = emu_game_get_source("dos", "system");
    bool pushed = gui_push_game_source_color(source);
    bool menu = ImGui::BeginMenu("Game", open && !loading);

    if (pushed)
        ImGui::PopStyleColor();

    if (!menu)
    {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", open ? "The DOS the game boots.\nChanging it saves, closes and reopens the game." :
                "Open a game to choose its DOS.");

        return;
    }

    const char* msdos = emu_dos_get_msdos_name();
    bool imported = msdos[0] != 0;
    const char* names[2] = { imported ? msdos : "MS-DOS", "FreeDOS" };
    int selected = emu_game_get_system() == GDOX_GAME_SYSTEM_MSDOS ? 0 : 1;
    std::string game_default = std::string("Game Default (") +
        names[emu_game_get_base_system() == GDOX_GAME_SYSTEM_MSDOS ? 0 : 1] + ")";
    float text_width = MAX(MAX(ImGui::CalcTextSize(names[0]).x, ImGui::CalcTextSize(names[1]).x),
        ImGui::CalcTextSize(game_default.c_str()).x);

    ImGui::PushItemWidth(gui_get_combo_width(text_width));
    pushed = gui_push_game_source_color(source);
    bool combo = ImGui::BeginCombo("##game_system", names[selected]);

    if (pushed)
        ImGui::PopStyleColor();

    if (combo)
    {
        if (ImGui::Selectable(game_default.c_str(), source != Emu_Game_Source_Change))
            gui_game_set_system(-1);

        ImGui::Separator();

        if (ImGui::Selectable(names[0], selected == 0, imported ? 0 : ImGuiSelectableFlags_Disabled))
            gui_game_set_system(GDOX_GAME_SYSTEM_MSDOS);

        if (!imported && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Import your own MS-DOS first.");

        if (ImGui::Selectable(names[1], selected == 1))
            gui_game_set_system(GDOX_GAME_SYSTEM_FREEDOS);

        ImGui::EndCombo();
    }
    else if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::Text("The DOS the game boots.");
        ImGui::Text("Changing it saves, closes and reopens the game.");
        gui_draw_game_source_tooltip(source);
        ImGui::EndTooltip();
    }

    ImGui::PopItemWidth();
    ImGui::EndMenu();
}

static void menu_keyboard_layout(void)
{
    static const char k_tooltip[] = "Your keyboard layout for DOS in games, via KEYB.\n"
        "Applies from the next game opened.\nA .gdox sets its own with keyboard in [dos].\n"
        "New built-in disks load it too.";

    if (!ImGui::BeginMenu("Keyboard Layout"))
    {
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", k_tooltip);

        return;
    }

    int selected = 0;
    float text_width = 0.0f;

    for (int i = 0; i < k_keyboard_layout_count; i++)
    {
        text_width = MAX(text_width, ImGui::CalcTextSize(k_keyboard_layouts[i].name).x);

        if (config_input.keyboard_layout == k_keyboard_layouts[i].id)
            selected = i;
    }

    ImGui::PushItemWidth(gui_get_combo_width(text_width));

    if (ImGui::BeginCombo("##keyboard_layout", k_keyboard_layouts[selected].name, ImGuiComboFlags_HeightLarge))
    {
        for (int i = 0; i < k_keyboard_layout_count; i++)
        {
            if (ImGui::Selectable(k_keyboard_layouts[i].name, selected == i))
                config_input.keyboard_layout = k_keyboard_layouts[i].id;

            if (i == 0)
                ImGui::Separator();
        }

        ImGui::EndCombo();
    }
    else if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", k_tooltip);

    ImGui::PopItemWidth();
    ImGui::EndMenu();
}

static void menu_recreate(bool loading)
{
    bool imported = emu_dos_get_msdos_name()[0] != 0;

    if (ImGui::MenuItem("Recreate Built-in MS-DOS Disk...", NULL, false, imported && !loading))
        gui_hard_disks_confirm_recreate(GDOX_GAME_SYSTEM_MSDOS);

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", imported ? "A new MS-DOS.hdd from your import.\nEverything installed on it is lost." :
            "Import your own MS-DOS first.");

    if (ImGui::MenuItem("Recreate Built-in FreeDOS Disk...", NULL, false, !loading))
        gui_hard_disks_confirm_recreate(GDOX_GAME_SYSTEM_FREEDOS);

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("A new FreeDOS.hdd.\nEverything installed on it is lost.");
}

static void menu_import(bool loading)
{
    bool imported = emu_dos_get_msdos_name()[0] != 0;

    if (ImGui::MenuItem("Import MS-DOS...", NULL, false, !loading))
        gui_file_dialog_import_msdos();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Your MS-DOS 5.0 to 6.22 Setup disks, any language.\nA disk image or a ZIP of them.\n"
            "Games and the built-in disk boot it unless set to FreeDOS.");

    if (ImGui::MenuItem("Remove MS-DOS", NULL, false, imported && !loading))
    {
        if (emu_dos_remove_msdos())
            gui_notify(gui_NotificationInfo, ICON_MD_VIDEOGAME_ASSET, "MS-DOS removed",
                "Games and the built-in disk boot FreeDOS. MS-DOS.hdd is kept.");
        else
            gui_notify(gui_NotificationError, NULL, "Unable to remove MS-DOS", emu_dos_get_msdos_path().c_str());
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Games and the built-in disk revert to FreeDOS.\nMS-DOS.hdd and your Setup disks are kept.");
}
