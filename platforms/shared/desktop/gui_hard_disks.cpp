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
#include "emu_floppy.h"
#include "emu_hard_disk.h"
#include "utils.h"

static const int k_create_sizes_mb[5] = { 40, 100, 250, 500, 2000 };
static const char* create_sizes = "40 MB\0" "100 MB\0" "250 MB\0" "500 MB\0" "2000 MB\0" "Custom\0\0";
static const char* const k_slot_names[GDOX_MAX_HARD_DISKS] = { "Primary Master", "Primary Slave", "Secondary Master",
    "Secondary Slave" };

static const int k_recreate_freedos = -1;

static bool open_create = false;
static bool open_quit = false;
static bool open_confirm = false;
static int dialog_id = 0;
static int confirm_id = 0;
static int create_choice = 1;
static int create_custom_mb = 100;
static int create_size_mb = 100;
static bool create_formatted = true;
static int source_id = 0;
static bool source_loading = false;
static bool source_reset = false;
static bool source_rebuild = false;

static void freedos_menu(void);
static void disk_menu(int id);
static bool load_source(int id, const char* path, bool rebuild);
static void confirm_erase(void);
static void attach(int id, const char* path);
static void draw_create_popup(void);
static void draw_confirm_popup(void);
static void draw_quit_popup(void);
static void notify_attached(int id, const char* path, const char* message);
static void attach_error(const char* path);

void gui_hard_disks_menu(void)
{
    if (!ImGui::BeginMenu("Hard Disks"))
        return;

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
        disk_menu(i);

    ImGui::Separator();
    freedos_menu();

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

    u32 changes = emu_hard_disk_take_state_changes();

    if (changes == 0)
        return;

    std::string ids;

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        if ((changes & (1 << i)) == 0)
            continue;

        ids += ids.empty() ? "" : ", ";
        ids += k_slot_names[i];
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

    if (!emu_hard_disk_attach(id, path, config_emulator.hard_disk_write_protected[id] != 0))
    {
        attach_error(path);
        return false;
    }

    notify_attached(id, path, "Hard disk attached");

    if (emu_is_empty() && emu_get_core()->GetFirmware()->IsReady())
        gui_action_power_on();

    return true;
}

bool gui_hard_disks_load_source(const char* path)
{
    emu_hard_disk_boot_freedos();
    int id = emu_hard_disk_get_free_id();

    if (id < 0)
    {
        gui_set_error_message("Every hard disk slot is in use.");
        return false;
    }

    return load_source(id, path, false);
}

void gui_hard_disks_source_loaded(void)
{
    const Emu_HardDiskSource* source = emu_hard_disk_get_prepared_source();
    bool protect = config_emulator.hard_disk_write_protected[source_id] != 0;
    bool built = (source->report.result != FAT_IMAGE_ERROR) && (source->report.result != FAT_IMAGE_TOO_BIG);
    bool reset = source_reset;
    bool rebuild = source_rebuild;
    source_loading = false;
    source_reset = false;
    source_rebuild = false;

    if (source->report.result == FAT_IMAGE_TOO_BIG)
    {
        char message[GDOX_MAX_PATH + 128];
        snprintf(message, sizeof(message), "This needs %u MB, and a FAT16 disk holds 2015 MB:\n%s",
            (u32)(source->report.content_size / (1024 * 1024)), source->path);
        gui_set_error_message(message);
    }
    else if (!built || !emu_hard_disk_attach_prepared_source(source_id, protect))
    {
        std::string message("Unable to make a hard disk from:\n");
        message += source->path;
        gui_set_error_message(message.c_str());
        built = false;
    }

    if (!built && !rebuild)
        return;

    if (built)
        notify_attached(source_id, source->path, "Hard disk attached");
    else
        emu_hard_disk_attach_prepared_source(source_id, protect);

    if (source->report.result == FAT_IMAGE_CHANGED)
        gui_notify(gui_NotificationWarning, ICON_MD_STORAGE, "This folder changed since its disk was made",
            "Rebuild From Folder picks that up, and drops what DOS wrote to the disk");

    if (source->report.left_out != 0)
    {
        char message[64];
        snprintf(message, sizeof(message), "%u items are too deep for DOS to reach", source->report.left_out);
        gui_notify(gui_NotificationWarning, ICON_MD_STORAGE, "Part of this folder was left out", message);
    }

    if (emu_is_empty() && emu_get_core()->GetFirmware()->IsReady())
        gui_action_power_on();
    else if (reset)
        gui_action_reset();
}

bool gui_hard_disks_is_loading_source(void)
{
    return source_loading;
}

void gui_hard_disks_reset_when_loaded(void)
{
    source_reset = true;
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
    if (emu_hard_disk_create(id, path, create_size_mb, create_formatted,
        config_emulator.hard_disk_write_protected[id] != 0))
        notify_attached(id, path, "Hard disk created");
    else
    {
        std::string message("Unable to create the hard disk image.\nAn existing file is never replaced:\n");
        message += path;
        gui_set_error_message(message.c_str());
    }
}

void gui_hard_disks_open_quit_confirmation(void)
{
    open_quit = true;
}

static void disk_menu(int id)
{
    Emu_HardDiskInfo info;
    emu_hard_disk_get_info(id, &info);
    const char* path = info.source[0] != 0 ? info.source : (info.pending ? info.pending_path : info.path);
    bool present = path[0] != 0;
    const char* name = present ? get_filename(path) : "Empty";
    char label[64];
    snprintf(label, sizeof(label), "%s: %.32s%s###hard_disk_%d", k_slot_names[id], name,
        strlen(name) > 32 ? "..." : "", id);

    if (!ImGui::BeginMenu(label))
        return;

    bool loading = gui_is_rom_loading() || emu_is_media_loading();

    if (ImGui::MenuItem("Attach...", NULL, false, !loading))
        gui_file_dialog_attach_hard_disk(id);

    if (ImGui::MenuItem("Attach Folder...", NULL, false, !loading))
        gui_file_dialog_attach_folder(id);

    if (ImGui::MenuItem("New Blank Disk...", NULL, false, !loading))
    {
        dialog_id = id;
        open_create = true;
    }

    if (ImGui::MenuItem("Rebuild From Folder...", NULL, false, !loading && (info.source[0] != 0)))
    {
        confirm_id = id;
        open_confirm = true;
    }

    if (ImGui::MenuItem("Detach", NULL, false, present))
    {
        emu_hard_disk_detach(id);
        Emu_HardDiskInfo detached;
        emu_hard_disk_get_info(id, &detached);
        gui_notify(gui_NotificationInfo, ICON_MD_EJECT, "Hard disk detached",
            detached.pending ? "At the next reset" : name);
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

    if (ImGui::MenuItem("Write Protected", NULL, &write_protected) &&
        !emu_hard_disk_set_write_protected(id, write_protected))
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

static void freedos_menu(void)
{
    bool loading = gui_is_rom_loading() || emu_is_media_loading();
    bool attached = false;

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        Emu_HardDiskInfo info;
        emu_hard_disk_get_info(i, &info);
        attached = attached || info.builtin;
    }

    if (ImGui::MenuItem("Attach FreeDOS Disk", NULL, false, !loading && !attached))
    {
        int id = emu_hard_disk_get_free_id();

        if (id < 0)
            gui_set_error_message("Every hard disk slot is in use.");
        else if (emu_hard_disk_attach_freedos(id))
            notify_attached(id, "FreeDOS", "Hard disk attached");
        else
            gui_set_error_message("Unable to make the FreeDOS disk.");
    }

    ImGui::MenuItem("Boot FreeDOS With No Disks", NULL, &config_emulator.hard_disk_freedos);

    if (ImGui::MenuItem("Recreate FreeDOS Disk...", NULL, false, !loading))
    {
        confirm_id = k_recreate_freedos;
        open_confirm = true;
    }
}

static bool load_source(int id, const char* path, bool rebuild)
{
    if (gui_is_rom_loading() || emu_is_media_loading())
        return false;

    if (emu_hard_disk_is_source_attached(path))
    {
        gui_set_error_message("This folder is attached already.");
        return false;
    }

    source_id = id;
    source_rebuild = rebuild;
    source_loading = gui_load_hard_disk_source(path, rebuild);
    return source_loading;
}

static void confirm_erase(void)
{
    if (gui_is_rom_loading() || emu_is_media_loading())
        return;

    bool powered = !emu_is_empty();

    if (powered)
        gui_action_power_off();

    if (confirm_id == k_recreate_freedos)
    {
        if (!emu_hard_disk_recreate_freedos())
            gui_set_error_message("Unable to make a new FreeDOS disk. The old one is untouched.");

        if (powered)
            gui_action_power_on();

        return;
    }

    Emu_HardDiskInfo info;
    emu_hard_disk_get_info(confirm_id, &info);
    std::string source = info.source;
    emu_get_core()->DetachHardDisk(confirm_id);
    load_source(confirm_id, source.c_str(), true);
}

static void attach(int id, const char* path)
{
    if (emu_hard_disk_is_source(path))
    {
        load_source(id, path, false);
        return;
    }

    if (emu_hard_disk_attach(id, path, config_emulator.hard_disk_write_protected[id] != 0))
        notify_attached(id, path, "Hard disk attached");
    else
        attach_error(path);
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
        ImGui::CloseCurrentPopup();
        gui_dialog_in_use = false;
        gui_file_dialog_create_hard_disk(dialog_id);
    }

    ImGui::SetItemDefaultFocus();
    ImGui::SameLine();

    if (ImGui::Button("Cancel", ImVec2(120, 0)))
    {
        ImGui::CloseCurrentPopup();
        gui_dialog_in_use = false;
    }

    ImGui::EndPopup();
}

static void draw_confirm_popup(void)
{
    gui_center_next_window();

    if (!ImGui::BeginPopupModal("Erase Hard Disk", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    if (confirm_id == k_recreate_freedos)
        ImGui::Text("This restarts the machine and makes a new FreeDOS disk.\n"
            "Everything installed on the old one is lost.\n\n");
    else
        ImGui::Text("This restarts the machine and makes a new disk from the folder.\n"
            "Everything DOS wrote to this disk is lost, saved games too.\n\n");

    ImGui::Separator();

    if (ImGui::Button("Erase", ImVec2(120, 0)))
    {
        ImGui::CloseCurrentPopup();
        gui_dialog_in_use = false;
        confirm_erase();
    }

    ImGui::SameLine();

    if (ImGui::Button("Cancel", ImVec2(120, 0)))
    {
        ImGui::CloseCurrentPopup();
        gui_dialog_in_use = false;
    }

    ImGui::SetItemDefaultFocus();
    ImGui::EndPopup();
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

    if (ImGui::Button("Retry", ImVec2(100, 0)))
    {
        if (emu_hard_disk_flush() && emu_floppy_flush())
        {
            ImGui::CloseCurrentPopup();
            gui_dialog_in_use = false;
            application_confirm_quit();
        }
        else
            gui_set_error_message("Unable to save the hard disk changes.");
    }

    ImGui::SameLine();

    if (ImGui::Button("Quit Without Saving", ImVec2(160, 0)))
    {
        ImGui::CloseCurrentPopup();
        gui_dialog_in_use = false;
        application_confirm_quit();
    }

    ImGui::SameLine();

    if (ImGui::Button("Cancel", ImVec2(100, 0)))
    {
        ImGui::CloseCurrentPopup();
        gui_dialog_in_use = false;
    }

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
