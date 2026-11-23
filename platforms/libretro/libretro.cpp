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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include "libretro.h"
#include "geardox.h"
#include "media/media_file.h"
#include "media/crc.h"
#include "audio/midi_synth_host.h"
#include "common/idle_flush.h"
#include "drive/fdc.h"
#include "drive/floppy_disk.h"
#include "drive/floppy_image.h"
#include "drive/floppy_set.h"
#include "drive/content.h"
#include "drive/hard_disk_image.h"
#include "drive/fat_image.h"
#include "drive/dos_disk.h"
#include "drive/game_disk.h"
#include "drive/folder_disk.h"
#include "drive/glide_import.h"
#include "video/video.h"
#include "video/voodoo.h"
#include "libretro_core_options.h"

#define RETRO_DEVICE_PC_JOYSTICK      RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 0)
#define RETRO_DEVICE_GAMEPAD_KEYS     RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_KEYBOARD, 0)

#define JOYSTICK_DEAD_ZONE 0.15f
#define FRAMESKIP_MAX 4
#define DISK_DRIVE_CDROM 0
#define DISK_DRIVE_FLOPPY_1 1
#define DISK_DRIVE_FLOPPY_2 2
#define DISK_DRIVE_COUNT (1 + FDC_DRIVES)
#define RTC_FIXED_DATE 631152000
#define PLAYLIST_MAX_SIZE (1024 * 1024)
#define CONTENT_MAX_SIZE (k_floppy_max_image_size * 4)
#define INPUT_STATE_SIZE (sizeof(float) * 2)

static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static struct retro_log_callback logging;
retro_log_printf_t log_cb;
static unsigned message_interface_version = 0;
static bool libretro_supports_bitmasks = false;
static bool libretro_supports_dupe = false;
static bool libretro_supports_keyboard = false;
static bool categories_supported = false;
static struct retro_midi_interface midi_interface;
static bool midi_supported = false;
static bool midi_output = false;
static bool midi_notified = false;
static u8 midi_restores = 0;
static u64 midi_frame_base = 0;
static u64 midi_last_sample = 0;
static bool midi_builtin = true;
static int midi_mt32_model = GDOX_MT32_MODEL_AUTO;
static MidiSynthHost midi_host;
static bool frameskip_threshold_visible = true;

static char retro_save_directory[4096];
static char retro_system_directory[4096];
static char retro_game_path[4096];
static bool content_loaded = false;

static GeardoxCore* core;
static GDOX_Runtime_Info runtime_info;
static u8* frame_buffer;
static s16 audio_buf[GDOX_AUDIO_BUFFER_SIZE];
static int audio_sample_count = 0;
static size_t savestate_size = 0;
static GDOX_Machine_Settings machine_settings;
#if !defined(GDOX_DISABLE_DYNAREC)
static int dynarec_option = -1;
#endif

static int current_screen_width = 0;
static int current_screen_height = 0;
static float current_aspect_ratio = 0.0f;
static float current_fps = 60.0f;
static float aspect_ratio = 0.0f;
static int frameskip_type = 0;
static unsigned frameskip_threshold = 33;
static unsigned frameskip_counter = 0;
static bool frameskip_changed = false;
static bool audio_buffer_active = false;
static unsigned audio_buffer_occupancy = 0;
static bool audio_buffer_underrun = false;

static int mouse_sensitivity = 7;
static GDOX_Joystick_Type joystick_type = GDOX_JOYSTICK_GAMEPAD;
static int cga_output = GDOX_CGA_OUTPUT_AUTO;
static unsigned input_devices[GDOX_MAX_GAMEPADS] = { RETRO_DEVICE_JOYPAD, RETRO_DEVICE_JOYPAD };
static int gamepad_mouse[GDOX_MAX_GAMEPADS] = { GDOX_GAMEPAD_MOUSE_LEFT_STICK, GDOX_GAMEPAD_MOUSE_LEFT_STICK };
static int gamepad_mouse_buttons[GDOX_MAX_GAMEPADS] = { 0, 0 };
static int gamepad_mouse_speed = k_gamepad_mouse_speed_default;
static float mouse_remainder_x = 0.0f;
static float mouse_remainder_y = 0.0f;
static bool retro_key_down[RETROK_LAST];
static int pointer_last_x = 0;
static int pointer_last_y = 0;
static std::string key_map_path;
static bool key_mapper_was_open = false;
static bool key_mapping_was_enabled = true;

enum Import_Step
{
    IMPORT_CURRENT = 0,
    IMPORT_DONE,
    IMPORT_FAILED,
    IMPORT_SKIPPED
};

typedef Import_Step (*Import_Function)(const char* source, const char* path, std::string* text);

struct Import_Source
{
    const char* folder;
    const char* file;
    bool folders;
    const char* name;
    const char* contents;
    const char* gone;
    Import_Function run;
};

struct DiskSetImage
{
    std::string path;
    std::string label;
    std::string label_suffix;
    int image;
    int disk;
    u32 crc;
    bool crc_known;
    bool cdrom;
    bool save_disk;
};

static std::vector<DiskSetImage> disk_images;
static int drive_entry[DISK_DRIVE_COUNT] = { -1, -1, -1 };
static unsigned disk_index = 0;
static bool disk_ejected = true;
static int open_drive = -1;
static int open_entry = -1;
static unsigned disk_initial_index = 0;
static std::string disk_initial_path;
static std::string floppy_working_path[FDC_DRIVES];
static IdleFlush floppy_idle_flush[FDC_DRIVES];
static IdleFlush hard_disk_idle_flush;
static bool hard_disk_state_warned = false;
static std::string hard_disk_working_path[GDOX_MAX_HARD_DISKS];
static bool hard_disk_image_attached = false;
static int dos_disk = 0;
static std::string msdos_zip;
static std::string ultrasnd_zip;
static std::string glide_ovl;
static int floppy_drive = 0;
static bool floppy_multidrive = false;
static bool save_disk = false;
static GDOX_Boot_Device boot_device = GDOX_BOOT_DEVICE_AUTO;
static GameDisk game_disk;
static bool game_active = false;
static bool game_descriptors = true;
static int game_pick = 0;
static bool game_write_folder = false;
static bool game_freedos_only = false;
static char game_keyboard_layout[8] = "us";
static int game_target = GDOX_GAME_TARGET_GAME;
static int game_program = -1;
static bool game_options_hidden = false;
static bool cmos_shared = true;
static bool cmos_shared_active = false;
static std::string cmos_path;
static IdleFlush cmos_idle_flush;

static bool load_content(const char* path);
static void unload_content(void);
static void set_machine_config(void);
static void report_machine(void);
static void show_message(const char* message, unsigned priority, enum retro_log_level level);
static void get_system_av_info(struct retro_system_av_info* info);
static void audio_buffer_status(bool active, unsigned occupancy, bool underrun_likely);
static void update_frameskip(void);
static void midi_begin_frame(int av_enable);
static void midi_end_frame(int av_enable, int frame_samples);
static void midi_write(MidiOut* midi);
static void midi_create_folders(const std::string& mt32, const std::string& soundfonts);
static void midi_scan(void);
static int midi_get_kind(void);
static bool midi_is_builtin(void);
static int midi_get_engine(void);
static bool midi_activate(int engine);
static int midi_get_mt32_index(void);
static void set_controller_info(void);
static void set_input_descriptors(void);
static int add_input_descriptors(struct retro_input_descriptor* descriptors, int count, unsigned port, const struct retro_input_descriptor* source, int source_count);
static void clear_input_state(void);
static void clear_keyboard_state(void);
static void save_input_state(u8* data);
static void load_input_state(const u8* data);
static void poll_input(void);
static void apply_input(void);
static void apply_joystick(void);
static void apply_gamepad_keys(void);
static void apply_dead_zone(int x, int y, s16& out_x, s16& out_y);
static void apply_mouse(void);
static void apply_key_mapper_pointer(void);
static u16 usage_from_retro_key(unsigned keycode);
static void key_map_load(void);
static void key_map_save(void);
static void key_map_update(void);
static void keyboard_event(bool down, unsigned keycode, uint32_t character, uint16_t key_modifiers);
static bool update_core_options_display(void);
static void check_variables(void);
static void update_floppy_drive(void);
static bool disk_load(const char* path);
static void game_message(const GameDisk_Message& message);
static bool game_open(const char* path, bool* game);
static void game_end(void);
static bool game_rebuild(void);
static std::string get_temp_path(void);
static void log_write_back(void);
static bool disk_add_images(const char* path, std::vector<DiskSetImage>& images);
static bool disk_add_cdrom(const char* path, std::vector<DiskSetImage>& images);
static bool disk_add_playlist(const char* path, std::vector<DiskSetImage>& images);
static bool disk_add_floppy_images(const char* path, std::vector<DiskSetImage>& images);
static bool disk_add_save_disk(const std::string& name, const char* label, const char* volume, std::vector<DiskSetImage>& images);
static unsigned disk_find_boot_floppy(void);
static int get_entry_drive(unsigned index);
static bool disk_insert(unsigned index, int drive);
static bool disk_eject(int drive);
static void disk_close_open_drive(int drive);
static void disk_sync(void);
static void disk_clear(void);
static void disk_shift_entry(int& entry, unsigned index, int delta);
static bool mount_state_media(const GDOX_SaveState_Media& media);
static bool mount_entries(const int* entries);
static int find_floppy_entry(u32 crc);
static int find_cdrom_entry(u32 crc);
static bool get_cdrom_crc(const char* path, u32* crc);
static void register_disk_control(void);
static bool disk_set_eject_state(bool ejected);
static bool disk_get_eject_state(void);
static unsigned disk_get_image_index(void);
static bool disk_set_image_index(unsigned index);
static unsigned disk_get_num_images(void);
static bool disk_replace_image_index(unsigned index, const struct retro_game_info* info);
static bool disk_add_image_index(void);
static bool disk_set_initial_image(unsigned index, const char* path);
static bool disk_get_image_path(unsigned index, char* path, size_t len);
static bool disk_get_image_label(unsigned index, char* label, size_t len);
static bool floppy_load_image(unsigned index, int floppy);
static bool floppy_flush(int floppy);
static void floppy_update(void);
static bool hard_disk_attach(const char* path);
static bool hard_disk_attach_source(const char* path);
static void hard_disk_attach_builtin(void);
static bool hard_disk_make_builtin(const std::string& path, int system);
static bool hard_disk_is_cdrom_slot(int id);
static int hard_disk_get_builtin_slot(void);
static Import_Step import_msdos(const char* source, const char* zip, std::string* text);
static Import_Step import_ultrasnd(const char* source, const char* zip, std::string* text);
static Import_Step import_glide(const char* source, const char* ovl, std::string* text);
static std::string system_import(const Import_Source& import);
static void hard_disk_update(void);
static void cmos_load(void);
static void cmos_save(void);
static void cmos_update(void);

static const Import_Source k_import_msdos = { "msdos", k_dos_import_file, false, "MS-DOS", "MS-DOS disks",
    "games run on FreeDOS", import_msdos };
static const Import_Source k_import_ultrasnd = { "ultrasnd", k_gus_import_file, true, "UltraSound software",
    "UltraSound software", "the card has no patches", import_ultrasnd };
static const Import_Source k_import_glide = { "3dfx", k_glide_import_file, true, "3dfx Voodoo driver",
    "3dfx Voodoo driver", "its import was removed", import_glide };

static void fallback_log(enum retro_log_level level, const char *fmt, ...)
{
    (void)level;
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);
}

static const unsigned k_retropad_inputs[GDOX_GAMEPAD_INPUT_LEFT_STICK_UP] =
{
    RETRO_DEVICE_ID_JOYPAD_B, RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_Y, RETRO_DEVICE_ID_JOYPAD_X,
    RETRO_DEVICE_ID_JOYPAD_UP, RETRO_DEVICE_ID_JOYPAD_DOWN, RETRO_DEVICE_ID_JOYPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_RIGHT,
    RETRO_DEVICE_ID_JOYPAD_L, RETRO_DEVICE_ID_JOYPAD_R, RETRO_DEVICE_ID_JOYPAD_L2, RETRO_DEVICE_ID_JOYPAD_R2,
    RETRO_DEVICE_ID_JOYPAD_L3, RETRO_DEVICE_ID_JOYPAD_R3, RETRO_DEVICE_ID_JOYPAD_SELECT, RETRO_DEVICE_ID_JOYPAD_START
};

static const char* const k_gamepad_mouse_button_ids[] = { "b_a", "y_x", "l_r", "l2_r2", "off" };

static const int k_gamepad_mouse_button_inputs[][2] =
{
    { GDOX_GAMEPAD_INPUT_A, GDOX_GAMEPAD_INPUT_B },
    { GDOX_GAMEPAD_INPUT_X, GDOX_GAMEPAD_INPUT_Y },
    { GDOX_GAMEPAD_INPUT_L1, GDOX_GAMEPAD_INPUT_R1 },
    { GDOX_GAMEPAD_INPUT_L2, GDOX_GAMEPAD_INPUT_R2 }
};

static const int k_gamepad_mouse_button_pairs =
    (int)(sizeof(k_gamepad_mouse_button_inputs) / sizeof(k_gamepad_mouse_button_inputs[0]));

static bool IsJoystickDevice(unsigned port)
{
    return (port == 0) && (input_devices[0] == RETRO_DEVICE_PC_JOYSTICK);
}

static bool IsGamepadKeysDevice(unsigned port)
{
    return (port < GDOX_MAX_GAMEPADS) &&
        ((input_devices[port] == RETRO_DEVICE_JOYPAD) || (input_devices[port] == RETRO_DEVICE_GAMEPAD_KEYS));
}

static std::string get_file_name(const char* path, bool strip_extension)
{
    std::string name = path_filename(trim_trailing_separators(path));
    return strip_extension ? path_strip_extension(name) : name;
}

unsigned retro_api_version(void)
{
    return RETRO_API_VERSION;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
    (void)cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
    audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
    input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
    input_state_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
    video_cb = cb;
}

void retro_set_environment(retro_environment_t cb)
{
    environ_cb = cb;

    bool support_no_game = true;
    environ_cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &support_no_game);

    register_disk_control();
    set_controller_info();
    libretro_set_core_options(environ_cb, &categories_supported);

    frameskip_threshold_visible = true;
    struct retro_core_options_update_display_callback display_callback = { update_core_options_display };
    environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK, &display_callback);
    update_core_options_display();
}

void retro_init(void)
{
    struct retro_vfs_interface_info vfs_interface_info = { };
    vfs_interface_info.required_interface_version = 1;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &vfs_interface_info) && vfs_interface_info.iface)
        MediaFile::SetVfsInterface(vfs_interface_info.iface, vfs_interface_info.required_interface_version);
    else
        MediaFile::SetVfsInterface(NULL);

    if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
        log_cb = logging.log;
    else
        log_cb = fallback_log;

    const char *dir = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &dir) && dir)
        snprintf(retro_save_directory, sizeof(retro_save_directory), "%s", dir);
    else
        snprintf(retro_save_directory, sizeof(retro_save_directory), "%s", ".");

    dir = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) && dir)
        snprintf(retro_system_directory, sizeof(retro_system_directory), "%s", dir);
    else
        retro_system_directory[0] = 0;

    log_cb(RETRO_LOG_INFO, "%s (%s) libretro\n", GDOX_TITLE, GDOX_VERSION);

    core = new GeardoxCore();
    core->Init();
    core->EnableHostHotkeys(true);
    core->LockRAMSize(true);
    core->GetRuntimeInfo(runtime_info);

    frame_buffer = new u8[GDOX_MAX_FRAME_BUFFER_WIDTH * GDOX_MAX_FRAME_BUFFER_HEIGHT * sizeof(u32)];

    clear_input_state();

    libretro_supports_bitmasks = environ_cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL);

    if (!environ_cb(RETRO_ENVIRONMENT_GET_CAN_DUPE, &libretro_supports_dupe))
        libretro_supports_dupe = false;

    if (!environ_cb(RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION, &message_interface_version))
        message_interface_version = 0;

    uint64_t quirks = RETRO_SERIALIZATION_QUIRK_ENDIAN_DEPENDENT;
    environ_cb(RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS, &quirks);

    memset(&midi_interface, 0, sizeof(midi_interface));
    midi_supported = environ_cb(RETRO_ENVIRONMENT_GET_MIDI_INTERFACE, &midi_interface) &&
        IsValidPointer(midi_interface.output_enabled) && IsValidPointer(midi_interface.write);
    midi_output = false;
}

void retro_deinit(void)
{
    if (content_loaded)
        unload_content();

    SafeDeleteArray(frame_buffer);
    SafeDelete(core);
    MediaFile::SetVfsInterface(NULL);

    message_interface_version = 0;
    libretro_supports_bitmasks = false;
    libretro_supports_dupe = false;
    libretro_supports_keyboard = false;
    audio_sample_count = 0;
    default_machine_settings(&machine_settings);
#if !defined(GDOX_DISABLE_DYNAREC)
    dynarec_option = -1;
#endif
    current_screen_width = 0;
    current_screen_height = 0;
    current_aspect_ratio = 0.0f;
    current_fps = 60.0f;
    aspect_ratio = 0.0f;
    frameskip_type = 0;
    frameskip_threshold = 33;
    frameskip_counter = 0;
    frameskip_changed = false;
    audio_buffer_active = false;
    mouse_sensitivity = 7;
    joystick_type = GDOX_JOYSTICK_GAMEPAD;
    cga_output = GDOX_CGA_OUTPUT_AUTO;
    disk_initial_index = 0;
    disk_initial_path.clear();
    floppy_drive = 0;
    floppy_multidrive = false;
    save_disk = false;
    cmos_shared = true;
    cmos_shared_active = false;
    cmos_path.clear();

    for (int i = 0; i < GDOX_MAX_GAMEPADS; i++)
    {
        input_devices[i] = RETRO_DEVICE_JOYPAD;
        gamepad_mouse[i] = GDOX_GAMEPAD_MOUSE_LEFT_STICK;
        gamepad_mouse_buttons[i] = 0;
    }

    gamepad_mouse_speed = k_gamepad_mouse_speed_default;
    clear_input_state();
}

void retro_reset(void)
{
    log_cb(RETRO_LOG_DEBUG, "Resetting...\n");

    check_variables();
    cmos_save();

    if (game_active)
        core->SetGameTarget(game_target);
    else
        core->SetBootDevice(boot_device);

    if (game_active && game_rebuild())
        core->PowerOn();
    else
        core->ResetMedia();

    report_machine();
    clear_input_state();
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
    if (port >= GDOX_MAX_GAMEPADS)
    {
        if (log_cb)
            log_cb(RETRO_LOG_DEBUG, "retro_set_controller_port_device invalid port number: %u\n", port);

        return;
    }

    input_devices[port] = device;

    if (log_cb)
        log_cb(RETRO_LOG_INFO, "Controller %u: %s\n", port, IsJoystickDevice(port) ? "PC Joystick" :
            IsGamepadKeysDevice(port) ? "Keyboard and Mouse" : "Unplugged");

    set_input_descriptors();
}

void retro_get_system_info(struct retro_system_info *info)
{
    memset(info, 0, sizeof(*info));
    info->library_name     = GDOX_TITLE;
    info->library_version  = GDOX_VERSION;
    info->need_fullpath    = true;
    info->block_extract    = true;
    info->valid_extensions = "gdox|exe|com|bat|cue|ins|chd|iso|gog|zip|img|ima|vfd|flp|dsk|d77|d88|hdm|xdf|mfm|bin|m3u|hdd|vhd|h0|h1|h2|h3|/";
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
    get_system_av_info(info);
    current_fps = (float)info->timing.fps;
}

void retro_run(void)
{
    bool core_options_updated = false;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &core_options_updated) && core_options_updated)
        check_variables();

    if (frameskip_changed)
        update_frameskip();

    poll_input();
    apply_input();

    bool render = true;
    int av_enable = 0;

    if (!environ_cb(RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE, &av_enable))
        av_enable = RETRO_AV_ENABLE_VIDEO | RETRO_AV_ENABLE_AUDIO;

    render = (av_enable & RETRO_AV_ENABLE_VIDEO) != 0;

    if (render && frameskip_type > 0 && audio_buffer_active)
    {
        bool skip = frameskip_type == 1 ? audio_buffer_underrun : audio_buffer_occupancy < frameskip_threshold;

        if (skip && frameskip_counter < FRAMESKIP_MAX)
        {
            render = false;
            frameskip_counter++;
        }
        else
            frameskip_counter = 0;
    }

    audio_sample_count = 0;
    bool powered = core->IsPoweredOn();
    midi_begin_frame(av_enable);
    core->RunToFrame(frame_buffer, audio_buf, &audio_sample_count, render);
    midi_end_frame(av_enable, audio_sample_count / 2);
    key_map_update();
    floppy_update();
    hard_disk_update();
    cmos_update();

    if (core->IsPowerOffRequested())
    {
        core->AcknowledgePowerOff();
        show_message("Powered off by software", 2, RETRO_LOG_INFO);
        log_cb(RETRO_LOG_INFO, "The guest powered the machine off\n");
        game_end();
        environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
    }
    else if (powered && !core->IsPoweredOn())
    {
        show_message("Powered off by software", 2, RETRO_LOG_INFO);
        log_cb(RETRO_LOG_INFO, "Powered off by software\n");
        environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
    }

    retro_system_av_info info;
    get_system_av_info(&info);
    bool fps_changed = fabsf((float)info.timing.fps - current_fps) > 0.1f;
    bool geometry_changed = (runtime_info.screen_width != current_screen_width) ||
                            (runtime_info.screen_height != current_screen_height) ||
                            (info.geometry.aspect_ratio != current_aspect_ratio);

    if (fps_changed || geometry_changed)
    {
        current_screen_width = runtime_info.screen_width;
        current_screen_height = runtime_info.screen_height;
        current_aspect_ratio = info.geometry.aspect_ratio;
        current_fps = (float)info.timing.fps;

        if (fps_changed)
        {
            log_cb(RETRO_LOG_INFO, "Refresh rate changed to %.2f Hz\n", current_fps);
            environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &info);

            if (frameskip_type > 0)
                frameskip_changed = true;
        }
        else
        {
            environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &info.geometry);
        }
    }

    video_cb((render || !libretro_supports_dupe) ? frame_buffer : NULL, runtime_info.screen_width,
        runtime_info.screen_height, runtime_info.screen_width * sizeof(u32));

    if (core->GetKeyMapper()->IsOpen())
    {
        int frames = MIN((int)(GDOX_AUDIO_SAMPLE_RATE / current_fps), GDOX_AUDIO_BUFFER_SIZE / 2);
        memset(audio_buf, 0, (size_t)frames * 2 * sizeof(s16));
        audio_sample_count = frames * 2;
    }

    if (audio_sample_count > 0)
        audio_batch_cb(audio_buf, audio_sample_count / 2);
}

bool retro_load_game(const struct retro_game_info *info)
{
    return load_content((info && info->path) ? info->path : "");
}

void retro_unload_game(void)
{
    unload_content();
}

unsigned retro_get_region(void)
{
    return RETRO_REGION_NTSC;
}

bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info)
{
    UNUSED(game_type);
    UNUSED(info);
    UNUSED(num_info);
    return false;
}

size_t retro_serialize_size(void)
{
    return savestate_size > 0 ? savestate_size + INPUT_STATE_SIZE : 0;
}

bool retro_serialize(void *data, size_t size)
{
    u8* buffer = reinterpret_cast<u8*>(data);
    size_t core_size = savestate_size;

    if ((savestate_size == 0) || (size < savestate_size + INPUT_STATE_SIZE) || !core->SaveState(buffer, core_size) ||
        (core_size != savestate_size))
        return false;

    save_input_state(buffer + savestate_size);
    return true;
}

bool retro_unserialize(const void *data, size_t size)
{
    const u8* buffer = reinterpret_cast<const u8*>(data);

    if ((savestate_size == 0) || (size < savestate_size + INPUT_STATE_SIZE))
        return false;

    GDOX_SaveState_Media media = {};
    bool loaded = core->LoadState(buffer, savestate_size, &media);

    if (!loaded && media.valid)
    {
        int entries[DISK_DRIVE_COUNT];
        bool ejected = disk_ejected;
        int drive = open_drive;
        int entry = open_entry;
        memcpy(entries, drive_entry, sizeof(entries));
        loaded = mount_state_media(media) && core->LoadState(buffer, savestate_size);

        if (loaded)
            disk_sync();
        else
        {
            mount_entries(entries);
            disk_ejected = ejected;
            open_drive = drive;
            open_entry = entry;
        }
    }

    if (loaded)
    {
        load_input_state(buffer + savestate_size);
        core->GetKeyMap()->Resync();
        bool changed = core->GetHardDiskStateChanges() != 0;
        bool mismatch = IsValidPointer(core->GetFolderDisk(0)) && core->GetFolderDisk(0)->IsQuarantined();

        if (changed && !hard_disk_state_warned)
        {
            const char* message = mismatch ? "The hard disk changed since this state was saved, its changes go to its "
                "Conflicts folder until it closes" : "The hard disk changed since this state was saved";
            log_cb(RETRO_LOG_WARN, "%s\n", message);
            show_message(message, 3, RETRO_LOG_WARN);
        }

        hard_disk_state_warned = changed;
        int context = RETRO_SAVESTATE_CONTEXT_NORMAL;
        int av_enable = 0;

        bool fast_states = environ_cb(RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE, &av_enable) &&
            ((av_enable & RETRO_AV_ENABLE_FAST_SAVESTATES) != 0);

        if (!environ_cb(RETRO_ENVIRONMENT_GET_SAVESTATE_CONTEXT, &context) && fast_states)
            context = RETRO_SAVESTATE_CONTEXT_RUNAHEAD_SAME_INSTANCE;

        if (context == RETRO_SAVESTATE_CONTEXT_NORMAL)
        {
            midi_restores |= 1;

            if (midi_output || core->GetAudio()->GetMidiSynth()->IsActive())
                core->GetAudio()->GetMidiOut()->Sync();
        }
    }

    return loaded;
}

void *retro_get_memory_data(unsigned id)
{
    switch (id)
    {
        case RETRO_MEMORY_SAVE_RAM:
            return cmos_shared_active ? NULL : core->GetRTC()->GetCMOS();
        case RETRO_MEMORY_SYSTEM_RAM:
            return core->GetMemory()->GetWorkingRAM();
        case RETRO_MEMORY_VIDEO_RAM:
            return core->GetMemory()->GetVideoRAM();
    }

    return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
    switch (id)
    {
        case RETRO_MEMORY_SAVE_RAM:
            return cmos_shared_active ? 0 : GDOX_CMOS_SIZE;
        case RETRO_MEMORY_SYSTEM_RAM:
            return core->GetMemory()->GetWorkingRAMSize();
        case RETRO_MEMORY_VIDEO_RAM:
            return core->GetMemory()->GetVideoRAMSize();
    }

    return 0;
}

void retro_cheat_reset(void)
{
}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
    (void)index;
    (void)enabled;
    (void)code;
}

static bool load_content(const char* path)
{
    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;

    if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
    {
        log_cb(RETRO_LOG_ERROR, "XRGB8888 is not supported.\n");
        return false;
    }

    check_variables();

    if (frameskip_type > 0)
        frameskip_changed = true;

    snprintf(retro_game_path, sizeof(retro_game_path), "%s", path);
    log_cb(RETRO_LOG_INFO, "Loading %s\n", path[0] != 0 ? path : "no content");
    MediaFile::RemoveTree(get_temp_path().c_str());
    MediaFile::MakeDirectories(get_temp_path().c_str());
    core->GetMedia()->SetTempPath(get_temp_path().c_str());

    set_machine_config();
    disk_clear();
    clear_input_state();
    msdos_zip = system_import(k_import_msdos);
    ultrasnd_zip = system_import(k_import_ultrasnd);
    glide_ovl = system_import(k_import_glide);
    midi_scan();
    bool game = false;

    if (!game_open(path, &game) || (!game && !disk_load(path)))
    {
        unload_content();
        return false;
    }

    hard_disk_attach_builtin();
    cmos_load();

    if (!game)
        core->SetBootDevice(boot_device);

    if (!core->PowerOn())
    {
        log_cb(RETRO_LOG_ERROR, "Unable to power on the machine\n");
        unload_content();
        return false;
    }

    report_machine();
    key_map_load();
    content_loaded = true;
    midi_notified = false;
    midi_restores = 0;

    if (!core->GetMaxSaveStateSize(savestate_size))
        savestate_size = 0;

    log_cb(RETRO_LOG_INFO, "Save state size: %zu bytes\n", savestate_size);
    disk_sync();

    struct retro_memory_descriptor descriptors[8] = {};
    unsigned count = 0;
    u8* ram = core->GetMemory()->GetWorkingRAM();
    u32 ram_size = core->GetMemory()->GetWorkingRAMSize();

    for (u32 start = 0; (start < ram_size) && (count < sizeof(descriptors) / sizeof(descriptors[0])); count++)
    {
        u32 length = 1;

        while (length * 2 <= ram_size - start)
            length *= 2;

        descriptors[count].flags = RETRO_MEMDESC_SYSTEM_RAM;
        descriptors[count].ptr = ram + start;
        descriptors[count].start = start;
        descriptors[count].len = length;
        start += length;
    }

    struct retro_memory_map memory_map = { descriptors, count };
    environ_cb(RETRO_ENVIRONMENT_SET_MEMORY_MAPS, &memory_map);

    bool achievements = true;
    environ_cb(RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS, &achievements);

    core->GetRuntimeInfo(runtime_info);

    struct retro_keyboard_callback keyboard = { keyboard_event };
    libretro_supports_keyboard = environ_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &keyboard);
    show_message("Turn on Game Focus to use the keyboard and mouse (Scroll Lock by default)", 3, RETRO_LOG_INFO);

    return true;
}

static void unload_content(void)
{
    if (midi_output)
    {
        MidiOut* midi = core->GetAudio()->GetMidiOut();
        midi->Silence();
        midi_write(midi);
    }

    for (int i = 0; i < FDC_DRIVES; i++)
    {
        if (!floppy_flush(i))
            log_cb(RETRO_LOG_ERROR, "Unable to save the floppy %d changes\n", i + 1);

        core->EjectFloppy(i);
    }

    if (!core->FlushHardDisks())
        log_cb(RETRO_LOG_ERROR, "Unable to save the hard disk changes\n");

    cmos_save();
    cmos_shared_active = false;
    core->GetKeyMapper()->Close();
    key_map_save();
    key_map_path.clear();
    core->EjectMedia();
    core->PowerOff();

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        core->DetachHardDisk(i);
        hard_disk_working_path[i].clear();
    }

    hard_disk_idle_flush.Reset();
    hard_disk_state_warned = false;
    hard_disk_image_attached = false;
    game_end();
    disk_clear();
    clear_input_state();
    MediaFile::RemoveTree(get_temp_path().c_str());

    content_loaded = false;
    retro_game_path[0] = 0;
    savestate_size = 0;
    current_fps = 60.0f;
    memset(frame_buffer, 0, GDOX_MAX_FRAME_BUFFER_WIDTH * GDOX_MAX_FRAME_BUFFER_HEIGHT * sizeof(u32));
}

static void set_machine_config(void)
{
    core->SetMachineSettings(machine_settings);

    if (floppy_drive == 1 && core->GetPendingMachineConfig().floppy_drives < 2)
        log_cb(RETRO_LOG_WARN, "The machine has one floppy drive, disk control uses Floppy 1\n");
}

static void report_machine(void)
{
    const GDOX_Machine_Config& config = core->GetMachineConfig();
    const GDOX_Machine_Report& report = core->GetMachineReport();
    char cpu[96];
    get_machine_cpu_text(config, cpu, sizeof(cpu));

    log_cb(RETRO_LOG_INFO, "Machine: %s, %s, %u MB RAM, %d floppy drives%s%s\n", k_machine_profiles[config.model].name,
        cpu, config.ram_size / (1024 * 1024), config.floppy_drives, IsValidPointer(report.title) ? ", game: " : "",
        IsValidPointer(report.title) ? report.title : "");

    for (int i = 0; i < report.warning_count; i++)
        log_cb(RETRO_LOG_WARN, "%s\n", report.warnings[i]);

    if (!IsValidPointer(report.title) && (report.warning_count == 0))
        return;

    static char message[1024];
    get_machine_headline(config, report, message, sizeof(message));

    for (int i = 0; i < report.warning_count; i++)
    {
        strncat_fit(message, ". ", sizeof(message));
        strncat_fit(message, report.warnings[i], sizeof(message));
    }

    show_message(message, 2, report.warning_count > 0 ? RETRO_LOG_WARN : RETRO_LOG_INFO);
}

// The frames RetroArch throws away for runahead have their audio off, and their MIDI goes with it. RetroArch rewinds
// with ordinary loads, so loads before two frames in a row are a rewind and play no notes
static void midi_begin_frame(int av_enable)
{
    MidiOut* midi = core->GetAudio()->GetMidiOut();
    MidiSynth* synth = core->GetAudio()->GetMidiSynth();
    bool wanted = midi_is_builtin();
    bool output = !wanted && midi_supported && midi_interface.output_enabled();
    bool fast = false;
    bool rewinding = midi_restores == 3;
    midi_restores = (u8)((midi_restores & 1) << 1);

    if (synth->IsOpen(GDOX_MIDI_ENGINE_MT32) && (midi_get_mt32_index() != midi_host.GetMT32Index()))
        midi_activate(GDOX_MIDI_ENGINE_MT32);

    int engine = wanted ? midi_get_engine() : GDOX_MIDI_ENGINE_NONE;
    synth->SetEngine(midi_activate(engine) ? engine : GDOX_MIDI_ENGINE_NONE);
    bool builtin = synth->IsActive();

    if (output != midi_output)
    {
        midi_output = output;
        midi->ForgetSynth();

        if (output)
            midi->Sync();
        else
            midi->Clear();
    }

    if (midi->TakeReset())
    {
        midi_notified = false;

        if (builtin)
            synth->Reset();
        else if (output)
            midi->Sync();
    }

    if (!environ_cb(RETRO_ENVIRONMENT_GET_FASTFORWARDING, &fast))
        fast = false;

    bool enabled = (output || builtin) && ((av_enable & RETRO_AV_ENABLE_AUDIO) != 0);
    MidiOut::MidiOut_Capture_Change change = midi->UpdateCapture(enabled, fast || rewinding, builtin);

    if (change == MidiOut::MIDI_OUT_CAPTURE_SILENCE)
        midi->Silence();
    else if (change == MidiOut::MIDI_OUT_CAPTURE_SYNC)
        midi->Sync();
}

static void midi_end_frame(int av_enable, int frame_samples)
{
    MidiOut* midi = core->GetAudio()->GetMidiOut();

    bool builtin = core->GetAudio()->GetMidiSynth()->IsActive();
    bool wanted = midi_is_builtin();

    if (midi->HasPlayed() && !midi_output && !builtin && !midi_notified && (wanted || midi_supported))
    {
        midi_notified = true;

        if (!wanted)
            show_message("This game plays MIDI music. Turn on MIDI output in Settings > Audio > MIDI", 4,
                RETRO_LOG_INFO);
        else if (midi_get_kind() == GDOX_MIDI_SYNTH_MT32)
            show_message("This game plays MT-32 music. Put MT-32 ROMs in <system>/geardox/mt32", 4, RETRO_LOG_INFO);
        else
            show_message("This game plays General MIDI music. Put a sound bank (.sf2) in <system>/geardox/soundfonts", 4,
                RETRO_LOG_INFO);
    }

    if (midi_output)
        midi_write(midi);
    else if (!builtin)
        midi->Clear();

    if ((av_enable & RETRO_AV_ENABLE_AUDIO) != 0)
        midi_frame_base += (u64)MAX(frame_samples, 0);
}

static void midi_create_folders(const std::string& mt32, const std::string& soundfonts)
{
    if (!MidiSynth::IsAvailable(GDOX_MIDI_ENGINE_MT32) && !MidiSynth::IsAvailable(GDOX_MIDI_ENGINE_SOUNDFONT))
        return;

    std::string parent(retro_system_directory);
    append_path_component(parent, "geardox");
    MediaFile::MakeDirectory(parent.c_str());

    if (MidiSynth::IsAvailable(GDOX_MIDI_ENGINE_MT32))
        MediaFile::MakeDirectory(mt32.c_str());

    if (MidiSynth::IsAvailable(GDOX_MIDI_ENGINE_SOUNDFONT))
        MediaFile::MakeDirectory(soundfonts.c_str());
}

static void midi_scan(void)
{
    MidiSynth* synth = core->GetAudio()->GetMidiSynth();
    std::string mt32(retro_system_directory);
    std::string soundfonts(retro_system_directory);
    append_path_component(mt32, "geardox");
    append_path_component(mt32, "mt32");
    append_path_component(soundfonts, "geardox");
    append_path_component(soundfonts, "soundfonts");
    std::vector<std::string> mt32_folders;
    std::vector<std::string> soundfont_folders;
    std::string previous = midi_host.GetSoundFont();

    if (retro_system_directory[0] != 0)
    {
        midi_create_folders(mt32, soundfonts);
        mt32_folders.push_back(mt32);
        mt32_folders.push_back(retro_system_directory);
        soundfont_folders.push_back(soundfonts);
    }

    midi_host.ScanMT32(mt32_folders);
    midi_host.FindSoundFont(soundfont_folders);

    for (int i = 0; i < midi_host.GetMT32Count(); i++)
        log_cb(RETRO_LOG_INFO, "MT-32 ROMs: %s\n", midi_host.GetMT32(i).name);

    if (synth->IsOpen(GDOX_MIDI_ENGINE_MT32))
        synth->CloseMT32();

    if (midi_host.GetSoundFont() != previous)
        synth->CloseSoundFont();
}

static int midi_get_kind(void)
{
    int kind = game_active ? game_disk.GetDescriptor().GetInfo().midi : -1;

    if (kind < 0)
        kind = core->GetAudio()->GetMidiOut()->GetDetected();

    return kind >= 0 ? kind : GDOX_MIDI_SYNTH_GM;
}

static bool midi_is_builtin(void)
{
    int out = game_active ? game_disk.GetDescriptor().GetInfo().midi_out : -1;

    if ((out < 0) && game_active && !game_disk.GetDescriptor().GetInfo().midi_port.empty())
        out = GDOX_MIDI_OUT_EXTERNAL;

    return out >= 0 ? out == GDOX_MIDI_OUT_BUILTIN : midi_builtin;
}

static int midi_get_engine(void)
{
    int engine = midi_get_kind() == GDOX_MIDI_SYNTH_MT32 ? GDOX_MIDI_ENGINE_MT32 : GDOX_MIDI_ENGINE_SOUNDFONT;
    bool mt32 = midi_host.IsAvailable(GDOX_MIDI_ENGINE_MT32);
    bool soundfont = midi_host.IsAvailable(GDOX_MIDI_ENGINE_SOUNDFONT);

    if ((engine == GDOX_MIDI_ENGINE_MT32) && !mt32)
        engine = soundfont ? GDOX_MIDI_ENGINE_SOUNDFONT : GDOX_MIDI_ENGINE_NONE;
    else if ((engine == GDOX_MIDI_ENGINE_SOUNDFONT) && !soundfont)
        engine = mt32 ? GDOX_MIDI_ENGINE_MT32 : GDOX_MIDI_ENGINE_NONE;

    return engine;
}

static int midi_get_mt32_index(void)
{
    int model = game_active ? game_disk.GetDescriptor().GetInfo().mt32_model : -1;

    if (model < 0)
        model = midi_mt32_model;

    return midi_host.FindMT32(model);
}

static bool midi_activate(int engine)
{
    MidiSynth* synth = core->GetAudio()->GetMidiSynth();

    if (engine == GDOX_MIDI_ENGINE_MT32)
    {
        int index = midi_get_mt32_index();

        if (synth->IsOpen(engine) && (index == midi_host.GetMT32Index()))
            return true;

        MidiSynthHost::MidiSynthHost_Open result = midi_host.OpenMT32(synth, index);

        if (result == MidiSynthHost::MIDI_SYNTH_HOST_OPENED)
            log_cb(RETRO_LOG_INFO, "Built-in MT-32: %s\n", midi_host.GetMT32(index).name);
        else if (result == MidiSynthHost::MIDI_SYNTH_HOST_FAILED)
            log_cb(RETRO_LOG_ERROR, "Unable to open the %s ROMs\n", midi_host.GetMT32(index).name);

        return result == MidiSynthHost::MIDI_SYNTH_HOST_OPENED;
    }

    if (engine == GDOX_MIDI_ENGINE_SOUNDFONT)
    {
        if (synth->IsOpen(engine))
            return true;

        MidiSynthHost::MidiSynthHost_Open result = midi_host.OpenSoundFont(synth);
        const char* path = midi_host.GetSoundFont().c_str();

        if (result == MidiSynthHost::MIDI_SYNTH_HOST_OPENED)
            log_cb(RETRO_LOG_INFO, "Built-in SoundFont: %s\n", path);
        else if (result == MidiSynthHost::MIDI_SYNTH_HOST_FAILED)
            log_cb(RETRO_LOG_ERROR, "Unable to load the SoundFont %s\n", path);

        return result == MidiSynthHost::MIDI_SYNTH_HOST_OPENED;
    }

    return false;
}

// Each message's first byte carries the time since the previous one, from where it fell in the frame's audio
static void midi_write(MidiOut* midi)
{
    int count = midi->GetMessageCount();
    const MidiOut::MidiOut_Message* messages = midi->GetMessages();
    const u8* data = midi->GetData();

    for (int i = 0; i < count; i++)
    {
        const MidiOut::MidiOut_Message& message = messages[i];
        u64 sample = message.offset == k_midi_out_immediate ? midi_frame_base : midi_frame_base + message.offset;
        u32 delta = 0;

        if (sample > midi_last_sample)
        {
            delta = (u32)MIN(((sample - midi_last_sample) * 1000000ULL) / GDOX_AUDIO_SAMPLE_RATE, (u64)0xFFFFFFFF);
            midi_last_sample = sample;
        }

        for (int j = 0; j < message.size; j++)
            midi_interface.write(data[message.data + j], j == 0 ? delta : 0);
    }

    if (count > 0)
    {
        if (IsValidPointer(midi_interface.flush))
            midi_interface.flush();

        midi->Clear();
    }
}

static void show_message(const char* message, unsigned priority, enum retro_log_level level)
{
    if (message_interface_version >= 1)
    {
        struct retro_message_ext msg = {};
        msg.msg = message;
        msg.duration = level == RETRO_LOG_INFO ? 4000 : 7000;
        msg.priority = priority;
        msg.level = level;
        msg.target = RETRO_MESSAGE_TARGET_OSD;
        msg.type = RETRO_MESSAGE_TYPE_NOTIFICATION;
        msg.progress = -1;
        environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &msg);
        return;
    }

    struct retro_message msg = {};
    msg.msg = message;
    msg.frames = 360;
    environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &msg);
}

static void get_system_av_info(struct retro_system_av_info* info)
{
    core->GetRuntimeInfo(runtime_info);

    info->geometry.base_width   = runtime_info.screen_width;
    info->geometry.base_height  = runtime_info.screen_height;
    info->geometry.max_width    = GDOX_MAX_FRAME_BUFFER_WIDTH;
    info->geometry.max_height   = GDOX_MAX_FRAME_BUFFER_HEIGHT;
    info->geometry.aspect_ratio = aspect_ratio == 0.0f ?
        (float)runtime_info.display_width / (float)runtime_info.display_height : aspect_ratio;
    info->timing.fps            = 1000.0f / runtime_info.frame_time;
    info->timing.sample_rate    = GDOX_AUDIO_SAMPLE_RATE;
}

static void audio_buffer_status(bool active, unsigned occupancy, bool underrun_likely)
{
    audio_buffer_active = active;
    audio_buffer_occupancy = occupancy;
    audio_buffer_underrun = underrun_likely;
}

static void update_frameskip(void)
{
    struct retro_audio_buffer_status_callback callback = { audio_buffer_status };
    unsigned latency = 0;
    frameskip_changed = false;
    frameskip_counter = 0;
    audio_buffer_active = false;

    if (frameskip_type > 0 && environ_cb(RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK, &callback))
    {
        latency = (unsigned)ceilf(6.0f * 1000.0f / current_fps);
        latency = ((latency + 31) / 32) * 32;
    }
    else
        environ_cb(RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK, NULL);

    environ_cb(RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY, &latency);
}

static void set_controller_info(void)
{
    static const struct retro_controller_description port_1[] = {
        { "Keyboard and Mouse", RETRO_DEVICE_GAMEPAD_KEYS },
        { "PC Joystick", RETRO_DEVICE_PC_JOYSTICK },
        { "None", RETRO_DEVICE_NONE }
    };

    static const struct retro_controller_description port_2[] = {
        { "Keyboard and Mouse", RETRO_DEVICE_GAMEPAD_KEYS },
        { "None", RETRO_DEVICE_NONE }
    };

    static const struct retro_controller_info ports[] = {
        { port_1, 3 },
        { port_2, 2 },
        { NULL, 0 }
    };

    environ_cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);

    set_input_descriptors();
}

static void set_input_descriptors(void)
{
    static const struct retro_input_descriptor k_joystick[] = {
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,     "Up" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,   "Down" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,   "Left" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,  "Right" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,      "Button 1" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,      "Button 2" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,      "Button 3" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,      "Button 4" },
        { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_X, "Stick X" },
        { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,  RETRO_DEVICE_ID_ANALOG_Y, "Stick Y" },
        { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Throttle (Flight Stick)" }
    };

    static const struct retro_input_descriptor k_mouse[] = {
        { 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT,     "Mouse Left" },
        { 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT,    "Mouse Right" },
        { 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE,   "Mouse Middle" }
    };

    static const char* const k_mouse_stick[GDOX_GAMEPAD_MOUSE_COUNT] =
        { NULL, "Mouse (Left Stick)", "Mouse (Right Stick)", NULL };
    struct retro_input_descriptor descriptors[32];
    int count = 0;

    if (IsJoystickDevice(0))
        count = add_input_descriptors(descriptors, count, 0, k_joystick, (int)(sizeof(k_joystick) / sizeof(k_joystick[0])));

    for (unsigned port = 0; port < GDOX_MAX_GAMEPADS; port++)
    {
        if (!IsGamepadKeysDevice(port))
            continue;

        int mouse = gamepad_mouse[port];

        if (IsValidPointer(k_mouse_stick[mouse]))
        {
            unsigned index = mouse == GDOX_GAMEPAD_MOUSE_LEFT_STICK ? RETRO_DEVICE_INDEX_ANALOG_LEFT :
                RETRO_DEVICE_INDEX_ANALOG_RIGHT;
            struct retro_input_descriptor stick[2] = {
                { port, RETRO_DEVICE_ANALOG, index, RETRO_DEVICE_ID_ANALOG_X, k_mouse_stick[mouse] },
                { port, RETRO_DEVICE_ANALOG, index, RETRO_DEVICE_ID_ANALOG_Y, k_mouse_stick[mouse] }
            };
            count = add_input_descriptors(descriptors, count, port, stick, 2);
        }

        if (gamepad_mouse_buttons[port] < k_gamepad_mouse_button_pairs)
        {
            const int* inputs = k_gamepad_mouse_button_inputs[gamepad_mouse_buttons[port]];
            struct retro_input_descriptor buttons[2] = {
                { port, RETRO_DEVICE_JOYPAD, 0, k_retropad_inputs[inputs[0]], "Mouse Left" },
                { port, RETRO_DEVICE_JOYPAD, 0, k_retropad_inputs[inputs[1]], "Mouse Right" }
            };
            count = add_input_descriptors(descriptors, count, port, buttons, 2);
        }
    }

    count = add_input_descriptors(descriptors, count, 0, k_mouse, (int)(sizeof(k_mouse) / sizeof(k_mouse[0])));

    memset(&descriptors[count], 0, sizeof(descriptors[count]));
    environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, descriptors);
}

static int add_input_descriptors(struct retro_input_descriptor* descriptors, int count, unsigned port,
    const struct retro_input_descriptor* source, int source_count)
{
    for (int i = 0; i < source_count; i++)
    {
        descriptors[count] = source[i];
        descriptors[count].port = port;
        count++;
    }

    return count;
}

static void clear_input_state(void)
{
    clear_keyboard_state();
    mouse_remainder_x = 0.0f;
    mouse_remainder_y = 0.0f;
}

static void clear_keyboard_state(void)
{
    memset(retro_key_down, 0, sizeof(retro_key_down));

    if (core)
    {
        core->ReleaseAllHostKeys();
        core->ReleaseAllKeys();
    }
}

static void save_input_state(u8* data)
{
    memcpy(data, &mouse_remainder_x, sizeof(float));
    memcpy(data + sizeof(float), &mouse_remainder_y, sizeof(float));
}

static void load_input_state(const u8* data)
{
    memcpy(&mouse_remainder_x, data, sizeof(float));
    memcpy(&mouse_remainder_y, data + sizeof(float), sizeof(float));

    if (!(fabsf(mouse_remainder_x) < 1.0f))
        mouse_remainder_x = 0.0f;

    if (!(fabsf(mouse_remainder_y) < 1.0f))
        mouse_remainder_y = 0.0f;
}

static void poll_input(void)
{
    input_poll_cb();

    for (unsigned i = 0; i < RETROK_LAST; i++)
    {
        if (!retro_key_down[i] && libretro_supports_keyboard)
            continue;

        bool down = input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, i) != 0;

        if (down != retro_key_down[i])
            keyboard_event(down, i, 0, 0);
    }
}

static void apply_input(void)
{
    apply_joystick();
    apply_gamepad_keys();
    apply_mouse();
}

static void apply_joystick(void)
{
    GDOX_Joystick_State state = { };

    if (IsJoystickDevice(0))
    {
        int bits = 0;

        if (libretro_supports_bitmasks)
            bits = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
        else
        {
            for (int i = 0; i <= RETRO_DEVICE_ID_JOYPAD_R; i++)
                bits |= input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, i) ? (1 << i) : 0;
        }

        int stick_x = input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
        int stick_y = input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
        apply_dead_zone(stick_x, stick_y, state.axes[GDOX_JOYSTICK_AXIS_AX], state.axes[GDOX_JOYSTICK_AXIS_AY]);

        bool up = (bits & (1 << RETRO_DEVICE_ID_JOYPAD_UP)) != 0;
        bool down = (bits & (1 << RETRO_DEVICE_ID_JOYPAD_DOWN)) != 0;
        bool left = (bits & (1 << RETRO_DEVICE_ID_JOYPAD_LEFT)) != 0;
        bool right = (bits & (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT)) != 0;
        bool flightstick = core->GetInput()->GetJoystickType() == GDOX_JOYSTICK_FLIGHTSTICK;

        if (flightstick)
        {
            int throttle_y = input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);
            s16 unused;
            apply_dead_zone(0, throttle_y, unused, state.axes[GDOX_JOYSTICK_AXIS_BY]);
        }

        Input::ApplyDPad(state, up, down, left, right, flightstick);

        if (bits & (1 << RETRO_DEVICE_ID_JOYPAD_B))
            state.buttons |= GDOX_JOYSTICK_BUTTON_1;

        if (bits & (1 << RETRO_DEVICE_ID_JOYPAD_A))
            state.buttons |= GDOX_JOYSTICK_BUTTON_2;

        if (bits & (1 << RETRO_DEVICE_ID_JOYPAD_Y))
            state.buttons |= GDOX_JOYSTICK_BUTTON_3;

        if (bits & (1 << RETRO_DEVICE_ID_JOYPAD_X))
            state.buttons |= GDOX_JOYSTICK_BUTTON_4;
    }

    core->GetInput()->SetJoystickState(state);
}

static void apply_gamepad_keys(void)
{
    for (unsigned port = 0; port < GDOX_MAX_GAMEPADS; port++)
    {
        GDOX_Gamepad_State state = { };
        GDOX_Gamepad_Mapping mapping;
        bool keys = IsGamepadKeysDevice(port) && !core->GetKeyMapper()->IsOpen();
        mapping.mouse = keys ? gamepad_mouse[port] : (int)GDOX_GAMEPAD_MOUSE_OFF;
        mapping.mouse_speed = gamepad_mouse_speed;
        mapping.dead_zone = (int)(JOYSTICK_DEAD_ZONE * 100.0f);

        for (int i = 0; i < GDOX_GAMEPAD_INPUT_COUNT; i++)
            mapping.targets[i] = GDOX_GAMEPAD_TARGET_NONE;

        if (keys && (gamepad_mouse_buttons[port] < k_gamepad_mouse_button_pairs))
        {
            const int* inputs = k_gamepad_mouse_button_inputs[gamepad_mouse_buttons[port]];
            mapping.targets[inputs[0]] = GDOX_GAMEPAD_TARGET_MOUSE_LEFT;
            mapping.targets[inputs[1]] = GDOX_GAMEPAD_TARGET_MOUSE_RIGHT;
        }

        if (keys)
        {
            int bits = 0;

            if (libretro_supports_bitmasks)
                bits = input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
            else
            {
                for (int i = 0; i <= RETRO_DEVICE_ID_JOYPAD_R3; i++)
                    bits |= input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, i) ? (1 << i) : 0;
            }

            for (int i = 0; i < GDOX_GAMEPAD_INPUT_LEFT_STICK_UP; i++)
            {
                if (bits & (1 << k_retropad_inputs[i]))
                    state.buttons |= 1u << i;
            }

            for (int a = 0; a < GDOX_GAMEPAD_AXIS_COUNT; a++)
            {
                unsigned index = a < 2 ? RETRO_DEVICE_INDEX_ANALOG_LEFT : RETRO_DEVICE_INDEX_ANALOG_RIGHT;
                unsigned id = (a & 1) == 0 ? RETRO_DEVICE_ID_ANALOG_X : RETRO_DEVICE_ID_ANALOG_Y;
                state.axes[a] = input_state_cb(port, RETRO_DEVICE_ANALOG, index, id);
            }
        }

        core->GetGamepadKeys()->Update((int)port, state, mapping);
    }
}

static void apply_dead_zone(int x, int y, s16& out_x, s16& out_y)
{
    float fx = (float)x / 32768.0f;
    float fy = (float)y / 32768.0f;
    float magnitude = sqrtf((fx * fx) + (fy * fy));

    out_x = 0;
    out_y = 0;

    if (magnitude <= JOYSTICK_DEAD_ZONE)
        return;

    float scale = (MIN(magnitude, 1.0f) - JOYSTICK_DEAD_ZONE) / (1.0f - JOYSTICK_DEAD_ZONE) / magnitude;
    out_x = (s16)CLAMP((int)(fx * scale * 32768.0f), -32768, 32767);
    out_y = (s16)CLAMP((int)(fy * scale * 32768.0f), -32768, 32767);
}

static void apply_mouse(void)
{
    if (core->GetKeyMapper()->IsOpen())
    {
        apply_key_mapper_pointer();
        return;
    }

    float scale = powf(2.0f, (float)(mouse_sensitivity - 7) / 4.0f);
    mouse_remainder_x += (float)input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X) * scale;
    mouse_remainder_y += (float)input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y) * scale;

    int delta_x = (int)mouse_remainder_x;
    int delta_y = (int)mouse_remainder_y;
    mouse_remainder_x -= (float)delta_x;
    mouse_remainder_y -= (float)delta_y;
    core->GetInput()->SetMouseDelta(delta_x, delta_y);

    u8 buttons = 0;

    if (input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT))
        buttons |= GDOX_MOUSE_LEFT;

    if (input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT))
        buttons |= GDOX_MOUSE_RIGHT;

    if (input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE))
        buttons |= GDOX_MOUSE_MIDDLE;

    core->GetInput()->SetMouseButtons(buttons);
}

static void apply_key_mapper_pointer(void)
{
    KeyMapper* mapper = core->GetKeyMapper();
    int x = input_state_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X);
    int y = input_state_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y);
    int dx = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
    int dy = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);

    if ((x != pointer_last_x) || (y != pointer_last_y))
    {
        pointer_last_x = x;
        pointer_last_y = y;
        mapper->SetPointer(((x + 0x7FFF) * mapper->GetWidth()) / 0xFFFE, ((y + 0x7FFF) * mapper->GetHeight()) / 0xFFFE);
    }
    else if ((dx != 0) || (dy != 0))
        mapper->MovePointer(dx, dy);

    u8 buttons = 0;

    if (input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT) ||
        input_state_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED))
        buttons |= KEY_MAPPER_BUTTON_LEFT;

    if (input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT))
        buttons |= KEY_MAPPER_BUTTON_RIGHT;

    mapper->SetPointerButtons(buttons);
}

static void key_map_load(void)
{
    std::string error;

    key_map_path = retro_save_directory;
    append_path_component(key_map_path, (safe_file_name(get_file_name(retro_game_path, true), "geardox") + ".keys").c_str());
    key_mapper_was_open = false;
    key_mapping_was_enabled = true;
    core->GetKeyMapper()->SetTitle(game_active ? game_disk.GetName() : get_file_name(retro_game_path, false).c_str());

    const char* game_keys = game_active ? game_disk.GetDescriptor().GetInfo().keys.c_str() : NULL;

    if (!core->GetKeyMap()->LoadFiles(key_map_path.c_str(), game_keys, &error))
    {
        log_cb(RETRO_LOG_ERROR, "Ignoring the key map %s: %s\n", key_map_path.c_str(), error.c_str());
        show_message(("Key map ignored: " + error).c_str(), 4, RETRO_LOG_ERROR);
    }
}

static void key_map_save(void)
{
    if (!core->GetKeyMap()->SaveUserFile(key_map_path.c_str()))
    {
        log_cb(RETRO_LOG_ERROR, "Unable to save the key map %s\n", key_map_path.c_str());
        show_message("Unable to save the key map", 3, RETRO_LOG_ERROR);
    }
}

static void key_map_update(void)
{
    bool open = core->GetKeyMapper()->IsOpen();
    bool enabled = core->GetKeyMap()->IsEnabled();

    if (open && !key_mapper_was_open && midi_output)
    {
        MidiOut* midi = core->GetAudio()->GetMidiOut();
        midi->Silence();
        midi_write(midi);
    }

    if (!open && key_mapper_was_open)
        key_map_save();

    if (enabled != key_mapping_was_enabled)
        show_message(enabled ? "Key mapping on" : "Key mapping off", 1, RETRO_LOG_INFO);

    key_mapper_was_open = open;
    key_mapping_was_enabled = enabled;
}

static u16 usage_from_retro_key(unsigned keycode)
{
    switch (keycode)
    {
        case RETROK_a: return 0x04;
        case RETROK_b: return 0x05;
        case RETROK_c: return 0x06;
        case RETROK_d: return 0x07;
        case RETROK_e: return 0x08;
        case RETROK_f: return 0x09;
        case RETROK_g: return 0x0A;
        case RETROK_h: return 0x0B;
        case RETROK_i: return 0x0C;
        case RETROK_j: return 0x0D;
        case RETROK_k: return 0x0E;
        case RETROK_l: return 0x0F;
        case RETROK_m: return 0x10;
        case RETROK_n: return 0x11;
        case RETROK_o: return 0x12;
        case RETROK_p: return 0x13;
        case RETROK_q: return 0x14;
        case RETROK_r: return 0x15;
        case RETROK_s: return 0x16;
        case RETROK_t: return 0x17;
        case RETROK_u: return 0x18;
        case RETROK_v: return 0x19;
        case RETROK_w: return 0x1A;
        case RETROK_x: return 0x1B;
        case RETROK_y: return 0x1C;
        case RETROK_z: return 0x1D;
        case RETROK_1: return 0x1E;
        case RETROK_2: return 0x1F;
        case RETROK_3: return 0x20;
        case RETROK_4: return 0x21;
        case RETROK_5: return 0x22;
        case RETROK_6: return 0x23;
        case RETROK_7: return 0x24;
        case RETROK_8: return 0x25;
        case RETROK_9: return 0x26;
        case RETROK_0: return 0x27;
        case RETROK_RETURN: return 0x28;
        case RETROK_ESCAPE: return 0x29;
        case RETROK_BACKSPACE: return 0x2A;
        case RETROK_TAB: return 0x2B;
        case RETROK_SPACE: return 0x2C;
        case RETROK_MINUS: return 0x2D;
        case RETROK_EQUALS: return 0x2E;
        case RETROK_LEFTBRACKET: return 0x2F;
        case RETROK_RIGHTBRACKET: return 0x30;
        case RETROK_BACKSLASH: return 0x31;
        case RETROK_SEMICOLON: return 0x33;
        case RETROK_QUOTE: return 0x34;
        case RETROK_BACKQUOTE: return 0x35;
        case RETROK_COMMA: return 0x36;
        case RETROK_PERIOD: return 0x37;
        case RETROK_SLASH: return 0x38;
        case RETROK_OEM_102: return 0x64;
        case RETROK_CAPSLOCK: return 0x39;
        case RETROK_F1: return 0x3A;
        case RETROK_F2: return 0x3B;
        case RETROK_F3: return 0x3C;
        case RETROK_F4: return 0x3D;
        case RETROK_F5: return 0x3E;
        case RETROK_F6: return 0x3F;
        case RETROK_F7: return 0x40;
        case RETROK_F8: return 0x41;
        case RETROK_F9: return 0x42;
        case RETROK_F10: return 0x43;
        case RETROK_F11: return 0x44;
        case RETROK_F12: return 0x45;
        case RETROK_PRINT: return 0x46;
        case RETROK_SYSREQ: return 0x9A;
        case RETROK_SCROLLOCK: return 0x47;
        case RETROK_PAUSE: return 0x48;
        case RETROK_BREAK: return 0x48;
        case RETROK_INSERT: return 0x49;
        case RETROK_HOME: return 0x4A;
        case RETROK_PAGEUP: return 0x4B;
        case RETROK_DELETE: return 0x4C;
        case RETROK_END: return 0x4D;
        case RETROK_PAGEDOWN: return 0x4E;
        case RETROK_RIGHT: return 0x4F;
        case RETROK_LEFT: return 0x50;
        case RETROK_DOWN: return 0x51;
        case RETROK_UP: return 0x52;
        case RETROK_NUMLOCK: return 0x53;
        case RETROK_KP_DIVIDE: return 0x54;
        case RETROK_KP_MULTIPLY: return 0x55;
        case RETROK_KP_MINUS: return 0x56;
        case RETROK_KP_PLUS: return 0x57;
        case RETROK_KP_ENTER: return 0x58;
        case RETROK_KP1: return 0x59;
        case RETROK_KP2: return 0x5A;
        case RETROK_KP3: return 0x5B;
        case RETROK_KP4: return 0x5C;
        case RETROK_KP5: return 0x5D;
        case RETROK_KP6: return 0x5E;
        case RETROK_KP7: return 0x5F;
        case RETROK_KP8: return 0x60;
        case RETROK_KP9: return 0x61;
        case RETROK_KP0: return 0x62;
        case RETROK_KP_PERIOD: return 0x63;
        case RETROK_LCTRL: return 0xE0;
        case RETROK_RCTRL: return 0xE4;
        case RETROK_LSHIFT: return 0xE1;
        case RETROK_RSHIFT: return 0xE5;
        case RETROK_LALT: return 0xE2;
        case RETROK_RALT: return 0xE6;
        case RETROK_LSUPER: return 0xE3;
        case RETROK_RSUPER: return 0xE7;
        case RETROK_LMETA: return 0xE3;
        case RETROK_RMETA: return 0xE7;
        case RETROK_MENU: return 0x65;
        default: return 0;
    }
}

static void keyboard_event(bool down, unsigned keycode, uint32_t character, uint16_t key_modifiers)
{
    UNUSED(character);
    UNUSED(key_modifiers);

    if (!core || keycode >= RETROK_LAST)
        return;

    u16 usage = usage_from_retro_key(keycode);

    if ((usage == 0) || (retro_key_down[keycode] == down))
        return;

    retro_key_down[keycode] = down;

    if (down)
        core->HostKeyPressed(usage);
    else
        core->HostKeyReleased(usage);
}

// A descriptor beats the core options it sets, so those are hidden while its game runs
static bool update_game_options_display(void)
{
    static const char* const k_options[][3] = {
        { "geardox_model", "machine", "model" }, { "geardox_cpu_clock", "machine", "cpu_mhz" },
        { "geardox_turbo", "machine", "turbo" }, { "geardox_main_ram", "machine", "ram_mb" },
        { "geardox_fpu", "machine", "fpu" }, { "geardox_floppy_a", "machine", "floppy_a_type" },
        { "geardox_floppy_b", "machine", "floppy_b_type" }, { "geardox_video_card", "machine", "video" },
        { "geardox_cga_output", "machine", "cga_output" }, { "geardox_voodoo", "machine", "voodoo" },
        { "geardox_sound_card", "sound", "card" }, { "geardox_sound_irq", "sound", "irq" },
        { "geardox_sound_dma", "sound", "dma" }, { "geardox_mpu", "sound", "mpu" },
        { "geardox_gus", "sound", "gus" },
        { "geardox_mouse", "input", "mouse" }, { "geardox_joystick", "input", "joystick" },
        { "geardox_cdrom_drive", "drives", "cdrom" },
        { "geardox_cdrom_speed", "machine", "cdrom_speed" }, { "geardox_drive_timing", "machine", "drive_timing" } };
    bool hide = game_active && game_descriptors;

    if (!hide && !game_options_hidden)
        return false;

    for (size_t i = 0; i < sizeof(k_options) / sizeof(k_options[0]); i++)
    {
        bool set = hide && game_disk.GetDescriptor().Has(k_options[i][1], k_options[i][2]);
        struct retro_core_option_display display = { k_options[i][0], !set };
        environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &display);
    }

    game_options_hidden = hide;
    return true;
}

static bool update_core_options_display(void)
{
    struct retro_variable var = { "geardox_frameskip", NULL };
    bool threshold_visible = environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && (strcmp(var.value, "Manual") == 0);
    bool changed = update_game_options_display();

    if (threshold_visible == frameskip_threshold_visible)
        return changed;

    struct retro_core_option_display display = { "geardox_frameskip_threshold", threshold_visible };
    environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &display);

    frameskip_threshold_visible = threshold_visible;
    return true;
}

static void check_variables(void)
{
    struct retro_variable var = { };

    var.key = "geardox_aspect_ratio";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        if (strcmp(var.value, "4:3 DAR") == 0)
            aspect_ratio = 4.0f / 3.0f;
        else if (strcmp(var.value, "16:9 DAR") == 0)
            aspect_ratio = 16.0f / 9.0f;
        else if (strcmp(var.value, "16:10 DAR") == 0)
            aspect_ratio = 16.0f / 10.0f;
        else
            aspect_ratio = 0.0f;
    }

    var.key = "geardox_native_resolution";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        core->GetVideo()->EnableNativeResolution(strcmp(var.value, "Enabled") == 0);
    }

    var.key = "geardox_cga_output";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        int found = find_cga_output(var.value);

        if (found >= 0)
            cga_output = found;

        if (!game_active || !game_disk.GetDescriptor().Has("machine", "cga_output"))
            core->GetVideo()->SetCGAOutput(cga_output);
    }

    var.key = "geardox_frameskip";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        int type = strcmp(var.value, "Auto") == 0 ? 1 : strcmp(var.value, "Manual") == 0 ? 2 : 0;

        if (type != frameskip_type)
        {
            frameskip_type = type;
            frameskip_changed = true;
        }
    }

    var.key = "geardox_frameskip_threshold";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        frameskip_threshold = (unsigned)CLAMP(atoi(var.value), 15, 60);
    }

    var.key = "geardox_mouse_sensitivity";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        mouse_sensitivity = CLAMP(atoi(var.value), 1, 15);
    }

    for (int i = 0; i < GDOX_MAX_GAMEPADS; i++)
    {
        char key[64];
        snprintf(key, sizeof(key), "geardox_pad%d_mouse", i + 1);
        var.key = key;
        var.value = NULL;

        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && (GamepadKeys::FindMouse(var.value) >= 0))
            gamepad_mouse[i] = GamepadKeys::FindMouse(var.value);

        snprintf(key, sizeof(key), "geardox_pad%d_mouse_buttons", i + 1);
        var.key = key;
        var.value = NULL;

        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        {
            for (int b = 0; b < (int)(sizeof(k_gamepad_mouse_button_ids) / sizeof(k_gamepad_mouse_button_ids[0])); b++)
            {
                if (strcmp(var.value, k_gamepad_mouse_button_ids[b]) == 0)
                    gamepad_mouse_buttons[i] = b;
            }
        }
    }

    var.key = "geardox_pad_mouse_speed";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        gamepad_mouse_speed = CLAMP(atoi(var.value), 1, 15);

    set_input_descriptors();

    var.key = "geardox_joystick";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        int found = find_joystick_type(var.value);

        if (found >= 0)
            joystick_type = (GDOX_Joystick_Type)found;
        else
            log_cb(RETRO_LOG_WARN, "Unknown joystick %s, keeping %s\n", var.value, k_joysticks[joystick_type].id);

        if (!game_active || !game_disk.GetDescriptor().Has("input", "joystick"))
            core->GetInput()->SetJoystickType(joystick_type);
    }

    var.key = "geardox_fm_volume";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        core->GetAudio()->SetFMVolume((float)CLAMP(atoi(var.value), 0, 200) / 100.0f);
    }

    var.key = "geardox_pcm_volume";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        core->GetAudio()->SetPCMVolume((float)CLAMP(atoi(var.value), 0, 200) / 100.0f);
    }

    var.key = "geardox_cdda_volume";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        core->GetAudio()->SetCDDAVolume((float)CLAMP(atoi(var.value), 0, 200) / 100.0f);
    }

    var.key = "geardox_buzzer_volume";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        core->GetAudio()->SetBuzzerVolume((float)CLAMP(atoi(var.value), 0, 200) / 100.0f);
    }

    var.key = "geardox_mt32_volume";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        core->GetAudio()->SetMT32Volume((float)CLAMP(atoi(var.value), 0, 200) / 100.0f);
    }

    var.key = "geardox_gm_volume";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        core->GetAudio()->SetGMVolume((float)CLAMP(atoi(var.value), 0, 200) / 100.0f);
    }

    var.key = "geardox_cdrom_preload";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        core->GetMedia()->PreloadCdRom(strcmp(var.value, "Enabled") == 0);
    }

    update_floppy_drive();

    var.key = "geardox_floppy_multidrive";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        floppy_multidrive = (strcmp(var.value, "Enabled") == 0);
    }

    var.key = "geardox_save_disk";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        save_disk = (strcmp(var.value, "Enabled") == 0);
    }

    var.key = "geardox_model";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        int found = find_machine_model(var.value);
        machine_settings.model = found >= 0 ? found : (int)k_machine_default_model;
    }

    var.key = "geardox_cpu_clock";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        machine_settings.cpu_mhz = strcmp(var.value, "auto") == 0 ? GDOX_MACHINE_AUTO : atoi(var.value);
    }

    var.key = "geardox_main_ram";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        machine_settings.ram_mb = atoi(var.value) > 0 ? atoi(var.value) : k_machine_default_ram_mb;
    }

    var.key = "geardox_fpu";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        machine_settings.fpu = strcmp(var.value, "none") == 0 ? 0 : 1;
    }

    static const char* const floppy_keys[GDOX_MAX_FLOPPY_DRIVES] = { "geardox_floppy_a", "geardox_floppy_b" };
    static const char* const floppy_types[GDOX_FLOPPY_TYPE_COUNT] = { "auto", "360k", "1200k", "720k", "1440k", "2880k" };

    for (int i = 0; i < GDOX_MAX_FLOPPY_DRIVES; i++)
    {
        var.key = floppy_keys[i];
        var.value = NULL;

        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        {
            machine_settings.floppy_type[i] = 0;

            for (int t = 1; t < GDOX_FLOPPY_TYPE_COUNT; t++)
            {
                if (strcmp(var.value, floppy_types[t]) == 0)
                    machine_settings.floppy_type[i] = t;
            }
        }
    }

    var.key = "geardox_sound_card";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        int found = find_sound_card(var.value);

        if (found >= 0)
            machine_settings.sound_card = found;
    }

    var.key = "geardox_video_card";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        int found = find_video_card(var.value);
        machine_settings.video_card = found >= 0 ? found : GDOX_MACHINE_AUTO;
    }

    var.key = "geardox_mouse";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        int found = find_mouse_type(var.value);

        if (found >= 0)
            machine_settings.mouse = found;
    }

    var.key = "geardox_cdrom_drive";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        int found = find_cdrom_position(var.value);

        if (found >= 0)
            machine_settings.cdrom = found;
    }

    var.key = "geardox_cdrom_speed";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        int found = find_cdrom_speed(var.value);

        if (found >= 0)
            machine_settings.cdrom_speed = found;
    }

    var.key = "geardox_drive_timing";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        int found = find_drive_timing(var.value);
        machine_settings.drive_timing = found >= 0 ? found : GDOX_MACHINE_AUTO;
    }

    var.key = "geardox_sound_irq";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        machine_settings.sound_irq = atoi(var.value);

    var.key = "geardox_sound_dma";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        machine_settings.sound_dma = atoi(var.value);

    var.key = "geardox_mpu";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        machine_settings.mpu = strcmp(var.value, "disabled") == 0 ? GDOX_MPU_OFF :
            strcmp(var.value, "uart") == 0 ? GDOX_MPU_UART : GDOX_MPU_INTELLIGENT;

    var.key = "geardox_gus";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        machine_settings.gus = strcmp(var.value, "enabled") == 0 ? 1 : 0;

    var.key = "geardox_voodoo";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        machine_settings.voodoo = strcmp(var.value, "enabled") == 0 ? 1 : 0;

    var.key = "geardox_midi";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        midi_builtin = strcmp(var.value, "retroarch") != 0;

    var.key = "geardox_mt32_model";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        for (int i = 0; i < GDOX_MT32_MODEL_COUNT; i++)
        {
            if (strcmp(var.value, k_mt32_models[i].id) == 0)
                midi_mt32_model = i;
        }
    }

    var.key = "geardox_turbo";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        bool turbo = strcmp(var.value, "Off") != 0;
        machine_settings.speed_mode = turbo ? 1 : 0;

        if ((core->IsTurbo() != turbo) && (!game_active || !game_disk.GetDescriptor().Has("machine", "turbo")))
            core->SetTurbo(turbo);
    }

    var.key = "geardox_descriptor";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        game_descriptors = strcmp(var.value, "disabled") != 0;

    var.key = "geardox_game_database";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        GameDisk::SetDatabaseEnabled(strcmp(var.value, "disabled") != 0);

    var.key = "geardox_game_pick";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        game_pick = MAX(atoi(var.value), 1) - 1;

    var.key = "geardox_game_target";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        game_target = strcmp(var.value, "setup") == 0 ? GDOX_GAME_TARGET_SETUP :
            strcmp(var.value, "prompt") == 0 ? GDOX_GAME_TARGET_PROMPT : GDOX_GAME_TARGET_GAME;
        game_program = strcmp(var.value, "voodoo") == 0 ? (int)GDOX_GAME_PROGRAM_VOODOO : -1;
    }

    var.key = "geardox_write_back";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        game_write_folder = strcmp(var.value, "game_folder") == 0;

    var.key = "geardox_game_dos";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        game_freedos_only = strcmp(var.value, "freedos") == 0;

    var.key = "geardox_keyboard_layout";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        snprintf(game_keyboard_layout, sizeof(game_keyboard_layout), "%s", var.value);

    var.key = "geardox_dos_disk";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        dos_disk = strcmp(var.value, "freedos") == 0 ? 1 : (strcmp(var.value, "disabled") == 0 ? 2 : 0);

    var.key = "geardox_boot_device";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        static const char* const devices[GDOX_BOOT_DEVICE_COUNT] = { "auto", "floppy1", "floppy2", "hdd", "cdrom" };
        boot_device = GDOX_BOOT_DEVICE_AUTO;

        for (int i = 0; i < GDOX_BOOT_DEVICE_COUNT; i++)
        {
            if (strcmp(var.value, devices[i]) == 0)
                boot_device = (GDOX_Boot_Device)i;
        }
    }

    var.key = "geardox_cmos";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        cmos_shared = (strcmp(var.value, "Shared") == 0);
    }

    var.key = "geardox_boot_menu";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        core->SetBootMenu(strcmp(var.value, "Enabled") == 0);
    }

    var.key = "geardox_rtc";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        core->GetRTC()->SetStartTime(strcmp(var.value, "Fixed date") == 0 ? RTC_FIXED_DATE : 0);
    }

#if defined(GDOX_ENABLE_VOODOO_THREAD)
    var.key = "geardox_voodoo_thread";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        core->GetVideo()->SetVoodooThreaded(strcmp(var.value, "enabled") == 0);
#endif

#if !defined(GDOX_DISABLE_DYNAREC)
    var.key = "geardox_dynarec";
    var.value = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        int dynarec = strcmp(var.value, "Enabled") == 0 ? 1 : 0;

        if (dynarec != dynarec_option)
        {
            dynarec_option = dynarec;
            core->GetI386()->EnableDynarec(dynarec == 1);

            if (dynarec == 1 && !core->GetI386()->IsDynarecEnabled())
                log_cb(RETRO_LOG_WARN, "Dynarec unavailable, the CPU runs in the interpreter, the Pentium models at a "
                    "fraction of their speed\n");
        }
    }
#endif
}

static void update_floppy_drive(void)
{
    struct retro_variable var = { "geardox_floppy_drive", NULL };

    if (!environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) || !var.value)
        return;

    int drive = strcmp(var.value, "Floppy 2") == 0 ? 1 : 0;

    if (drive != floppy_drive)
    {
        floppy_drive = drive;
        disk_sync();
    }
}

static bool disk_load(const char* path)
{
    if (path[0] != 0 && !disk_add_images(path, disk_images))
        return false;

    if (save_disk)
    {
        std::string name = safe_file_name(get_file_name(retro_game_path, true), "geardox") + ".save.d77";

        if (!disk_add_save_disk(name, "Save Disk", NULL, disk_images))
            return false;
    }

    unsigned boot = 0;

    if (disk_initial_index < disk_images.size() && disk_images[disk_initial_index].cdrom &&
        disk_images[disk_initial_index].path == disk_initial_path)
        boot = disk_initial_index;

    disk_initial_index = 0;
    disk_initial_path.clear();

    if (disk_images.empty() || disk_images[boot].save_disk)
        return true;

    if (!disk_images[boot].cdrom && ends_with_no_case(path, ".zip"))
        boot = disk_find_boot_floppy();

    if (!disk_insert(boot, disk_images[boot].cdrom ? DISK_DRIVE_CDROM : DISK_DRIVE_FLOPPY_1))
        return false;

    for (size_t i = 0; i < disk_images.size(); i++)
    {
        if (disk_images[i].cdrom != disk_images[boot].cdrom && !disk_images[i].save_disk)
        {
            if (!disk_insert((unsigned)i, disk_images[i].cdrom ? DISK_DRIVE_CDROM : DISK_DRIVE_FLOPPY_1))
                return false;

            break;
        }
    }

    int boot_floppy = drive_entry[DISK_DRIVE_FLOPPY_1];
    bool multidrive = floppy_multidrive && boot_floppy >= 0 && core->GetPendingMachineConfig().floppy_drives > 1;

    for (size_t k = 1; multidrive && k < disk_images.size(); k++)
    {
        size_t i = ((size_t)boot_floppy + k) % disk_images.size();

        if (!disk_images[i].cdrom && !disk_images[i].save_disk)
        {
            if (!disk_insert((unsigned)i, DISK_DRIVE_FLOPPY_2))
                log_cb(RETRO_LOG_WARN, "Floppy MultiDrive: %s is not inserted in Floppy 2\n", disk_images[i].path.c_str());

            break;
        }
    }

    disk_index = boot;
    disk_ejected = false;
    return true;
}

static bool disk_add_images(const char* path, std::vector<DiskSetImage>& images)
{
    GDOX_Content content = IsCdRomUri(path) ? GDOX_CONTENT_MEDIA : Content::Classify(path);

    if (content == GDOX_CONTENT_PLAYLIST)
        return disk_add_playlist(path, images);

    if (content == GDOX_CONTENT_HARD_DISK_SOURCE)
        return hard_disk_attach_source(path);

    if (content == GDOX_CONTENT_HARD_DISK)
        return hard_disk_attach(path);

    if (content == GDOX_CONTENT_FLOPPY)
    {
        if (disk_add_floppy_images(path, images))
            return true;

        log_cb(RETRO_LOG_ERROR, "Unable to read the floppy disk image %s\n", path);
        return false;
    }

    return disk_add_cdrom(path, images);
}

static bool disk_add_cdrom(const char* path, std::vector<DiskSetImage>& images)
{
    if (!IsCdRomUri(path) && !MediaFile::IsDirectory(path))
    {
        MediaFile* file = MediaFile::OpenFile(path);

        if (!IsValidPointer(file))
            return false;

        SafeDelete(file);
    }

    DiskSetImage image;
    image.path = path;
    image.label = get_file_name(path, false);
    image.image = 0;
    image.disk = 0;
    image.crc = 0;
    image.crc_known = false;
    image.cdrom = true;
    image.save_disk = false;
    images.push_back(image);
    return true;
}

static bool disk_add_playlist(const char* path, std::vector<DiskSetImage>& images)
{
    std::vector<u8> data;
    std::vector<Media::PlaylistEntry> entries;

    if (!MediaFile::ReadFile(path, PLAYLIST_MAX_SIZE, &data) ||
        !Media::ParsePlaylist(path, reinterpret_cast<const char*>(data.data()), data.size(), entries))
        return false;

    std::string save_name = safe_file_name(get_file_name(path, true), "geardox");
    int save_disks = 0;

    for (size_t i = 0; i < entries.size(); i++)
    {
        const Media::PlaylistEntry& entry = entries[i];

        if (entry.save_disk)
        {
            char suffix[32];
            char label[32];
            snprintf(suffix, sizeof(suffix), ".save%d.d77", save_disks);
            snprintf(label, sizeof(label), "Save Disk %d", save_disks);

            if (!disk_add_save_disk(save_name + suffix, label, entry.label.c_str(), images))
                return false;

            save_disks++;
            continue;
        }

        size_t first = images.size();

        if (ends_with_no_case(entry.path.c_str(), ".m3u") || !disk_add_images(entry.path.c_str(), images))
        {
            log_cb(RETRO_LOG_ERROR, "Playlist entry is missing or not a disk image: %s\n", entry.path.c_str());
            return false;
        }

        for (size_t j = first; entry.has_label && j < images.size(); j++)
            images[j].label = entry.label.empty() ? "" : entry.label + images[j].label_suffix;
    }

    return true;
}

static bool disk_add_floppy_images(const char* path, std::vector<DiskSetImage>& images)
{
    std::vector<FloppySet_Disk> disks;

    if (!FloppySet::ListDisks(path, disks))
        return false;

    for (size_t i = 0; i < disks.size(); i++)
    {
        const FloppySet_Disk& disk = disks[i];
        std::string file_name = get_file_name(disk.images > 1 ? disk.member.c_str() : path, false);
        std::string label = FloppySet::GetDiskLabel(disk);

        DiskSetImage image;
        image.path = path;
        image.label = file_name + label;
        image.label_suffix = (disk.images > 1 ? ": " + file_name : "") + label;
        image.image = disk.image;
        image.disk = disk.disk;
        image.crc = disk.crc;
        image.crc_known = true;
        image.cdrom = false;
        image.save_disk = false;
        images.push_back(image);
    }

    return true;
}

static bool disk_add_save_disk(const std::string& name, const char* label, const char* volume, std::vector<DiskSetImage>& images)
{
    std::string path(retro_save_directory);
    append_path_component(path, name.c_str());

    if (!MediaFile::Exists(path.c_str()))
    {
        u32 size = 0;
        u8* blank = FloppyImage::CreateBlank(FLOPPY_BLANK_1440K, true, &size);

        if (IsValidPointer(blank))
            FloppyImage::SetDiskName(blank, size, volume);

        bool created = IsValidPointer(blank) && MediaFile::WriteFileAtomic(path.c_str(), blank, size);
        SafeDeleteArray(blank);

        if (!created)
        {
            log_cb(RETRO_LOG_ERROR, "Unable to create the save disk %s\n", path.c_str());
            return false;
        }

        log_cb(RETRO_LOG_INFO, "Save disk created: %s\n", path.c_str());
    }

    DiskSetImage image;
    image.path = path;
    image.label = label;
    image.image = 0;
    image.disk = 0;
    image.crc = CalculateCRC32(0, name.c_str(), name.size());
    image.crc_known = true;
    image.cdrom = false;
    image.save_disk = true;
    images.push_back(image);
    return true;
}

static unsigned disk_find_boot_floppy(void)
{
    for (size_t i = 0; i < disk_images.size(); i++)
    {
        const DiskSetImage& image = disk_images[i];

        if (!image.cdrom && !image.save_disk && FloppySet::IsBootDisk(image.path.c_str(), image.image, image.disk))
            return (unsigned)i;
    }

    return 0;
}

static int get_entry_drive(unsigned index)
{
    if (index < disk_images.size() && disk_images[index].cdrom)
        return DISK_DRIVE_CDROM;

    return DISK_DRIVE_FLOPPY_1 + (core->GetPendingMachineConfig().floppy_drives > 1 ? floppy_drive : 0);
}

static bool disk_insert(unsigned index, int drive)
{
    if (index >= disk_images.size() || disk_images[index].path.empty())
        return false;

    DiskSetImage& image = disk_images[index];

    if (drive == DISK_DRIVE_CDROM)
    {
        bool loaded = core->LoadMedia(image.path.c_str());
        drive_entry[DISK_DRIVE_CDROM] = loaded ? (int)index : -1;

        if (loaded)
        {
            image.crc = core->GetMedia()->GetCRC();
            image.crc_known = true;
        }

        return loaded;
    }

    for (int i = DISK_DRIVE_FLOPPY_1; i < DISK_DRIVE_COUNT; i++)
    {
        if (i != drive && drive_entry[i] >= 0 && disk_images[drive_entry[i]].path == image.path &&
            disk_images[drive_entry[i]].image == image.image && disk_images[drive_entry[i]].disk == image.disk)
        {
            static char message[64];
            snprintf(message, sizeof(message), "This floppy disk is already in Floppy %d", i);
            show_message(message, 2, RETRO_LOG_WARN);
            return false;
        }
    }

    int floppy = drive - DISK_DRIVE_FLOPPY_1;

    if (!floppy_flush(floppy))
        return false;

    if (floppy_load_image(index, floppy))
        return true;

    if (!core->GetFloppy(floppy)->IsInserted())
    {
        drive_entry[drive] = -1;
        floppy_working_path[floppy].clear();
    }

    return false;
}

static bool disk_eject(int drive)
{
    if (drive == DISK_DRIVE_CDROM)
        core->EjectMedia();
    else
    {
        int floppy = drive - DISK_DRIVE_FLOPPY_1;

        if (!floppy_flush(floppy))
        {
            show_message("Unable to save the floppy disk changes, the disk stays in the drive", 3, RETRO_LOG_ERROR);
            return false;
        }

        core->EjectFloppy(floppy);
        floppy_working_path[floppy].clear();
    }

    drive_entry[drive] = -1;
    return true;
}

static void disk_close_open_drive(int drive)
{
    if (open_drive < 0 || open_drive == drive)
        return;

    if (open_entry >= 0 && drive_entry[open_drive] < 0)
        disk_insert((unsigned)open_entry, open_drive);

    open_drive = -1;
    open_entry = -1;
}

static void disk_sync(void)
{
    if (disk_ejected || disk_index >= disk_images.size())
        return;

    int drive = get_entry_drive(disk_index);

    if (drive_entry[drive] >= 0)
    {
        disk_index = (unsigned)drive_entry[drive];
        return;
    }

    disk_ejected = true;
    open_drive = drive;
    open_entry = -1;
}

static void disk_clear(void)
{
    disk_images.clear();
    disk_index = 0;
    disk_ejected = true;
    open_drive = -1;
    open_entry = -1;

    for (int i = 0; i < DISK_DRIVE_COUNT; i++)
        drive_entry[i] = -1;

    for (int i = 0; i < FDC_DRIVES; i++)
    {
        floppy_working_path[i].clear();
        floppy_idle_flush[i].Reset();
    }
}

static void disk_shift_entry(int& entry, unsigned index, int delta)
{
    if (entry < (int)index)
        return;

    entry = (delta < 0 && entry == (int)index) ? -1 : entry + delta;
}

static bool mount_state_media(const GDOX_SaveState_Media& media)
{
    int entries[DISK_DRIVE_COUNT];
    bool changed = false;

    for (int i = 0; i < DISK_DRIVE_COUNT; i++)
        entries[i] = drive_entry[i];

    if (!media.cdrom_inserted)
        entries[DISK_DRIVE_CDROM] = -1;
    else if (!core->GetMedia()->IsReady() || core->GetMedia()->GetCRC() != media.cdrom_crc)
    {
        entries[DISK_DRIVE_CDROM] = find_cdrom_entry(media.cdrom_crc);

        if (entries[DISK_DRIVE_CDROM] < 0)
            return false;
    }

    for (int i = 0; i < FDC_DRIVES; i++)
    {
        FloppyDisk* disk = core->GetFloppy(i);
        int drive = DISK_DRIVE_FLOPPY_1 + i;

        if (!media.floppy_inserted[i])
            entries[drive] = -1;
        else if (!disk->IsInserted() || disk->GetBaseCRC() != media.floppy_crc[i])
        {
            entries[drive] = find_floppy_entry(media.floppy_crc[i]);

            if (entries[drive] < 0)
                return false;
        }
    }

    for (int i = 0; i < DISK_DRIVE_COUNT; i++)
        changed = changed || entries[i] != drive_entry[i];

    if (!changed || !mount_entries(entries))
        return false;

    if (disk_ejected && disk_index < disk_images.size() && drive_entry[get_entry_drive(disk_index)] >= 0)
    {
        disk_ejected = false;
        open_drive = -1;
        open_entry = -1;
    }

    log_cb(RETRO_LOG_INFO, "Disks changed to match the save state\n");
    return true;
}

static bool mount_entries(const int* entries)
{
    for (int i = 0; i < DISK_DRIVE_COUNT; i++)
    {
        if (entries[i] != drive_entry[i] && drive_entry[i] >= 0 && (i != DISK_DRIVE_CDROM || entries[i] < 0) && !disk_eject(i))
            return false;
    }

    for (int i = 0; i < DISK_DRIVE_COUNT; i++)
    {
        if (entries[i] != drive_entry[i] && entries[i] >= 0 && !disk_insert((unsigned)entries[i], i))
            return false;
    }

    return true;
}

static int find_floppy_entry(u32 crc)
{
    for (size_t i = 0; i < disk_images.size(); i++)
    {
        if (!disk_images[i].cdrom && disk_images[i].crc_known && disk_images[i].crc == crc)
            return (int)i;
    }

    return -1;
}

static int find_cdrom_entry(u32 crc)
{
    for (size_t i = 0; i < disk_images.size(); i++)
    {
        DiskSetImage& image = disk_images[i];

        if (!image.cdrom)
            continue;

        if (!image.crc_known)
            image.crc_known = get_cdrom_crc(image.path.c_str(), &image.crc);

        if (image.crc_known && image.crc == crc)
            return (int)i;
    }

    return -1;
}

static bool get_cdrom_crc(const char* path, u32* crc)
{
    CdRomMedia media;
    media.Init();

    if (!media.LoadFromFile(path, false))
        return false;

    *crc = media.GetCRC();
    return true;
}

static void register_disk_control(void)
{
    struct retro_disk_control_ext_callback disk_control = {
        disk_set_eject_state,
        disk_get_eject_state,
        disk_get_image_index,
        disk_set_image_index,
        disk_get_num_images,
        disk_replace_image_index,
        disk_add_image_index,
        disk_set_initial_image,
        disk_get_image_path,
        disk_get_image_label
    };

    if (!environ_cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE, (void*)&disk_control))
    {
        struct retro_disk_control_callback legacy = {
            disk_set_eject_state,
            disk_get_eject_state,
            disk_get_image_index,
            disk_set_image_index,
            disk_get_num_images,
            disk_replace_image_index,
            disk_add_image_index
        };

        environ_cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE, (void*)&legacy);
    }
}

static bool disk_set_eject_state(bool ejected)
{
    update_floppy_drive();

    if (disk_ejected == ejected)
        return true;

    if (ejected)
    {
        if (disk_index < disk_images.size())
        {
            int drive = get_entry_drive(disk_index);
            int entry = drive_entry[drive];

            if (entry >= 0 && !disk_eject(drive))
                return false;

            open_drive = drive;
            open_entry = entry;
        }

        disk_ejected = true;
        return true;
    }

    if (disk_index < disk_images.size())
    {
        int drive = get_entry_drive(disk_index);
        disk_close_open_drive(drive);

        if (!disk_insert(disk_index, drive))
            return false;
    }

    open_drive = -1;
    open_entry = -1;
    disk_ejected = false;
    return true;
}

static bool disk_get_eject_state(void)
{
    update_floppy_drive();
    return disk_ejected;
}

static unsigned disk_get_image_index(void)
{
    update_floppy_drive();
    return disk_index;
}

static bool disk_set_image_index(unsigned index)
{
    update_floppy_drive();

    if (!disk_ejected || index > disk_images.size())
        return false;

    if (index < disk_images.size())
        disk_close_open_drive(get_entry_drive(index));

    disk_index = index;
    return true;
}

static unsigned disk_get_num_images(void)
{
    return (unsigned)disk_images.size();
}

static bool disk_replace_image_index(unsigned index, const struct retro_game_info* info)
{
    if (!disk_ejected || index >= disk_images.size())
        return false;

    for (int i = 0; i < DISK_DRIVE_COUNT; i++)
    {
        if (drive_entry[i] == (int)index)
            return false;
    }

    if (!info || !info->path)
    {
        disk_images.erase(disk_images.begin() + index);

        for (int i = 0; i < DISK_DRIVE_COUNT; i++)
            disk_shift_entry(drive_entry[i], index, -1);

        disk_shift_entry(open_entry, index, -1);

        if (disk_index > index || (disk_index >= disk_images.size() && disk_index > 0))
            disk_index--;

        return true;
    }

    std::vector<DiskSetImage> images;

    if (!disk_add_images(info->path, images) || images.empty())
        return false;

    if (open_entry == (int)index)
        open_entry = -1;

    int added = (int)images.size() - 1;
    disk_images[index] = images[0];
    disk_images.insert(disk_images.begin() + index + 1, images.begin() + 1, images.end());

    for (int i = 0; i < DISK_DRIVE_COUNT; i++)
        disk_shift_entry(drive_entry[i], index + 1, added);

    disk_shift_entry(open_entry, index + 1, added);

    if (disk_index > index)
        disk_index += added;

    return true;
}

static bool disk_add_image_index(void)
{
    if (!disk_ejected)
        return false;

    DiskSetImage image;
    image.image = 0;
    image.disk = 0;
    image.crc = 0;
    image.crc_known = false;
    image.cdrom = false;
    image.save_disk = false;
    disk_images.push_back(image);
    return true;
}

static bool disk_set_initial_image(unsigned index, const char* path)
{
    if (!path || !path[0])
        return false;

    disk_initial_index = index;
    disk_initial_path = path;
    return true;
}

static bool disk_get_image_path(unsigned index, char* path, size_t len)
{
    if (!path || len == 0 || index >= disk_images.size() || disk_images[index].path.empty())
        return false;

    snprintf(path, len, "%s", disk_images[index].path.c_str());
    return true;
}

static bool disk_get_image_label(unsigned index, char* label, size_t len)
{
    if (!label || len == 0 || index >= disk_images.size())
        return false;

    snprintf(label, len, "%s", disk_images[index].label.c_str());
    return true;
}

static bool floppy_load_image(unsigned index, int floppy)
{
    const DiskSetImage& image = disk_images[index];
    std::vector<u8> file;
    std::vector<u8> copy;
    std::string working(image.path);
    const u8* data = NULL;
    const u8* source = NULL;
    u32 size = 0;
    u32 source_size = 0;
    u32 data_crc = 0;

    if (image.save_disk)
    {
        if (!MediaFile::ReadFile(image.path.c_str(), CONTENT_MAX_SIZE, &file))
            return false;

        data = file.data();
        size = (u32)file.size();
    }
    else
    {
        u32 offset = 0;

        if (!FloppySet::ReadDisk(image.path.c_str(), image.image, image.disk, file, &offset, &size))
            return false;

        char suffix[64];
        bool earlier_member = false;

        for (size_t i = 0; !earlier_member && i < disk_images.size(); i++)
        {
            const DiskSetImage& other = disk_images[i];
            earlier_member = !other.cdrom && !other.save_disk && other.path == image.path &&
                other.image < image.image && other.crc == image.crc;
        }

        if (earlier_member)
            snprintf(suffix, sizeof(suffix), ".%08X.%d.%d.geardox.d77", image.crc, image.image + 1, image.disk + 1);
        else if (FloppyImage::HasEarlierCopy(file.data(), (u32)file.size(), image.disk))
            snprintf(suffix, sizeof(suffix), ".%08X.%d.geardox.d77", image.crc, image.disk + 1);
        else
            snprintf(suffix, sizeof(suffix), ".%08X.geardox.d77", image.crc);

        working = retro_save_directory;
        append_path_component(working, (get_file_name(image.path.c_str(), true) + suffix).c_str());
        data = file.data() + offset;
        source = data;
        source_size = size;

        if (FloppySet::ReadWorkingCopy(working.c_str(), copy))
        {
            log_cb(RETRO_LOG_INFO, "Loading floppy working copy: %s\n", working.c_str());
            data_crc = FloppyImage::CalculateDataCRC(data, size);
            data = copy.data();
            size = (u32)copy.size();
        }
    }

    if (!core->InsertFloppy(floppy, data, size, false, image.crc, data_crc, source, source_size))
        return false;

    floppy_working_path[floppy] = working;
    floppy_idle_flush[floppy].Reset();
    drive_entry[DISK_DRIVE_FLOPPY_1 + floppy] = (int)index;
    log_cb(RETRO_LOG_INFO, "Floppy %d: %s\n", floppy + 1, image.path.c_str());
    return true;
}

static bool floppy_flush(int floppy)
{
    FloppyDisk* disk = core->GetFloppy(floppy);

    if (!disk->IsInserted() || !disk->IsDirty())
        return true;

    if (floppy_working_path[floppy].empty() ||
        !MediaFile::WriteFileAtomic(floppy_working_path[floppy].c_str(), disk->GetImage(), disk->GetImageSize()))
        return false;

    disk->ClearDirty();
    log_cb(RETRO_LOG_INFO, "Floppy changes saved to %s\n", floppy_working_path[floppy].c_str());
    return true;
}

static void floppy_update(void)
{
    for (int i = 0; i < FDC_DRIVES; i++)
    {
        FloppyDisk* disk = core->GetFloppy(i);

        if (!disk->IsInserted() ||
            !floppy_idle_flush[i].Tick(disk->GetWriteCount(), disk->IsDirty(), k_idle_flush_floppy_frames))
            continue;

        if (floppy_idle_flush[i].Done(floppy_flush(i)))
        {
            log_cb(RETRO_LOG_ERROR, "Unable to save the floppy %d changes\n", i + 1);
            show_message("Unable to save the floppy disk changes", 3, RETRO_LOG_ERROR);
        }
    }
}

static bool hard_disk_attach(const char* path)
{
    if (content_loaded)
    {
        log_cb(RETRO_LOG_ERROR, "Hard disks are attached with the content and can't be swapped: %s\n", path);
        return false;
    }

    int id = HardDiskImage::GetImageId(path);

    for (int i = 0; (id < 0) && (i < GDOX_MAX_HARD_DISKS); i++)
    {
        if (!core->GetHardDisk(i)->IsOpen() && !hard_disk_is_cdrom_slot(i))
            id = i;
    }

    if (id < 0)
    {
        log_cb(RETRO_LOG_ERROR, "Every hard disk slot is in use: %s\n", path);
        return false;
    }

    if (core->GetHardDisk(id)->IsOpen())
    {
        log_cb(RETRO_LOG_ERROR, "Hard disk slot %d is in use: %s\n", id, path);
        return false;
    }

    std::string name = get_file_name(path, false);
    size_t dot = name.find_last_of('.');
    std::string working = retro_save_directory;
    std::string extension = ends_with_no_case(path, ".chd") ? ".hdd" : name.substr(dot);
    append_path_component(working, (safe_file_name(name.substr(0, dot), "geardox") + ".save" + extension).c_str());

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        const std::string& other = hard_disk_working_path[i];

        if (core->GetHardDisk(i)->IsOpen() && (other.length() == working.length()) &&
            ends_with_no_case(other.c_str(), working.c_str()))
        {
            log_cb(RETRO_LOG_ERROR, "The hard disk in slot %d already uses the working copy %s: %s\n", i, working.c_str(), path);
            return false;
        }
    }

    if (!core->AttachHardDisk(id, path, false, working.c_str()))
    {
        log_cb(RETRO_LOG_ERROR, "Unable to attach the hard disk image %s\n", path);
        return false;
    }

    hard_disk_working_path[id] = working;
    hard_disk_image_attached = true;

    log_cb(RETRO_LOG_INFO, "Hard disk in slot %d: %s\n", id, path);
    return true;
}

// A ZIP with no floppy images is a folder. Slot 0 waits for FreeDOS, so the files show up as D:
static bool hard_disk_attach_source(const char* path)
{
    if (content_loaded)
    {
        log_cb(RETRO_LOG_ERROR, "Hard disks are attached with the content and can't be swapped: %s\n", path);
        return false;
    }

    std::string error;
    FolderDisk* disk = FolderDisk::OpenSource(path, retro_save_directory, get_temp_path().c_str(), &error);

    if (!IsValidPointer(disk))
    {
        log_cb(RETRO_LOG_ERROR, "Unable to make a hard disk from %s: %s\n", path, error.c_str());
        show_message(error.c_str(), 3, RETRO_LOG_ERROR);
        return false;
    }

    if (disk->GetLeftOut() != 0)
        log_cb(RETRO_LOG_WARN, "%u items of %s are too deep for DOS to reach\n", disk->GetLeftOut(), path);

    int id = -1;

    bool builtin_waits = (dos_disk != 2) && !hard_disk_image_attached;

    for (int i = 0; (id < 0) && (i < GDOX_MAX_HARD_DISKS); i++)
    {
        if (!core->GetHardDisk(i)->IsOpen() && !hard_disk_is_cdrom_slot(i) &&
            !(builtin_waits && (i == hard_disk_get_builtin_slot())))
            id = i;
    }

    if ((id < 0) || !core->AttachHardDisk(id, disk))
    {
        log_cb(RETRO_LOG_ERROR, "Unable to attach the hard disk made from %s\n", path);
        return false;
    }

    log_cb(RETRO_LOG_INFO, "Hard disk in slot %d: %s\n", id, path);
    return true;
}

// The user's own Setup disks in <system>/geardox/msdos are imported into the save directory, and again when they
// change. With none there, msdos.zip goes and games and the built-in disk run on FreeDOS
static Import_Step import_msdos(const char* source, const char* zip, std::string* text)
{
    DosImport import;
    DosImport_System system;
    std::string error;
    u32 imported = 0;

    if (!import.AddSource(source))
    {
        *text = import.GetError();
        return IMPORT_SKIPPED;
    }

    if (DosImport::ReadSourceFingerprint(zip, &imported) && (imported == import.GetSourceFingerprint()) &&
        DosImport::Load(zip, &system, &error))
        return IMPORT_CURRENT;

    if (import.Import(zip) != DOS_IMPORT_DONE)
    {
        *text = import.GetError();
        return IMPORT_FAILED;
    }

    *text = std::string(import.GetName()) + " imported";
    return IMPORT_DONE;
}

static Import_Step import_ultrasnd(const char* source, const char* zip, std::string* text)
{
    GusImport import;
    GusImport_Software software;
    std::string error;
    u32 imported = 0;

    if (!import.AddSource(source))
    {
        *text = import.GetError();
        return IMPORT_SKIPPED;
    }

    if (GusImport::ReadSourceFingerprint(zip, &imported) && (imported == import.GetSourceFingerprint()) &&
        GusImport::Load(zip, &software, &error))
        return IMPORT_CURRENT;

    if (import.Import(zip) != GUS_IMPORT_DONE)
    {
        *text = import.GetError();
        return IMPORT_FAILED;
    }

    *text = import.GetMessage();
    return IMPORT_DONE;
}

static Import_Step import_glide(const char* source, const char* ovl, std::string* text)
{
    GlideImport import;

    if (import.Find(source) != GLIDE_IMPORT_DONE)
    {
        *text = import.GetError();
        return IMPORT_FAILED;
    }

    if (import.IsImported(ovl))
        return IMPORT_CURRENT;

    if (import.Write(ovl) != GLIDE_IMPORT_DONE)
    {
        *text = import.GetError();
        return IMPORT_FAILED;
    }

    *text = import.GetMessage();
    return IMPORT_DONE;
}

static std::string system_import(const Import_Source& import)
{
    std::string path(retro_save_directory);
    std::string source(retro_system_directory);
    std::vector<std::string> folders;
    std::vector<std::string> files;
    append_path_component(path, import.file);
    append_path_component(source, "geardox");
    append_path_component(source, import.folder);

    if ((retro_system_directory[0] == 0) || !MediaFile::ListDirectory(source.c_str(), folders, files) ||
        (files.empty() && (!import.folders || folders.empty())))
    {
        if (MediaFile::Exists(path.c_str()) && MediaFile::RemoveFile(path.c_str()))
            log_cb(RETRO_LOG_INFO, "No %s in %s any more, %s\n", import.contents, source.c_str(), import.gone);

        return "";
    }

    std::string text;
    Import_Step step = import.run(source.c_str(), path.c_str(), &text);
    static char message[512];

    if (step == IMPORT_CURRENT)
        return path;

    if (step == IMPORT_SKIPPED)
    {
        log_cb(RETRO_LOG_WARN, "%s in %s not imported: %s\n", import.name, source.c_str(), text.c_str());
        return "";
    }

    if (step == IMPORT_FAILED)
    {
        snprintf(message, sizeof(message), "%s not imported: %s", import.name, text.c_str());
        show_message(message, 3, RETRO_LOG_WARN);
        return "";
    }

    snprintf(message, sizeof(message), "%s from %s", text.c_str(), source.c_str());
    show_message(message, 2, RETRO_LOG_INFO);
    return path;
}

// A .gdox, or a folder or ZIP with one: the game on C:, Primary Master, as on the desktop. Images, manifests and
// overlays live in the save directory
static void game_message(const GameDisk_Message& message)
{
    static char text[1024];

    switch (message.kind)
    {
        case GAME_DISK_MESSAGE_RECOVERED:
            show_message(message.title.c_str(), 2, RETRO_LOG_WARN);
            break;
        case GAME_DISK_MESSAGE_RECOGNISED:
            snprintf(text, sizeof(text), "%s: %s", message.title.c_str(), message.detail.c_str());
            show_message(text, 1, RETRO_LOG_INFO);
            break;
        case GAME_DISK_MESSAGE_REFUSED:
            log_cb(RETRO_LOG_ERROR, "%s: %s\n", message.title.c_str(), message.detail.c_str());
            show_message(message.title.c_str(), 3, RETRO_LOG_ERROR);
            break;
        case GAME_DISK_MESSAGE_VOODOO:
            snprintf(text, sizeof(text), "%s: %s", message.title.c_str(), message.detail.c_str());
            show_message(text, 3, RETRO_LOG_WARN);
            break;
        case GAME_DISK_MESSAGE_VOODOO_DRIVER:
            snprintf(text, sizeof(text), "%s%s%s: put its ZIP or GLIDE2X.OVL in <system>/geardox/3dfx",
                message.title.c_str(), message.detail.empty() ? "" : ", ", message.detail.c_str());
            show_message(text, 3, RETRO_LOG_WARN);
            break;
        default:
            log_cb(RETRO_LOG_INFO, "%s: %s\n", message.title.c_str(), message.detail.c_str());
            break;
    }
}

static bool game_open(const char* path, bool* game)
{
    std::vector<GameDisk_Found> found;
    *game = false;

    if (!game_descriptors || (path[0] == 0) || !GameDisk::Find(path, &found))
        return true;

    *game = true;

    size_t pick = MIN((size_t)game_pick, found.size() - 1);

    if (found.size() > 1)
        log_cb(RETRO_LOG_INFO, "%u games, number %u by name runs: %s\n", (unsigned)found.size(), (unsigned)pick + 1,
            GameDisk::GetFoundName(found[pick]).c_str());

    static char message[1024];

    if (!game_disk.Open(found[pick], retro_save_directory, get_temp_path().c_str(), false, game_write_folder,
        game_freedos_only ? "" : msdos_zip.c_str(), ultrasnd_zip.c_str(), glide_ovl.c_str()))
    {
        snprintf(message, sizeof(message), "%s", game_disk.GetError());
        show_message(message, 3, RETRO_LOG_ERROR);
        return false;
    }

    if (game_freedos_only)
        game_disk.SetSystem(GDOX_GAME_SYSTEM_FREEDOS);

    game_disk.SetKeyboardLayout(game_keyboard_layout);
    game_disk.SetProgram(game_program);

    for (int i = 0; i < game_disk.GetDescriptor().GetWarningCount(); i++)
        log_cb(RETRO_LOG_WARN, "%s\n", game_disk.GetDescriptor().GetWarning(i));

    GDOX_Machine_Settings settings;
    game_disk.GetGameSettings(machine_settings, &settings);
    core->SetGameSettings(settings, game_disk.GetName());

    if (!game_disk.Prepare(core->GetPendingMachineConfig()))
    {
        snprintf(message, sizeof(message), "%s", game_disk.GetError());
        show_message(message, 3, RETRO_LOG_ERROR);
        game_disk.Close();
        core->ClearGameSettings();
        return false;
    }

    std::vector<GameDisk_Message> messages;
    game_disk.GetMessages(&messages);

    for (size_t i = 0; i < messages.size(); i++)
        game_message(messages[i]);

    bool attached_c = game_disk.IsImage() ? core->AttachHardDisk(0, game_disk.GetHardDisk(0), false,
        game_disk.GetWorkingPath().c_str()) : core->AttachHardDisk(0, game_disk.TakeDisk());

    if (!attached_c)
    {
        log_cb(RETRO_LOG_ERROR, "Unable to attach the game's disk %s\n", game_disk.GetSourcePath());
        game_disk.Close();
        core->ClearGameSettings();
        return false;
    }

    hard_disk_image_attached = true;
    game_active = true;
    log_cb(RETRO_LOG_INFO, "Game %s on %s: %s\n", game_disk.GetName(), game_disk.GetSystemName(), game_disk.GetFolder());

    for (int id = 1; id < GDOX_MAX_HARD_DISKS; id++)
    {
        FolderDisk* folder = game_disk.TakeHardDisk(id);
        bool attached = IsValidPointer(folder) ? core->AttachHardDisk(id, folder) :
            ((game_disk.GetHardDisk(id)[0] == 0) || core->AttachHardDisk(id, game_disk.GetHardDisk(id), false, ""));

        if (!attached)
            log_cb(RETRO_LOG_ERROR, "Unable to attach the game's hard disk %s\n", game_disk.GetHardDisk(id));
    }

    for (int drive = 0; drive < 2; drive++)
    {
        const std::vector<std::string>& floppies = game_disk.GetFloppies(drive);

        for (size_t i = 0; i < floppies.size(); i++)
        {
            size_t index = disk_images.size();

            if (!disk_add_images(floppies[i].c_str(), disk_images))
                return false;

            if ((i == 0) && (index < disk_images.size()))
                disk_insert((unsigned)index, drive == 0 ? DISK_DRIVE_FLOPPY_1 : DISK_DRIVE_FLOPPY_2);
        }
    }

    const std::vector<std::string>& cds = game_disk.GetCdRoms();

    for (size_t i = 0; i < cds.size(); i++)
    {
        size_t index = disk_images.size();
        bool added = MediaFile::IsDirectory(cds[i].c_str()) ? disk_add_cdrom(cds[i].c_str(), disk_images) :
            disk_add_images(cds[i].c_str(), disk_images);

        if (!added)
            return false;

        if ((i == 0) && (index < disk_images.size()))
            disk_insert((unsigned)index, DISK_DRIVE_CDROM);
    }

    const GDOX_Game_Info& info = game_disk.GetDescriptor().GetInfo();

    if (info.joystick >= 0)
        core->GetInput()->SetJoystickType((GDOX_Joystick_Type)info.joystick);

    if (info.cga_output >= 0)
        core->GetVideo()->SetCGAOutput(info.cga_output);

    core->SetGameTarget(game_target);
    core->SetBootDevice(info.boot_floppy ? GDOX_BOOT_DEVICE_FLOPPY_0 : GDOX_BOOT_DEVICE_HARD_DISK);
    update_core_options_display();
    return true;
}

static bool game_rebuild(void)
{
    GDOX_Machine_Settings settings;
    game_disk.SetProgram(game_program);
    game_disk.GetGameSettings(machine_settings, &settings);
    core->SetGameSettings(settings, game_disk.GetName());

    if (!game_disk.NeedsRestart(core->GetPendingMachineConfig()))
        return false;

    core->PowerOff();
    core->DetachHardDisk(0);
    log_write_back();

    if (!game_disk.Prepare(core->GetPendingMachineConfig()) || !core->AttachHardDisk(0, game_disk.TakeDisk()))
    {
        static char message[1024];
        snprintf(message, sizeof(message), "Unable to build the game's disk again: %s", game_disk.GetError());
        show_message(message, 3, RETRO_LOG_ERROR);
        game_end();
        return true;
    }

    std::vector<GameDisk_Message> messages;
    game_disk.GetMessages(&messages);

    for (size_t i = 0; i < messages.size(); i++)
        game_message(messages[i]);

    log_cb(RETRO_LOG_INFO, "Game %s: its disk again for the reset\n", game_disk.GetName());
    return true;
}

static void game_end(void)
{
    if (!game_active)
        return;

    game_active = false;

    if (core->IsPoweredOn())
        core->PowerOff();

    core->DetachHardDisk(0);

    for (int id = 1; id < GDOX_MAX_HARD_DISKS; id++)
    {
        if (game_disk.GetHardDisk(id)[0] != 0)
            core->DetachHardDisk(id);
    }

    log_write_back();
    game_disk.Close();
    core->ClearGameSettings();
    core->SetGameTarget(-1);
    core->GetVideo()->SetCGAOutput(cga_output);
    update_core_options_display();
}

static void log_write_back(void)
{
    const FolderDisk_Result& r = core->GetHardDiskResult(0);

    if (game_disk.IsImage())
        return;

    if (!r.done && !r.refused && (r.failed == 0))
        return;

    log_cb(r.done ? RETRO_LOG_INFO : RETRO_LOG_ERROR, "Write-back of %s: %u written, %u deleted, %u folders, %u conflicts, "
        "%u failed\n", game_disk.GetName(), r.written, r.deleted, r.directories, r.conflicts, r.failed);

    for (size_t i = 0; i < r.notes.size(); i++)
        log_cb(RETRO_LOG_WARN, "%s\n", r.notes[i].c_str());

    if (!r.done)
        log_cb(RETRO_LOG_ERROR, "The game's changes weren't written back, the next load tries again\n");
}

static std::string get_temp_path(void)
{
    return path_join(retro_save_directory, "Geardox Temp");
}

static void hard_disk_attach_builtin(void)
{
    if ((dos_disk == 2) || hard_disk_image_attached)
        return;

    int system = ((dos_disk == 0) && !msdos_zip.empty()) ? (int)GDOX_GAME_SYSTEM_MSDOS : (int)GDOX_GAME_SYSTEM_FREEDOS;
    std::string path(retro_save_directory);
    append_path_component(path, system == GDOX_GAME_SYSTEM_MSDOS ? k_dos_disk_msdos : k_dos_disk_freedos);

    if ((!MediaFile::Exists(path.c_str()) && !hard_disk_make_builtin(path, system)) && (system == GDOX_GAME_SYSTEM_MSDOS))
    {
        show_message("Unable to make the built-in MS-DOS disk, FreeDOS booted instead", 3, RETRO_LOG_WARN);
        system = GDOX_GAME_SYSTEM_FREEDOS;
        path = retro_save_directory;
        append_path_component(path, k_dos_disk_freedos);
    }

    if ((!MediaFile::Exists(path.c_str()) && !hard_disk_make_builtin(path, system)) ||
        !core->AttachHardDisk(hard_disk_get_builtin_slot(), path.c_str(), false, ""))
    {
        log_cb(RETRO_LOG_ERROR, "Unable to attach the built-in disk %s\n", path.c_str());
        return;
    }

    log_cb(RETRO_LOG_INFO, "Hard disk in slot %d: %s\n", hard_disk_get_builtin_slot(), path.c_str());
}

static bool hard_disk_make_builtin(const std::string& path, int system)
{
    std::string error;

    if (DosDisk::WriteBuiltin(path.c_str(), system, msdos_zip.c_str(), game_keyboard_layout, false, &error))
        return true;

    log_cb(RETRO_LOG_ERROR, "%s\n", error.c_str());
    return false;
}

static bool hard_disk_is_cdrom_slot(int id)
{
    return id == (int)core->GetPendingMachineConfig().cdrom - 1;
}

static int hard_disk_get_builtin_slot(void)
{
    return hard_disk_is_cdrom_slot(0) ? 1 : 0;
}

static void hard_disk_update(void)
{
    u32 count = 0;
    u32 unflushed = 0;

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        HardDiskImage* image = core->GetHardDisk(i);
        count += image->GetWriteCount();
        unflushed += image->GetUnflushedBlocks();
    }

    if (!hard_disk_idle_flush.Tick(count, unflushed != 0, k_idle_flush_hard_disk_frames))
        return;

    if (hard_disk_idle_flush.Done(core->FlushHardDisks()))
    {
        log_cb(RETRO_LOG_ERROR, "Unable to save the hard disk changes\n");
        show_message("Unable to save the hard disk changes", 3, RETRO_LOG_ERROR);
    }
}

static void cmos_load(void)
{
    const GDOX_Machine_Profile& profile = k_machine_profiles[core->GetPendingMachineConfig().model];
    std::string name(profile.key);

    cmos_shared_active = cmos_shared;
    cmos_idle_flush.Reset();
    cmos_path = retro_save_directory;
    append_path_component(cmos_path, (name + ".geardox.cmos").c_str());

    GDOX_CMOS_File_Result result = core->LoadCMOSFile(cmos_shared_active ? cmos_path.c_str() : NULL);

    if (result == GDOX_CMOS_FILE_LOADED)
        log_cb(RETRO_LOG_INFO, "CMOS loaded from %s\n", cmos_path.c_str());
    else if (result == GDOX_CMOS_FILE_INVALID)
        log_cb(RETRO_LOG_ERROR, "Invalid CMOS file: %s\n", cmos_path.c_str());
}

static void cmos_save(void)
{
    if (!cmos_shared_active)
        return;

    GDOX_CMOS_File_Result result = core->SaveCMOSFileIfChanged(cmos_path.c_str());

    if (result == GDOX_CMOS_FILE_UNCHANGED)
        return;

    bool saved = result == GDOX_CMOS_FILE_SAVED;

    if (saved)
        log_cb(RETRO_LOG_INFO, "CMOS saved to %s\n", cmos_path.c_str());

    if (cmos_idle_flush.Done(saved))
    {
        log_cb(RETRO_LOG_ERROR, "Unable to save the CMOS to %s\n", cmos_path.c_str());
        show_message("Unable to save the CMOS", 3, RETRO_LOG_ERROR);
    }
}

static void cmos_update(void)
{
    if (cmos_idle_flush.Tick(0, true, k_idle_flush_cmos_frames))
        cmos_save();
}
