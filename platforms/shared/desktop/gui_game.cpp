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
#include "emu_dos.h"
#include "emu_game.h"
#include "gui.h"
#include "gui_actions.h"
#include "gui_colors.h"
#include "gui_filedialogs.h"
#include "gui_hard_disks.h"
#include "gui_menus.h"
#include "gui_notifications.h"
#include "utils.h"
#include "media/media_file.h"

static std::vector<GameDisk_Found> choices;
static std::vector<std::string> programs;
static std::string chosen_program;
static int next_target = GDOX_GAME_TARGET_GAME;
static int next_program = -1;
static bool open_choice = false;
static bool open_programs = false;
static bool open_write_back = false;

static bool start(const GameDisk_Found& found);
static void boot_to(int target, int program);
static std::string program_name(const std::string& command);
static void menu_boot(bool enabled);
static void menu_files(bool open);
static void menu_options(void);
static void show_folder(const char* path);
static void menu_recent(void);
static void popup_choice(void);
static void popup_programs(void);
static void save_descriptor(void);
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
        ImGui::SetTooltip("A .gdox file, a game's ZIP or one of its programs.");

    if (ImGui::MenuItem("Open Game Folder...", NULL, false, !loading))
        gui_file_dialog_open_game_folder();

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("A game's folder. With a .gdox file in it or whithout it.");

    menu_recent();
    ImGui::Separator();
    menu_boot(open && !loading);
    menu_files(open);
    ImGui::Separator();

    if (ImGui::MenuItem("Last Save Report...", NULL, false, emu_game_get_last_write_back().done ||
        emu_game_get_last_write_back().refused))
        open_write_back = true;

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("What the last game saved on closing:\nfiles saved, deleted, conflicts.");

    ImGui::Separator();
    menu_options();
    ImGui::EndMenu();
}

static void menu_boot(bool enabled)
{
    if (!ImGui::BeginMenu("Boot To", enabled))
    {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Restarts into the game, its setup or the DOS prompt.\nThe disk stays as it is.\n"
                "A change of program opens the game again.");

        return;
    }

    GameDisk* disk = emu_game_get_disk();
    const GDOX_Game_Info& info = disk->GetDescriptor().GetInfo();
    int target = emu_game_get_target();
    bool game = target == GDOX_GAME_TARGET_GAME;
    bool voodoo = disk->HasVoodooProgram();
    bool chosen = emu_game_get_program() == GDOX_GAME_PROGRAM_VOODOO;
    int source = voodoo && !chosen ? emu_game_get_source("run", "program") : (int)Emu_Game_Source_User;
    bool pushed = gui_push_game_source_color(source);

    if (ImGui::MenuItem("Game", program_name(info.exe).c_str(), game && !disk->UsesVoodooExe()))
        boot_to(GDOX_GAME_TARGET_GAME, voodoo ? (int)GDOX_GAME_PROGRAM_GAME : -1);

    if (pushed)
        ImGui::PopStyleColor();

    if (voodoo && ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::Text("The game's own program.");
        ImGui::Text("Switching opens the game again and keeps the choice.");
        gui_draw_game_source_tooltip(source);
        ImGui::EndTooltip();
    }

    if (voodoo)
    {
        const GDOX_Machine_Profile& profile = k_machine_profiles[emu_get_core()->GetPendingMachineConfig().model];
        bool pci = (profile.features & GDOX_MACHINE_FEATURE_PCI) != 0;
        bool driver = !disk->VoodooProgramNeedsDriver() || (emu_dos_get_glide_name()[0] != 0);
        std::string name = program_name(info.voodoo_exe);
        source = chosen ? emu_game_get_source("run", "program") : (int)Emu_Game_Source_User;
        pushed = gui_push_game_source_color(source);

        if (ImGui::MenuItem("3dfx Voodoo", name.c_str(), game && disk->UsesVoodooExe(), pci && driver))
            boot_to(GDOX_GAME_TARGET_GAME, GDOX_GAME_PROGRAM_VOODOO);

        if (pushed)
            ImGui::PopStyleColor();

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::BeginTooltip();

            if (!pci)
                ImGui::Text("The 3dfx Voodoo needs a Pentium machine.");
            else if (!driver)
            {
                ImGui::Text("%s needs the 3dfx Voodoo driver.", name.c_str());
                ImGui::Text("Import it in Video > 3dfx Voodoo Driver.");
            }
            else
            {
                ImGui::Text("The game's 3dfx program, with the 3dfx Voodoo fitted.");
                ImGui::Text("Switching opens the game again and keeps the choice.");
            }

            gui_draw_game_source_tooltip(source);
            ImGui::EndTooltip();
        }
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Setup", NULL, target == GDOX_GAME_TARGET_SETUP, emu_game_has_setup()))
        boot_to(GDOX_GAME_TARGET_SETUP, -1);

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", emu_game_has_setup() ? "Runs the game's setup for its sound card or controls.\n"
            "Ends at the DOS prompt." : "The game descriptor names no setup program.");

    if (ImGui::MenuItem("DOS Prompt", NULL, target == GDOX_GAME_TARGET_PROMPT))
        boot_to(GDOX_GAME_TARGET_PROMPT, -1);

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The DOS prompt in the game's folder.");

    ImGui::EndMenu();
}

static void boot_to(int target, int program)
{
    GameDisk_Found found;
    bool switching = (program >= 0) && (program != emu_game_get_program());

    if (switching && !emu_game_set_program(program))
        return;

    if ((switching || emu_game_needs_restart()) && emu_game_get_found(&found))
    {
        next_target = target;
        next_program = switching ? -1 : emu_game_get_program();
        start(found);
        return;
    }

    emu_game_set_target(target);
}

static std::string program_name(const std::string& command)
{
    std::string program = command.substr(0, command.find(' '));
    size_t slash = program.find_last_of("\\/");
    return slash == std::string::npos ? program : program.substr(slash + 1);
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

    if (ImGui::MenuItem("Show Saves Folder", NULL, false, MediaFile::IsDirectory(emu_game_get_saves_path())))
        show_folder(emu_game_get_saves_path());

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Where the game's changes are saved on close:\n%s", emu_game_get_saves_path());

    ImGui::Separator();

    if (ImGui::MenuItem("Save Descriptor..."))
        save_descriptor();

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Saves the game's .gdox with your changes and key map\nin it, then opens the game from it.");

    if (ImGui::MenuItem("Save Hard Disk Image...", NULL, false, gui_hard_disks_can_save(0)))
        gui_hard_disks_save(0);

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("A copy of the game's C: as it is now, with its DOS,\nin a hard disk image that boots on its own.");

    if (ImGui::MenuItem("Forget This Game's Changes", NULL, false, emu_game_has_overrides()))
    {
        emu_game_reset_settings();
        gui_notify(gui_NotificationInfo, ICON_MD_VIDEOGAME_ASSET, "This game's changes were forgotten",
            "Restart the game to apply its defaults", "game_settings");
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Forgets every change made for this game:\nMachine, CGA Output, Emulated Joystick,\n"
            "Host Gamepads, Game Music, volumes, DOS\nand the program in Boot To.\nThe key map is kept.");

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
        static const char* const k_places[2] = { "The Game's Folder", "The Save Files Folder" };
        static const char* const k_tooltips[2] =
        {
            "Changes are written back to the game's folder.\nZIPs and read-only folders use the Save Files folder.",
            "The game's folder is never written.\nIts changes go to its own folder in Save Files\n"
            "and are laid over it each run."
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
        ImGui::OpenPopup("Save Descriptor");
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

bool gui_game_open(const std::vector<GameDisk_Found>& found)
{
    if (found.empty())
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

void gui_game_set_next_program(int program)
{
    next_program = program;
}

bool gui_game_restart_if_needed(void)
{
    GameDisk_Found found;

    if (!emu_game_needs_restart() || !emu_game_get_found(&found))
        return false;

    next_target = emu_game_get_target();
    next_program = emu_game_get_program();
    start(found);
    return true;
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

void gui_game_dialog_save_descriptor(const char* path)
{
    if (!emu_game_save_descriptor(path, chosen_program.c_str()))
    {
        gui_notify(gui_NotificationError, NULL, "Unable to save the descriptor", path);
        return;
    }

    gui_notify(gui_NotificationSuccess, ICON_MD_SAVE, "Descriptor saved", path);
    gui_load_rom(path);
}

static void save_descriptor(void)
{
    chosen_program.clear();

    if (emu_game_has_program())
    {
        gui_file_dialog_save_descriptor(emu_game_get_descriptor_location().c_str());
        return;
    }

    emu_game_list_programs(emu_game_get_disk()->GetRootPath(), &programs);

    if (programs.empty())
    {
        gui_set_error_message("This game's folder has no .EXE, .COM or .BAT in its root or one level down.");
        return;
    }

    open_programs = true;
}

static bool start(const GameDisk_Found& found)
{
    int target = next_target;
    int program = next_program;
    next_target = GDOX_GAME_TARGET_GAME;
    next_program = -1;

    if (!gui_start_game(found, target, program))
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
            gui_close_popup();
            start(chosen);
        }

        ImGui::PopID();
    }

    ImGui::Separator();

    if (ImGui::Button("Cancel", ImVec2(120, 0)))
    {
        gui_close_popup();
    }

    ImGui::EndPopup();
}

static void popup_programs(void)
{
    gui_center_next_window();

    if (!ImGui::BeginPopupModal("Save Descriptor", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::Text("Which program starts %s?", emu_game_get_name());
    ImGui::Separator();
    ImGui::BeginChild("##programs", ImVec2(360, 240), true);

    for (size_t i = 0; i < programs.size(); i++)
    {
        ImGui::PushID((int)i);

        if (ImGui::Selectable(programs[i].c_str()))
        {
            gui_close_popup();
            chosen_program = programs[i];
            gui_file_dialog_save_descriptor(emu_game_get_descriptor_location().c_str());
        }

        ImGui::PopID();
    }

    ImGui::EndChild();

    if (ImGui::Button("Cancel", ImVec2(120, 0)))
    {
        gui_close_popup();
    }

    ImGui::EndPopup();
}

static void popup_write_back(void)
{
    gui_center_next_window();

    if (!ImGui::BeginPopupModal("Last Save Report", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    const FolderDisk_Result& r = emu_game_get_last_write_back();
    ImGui::TextColored(cyan, "%s", emu_game_get_last_write_back_name());

    if (r.refused)
        ImGui::TextColored(red, "Nothing was saved: the game's disk didn't add up. Its changes are kept aside.");
    else
        ImGui::Text("%u files saved, %u deleted, %u folders, %u conflicts, %u not saved", r.written, r.deleted,
            r.directories, r.conflicts, r.failed);

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
        gui_close_popup();
    }

    ImGui::EndPopup();
}
