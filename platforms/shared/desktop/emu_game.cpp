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

#define EMU_GAME_IMPORT
#include "emu_game.h"

#include <algorithm>
#include <SDL3/SDL.h>
#include "emu.h"
#include "config.h"
#include "utils.h"
#include "emu_dos.h"
#include "emu_floppy.h"
#include "emu_hard_disk.h"
#include "emu_dev.h"
#include "gui_notifications.h"
#include "media/media_file.h"
#include "media/crc.h"

static GameDisk disk;
static int target = GDOX_GAME_TARGET_GAME;
static config_Machine game_machine;
static config_Machine base_machine;
static int game_joystick = -1;
static int base_joystick = -1;
static int game_midi = -1;
static int base_midi = -1;
static int game_cga_output = -1;
static int base_cga_output = -1;
static int game_midi_out = -1;
static int base_midi_out = -1;
static std::string game_midi_port;
static std::string base_midi_port;
static int game_mt32_model = -1;
static int base_mt32_model = -1;
static int game_volumes[GDOX_AUDIO_LEVEL_COUNT] = { -1, -1, -1, -1, -1, -1, -1, -1 };
static int base_volumes[GDOX_AUDIO_LEVEL_COUNT] = { -1, -1, -1, -1, -1, -1, -1, -1 };
static int game_mutes[GDOX_AUDIO_LEVEL_COUNT] = { -1, -1, -1, -1, -1, -1, -1, -1 };
static int base_mutes[GDOX_AUDIO_LEVEL_COUNT] = { -1, -1, -1, -1, -1, -1, -1, -1 };
static GDOX_Gamepad_Game game_gamepads[GDOX_MAX_GAMEPADS];
static GDOX_Gamepad_Game base_gamepads[GDOX_MAX_GAMEPADS];
static bool preparing = false;
static bool prepared = false;
static bool write_back_pending = false;
static GDOX_Machine_Config prepare_config;
static GDOX_Boot_Device saved_boot_device = GDOX_BOOT_DEVICE_AUTO;
static std::string hard_disk_images[GDOX_MAX_HARD_DISKS];
static FatImage_Report hard_disk_reports[GDOX_MAX_HARD_DISKS];
static std::string error_text;
static GameDisk_Result last_result;
static std::string last_result_name;
static bool game_floppies[2] = { false, false };
static bool game_cdrom = false;
static GameDisk_Found current_found;
static std::vector<std::pair<std::string, std::string> > game_keys;
static const char k_empty_overrides[] = "[geardox]\nversion = 1\n";
static const char* const k_game_keys[][2] = {
    { "machine", "model" }, { "machine", "cpu_mhz" }, { "machine", "turbo" }, { "machine", "ram_mb" },
    { "machine", "fpu" }, { "machine", "floppy_a_type" }, { "machine", "floppy_b_type" },
    { "machine", "cdrom_speed" }, { "machine", "drive_timing" }, { "machine", "video" }, { "machine", "cga_output" },
    { "machine", "voodoo" },
    { "sound", "card" }, { "sound", "irq" }, { "sound", "dma" }, { "sound", "mpu" }, { "sound", "gus" },
    { "sound", "midi" }, { "sound", "midi_out" }, { "sound", "midi_port" }, { "sound", "mt32_model" },
    { "input", "mouse" }, { "input", "joystick" } };

static std::string get_save_directory(void);
static const std::vector<std::pair<std::string, std::string> >& get_game_keys(void);
static bool same_overrides(const GameDescriptor& a, const GameDescriptor& b);
static void notify_saved(void);
static void notify_shadowed(void);
static void overlay_settings(config_Machine* machine, const GDOX_Machine_Settings& game);
static void update_machines(void);
static void apply_game_settings(void);
static std::string write_list(const std::vector<std::string>& paths, const char* name);
static void notify_write_back(void);
static void prepare_hard_disk(int id);

bool emu_game_is_content(const char* path)
{
    GameDisk::SetDatabaseEnabled(config_game.database);
    return GameDisk::IsGameContent(path);
}

bool emu_game_find(const char* path, std::vector<GameDisk_Found>* found)
{
    GameDisk::SetDatabaseEnabled(config_game.database);
    return GameDisk::Find(path, found);
}

bool emu_game_open(const GameDisk_Found& found, int game_target)
{
    error_text.clear();

    if (preparing || !emu_game_close())
        return false;

    if (emu_is_powered_on())
        emu_power_off();

    if (!disk.Open(found, get_save_directory().c_str(), get_save_directory().c_str(), config_game.write_back == 0,
        emu_dos_get_msdos_path().c_str(), emu_dos_get_ultrasnd_path().c_str(), emu_dos_get_glide_path().c_str()))
    {
        error_text = disk.GetError();
        return false;
    }

    disk.SetKeyboardLayout(config_input.keyboard_layout.c_str());
    current_found = found;
    target = CLAMP(game_target, 0, GDOX_GAME_TARGET_COUNT - 1);
    saved_boot_device = emu_get_core()->GetBootDevice();
    update_machines();
    apply_game_settings();
    emu_audio_update_levels();
    prepare_config = emu_get_core()->GetPendingMachineConfig();
    preparing = true;
    prepared = false;
    emu_load_game_async();
    return true;
}

// Touches nothing of the emulator's but the disc, while the machine is off
void emu_game_prepare_thread(void)
{
    prepared = disk.Prepare(prepare_config);

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        hard_disk_images[i].clear();
        memset(&hard_disk_reports[i], 0, sizeof(hard_disk_reports[i]));

        if (prepared)
            prepare_hard_disk(i);
    }
}

bool emu_game_start(void)
{
    preparing = false;
    GeardoxCore* core = emu_get_core();

    if (!prepared)
    {
        error_text = disk.GetError();
        disk.Close();
        core->ClearGameSettings();
        emu_update_joystick_type();
        emu_update_cga_output();
        return false;
    }

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
        emu_hard_disk_detach(i);

    if (!emu_hard_disk_attach_game(0, disk.GetImagePath(), disk.GetDescriptorPath()))
    {
        error_text = std::string("Unable to attach the game's disk ") + disk.GetImagePath();
        disk.Close();
        core->ClearGameSettings();
        return false;
    }

    for (int i = 1; i < GDOX_MAX_HARD_DISKS; i++)
    {
        if (!hard_disk_images[i].empty() && !emu_hard_disk_attach_game(i, hard_disk_images[i].c_str(), disk.GetHardDisk(i)))
            gui_notify(gui_NotificationWarning, ICON_MD_STORAGE, "Unable to attach the game's hard disk", disk.GetHardDisk(i));
    }

    for (int drive = 0; drive < 2; drive++)
    {
        const std::vector<std::string>& floppies = disk.GetFloppies(drive);

        if (floppies.empty())
            continue;

        std::string list = floppies.size() > 1 ? write_list(floppies, drive == 0 ? "a.m3u" : "b.m3u") : floppies[0];
        game_floppies[drive] = emu_floppy_insert(drive, list.c_str(), false, disk.IsWritingToOverlay());

        if (!game_floppies[drive])
            gui_notify(gui_NotificationWarning, ICON_MD_SAVE, "Unable to insert the game's floppy", list.c_str());
    }

    const std::vector<std::string>& cds = disk.GetCdRoms();

    if (!cds.empty())
    {
        bool floppies = false;

        if (cds.size() > 1)
            emu_cdrom_playlist_load(write_list(cds, "cd.m3u").c_str(), &floppies);
        else if (!emu_cdrom_playlist_select(cds[0].c_str()))
            emu_cdrom_playlist_clear();

        game_cdrom = core->LoadMedia(cds[0].c_str());

        if (!game_cdrom)
            gui_notify(gui_NotificationWarning, ICON_MD_ALBUM, "Unable to load the game's disc", cds[0].c_str());
    }

    core->SetGameTarget(target);

    if (target == GDOX_GAME_TARGET_DEBUG)
    {
        const GDOX_Game_Info& info = disk.GetDescriptor().GetInfo();
        bool voodoo = disk.UsesVoodooExe();
        emu_dev_prepare_run(voodoo ? info.voodoo_exe.c_str() : info.exe.c_str(),
            voodoo ? info.voodoo_args.c_str() : info.args.c_str());
    }

    core->SetBootDevice(disk.GetDescriptor().GetInfo().boot_floppy ? GDOX_BOOT_DEVICE_FLOPPY_0 : GDOX_BOOT_DEVICE_HARD_DISK);
    emu_update_joystick_type();
    emu_update_cga_output();
    config_push_recent_game(disk.GetDescriptorPath());

    if (disk.WasRecovered() || (disk.GetRefusedPath()[0] != 0))
    {
        last_result = disk.GetLastWriteBack();
        last_result_name = disk.GetName();
    }

    if (disk.WasRecovered())
    {
        const GameDisk_Result& r = disk.GetLastWriteBack();
        char detail[128];
        snprintf(detail, sizeof(detail), "%u files written back, %u conflicts", r.written, r.conflicts);
        gui_notify(gui_NotificationWarning, ICON_MD_RESTORE, "The last session didn't end, its changes were saved", detail);
    }

    if (disk.GetRefusedPath()[0] != 0)
        gui_notify(gui_NotificationError, ICON_MD_SAVE, "The last session's disk didn't add up, its changes weren't saved",
            disk.GetRefusedPath());

    if (disk.IsFolderReadOnly())
        gui_notify(gui_NotificationInfo, ICON_MD_FOLDER, "The game's folder is read-only: saves go to the save folder",
            disk.GetOverlayPath(), NULL, gui_NotificationDurationLong);

    if (disk.IsFromDatabase())
    {
        std::string detail = disk.GetName();

        if (disk.HasReleaseSettings())
            detail += ", with this copy's sound settings";
        else if (disk.HasWithheldSettings())
            detail += ", with the default sound settings";

        gui_notify(gui_NotificationInfo, ICON_MD_VIDEOGAME_ASSET, "Recognised from the game database", detail.c_str());
    }

    if (disk.GetLeftOut() != 0)
    {
        char detail[64];
        snprintf(detail, sizeof(detail), "%u items left out", disk.GetLeftOut());
        gui_notify(gui_NotificationWarning, ICON_MD_STORAGE, "Part of the game's folder didn't fit DOS", detail);
    }

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        if (hard_disk_reports[i].result == FAT_IMAGE_CHANGED)
            gui_notify(gui_NotificationWarning, ICON_MD_STORAGE, "A hard disk's folder changed since its disk was made",
                disk.GetHardDisk(i));
    }

    notify_shadowed();
    Log("Game: %s, %s", disk.GetName(), disk.WasBuilt() ? "disk built" : "disk reused");
    return emu_power_on();
}

bool emu_game_close(void)
{
    if (preparing)
        return false;

    if (!disk.IsOpen())
        return true;

    GeardoxCore* core = emu_get_core();

    if (emu_is_powered_on())
        emu_power_off();

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        Emu_HardDiskInfo info;
        emu_hard_disk_get_info(i, &info);

        if (info.game)
            emu_hard_disk_detach(i);
    }

    for (int drive = 0; drive < 2; drive++)
    {
        if (game_floppies[drive])
            emu_floppy_eject(drive, false);

        game_floppies[drive] = false;
    }

    if (game_cdrom)
    {
        emu_eject_media();
        emu_cdrom_playlist_clear();
    }

    game_cdrom = false;

    bool written = disk.Finish();
    last_result = disk.GetLastWriteBack();
    last_result_name = disk.GetName();
    write_back_pending = !written;
    notify_write_back();

    if (written)
        disk.Close();

    core->ClearGameSettings();
    core->SetGameTarget(-1);
    core->SetBootDevice(saved_boot_device);
    game_joystick = -1;
    game_cga_output = -1;
    game_midi = -1;
    game_midi_out = -1;
    game_midi_port.clear();
    game_mt32_model = -1;

    for (int i = 0; i < GDOX_AUDIO_LEVEL_COUNT; i++)
    {
        game_volumes[i] = -1;
        game_mutes[i] = -1;
    }

    for (int i = 0; i < GDOX_MAX_GAMEPADS; i++)
        GamepadKeys::ClearGame(&game_gamepads[i]);

    emu_update_joystick_type();
    emu_update_cga_output();
    emu_audio_update_levels();
    emu_apply_machine_settings();
    return true;
}

bool emu_game_is_open(void)
{
    return disk.IsOpen() && !write_back_pending;
}

const char* emu_game_get_name(void)
{
    return disk.IsOpen() ? disk.GetName() : "";
}

const char* emu_game_get_error(void)
{
    return error_text.c_str();
}

void emu_game_set_target(int game_target)
{
    if (!emu_game_is_open())
        return;

    target = CLAMP(game_target, 0, GDOX_GAME_TARGET_COUNT - 1);
    emu_get_core()->SetGameTarget(target);

    if (emu_is_powered_on())
        emu_reset();
    else
        emu_power_on();
}

int emu_game_get_target(void)
{
    return target;
}

bool emu_game_update(void)
{
    GeardoxCore* core = emu_get_core();

    if (!core->IsPowerOffRequested())
        return false;

    core->AcknowledgePowerOff();

    if (emu_game_is_open())
    {
        gui_notify(gui_NotificationInfo, ICON_MD_POWER_SETTINGS_NEW, "The game powered the machine off", disk.GetName(), "power");
        emu_game_close();

        if (config_game.close_on_power_off)
        {
            SDL_Event event;
            SDL_zero(event);
            event.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&event);
        }

        return true;
    }

    emu_power_off();
    gui_notify(gui_NotificationInfo, ICON_MD_POWER_SETTINGS_NEW, "Powered off by software", NULL, "power");
    return true;
}

config_Machine* emu_game_get_machine(void)
{
    return emu_game_is_open() ? &game_machine : &config_machine;
}

const config_Machine* emu_game_get_base_machine(void)
{
    return emu_game_is_open() ? &base_machine : &config_machine;
}

int emu_game_get_source(const char* section, const char* key)
{
    if (!emu_game_is_open())
        return Emu_Game_Source_User;

    if (disk.GetOverrides().Has(section, key))
        return Emu_Game_Source_Change;

    return disk.GetBase().Has(section, key) ? Emu_Game_Source_Descriptor : Emu_Game_Source_User;
}

bool emu_game_is_from_database(void)
{
    return emu_game_is_open() && disk.IsFromDatabase();
}

// Machine menu changes during a game go to its overrides: only what differs from the descriptor over the user's
void emu_game_apply_machine(void)
{
    if (!emu_game_is_open())
        return;

    GameDescriptor before = disk.GetOverrides();
    GameDescriptor overrides = before;
    const std::vector<std::pair<std::string, std::string> >& keys = get_game_keys();

    if (overrides.IsEmpty())
        overrides.Parse("", k_empty_overrides, sizeof(k_empty_overrides) - 1);

    for (size_t i = 0; i < keys.size(); i++)
        overrides.Unset(keys[i].first.c_str(), keys[i].second.c_str());

    const config_Machine& m = game_machine;
    const config_Machine& b = base_machine;
    static const char* const k_floppy_types[GDOX_FLOPPY_TYPE_COUNT] = { "", "360k", "1200k", "720k", "1440k", "2880k" };
    static const char* const k_floppy_keys[2] = { "floppy_a_type", "floppy_b_type" };

    if ((m.model != b.model) && (m.model >= 0))
        overrides.Set("machine", "model", k_machine_profiles[m.model].key);

    if ((m.cpu_mhz != b.cpu_mhz) && (m.cpu_mhz > 0))
        overrides.Set("machine", "cpu_mhz", std::to_string(m.cpu_mhz).c_str());

    if (m.ram_mb != b.ram_mb)
        overrides.Set("machine", "ram_mb", std::to_string(m.ram_mb).c_str());

    if ((m.fpu != 0) != (b.fpu != 0))
        overrides.Set("machine", "fpu", m.fpu != 0 ? "80387" : "none");

    for (int i = 0; i < 2; i++)
    {
        if ((m.floppy_type[i] != b.floppy_type[i]) && (m.floppy_type[i] > 0) && (m.floppy_type[i] < GDOX_FLOPPY_TYPE_COUNT))
            overrides.Set("machine", k_floppy_keys[i], k_floppy_types[m.floppy_type[i]]);
    }

    if ((m.video_card != b.video_card) && (m.video_card >= 0))
        overrides.Set("machine", "video", k_video_cards[m.video_card].id);

    if (m.turbo != b.turbo)
        overrides.Set("machine", "turbo", m.turbo ? "on" : "off");

    if (m.sound_card != b.sound_card)
        overrides.Set("sound", "card", k_sound_cards[m.sound_card].id);

    if (m.sound_irq != b.sound_irq)
        overrides.Set("sound", "irq", std::to_string(m.sound_irq).c_str());

    if (m.sound_dma != b.sound_dma)
        overrides.Set("sound", "dma", std::to_string(m.sound_dma).c_str());

    if (m.mpu != b.mpu)
        overrides.Set("sound", "mpu", m.mpu == GDOX_MPU_INTELLIGENT ? "on" : m.mpu == GDOX_MPU_UART ? "uart" : "off");

    if (m.gus != b.gus)
        overrides.Set("sound", "gus", m.gus ? "on" : "off");

    if (m.voodoo != b.voodoo)
        overrides.Set("machine", "voodoo", m.voodoo ? "on" : "off");

    if (m.mouse != b.mouse)
        overrides.Set("input", "mouse", k_mice[m.mouse].id);

    if ((game_joystick != base_joystick) && (game_joystick >= 0))
        overrides.Set("input", "joystick", k_joysticks[game_joystick].id);

    if ((game_cga_output != base_cga_output) && (game_cga_output >= 0))
        overrides.Set("machine", "cga_output", k_cga_outputs[game_cga_output].id);

    if ((game_midi != base_midi) && (game_midi >= 0))
        overrides.Set("sound", "midi", k_midi_synths[game_midi].id);

    if ((game_midi_out != base_midi_out) && (game_midi_out >= 0))
        overrides.Set("sound", "midi_out", k_midi_outs[game_midi_out].id);

    if (game_midi_port != base_midi_port)
        overrides.Set("sound", "midi_port", game_midi_port.c_str());

    if ((game_mt32_model != base_mt32_model) && (game_mt32_model >= 0))
        overrides.Set("sound", "mt32_model", k_mt32_models[game_mt32_model].id);

    for (int i = 0; i < GDOX_AUDIO_LEVEL_COUNT; i++)
    {
        if ((game_volumes[i] != base_volumes[i]) && (game_volumes[i] >= 0))
            overrides.Set("sound", k_audio_levels[i].volume_key, std::to_string(game_volumes[i]).c_str());

        if ((game_mutes[i] != base_mutes[i]) && (game_mutes[i] >= 0) && IsValidPointer(k_audio_levels[i].mute_key))
            overrides.Set("sound", k_audio_levels[i].mute_key, game_mutes[i] == 1 ? "on" : "off");
    }

    for (int p = 0; p < GDOX_MAX_GAMEPADS; p++)
    {
        const GDOX_Gamepad_Game& gamepad = game_gamepads[p];
        const GDOX_Gamepad_Game& base = base_gamepads[p];
        const char* section = GameDescriptor::GetGamepadSection(p);

        if ((gamepad.use != base.use) && (gamepad.use >= 0))
            overrides.Set(section, "use", k_gamepad_uses[gamepad.use].id);

        if ((gamepad.mouse != base.mouse) && (gamepad.mouse >= 0))
            overrides.Set(section, "mouse", k_gamepad_mice[gamepad.mouse].id);

        for (int i = 0; i < GDOX_GAMEPAD_INPUT_COUNT; i++)
        {
            if ((gamepad.targets[i] != base.targets[i]) && (gamepad.targets[i] >= 0))
                overrides.Set(section, k_gamepad_inputs[i].id, GamepadKeys::GetTargetName(gamepad.targets[i]));
        }
    }

    if (m.cdrom_speed != b.cdrom_speed)
        overrides.Set("machine", "cdrom_speed", k_cdrom_speeds[m.cdrom_speed].id);

    if ((m.drive_timing != b.drive_timing) && (m.drive_timing >= 0))
        overrides.Set("machine", "drive_timing", k_drive_timings[m.drive_timing].id);

    if (!same_overrides(before, overrides))
    {
        if (disk.SaveOverrides(overrides))
            notify_saved();
        else
            gui_notify(gui_NotificationError, NULL, "Unable to save the game's settings", disk.GetError());
    }

    apply_game_settings();
    emu_update_joystick_type();
    emu_update_cga_output();
}

int emu_game_get_joystick(void)
{
    return emu_game_is_open() ? game_joystick : -1;
}

int emu_game_get_base_joystick(void)
{
    return emu_game_is_open() ? base_joystick : -1;
}

void emu_game_set_joystick(int joystick)
{
    if (!emu_game_is_open())
        return;

    game_joystick = joystick;
    emu_game_apply_machine();
}

const GDOX_Gamepad_Game* emu_game_get_gamepad(int pad)
{
    if (!emu_game_is_open() || (pad < 0) || (pad >= GDOX_MAX_GAMEPADS))
        return NULL;

    return &game_gamepads[pad];
}

const GDOX_Gamepad_Game* emu_game_get_base_gamepad(int pad)
{
    if (!emu_game_is_open() || (pad < 0) || (pad >= GDOX_MAX_GAMEPADS))
        return NULL;

    return &base_gamepads[pad];
}

void emu_game_set_gamepad(int pad, const GDOX_Gamepad_Game& gamepad)
{
    if (!emu_game_is_open() || (pad < 0) || (pad >= GDOX_MAX_GAMEPADS))
        return;

    game_gamepads[pad] = gamepad;
    emu_game_apply_machine();
}

int emu_game_get_cga_output(void)
{
    return emu_game_is_open() ? game_cga_output : -1;
}

int emu_game_get_base_cga_output(void)
{
    return emu_game_is_open() ? base_cga_output : -1;
}

void emu_game_set_cga_output(int output)
{
    if (!emu_game_is_open())
        return;

    game_cga_output = output;
    emu_game_apply_machine();
}

int emu_game_get_midi(void)
{
    return emu_game_is_open() ? game_midi : -1;
}

int emu_game_get_base_midi(void)
{
    return emu_game_is_open() ? base_midi : -1;
}

void emu_game_set_midi(int synth)
{
    if (!emu_game_is_open())
        return;

    game_midi = synth;
    emu_game_apply_machine();
}

int emu_game_get_midi_out(void)
{
    return emu_game_is_open() ? game_midi_out : -1;
}

int emu_game_get_base_midi_out(void)
{
    return emu_game_is_open() ? base_midi_out : -1;
}

void emu_game_set_midi_out(int out, const char* port)
{
    if (!emu_game_is_open())
        return;

    game_midi_out = out;
    game_midi_port = port;
    emu_game_apply_machine();
}

const char* emu_game_get_midi_port(void)
{
    return emu_game_is_open() ? game_midi_port.c_str() : "";
}

const char* emu_game_get_base_midi_port(void)
{
    return emu_game_is_open() ? base_midi_port.c_str() : "";
}

int emu_game_get_mt32_model(void)
{
    return emu_game_is_open() ? game_mt32_model : -1;
}

int emu_game_get_base_mt32_model(void)
{
    return emu_game_is_open() ? base_mt32_model : -1;
}

void emu_game_set_mt32_model(int model)
{
    if (!emu_game_is_open())
        return;

    game_mt32_model = model;
    emu_game_apply_machine();
}

int emu_game_get_volume(int level)
{
    return emu_game_is_open() ? game_volumes[level] : -1;
}

int emu_game_get_base_volume(int level)
{
    return emu_game_is_open() ? base_volumes[level] : -1;
}

int emu_game_get_mute(int level)
{
    return emu_game_is_open() ? game_mutes[level] : -1;
}

int emu_game_get_base_mute(int level)
{
    return emu_game_is_open() ? base_mutes[level] : -1;
}

void emu_game_set_level(int level, int volume, int mute, bool save)
{
    if (!emu_game_is_open())
        return;

    game_volumes[level] = volume;
    game_mutes[level] = mute;

    if (save)
        emu_game_apply_machine();
}

bool emu_game_has_overrides(void)
{
    return emu_game_is_open() && !disk.GetOverrides().IsEmpty();
}

bool emu_game_reset_settings(void)
{
    if (!emu_game_is_open() || !disk.ClearOverrides())
        return false;

    update_machines();
    apply_game_settings();
    emu_update_joystick_type();
    emu_update_cga_output();
    emu_audio_update_levels();
    return true;
}

// The descriptor with its overrides as one file. Elsewhere than its folder, its paths become absolute
bool emu_game_save_descriptor(const char* path)
{
    if (!emu_game_is_open())
        return false;

    GameDescriptor out = disk.GetDescriptor();
    std::string from = disk.GetDescriptorPath();
    char directory[GDOX_MAX_PATH];
    char base[GDOX_MAX_PATH];
    get_directory(path, directory, sizeof(directory));
    get_directory(from.c_str(), base, sizeof(base));

    if (strcmp(directory, base) != 0)
    {
        out.Set("game", "source", disk.GetSourcePath());
        static const char* const k_lists[3] = { "floppy_a", "floppy_b", "cdrom" };

        for (int i = 0; i < 3; i++)
        {
            const std::vector<std::string>& paths = i < 2 ? disk.GetFloppies(i) : disk.GetCdRoms();
            std::string value;

            for (size_t p = 0; p < paths.size(); p++)
                value += (p > 0 ? ", " : "") + paths[p];

            if (!value.empty())
                out.Set("drives", k_lists[i], value.c_str());
        }

        for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
        {
            if (disk.GetHardDisk(i)[0] != 0)
                out.Set("drives", GameDescriptor::GetHardDiskKey(i), disk.GetHardDisk(i));
        }
    }

    out.SetKeys(emu_get_core()->GetKeyMap()->GetMerged());
    return out.WriteFile(path);
}

std::string emu_game_get_key_map_path(void)
{
    if (!emu_game_is_open())
        return "";

    char settings[GDOX_MAX_PATH];
    char name[GDOX_MAX_PATH];
    char path[GDOX_MAX_PATH];
    get_filename_without_extension(disk.GetOverridesPath(), settings, sizeof(settings));
    get_filename_without_extension(settings, name, sizeof(name));
    join_path(get_save_directory().c_str(), (std::string(name) + ".keys").c_str(), path, sizeof(path));
    return path;
}

const char* emu_game_get_keys(void)
{
    return emu_game_is_open() ? disk.GetDescriptor().GetInfo().keys.c_str() : "";
}

bool emu_game_retry_write_back(void)
{
    if (!write_back_pending || !disk.IsOpen())
        return false;

    write_back_pending = !disk.RetryFinish();
    last_result = disk.GetLastWriteBack();
    notify_write_back();

    if (!write_back_pending)
        disk.Close();

    return !write_back_pending;
}

bool emu_game_is_write_back_pending(void)
{
    return write_back_pending;
}

const GameDisk_Result& emu_game_get_last_write_back(void)
{
    return last_result;
}

const char* emu_game_get_last_write_back_name(void)
{
    return last_result_name.c_str();
}

const char* emu_game_get_saves_path(void)
{
    if (!disk.IsOpen())
        return "";

    return disk.IsWritingToOverlay() ? disk.GetOverlayPath() : disk.GetSourcePath();
}

bool emu_game_has_setup(void)
{
    return emu_game_is_open() && !disk.GetDescriptor().GetInfo().setup_exe.empty();
}

GameDisk* emu_game_get_disk(void)
{
    return &disk;
}

void emu_game_list_programs(const char* folder, std::vector<std::string>* programs)
{
    programs->clear();
    std::vector<std::string> folders;
    std::vector<std::string> files;

    if (!MediaFile::ListDirectory(folder, folders, files))
        return;

    for (size_t i = 0; i < files.size(); i++)
    {
        const char* name = files[i].c_str();

        if (ends_with_no_case(name, ".exe") || ends_with_no_case(name, ".com") || ends_with_no_case(name, ".bat"))
            programs->push_back(files[i]);
    }

    for (size_t f = 0; f < folders.size(); f++)
    {
        if (folders[f][0] == '.')
            continue;

        std::string sub(folder);
        append_path_component(sub, folders[f].c_str());
        std::vector<std::string> sub_folders;
        std::vector<std::string> sub_files;

        if (!MediaFile::ListDirectory(sub.c_str(), sub_folders, sub_files))
            continue;

        for (size_t i = 0; i < sub_files.size(); i++)
        {
            const char* name = sub_files[i].c_str();

            if (ends_with_no_case(name, ".exe") || ends_with_no_case(name, ".com") || ends_with_no_case(name, ".bat"))
                programs->push_back(folders[f] + "\\" + sub_files[i]);
        }
    }

    std::sort(programs->begin(), programs->end());
}

bool emu_game_new_descriptor(const char* folder, const char* program, std::string* path)
{
    std::string source = trim_trailing_separators(folder);
    std::string name = path_filename(source);
    std::string dos_folder;

    for (size_t i = 0; (i < name.size()) && (dos_folder.size() < 8); i++)
    {
        char c = name[i];

        if (((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')))
            dos_folder += (char)(((c >= 'a') && (c <= 'z')) ? c - 32 : c);
    }

    if ((dos_folder == "FREEDOS") || (dos_folder == "TEMP"))
        dos_folder.clear();

    error_text.clear();
    std::string head = "[geardox]\nversion = 1\n\n[game]\nname = " + name + "\n";
    std::string tail = "folder = C:\\" + dos_folder + "\n\n[run]\nexe = " + program + "\n";
    GameDescriptor descriptor;
    char file[GDOX_MAX_PATH];

    if (config_game.write_back == 0)
    {
        std::string text = head + tail;
        u64 size = 0;
        s64 modified = 0;
        join_path(source.c_str(), (name + ".gdox").c_str(), file, sizeof(file));

        if (MediaFile::GetFileInfo(file, &size, &modified))
        {
            error_text = std::string("The game's folder already has ") + name + ".gdox: open or edit it instead.";
            return false;
        }

        if (descriptor.Parse("", text.data(), text.size()) && descriptor.WriteFile(file))
        {
            *path = file;
            return true;
        }
    }

    std::string text = head + "source = " + source + "\n" + tail;

    if (!descriptor.Parse("", text.data(), text.size()))
    {
        error_text = descriptor.GetError();
        return false;
    }

    std::string directory = get_save_directory();
    std::string id = crc_key(source);
    create_directory_if_not_exists(directory.c_str());
    join_path(directory.c_str(), (name + "." + id + ".gdox").c_str(), file, sizeof(file));
    *path = file;

    if (!descriptor.WriteFile(file))
    {
        error_text = std::string("Unable to write ") + file;
        return false;
    }

    return true;
}

static void prepare_hard_disk(int id)
{
    std::string source = disk.GetHardDisk(id);

    if (source.empty())
        return;

    if (!FatImage::IsSource(source.c_str()))
    {
        hard_disk_images[id] = source;
        return;
    }

    if (!FatImage::PrepareSource(source.c_str(), get_save_directory().c_str(), false, &hard_disk_images[id],
        &hard_disk_reports[id]))
        hard_disk_images[id].clear();
}

static std::string get_save_directory(void)
{
    bool custom = (Directory_Location)config_emulator.savefiles_dir_option == Directory_Location_Custom;
    return custom ? config_emulator.savefiles_path : std::string(config_root_path);
}

static const std::vector<std::pair<std::string, std::string> >& get_game_keys(void)
{
    if (!game_keys.empty())
        return game_keys;

    for (size_t i = 0; i < sizeof(k_game_keys) / sizeof(k_game_keys[0]); i++)
        game_keys.push_back(std::make_pair(std::string(k_game_keys[i][0]), std::string(k_game_keys[i][1])));

    for (int i = 0; i < GDOX_AUDIO_LEVEL_COUNT; i++)
    {
        game_keys.push_back(std::make_pair(std::string("sound"), std::string(k_audio_levels[i].volume_key)));

        if (IsValidPointer(k_audio_levels[i].mute_key))
            game_keys.push_back(std::make_pair(std::string("sound"), std::string(k_audio_levels[i].mute_key)));
    }

    for (int p = 0; p < GDOX_MAX_GAMEPADS; p++)
    {
        std::string section = GameDescriptor::GetGamepadSection(p);
        game_keys.push_back(std::make_pair(section, std::string("use")));
        game_keys.push_back(std::make_pair(section, std::string("mouse")));

        for (int i = 0; i < GDOX_GAMEPAD_INPUT_COUNT; i++)
            game_keys.push_back(std::make_pair(section, std::string(k_gamepad_inputs[i].id)));
    }

    return game_keys;
}

static bool same_overrides(const GameDescriptor& a, const GameDescriptor& b)
{
    return (a.IsEmpty() && b.IsEmpty()) || (a.Write() == b.Write());
}

static void notify_saved(void)
{
    std::string name = disk.GetName();

    if (disk.GetOverrides().IsEmpty())
        gui_notify(gui_NotificationInfo, ICON_MD_VIDEOGAME_ASSET, (name + " uses its defaults again").c_str(), NULL,
            "game_settings");
    else
        gui_notify(gui_NotificationInfo, ICON_MD_VIDEOGAME_ASSET, ("Saved for " + name + " only").c_str(),
            "Forget them in Game > Game Files", "game_settings");
}

static void notify_shadowed(void)
{
    const GameDescriptor& overrides = disk.GetOverrides();
    const GameDescriptor& base = disk.GetBase();
    u64 size = 0;
    s64 descriptor_time = 0;
    s64 overrides_time = 0;
    bool editable = current_found.inner.empty() || GameDescriptor::IsDescriptor(current_found.inner.c_str());

    if (overrides.IsEmpty() || disk.IsFromDatabase() || !editable ||
        !MediaFile::GetFileInfo(disk.GetDescriptorPath(), &size, &descriptor_time) ||
        !MediaFile::GetFileInfo(disk.GetOverridesPath(), &size, &overrides_time) || (descriptor_time <= overrides_time))
        return;

    std::vector<std::pair<std::string, std::string> > keys = get_game_keys();
    keys.push_back(std::make_pair(std::string("dos"), std::string("system")));
    std::string names;

    for (size_t i = 0; i < keys.size(); i++)
    {
        const char* section = keys[i].first.c_str();
        const char* key = keys[i].second.c_str();

        if (overrides.Has(section, key) && base.Has(section, key) && (overrides.Get(section, key) != base.Get(section, key)))
            names += (names.empty() ? "" : ", ") + keys[i].first + "." + keys[i].second;
    }

    if (names.empty())
        return;

    names += "\nForget them in Game > Game Files";
    gui_notify(gui_NotificationWarning, ICON_MD_VIDEOGAME_ASSET, "Your changes override the edited game descriptor",
        names.c_str());
    disk.SaveOverrides(GameDescriptor(overrides));
}

int emu_game_get_system(void)
{
    return disk.IsOpen() ? disk.GetSystem() : -1;
}

int emu_game_get_base_system(void)
{
    if (!disk.IsOpen())
        return -1;

    int system = disk.GetBase().GetInfo().system;
    return system >= 0 ? system : disk.HasMSDOS() ? (int)GDOX_GAME_SYSTEM_MSDOS : (int)GDOX_GAME_SYSTEM_FREEDOS;
}

// The open game's own DOS, kept with its overrides. The disk is built again when the game opens next
bool emu_game_set_system(int system)
{
    if (!emu_game_is_open())
        return false;

    GameDescriptor before = disk.GetOverrides();
    GameDescriptor overrides = before;

    if (overrides.IsEmpty())
        overrides.Parse("", k_empty_overrides, sizeof(k_empty_overrides) - 1);

    if (system < 0)
        overrides.Unset("dos", "system");
    else
        overrides.Set("dos", "system", system == GDOX_GAME_SYSTEM_MSDOS ? "msdos" : "freedos");

    if (same_overrides(before, overrides))
        return true;

    if (!disk.SaveOverrides(overrides))
    {
        gui_notify(gui_NotificationError, NULL, "Unable to save the game's settings", disk.GetError());
        return false;
    }

    notify_saved();
    return true;
}

bool emu_game_get_found(GameDisk_Found* found)
{
    if (!disk.IsOpen())
        return false;

    *found = current_found;
    return true;
}

void emu_game_config_to_settings(const config_Machine& machine, GDOX_Machine_Settings* settings)
{
    settings->model = machine.model;
    settings->cpu_mhz = machine.cpu_mhz;
    settings->ram_mb = machine.ram_mb;
    settings->fpu = machine.fpu;
    settings->floppy_type[0] = machine.floppy_type[0];
    settings->floppy_type[1] = machine.floppy_type[1];
    settings->sound_card = machine.sound_card;
    settings->sound_irq = machine.sound_irq;
    settings->sound_dma = machine.sound_dma;
    settings->mpu = machine.mpu;
    settings->gus = machine.gus ? 1 : 0;
    settings->video_card = machine.video_card;
    settings->voodoo = machine.voodoo ? 1 : 0;
    settings->mouse = machine.mouse;
    settings->cdrom = machine.cdrom;
    settings->cdrom_speed = machine.cdrom_speed;
    settings->drive_timing = machine.drive_timing;
    settings->speed_mode = machine.turbo ? 1 : 0;
}

static void overlay_settings(config_Machine* machine, const GDOX_Machine_Settings& game)
{
    machine->model = game.model >= 0 ? game.model : machine->model;
    machine->cpu_mhz = game.cpu_mhz > 0 ? game.cpu_mhz : machine->cpu_mhz;
    machine->ram_mb = game.ram_mb > 0 ? game.ram_mb : machine->ram_mb;
    machine->fpu = game.fpu >= 0 ? game.fpu : machine->fpu;

    for (int i = 0; i < 2; i++)
        machine->floppy_type[i] = game.floppy_type[i] > 0 ? game.floppy_type[i] : machine->floppy_type[i];

    machine->sound_card = game.sound_card >= 0 ? game.sound_card : machine->sound_card;
    machine->sound_irq = game.sound_irq > 0 ? game.sound_irq : machine->sound_irq;
    machine->sound_dma = game.sound_dma > 0 ? game.sound_dma : machine->sound_dma;
    machine->mpu = game.mpu >= 0 ? game.mpu : machine->mpu;
    machine->gus = game.gus >= 0 ? game.gus != 0 : machine->gus;
    machine->video_card = game.video_card >= 0 ? game.video_card : machine->video_card;
    machine->voodoo = game.voodoo >= 0 ? game.voodoo != 0 : machine->voodoo;
    machine->mouse = game.mouse >= 0 ? game.mouse : machine->mouse;
    machine->cdrom = game.cdrom >= 0 ? game.cdrom : machine->cdrom;
    machine->cdrom_speed = game.cdrom_speed >= 0 ? game.cdrom_speed : machine->cdrom_speed;
    machine->drive_timing = game.drive_timing >= 0 ? game.drive_timing : machine->drive_timing;
    machine->turbo = game.speed_mode >= 0 ? game.speed_mode != 0 : machine->turbo;
}

// The menus show the game's machine: the descriptor and its overrides over the user's. The base leaves out the
// overrides, so a change can be told from it
static void update_machines(void)
{
    GDOX_Machine_Settings user;
    GDOX_Machine_Settings game;
    GDOX_Machine_Settings without;
    const GameDescriptor& overrides = disk.GetOverrides();
    const GameDescriptor& base = disk.GetBase();
    const std::vector<std::pair<std::string, std::string> >& keys = get_game_keys();
    GameDescriptor stripped = disk.GetDescriptor();

    emu_game_config_to_settings(config_machine, &user);
    disk.GetGameSettings(user, &game);
    game_machine = config_machine;
    overlay_settings(&game_machine, game);

    for (size_t i = 0; i < keys.size(); i++)
    {
        const char* section = keys[i].first.c_str();
        const char* key = keys[i].second.c_str();

        if (!overrides.Has(section, key))
            continue;

        if (base.Has(section, key))
            stripped.Set(section, key, base.Get(section, key).c_str());
        else
            stripped.Unset(section, key);
    }

    GameDisk::GetGameSettings(stripped, user, &without, disk.GetSystem());
    base_machine = config_machine;
    overlay_settings(&base_machine, without);
    game_joystick = disk.GetDescriptor().GetInfo().joystick;
    base_joystick = stripped.GetInfo().joystick;
    game_cga_output = disk.GetDescriptor().GetInfo().cga_output;
    base_cga_output = stripped.GetInfo().cga_output;
    game_midi = disk.GetDescriptor().GetInfo().midi;
    base_midi = stripped.GetInfo().midi;
    game_midi_out = disk.GetDescriptor().GetInfo().midi_out;
    base_midi_out = stripped.GetInfo().midi_out;
    game_midi_port = disk.GetDescriptor().GetInfo().midi_port;
    base_midi_port = stripped.GetInfo().midi_port;
    game_mt32_model = disk.GetDescriptor().GetInfo().mt32_model;
    base_mt32_model = stripped.GetInfo().mt32_model;

    for (int i = 0; i < GDOX_AUDIO_LEVEL_COUNT; i++)
    {
        game_volumes[i] = disk.GetDescriptor().GetInfo().volumes[i];
        base_volumes[i] = stripped.GetInfo().volumes[i];
        game_mutes[i] = disk.GetDescriptor().GetInfo().mutes[i];
        base_mutes[i] = stripped.GetInfo().mutes[i];
    }

    for (int i = 0; i < GDOX_MAX_GAMEPADS; i++)
    {
        game_gamepads[i] = disk.GetDescriptor().GetInfo().gamepads[i];
        base_gamepads[i] = stripped.GetInfo().gamepads[i];
    }
}

static void apply_game_settings(void)
{
    GDOX_Machine_Settings user;
    GDOX_Machine_Settings game;
    emu_game_config_to_settings(config_machine, &user);
    disk.GetGameSettings(user, &game);
    emu_get_core()->SetGameSettings(game, disk.GetName());
}

static std::string write_list(const std::vector<std::string>& paths, const char* name)
{
    std::string text;

    for (size_t i = 0; i < paths.size(); i++)
        text += paths[i] + "\n";

    std::string list = std::string(disk.GetImagePath()) + "." + name;
    MediaFile* file = MediaFile::OpenNewFile(list.c_str());

    if (IsValidPointer(file))
    {
        file->WriteAt(0, text.data(), text.size());
        file->Flush();
        SafeDelete(file);
    }

    return list;
}

static void notify_write_back(void)
{
    const GameDisk_Result& r = disk.GetLastWriteBack();
    std::string detail;

    if (!r.done)
    {
        if (r.refused)
        {
            for (size_t i = 0; i < r.notes.size(); i++)
                detail += (i > 0 ? "\n" : "") + r.notes[i];

            gui_notify(gui_NotificationError, ICON_MD_SAVE, "The game's changes weren't written back", detail.c_str());
        }

        return;
    }

    char message[128];
    snprintf(message, sizeof(message), "%s: %u written, %u deleted", disk.GetName(), r.written, r.deleted + r.directories);

    for (size_t i = 0; i < r.notes.size() && i < 4; i++)
        detail += (i > 0 ? "\n" : "") + r.notes[i];

    if ((r.conflicts > 0) || (r.kept > 0) || (r.startup > 0))
        gui_notify(gui_NotificationWarning, ICON_MD_SAVE, message, detail.c_str());
    else if ((r.written > 0) || (r.deleted > 0))
        gui_notify(gui_NotificationInfo, ICON_MD_SAVE, message);
}
