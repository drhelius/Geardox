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

#define GUI_GAME_IMPORT
#include "gui_game.h"

#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "imgui.h"
#include "fonts/IconsMaterialDesign.h"
#include "config.h"
#include "emu.h"
#include "emu_game.h"
#include "gui.h"
#include "gui_actions.h"
#include "gui_colors.h"
#include "gui_filedialogs.h"
#include "gui_notifications.h"
#include "utils.h"

static std::vector<GameDisk_Found> choices;
static std::vector<std::string> programs;
static std::string program_folder;
static int next_target = GDOX_GAME_TARGET_GAME;
static bool open_choice = false;
static bool open_programs = false;
static bool open_write_back = false;

static bool start(const GameDisk_Found& found);
static void menu_boot(bool enabled);
static void menu_files(bool open);
static void menu_options(void);
static void show_folder(const char* path);
static bool is_folder(const char* path);
static void menu_recent(void);
static void popup_choice(void);
static void popup_programs(void);
static void popup_write_back(void);

void gui_game_menu(void)
{
    if (!ImGui::BeginMenu("Game"))
        return;

    gui_in_use = true;
    bool loading = gui_is_rom_loading() || emu_is_media_loading();
    bool open = emu_game_is_open();

    if (open)
    {
        std::string close = std::string("Close ") + emu_game_get_name();

        if (ImGui::MenuItem(close.c_str(), NULL, false, !loading))
            emu_game_close();

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Powers off and saves the game's changes.");

        ImGui::Separator();
    }

    if (ImGui::MenuItem("Open Game...", NULL, false, !loading))
        gui_file_dialog_open_game();

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("A .gdox game descriptor, or a ZIP with one in its root.");

    if (ImGui::MenuItem("Open Game Folder...", NULL, false, !loading))
        gui_file_dialog_open_game_folder();

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("A folder with a .gdox game descriptor in its root.");

    menu_recent();
    ImGui::Separator();
    menu_boot(open && !loading);
    menu_files(open);

    if (ImGui::MenuItem("Last Save Report...", NULL, false, emu_game_get_last_write_back().done ||
        emu_game_get_last_write_back().refused))
        open_write_back = true;

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("What the last game saved on closing:\nfiles saved, deleted, conflicts.");

    if (emu_game_is_write_back_pending())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)red);

        if (ImGui::MenuItem("Retry Saving"))
            emu_game_retry_write_back();

        ImGui::PopStyleColor();

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s's changes weren't saved.\nSee Last Save Report...", emu_game_get_last_write_back_name());
    }

    ImGui::Separator();

    if (ImGui::MenuItem("New Game Descriptor...", NULL, false, !loading))
        gui_file_dialog_new_descriptor();

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Pick the game's folder and its start program.\nWrites the .gdox there and opens the game.\n"
            "Read-only folders use the save folder instead.\nSo does Save Games To > Geardox's Save Folder.");

    ImGui::Separator();
    menu_options();
    ImGui::EndMenu();
}

static void menu_boot(bool enabled)
{
    if (!ImGui::BeginMenu("Boot To", enabled))
    {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Restarts into the game, its setup or the DOS prompt.\nThe disk stays as it is.");

        return;
    }

    int target = emu_game_get_target();

    if (ImGui::MenuItem("Game", NULL, target == GDOX_GAME_TARGET_GAME))
        emu_game_set_target(GDOX_GAME_TARGET_GAME);

    if (ImGui::MenuItem("Setup", NULL, target == GDOX_GAME_TARGET_SETUP, emu_game_has_setup()))
        emu_game_set_target(GDOX_GAME_TARGET_SETUP);

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", emu_game_has_setup() ? "Runs the game's setup for its sound card or controls.\n"
            "Ends at the DOS prompt." : "The game descriptor names no setup program.");

    if (ImGui::MenuItem("DOS Prompt", NULL, target == GDOX_GAME_TARGET_PROMPT))
        emu_game_set_target(GDOX_GAME_TARGET_PROMPT);

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The DOS prompt in the game's folder.");

    ImGui::EndMenu();
}

static void menu_files(bool open)
{
    if (!ImGui::BeginMenu("Game Files", open))
        return;

    GameDisk* disk = emu_game_get_disk();

    if (ImGui::MenuItem("Show Game Folder"))
        show_folder(disk->GetSourcePath());

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", disk->GetSourcePath());

    if (ImGui::MenuItem("Show Saves Folder", NULL, false, is_folder(emu_game_get_saves_path())))
        show_folder(emu_game_get_saves_path());

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Where the game's changes are saved on close:\n%s", emu_game_get_saves_path());

    ImGui::Separator();

    if (ImGui::MenuItem("Save Descriptor As..."))
        gui_file_dialog_save_descriptor();

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Saves one descriptor with the game's changes merged in.");

    if (ImGui::MenuItem("Forget This Game's Changes", NULL, false, emu_game_has_overrides()))
    {
        emu_game_reset_settings();
        gui_notify(gui_NotificationInfo, ICON_MD_VIDEOGAME_ASSET, "This game's changes were forgotten",
            "Restart the game to apply its defaults", "game_settings");
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Forgets every change made for this game:\nMachine, CGA Output, Emulated Joystick,\n"
            "Host Gamepads, Game Synth and DOS.\nThe key map is kept.");

    ImGui::EndMenu();
}

void gui_game_set_system(int system)
{
    GameDisk_Found found;
    bool changed = (system < 0 ? emu_game_get_base_system() : system) != emu_game_get_system();

    if ((!changed && (system >= 0)) || !emu_game_get_found(&found))
        return;

    next_target = emu_game_get_target();

    if (emu_game_set_system(system) && changed)
        start(found);
}

static void menu_options(void)
{
    if (ImGui::BeginMenu("Save Games To"))
    {
        static const char* const k_places[2] = { "The Game's Folder", "Geardox's Save Folder" };
        static const char* const k_tooltips[2] =
        {
            "Changes are written back to the game's folder.\nZIPs and read-only folders use the save folder.",
            "The game's folder is never written.\nChanges in the save folder are laid over it each run."
        };
        int selected = config_game.write_back != 0 ? 1 : 0;
        float text_width = MAX(ImGui::CalcTextSize(k_places[0]).x, ImGui::CalcTextSize(k_places[1]).x);

        ImGui::PushItemWidth(gui_get_combo_width(text_width));

        if (ImGui::BeginCombo("##write_back", k_places[selected]))
        {
            for (int i = 0; i < 2; i++)
            {
                if (ImGui::Selectable(k_places[i], selected == i))
                    config_game.write_back = i;

                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", k_tooltips[i]);
            }

            ImGui::EndCombo();
        }
        else if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", k_tooltips[selected]);

        ImGui::PopItemWidth();
        ImGui::EndMenu();
    }

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Where games keep their changes.\nApplies from the next game opened.");

    ImGui::MenuItem("Quit When a Game Powers Off", NULL, &config_game.close_on_power_off);

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Closes Geardox when a game with on_exit = poweroff quits.");

    ImGui::MenuItem("Use Game Database", NULL, &config_game.database);

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Identifies game folders and ZIPs without a .gdox\nusing the built-in game database.\n"
            "A .gdox in the folder always takes priority.");
}

// The OS shows a folder from a file:// URL, with everything but the path's own characters escaped
static void show_folder(const char* path)
{
    std::string url = "file://";

    if ((path[0] != '/') && (path[0] != 0))
        url += "/";

    for (const char* p = path; *p != 0; p++)
    {
        unsigned char c = (unsigned char)*p;

        if (((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')) ||
            (strchr("/-_.~:", c) != NULL))
            url += (char)c;
        else if (c == '\\')
            url += '/';
        else
        {
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", c);
            url += hex;
        }
    }

    if (!SDL_OpenURL(url.c_str()))
        gui_notify(gui_NotificationError, NULL, "Unable to show the folder", path);
}

static bool is_folder(const char* path)
{
    SDL_PathInfo info;
    return (path[0] != 0) && SDL_GetPathInfo(path, &info) && (info.type == SDL_PATHTYPE_DIRECTORY);
}

void gui_game_popups(void)
{
    if (open_choice)
    {
        open_choice = false;
        gui_dialog_in_use = true;
        ImGui::OpenPopup("Choose a Game");
    }

    if (open_programs)
    {
        open_programs = false;
        gui_dialog_in_use = true;
        ImGui::OpenPopup("New Game Descriptor");
    }

    if (open_write_back)
    {
        open_write_back = false;
        gui_dialog_in_use = true;
        ImGui::OpenPopup("Last Save Report");
    }

    popup_choice();
    popup_programs();
    popup_write_back();
}

bool gui_game_open(const char* path)
{
    std::vector<GameDisk_Found> found;

    if (!emu_game_find(path, &found))
        return false;

    if (found.size() > 1)
    {
        choices = found;
        open_choice = true;
        return true;
    }

    return start(found[0]);
}

void gui_game_set_next_target(int target)
{
    next_target = target;
}

void gui_game_loaded(void)
{
    if (!emu_game_start())
    {
        std::string message("Unable to open the game:\n");
        message += emu_game_get_error();
        gui_set_error_message(message.c_str());
        return;
    }

    gui_notify(gui_NotificationInfo, ICON_MD_VIDEOGAME_ASSET, emu_game_get_name(), NULL, "power");

    if (config_emulator.start_paused)
        emu_pause();
}

void gui_game_dialog_new_descriptor(const char* folder)
{
    program_folder = folder;
    emu_game_list_programs(folder, &programs);

    if (programs.empty())
    {
        gui_set_error_message("This folder has no .EXE, .COM or .BAT in its root or one level down.");
        return;
    }

    open_programs = true;
}

void gui_game_dialog_save_descriptor(const char* path)
{
    if (emu_game_save_descriptor(path))
        gui_notify(gui_NotificationSuccess, ICON_MD_SAVE, "Descriptor saved", path);
    else
        gui_notify(gui_NotificationError, NULL, "Unable to save the descriptor", path);
}

static bool start(const GameDisk_Found& found)
{
    int target = next_target;
    next_target = GDOX_GAME_TARGET_GAME;

    if (!gui_start_game(found, target))
    {
        std::string message("Unable to open the game:\n");
        message += emu_game_get_error();
        gui_set_error_message(message.c_str());
        return false;
    }

    return true;
}

static void menu_recent(void)
{
    bool loading = gui_is_rom_loading() || emu_is_media_loading();

    if (!ImGui::BeginMenu("Recent Games", !loading))
        return;

    bool any = false;

    for (int i = 0; i < config_max_recent_games; i++)
    {
        if (config_game.recent[i].empty())
            continue;

        any = true;
        ImGui::PushID(i);

        if (ImGui::MenuItem(get_filename(config_game.recent[i].c_str())))
        {
            std::string path = config_game.recent[i];
            gui_load_rom(path.c_str());
        }

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", config_game.recent[i].c_str());

        ImGui::PopID();
    }

    if (!any)
        ImGui::TextDisabled("None");

    ImGui::EndMenu();
}

static void popup_choice(void)
{
    gui_center_next_window();

    if (!ImGui::BeginPopupModal("Choose a Game", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::Text("This holds more than one game:");
    ImGui::Separator();

    for (size_t i = 0; i < choices.size(); i++)
    {
        const GameDisk_Found& found = choices[i];
        std::string name = GameDisk::GetFoundName(found);
        ImGui::PushID((int)i);

        if (ImGui::Selectable(name.c_str()))
        {
            GameDisk_Found chosen = found;
            ImGui::CloseCurrentPopup();
            gui_dialog_in_use = false;
            start(chosen);
        }

        ImGui::PopID();
    }

    ImGui::Separator();

    if (ImGui::Button("Cancel", ImVec2(120, 0)))
    {
        ImGui::CloseCurrentPopup();
        gui_dialog_in_use = false;
    }

    ImGui::EndPopup();
}

static void popup_programs(void)
{
    gui_center_next_window();

    if (!ImGui::BeginPopupModal("New Game Descriptor", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::Text("Which program starts the game in %s?", get_filename(program_folder.c_str()));
    ImGui::Separator();
    ImGui::BeginChild("##programs", ImVec2(360, 240), true);

    for (size_t i = 0; i < programs.size(); i++)
    {
        ImGui::PushID((int)i);

        if (ImGui::Selectable(programs[i].c_str()))
        {
            std::string path;
            ImGui::CloseCurrentPopup();
            gui_dialog_in_use = false;

            if (emu_game_new_descriptor(program_folder.c_str(), programs[i].c_str(), &path))
            {
                gui_notify(gui_NotificationSuccess, ICON_MD_VIDEOGAME_ASSET, "Game descriptor written", path.c_str());
                gui_load_rom(path.c_str());
            }
            else
                gui_set_error_message((std::string("Unable to write the game descriptor:\n") + emu_game_get_error()).c_str());
        }

        ImGui::PopID();
    }

    ImGui::EndChild();

    if (ImGui::Button("Cancel", ImVec2(120, 0)))
    {
        ImGui::CloseCurrentPopup();
        gui_dialog_in_use = false;
    }

    ImGui::EndPopup();
}

static void popup_write_back(void)
{
    gui_center_next_window();

    if (!ImGui::BeginPopupModal("Last Save Report", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    const GameDisk_Result& r = emu_game_get_last_write_back();
    ImGui::TextColored(cyan, "%s", emu_game_get_last_write_back_name());

    if (r.refused)
        ImGui::TextColored(red, "Nothing was saved: the game's disk couldn't be processed. It's kept for a retry.");
    else
        ImGui::Text("%u files saved, %u deleted, %u folders, %u conflicts, %u not saved", r.written, r.deleted,
            r.directories, r.conflicts, r.kept);

    if (!r.notes.empty())
    {
        ImGui::Separator();
        ImGui::BeginChild("##notes", ImVec2(560, 200), true);

        for (size_t i = 0; i < r.notes.size(); i++)
            ImGui::TextWrapped("%s", r.notes[i].c_str());

        ImGui::EndChild();
    }

    if (ImGui::Button("OK", ImVec2(120, 0)))
    {
        ImGui::CloseCurrentPopup();
        gui_dialog_in_use = false;
    }

    ImGui::EndPopup();
}
