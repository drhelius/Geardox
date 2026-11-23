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

#define GUI_HARD_DISKS_IMPORT
#include "gui_hard_disks.h"
#include "drive/folder_disk.h"

#include <string>
#include "imgui.h"
#include "gui.h"
#include "gui_actions.h"
#include "gui_colors.h"
#include "gui_filedialogs.h"
#include "gui_notifications.h"
#include "application.h"
#include "config.h"
#include "emu.h"
#include "emu_dos.h"
#include "media/game_descriptor.h"
#include "emu_floppy.h"
#include "emu_hard_disk.h"
#include "emu_game.h"
#include "utils.h"

static const int k_create_sizes_mb[5] = { 40, 100, 250, 500, 2000 };
static const char* create_sizes = "40 MB\0" "100 MB\0" "250 MB\0" "500 MB\0" "2000 MB\0" "Custom\0\0";
static const char* const k_slot_names[GDOX_MAX_HARD_DISKS] = { "Primary Master", "Primary Slave", "Secondary Master",
    "Secondary Slave" };


static bool open_create = false;
static bool open_quit = false;
static bool open_confirm = false;
static int dialog_id = 0;
static int create_choice = 1;
static int create_custom_mb = 100;
static int create_size_mb = 100;
static bool create_formatted = true;
static int source_id = 0;
static std::string source_path;
static int image_id = 0;
static std::string image_path;
static bool image_write_protected = false;
static bool image_power_on = false;
static int save_id = 0;
static std::string save_path;
static std::string save_error;
static std::string save_warning;
static bool saved = false;
static int recreate_system = GDOX_GAME_SYSTEM_FREEDOS;

static void default_menu(void);
static void builtin_items(int id, bool loading);
static const char* builtin_name(int system);
static void disk_menu(int id);
static bool load_source(int id, const char* path);
static void source_task(void);
static void source_done(void);
static bool load_image(int id, const char* path, bool power_on);
static void image_task(void);
static void image_done(void);
static void save_task(void);
static void save_done(void);
static void confirm_erase(void);
static void attach(int id, const char* path);
static void draw_create_popup(void);
static void draw_confirm_popup(void);
static void draw_quit_popup(void);
static bool flush_disks(void);
static void notify_attached(int id, const char* path, const char* message);
static void attach_error(const char* path);

void gui_hard_disks_menu(void)
{
    if (!ImGui::BeginMenu("Hard Disks"))
        return;

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
        disk_menu(i);

    ImGui::Separator();
    default_menu();

    if (!emu_is_empty() && gui_hard_disks_is_pending() && ImGui::MenuItem("Reset to Apply Changes"))
        gui_action_reset();

    ImGui::EndMenu();
}

void gui_hard_disks_popups(void)
{
    if (open_create)
    {
        open_create = false;
        gui_dialog_in_use = true;
        ImGui::OpenPopup("New Blank Hard Disk");
    }

    if (open_quit)
    {
        open_quit = false;
        gui_dialog_in_use = true;
        ImGui::OpenPopup("Unsaved Hard Disk Changes on Quit");
    }

    if (open_confirm)
    {
        open_confirm = false;
        gui_dialog_in_use = true;
        ImGui::OpenPopup("Erase Hard Disk");
    }

    draw_create_popup();
    draw_confirm_popup();
    draw_quit_popup();
}

void gui_hard_disks_update(void)
{
    if (emu_hard_disk_take_flush_error())
        gui_notify(gui_NotificationError, NULL, "Unable to save the hard disk changes", "They are kept and retried");

    std::string builtin_error;

    if (emu_hard_disk_take_fallback_error(&builtin_error))
        gui_notify(gui_NotificationError, NULL, "FreeDOS booted instead of the default disk", builtin_error.c_str());

    u32 changes = emu_hard_disk_take_state_changes();

    if (changes == 0)
        return;

    std::string ids;

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        if (IS_NOT_SET_BIT(changes, i))
            continue;

        ids += ids.empty() ? "" : ", ";
        ids += k_slot_names[i];
    }

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        FolderDisk* folder = emu_get_core()->GetFolderDisk(i);

        if (IS_SET_BIT(changes, i) && IsValidPointer(folder) && folder->IsQuarantined())
            ids += std::string(". ") + k_slot_names[i] + "'s changes go to " + folder->GetConflictsFolder() + " until it closes";
    }

    gui_notify(gui_NotificationWarning, ICON_MD_STORAGE, "The hard disk changed since this state", ids.c_str());
}

bool gui_hard_disks_load(const char* path)
{
    int id = emu_hard_disk_get_image_id(path);

    if (id < 0)
    {
        gui_set_error_message("Every hard disk slot is in use.");
        return false;
    }

    return load_image(id, path, true);
}

bool gui_hard_disks_load_source(const char* path)
{
    emu_hard_disk_boot_default();
    int id = emu_hard_disk_get_free_id();

    if (id < 0)
    {
        gui_set_error_message("Every hard disk slot is in use.");
        return false;
    }

    return load_source(id, path);
}

static void source_task(void)
{
    emu_hard_disk_prepare_source(source_path.c_str());
}

static void source_done(void)
{
    const Emu_HardDiskSource* source = emu_hard_disk_get_prepared_source();

    if (!source->ready || !emu_hard_disk_attach_prepared_source(source_id, false))
    {
        std::string message("Unable to make a hard disk from:\n");
        message += source->path;
        message += source->error[0] != 0 ? std::string("\n") + source->error : std::string();
        gui_set_error_message(message.c_str());
        return;
    }

    notify_attached(source_id, source->path, "Hard disk attached");

    if (source->left_out != 0)
    {
        char message[64];
        snprintf(message, sizeof(message), "%u items are too deep for DOS to reach", source->left_out);
        gui_notify(gui_NotificationWarning, ICON_MD_STORAGE, "Part of this folder was left out", message);
    }

    if (emu_is_empty())
        gui_action_power_on();
}

static bool load_image(int id, const char* path, bool power_on)
{
    if (gui_is_rom_loading() || emu_is_media_loading())
        return false;

    image_id = id;
    image_path = path;
    image_write_protected = config_emulator.hard_disk_write_protected[id] != 0;
    image_power_on = power_on;
    return gui_load_async(image_task, image_done);
}

static void image_task(void)
{
    emu_hard_disk_prepare_image(image_id, image_path.c_str(), image_write_protected);
}

static void image_done(void)
{
    if (!emu_hard_disk_attach_prepared_image(image_id, false))
    {
        attach_error(image_path.c_str());
        return;
    }

    notify_attached(image_id, image_path.c_str(), "Hard disk attached");

    if (image_power_on && emu_is_empty())
        gui_action_power_on();
}

bool gui_hard_disks_is_pending(void)
{
    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        Emu_HardDiskInfo info;
        emu_hard_disk_get_info(i, &info);

        if (info.pending)
            return true;
    }

    return false;
}

void gui_hard_disks_dialog_attach(int id, const char* path)
{
    attach(id, path);
}

void gui_hard_disks_dialog_create(int id, const char* path)
{
    if (emu_hard_disk_create(id, path, create_size_mb, create_formatted, config_emulator.hard_disk_write_protected[id] != 0))
        notify_attached(id, path, "Hard disk created");
    else
    {
        std::string message("Unable to create the hard disk image.\nAn existing file is never replaced:\n");
        message += path;
        gui_set_error_message(message.c_str());
    }
}

void gui_hard_disks_dialog_default(const char* path)
{
    if (!emu_hard_disk_is_image(path) || emu_hard_disk_is_source(path))
    {
        std::string message("Not a hard disk image:\n");
        message += path;
        gui_set_error_message(message.c_str());
        return;
    }

    emu_hard_disk_set_default(config_DefaultDisk_Custom, path);
}

void gui_hard_disks_dialog_save(int id, const char* path)
{
    if (!gui_hard_disks_can_save(id))
        return;

    save_id = id;
    save_path = path;
    gui_save_async(save_task, save_done);
}

void gui_hard_disks_save(int id)
{
    Emu_HardDiskInfo info;
    emu_hard_disk_get_info(id, &info);
    std::string name = path_filename(trim_trailing_separators(info.path));

    if (info.folder)
        name = info.game && (id == 0) ? emu_game_get_name() : path_strip_extension(name, ".zip");
    else
        name = path_strip_extension(name) + " Copy";

    gui_file_dialog_save_hard_disk(id, (safe_file_name(name, "Disk") + ".hdd").c_str());
}

bool gui_hard_disks_can_save(int id)
{
    Emu_HardDiskInfo info;
    return emu_hard_disk_get_info(id, &info) && info.attached && !info.pending && !gui_is_rom_loading() &&
        !emu_is_media_loading();
}

static void save_task(void)
{
    saved = emu_hard_disk_save(save_id, save_path.c_str(), &save_error, &save_warning);
}

static void save_done(void)
{
    if (!saved)
    {
        gui_set_error_message(("Unable to save the hard disk image:\n" + save_path + "\n" + save_error).c_str());
        return;
    }

    gui_notify(gui_NotificationSuccess, ICON_MD_SAVE, "Hard disk image saved", save_path.c_str());

    if (!save_warning.empty())
        gui_notify(gui_NotificationWarning, ICON_MD_STORAGE, "DOS was writing to the disk, save the image again",
            save_warning.c_str());
}

void gui_hard_disks_open_quit_confirmation(void)
{
    open_quit = true;
}

static void disk_menu(int id)
{
    if (emu_hard_disk_is_cdrom_position(id))
    {
        char taken[64];
        snprintf(taken, sizeof(taken), "%s: CD-ROM drive", k_slot_names[id]);
        ImGui::MenuItem(taken, NULL, false, false);
        return;
    }

    Emu_HardDiskInfo info;
    emu_hard_disk_get_info(id, &info);
    const char* path = info.source[0] != 0 ? info.source : (info.pending ? info.pending_path : info.path);
    bool present = path[0] != 0;
    std::string builtin = info.builtin ? std::string("Built-in ") + builtin_name(info.builtin_system) : "";
    const char* name = info.builtin ? builtin.c_str() : (present ? get_filename(path) : "Empty");
    char label[64];
    snprintf(label, sizeof(label), "%s: %.32s%s###hard_disk_%d", k_slot_names[id], name, strlen(name) > 32 ? "..." : "", id);

    if (!ImGui::BeginMenu(label))
        return;

    bool loading = gui_is_rom_loading() || emu_is_media_loading();

    if (ImGui::MenuItem("Attach...", NULL, false, !loading))
        gui_file_dialog_attach_hard_disk(id);

    if (ImGui::MenuItem("Attach Folder...", NULL, false, !loading))
        gui_file_dialog_attach_folder(id);

    builtin_items(id, loading);

    if (ImGui::MenuItem("New Blank Disk...", NULL, false, !loading))
    {
        dialog_id = id;
        open_create = true;
    }

    if (ImGui::MenuItem("Save Image As...", NULL, false, gui_hard_disks_can_save(id)))
        gui_hard_disks_save(id);

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("A copy of this disk as it is now, in a hard disk image.\nThe disk in use stays the same.");

    if (ImGui::MenuItem("Detach", NULL, false, present))
    {
        emu_hard_disk_detach(id);
        Emu_HardDiskInfo detached;
        emu_hard_disk_get_info(id, &detached);
        gui_notify(gui_NotificationInfo, ICON_MD_EJECT, "Hard disk detached", detached.pending ? "At the next reset" : name);
    }

    ImGui::Separator();

    if (!present)
        ImGui::TextDisabled("Empty");
    else
    {
        if (info.attached && !info.pending)
            ImGui::Text("%.40s%s, %u MB", name, strlen(name) > 40 ? "..." : "", (info.blocks + 1024) / 2048);
        else
            ImGui::Text("%.40s%s", name, strlen(name) > 40 ? "..." : "");

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", path);
    }

    if (info.pending && info.pending_attached)
        ImGui::TextColored(orange, "Attaches at the next reset");
    else if (info.pending)
        ImGui::TextColored(orange, "%.32s detaches at the next reset", get_filename(info.path));

    ImGui::Separator();

    bool write_protected = present ? info.pending_write_protected : config_emulator.hard_disk_write_protected[id] != 0;

    if (ImGui::MenuItem("Write Protected", NULL, &write_protected) && !emu_hard_disk_set_write_protected(id, write_protected))
        gui_notify(gui_NotificationError, NULL, "Unable to change the write protection of this disk");

    ImGui::Separator();

    if (ImGui::BeginMenu("Recent", !loading))
    {
        for (int i = 0; i < config_max_recent_hard_disks; i++)
        {
            const std::string& recent = config_emulator.recent_hard_disks[i];

            if (recent.empty())
                continue;

            ImGui::PushID(i);

            if (ImGui::MenuItem(get_filename(recent.c_str())))
            {
                std::string path = recent;
                attach(id, path.c_str());
            }

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", recent.c_str());

            ImGui::PopID();
        }

        ImGui::EndMenu();
    }

    ImGui::EndMenu();
}

static void default_menu(void)
{
    static const char k_tooltip[] = "Attached at power on when no hard disk is.\n"
        "Swaps at the next reset if it's the only one.";

    if (!ImGui::BeginMenu("Default Disk"))
    {
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", k_tooltip);

        return;
    }

    bool loading = gui_is_rom_loading() || emu_is_media_loading();
    bool imported = emu_dos_get_msdos_name()[0] != 0;
    const std::string& custom = config_emulator.hard_disk_default_path;
    std::string names[config_DefaultDisk_Count] = { std::string("Built-in ") + builtin_name(GDOX_GAME_SYSTEM_MSDOS),
        "Built-in FreeDOS", custom.empty() ? "" : std::string("Custom (") + get_filename(custom.c_str()) + ")", "None" };
    int selected = emu_hard_disk_get_default();
    float text_width = 0.0f;

    for (int i = 0; i < config_DefaultDisk_Count; i++)
        text_width = MAX(text_width, ImGui::CalcTextSize(names[i].c_str()).x);

    ImGui::PushItemWidth(gui_get_combo_width(text_width));

    if (ImGui::BeginCombo("##default_disk", names[selected].c_str()))
    {
        for (int i = 0; i < config_DefaultDisk_Count; i++)
        {
            bool available = (i != config_DefaultDisk_MSDOS) || imported;

            if ((i == config_DefaultDisk_Custom) && custom.empty())
                continue;

            if (i == config_DefaultDisk_Custom || i == config_DefaultDisk_None)
                ImGui::Separator();

            ImGui::PushID(i);

            if (ImGui::Selectable(names[i].c_str(), selected == i, available ? 0 : ImGuiSelectableFlags_Disabled))
                emu_hard_disk_set_default(i, NULL);

            ImGui::PopID();

            if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                continue;

            if (!available)
                ImGui::SetTooltip("Import your own MS-DOS first.");
            else if (i == config_DefaultDisk_Custom)
                ImGui::SetTooltip("%s", custom.c_str());
            else if (i == config_DefaultDisk_None)
                ImGui::SetTooltip("Boots floppies and CD-ROMs only.");
        }

        ImGui::EndCombo();
    }
    else if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", k_tooltip);

    ImGui::PopItemWidth();

    if (ImGui::MenuItem("Choose Custom Image...", NULL, false, !loading))
        gui_file_dialog_choose_default_hard_disk();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Your own hard disk image as the default disk.");

    ImGui::EndMenu();
}

static void builtin_items(int id, bool loading)
{
    static const int k_systems[2] = { GDOX_GAME_SYSTEM_MSDOS, GDOX_GAME_SYSTEM_FREEDOS };
    static const char* const k_tooltips[2] =
    {
        "MS-DOS.hdd, made from your import on first use.",
        "FreeDOS.hdd, made on first use."
    };
    bool imported = emu_dos_get_msdos_name()[0] != 0;

    for (int i = 0; i < 2; i++)
    {
        bool available = (k_systems[i] != GDOX_GAME_SYSTEM_MSDOS) || imported;
        bool attached = emu_hard_disk_is_builtin_attached(k_systems[i]);
        std::string label = std::string("Attach Built-in ") + builtin_name(k_systems[i]);

        if (ImGui::MenuItem(label.c_str(), NULL, false, !loading && available && !attached))
        {
            std::string error;

            if (emu_hard_disk_attach_builtin(id, k_systems[i]))
                notify_attached(id, builtin_name(k_systems[i]), "Hard disk attached");
            else if (emu_hard_disk_take_builtin_error(&error))
                gui_set_error_message(error.c_str());
            else
                gui_set_error_message("Unable to attach the built-in disk.");
        }

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", !available ? "Import your own MS-DOS first." :
                (attached ? "Attached already." : k_tooltips[i]));
    }
}

static const char* builtin_name(int system)
{
    const char* msdos = emu_dos_get_msdos_name();

    if (system != GDOX_GAME_SYSTEM_MSDOS)
        return "FreeDOS";

    return msdos[0] != 0 ? msdos : "MS-DOS";
}

void gui_hard_disks_confirm_recreate(int system)
{
    recreate_system = system;
    open_confirm = true;
}

static bool load_source(int id, const char* path)
{
    if (gui_is_rom_loading() || emu_is_media_loading())
        return false;

    if (emu_hard_disk_is_source_attached(path))
    {
        gui_set_error_message("This folder is attached already.");
        return false;
    }

    source_id = id;
    source_path = trim_trailing_separators(path);
    return gui_load_async(source_task, source_done);
}

static void confirm_erase(void)
{
    if (gui_is_rom_loading() || emu_is_media_loading())
        return;

    bool restart = !emu_is_empty() && emu_hard_disk_is_builtin_attached(recreate_system);
    std::string error;

    if (restart)
        gui_action_power_off();

    if (!emu_hard_disk_recreate_builtin(recreate_system))
    {
        emu_hard_disk_take_builtin_error(&error);
        gui_set_error_message(("Unable to make a new built-in disk. The old one is untouched.\n" + error).c_str());
    }

    if (restart)
        gui_action_power_on();
}

static void attach(int id, const char* path)
{
    if (emu_hard_disk_is_source(path))
    {
        load_source(id, path);
        return;
    }

    load_image(id, path, false);
}

static void attach_error(const char* path)
{
    std::string message("Unable to attach the hard disk image:\n");
    message += path;

    if (ends_with_no_case(path, ".vhd"))
        message += "\nOnly fixed VHD images are supported.";

    gui_set_error_message(message.c_str());
}

static void draw_create_popup(void)
{
    gui_center_next_window();

    if (!ImGui::BeginPopupModal("New Blank Hard Disk", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::Text("Create a blank hard disk as %s.\n\n", k_slot_names[dialog_id]);

    ImGui::Text("Size:");
    ImGui::PushItemWidth(248.0f);
    ImGui::Combo("##create_size", &create_choice, create_sizes);

    if (create_choice == 5)
    {
        ImGui::InputInt("MB##create_custom", &create_custom_mb);
        create_custom_mb = CLAMP(create_custom_mb, create_formatted ? 33 : 1, 2047);
    }

    ImGui::PopItemWidth();
    ImGui::Checkbox("Formatted", &create_formatted);

    if (!create_formatted)
        ImGui::Text("Unformatted disks need FDISK and FORMAT before use.");

    ImGui::NewLine();
    ImGui::Separator();

    if (ImGui::Button("Create...", ImVec2(120, 0)))
    {
        create_size_mb = create_choice < 5 ? k_create_sizes_mb[create_choice] : create_custom_mb;
        gui_close_popup();
        gui_file_dialog_create_hard_disk(dialog_id);
    }

    ImGui::SetItemDefaultFocus();
    ImGui::SameLine();

    if (ImGui::Button("Cancel", ImVec2(120, 0)))
    {
        gui_close_popup();
    }

    ImGui::EndPopup();
}

static void draw_confirm_popup(void)
{
    gui_center_next_window();

    if (!ImGui::BeginPopupModal("Erase Hard Disk", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    bool restart = !emu_is_empty() && emu_hard_disk_is_builtin_attached(recreate_system);

    ImGui::Text("%s a new built-in %s disk.\nEverything installed on the old one is lost.\n\n",
        restart ? "This restarts the machine and makes" : "This makes",
        recreate_system == GDOX_GAME_SYSTEM_MSDOS ? "MS-DOS" : "FreeDOS");

    ImGui::Separator();

    if (ImGui::Button("Erase", ImVec2(120, 0)))
    {
        gui_close_popup();
        confirm_erase();
    }

    ImGui::SameLine();

    if (ImGui::Button("Cancel", ImVec2(120, 0)))
    {
        gui_close_popup();
    }

    ImGui::SetItemDefaultFocus();
    ImGui::EndPopup();
}

static bool flush_disks(void)
{
    return emu_hard_disk_flush() && emu_floppy_flush();
}

static void draw_quit_popup(void)
{
    gui_center_next_window();

    if (!ImGui::BeginPopupModal("Unsaved Hard Disk Changes on Quit", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::Text("Some hard disk changes could not be saved.\n");
    ImGui::Text("Free some space or fix the file permissions and retry, or quit without them.\n\n");
    ImGui::Separator();

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        Emu_HardDiskInfo info;
        emu_hard_disk_get_info(i, &info);

        if (info.unflushed != 0)
            ImGui::Text("%s: %s", k_slot_names[i], get_filename(info.path));
    }

    ImGui::Separator();

    gui_draw_unsaved_quit_buttons(flush_disks, "Unable to save the hard disk changes.");

    ImGui::EndPopup();
}

static void notify_attached(int id, const char* path, const char* message)
{
    Emu_HardDiskInfo info;
    emu_hard_disk_get_info(id, &info);
    char text[64];
    snprintf(text, sizeof(text), "%s as %s", message, k_slot_names[id]);
    gui_notify(gui_NotificationSuccess, ICON_MD_STORAGE, text, info.pending ? "At the next reset" : get_filename(path));
}
