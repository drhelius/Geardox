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

#define GUI_MENUS_IMPORT
#include "gui_menus.h"
#include "gui.h"
#include "gui_filedialogs.h"
#include "gui_popups.h"
#include "gui_actions.h"
#include "gui_colors.h"
#include "gui_floppy.h"
#include "gui_hard_disks.h"
#include "gui_notifications.h"
#include "gui_shader.h"
#include "config.h"
#include "application.h"
#include "display.h"
#include "gamepad.h"
#include "sound_queue.h"
#include "emu.h"
#include "emu_game.h"
#include "emu_midi.h"
#include "midi_port.h"
#include "gui_game.h"
#include "gui_dos.h"
#include "ogl_renderer.h"
#include "ogl_shader_chain.h"
#include "shader_preset.h"
#include "utils.h"
#include "geardox.h"
#include "drive/hard_disk_image.h"
#include "rewind.h"
#include "events.h"
#include "game_focus.h"
#include "hotkeys.h"
#include "input_macro.h"

static bool open_rom = false;
static bool open_state = false;
static bool save_state = false;
static bool open_about = false;
static bool open_load_defaults = false;
static bool save_screenshot = false;
static bool save_vgm = false;
static bool save_video = false;
static bool choose_savestates_path = false;
static bool choose_savefiles_path = false;
static bool choose_screenshots_path = false;
static bool choose_video_recordings_path = false;
#if defined(GDOX_ENABLE_PHYSICAL_CDROM)
static bool open_physical_cdrom = false;
#endif
static const GuiColor& machine_warning_color = amber;
static const GuiColor& machine_game_color = cyan;
static config_Machine* menu_settings = &config_machine;
static std::vector<MidiPortInfo> midi_ports;
static const GuiColor& service_mcp_http_color = green;
static const GuiColor& service_mcp_stdio_color = amber;
static const GuiColor& midi_playing_color = green;
static const GuiColor& midi_stopped_color = red;
static const GuiColor& video_recording_color = red;
static const GuiColor& vgm_recording_color = red;

static void menu_geardox(void);
static void menu_drives(void);
static void menu_cdrom(void);
static void menu_emulator(void);
static void menu_machine(void);
static bool menu_machine_model(void);
static bool menu_machine_cpu_speed(const GDOX_Machine_Profile& profile);
static bool menu_machine_ram(const GDOX_Machine_Profile& profile);
static bool menu_machine_fpu(const GDOX_Machine_Profile& profile);
static bool menu_machine_sound(void);
static bool menu_machine_video(void);
static bool menu_machine_mouse(void);
static bool menu_machine_cdrom_drive(void);
static bool menu_machine_cdrom_speed(void);
static bool menu_machine_drive_timing(void);
static bool menu_machine_floppy(int drive);
static const char* get_machine_fpu_text(const GDOX_Machine_Profile& profile);
static void menu_machine_hardware(GDOX_Machine_Model model);
static void get_machine_hardware(GDOX_Machine_Model model, char* text, size_t text_size);
static void menu_boot_device(void);
static void draw_machine_profile_tooltip(GDOX_Machine_Model model);
static void get_machine_summary(const GDOX_Machine_Config& config, char* text, size_t text_size);
static float get_combo_items_width(const char* items);
static bool menu_machine_sound_resource(const char* name, const char* id, const u8* values, int* setting, bool enabled);
static void draw_directory_tooltip(int option, const char* custom, const char* about, bool media);
static void menu_video(void);
static void menu_input(void);
static void menu_audio(void);
static void menu_audio_source_mute(const char* label, int source);
static void menu_audio_source_volume(const char* label, const char* id, int source, float* volume);
static void menu_audio_midi(void);
static bool menu_audio_midi_port(const char* label, const char* id, std::string* port, const char* tooltip);
static void menu_audio_midi_game_synth(void);
static int find_midi_port(const char* port);
static void menu_debug(void);
static void menu_about(void);
static void draw_background_color_menu(const char* label, int theme);
static void draw_mcp_status(void);
static void draw_recording_status(const char* status, ImVec4 color, float dot_radius, float dot_width);
static void file_dialogs(void);
static bool media_menu_actions_enabled(void);
static const char* get_current_media_directory_text(void);
static void gamepad_configuration_item(const char* text, int* button, int player);
static void hotkey_configuration_item(const char* text, config_Hotkey* hotkey);
static void hotkey_configuration_menu(bool debug);
static void gamepad_device_selector(int player);
static void menu_input_gamepad(int pad);
static void gamepad_choice_selector(int pad, bool use);
static void gamepad_key_item(int pad, int input, float offset);
static void draw_savestate_slot_info(int slot);

void gui_init_menus(void)
{
    gui_shortcut_open_rom = false;
}

void gui_main_menu(void)
{
    open_rom = false;
    open_state = false;
    save_state = false;
    open_about = false;
    open_load_defaults = false;
    save_screenshot = false;
    save_vgm = false;
    save_video = false;
    choose_savestates_path = false;
    choose_savefiles_path = false;
    choose_screenshots_path = false;
    choose_video_recordings_path = false;
#if defined(GDOX_ENABLE_PHYSICAL_CDROM)
    open_physical_cdrom = false;
#endif

    if (application_show_menu && ImGui::BeginMainMenuBar())
    {
        menu_geardox();
        gui_game_menu();
        gui_dos_menu();
        menu_drives();
        menu_emulator();
        menu_video();
        menu_input();
        menu_audio();
        menu_debug();
        menu_about();
        draw_mcp_status();

        gui_main_menu_height = (int)ImGui::GetWindowSize().y;

        ImGui::EndMainMenuBar();
    }

    file_dialogs();
}

static void menu_geardox(void)
{
    if (ImGui::BeginMenu(GDOX_TITLE))
    {
        gui_in_use = true;
        bool media_actions_enabled = media_menu_actions_enabled();
        bool loading = gui_is_rom_loading() || emu_is_media_loading();
        bool powered_on = !emu_is_empty();

        if (ImGui::MenuItem("Power On", config_hotkeys[config_HotkeyIndex_PowerOn].str, false, !loading && !powered_on))
        {
            gui_action_power_on();
        }

        if (ImGui::MenuItem("Power Off", config_hotkeys[config_HotkeyIndex_PowerOff].str, false, !loading && powered_on))
        {
            gui_action_power_off();
        }

        if (ImGui::MenuItem("Reset", config_hotkeys[config_HotkeyIndex_Reset].str, false, media_actions_enabled))
        {
            gui_action_reset();
        }

        if (ImGui::BeginMenu("Reset and Boot From", media_actions_enabled))
        {
            menu_boot_device();
            ImGui::EndMenu();
        }

        if (ImGui::MenuItem("Pause", config_hotkeys[config_HotkeyIndex_Pause].str, &config_emulator.paused, media_actions_enabled))
        {
            gui_action_pause();
        }

        ImGui::Separator();

        if (ImGui::BeginMenu("Fast Forward"))
        {
            if (ImGui::MenuItem("Enabled", config_hotkeys[config_HotkeyIndex_FFWD].str, &config_emulator.ffwd, media_actions_enabled))
            {
                gui_action_ffwd();
            }

            if (ImGui::BeginMenu("Speed"))
            {
                const char* speeds = "X 1.5\0X 2\0X 2.5\0X 3\0Unlimited\0\0";
                ImGui::PushItemWidth(get_combo_items_width(speeds));
                ImGui::Combo("##fwd", &config_emulator.ffwd_speed, speeds);
                ImGui::PopItemWidth();
                ImGui::EndMenu();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Rewind"))
        {
            if (ImGui::MenuItem("Enabled", config_hotkeys[config_HotkeyIndex_Rewind].str, &config_rewind.enabled))
            {
                rewind_reset();
            }

            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("Rewind uses save states while it runs, which costs performance.");
                ImGui::Text("If you don't use rewind, keep it disabled.");
                ImGui::EndTooltip();
            }

            ImGui::PushItemWidth(140.0f);
            ImGui::SliderFloat("Speed", &config_rewind.speed, 1.0f, 8.0f, "%.0fx");
            ImGui::PopItemWidth();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Run-Ahead"))
        {
            const char* frames = "Disabled\0" "1 Frame\0" "2 Frames\0" "3 Frames\0\0";
            ImGui::PushItemWidth(get_combo_items_width(frames));
            ImGui::Combo("##runahead", &config_emulator.runahead, frames);
            ImGui::PopItemWidth();

            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("Reduces input lag by speculatively running extra frames each update.");
                ImGui::Text("Every frame multiplies CPU cost, so use the lowest value that feels right.");
                ImGui::Text("Ignored while fast-forwarding or using the debugger.");
                ImGui::EndTooltip();
            }

            ImGui::EndMenu();
        }

        ImGui::Separator();

        if (ImGui::BeginMenu("Savestates"))
        {
            if (ImGui::MenuItem("Save As...", "", false, media_actions_enabled))
                save_state = true;

            if (ImGui::MenuItem("Load From...", "", false, media_actions_enabled))
                open_state = true;

            ImGui::Separator();

            if (ImGui::BeginMenu("Slot"))
            {
                const char* slots = "Slot 1\0Slot 2\0Slot 3\0Slot 4\0Slot 5\0\0";
                ImGui::PushItemWidth(get_combo_items_width(slots));
                ImGui::Combo("##slot", &config_emulator.save_slot, slots);
                ImGui::PopItemWidth();

                ImGui::Separator();
                draw_savestate_slot_info(config_emulator.save_slot);
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Save", config_hotkeys[config_HotkeyIndex_SaveState].str, false, media_actions_enabled))
            {
                gui_action_save_state(NULL);
            }

            if (ImGui::MenuItem("Load", config_hotkeys[config_HotkeyIndex_LoadState].str, false, media_actions_enabled))
            {
                gui_action_load_state(NULL);
            }

            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("Slot: %d", config_emulator.save_slot + 1);
                ImGui::Separator();
                draw_savestate_slot_info(config_emulator.save_slot);
                ImGui::EndTooltip();
            }

            ImGui::EndMenu();
        }

        ImGui::Separator();

        if (ImGui::BeginMenu("Screenshots"))
        {
            if (ImGui::MenuItem("Save As...", "", false, media_actions_enabled))
            {
                save_screenshot = true;
            }

            if (ImGui::MenuItem("Save", config_hotkeys[config_HotkeyIndex_Screenshot].str, false, media_actions_enabled))
            {
                gui_action_save_screenshot(NULL);
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Video Recording"))
        {
            bool is_recording = emu_is_video_recording();

            if (ImGui::MenuItem("Start Recording As...", "", false, !is_recording && media_actions_enabled))
            {
                save_video = true;
            }

            if (ImGui::MenuItem("Start Recording", config_hotkeys[config_HotkeyIndex_VideoRecording].str, false,
                !is_recording && media_actions_enabled))
            {
                gui_action_start_video_recording(NULL);
            }

            if (ImGui::MenuItem("Stop Recording", config_hotkeys[config_HotkeyIndex_VideoRecording].str, false, is_recording))
            {
                gui_action_stop_video_recording();
            }

            ImGui::Separator();

            if (ImGui::BeginMenu("Scale", !is_recording))
            {
                ImGui::PushItemWidth(100.0f);
                ImGui::SliderInt("##video_recording_scale", &config_video.recording_scale, 1, 20, "%dx");
                ImGui::PopItemWidth();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Aspect Ratio", !is_recording))
            {
                const char* ratios = "Follow Screen\0Square Pixels (1:1 PAR)\0Standard (4:3 DAR)\0Wide (16:9 DAR)\0\0";
                ImGui::PushItemWidth(get_combo_items_width(ratios));
                ImGui::Combo("##video_recording_ratio", &config_video.recording_ratio, ratios);
                ImGui::PopItemWidth();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Quality", !is_recording))
            {
                const char* qualities = "Low\0Medium\0High\0Lossless (Uncompressed)\0Lossless (Compressed)\0\0";
                ImGui::PushItemWidth(get_combo_items_width(qualities));
                ImGui::Combo("##video_recording_quality", &config_video.recording_quality, qualities);
                ImGui::PopItemWidth();
                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    ImGui::Text("Low and Medium halve the color resolution, best used at 2x or higher.");
                    ImGui::Text("Lossless (Uncompressed) produces very large files.");
                    ImGui::Text("Lossless (Compressed) stores PNG frames, it needs an FFmpeg based player like VLC or mpv.");
                    ImGui::EndTooltip();
                }
                ImGui::EndMenu();
            }

            ImGui::EndMenu();
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Load Default Settings", NULL, false, !gui_is_rom_loading() && !emu_is_media_loading()))
        {
            open_load_defaults = true;
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Quit", config_hotkeys[config_HotkeyIndex_Quit].str))
        {
            application_trigger_quit();
        }

        ImGui::EndMenu();
    }
}

static void menu_drives(void)
{
    if (ImGui::BeginMenu("Drives"))
    {
        gui_in_use = true;
        GeardoxCore* core = emu_get_core();
        int floppy_drives = emu_is_empty() ? core->GetPendingMachineConfig().floppy_drives :
            core->GetMachineConfig().floppy_drives;

        menu_cdrom();

        if (floppy_drives > 0)
            gui_floppy_menu(floppy_drives);

        gui_hard_disks_menu();

        ImGui::EndMenu();
    }
}

static void menu_cdrom(void)
{
    if (!ImGui::BeginMenu("CD-ROM"))
        return;

    Media* media = emu_get_core()->GetMedia();
    bool loading = gui_is_rom_loading() || emu_is_media_loading();
    bool inserted = !loading && media->IsReady();
    bool can_insert = !loading && !media->IsReady();

    if (ImGui::MenuItem("Insert Image...", config_hotkeys[config_HotkeyIndex_OpenROM].str, false, can_insert))
    {
        open_rom = true;
    }

#if defined(GDOX_ENABLE_PHYSICAL_CDROM)
    if (ImGui::MenuItem("Insert Physical Disc...", NULL, false, can_insert))
    {
        open_physical_cdrom = true;
    }
#endif

    if (ImGui::MenuItem("Eject", NULL, false, inserted))
    {
        gui_action_eject_media();
    }

    int discs = emu_cdrom_playlist_get_count();

    if ((discs > 0) && ImGui::BeginMenu("Disc in Playlist", can_insert))
    {
        for (int i = 0; i < discs; i++)
        {
            ImGui::PushID(i);

            if (ImGui::MenuItem(emu_cdrom_playlist_get_name(i)))
                gui_load_playlist_disc(i);

            ImGui::PopID();
        }

        ImGui::EndMenu();
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Preload CD-ROM in RAM", "", &config_emulator.preload_cdrom))
    {
        emu_set_preload_cdrom(config_emulator.preload_cdrom);
    }

    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::Text("This option will preload all CD-ROM tracks in RAM.");
        ImGui::Text("Load a new CD-ROM image to apply changes.");
        ImGui::EndTooltip();
    }

    ImGui::Separator();

    if (loading)
        ImGui::TextDisabled("Loading...");
    else if (!inserted)
        ImGui::TextDisabled("Empty");
    else
    {
        const char* name = media->GetFileName();
        const char* path = media->GetFilePath();

#if defined(GDOX_ENABLE_PHYSICAL_CDROM)
        if (media->IsPhysicalCdRom())
            name = path = media->GetPhysicalCdRomDeviceId();
#endif

        ImGui::Text("%.32s%s", name, strlen(name) > 32 ? "..." : "");

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", path);
    }

    ImGui::Separator();

    if (ImGui::BeginMenu("Recent", can_insert))
    {
        for (int i = 0; i < config_max_recent_roms; i++)
        {
            if (config_emulator.recent_roms[i].length() > 0)
            {
                if (ImGui::MenuItem(config_emulator.recent_roms[i].c_str()))
                {
                    char media_path[4096];
                    strncpy_fit(media_path, config_emulator.recent_roms[i].c_str(), sizeof(media_path));
                    gui_load_rom(media_path);
                }
            }
        }

        ImGui::EndMenu();
    }

    ImGui::EndMenu();
}

static void draw_directory_tooltip(int option, const char* custom, const char* about, bool media)
{
    if (!ImGui::IsItemHovered())
        return;

    ImGui::BeginTooltip();

    if (IsValidPointer(about))
        ImGui::Text("%s", about);

    switch ((Directory_Location)option)
    {
        case Directory_Location_Default:
            ImGui::Text("%s", config_root_path);
            break;
        case Directory_Location_ROM:
            if (!media)
                ImGui::Text("Next to each disk image.");
            else if (emu_is_empty())
                ImGui::Text("The folder of the disk or CD in use.");
            else
                ImGui::Text("%s", get_current_media_directory_text());
            break;
        case Directory_Location_Custom:
            ImGui::Text("%s", custom);
            break;
    }

    ImGui::EndTooltip();
}

static bool media_menu_actions_enabled(void)
{
    if (emu_is_empty())
        return false;

#if defined(GDOX_ENABLE_PHYSICAL_CDROM)
    if (emu_get_core()->GetMedia()->HasPhysicalCdRomError())
        return false;
#endif

    return true;
}

static void menu_emulator(void)
{
    if (ImGui::BeginMenu("Emulator"))
    {
        gui_in_use = true;

        menu_machine();

        ImGui::Separator();

        if (ImGui::BeginMenu("Directories"))
        {
            if (ImGui::BeginMenu("Save States"))
            {
                const char* locations = "Default Location\0Same as ROM\0Custom Location\0\0";
                ImGui::PushItemWidth(get_combo_items_width(locations));

                if (ImGui::Combo("##savestate_option", &config_emulator.savestates_dir_option, locations))
                {
                    update_savestates_data();
                }

                draw_directory_tooltip(config_emulator.savestates_dir_option, config_emulator.savestates_path.c_str(), NULL, true);
                ImGui::PopItemWidth();

                switch ((Directory_Location)config_emulator.savestates_dir_option)
                {
                    case Directory_Location_Default:
                    case Directory_Location_ROM:
                        break;

                    case Directory_Location_Custom:
                    {
                        if (ImGui::MenuItem("Choose..."))
                            choose_savestates_path = true;

                        ImGui::PushItemWidth(450);

                        if (ImGui::InputText("##savestate_path", gui_savestates_path, IM_ARRAYSIZE(gui_savestates_path),
                            ImGuiInputTextFlags_AutoSelectAll))
                        {
                            config_emulator.savestates_path.assign(gui_savestates_path);
                            update_savestates_data();
                        }

                        ImGui::PopItemWidth();
                        break;
                    }
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Save Files"))
            {
                const char* locations = "Default Location\0Same as Disk\0Custom Location\0\0";
                ImGui::PushItemWidth(get_combo_items_width(locations));
                ImGui::Combo("##savefiles_option", &config_emulator.savefiles_dir_option, locations);
                draw_directory_tooltip(config_emulator.savefiles_dir_option, config_emulator.savefiles_path.c_str(),
                    "Working copies of floppies and CHD disks,\nand each machine's CMOS. With Same as Disk,\n"
                    "the CMOS stays in the default location.", false);
                ImGui::PopItemWidth();

                switch ((Directory_Location)config_emulator.savefiles_dir_option)
                {
                    case Directory_Location_Default:
                    case Directory_Location_ROM:
                        break;

                    case Directory_Location_Custom:
                    {
                        if (ImGui::MenuItem("Choose..."))
                            choose_savefiles_path = true;

                        ImGui::PushItemWidth(450);

                        if (ImGui::InputText("##savefiles_path", gui_savefiles_path, IM_ARRAYSIZE(gui_savefiles_path),
                            ImGuiInputTextFlags_AutoSelectAll))
                        {
                            config_emulator.savefiles_path.assign(gui_savefiles_path);
                        }

                        ImGui::PopItemWidth();
                        break;
                    }
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Screenshots"))
            {
                const char* locations = "Default Location\0Same as ROM\0Custom Location\0\0";
                ImGui::PushItemWidth(get_combo_items_width(locations));
                ImGui::Combo("##screenshots_option", &config_emulator.screenshots_dir_option, locations);
                draw_directory_tooltip(config_emulator.screenshots_dir_option, config_emulator.screenshots_path.c_str(), NULL, true);
                ImGui::PopItemWidth();

                switch ((Directory_Location)config_emulator.screenshots_dir_option)
                {
                    case Directory_Location_Default:
                    case Directory_Location_ROM:
                        break;

                    case Directory_Location_Custom:
                    {
                        if (ImGui::MenuItem("Choose..."))
                        {
                            choose_screenshots_path = true;
                        }

                        ImGui::PushItemWidth(450);

                        if (ImGui::InputText("##screenshots_path", gui_screenshots_path, IM_ARRAYSIZE(gui_screenshots_path), ImGuiInputTextFlags_AutoSelectAll))
                        {
                            config_emulator.screenshots_path.assign(gui_screenshots_path);
                        }

                        ImGui::PopItemWidth();
                        break;
                    }
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Video Recordings"))
            {
                const char* locations = "Default Location\0Same as ROM\0Custom Location\0\0";
                ImGui::PushItemWidth(get_combo_items_width(locations));
                ImGui::Combo("##video_recordings_option", &config_emulator.video_recordings_dir_option, locations);
                draw_directory_tooltip(config_emulator.video_recordings_dir_option, config_emulator.video_recordings_path.c_str(), NULL, true);
                ImGui::PopItemWidth();

                switch ((Directory_Location)config_emulator.video_recordings_dir_option)
                {
                    case Directory_Location_Default:
                    case Directory_Location_ROM:
                        break;

                    case Directory_Location_Custom:
                    {
                        if (ImGui::MenuItem("Choose..."))
                        {
                            choose_video_recordings_path = true;
                        }

                        ImGui::PushItemWidth(450);

                        if (ImGui::InputText("##video_recordings_path", gui_video_recordings_path, IM_ARRAYSIZE(gui_video_recordings_path), ImGuiInputTextFlags_AutoSelectAll))
                        {
                            config_emulator.video_recordings_path.assign(gui_video_recordings_path);
                        }

                        ImGui::PopItemWidth();
                        break;
                    }
                }

                ImGui::EndMenu();
            }

            ImGui::EndMenu();
        }

        ImGui::Separator();

        ImGui::MenuItem("Power On at Startup", "", &config_emulator.power_on_startup);
        ImGui::MenuItem("Start Paused", "", &config_emulator.start_paused);
        ImGui::MenuItem("Pause When Inactive", "", &config_emulator.pause_when_inactive);

        if (ImGui::MenuItem("Allow Screen Saver", "", &config_emulator.allow_screensaver))
        {
            if (config_emulator.allow_screensaver)
                SDL_EnableScreenSaver();
            else
                SDL_DisableScreenSaver();
        }

        ImGui::Separator();

        //ImGui::MenuItem("Show Media Info", "", &config_emulator.show_info);
        ImGui::MenuItem("Show Notifications", "", &config_emulator.show_notifications);

        if (ImGui::BeginMenu("Drive Activity"))
        {
            ImGui::MenuItem("Enabled", "", &config_emulator.show_drive_activity);
            ImGui::Separator();
            ImGui::MenuItem("CD-ROM", "", &config_emulator.show_drive_activity_cdrom, config_emulator.show_drive_activity);
            ImGui::MenuItem("Hard Disk", "", &config_emulator.show_drive_activity_hard_disk, config_emulator.show_drive_activity);
            ImGui::MenuItem("Floppy", "", &config_emulator.show_drive_activity_floppy, config_emulator.show_drive_activity);
            ImGui::EndMenu();
        }

        ImGui::Separator();

        if (ImGui::BeginMenu("Hotkeys"))
        {
            hotkey_configuration_menu(false);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Debug Hotkeys"))
        {
            hotkey_configuration_menu(true);
            ImGui::EndMenu();
        }

        ImGui::Separator();

        ImGui::MenuItem("Single Instance", "", &config_debug.single_instance);

        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("RESTART REQUIRED");
            ImGui::NewLine();
            ImGui::Text("When enabled, opening media while another instance is running");
            ImGui::Text("will send the media to the running instance instead of");
            ImGui::Text("starting a new one.");
            ImGui::EndTooltip();
        }

        ImGui::EndMenu();
    }
}

static void menu_machine(void)
{
    if (!ImGui::BeginMenu("Machine"))
        return;

    menu_settings = emu_game_get_machine();

    GeardoxCore* core = emu_get_core();
    bool running = !emu_is_empty();
    const GDOX_Machine_Config& machine = running ? core->GetMachineConfig() : core->GetPendingMachineConfig();
    const GDOX_Machine_Report& report = core->GetPendingMachineReport();
    const GDOX_Machine_Profile& profile = k_machine_profiles[core->GetPendingMachineConfig().model];
    bool changed = false;
    char summary[128];

    get_machine_summary(machine, summary, sizeof(summary));
    ImGui::TextDisabled("%s", summary);

    if (running && core->IsMachineConfigPending())
    {
        char pending[128];
        get_machine_summary(core->GetPendingMachineConfig(), pending, sizeof(pending));

        if (strcmp(pending, summary) != 0)
            ImGui::TextDisabled("After reset: %s", pending);
    }

    if (emu_game_is_open())
        ImGui::TextColored(machine_game_color, "%s: changes here are this game's", emu_game_get_name());

    for (int i = 0; i < report.warning_count; i++)
        ImGui::TextColored(machine_warning_color, "%s", report.warnings[i]);

    ImGui::Separator();

    if (menu_machine_model())
        changed = true;

    if (menu_machine_cpu_speed(profile))
        changed = true;

    if (menu_machine_ram(profile))
        changed = true;

    if (menu_machine_fpu(profile))
        changed = true;

    if (menu_machine_floppy(0))
        changed = true;

    if (menu_machine_floppy(1))
        changed = true;

    if (menu_machine_sound())
        changed = true;

    if (menu_machine_video())
        changed = true;

    if (menu_machine_mouse())
        changed = true;

    ImGui::BeginDisabled(emu_game_is_open());

    if (menu_machine_cdrom_drive())
        changed = true;

    ImGui::EndDisabled();

    if (menu_machine_cdrom_speed())
        changed = true;

    if (menu_machine_drive_timing())
        changed = true;

    menu_machine_hardware(core->GetPendingMachineConfig().model);

    ImGui::Separator();

    bool turbo = core->IsTurbo();

    if (ImGui::MenuItem("Turbo", config_hotkeys[config_HotkeyIndex_Turbo].str, &turbo))
    {
        emu_set_turbo(turbo);
        menu_settings->turbo = turbo;

        if (emu_game_is_open())
            emu_game_apply_machine();
    }

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The board's Turbo button: off runs the CPU\nat a third of its clock, for games too fast.\n"
            "Applies at once.");

    if (ImGui::MenuItem("BIOS Boot Menu", "", &config_emulator.bios_boot_menu))
        emu_apply_bios_settings();

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Esc opens the BIOS boot menu for 2.5 seconds\nat each boot. Reset the emulator to apply changes.");

#if !defined(GDOX_DISABLE_DYNAREC)

    ImGui::Separator();

    if (ImGui::MenuItem("CPU Dynarec", "", &config_emulator.dynarec))
        emu_set_dynarec(config_emulator.dynarec);

    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::Text("Dynamic recompilation: most games run faster,");
        ImGui::Text("a few slower. The debugger uses the interpreter");
        ImGui::Text("while a breakpoint, trace or profile is on.");

        if (config_emulator.dynarec && !emu_is_dynarec_enabled())
            ImGui::TextColored(red, "Not available here, the interpreter runs.");

        ImGui::EndTooltip();
    }
#endif

    if (changed)
        emu_apply_machine_settings();

    if (running && core->IsMachineConfigPending())
    {
        ImGui::Separator();

        if (ImGui::MenuItem("Reset to Apply Changes"))
            gui_action_reset();
    }

    ImGui::EndMenu();
}

static bool menu_machine_model(void)
{
    if (!ImGui::BeginMenu("Model"))
        return false;

    bool changed = false;
    float name_width = 0.0f;
    float cpu_width = 0.0f;
    char cpu[32];
    char preview[64];

    for (int i = 0; i < GDOX_MACHINE_RELEASE_ORDER_COUNT; i++)
    {
        const GDOX_Machine_Profile& profile = k_machine_profiles[k_machine_release_order[i]];
        snprintf(cpu, sizeof(cpu), "%s %d MHz", k_machine_cpus[profile.cpu].name, (int)(profile.cpu_clock_rate / 1000000));
        name_width = MAX(name_width, ImGui::CalcTextSize(profile.name).x);
        cpu_width = MAX(cpu_width, ImGui::CalcTextSize(cpu).x);
    }

    float cpu_column = name_width + ImGui::GetStyle().ItemSpacing.x * 4.0f;

    snprintf(preview, sizeof(preview), "%s", k_machine_profiles[menu_settings->model].name);

    ImGui::PushItemWidth(gui_get_combo_width(MAX(cpu_column + cpu_width, ImGui::CalcTextSize(preview).x)));

    if (ImGui::BeginCombo("##machine_model", preview))
    {
        for (int i = 0; i < GDOX_MACHINE_RELEASE_ORDER_COUNT; i++)
        {
            GDOX_Machine_Model model = k_machine_release_order[i];
            const GDOX_Machine_Profile& profile = k_machine_profiles[model];
            snprintf(cpu, sizeof(cpu), "%s %d MHz", k_machine_cpus[profile.cpu].name, (int)(profile.cpu_clock_rate / 1000000));

            ImGui::BeginDisabled(!profile.emulated);

            if (ImGui::Selectable(profile.name, menu_settings->model == model))
            {
                menu_settings->model = model;
                changed = true;
            }

            bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
            ImGui::SameLine(cpu_column);
            ImGui::TextDisabled("%s", cpu);
            ImGui::EndDisabled();

            if (hovered)
                draw_machine_profile_tooltip(model);
        }

        ImGui::EndCombo();
    }
    else if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::Text("The 486DX2-66 suits most games of 1990 to 1995.");
        ImGui::Text("Too fast? A 386, or Turbo off.");
        ImGui::Text("Reset the emulator to apply changes.");
        ImGui::EndTooltip();
    }

    ImGui::PopItemWidth();
    ImGui::EndMenu();
    return changed;
}

static bool menu_machine_cpu_speed(const GDOX_Machine_Profile& profile)
{
    if (!ImGui::BeginMenu("CPU Speed"))
        return false;

    bool changed = false;
    int original = (int)(profile.cpu_clock_rate / 1000000);
    int in_use = (int)(emu_get_core()->GetPendingMachineConfig().cpu_clock_rate / 1000000);
    float text_width = 0.0f;
    char labels[GDOX_MACHINE_CPU_SPEED_COUNT][32];
    char auto_label[32];
    snprintf(auto_label, sizeof(auto_label), "Model Default (%d MHz)", menu_settings->cpu_mhz == GDOX_MACHINE_AUTO ? in_use : original);
    const char* preview = auto_label;
    text_width = ImGui::CalcTextSize(auto_label).x;

    for (int i = 0; i < GDOX_MACHINE_CPU_SPEED_COUNT; i++)
    {
        int speed = k_machine_cpu_speeds_mhz[i];
        snprintf(labels[i], sizeof(labels[i]), speed == original ? "%d MHz (Original)" : "%d MHz", speed);
        text_width = MAX(text_width, ImGui::CalcTextSize(labels[i]).x);

        if (speed == menu_settings->cpu_mhz)
            preview = labels[i];
    }

    ImGui::PushItemWidth(gui_get_combo_width(text_width));

    if (ImGui::BeginCombo("##machine_cpu_speed", preview))
    {
        if (ImGui::Selectable(auto_label, menu_settings->cpu_mhz == GDOX_MACHINE_AUTO))
        {
            menu_settings->cpu_mhz = GDOX_MACHINE_AUTO;
            changed = true;
        }

        ImGui::Separator();

        for (int i = 0; i < GDOX_MACHINE_CPU_SPEED_COUNT; i++)
        {
            if (ImGui::Selectable(labels[i], menu_settings->cpu_mhz == k_machine_cpu_speeds_mhz[i]))
            {
                menu_settings->cpu_mhz = k_machine_cpu_speeds_mhz[i];
                changed = true;
            }
        }

        ImGui::EndCombo();
    }
    else if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::Text("Model Default: the model's original clock.");
        ImGui::Text("Devices keep their real timing at any speed.");
        ImGui::Text("Reset the emulator to apply changes.");
        ImGui::EndTooltip();
    }

    ImGui::PopItemWidth();
    ImGui::EndMenu();
    return changed;
}

static bool menu_machine_ram(const GDOX_Machine_Profile& profile)
{
    if (!ImGui::BeginMenu("Main RAM"))
        return false;

    GeardoxCore* core = emu_get_core();
    bool changed = false;
    int in_use = (int)(core->GetPendingMachineConfig().ram_size / (1024 * 1024));
    float text_width = 0.0f;
    char labels[GDOX_MACHINE_RAM_SIZE_COUNT][32];
    char preview[96] = "";

    for (int i = 0; i < GDOX_MACHINE_RAM_SIZE_COUNT; i++)
    {
        int size = k_machine_ram_sizes_mb[i];
        snprintf(labels[i], sizeof(labels[i]), size == k_machine_default_ram_mb ? "%d MB (Default)" : "%d MB", size);
        text_width = MAX(text_width, ImGui::CalcTextSize(labels[i]).x);

        if (size == menu_settings->ram_mb)
            snprintf(preview, sizeof(preview), "%s", labels[i]);
    }

    if (menu_settings->ram_mb != in_use)
        snprintf(preview, sizeof(preview), "%d MB, %d MB on the %s", menu_settings->ram_mb, in_use, profile.name);

    ImGui::PushItemWidth(gui_get_combo_width(MAX(text_width, ImGui::CalcTextSize(preview).x)));

    if (ImGui::BeginCombo("##machine_ram", preview))
    {
        for (int i = 0; i < GDOX_MACHINE_RAM_SIZE_COUNT; i++)
        {
            if (ImGui::Selectable(labels[i], menu_settings->ram_mb == k_machine_ram_sizes_mb[i]))
            {
                menu_settings->ram_mb = k_machine_ram_sizes_mb[i];
                changed = true;
            }
        }

        ImGui::EndCombo();
    }
    else if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::Text("8 MB runs nearly every DOS game.");
        ImGui::Text("Sizes a model can't take round up.");
        ImGui::Text("Save states and rewind grow with it.");
        ImGui::Text("Reset the emulator to apply changes.");
        ImGui::EndTooltip();
    }

    ImGui::PopItemWidth();
    ImGui::EndMenu();
    return changed;
}

static bool menu_machine_floppy(int drive)
{
    static const char* const labels[GDOX_FLOPPY_TYPE_COUNT] =
    {
        "Auto", "360 KB 5.25", "1.2 MB 5.25", "720 KB 3.5", "1.44 MB 3.5", "2.88 MB 3.5"
    };

    if (!ImGui::BeginMenu(drive == 0 ? "Floppy A" : "Floppy B"))
        return false;

    bool changed = false;
    int& type = menu_settings->floppy_type[drive];

    float text_width = 0.0f;

    for (int i = 0; i < GDOX_FLOPPY_TYPE_COUNT; i++)
        text_width = MAX(text_width, ImGui::CalcTextSize(labels[i]).x);

    ImGui::PushItemWidth(gui_get_combo_width(text_width));

    if (ImGui::BeginCombo(drive == 0 ? "##machine_floppy_a" : "##machine_floppy_b", labels[type]))
    {
        for (int i = 0; i < GDOX_FLOPPY_TYPE_COUNT; i++)
        {
            if (ImGui::Selectable(labels[i], type == i))
            {
                type = i;
                changed = true;
            }

            if (i == 0)
                ImGui::Separator();
        }

        ImGui::EndCombo();
    }
    else if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::Text("Auto: the smallest drive for the disk at power on.");
        ImGui::Text("A 720 KB disk gets a 1.44 MB drive,");
        ImGui::Text("which reads both.");
        ImGui::Text("Reset the emulator to apply changes.");
        ImGui::EndTooltip();
    }

    ImGui::PopItemWidth();
    ImGui::EndMenu();
    return changed;
}

static bool menu_machine_sound(void)
{
    if (!ImGui::BeginMenu("Sound Card"))
        return false;

    bool changed = false;
    int& card = menu_settings->sound_card;

    float text_width = 0.0f;

    for (int i = 0; i < GDOX_SOUND_CARD_COUNT; i++)
        text_width = MAX(text_width, ImGui::CalcTextSize(k_sound_cards[i].name).x);

    ImGui::PushItemWidth(gui_get_combo_width(text_width));

    if (ImGui::BeginCombo("##machine_sound_card", k_sound_cards[card].name))
    {
        for (int i = 0; i < GDOX_SOUND_CARD_COUNT; i++)
        {
            if (ImGui::Selectable(k_sound_cards[i].name, card == i))
            {
                card = i;
                changed = true;
            }
        }

        ImGui::EndCombo();
    }
    else if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::Text("Games detect the card.");
        ImGui::Text("The Sound Blaster 16 suits nearly all.");
        ImGui::Text("Reset the emulator to apply changes.");
        ImGui::EndTooltip();
    }

    ImGui::PopItemWidth();
    ImGui::Separator();

    if (menu_machine_sound_resource("IRQ", "##machine_sound_irq", k_sound_card_irqs, &menu_settings->sound_irq,
        k_sound_cards[card].dsp_major != 0))
        changed = true;

    if (menu_machine_sound_resource("DMA", "##machine_sound_dma", k_sound_card_dmas, &menu_settings->sound_dma,
        k_sound_cards[card].dsp_major != 0))
        changed = true;

    ImGui::Separator();
    bool mpu = menu_settings->mpu != 0;

    if (ImGui::MenuItem("MPU-401", "", &mpu))
    {
        menu_settings->mpu = mpu ? 1 : 0;
        changed = true;
    }

    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();

        if (k_sound_cards[card].mpu)
            ImGui::Text("The card's MIDI port at 330, on its IRQ.");
        else
            ImGui::Text("A separate MPU-401 at 330, on IRQ 2 (9).");

        ImGui::Text("Its music plays to Audio > MIDI Out.");
        ImGui::Text("Reset the emulator to apply changes.");
        ImGui::EndTooltip();
    }

    ImGui::EndMenu();
    return changed;
}

static bool menu_machine_sound_resource(const char* name, const char* id, const u8* values, int* setting, bool enabled)
{
    if (!ImGui::BeginMenu(name, enabled))
        return false;

    bool changed = false;
    char labels[2][16];
    int selected = 0;
    float text_width = 0.0f;

    for (int i = 0; i < 2; i++)
    {
        snprintf(labels[i], sizeof(labels[i]), "%s %d", name, values[i]);
        text_width = MAX(text_width, ImGui::CalcTextSize(labels[i]).x);

        if (*setting == values[i])
            selected = i;
    }

    ImGui::PushItemWidth(gui_get_combo_width(text_width));

    if (ImGui::BeginCombo(id, labels[selected]))
    {
        for (int i = 0; i < 2; i++)
        {
            if (ImGui::Selectable(labels[i], selected == i))
            {
                *setting = values[i];
                changed = true;
            }
        }

        ImGui::EndCombo();
    }
    else if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::Text("For old games that need a fixed IRQ or DMA.");
        ImGui::Text("BLASTER in the built-in disks' AUTOEXEC.BAT");
        ImGui::Text("doesn't follow it.");
        ImGui::Text("Reset the emulator to apply changes.");
        ImGui::EndTooltip();
    }

    ImGui::PopItemWidth();
    ImGui::EndMenu();
    return changed;
}

static bool menu_machine_video(void)
{
    if (!ImGui::BeginMenu("Video Card"))
        return false;

    bool changed = false;
    int& card = menu_settings->video_card;
    const GDOX_Machine_Profile& profile = k_machine_profiles[emu_get_core()->GetPendingMachineConfig().model];
    bool local_bus = (profile.features & GDOX_MACHINE_FEATURE_LOCAL_BUS) != 0;
    char default_label[64];
    snprintf(default_label, sizeof(default_label), "Model Default (%s)",
        k_video_cards[local_bus ? GDOX_VIDEO_CARD_GD5428_VLB : GDOX_VIDEO_CARD_GD5428].name);
    const char* preview = card == GDOX_MACHINE_AUTO ? default_label : k_video_cards[card].name;

    float text_width = ImGui::CalcTextSize(default_label).x;

    for (int i = 0; i < GDOX_VIDEO_CARD_COUNT; i++)
        text_width = MAX(text_width, ImGui::CalcTextSize(k_video_cards[i].name).x);

    ImGui::PushItemWidth(gui_get_combo_width(text_width));

    if (ImGui::BeginCombo("##machine_video_card", preview))
    {
        if (ImGui::Selectable(default_label, card == GDOX_MACHINE_AUTO))
        {
            card = GDOX_MACHINE_AUTO;
            changed = true;
        }

        ImGui::Separator();

        for (int i = 0; i < GDOX_VIDEO_CARD_COUNT; i++)
        {
            if (ImGui::Selectable(k_video_cards[i].name, card == i))
            {
                card = i;
                changed = true;
            }
        }

        ImGui::EndCombo();
    }
    else if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::Text("Model Default: a Cirrus Logic GD5428,");
        ImGui::Text("on the VL-Bus on 486s, ISA otherwise.");
        ImGui::Text("Models without a local bus get the ISA card.");
        ImGui::Text("The IBM CGA and EGA are 8-bit cards for older games.");
        ImGui::Text("Reset the emulator to apply changes.");
        ImGui::EndTooltip();
    }

    ImGui::PopItemWidth();
    ImGui::EndMenu();
    return changed;
}

static bool menu_machine_mouse(void)
{
    if (!ImGui::BeginMenu("Mouse"))
        return false;

    bool changed = false;
    int mouse = CLAMP(menu_settings->mouse, 0, GDOX_MOUSE_TYPE_COUNT - 1);
    float text_width = 0.0f;

    for (int i = 0; i < GDOX_MOUSE_TYPE_COUNT; i++)
        text_width = MAX(text_width, ImGui::CalcTextSize(k_mice[i].name).x);

    ImGui::PushItemWidth(gui_get_combo_width(text_width));

    if (ImGui::BeginCombo("##machine_mouse", k_mice[mouse].name))
    {
        for (int i = 0; i < GDOX_MOUSE_TYPE_COUNT; i++)
        {
            if (ImGui::Selectable(k_mice[i].name, mouse == i))
            {
                menu_settings->mouse = i;
                changed = true;
            }
        }

        ImGui::EndCombo();
    }
    else if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::Text("PS/2: up to 200 reports a second, COM ports free.");
        ImGui::Text("Serial: a Microsoft mouse on COM1, as on 1989 PCs.");
        ImGui::Text("DOS mouse drivers find either.");
        ImGui::Text("Reset the emulator to apply changes.");
        ImGui::EndTooltip();
    }

    ImGui::PopItemWidth();
    ImGui::EndMenu();
    return changed;
}

static bool menu_machine_cdrom_drive(void)
{
    if (!ImGui::BeginMenu("CD-ROM Drive"))
        return false;

    bool changed = false;
    int cdrom = CLAMP(menu_settings->cdrom, 0, GDOX_CDROM_POSITION_COUNT - 1);
    float text_width = 0.0f;

    for (int i = 0; i < GDOX_CDROM_POSITION_COUNT; i++)
        text_width = MAX(text_width, ImGui::CalcTextSize(k_cdrom_positions[i].name).x);

    ImGui::PushItemWidth(gui_get_combo_width(text_width));

    if (ImGui::BeginCombo("##machine_cdrom_drive", k_cdrom_positions[cdrom].name))
    {
        for (int i = 0; i < GDOX_CDROM_POSITION_COUNT; i++)
        {
            if (ImGui::Selectable(k_cdrom_positions[i].name, cdrom == i))
            {
                menu_settings->cdrom = i;
                changed = true;
            }
        }

        ImGui::EndCombo();
    }
    else if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::Text("The ATAPI drive's IDE position,");
        ImGui::Text("closed to hard disks.");
        ImGui::Text("Secondary Master: the primary channel stays");
        ImGui::Text("for hard disks.");
        ImGui::Text("Primary Slave: as 1994 PCs with one channel.");
        ImGui::Text("Reset the emulator to apply changes.");
        ImGui::EndTooltip();
    }

    ImGui::PopItemWidth();
    ImGui::EndMenu();
    return changed;
}

static bool menu_machine_drive_timing(void)
{
    if (!ImGui::BeginMenu("Drive Timing"))
        return false;

    bool changed = false;
    int timing = menu_settings->drive_timing < GDOX_DRIVE_TIMING_COUNT ? menu_settings->drive_timing : GDOX_MACHINE_AUTO;
    float text_width = ImGui::CalcTextSize("Auto").x;

    for (int i = 0; i < GDOX_DRIVE_TIMING_COUNT; i++)
        text_width = MAX(text_width, ImGui::CalcTextSize(k_drive_timings[i].name).x);

    ImGui::PushItemWidth(gui_get_combo_width(text_width));

    if (ImGui::BeginCombo("##machine_drive_timing", timing < 0 ? "Auto" : k_drive_timings[timing].name))
    {
        if (ImGui::Selectable("Auto", timing < 0))
        {
            menu_settings->drive_timing = GDOX_MACHINE_AUTO;
            changed = true;
        }

        for (int i = 0; i < GDOX_DRIVE_TIMING_COUNT; i++)
        {
            if (ImGui::Selectable(k_drive_timings[i].name, timing == i))
            {
                menu_settings->drive_timing = i;
                changed = true;
            }
        }

        ImGui::EndCombo();
    }
    else if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::Text("Seek, rotation and spin-up times");
        ImGui::Text("of disks, floppies and CD-ROMs.");
        ImGui::Text("Auto: fast for games, real otherwise.");
        ImGui::Text("Transfer rates don't change.");
        ImGui::Text("Reset the emulator to apply changes.");
        ImGui::EndTooltip();
    }

    ImGui::PopItemWidth();
    ImGui::EndMenu();
    return changed;
}

static bool menu_machine_cdrom_speed(void)
{
    if (!ImGui::BeginMenu("CD-ROM Speed"))
        return false;

    bool changed = false;
    int speed = CLAMP(menu_settings->cdrom_speed, 0, GDOX_CDROM_SPEED_COUNT - 1);
    float text_width = 0.0f;

    for (int i = 0; i < GDOX_CDROM_SPEED_COUNT; i++)
        text_width = MAX(text_width, ImGui::CalcTextSize(k_cdrom_speeds[i].name).x);

    ImGui::PushItemWidth(gui_get_combo_width(text_width));

    if (ImGui::BeginCombo("##machine_cdrom_speed", k_cdrom_speeds[speed].name))
    {
        for (int i = 0; i < GDOX_CDROM_SPEED_COUNT; i++)
        {
            if (ImGui::Selectable(k_cdrom_speeds[i].name, speed == i))
            {
                menu_settings->cdrom_speed = i;
                changed = true;
            }
        }

        ImGui::EndCombo();
    }
    else if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::Text("Spin speed, so seek and read times.");
        ImGui::Text("Games that stream from the disc");
        ImGui::Text("may want 4x or more.");
        ImGui::Text("Reset the emulator to apply changes.");
        ImGui::EndTooltip();
    }

    ImGui::PopItemWidth();

    ImGui::EndMenu();
    return changed;
}

static bool menu_machine_fpu(const GDOX_Machine_Profile& profile)
{
    if (!ImGui::BeginMenu("FPU"))
        return false;

    bool changed = false;
    bool in_use = emu_get_core()->GetPendingMachineConfig().fpu;
    bool integrated = profile.fpu == GDOX_MACHINE_FPU_INTEGRATED;
    char labels[2][64];
    char preview[96];
    snprintf(labels[0], sizeof(labels[0]), "None");
    snprintf(labels[1], sizeof(labels[1]), "%s (Default)", get_machine_fpu_name(profile.cpu));
    snprintf(preview, sizeof(preview), "%s", integrated ? get_machine_fpu_name(profile.cpu) :
        labels[menu_settings->fpu != 0 ? 1 : 0]);

    if (!integrated && (menu_settings->fpu != 0) && !in_use)
        snprintf(preview, sizeof(preview), "%s, none on the %s", labels[1], profile.name);

    float text_width = MAX(ImGui::CalcTextSize(labels[1]).x, ImGui::CalcTextSize(preview).x);
    ImGui::PushItemWidth(gui_get_combo_width(text_width));
    ImGui::BeginDisabled(integrated);

    if (ImGui::BeginCombo("##machine_fpu", preview))
    {
        if (ImGui::Selectable(labels[1], menu_settings->fpu != 0))
        {
            menu_settings->fpu = 1;
            changed = true;
        }

        if (ImGui::Selectable(labels[0], menu_settings->fpu == 0))
        {
            menu_settings->fpu = 0;
            changed = true;
        }

        ImGui::EndCombo();
    }

    ImGui::EndDisabled();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::BeginTooltip();

        if (integrated)
            ImGui::Text("The %s has its FPU built in.", k_machine_cpus[profile.cpu].name);
        else
        {
            ImGui::Text("An 80387 in the 386's socket.");
            ImGui::Text("A few games need one,");
            ImGui::Text("some behave differently with one.");
            ImGui::Text("Reset the emulator to apply changes.");
        }

        ImGui::EndTooltip();
    }

    ImGui::PopItemWidth();
    ImGui::EndMenu();
    return changed;
}

static const char* get_machine_fpu_text(const GDOX_Machine_Profile& profile)
{
    if (profile.fpu == GDOX_MACHINE_FPU_INTEGRATED)
        return "integrated";

    if (profile.fpu == GDOX_MACHINE_FPU_NONE)
        return "none, no socket";

    return profile.cpu == GDOX_MACHINE_CPU_80486SX ? "487SX, optional" : "80387, optional";
}

static void menu_machine_hardware(GDOX_Machine_Model model)
{
    if (!ImGui::BeginMenu("Summary"))
        return;

    char hardware[1024];
    get_machine_hardware(model, hardware, sizeof(hardware));
    ImVec2 size = ImGui::CalcTextSize(hardware);
    ImVec2 padding = ImGui::GetStyle().FramePadding;

    ImGui::InputTextMultiline("##hardware", hardware, sizeof(hardware),
        ImVec2(size.x + (padding.x * 2.0f), size.y + (padding.y * 2.0f)), ImGuiInputTextFlags_ReadOnly);

    ImGui::EndMenu();
}

static void get_machine_hardware(GDOX_Machine_Model model, char* text, size_t text_size)
{
    const GDOX_Machine_Profile& profile = k_machine_profiles[model];

    snprintf(text, text_size, "CPU: %s at %d MHz\nFPU: %s\nFloppy drives: %d", k_machine_cpus[profile.cpu].name,
        (int)(profile.cpu_clock_rate / 1000000), get_machine_fpu_text(profile), profile.floppy_drives);
}

static void draw_machine_profile_tooltip(GDOX_Machine_Model model)
{
    const GDOX_Machine_Profile& profile = k_machine_profiles[model];
    char ram[96] = "";

    for (int i = 0; profile.ram_sizes_mb[i] != 0; i++)
    {
        char size[8];
        snprintf(size, sizeof(size), i == 0 ? "%d" : ", %d", profile.ram_sizes_mb[i]);
        strncat_fit(ram, size, sizeof(ram));
    }

    ImGui::BeginTooltip();
    ImGui::Text("Models: %s", profile.models);
    ImGui::Text("Released: %s", profile.released);
    ImGui::Text("CPU: %s at %d MHz", k_machine_cpus[profile.cpu].name, (int)(profile.cpu_clock_rate / 1000000));
    ImGui::Text("FPU: %s", get_machine_fpu_text(profile));
    ImGui::Text("RAM: %s MB", ram);
    ImGui::Text("Floppy drives: %d", profile.floppy_drives);

    if (!profile.emulated)
    {
        ImGui::Separator();
        ImGui::TextDisabled("Not emulated yet");
    }

    ImGui::EndTooltip();
}

static void menu_boot_device(void)
{
    static const char* const names[GDOX_BOOT_DEVICE_COUNT] = { "", "Floppy 1", "Floppy 2", "Hard Disk", "CD-ROM" };
    GeardoxCore* core = emu_get_core();
    const GDOX_Machine_Config& config = core->GetPendingMachineConfig();

    for (int i = GDOX_BOOT_DEVICE_FLOPPY_0; i < GDOX_BOOT_DEVICE_COUNT; i++)
    {
        bool enabled = true;

        if ((i == GDOX_BOOT_DEVICE_FLOPPY_0) || (i == GDOX_BOOT_DEVICE_FLOPPY_1))
            enabled = (i - GDOX_BOOT_DEVICE_FLOPPY_0) < config.floppy_drives;
        else if (i == GDOX_BOOT_DEVICE_HARD_DISK)
            enabled = core->HasHardDisks();
        else if (i == GDOX_BOOT_DEVICE_CDROM)
            enabled = config.cdrom != GDOX_CDROM_NONE;

        if ((i == GDOX_BOOT_DEVICE_HARD_DISK) || (i == GDOX_BOOT_DEVICE_CDROM))
            ImGui::Separator();

        if (ImGui::MenuItem(names[i], NULL, false, enabled))
        {
            core->SetBootDevice((GDOX_Boot_Device)i);
            gui_action_reset();
        }
    }
}

static void get_machine_summary(const GDOX_Machine_Config& config, char* text, size_t text_size)
{
    bool card = config.fpu && !k_machine_cpus[config.cpu].fpu;
    snprintf(text, text_size, "%s · %s %u MHz%s%s · %u MB", k_machine_profiles[config.model].name,
        k_machine_cpus[config.cpu].name, config.cpu_clock_rate / 1000000, card ? " + " : "",
        card ? get_machine_fpu_name(config.cpu) : "", config.ram_size / (1024 * 1024));
}

static float get_combo_items_width(const char* items)
{
    float text_width = 0.0f;

    for (const char* item = items; *item != 0; item += strlen(item) + 1)
        text_width = MAX(text_width, ImGui::CalcTextSize(item).x);

    return gui_get_combo_width(text_width);
}

static void menu_video(void)
{
    if (ImGui::BeginMenu("Video"))
    {
        gui_in_use = true;

        bool is_recording = emu_is_video_recording();

        if (ImGui::MenuItem("Full Screen", config_hotkeys[config_HotkeyIndex_Fullscreen].str, &config_emulator.fullscreen, !is_recording))
        {
            application_trigger_fullscreen(config_emulator.fullscreen);
        }

#if !defined(__APPLE__)
        if (ImGui::BeginMenu("Fullscreen Mode", !is_recording))
        {
            const char* modes = "Borderless\0Exclusive\0\0";
            ImGui::PushItemWidth(get_combo_items_width(modes));
            if (ImGui::Combo("##fullscreen_mode", &config_emulator.fullscreen_mode, modes) &&
                config_emulator.fullscreen)
            {
                application_trigger_fullscreen(true);
            }
            ImGui::PopItemWidth();
            ImGui::EndMenu();
        }
#endif

        ImGui::Separator();

        ImGui::MenuItem("Always Show Menu", config_hotkeys[config_HotkeyIndex_ShowMainMenu].str, &config_emulator.always_show_menu);

        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("This option will enable menu even in fullscreen.");
            ImGui::Text("Menu always shows in debug mode.");
            ImGui::EndTooltip();
        }

        if (ImGui::MenuItem("Resize Window to Content", NULL, false, !is_recording))
        {
            if (!config_debug.debug)
            {
                application_trigger_fit_to_content(gui_main_window_width, gui_main_window_height + gui_main_menu_height);
            }
        }

        ImGui::Separator();

        if (ImGui::BeginMenu("Scale"))
        {
            const char* scales = "Integer Scale (Auto)\0Integer Scale (Manual)\0"
                "Scale to Window Height\0Scale to Window Width & Height\0\0";
            ImGui::PushItemWidth(get_combo_items_width(scales));
            ImGui::Combo("##scale", &config_video.scale, scales);

            if (config_video.scale == 1)
                ImGui::SliderInt("##scale_manual", &config_video.scale_manual, 1, 20);

            ImGui::PopItemWidth();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Aspect Ratio"))
        {
            const char* ratios = "Square Pixels (1:1 PAR)\0Standard (4:3 DAR)\0Wide (16:9 DAR)\0\0";
            ImGui::PushItemWidth(get_combo_items_width(ratios));
            ImGui::Combo("##ratio", &config_video.ratio, ratios);
            ImGui::PopItemWidth();
            ImGui::EndMenu();
        }

        if (ImGui::MenuItem("Native Resolution", "", &config_video.native_resolution))
            emu_set_native_resolution(config_video.native_resolution);

        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Output one pixel per game pixel, as shaders expect.");
            ImGui::Text("When disabled, zoomed modes are output at monitor resolution.");
            ImGui::EndTooltip();
        }

        if (ImGui::BeginMenu("CGA Output"))
        {
            int output = emu_game_get_cga_output() >= 0 ? emu_game_get_cga_output() : config_video.cga_output;
            float text_width = 0.0f;

            for (int i = 0; i < GDOX_CGA_OUTPUT_COUNT; i++)
                text_width = MAX(text_width, ImGui::CalcTextSize(k_cga_outputs[i].name).x);

            ImGui::PushItemWidth(gui_get_combo_width(text_width));

            if (ImGui::BeginCombo("##cga_output", k_cga_outputs[output].name))
            {
                for (int i = 0; i < GDOX_CGA_OUTPUT_COUNT; i++)
                {
                    if (ImGui::Selectable(k_cga_outputs[i].name, output == i))
                    {
                        if (emu_game_is_open())
                            emu_game_set_cga_output(i);
                        else
                            config_video.cga_output = i;

                        emu_update_cga_output();
                    }
                }

                ImGui::EndCombo();
            }
            else if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("The monitor on the IBM CGA's output.");
                ImGui::Text("Auto: composite colour where a game turns on");
                ImGui::Text("640x200 with the colour burst, RGB elsewhere.");
                ImGui::EndTooltip();
            }

            ImGui::PopItemWidth();
            ImGui::EndMenu();
        }

        ImGui::Separator();

        if (ImGui::BeginMenu("Vertical Sync"))
        {
#if defined(_WIN32)
            const char* sync_modes = "Disabled\0Fixed Vertical Sync\0Variable Refresh Rate (VRR)\0\0";
#else
            const char* sync_modes = "Disabled\0Enabled\0\0";
#endif
            ImGui::PushItemWidth(get_combo_items_width(sync_modes));

            if (ImGui::Combo("##sync_mode", &config_video.sync_mode, sync_modes))
            {
                if (config_video.sync_mode != config_VideoSync_Disabled)
                {
                    config_audio.sync = true;
                    config_emulator.ffwd = false;
                    emu_audio_reset();
                }

                display_use_vsync_if_enabled();
            }

            ImGui::PopItemWidth();

#if defined(_WIN32)
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("Disabled: do not synchronize presentation to the monitor.");
                ImGui::Text("Fixed Vertical Sync: use normal VSync.");
                ImGui::Text("VRR: present at the emulator frame rate.");
                ImGui::Text("\nVRR requires fullscreen, a VRR display, and G-SYNC,");
                ImGui::Text("FreeSync, or Adaptive Sync enabled in your monitor and GPU driver settings.");
                ImGui::EndTooltip();
            }
#endif
            ImGui::EndMenu();
        }

        ImGui::MenuItem("Show FPS", "", &config_video.fps);

        ImGui::Separator();

        gui_shader_menu();

        ImGui::Separator();

        if (ImGui::BeginMenu("Theme"))
        {
            const char* themes = "Light\0Dark\0\0";
            ImGui::PushItemWidth(get_combo_items_width(themes));

            if (ImGui::Combo("##theme", &config_emulator.theme, themes))
            {
                gui_set_style();
            }

            ImGui::PopItemWidth();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Background Color"))
        {
            draw_background_color_menu("Dark Theme", config_Theme_Dark);
            draw_background_color_menu("Light Theme", config_Theme_Light);

            ImGui::EndMenu();
        }

        ImGui::EndMenu();
    }
}

static void draw_background_color_menu(const char* label, int theme)
{
    if (ImGui::BeginMenu(label))
    {
        ImGui::PushID(theme);

        ImGui::ColorEdit3("##normal_bg", config_video.background_color[theme], ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_Float);
        ImGui::SameLine();
        ImGui::Text("Normal Background");

        ImGui::Separator();

        if (ImGui::ColorEdit3("##debugger_bg", config_video.background_color_debugger[theme], ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_Float))
        {
            if (config_emulator.theme == theme)
                gui_set_style();
        }

        ImGui::SameLine();
        ImGui::Text("Debugger Background");

        ImGui::PopID();
        ImGui::EndMenu();
    }
}

static void menu_input(void)
{
    if (ImGui::BeginMenu("Input"))
    {
        gui_in_use = true;

        if (ImGui::MenuItem("Game Focus", config_hotkeys[config_HotkeyIndex_GameFocus].str, false, !emu_is_empty()))
            game_focus_set(true);

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::BeginTooltip();
            ImGui::Text("Locks the mouse in the window and turns hotkeys off,");
            ImGui::Text("except Game Focus, Fullscreen and the keyboard mapper's.");
            ImGui::Text("Going fullscreen also enters it.");
            ImGui::EndTooltip();
        }

        ImGui::Separator();

        if (ImGui::BeginMenu("Keyboard"))
        {
            if (ImGui::MenuItem("Keyboard Mapper...", config_hotkeys[config_HotkeyIndex_KeyboardMapper].str,
                emu_key_mapper_is_open(), !emu_is_empty()))
                emu_key_mapper_toggle();

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::BeginTooltip();
                ImGui::Text("Shows the PC keyboard to bind host keys to it,");
                ImGui::Text("for this game only. The PC stops while it's open.");
                ImGui::EndTooltip();
            }

            bool key_mapping = emu_key_mapping_is_enabled();

            if (ImGui::MenuItem("Key Mapping", config_hotkeys[config_HotkeyIndex_KeyMapping].str, &key_mapping,
                !emu_is_empty()))
                gui_action_toggle_key_mapping();

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::BeginTooltip();
                ImGui::Text("Off: every key types its own, to name a");
                ImGui::Text("character in a game that moves on letters.");
                ImGui::EndTooltip();
            }

            ImGui::Separator();

            if (input_macro_get_owner() == Input_Macro_Owner_GUI)
            {
                if (ImGui::MenuItem("Stop Pasting"))
                    gui_action_stop_pasting();
            }
            else
            {
                if (ImGui::MenuItem("Paste as Keystrokes", NULL, false, !emu_is_empty() &&
                    input_macro_get_owner() == Input_Macro_Owner_None))
                    gui_action_paste_keystrokes();

                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                {
                    ImGui::BeginTooltip();
                    ImGui::Text("Types the clipboard on the PC keyboard,");
                    ImGui::Text("up to %d characters.", k_input_macro_max_paste);
                    ImGui::Text("Characters it can't type are skipped.");
                    ImGui::EndTooltip();
                }
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Mouse"))
        {
            ImGui::SliderInt("##mouse_sensitivity", &config_emulator.mouse_sensitivity, 1, 15, "Sensitivity = %d");

            ImGui::EndMenu();
        }

        ImGui::Separator();

        if (ImGui::BeginMenu("Emulated Joystick"))
        {
            int joystick = emu_game_get_joystick() >= 0 ? emu_game_get_joystick() : config_input.joystick_type;
            float text_width = 0.0f;

            for (int i = 0; i < GDOX_JOYSTICK_TYPE_COUNT; i++)
                text_width = MAX(text_width, ImGui::CalcTextSize(k_joysticks[i].name).x);

            ImGui::PushItemWidth(gui_get_combo_width(text_width));

            if (ImGui::BeginCombo("##joystick", k_joysticks[joystick].name))
            {
                for (int i = 0; i < GDOX_JOYSTICK_TYPE_COUNT; i++)
                {
                    if (ImGui::Selectable(k_joysticks[i].name, joystick == i))
                    {
                        if (emu_game_is_open())
                            emu_game_set_joystick(i);
                        else
                            config_input.joystick_type = i;

                        emu_update_joystick_type();
                    }
                }

                ImGui::EndCombo();
            }
            else if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("What Host Gamepad 1 plays on the game port");
                ImGui::Text("when it's used as Joystick.");
                ImGui::Text("Joystick: analog stick, two buttons.");
                ImGui::Text("Gravis GamePad: eight directions on the D-pad");
                ImGui::Text("or left stick, four buttons.");
                ImGui::Text("CH FlightStick Pro: stick, four buttons,");
                ImGui::Text("throttle on the right stick, hat on the D-pad.");
                ImGui::EndTooltip();
            }

            ImGui::PopItemWidth();
            ImGui::EndMenu();
        }

        ImGui::Separator();

        menu_input_gamepad(0);
        menu_input_gamepad(1);

        ImGui::EndMenu();
    }
}

static void menu_audio(void)
{
    if (ImGui::BeginMenu("Audio"))
    {
        gui_in_use = true;

        bool mute = !config_audio.enable;

        if (ImGui::MenuItem("Mute All Audio", config_hotkeys[config_HotkeyIndex_Mute].str, &mute))
        {
            config_audio.enable = !mute;
            emu_audio_mute(mute);
        }

        ImGui::Separator();

        if (ImGui::BeginMenu("Master Volume", config_audio.enable))
        {
            ImGui::PushItemWidth(200.0f);

            if (ImGui::SliderFloat("##master_volume", &config_audio.master_volume, 0.0f, 2.0f, "Volume = %.2f", ImGuiSliderFlags_AlwaysClamp))
            {
                emu_audio_set_master_volume(config_audio.master_volume);
            }

            ImGui::PopItemWidth();

            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("Anything above 1.00 may cause clipping.");
                ImGui::EndTooltip();
            }

            ImGui::EndMenu();
        }

        ImGui::Separator();

        menu_audio_source_mute("Mute FM", Audio::AUDIO_SOURCE_FM);
        menu_audio_source_mute("Mute PCM", Audio::AUDIO_SOURCE_PCM);
        menu_audio_source_mute("Mute CD-DA", Audio::AUDIO_SOURCE_CDDA);
        menu_audio_source_mute("Mute Buzzer", Audio::AUDIO_SOURCE_BUZZER);
        ImGui::MenuItem("Mute MIDI", "", &config_audio.midi_mute, config_audio.enable);

        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Stops the MIDI notes.");
            ImGui::Text("The synth keeps the game's instruments.");
            ImGui::EndTooltip();
        }

        ImGui::Separator();

        menu_audio_source_volume("FM Volume", "##fm_volume", Audio::AUDIO_SOURCE_FM, &config_audio.fm_volume);
        menu_audio_source_volume("PCM Volume", "##pcm_volume", Audio::AUDIO_SOURCE_PCM, &config_audio.pcm_volume);
        menu_audio_source_volume("CD-DA Volume", "##cdda_volume", Audio::AUDIO_SOURCE_CDDA, &config_audio.cdda_volume);
        menu_audio_source_volume("Buzzer Volume", "##buzzer_volume", Audio::AUDIO_SOURCE_BUZZER, &config_audio.buzzer_volume);

        ImGui::Separator();
        menu_audio_midi();
        ImGui::Separator();

        if (ImGui::BeginMenu("Buffer Size", config_audio.enable))
        {
            ImGui::PushItemWidth(150.0f);

            if (ImGui::SliderInt("##buffer_count", &config_audio.buffer_count, 2, 5, "Buffers = %d"))
            {
                emu_audio_restart();
            }

            ImGui::PopItemWidth();

            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("Audio latency: %.0f ms", sound_queue_get_target_latency_ms());
                ImGui::Text("\nLower values reduce audio latency.");
                ImGui::Text("Higher values prevent audio underruns.");
                ImGui::Text("Enabling VSync may force higher buffer counts.");

                ImGui::EndTooltip();
            }

            ImGui::EndMenu();
        }

#ifndef GDOX_DISABLE_VGMRECORDER
        ImGui::Separator();

        if (ImGui::BeginMenu("VGM Recorder"))
        {
            bool is_recording = emu_is_vgm_recording();

            if (ImGui::MenuItem("Start Recording...", "", false, !is_recording && !emu_is_empty()))
            {
                save_vgm = true;
            }

            if (ImGui::MenuItem("Stop Recording", "", false, is_recording))
            {
                emu_stop_vgm_recording();
                gui_notify(gui_NotificationInfo, ICON_MD_STOP, "VGM recording stopped", NULL, "vgm");
            }

            ImGui::EndMenu();
        }
#endif

        ImGui::EndMenu();
    }
}

static void menu_audio_source_mute(const char* label, int source)
{
    bool mute = emu_audio_is_source_muted(source);

    if (ImGui::MenuItem(label, "", &mute, config_audio.enable))
        emu_audio_set_source_mute(source, mute);
}

static void menu_audio_source_volume(const char* label, const char* id, int source, float* volume)
{
    if (ImGui::BeginMenu(label, config_audio.enable))
    {
        ImGui::PushItemWidth(200.0f);

        if (ImGui::SliderFloat(id, volume, 0.0f, 2.0f, "Volume = %.2f", ImGuiSliderFlags_AlwaysClamp))
            emu_audio_set_source_volume(source, *volume);

        ImGui::PopItemWidth();

        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Anything above 1.00 may cause clipping.");
            ImGui::EndTooltip();
        }

        ImGui::EndMenu();
    }
}

static void menu_audio_midi(void)
{
    if (!ImGui::BeginMenu("MIDI Out", MidiPort::IsSupported()))
        return;

    if (menu_audio_midi_port("General MIDI Port", "##midi_gm_port", &config_audio.midi_gm_port,
        "A General MIDI, GS or XG synth or module.\nPlays games set up for General MIDI, GS or XG.\n"
        "With no MT-32 port, plays MT-32 games too."))
        emu_midi_update_port();

    if (menu_audio_midi_port("MT-32 Port", "##midi_mt32_port", &config_audio.midi_mt32_port,
        "An MT-32 emulator, or a real MT-32,\nCM-32L or LAPC-I.\nPlays games set up for Roland MT-32.\n"
        "With no GM port, plays General MIDI games too."))
        emu_midi_update_port();

    menu_audio_midi_game_synth();
    ImGui::Separator();

    const char* port = emu_midi_get_port();
    const char* wanted = emu_midi_get_wanted_port();

    if (port[0] != 0)
        ImGui::TextColored(midi_playing_color, "Playing to %s", port);
    else if (wanted[0] != 0)
        ImGui::TextColored(midi_stopped_color, "%s not found", wanted);
    else
        ImGui::TextColored(midi_stopped_color, "No MIDI port");

    ImGui::EndMenu();
}

// The list is read again each time the submenu opens, so a synth started after Geardox shows up
static bool menu_audio_midi_port(const char* label, const char* id, std::string* port, const char* tooltip)
{
    if (!ImGui::BeginMenu(label))
        return false;

    if (ImGui::IsWindowAppearing())
        MidiPort::ListPorts(midi_ports);

    bool changed = false;
    char missing[MIDI_PORT_NAME_SIZE + 16];
    const char* preview = "None";
    float text_width = ImGui::CalcTextSize(preview).x;
    int selected = find_midi_port(port->c_str());

    for (int i = 0; i < (int)midi_ports.size(); i++)
        text_width = MAX(text_width, ImGui::CalcTextSize(midi_ports[i].id).x);

    if (selected >= 0)
        preview = midi_ports[selected].id;
    else if (!port->empty())
    {
        snprintf(missing, sizeof(missing), "%s (not found)", port->c_str());
        preview = missing;
        text_width = MAX(text_width, ImGui::CalcTextSize(missing).x);
    }

    ImGui::PushItemWidth(gui_get_combo_width(text_width));

    if (ImGui::BeginCombo(id, preview))
    {
        if (ImGui::Selectable("None", port->empty()))
        {
            port->clear();
            changed = true;
        }

        for (int i = 0; i < (int)midi_ports.size(); i++)
        {
            ImGui::PushID(i);

            if (ImGui::Selectable(midi_ports[i].id, i == selected))
            {
                *port = midi_ports[i].id;
                changed = true;
            }

            ImGui::PopID();
        }

        if (!port->empty() && (selected < 0))
            ImGui::Selectable(missing, true, ImGuiSelectableFlags_Disabled);

        ImGui::EndCombo();
    }
    else if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s\nStart the synth first:\nports are read when this menu opens.", tooltip);

    ImGui::PopItemWidth();
    ImGui::EndMenu();
    return changed;
}

static void menu_audio_midi_game_synth(void)
{
    if (!ImGui::BeginMenu("Game Synth"))
        return;

    bool game = emu_game_is_open();

    if (game && ImGui::IsWindowAppearing())
        MidiPort::ListPorts(midi_ports);

    int synth = emu_midi_get_synth();
    std::string game_port = emu_game_get_midi_port();
    int selected = find_midi_port(game_port.c_str());
    char labels[GDOX_MIDI_SYNTH_COUNT][MIDI_PORT_NAME_SIZE + 16];
    char missing[MIDI_PORT_NAME_SIZE + 16];
    float text_width = 0.0f;

    for (int i = 0; i < GDOX_MIDI_SYNTH_COUNT; i++)
    {
        const char* port = emu_midi_get_synth_port(i);
        snprintf(labels[i], sizeof(labels[i]), "%s (%s)", k_midi_synths[i].name, port[0] != 0 ? port : "None");
        text_width = MAX(text_width, ImGui::CalcTextSize(labels[i]).x);
    }

    for (int i = 0; game && (i < (int)midi_ports.size()); i++)
        text_width = MAX(text_width, ImGui::CalcTextSize(midi_ports[i].id).x);

    const char* preview = labels[synth];

    if (selected >= 0)
        preview = midi_ports[selected].id;
    else if (!game_port.empty())
    {
        snprintf(missing, sizeof(missing), "%s (not found)", game_port.c_str());
        preview = missing;
        text_width = MAX(text_width, ImGui::CalcTextSize(missing).x);
    }

    ImGui::PushItemWidth(gui_get_combo_width(text_width));

    if (ImGui::BeginCombo("##midi_game_synth", preview))
    {
        for (int i = 0; i < GDOX_MIDI_SYNTH_COUNT; i++)
        {
            if (ImGui::Selectable(labels[i], game_port.empty() && (synth == i)))
            {
                if (game)
                    emu_game_set_midi(i, "");
                else
                    config_audio.midi_default = i;

                emu_midi_update_port();
            }
        }

        if (game)
        {
            ImGui::Separator();

            for (int i = 0; i < (int)midi_ports.size(); i++)
            {
                ImGui::PushID(i);

                if (ImGui::Selectable(midi_ports[i].id, i == selected))
                {
                    emu_game_set_midi(emu_game_get_midi(), midi_ports[i].id);
                    emu_midi_update_port();
                }

                ImGui::PopID();
            }

            if (!game_port.empty() && (selected < 0))
                ImGui::Selectable(missing, true, ImGuiSelectableFlags_Disabled);
        }

        ImGui::EndCombo();
    }
    else if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::Text("What the game was set up for, which picks");
        ImGui::Text("its port, or any port for this game only.");
        ImGui::Text("With no game open, the default for games.");
        ImGui::Text("A .gdox sets it with midi or midi_port in [sound].");

        if (game)
        {
            ImGui::Text("Start the synth first:");
            ImGui::Text("ports are read when this menu opens.");
        }

        ImGui::EndTooltip();
    }

    ImGui::PopItemWidth();
    ImGui::EndMenu();
}

static int find_midi_port(const char* port)
{
    for (int i = 0; (port[0] != 0) && (i < (int)midi_ports.size()); i++)
    {
        if (MidiPort::IsSamePort(port, midi_ports[i].id))
            return i;
    }

    return -1;
}

static void menu_debug(void)
{
#if !defined(GDOX_DISABLE_DISASSEMBLER)
    if (ImGui::BeginMenu("Debug"))
    {
        gui_in_use = true;

        ImGui::MenuItem("Enable", "", &config_debug.debug);

        ImGui::Separator();

        if (ImGui::MenuItem("Save Debug Settings...", "", false, config_debug.debug))
        {
            gui_file_dialog_save_debug_settings();
        }

        if (ImGui::MenuItem("Load Debug Settings...", "", false, config_debug.debug))
        {
            gui_file_dialog_load_debug_settings();
        }

        ImGui::MenuItem("Auto Save/Load Debug Settings", "", &config_debug.auto_debug_settings, config_debug.debug);

        ImGui::Separator();

        bool can_reload = config_debug.debug && !emu_is_empty() && emu_get_core()->GetMedia()->IsReady();

        if (ImGui::MenuItem("Reload CD-ROM and Reset", config_hotkeys[config_HotkeyIndex_ReloadROM].str, false, can_reload))
        {
            gui_action_reload_rom();
        }

        ImGui::Separator();

        if (ImGui::BeginMenu("MCP Server", config_debug.debug))
        {
            bool mcp_running = emu_mcp_is_running();
            int transport_mode = emu_mcp_get_transport_mode();
            bool http_running = mcp_running && (transport_mode == 1);
            bool stdio_running = mcp_running && (transport_mode == 0);

            if (ImGui::MenuItem("Start HTTP Server", "", false, !mcp_running))
            {
                if (strlen(gui_mcp_http_address) == 0)
                    strncpy_fit(gui_mcp_http_address, "127.0.0.1", sizeof(gui_mcp_http_address));

                config_emulator.mcp_http_address = gui_mcp_http_address;
                emu_mcp_set_transport(1, config_emulator.mcp_tcp_port, config_emulator.mcp_http_address.c_str());
                emu_mcp_start();
            }

            if (ImGui::MenuItem("Stop HTTP Server", "", false, http_running))
            {
                emu_mcp_stop();
            }

            ImGui::Separator();

            if (stdio_running)
                ImGui::TextColored(service_mcp_stdio_color, "STDIO mode active");
            else if (http_running)
                ImGui::TextColored(service_mcp_http_color, "Listening on %s:%d", emu_mcp_get_http_address(),
                    emu_mcp_get_http_port());
            else
                ImGui::TextColored(red, "Stopped");

            ImGui::Separator();

            ImGui::Text("HTTP Address:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120);

            if (ImGui::InputText("##mcp_address", gui_mcp_http_address, IM_ARRAYSIZE(gui_mcp_http_address), ImGuiInputTextFlags_AutoSelectAll))
                config_emulator.mcp_http_address = gui_mcp_http_address;

            ImGui::Text("HTTP Port:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(50);

            if (ImGui::InputInt("##mcp_port", &config_emulator.mcp_tcp_port, 0, 0))
            {
                if (config_emulator.mcp_tcp_port < 1)
                    config_emulator.mcp_tcp_port = 1;

                if (config_emulator.mcp_tcp_port > 65535)
                    config_emulator.mcp_tcp_port = 65535;
            }

            ImGui::EndMenu();
        }

        ImGui::Separator();

        if (ImGui::BeginMenu("Debug Output Screen", config_debug.debug))
        {
            ImGui::MenuItem("Show Output Screen", "", &config_debug.show_screen, config_debug.debug);

            if (ImGui::BeginMenu("Scale", config_debug.debug))
            {
                ImGui::PushItemWidth(200.0f);
                ImGui::SliderInt("##debug_scale", &config_debug.scale, 1, 10);
                ImGui::PopItemWidth();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Aspect Ratio", config_debug.debug))
            {
                const char* ratios = "Square Pixels (1:1 PAR)\0Standard (4:3 DAR)\0Wide (16:9 DAR)\0\0";
                ImGui::PushItemWidth(get_combo_items_width(ratios));
                ImGui::Combo("##debug_ratio", &config_debug.ratio, ratios);
                ImGui::PopItemWidth();
                ImGui::EndMenu();
            }

            ImGui::EndMenu();
        }

        ImGui::Separator();
        ImGui::MenuItem("Show Disassembler", "", &config_debug.show_disassembler, config_debug.debug);
        ImGui::MenuItem("Show Memory Workspace", "", &config_debug.show_memory, config_debug.debug);
        ImGui::MenuItem("Show Trace Logger", "", &config_debug.show_trace_logger, config_debug.debug);
        ImGui::MenuItem("Show Profiler", "", &config_debug.show_profiler, config_debug.debug);

        ImGui::Separator();

        if (ImGui::BeginMenu("Intel 80386", config_debug.debug))
        {
            ImGui::MenuItem("Show Status", "", &config_debug.show_processor);
            ImGui::MenuItem("Show System Registers", "", &config_debug.show_processor_details);
            ImGui::MenuItem("Show Descriptor Tables", "", &config_debug.show_i386_descriptors);
            ImGui::MenuItem("Show Paging", "", &config_debug.show_i386_paging);
            ImGui::MenuItem("Show Call Stack", "", &config_debug.show_call_stack);
            ImGui::MenuItem("Show Breakpoints", "", &config_debug.show_breakpoints);
            ImGui::MenuItem("Show Symbols", "", &config_debug.show_symbols);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Intel 80387", config_debug.debug))
        {
            ImGui::MenuItem("Show Status", "", &config_debug.show_x87);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Audio", config_debug.debug))
        {
            ImGui::MenuItem("Show OPL", "", &config_debug.show_opl);
            ImGui::MenuItem("Show Sound Blaster", "", &config_debug.show_sound_blaster);
            ImGui::MenuItem("Show Sound Control", "", &config_debug.show_sound_control);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("CD-ROM", config_debug.debug))
        {
            ImGui::MenuItem("Show TOC", "", &config_debug.show_cdrom_toc);
            ImGui::MenuItem("Show CD Audio", "", &config_debug.show_cdrom_audio);
            ImGui::MenuItem("Show Drive", "", &config_debug.show_cdrom_drive);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Floppy", config_debug.debug))
        {
            ImGui::MenuItem("Show Drives", "", &config_debug.show_floppy_drives);
            ImGui::MenuItem("Show Disk Viewer", "", &config_debug.show_disk_viewer);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("IDE", config_debug.debug))
        {
            ImGui::MenuItem("Show Controller", "", &config_debug.show_ide);
            ImGui::MenuItem("Show Disks", "", &config_debug.show_ide_disks);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("System", config_debug.debug))
        {
            ImGui::MenuItem("Show Interrupts", "", &config_debug.show_pic);
            ImGui::MenuItem("Show Timers", "", &config_debug.show_pit);
            ImGui::MenuItem("Show DMA", "", &config_debug.show_dma);
            ImGui::MenuItem("Show System Control", "", &config_debug.show_system_control);
            ImGui::MenuItem("Show RTC", "", &config_debug.show_rtc);
            ImGui::MenuItem("Show BIOS Log", "", &config_debug.show_bios_log);
            ImGui::Separator();

            if (ImGui::MenuItem("BIOS Debug Console", "", &config_emulator.bios_debug_console))
                emu_apply_bios_settings();

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Port 402h, where the BIOS writes its messages.");

            if (ImGui::MenuItem("Log BIOS Messages", "", &config_emulator.bios_log))
                emu_apply_bios_settings();

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Copies each line of the BIOS log\nto the emulator log.");

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Video", config_debug.debug))
        {
            ImGui::MenuItem("Show VGA", "", &config_debug.show_vga);
            ImGui::MenuItem("Show VGA Registers", "", &config_debug.show_vga_registers);
            ImGui::MenuItem("Show Palettes", "", &config_debug.show_palettes);
            ImGui::MenuItem("Show VRAM Viewer", "", &config_debug.show_vram_viewer);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Input", config_debug.debug))
        {
            ImGui::MenuItem("Show Keyboard", "", &config_debug.show_keyboard);
            ImGui::MenuItem("Show Mouse and Joystick", "", &config_debug.show_mouse_joystick);
            ImGui::MenuItem("Show Serial Ports", "", &config_debug.show_serial_ports);
            ImGui::MenuItem("Show Parallel Port", "", &config_debug.show_parallel_port);
            ImGui::EndMenu();
        }

        ImGui::Separator();
        ImGui::MenuItem("Show Rewind", "", &config_debug.show_rewind, config_debug.debug);

        ImGui::Separator();

#if defined(__APPLE__) || defined(_WIN32)
        ImGui::MenuItem("Multi-Viewport", "", &config_debug.multi_viewport, config_debug.debug);

        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("RESTART REQUIRED");
            ImGui::NewLine();
            ImGui::Text("Enables docking of debug windows outside of main window.");
            ImGui::EndTooltip();
        }
#endif

        ImGui::Separator();

        if (ImGui::BeginMenu("Font", config_debug.debug))
        {
            if (ImGui::BeginMenu("Type"))
            {
                const char* types = "ProggyForever\0ProggyClean\0\0";
                ImGui::PushItemWidth(get_combo_items_width(types));

                if (ImGui::Combo("##font_type", &config_debug.font_type, types))
                {
                    gui_default_font = gui_default_fonts[config_debug.font_type][config_debug.font_size];
                }

                ImGui::PopItemWidth();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Size"))
            {
                const char* sizes = "Small\0Medium\0Big\0Large\0\0";
                ImGui::PushItemWidth(get_combo_items_width(sizes));

                if (ImGui::Combo("##font_size", &config_debug.font_size, sizes))
                {
                    gui_default_font = gui_default_fonts[config_debug.font_type][config_debug.font_size];
                }

                ImGui::PopItemWidth();
                ImGui::EndMenu();
            }

            ImGui::EndMenu();
        }

        ImGui::EndMenu();
    }
#endif
}

static void menu_about(void)
{
    if (ImGui::BeginMenu("About"))
    {
        gui_in_use = true;

        if (ImGui::MenuItem("About " GDOX_TITLE " " GDOX_VERSION " ..."))
        {
            open_about = true;
        }

        ImGui::EndMenu();
    }
}

static void draw_mcp_status(void)
{
    bool video_recording = emu_is_video_recording();
    bool vgm_recording = emu_is_vgm_recording();
    bool mcp_running = emu_mcp_is_running();

    if (!video_recording && !vgm_recording && !mcp_running)
        return;

    const char* video_recording_status = "REC VIDEO";
    const char* vgm_recording_status = "REC VGM";
    char mcp_status[128];
    ImVec4 mcp_color = service_mcp_http_color;

    if (mcp_running)
    {
        int transport_mode = emu_mcp_get_transport_mode();

        if (transport_mode == 0)
        {
            snprintf(mcp_status, sizeof(mcp_status), "MCP: STDIO");
            mcp_color = service_mcp_stdio_color;
        }
        else
        {
            snprintf(mcp_status, sizeof(mcp_status), "MCP: HTTP (%s:%d)", emu_mcp_get_http_address(), emu_mcp_get_http_port());
        }
    }

    ImGuiStyle& style = ImGui::GetStyle();
    float spacing = style.ItemSpacing.x * 2.0f;
    float dot_radius = ImGui::GetFontSize() * 0.22f;
    float dot_width = (dot_radius * 2.0f) + style.ItemInnerSpacing.x;
    float text_width = 0.0f;

    if (video_recording)
        text_width += dot_width + ImGui::CalcTextSize(video_recording_status).x;
    if (vgm_recording)
    {
        if (text_width > 0.0f)
            text_width += spacing;
        text_width += dot_width + ImGui::CalcTextSize(vgm_recording_status).x;
    }
    if (mcp_running)
    {
        if (text_width > 0.0f)
            text_width += spacing;
        text_width += ImGui::CalcTextSize(mcp_status).x;
    }

    float status_x = ImGui::GetWindowWidth() - text_width - style.ItemSpacing.x - 10.0f;
    float cursor_x = ImGui::GetCursorPosX();

    if (status_x <= cursor_x + style.ItemSpacing.x)
        return;

    ImGui::SameLine(status_x);
    ImGui::AlignTextToFramePadding();

    if (video_recording)
        draw_recording_status(video_recording_status, video_recording_color, dot_radius, dot_width);

    if (vgm_recording)
    {
        if (video_recording)
            ImGui::SameLine(0.0f, spacing);

        draw_recording_status(vgm_recording_status, vgm_recording_color, dot_radius, dot_width);
    }

    if (mcp_running)
    {
        if (video_recording || vgm_recording)
            ImGui::SameLine(0.0f, spacing);

        ImGui::TextColored(mcp_color, "%s", mcp_status);
    }
}

static void draw_recording_status(const char* status, ImVec4 color, float dot_radius, float dot_width)
{
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + dot_width);
    ImGui::TextColored(color, "%s", status);

    ImVec2 text_min = ImGui::GetItemRectMin();
    ImVec2 text_max = ImGui::GetItemRectMax();
    ImVec2 dot_center = ImVec2(text_min.x - dot_width + dot_radius, (text_min.y + text_max.y) * 0.5f);
    ImGui::GetWindowDrawList()->AddCircleFilled(dot_center, dot_radius, ImGui::GetColorU32(color));
}

static void file_dialogs(void)
{
    gui_file_dialog_process_results();

    if (open_rom || gui_shortcut_open_rom)
    {
        gui_shortcut_open_rom = false;
        gui_file_dialog_open_rom();
    }

    if (open_state)
        gui_file_dialog_load_state();

    if (save_state)
        gui_file_dialog_save_state();

    if (save_screenshot)
        gui_file_dialog_save_screenshot();

    if (save_vgm)
        gui_file_dialog_save_vgm();

    if (save_video)
        gui_file_dialog_save_video();

    if (choose_savestates_path)
        gui_file_dialog_choose_savestate_path();

    if (choose_savefiles_path)
        gui_file_dialog_choose_savefiles_path();

    if (choose_screenshots_path)
        gui_file_dialog_choose_screenshot_path();

    if (choose_video_recordings_path)
        gui_file_dialog_choose_video_recording_path();

#if defined(GDOX_ENABLE_PHYSICAL_CDROM)
    if (open_physical_cdrom)
        gui_popup_open_physical_cdrom();
#endif

    if (open_about)
    {
        gui_dialog_in_use = true;
        ImGui::OpenPopup("About " GDOX_TITLE);
    }

    if (open_load_defaults)
    {
        gui_dialog_in_use = true;
        ImGui::OpenPopup("Load Default Settings");
    }

    gui_popup_modal_about();
    gui_popup_modal_load_defaults();
    gui_floppy_popups();
    gui_hard_disks_popups();
    gui_game_popups();
#if defined(GDOX_ENABLE_PHYSICAL_CDROM)
    gui_popup_modal_physical_cdrom();
#endif
}

static const char* get_current_media_directory_text(void)
{
#if defined(GDOX_ENABLE_PHYSICAL_CDROM)
    if (!emu_is_empty() && emu_get_core()->GetMedia()->IsPhysicalCdRom())
        return config_root_path;
#endif

    return emu_get_core()->GetMedia()->GetFileDirectory();
}

static void gamepad_configuration_item(const char* text, int* button, int player)
{
    ImGui::Text("%s", text);
    ImGui::SameLine(140);

    const char* button_name = "";

    if (*button == SDL_GAMEPAD_BUTTON_INVALID)
    {
        button_name = "";
    }
    else if (*button >= 0 && *button < SDL_GAMEPAD_BUTTON_COUNT)
    {
        static const char* gamepad_names[21] = {"A", "B", "X" ,"Y", "BACK", "GUIDE", "START", "L3", "R3", "L1", "R1", "UP", "DOWN", "LEFT", "RIGHT", "MISC", "PAD1", "PAD2", "PAD3", "PAD4", "TOUCH"};
        button_name = gamepad_names[*button];
    }
    else if (*button >= GAMEPAD_VBTN_AXIS_BASE)
    {
        int axis = *button - GAMEPAD_VBTN_AXIS_BASE;

        if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER)
            button_name = "L2";
        else if (axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
            button_name = "R2";
        else
            button_name = "??";
    }

    char button_label[256];
    snprintf(button_label, sizeof(button_label), "%s##%p%d", button_name, (void*)button, player);

    if (ImGui::Button(button_label, ImVec2(70,0)))
    {
        gui_configured_button = button;
        ImGui::OpenPopup("Gamepad Configuration");
    }

    ImGui::SameLine();

    char remove_label[256];
    snprintf(remove_label, sizeof(remove_label), "X##rg%p%d", (void*)button, player);

    if (ImGui::Button(remove_label))
    {
        *button = SDL_GAMEPAD_BUTTON_INVALID;
    }
}

static void hotkey_configuration_item(const char* text, config_Hotkey* hotkey)
{
    ImGui::Text("%s", text);
    ImGui::SameLine(200);

    char button_label[256];
    snprintf(button_label, sizeof(button_label), "%s##%s", hotkey->str[0] != '\0' ? hotkey->str : "<None>", text);

    if (ImGui::Button(button_label, ImVec2(150,0)))
    {
        gui_configured_hotkey = hotkey;
        ImGui::OpenPopup("Hotkey Configuration");
    }

    ImGui::SameLine();

    char remove_label[256];
    snprintf(remove_label, sizeof(remove_label), "X##rh%s", text);

    if (ImGui::Button(remove_label))
    {
        hotkey->key = SDL_SCANCODE_UNKNOWN;
        hotkey->mod = SDL_KMOD_NONE;
        config_update_hotkey_string(hotkey);
    }
}

static void hotkey_configuration_menu(bool debug)
{
    for (int i = 0; i < config_HotkeyIndex_COUNT; i++)
    {
        if (((hotkey_definitions[i].flags & HOTKEY_FLAG_DEBUG) != 0) != debug)
            continue;

        char label[64];
        snprintf(label, sizeof(label), "%s:", hotkey_definitions[i].label);
        hotkey_configuration_item(label, &config_hotkeys[i]);
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Reset to Defaults"))
        hotkeys_reset_defaults(debug);

    gui_popup_modal_hotkey();
}

static void gamepad_device_selector(int player)
{
    if (player < 0 || player >= GDOX_MAX_GAMEPADS)
        return;

    const int max_detected_gamepads = 32;
    SDL_JoystickID id_map[max_detected_gamepads];
    id_map[0] = 0;
    int count = 1;

    std::string items;
    items.reserve(4096);
    items.append("<None>");
    items.push_back('\0');
    float text_width = ImGui::CalcTextSize("<None>").x;

    Gamepad_Detected_Info detected[max_detected_gamepads];
    int num_detected = gamepad_get_detected(detected, max_detected_gamepads);

    SDL_JoystickID current_id = 0;

    if (IsValidPointer(gamepad_controller[player]))
        current_id = SDL_GetJoystickID(SDL_GetGamepadJoystick(gamepad_controller[player]));

    int selected = 0;

    for (int i = 0; i < num_detected && count < max_detected_gamepads; i++)
    {
        const char* name = detected[i].name;

        if (!IsValidPointer(name))
            name = "Unknown Gamepad";

        id_map[count] = detected[i].id;

        if (current_id == detected[i].id)
            selected = count;

        size_t len = strlen(detected[i].guid_str);
        const char* id_8 = detected[i].guid_str + (len > 8 ? len - 8 : 0);

        char label[192];
        snprintf(label, sizeof(label), "%s (ID: %s)", name, id_8);

        items.append(label);
        items.push_back('\0');
        text_width = MAX(text_width, ImGui::CalcTextSize(label).x);
        count++;
    }

    items.push_back('\0');

    char label[32];
    snprintf(label, sizeof(label), "##device_player%d", player + 1);

    ImGui::PushItemWidth(gui_get_combo_width(text_width));

    if (ImGui::Combo(label, &selected, items.c_str()))
    {
        SDL_JoystickID instance_id = id_map[selected];
        gamepad_assign(player, instance_id);
    }

    ImGui::PopItemWidth();
}

static void menu_input_gamepad(int pad)
{
    char title[32];
    snprintf(title, sizeof(title), "Host Gamepad %d", pad + 1);

    if (!ImGui::BeginMenu(title))
        return;

    if (!gamepad_controller[pad])
        ImGui::TextDisabled("No gamepad detected");
    else
        ImGui::TextDisabled("Gamepad detected");

    ImGui::Separator();

    if (ImGui::BeginMenu("Device"))
    {
        gamepad_device_selector(pad);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Use As"))
    {
        gamepad_choice_selector(pad, true);
        ImGui::EndMenu();
    }

    bool keys = gamepad_get_use(pad) == GDOX_GAMEPAD_USE_KEYS;
    ImGui::Separator();

    if (ImGui::BeginMenu("Key Mapping", keys))
    {
        float offset = 0.0f;

        for (int i = 0; i < GDOX_GAMEPAD_INPUT_COUNT; i++)
            offset = MAX(offset, ImGui::CalcTextSize(k_gamepad_inputs[i].name).x);

        offset += ImGui::CalcTextSize(": ").x + ImGui::GetStyle().ItemSpacing.x;

        for (int i = 0; i < GDOX_GAMEPAD_INPUT_COUNT; i++)
        {
            if ((i == GDOX_GAMEPAD_INPUT_L1) || (i == GDOX_GAMEPAD_INPUT_BACK) || (i == GDOX_GAMEPAD_INPUT_LEFT_STICK_UP) ||
                (i == GDOX_GAMEPAD_INPUT_DPAD_UP) || (i == GDOX_GAMEPAD_INPUT_RIGHT_STICK_UP))
                ImGui::Separator();

            gamepad_key_item(pad, i, offset);
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Reset to Defaults"))
            gamepad_reset_targets(pad);

        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("During a game, back to the game's own mapping.");
            ImGui::EndTooltip();
        }

        gui_popup_modal_gamepad_key();

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Mouse", keys))
    {
        if (ImGui::BeginMenu("Move Mouse With"))
        {
            gamepad_choice_selector(pad, false);
            ImGui::EndMenu();
        }

        ImGui::PushItemWidth(200.0f);
        ImGui::SliderInt("##gamepad_mouse_speed", &config_input_gamepad_keys[pad].mouse_speed, 1, 15, "Speed = %d");
        ImGui::SliderInt("##gamepad_mouse_dead_zone", &config_input_gamepad_keys[pad].dead_zone, 0, 50, "Dead Zone = %d%%");
        ImGui::PopItemWidth();
        ImGui::EndMenu();
    }

    if (pad == 0)
    {
        ImGui::Separator();

        if (ImGui::BeginMenu("Stick", !keys))
        {
            ImGui::MenuItem("Invert X Axis", "", &config_input_gamepad.gamepad_invert_x_axis);
            ImGui::MenuItem("Invert Y Axis", "", &config_input_gamepad.gamepad_invert_y_axis);
            ImGui::PushItemWidth(200.0f);
            ImGui::SliderInt("##dead_zone", &config_input_gamepad.gamepad_dead_zone, 0, 50, "Dead Zone = %d%%");
            ImGui::PopItemWidth();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Button Configuration", !keys))
        {
            ImGui::TextDisabled("Game Port Buttons");
            ImGui::Separator();
            gamepad_configuration_item("Button 1:", &config_input_gamepad.gamepad_buttons[0], 0);
            gamepad_configuration_item("Button 2:", &config_input_gamepad.gamepad_buttons[1], 0);
            gamepad_configuration_item("Button 3:", &config_input_gamepad.gamepad_buttons[2], 0);
            gamepad_configuration_item("Button 4:", &config_input_gamepad.gamepad_buttons[3], 0);

            gui_popup_modal_gamepad(0);

            ImGui::EndMenu();
        }

        ImGui::Separator();

        if (ImGui::BeginMenu("Shortcut Configuration"))
        {
            ImGui::TextDisabled("Gamepad Shortcuts");
            ImGui::Separator();

            for (int i = 0; i < config_HotkeyIndex_COUNT; i++)
            {
                if ((hotkey_definitions[i].flags & HOTKEY_FLAG_GAMEPAD) == 0)
                    continue;

                char label[64];
                snprintf(label, sizeof(label), "%s:", hotkey_definitions[i].label);
                gamepad_configuration_item(label, &config_input_gamepad_shortcuts.gamepad_shortcuts[i], 0);
            }

            gui_popup_modal_gamepad(0);

            ImGui::EndMenu();
        }
    }

    ImGui::EndMenu();
}

static void gamepad_choice_selector(int pad, bool use)
{
    int choices[GDOX_GAMEPAD_MOUSE_COUNT];
    const GDOX_Gamepad_Info* infos = use ? k_gamepad_uses : k_gamepad_mice;
    int count = 0;

    if (use)
    {
        choices[count++] = GDOX_GAMEPAD_USE_KEYS;
        choices[count++] = pad == 0 ? GDOX_GAMEPAD_USE_JOYSTICK : GDOX_GAMEPAD_USE_OFF;
    }
    else
    {
        for (int i = 0; i < GDOX_GAMEPAD_MOUSE_COUNT; i++)
            choices[count++] = i;
    }

    int current = use ? gamepad_get_use(pad) : gamepad_get_mouse(pad);
    float text_width = 0.0f;

    for (int i = 0; i < count; i++)
        text_width = MAX(text_width, ImGui::CalcTextSize(infos[choices[i]].name).x);

    char label[32];
    snprintf(label, sizeof(label), "##gamepad_%s%d", use ? "use" : "mouse", pad + 1);
    ImGui::PushItemWidth(gui_get_combo_width(text_width));

    if (ImGui::BeginCombo(label, infos[current].name))
    {
        for (int i = 0; i < count; i++)
        {
            if (!ImGui::Selectable(infos[choices[i]].name, current == choices[i]))
                continue;

            if (use)
                gamepad_set_use(pad, choices[i]);
            else
                gamepad_set_mouse(pad, choices[i]);
        }

        ImGui::EndCombo();
    }
    else if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();

        if (use)
        {
            ImGui::Text("Keyboard and Mouse: each button presses a PC key");
            ImGui::Text("or clicks, a stick or the D-pad moves the mouse.");

            if (pad == 0)
                ImGui::Text("Joystick: plays the joystick on the game port.");
        }
        else
        {
            ImGui::Text("The stick or D-pad that moves the PC's mouse.");
            ImGui::Text("It sends no keys while it does.");
        }

        ImGui::Text("During a game, the change is for that game only.");
        ImGui::EndTooltip();
    }

    ImGui::PopItemWidth();
}

static void gamepad_key_item(int pad, int input, float offset)
{
    bool disabled = GamepadKeys::MovesMouse(gamepad_get_mouse(pad), input);
    int target = gamepad_get_target(pad, input);
    char label[96];

    ImGui::BeginDisabled(disabled);
    ImGui::Text("%s:", k_gamepad_inputs[input].name);
    ImGui::SameLine(offset);
    snprintf(label, sizeof(label), "%s##gamepad_key%d_%d", disabled ? "Mouse" : GamepadKeys::GetTargetLabel(target), pad,
        input);

    if (ImGui::Button(label, ImVec2(120, 0)))
    {
        gui_configured_gamepad_pad = pad;
        gui_configured_gamepad_input = input;
        ImGui::OpenPopup("Gamepad Key Configuration");
    }

    ImGui::SameLine();
    snprintf(label, sizeof(label), "X##gamepad_key_clear%d_%d", pad, input);

    if (ImGui::Button(label))
        gamepad_set_target(pad, input, GDOX_GAMEPAD_TARGET_NONE);

    ImGui::EndDisabled();
}

static void draw_savestate_slot_info(int slot)
{
    if (slot < 0 || slot >= 5)
        return;

    if (emu_savestates[slot].magic == GDOX_SAVESTATE_MAGIC)
    {
        if (emu_savestates[slot].version < GDOX_SAVESTATE_MIN_VERSION || emu_savestates[slot].version > GDOX_SAVESTATE_VERSION)
        {
            ImGui::TextColored(red, "This savestate is from an older version and will not work");

            if (emu_savestates[slot].emu_build[0] != 0)
            {
                ImGui::TextColored(red, "Use %s - %s", GDOX_TITLE, emu_savestates[slot].emu_build);
            }

            ImGui::Separator();
        }

        ImGui::Text("%s", emu_savestates[slot].rom_name);
        char date[64];
        get_date_time_string(emu_savestates[slot].timestamp, date, sizeof(date));
        ImGui::Text("%s", date);

        if (IsValidPointer(emu_savestates_screenshots[slot].data))
        {
            float width = (float)emu_savestates_screenshots[slot].width;
            float height = (float)emu_savestates_screenshots[slot].height;
            ImGui::Image((ImTextureID)(intptr_t)ogl_renderer_emu_savestates, ImVec2(320.0f, 240.0f),
                ImVec2(0, 0), ImVec2(width / (float)SYSTEM_TEXTURE_WIDTH, height / (float)SYSTEM_TEXTURE_HEIGHT));
        }
    }
    else
    {
        ImGui::TextColored(gray, "Slot %d is empty", slot + 1);
    }
}
