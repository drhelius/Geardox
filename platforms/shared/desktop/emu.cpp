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

#define EMU_IMPORT
#include "emu.h"

#include <SDL3/SDL.h>
#include <atomic>
#include <fstream>
#include <iterator>
#include <math.h>
#include <new>
#include <string.h>
#include <string>
#include <thread>
#include <vector>
#include "config.h"
#include "debug/gui_debug_trace_logger.h"
#include "emu_floppy.h"
#include "emu_hard_disk.h"
#include "events.h"
#include "gui_notifications.h"
#include "input_macro.h"
#include "mcp/mcp_manager.h"
#include "rewind.h"
#include "runahead.h"
#include "sound_queue.h"
#include "utils.h"
#include "video_recorder.h"
#include "ogl_renderer.h"
#include "video/video.h"
#if defined(GDOX_ENABLE_PHYSICAL_CDROM)
#include "cdrom/cdrom_drive.h"
#endif

#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(_WIN32)
#define STBIW_WINDOWS_UTF8
#endif
#include "stb_image_write.h"

enum Loading_State
{
    Loading_State_None = 0,
    Loading_State_Loading,
    Loading_State_Finished
};

enum Loading_Request_Type
{
    Loading_Request_File = 0,
    Loading_Request_PhysicalCdRom
};

static GeardoxCore* geardox = NULL;
static s16* audio_buffer = NULL;
static u8* rgba_frame_buffer = NULL;
static bool audio_enabled = true;
static McpManager* mcp_manager = NULL;
static Uint64 rewind_last_counter = 0;
static double rewind_pop_accumulator = 0.0;
static bool video_recording_output = false;
static bool emu_debug_step_start = false;

static std::atomic<int> loading_state(Loading_State_None);
static std::thread loading_thread;
static bool loading_thread_active = false;
static bool loading_result = false;
static char loading_file_path[GDOX_MAX_PATH] = { };
static Loading_Request_Type loading_request_type = Loading_Request_File;
static std::vector<std::string> cdrom_playlist;
static int cdrom_playlist_index = -1;
static int cmos_model = -1;
static u8 cmos_saved[GDOX_CMOS_SIZE];

static void load_media_thread_func(void);
static void reset_buffers(void);
static void reset_run_state(void);
static const char* get_configurated_dir(int option, const char* path);
static void get_video_recording_size(const GDOX_Runtime_Info& runtime, int* width, int* height);
static void reset_rewind_timing(void);
static int get_rewind_pop_budget(void);
static bool unload_media(void);
static bool get_floppy_state_path(int index, char* path, size_t path_size);
static void restore_state_frame(const char* path, int index);
static bool get_cmos_path(int model, char* path, size_t path_size);
static void load_cmos(int model);
static void save_cmos(void);
static void sync_cmos(void);
static void notify_machine(void);
static Emu_Pad_Source pad_sources[GDOX_MAX_GAMEPADS];
#if defined(GDOX_ENABLE_PHYSICAL_CDROM)
static void stop_physical_cdrom_after_error(void);
#endif

bool emu_init(void)
{
    size_t frame_buffer_size = (size_t)GDOX_MAX_FRAME_BUFFER_WIDTH * GDOX_MAX_FRAME_BUFFER_HEIGHT * 4;
    emu_frame_buffer = new (std::nothrow) u8[frame_buffer_size];
    emu_debug_framebuffer = new (std::nothrow) u8[(size_t)EMU_DEBUG_FRAMEBUFFER_WIDTH * EMU_DEBUG_FRAMEBUFFER_HEIGHT * 4];
    audio_buffer = new (std::nothrow) s16[GDOX_AUDIO_BUFFER_SIZE];

    if (!IsValidPointer(emu_frame_buffer) || !IsValidPointer(emu_debug_framebuffer) || !IsValidPointer(audio_buffer))
    {
        Error("Unable to allocate emulator buffers");
        SafeDeleteArray(emu_frame_buffer);
        SafeDeleteArray(emu_debug_framebuffer);
        SafeDeleteArray(audio_buffer);
        return false;
    }

    memset(emu_debug_framebuffer, 0, (size_t)EMU_DEBUG_FRAMEBUFFER_WIDTH * EMU_DEBUG_FRAMEBUFFER_HEIGHT * 4);
    reset_buffers();

    geardox = new (std::nothrow) GeardoxCore();

    if (!IsValidPointer(geardox))
    {
        Error("Unable to allocate Geardox core");
        SafeDeleteArray(emu_frame_buffer);
        SafeDeleteArray(emu_debug_framebuffer);
        SafeDeleteArray(audio_buffer);
        return false;
    }

    geardox->Init();
    geardox->GetMedia()->SetTempPath(config_temp_path);
    emu_floppy_init();

    sound_queue_init();

    for (int i = 0; i < 5; i++)
        InitPointer(emu_savestates_screenshots[i].data);

    emu_savestates_generation = 0;
    emu_debug_command = Debug_Command_None;
    emu_debug_pc_changed = false;
    emu_debug_step_frames_pending = 0;
    emu_debug_disable_breakpoints = false;
    emu_debug_breakpoint_stops = 0;
    rewind_init();
    runahead_init();

    mcp_manager = new (std::nothrow) McpManager();

    if (!IsValidPointer(mcp_manager))
    {
        runahead_destroy();
        rewind_destroy();
        sound_queue_destroy();
        SafeDelete(geardox);
        SafeDeleteArray(audio_buffer);
        SafeDeleteArray(emu_frame_buffer);
        SafeDeleteArray(emu_debug_framebuffer);
        return false;
    }

    mcp_manager->Init(geardox);

    emu_frame_counter = 0;
    emu_audio_sync = true;
    emu_audio_rate_control = false;
    emu_audio_ratio = 1.0f;
    audio_enabled = true;
    return true;
}

void emu_destroy(void)
{
    if (loading_thread_active)
    {
        loading_thread.join();
        loading_thread_active = false;
    }

    loading_state.store(Loading_State_None);
    emu_stop_vgm_recording();
    emu_stop_video_recording();

    if (!emu_floppy_flush())
        Error("Unable to save one or more floppy disks on exit");

    save_cmos();
    emu_hard_disk_flush();

    runahead_destroy();
    rewind_destroy();
    SafeDelete(mcp_manager);
    sound_queue_destroy();
    SafeDelete(geardox);
    SafeDeleteArray(audio_buffer);
    SafeDeleteArray(emu_frame_buffer);
    SafeDeleteArray(emu_debug_framebuffer);
    SafeDeleteArray(rgba_frame_buffer);

    for (int i = 0; i < 5; i++)
        SafeDeleteArray(emu_savestates_screenshots[i].data);
}

void emu_update(void)
{
    if (loading_state.load() != Loading_State_None)
        return;

    emu_mcp_pump_commands();
    input_macro_update(Input_Macro_Owner_GUI);

#if defined(GDOX_ENABLE_PHYSICAL_CDROM)
    if (geardox->GetMedia()->HasPhysicalCdRomError())
    {
        stop_physical_cdrom_after_error();
        return;
    }
#endif

    if (emu_is_empty())
        return;

    int sample_count = 0;
    bool frame_executed = false;
    bool frame_completed = false;

    if (rewind_is_active())
    {
        int to_pop = get_rewind_pop_budget();

        if (to_pop > 0)
            rewind_pop(to_pop);

        int silence_count = GDOX_AUDIO_QUEUE_SIZE;
        memset(audio_buffer, 0, silence_count * sizeof(s16));
        sound_queue_write(audio_buffer, silence_count, false);
        return;
    }

    reset_rewind_timing();

    if (config_debug.debug)
    {
        bool stopped = false;
        bool breakpoint_hit = false;
        bool frames_stepped = false;

        if (emu_debug_command != Debug_Command_None && !geardox->IsPaused())
        {
            GeardoxCore::GDOX_Debug_Run debug_run;

            debug_run.step_debugger = emu_debug_command == Debug_Command_Step;
            debug_run.step_over = emu_debug_command == Debug_Command_StepOver;
            debug_run.step_start = emu_debug_step_start;
            debug_run.stop_on_breakpoint = !emu_debug_disable_breakpoints;
            debug_run.stop_on_run_to_breakpoint = true;
            debug_run.skip_interrupts_on_step = config_debug.step_skip_interrupts &&
                emu_debug_command == Debug_Command_Step;
            debug_run.stopped = false;
            debug_run.breakpoint_hit = false;

            rewind_commit_seek();

            GDOX_Run_Result result = geardox->RunToFrame(emu_frame_buffer, audio_buffer, &sample_count, &debug_run);
            frame_executed = result != GDOX_RUN_NOT_READY;
            frame_completed = result == GDOX_RUN_FRAME_READY && !debug_run.stopped;
            stopped = debug_run.stopped;
            breakpoint_hit = debug_run.breakpoint_hit;

            if (result == GDOX_RUN_FRAME_READY)
                emu_debug_step_start = false;
        }

        // Frame steps run whole frames with breakpoints active; a breakpoint ends the remaining frames
        if (stopped)
            emu_debug_step_frames_pending = 0;
        else if (emu_debug_command == Debug_Command_StepFrame && frame_completed)
        {
            emu_debug_step_frames_pending--;
            frames_stepped = emu_debug_step_frames_pending <= 0;
        }

        if (stopped && breakpoint_hit)
            emu_debug_breakpoint_stops++;

        // Steps keep running frames until the core reports where they stop
        if (stopped || frames_stepped)
        {
            emu_debug_command = Debug_Command_None;
            emu_debug_step_frames_pending = 0;
            emu_debug_pc_changed = true;

            if (config_debug.dis_look_ahead_count > 0)
                geardox->GetI386()->DisassembleAhead(config_debug.dis_look_ahead_count);
        }
    }
    else if (!geardox->IsPaused())
    {
        rewind_commit_seek();

        GDOX_Run_Result result = GDOX_RUN_NOT_READY;
        int runahead = geardox->GetFirmware()->IsReady() ? runahead_get_frames() : 0;

        if (runahead > 0)
        {
            runahead_run(runahead, emu_frame_buffer, audio_buffer, &sample_count);
            result = GDOX_RUN_FRAME_READY;
        }
        else
            result = geardox->RunToFrame(emu_frame_buffer, audio_buffer, &sample_count);

        frame_executed = result != GDOX_RUN_NOT_READY;
        frame_completed = result == GDOX_RUN_FRAME_READY;
    }

    emu_debug_update();

    if (frame_executed)
    {
        if (frame_completed)
        {
            emu_frame_counter++;
            emu_frame_stale = false;
            emu_floppy_update();
            emu_hard_disk_update();
        }

        rewind_push();

        if (video_recorder_is_recording())
        {
            video_recorder_add_audio(audio_buffer, sample_count);

            if (frame_completed)
            {
                if (video_recording_output)
                    emu_video_recording_frame_pending = true;
                else
                {
                    GDOX_Runtime_Info runtime;
                    emu_get_runtime(runtime);
                    const u8* rgba = emu_get_rgba_frame(runtime.screen_width, runtime.screen_height);

                    if (IsValidPointer(rgba))
                        video_recorder_add_video(rgba, runtime.screen_width, runtime.screen_height, 4);
                }
            }
        }
    }

    if (sample_count > 0 && !geardox->IsPaused())
        sound_queue_write(audio_buffer, sample_count, emu_audio_sync, emu_audio_rate_control, emu_audio_ratio);
    else if (geardox->IsPaused())
    {
        int silence_count = GDOX_AUDIO_QUEUE_SIZE;
        memset(audio_buffer, 0, silence_count * sizeof(s16));
        sound_queue_write(audio_buffer, silence_count, false);
    }

    if (!geardox->IsPoweredOn())
    {
        reset_run_state();
        update_savestates_data();
        gui_notify(gui_NotificationInfo, ICON_MD_POWER_SETTINGS_NEW, "Powered off by software", NULL, "power");
    }
}

void emu_load_media_async(const char* file_path)
{
    if (!IsValidPointer(file_path) || file_path[0] == '\0' || loading_state.load() != Loading_State_None)
    {
        return;
    }

    if (loading_thread_active)
    {
        loading_thread.join();
        loading_thread_active = false;
    }

    gui_debug_trace_logger_reset("media_change");
    strncpy_fit(loading_file_path, file_path, sizeof(loading_file_path));
    loading_request_type = Loading_Request_File;
    loading_result = false;
    loading_state.store(Loading_State_Loading);
    loading_thread = std::thread(load_media_thread_func);
    loading_thread_active = true;
}

void emu_load_physical_cdrom_async(const char* device_id)
{
#if defined(GDOX_ENABLE_PHYSICAL_CDROM)
    if (!IsValidPointer(device_id) || device_id[0] == '\0' || loading_state.load() != Loading_State_None)
    {
        Debug("Ignoring physical CD-ROM async load request while another media load is active");
        return;
    }

    if (loading_thread_active)
    {
        loading_thread.join();
        loading_thread_active = false;
    }

    gui_debug_trace_logger_reset("media_change");
    Log("Queueing physical CD-ROM async load: %s", device_id);
    strncpy_fit(loading_file_path, device_id, sizeof(loading_file_path));
    loading_request_type = Loading_Request_PhysicalCdRom;
    loading_result = false;
    loading_state.store(Loading_State_Loading);
    loading_thread = std::thread(load_media_thread_func);
    loading_thread_active = true;
#else
    UNUSED(device_id);
#endif
}

bool emu_is_media_loading(void)
{
    return loading_state.load() == Loading_State_Loading;
}

bool emu_finish_media_loading(void)
{
    if (loading_state.load() != Loading_State_Finished)
        return false;

    if (loading_thread_active)
    {
        loading_thread.join();
        loading_thread_active = false;
    }

    loading_state.store(Loading_State_None);

    if (!loading_result)
    {
#if defined(GDOX_ENABLE_PHYSICAL_CDROM)
        if (loading_request_type == Loading_Request_PhysicalCdRom)
            Debug("Physical CD-ROM async load failed: %s", loading_file_path);
#endif
        return false;
    }

    emu_audio_reset();
    update_savestates_data();
    rewind_reset();
    return true;
}

void emu_set_gamepad_state(GDOX_Controllers controller, const GDOX_GamePad_State& state)
{
    if (!IsValidPointer(geardox))
        return;

    geardox->GetInput()->SetGamePadState((int)controller, state);
}

void emu_key_pressed(GDOX_Keys key)
{
    if (IsValidPointer(geardox))
        geardox->KeyPressed(key);
}

void emu_key_released(GDOX_Keys key)
{
    if (IsValidPointer(geardox))
        geardox->KeyReleased(key);
}

void emu_release_all_keys(void)
{
    if (IsValidPointer(geardox))
        geardox->ReleaseAllKeys();
}

void emu_set_mouse_delta(GDOX_Controllers controller, int x, int y)
{
    if (IsValidPointer(geardox))
        geardox->GetInput()->SetMouseDelta((int)controller, x, y);
}

void emu_clear_mouse(GDOX_Controllers controller)
{
    if (IsValidPointer(geardox))
        geardox->GetInput()->ClearMouseInput((int)controller);
}

void emu_pause(void)
{
    if (IsValidPointer(geardox))
        geardox->Pause(true);
}

void emu_resume(void)
{
    if (IsValidPointer(geardox))
        geardox->Pause(false);
}

bool emu_is_paused(void)
{
    return IsValidPointer(geardox) && geardox->IsPaused();
}

bool emu_is_debug_idle(void)
{
    return config_debug.debug && emu_debug_command == Debug_Command_None;
}

bool emu_is_empty(void)
{
    if (loading_state.load() != Loading_State_None)
        return true;

    return !IsValidPointer(geardox) || !geardox->IsPoweredOn();
}

// Unlike emu_is_empty a running machine stays powered on while a disc swap loads
bool emu_is_powered_on(void)
{
    return IsValidPointer(geardox) && geardox->IsPoweredOn();
}

bool emu_power_on(void)
{
    if (!IsValidPointer(geardox) || loading_state.load() != Loading_State_None)
        return false;

    gui_debug_trace_logger_reset("power");
    sync_cmos();

    if (!geardox->PowerOn())
        return false;

    emu_update_pad_types();
    notify_machine();
    emu_floppy_check_drives();
    reset_run_state();
    update_savestates_data();
    return true;
}

void emu_power_off(void)
{
    if (!IsValidPointer(geardox) || loading_state.load() != Loading_State_None)
        return;

    gui_debug_trace_logger_reset("power");
    geardox->PowerOff();
    save_cmos();
    reset_run_state();
    update_savestates_data();
}

void emu_reset(void)
{
    if (!IsValidPointer(geardox))
        return;

    gui_debug_trace_logger_reset("reset");
    sync_cmos();
    geardox->ResetMedia();
    emu_update_pad_types();
    notify_machine();
    emu_floppy_check_drives();
    reset_run_state();
}

bool emu_cdrom_playlist_load(const char* path, bool* floppies)
{
    emu_cdrom_playlist_clear();

    std::ifstream file;
    open_ifstream_utf8(file, path, std::ios::in | std::ios::binary);

    if (!file.is_open())
        return false;

    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::vector<Media::PlaylistEntry> entries;

    if (!Media::ParsePlaylist(path, text.c_str(), text.size(), entries))
        return false;

    bool others = false;

    for (size_t i = 0; i < entries.size(); i++)
    {
        const char* entry = entries[i].path.c_str();

        if (entries[i].save_disk)
            continue;

        if (ends_with_no_case(entry, ".m3u") || emu_floppy_is_image(entry))
            others = true;
        else
            cdrom_playlist.push_back(entries[i].path);
    }

    if (IsValidPointer(floppies))
        *floppies = others;

    return !cdrom_playlist.empty();
}

void emu_cdrom_playlist_clear(void)
{
    cdrom_playlist.clear();
    cdrom_playlist_index = -1;
}

bool emu_cdrom_playlist_select(const char* path)
{
    for (size_t i = 0; i < cdrom_playlist.size(); i++)
    {
        if (cdrom_playlist[i] == path)
        {
            cdrom_playlist_index = (int)i;
            return true;
        }
    }

    return false;
}

int emu_cdrom_playlist_get_count(void)
{
    return (int)cdrom_playlist.size();
}

int emu_cdrom_playlist_get_index(void)
{
    return cdrom_playlist_index;
}

const char* emu_cdrom_playlist_get_path(int index)
{
    return (index >= 0) && (index < (int)cdrom_playlist.size()) ? cdrom_playlist[index].c_str() : NULL;
}

const char* emu_cdrom_playlist_get_name(int index)
{
    const char* path = emu_cdrom_playlist_get_path(index);

    if (!IsValidPointer(path))
        return NULL;

    const char* name = path + strlen(path);

    while ((name > path) && (name[-1] != '/') && (name[-1] != '\\'))
        name--;

    return name;
}

bool emu_eject_media(void)
{
    if (loading_state.load() != Loading_State_None)
    {
        Debug("Ignoring media eject request while media is loading");
        return false;
    }

#if defined(GDOX_ENABLE_PHYSICAL_CDROM)
    if (geardox->GetMedia()->IsPhysicalCdRom())
    {
        char device_id[256];
        strncpy_fit(device_id, geardox->GetMedia()->GetPhysicalCdRomDeviceId(), sizeof(device_id));
        unload_media();

        Log("Ejecting physical CD-ROM: %s", device_id);
        bool ejected = CdRomDrive::Eject(device_id);
        Debug("Physical CD-ROM eject finished: %s (%s)", device_id, ejected ? "success" : "failure");
        return ejected;
    }
#endif

    return unload_media();
}

void emu_set_preload_cdrom(bool enabled)
{
    if (IsValidPointer(geardox))
        geardox->GetMedia()->PreloadCdRom(enabled);
}

void emu_apply_bios_settings(void)
{
    if (!IsValidPointer(geardox))
        return;

    geardox->SetBootMenu(config_emulator.bios_boot_menu);
    geardox->SetDebugConsole(config_emulator.bios_debug_console, config_emulator.bios_log);
}

// The dynarec runs while the debugger is off, the debugger always steps the interpreter
void emu_set_dynarec(bool enabled)
{
    if (!IsValidPointer(geardox))
        return;

    geardox->GetI386()->EnableDynarec(enabled);

    if (enabled && !geardox->GetI386()->IsDynarecEnabled())
        Log("Dynarec unavailable, the CPU runs in the interpreter");
}

bool emu_is_dynarec_enabled(void)
{
    return IsValidPointer(geardox) && geardox->GetI386()->IsDynarecEnabled();
}

void emu_set_native_resolution(bool enabled)
{
    if (IsValidPointer(geardox))
        geardox->GetVideo()->EnableNativeResolution(enabled);
}

void emu_apply_machine_settings(void)
{
    if (!IsValidPointer(geardox))
        return;

    GDOX_Machine_Settings settings;
    settings.model = config_machine.model;
    settings.cpu_mhz = config_machine.cpu_mhz;
    settings.ram_mb = config_machine.ram_mb;
    settings.fpu = config_machine.fpu;
    settings.speed_mode = GDOX_MACHINE_AUTO;
    geardox->SetMachineSettings(settings);
}

bool emu_latch_auto_media(void)
{
    if (!IsValidPointer(geardox))
        return false;

    geardox->LatchAutoMedia();
    emu_update_pad_types();

    if (!geardox->IsPoweredOn())
        return false;

    if (geardox->IsMachineConfigPending())
        return true;

    notify_machine();
    return false;
}

void emu_audio_mute(bool mute)
{
    audio_enabled = !mute;

    if (IsValidPointer(geardox))
        geardox->GetAudio()->Mute(mute);

    emu_audio_reset();
}

void emu_audio_set_master_volume(float volume)
{
    if (IsValidPointer(geardox))
        geardox->GetAudio()->SetMasterVolume(volume);
}

void emu_audio_set_source_volume(int source, float volume)
{
    if (!IsValidPointer(geardox))
        return;

    Audio* audio = geardox->GetAudio();

    switch (source)
    {
        case Audio::AUDIO_SOURCE_FM:
            audio->SetFMVolume(volume);
            break;
        case Audio::AUDIO_SOURCE_PCM:
            audio->SetPCMVolume(volume);
            break;
        case Audio::AUDIO_SOURCE_CDDA:
            audio->SetCDDAVolume(volume);
            break;
        case Audio::AUDIO_SOURCE_BUZZER:
            audio->SetBuzzerVolume(volume);
            break;
        default:
            break;
    }
}

void emu_audio_set_source_mute(int source, bool mute)
{
    if (IsValidPointer(geardox))
        geardox->GetAudio()->SetSourceMute(source, mute);
}

bool emu_audio_is_source_muted(int source)
{
    return IsValidPointer(geardox) && geardox->GetAudio()->IsSourceMuted(source);
}

void emu_audio_reset(void)
{
    if (audio_enabled && sound_queue_is_open())
        sound_queue_flush();
    else
        emu_audio_restart();
}

void emu_audio_restart(void)
{
    sound_queue_stop();

    if (audio_enabled)
        sound_queue_start(GDOX_AUDIO_SAMPLE_RATE, 2, GDOX_AUDIO_QUEUE_SIZE, config_audio.buffer_count);
}

bool emu_is_audio_enabled(void)
{
    return audio_enabled;
}

bool emu_is_audio_open(void)
{
    return sound_queue_is_open();
}

bool emu_save_state_slot(int index)
{
    if (emu_is_empty())
        return false;

    const char* dir = get_configurated_dir(config_emulator.savestates_dir_option,
        config_emulator.savestates_path.c_str());
    char path[GDOX_MAX_PATH];
    bool saved = get_floppy_state_path(index, path, sizeof(path)) ? geardox->SaveState(path, -1, true) :
        geardox->SaveState(dir, index, true);

    update_savestates_data();
    return saved;
}

bool emu_load_state_slot(int index)
{
    if (emu_is_empty())
        return false;

    const char* dir = get_configurated_dir(config_emulator.savestates_dir_option,
        config_emulator.savestates_path.c_str());
    char path[GDOX_MAX_PATH];

    if (get_floppy_state_path(index, path, sizeof(path)))
    {
        dir = path;
        index = -1;
    }

    if (!geardox->LoadState(dir, index))
        return false;

    restore_state_frame(dir, index);
    emu_floppy_reconcile();
    emu_hard_disk_state_loaded(false);
    emu_debug_state_restored();
    events_sync_input();
    rewind_reset();
    return true;
}

bool emu_save_state_file(const char* file_path)
{
    if (emu_is_empty())
        return false;

    return geardox->SaveState(file_path, -1, true);
}

bool emu_load_state_file(const char* file_path)
{
    if (emu_is_empty() || !geardox->LoadState(file_path))
        return false;

    restore_state_frame(file_path, -1);
    emu_floppy_reconcile();
    emu_hard_disk_state_loaded(false);
    emu_debug_state_restored();
    events_sync_input();
    rewind_reset();
    return true;
}

void emu_debug_state_restored(void)
{
    geardox->GetI386()->ResetDebuggerExecutionState();
    emu_debug_command = Debug_Command_None;
    emu_debug_step_frames_pending = 0;
    emu_debug_pc_changed = true;
    emu_machine_generation++;
}

void update_savestates_data(void)
{
    emu_savestates_generation++;

    for (int i = 0; i < 5; i++)
    {
        emu_savestates[i].magic = 0;
        emu_savestates[i].rom_name[0] = 0;
        SafeDeleteArray(emu_savestates_screenshots[i].data);
        emu_savestates_screenshots[i].width = 0;
        emu_savestates_screenshots[i].height = 0;
        emu_savestates_screenshots[i].size = 0;
    }

    if (emu_is_empty())
        return;

    for (int i = 0; i < 5; i++)
    {
        const char* dir = get_configurated_dir(config_emulator.savestates_dir_option,
            config_emulator.savestates_path.c_str());
        char path[GDOX_MAX_PATH];
        int index = i + 1;

        if (get_floppy_state_path(index, path, sizeof(path)))
        {
            dir = path;
            index = -1;
        }

        if (!geardox->GetSaveStateHeader(index, dir, &emu_savestates[i]))
            continue;

        if (emu_savestates[i].screenshot_size > 0)
        {
            emu_savestates_screenshots[i].data = new (std::nothrow) u8[emu_savestates[i].screenshot_size];

            if (!IsValidPointer(emu_savestates_screenshots[i].data))
                continue;

            emu_savestates_screenshots[i].size = emu_savestates[i].screenshot_size;

            if (!geardox->GetSaveStateScreenshot(index, dir, &emu_savestates_screenshots[i]))
            {
                SafeDeleteArray(emu_savestates_screenshots[i].data);
                emu_savestates_screenshots[i].size = 0;
            }
        }
    }
}

void emu_get_runtime(GDOX_Runtime_Info& runtime)
{
    memset(&runtime, 0, sizeof(runtime));

    if (IsValidPointer(geardox))
        geardox->GetRuntimeInfo(runtime);
    else
    {
        runtime.screen_width = GDOX_FRAME_BUFFER_WIDTH;
        runtime.screen_height = GDOX_FRAME_BUFFER_HEIGHT;
        runtime.display_width = GDOX_FRAME_BUFFER_WIDTH;
        runtime.display_height = GDOX_FRAME_BUFFER_HEIGHT;
        runtime.sample_rate = GDOX_AUDIO_SAMPLE_RATE;
    }
}

double emu_get_frame_rate(void)
{
    if (!IsValidPointer(geardox))
        return 60.0;

    GDOX_Runtime_Info runtime;
    emu_get_runtime(runtime);

    return runtime.frame_time > 0.0f ? 1000.0 / runtime.frame_time : 60.0;
}

void emu_get_info(char* info, int buffer_size)
{
    if (!IsValidPointer(info) || buffer_size <= 0)
        return;

    if (emu_is_empty())
    {
        strncpy_fit(info, "No media selected", (size_t)buffer_size);
        return;
    }

    Media* media = geardox->GetMedia();
    Firmware* firmware = geardox->GetFirmware();

    if (media->IsCDROM())
    {
        CdRomMedia* cdrom_media = geardox->GetCDROMMedia();
        GDOX_CdRomMSF length = cdrom_media->GetCdRomLength();
        snprintf(info, (size_t)buffer_size, "File Name: %s\nPath: %s\nCD-ROM Tracks: %d\nCD-ROM Length: %02d:%02d:%02d\n"
            "CRC: %08X\nFirmware Ready: %s", media->GetFileName(), media->GetFilePath(), cdrom_media->GetTrackCount(),
            length.minutes, length.seconds, length.frames, media->GetCRC(), firmware->IsReady() ? "Yes" : "No");
        return;
    }

    snprintf(info, (size_t)buffer_size, "File Name: %s\nPath: %s\nSize: %d bytes\nCRC: %08X\nFirmware Ready: %s",
        media->GetFileName(), media->GetFilePath(), media->GetSize(), media->GetCRC(),
        firmware->IsReady() ? "Yes" : "No");
}

GeardoxCore* emu_get_core(void)
{
    return geardox;
}

void emu_debug_step_over(void)
{
    if (!IsValidPointer(geardox))
        return;

    emu_debug_command = Debug_Command_StepOver;
    emu_debug_step_start = true;
    emu_resume();
}

void emu_debug_step_into(void)
{
    emu_debug_command = Debug_Command_Step;
    emu_debug_step_start = true;
    emu_resume();
}

bool emu_debug_step_out(void)
{
    if (!IsValidPointer(geardox))
        return false;

    bool call = geardox->GetI386()->AddStepOutBreakpoint();

    if (call)
        emu_debug_command = Debug_Command_Continue;
    else
    {
        emu_debug_command = Debug_Command_Step;
        emu_debug_step_start = true;
    }

    emu_resume();
    return call;
}

void emu_debug_step_frame(void)
{
    emu_debug_step_frames(1);
}

void emu_debug_step_frames(int frames)
{
    if (frames < 1)
        frames = 1;

    emu_debug_step_frames_pending += frames;
    emu_debug_command = Debug_Command_StepFrame;
    emu_resume();
}

void emu_debug_break(void)
{
    emu_resume();
    emu_debug_step_frames_pending = 0;
    emu_debug_step_start = false;

    if (!IsValidPointer(geardox))
        return;

    geardox->GetI386()->ClearRunToBreakpoint();

    if (emu_debug_command == Debug_Command_None)
        return;

    emu_debug_command = Debug_Command_None;
    emu_debug_pc_changed = true;

    if (config_debug.dis_look_ahead_count > 0)
        geardox->GetI386()->DisassembleAhead(config_debug.dis_look_ahead_count);
}

void emu_debug_continue(void)
{
    emu_debug_command = Debug_Command_Continue;
    emu_resume();
}

void emu_set_pad_type(GDOX_Controllers controller, GDOX_Controller_Type type)
{
    if (IsValidPointer(geardox))
        geardox->GetInput()->SetControllerType((int)controller, type);
}

GDOX_Controller_Type emu_get_pad_type(GDOX_Controllers controller)
{
    if (!IsValidPointer(geardox))
        return GDOX_CONTROLLER_NONE;

    return geardox->GetInput()->GetControllerType((int)controller);
}

void emu_update_pad_types(void)
{
    if (!IsValidPointer(geardox))
        return;

    GDOX_Controller_Type types[GDOX_MAX_GAMEPADS];
    bool decided[GDOX_MAX_GAMEPADS];
    bool mouse = false;

    for (int i = 0; i < GDOX_MAX_GAMEPADS; i++)
    {
        decided[i] = config_input.controller_type[i] != config_ControllerType_Auto;
        types[i] = decided[i] ? (GDOX_Controller_Type)config_input.controller_type[i] : GDOX_CONTROLLER_NONE;
        pad_sources[i] = Emu_Pad_Source_Explicit;
        mouse = mouse || (types[i] == GDOX_CONTROLLER_MOUSE);
    }

    for (int i = 0; i < GDOX_MAX_GAMEPADS; i++)
    {
        GDOX_Controller_Type type = GDOX_CONTROLLER_NONE;

        if (decided[i] || !geardox->GetDatabaseController(i, type) || (mouse && (type == GDOX_CONTROLLER_MOUSE)))
            continue;

        types[i] = type;
        decided[i] = true;
        pad_sources[i] = Emu_Pad_Source_Database;
        mouse = mouse || (type == GDOX_CONTROLLER_MOUSE);
    }

    for (int i = 0; i < GDOX_MAX_GAMEPADS; i++)
    {
        if (!decided[i])
        {
            bool default_mouse = i == GDOX_CONTROLLER_2;
            types[i] = (default_mouse && !mouse) ? GDOX_CONTROLLER_MOUSE : GDOX_CONTROLLER_GAMEPAD;
            pad_sources[i] = (default_mouse && mouse) ? Emu_Pad_Source_Fallback : Emu_Pad_Source_Default;
            mouse = mouse || (types[i] == GDOX_CONTROLLER_MOUSE);
        }

        emu_set_pad_type((GDOX_Controllers)i, types[i]);
    }
}

Emu_Pad_Source emu_get_pad_source(GDOX_Controllers controller)
{
    return pad_sources[controller];
}

bool emu_get_pad_mismatch(GDOX_Controllers controller, GDOX_Controller_Type* wanted)
{
    GDOX_Controller_Type type = GDOX_CONTROLLER_NONE;

    if (!IsValidPointer(geardox) || !geardox->GetDatabaseController((int)controller, type))
        return false;

    GDOX_Controller_Type in_use = emu_get_pad_type(controller);

    if (in_use == type)
        return false;

    *wanted = type;
    return true;
}

const char* emu_get_pad_type_name(int type)
{
    static const char* const names[] = { "None", "Gamepad", "Mouse" };
    return ((type >= GDOX_CONTROLLER_NONE) && (type <= GDOX_CONTROLLER_MOUSE)) ? names[type] : "Unknown";
}

bool emu_save_screenshot(const char* file_path)
{
    if (!IsValidPointer(file_path) || file_path[0] == '\0' || !IsValidPointer(emu_frame_buffer))
    {
        return false;
    }

    GDOX_Runtime_Info runtime;
    emu_get_runtime(runtime);
    const u8* rgba = emu_get_rgba_frame(runtime.screen_width, runtime.screen_height);
    int result = 0;

    if (IsValidPointer(rgba))
        result = stbi_write_png(file_path, runtime.screen_width, runtime.screen_height, 4, rgba, runtime.screen_width * 4);

    if (result != 0)
        Log("Screenshot saved to %s", file_path);
    else
        Error("Unable to save screenshot to %s", file_path);

    return result != 0;
}

static u32 debug_rgba(u32 color)
{
    return 0xFF000000U | ((color & 0xFF) << 16) | (color & 0xFF00) | ((color >> 16) & 0xFF);
}

// One row per pitch of CRTC addresses from address 0, in the rotation the CRTC uses now. The plane views are plain offsets
static void debug_buffer_info(int buffer, int pitch, Emu_Debug_Buffer_Info& info)
{
    Video* video = geardox->GetVideo();
    Video::Video_State* state = video->GetState();
    const u8* crtc = state->crtc;
    int row_scans = (crtc[k_video_cr_max_scan] & 0x1F) + 1;
    u32 addresses = 0x10000;

    if ((crtc[k_video_cr_underline] & 0x40) != 0)
        addresses = 0x4000;
    else if ((crtc[k_video_cr_mode] & 0x40) == 0)
        addresses = 0x8000;

    memset(&info, 0, sizeof(info));
    info.pitch = pitch > 0 ? pitch : crtc[k_video_cr_offset] * 2;

    if (info.pitch == 0)
        info.pitch = 80;

    info.cell_width = buffer == Emu_Debug_Buffer_256 ? 4 : 8;
    info.cell_height = buffer == Emu_Debug_Buffer_Text ? row_scans : 1;
    info.width = MIN(info.pitch * info.cell_width, EMU_DEBUG_FRAMEBUFFER_WIDTH);

    if (buffer <= Emu_Debug_Buffer_Plane3)
        addresses = VIDEO_PLANE_SIZE;

    info.height = MIN((int)(addresses / (u32)info.pitch) * info.cell_height, EMU_DEBUG_FRAMEBUFFER_HEIGHT);

    u32 start = video->GetStartAddress();
    int rows = (int)state->display_lines / (row_scans << ((crtc[k_video_cr_max_scan] >> 7) & 0x01));

    info.window_x = (int)(start % (u32)info.pitch) * info.cell_width;
    info.window_y = (int)(start / (u32)info.pitch) * info.cell_height;
    info.window_width = ((int)crtc[k_video_cr_horizontal_display] + 1) * info.cell_width;
    info.window_height = rows * info.cell_height;
}

static void debug_decode_text(u32* pixels, const Emu_Debug_Buffer_Info& info)
{
    Video* video = geardox->GetVideo();
    Video::Video_State* state = video->GetState();
    u8 map = state->sequencer[k_video_sr_character_map];
    u32 font_a = ((map >> 2) & 0x03) * 0x4000 + ((map & 0x20) != 0 ? 0x2000 : 0);
    u32 font_b = (map & 0x03) * 0x4000 + ((map & 0x10) != 0 ? 0x2000 : 0);
    int columns = info.width / info.cell_width;
    int rows = info.height / info.cell_height;

    for (int row = 0; row < rows; row++)
    {
        for (int column = 0; column < columns; column++)
        {
            u32 offset = video->GetCRTCOffset((u32)row * (u32)info.pitch + (u32)column, 0);
            u8 character = video->PeekPlane(0, offset);
            u8 attribute = video->PeekPlane(1, offset);
            u32 foreground = debug_rgba(video->GetAttributeColor(attribute & 0x0F));
            u32 background = debug_rgba(video->GetAttributeColor(attribute >> 4));
            u32 font = ((attribute & 0x08) != 0 ? font_a : font_b) + (u32)character * 32;

            for (int y = 0; y < info.cell_height; y++)
            {
                u8 bits = video->PeekPlane(2, font + (u32)y);
                u32* line = pixels + (row * info.cell_height + y) * EMU_DEBUG_FRAMEBUFFER_WIDTH + column * 8;

                for (int x = 0; x < 8; x++)
                    line[x] = ((bits >> (7 - x)) & 0x01) != 0 ? foreground : background;
            }
        }
    }
}

void emu_debug_decode_buffer(int buffer, int pitch, u8* output, Emu_Debug_Buffer_Info& info)
{
    debug_buffer_info(buffer, pitch, info);

    u32* pixels = (u32*)output;

    if (buffer == Emu_Debug_Buffer_Text)
    {
        debug_decode_text(pixels, info);
        return;
    }

    Video* video = geardox->GetVideo();
    int columns = info.width / info.cell_width;

    for (int y = 0; y < info.height; y++)
    {
        u32* line = pixels + y * EMU_DEBUG_FRAMEBUFFER_WIDTH;

        for (int column = 0; column < columns; column++)
        {
            u32 address = (u32)y * (u32)info.pitch + (u32)column;
            u32 offset = buffer <= Emu_Debug_Buffer_Plane3 ? address & 0xFFFF : video->GetCRTCOffset(address, 0);
            u8 planes[VIDEO_PLANE_COUNT];

            for (int plane = 0; plane < VIDEO_PLANE_COUNT; plane++)
                planes[plane] = video->PeekPlane(plane, offset);

            if (buffer == Emu_Debug_Buffer_256)
            {
                for (int x = 0; x < 4; x++)
                    line[column * 4 + x] = debug_rgba(video->GetDACColor(planes[x]));
            }
            else if (buffer == Emu_Debug_Buffer_Planar)
            {
                for (int x = 0; x < 8; x++)
                {
                    int bit = 7 - x;
                    int value = ((planes[0] >> bit) & 1) | (((planes[1] >> bit) & 1) << 1) |
                        (((planes[2] >> bit) & 1) << 2) | (((planes[3] >> bit) & 1) << 3);
                    line[column * 8 + x] = debug_rgba(video->GetAttributeColor(value));
                }
            }
            else
            {
                for (int x = 0; x < 8; x++)
                    line[column * 8 + x] = ((planes[buffer] >> (7 - x)) & 0x01) != 0 ? 0xFFFFFFFFU : 0xFF000000U;
            }
        }
    }
}

void emu_debug_update(void)
{
    static u64 last_key = 0;

    if (!config_debug.show_vram_viewer || emu_is_empty())
    {
        last_key = 0;
        return;
    }

    u64 key = geardox->GetMemory()->GetDebugSnapshotId() * 1099511628211ULL;
    key = (key ^ geardox->GetScheduler()->GetClocks()) * 1099511628211ULL;
    key = (key ^ (u64)config_debug.vram_viewer_tab) * 1099511628211ULL;
    key = (key ^ (u64)config_debug.vram_viewer_pitch) * 1099511628211ULL;

    if (key == last_key)
        return;

    last_key = key;
    emu_debug_decode_buffer(config_debug.vram_viewer_tab, config_debug.vram_viewer_pitch, emu_debug_framebuffer,
        emu_debug_framebuffer_info);
}

int emu_get_debug_buffer_png(int buffer, int pitch, unsigned char** out_buffer, Emu_Debug_Buffer_Info& info)
{
    if (!IsValidPointer(out_buffer) || emu_is_empty())
        return 0;

    *out_buffer = NULL;

    u8* pixels = new (std::nothrow) u8[(size_t)EMU_DEBUG_FRAMEBUFFER_WIDTH * EMU_DEBUG_FRAMEBUFFER_HEIGHT * 4];

    if (!IsValidPointer(pixels))
        return 0;

    emu_debug_decode_buffer(buffer, pitch, pixels, info);

    int size = 0;
    *out_buffer = stbi_write_png_to_mem(pixels, EMU_DEBUG_FRAMEBUFFER_WIDTH * 4, info.width, info.height, 4, &size);
    SafeDeleteArray(pixels);
    return size;
}

int emu_get_screenshot_png(unsigned char** out_buffer)
{
    if (!IsValidPointer(out_buffer) || !IsValidPointer(emu_frame_buffer))
        return 0;

    *out_buffer = NULL;
    GDOX_Runtime_Info runtime;
    emu_get_runtime(runtime);
    const u8* rgba = emu_get_rgba_frame(runtime.screen_width, runtime.screen_height);

    if (!IsValidPointer(rgba))
        return 0;

    int size = 0;
    *out_buffer = stbi_write_png_to_mem(rgba, runtime.screen_width * 4, runtime.screen_width,
        runtime.screen_height, 4, &size);
    return size;
}

bool emu_start_vgm_recording(const char* file_path)
{
    if (emu_is_empty())
        return false;

    if (geardox->GetAudio()->IsVgmRecording())
        emu_stop_vgm_recording();

    char game_name[GDOX_MAX_PATH];
    get_filename_without_extension(geardox->GetMedia()->GetFileName(), game_name, sizeof(game_name));

    GDOX_VGM_Metadata metadata;
    metadata.game_name = game_name;
    metadata.system_name = "IBM PC/AT";
    metadata.comment = "Created with " GDOX_TITLE " " GDOX_VERSION;

    if (!geardox->GetAudio()->StartVgmRecording(file_path, GDOX_SOUND_CLOCK_RATE, metadata))
        return false;

    Log("VGM recording started: %s", file_path);
    return true;
}

void emu_stop_vgm_recording(void)
{
    if (geardox->GetAudio()->IsVgmRecording())
    {
        geardox->GetAudio()->StopVgmRecording();
        Log("VGM recording stopped");
    }
}

bool emu_is_vgm_recording(void)
{
    return geardox->GetAudio()->IsVgmRecording();
}

bool emu_start_video_recording(const char* file_path)
{
    if (emu_is_empty())
        return false;

    if (video_recorder_is_recording())
        emu_stop_video_recording();

    GDOX_Runtime_Info runtime;
    emu_get_runtime(runtime);

    int width = 0;
    int height = 0;
    video_recording_output = ogl_renderer_get_output_size(&width, &height) && (width > 1) && (height > 1);
    emu_video_recording_frame_pending = false;

    if (video_recording_output)
    {
        width &= ~1;
        height &= ~1;
    }
    else
        get_video_recording_size(runtime, &width, &height);

    if (!video_recorder_start(file_path, width, height, emu_get_frame_rate(), GDOX_AUDIO_SAMPLE_RATE,
        (Video_Recorder_Quality)config_video.recording_quality))
        return false;

    Log("Video recording started: %s (%dx%d%s)", file_path, width, height, video_recording_output ? ", shader output" : "");
    return true;
}

void emu_stop_video_recording(void)
{
    if (video_recorder_is_recording())
    {
        video_recorder_stop();
        Log("Video recording stopped");
    }
}

bool emu_is_video_recording(void)
{
    return video_recorder_is_recording();
}

static void get_video_recording_size(const GDOX_Runtime_Info& runtime, int* width, int* height)
{
    int selected_ratio = config_debug.debug ? config_debug.ratio : config_video.ratio;
    float ratio = 0.0f;

    if (config_video.recording_ratio > 0)
        selected_ratio = config_video.recording_ratio - 1;

    switch (selected_ratio)
    {
        case 1:
            ratio = 4.0f / 3.0f;
            break;
        case 2:
            ratio = 16.0f / 9.0f;
            break;
        case 3:
            ratio = 16.0f / 10.0f;
            break;
        case 4:
            ratio = 6.0f / 5.0f;
            break;
        default:
            ratio = (float)runtime.display_width / (float)runtime.display_height;
    }

    *height = runtime.display_height * config_video.recording_scale;
    *width = (int)roundf((float)*height * ratio);
    *width += *width & 1;
    *height += *height & 1;
}

bool emu_load_bios(const char* path)
{
    if (!IsValidPointer(geardox))
        return false;

    sync_cmos();
    return geardox->LoadBios(path);
}

void emu_apply_firmware_folders(void)
{
    if (!IsValidPointer(geardox))
        return;

    for (int i = 0; i < GDOX_MACHINE_RELEASE_ORDER_COUNT; i++)
    {
        GDOX_Machine_Model model = k_machine_release_order[i];
        geardox->SetModelFirmwareFolder(model, config_machine.firmware_paths[model].c_str());
    }

    geardox->SearchFirmware();
}

bool emu_reload_firmware(void)
{
    if (!IsValidPointer(geardox))
        return false;

    sync_cmos();
    return geardox->ReloadFirmware();
}

void emu_mcp_set_transport(int mode, int tcp_port, const char* tcp_address)
{
    if (IsValidPointer(mcp_manager))
        mcp_manager->SetTransportMode((McpTransportMode)mode, tcp_port, tcp_address);
}

void emu_mcp_start(void)
{
    if (IsValidPointer(mcp_manager))
        mcp_manager->Start();
}

void emu_mcp_stop(void)
{
    if (IsValidPointer(mcp_manager))
        mcp_manager->Stop();
}

bool emu_mcp_is_running(void)
{
    return IsValidPointer(mcp_manager) && mcp_manager->IsRunning();
}

int emu_mcp_get_transport_mode(void)
{
    return IsValidPointer(mcp_manager) ? mcp_manager->GetTransportMode() : 0;
}

const char* emu_mcp_get_http_address(void)
{
    return IsValidPointer(mcp_manager) ? mcp_manager->GetTcpAddress() : "127.0.0.1";
}

int emu_mcp_get_http_port(void)
{
    return IsValidPointer(mcp_manager) ? mcp_manager->GetTcpPort() : 7777;
}

void emu_mcp_pump_commands(void)
{
    if (IsValidPointer(mcp_manager))
        mcp_manager->PumpCommands();
}

void emu_reset_rewind_timing(void)
{
    reset_rewind_timing();
}

static void load_media_thread_func(void)
{
#if defined(GDOX_ENABLE_PHYSICAL_CDROM)
    if (loading_request_type == Loading_Request_PhysicalCdRom)
        loading_result = geardox->LoadPhysicalCdRom(loading_file_path);
    else
#endif
        loading_result = geardox->LoadMedia(loading_file_path);

    loading_state.store(Loading_State_Finished);
}

static void reset_buffers(void)
{
    size_t frame_buffer_size = (size_t)GDOX_MAX_FRAME_BUFFER_WIDTH * GDOX_MAX_FRAME_BUFFER_HEIGHT * 4;
    memset(emu_frame_buffer, 0, frame_buffer_size);
    memset(audio_buffer, 0, GDOX_AUDIO_BUFFER_SIZE * sizeof(s16));
    emu_frame_stale = false;
}

const u8* emu_get_rgba_frame(int width, int height)
{
    if (!IsValidPointer(rgba_frame_buffer))
        rgba_frame_buffer = new (std::nothrow) u8[(size_t)GDOX_MAX_FRAME_BUFFER_WIDTH * GDOX_MAX_FRAME_BUFFER_HEIGHT * 4];

    if (!IsValidPointer(rgba_frame_buffer))
        return NULL;

    const u32* source = (const u32*)emu_frame_buffer;
    u8* destination = rgba_frame_buffer;
    int pixels = width * height;

    for (int i = 0; i < pixels; i++, destination += 4)
    {
        u32 color = source[i];
        destination[0] = (u8)(color >> 16);
        destination[1] = (u8)(color >> 8);
        destination[2] = (u8)color;
        destination[3] = 0xFF;
    }

    return rgba_frame_buffer;
}

static void reset_run_state(void)
{
    emu_debug_command = Debug_Command_None;
    emu_debug_pc_changed = true;
    emu_frame_counter = 0;
    emu_machine_generation++;
    reset_buffers();
    emu_audio_reset();
    rewind_reset();
}

static const char* get_configurated_dir(int location, const char* path)
{
    switch ((Directory_Location)location)
    {
        default:
        case Directory_Location_Default:
            return config_root_path;
        case Directory_Location_ROM:
#if defined(GDOX_ENABLE_PHYSICAL_CDROM)
            if (geardox->GetMedia()->IsPhysicalCdRom())
                return config_root_path;
#endif
            return NULL;
        case Directory_Location_Custom:
            return path;
    }
}

static bool unload_media(void)
{
    if (loading_state.load() != Loading_State_None)
        return false;

    if (!geardox->GetMedia()->IsReady())
        return false;

    gui_debug_trace_logger_reset("media_change");
    emu_debug_command = Debug_Command_None;
    reset_buffers();
    emu_audio_reset();
    geardox->EjectMedia();
    rewind_reset();
    update_savestates_data();
    return true;
}

static void restore_state_frame(const char* path, int index)
{
    GDOX_Runtime_Info runtime;
    emu_get_runtime(runtime);
    GDOX_SaveState_Screenshot screenshot;
    screenshot.width = 0;
    screenshot.height = 0;
    screenshot.size = GDOX_MAX_FRAME_BUFFER_WIDTH * GDOX_MAX_FRAME_BUFFER_HEIGHT * 4;
    screenshot.data = new (std::nothrow) u8[screenshot.size];

    emu_frame_stale = !IsValidPointer(screenshot.data) || !geardox->GetSaveStateScreenshot(index, path, &screenshot) ||
        ((int)screenshot.width != runtime.screen_width) || ((int)screenshot.height != runtime.screen_height);

    if (!emu_frame_stale)
        memcpy(emu_frame_buffer, screenshot.data, screenshot.size);

    SafeDeleteArray(screenshot.data);
}

static bool get_floppy_state_path(int index, char* path, size_t path_size)
{
    if (geardox->GetMedia()->IsReady())
        return false;

    const char* content = emu_floppy_get_content_path();
    char directory[GDOX_MAX_PATH];

    if (content[0] == '\0')
        content = emu_hard_disk_get_content_path();

    char name[GDOX_MAX_PATH];
    char file_name[GDOX_MAX_PATH];

    switch ((Directory_Location)config_emulator.savestates_dir_option)
    {
        case Directory_Location_ROM:
            if (content[0] != '\0')
                get_directory(content, directory, sizeof(directory));
            else
                strncpy_fit(directory, config_root_path, sizeof(directory));
            break;
        case Directory_Location_Custom:
            strncpy_fit(directory, config_emulator.savestates_path.c_str(), sizeof(directory));
            break;
        default:
            strncpy_fit(directory, config_root_path, sizeof(directory));
            break;
    }

    if (content[0] != '\0')
        get_filename_without_extension(content, name, sizeof(name));
    else
        strncpy_fit(name, "PC", sizeof(name));

    snprintf(file_name, sizeof(file_name), "%s.state%d", name, index);
    return join_path(directory, file_name, path, path_size);
}

static bool get_cmos_path(int model, char* path, size_t path_size)
{
    const char* directory = get_configurated_dir(config_emulator.savefiles_dir_option, config_emulator.savefiles_path.c_str());
    char file_name[GDOX_MAX_PATH];

    if (!IsValidPointer(directory))
        directory = config_root_path;

    snprintf(file_name, sizeof(file_name), "%s.geardox.cmos", k_machine_profiles[model].key);
    return join_path(directory, file_name, path, path_size);
}

static void load_cmos(int model)
{
    MC146818* rtc = geardox->GetRTC();
    char path[GDOX_MAX_PATH];
    std::ifstream file;

    rtc->ResetCMOS();
    cmos_model = model;

    if (get_cmos_path(model, path, sizeof(path)))
        open_ifstream_utf8(file, path, std::ios::in | std::ios::binary);

    if (file.is_open())
    {
        u8 data[GDOX_CMOS_FILE_SIZE + 1];
        file.read(reinterpret_cast<char*>(data), sizeof(data));

        if (!rtc->LoadCMOSFile(data, (size_t)file.gcount()))
            Error("Invalid CMOS file: %s", path);
        else
            Log("CMOS loaded from %s", path);
    }

    memcpy(cmos_saved, rtc->GetCMOS(), GDOX_CMOS_SIZE);
}

static void save_cmos(void)
{
    if (cmos_model < 0)
        return;

    const u8* cmos = geardox->GetRTC()->GetCMOS();
    char path[GDOX_MAX_PATH];

    if (memcmp(cmos, cmos_saved, GDOX_CMOS_SIZE) == 0 || !get_cmos_path(cmos_model, path, sizeof(path)))
        return;

    u8 data[GDOX_CMOS_FILE_SIZE];
    geardox->GetRTC()->SaveCMOSFile(data);
    std::string temporary(path);
    temporary += ".tmp";
    std::ofstream file;
    open_ofstream_utf8(file, temporary.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(data), GDOX_CMOS_FILE_SIZE);
    file.close();

    if (!file.good() || !SDL_RenamePath(temporary.c_str(), path))
    {
        SDL_RemovePath(temporary.c_str());
        Error("Unable to save CMOS: %s", path);
        return;
    }

    memcpy(cmos_saved, cmos, GDOX_CMOS_SIZE);
    Log("CMOS saved to %s", path);
}

static void notify_machine(void)
{
    const GDOX_Machine_Config& config = geardox->GetMachineConfig();
    const GDOX_Machine_Report& report = geardox->GetMachineReport();
    bool model = (report.model == GDOX_MACHINE_SOURCE_DATABASE) && (config.model != GDOX_MACHINE_386DX_33);
    bool cpu = report.cpu_clock == GDOX_MACHINE_SOURCE_DATABASE;
    bool ram = report.ram == GDOX_MACHINE_SOURCE_DATABASE;
    bool fpu = report.fpu == GDOX_MACHINE_SOURCE_DATABASE;
    GDOX_Floppy_Notice floppy;
    bool floppy_notice = geardox->GetFloppyNotice(floppy);
    const char* title = report.title;
    const char* origin = IsValidPointer(report.title) ? "game database" : "Auto";
    char value[48];
    std::string ports;
    std::string port_warnings;

    for (int i = 0; i < GDOX_MAX_GAMEPADS; i++)
    {
        GDOX_Controller_Type type = emu_get_pad_type((GDOX_Controllers)i);
        GDOX_Controller_Type wanted = GDOX_CONTROLLER_NONE;
        GDOX_Controller_Type plain = (i == GDOX_CONTROLLER_2) ? GDOX_CONTROLLER_MOUSE : GDOX_CONTROLLER_GAMEPAD;

        if ((pad_sources[i] == Emu_Pad_Source_Database) && (type != plain))
        {
            snprintf(value, sizeof(value), "%sPort %d %s", ports.empty() ? "" : ", ", i + 1, emu_get_pad_type_name(type));
            ports += value;
        }

        if (emu_get_pad_mismatch((GDOX_Controllers)i, &wanted))
        {
            snprintf(value, sizeof(value), "%sPort %d should be %s", port_warnings.empty() ? "" : "\n", i + 1,
                emu_get_pad_type_name(wanted));
            port_warnings += value;
        }
    }

    if (!IsValidPointer(report.title) || (!model && !cpu && !ram && !fpu && (report.warning_count == 0) &&
        !floppy_notice && ports.empty() && port_warnings.empty()))
        return;

    char message[256];
    std::string choices;
    std::string detail;

    if (model)
        choices = k_machine_profiles[config.model].name;

    if (cpu)
    {
        snprintf(value, sizeof(value), "%s%u MHz", choices.empty() ? "" : ", ", config.cpu_clock_rate / 1000000);
        choices += value;
    }

    if (ram)
    {
        snprintf(value, sizeof(value), "%s%u MB", choices.empty() ? "" : ", ", config.ram_size / (1024 * 1024));
        choices += value;
    }

    if (fpu)
    {
        snprintf(value, sizeof(value), "%s%s", choices.empty() ? "" : ", ", get_machine_fpu_name(config.cpu));
        choices += value;
    }

    if (!ports.empty())
        choices += (choices.empty() ? "" : ", ") + ports;

    if (choices.empty())
        snprintf(message, sizeof(message), "%s (%s)", title, origin);
    else
        snprintf(message, sizeof(message), "%s: %s (%s)", title, choices.c_str(), origin);

    Log("%s", message);

    for (int i = 0; i < report.warning_count; i++)
    {
        Log("%s", report.warnings[i]);
        detail += (i > 0 ? "\n" : "");
        detail += report.warnings[i];
    }

    for (int i = 0; i < floppy.line_count; i++)
    {
        Log("%s", floppy.lines[i]);
        detail += (detail.empty() ? "" : "\n");
        detail += floppy.lines[i];
    }

    if (!port_warnings.empty())
    {
        Log("%s", port_warnings.c_str());
        detail += (detail.empty() ? "" : "\n");
        detail += port_warnings;
    }

    bool warning = (report.warning_count > 0) || floppy.warning || !port_warnings.empty();
    gui_notify(warning ? gui_NotificationWarning : gui_NotificationInfo, ICON_MD_MEMORY, message,
        detail.empty() ? NULL : detail.c_str(), "machine");
}

static void sync_cmos(void)
{
    int model = geardox->GetPendingMachineConfig().model;

    save_cmos();

    if (model != cmos_model)
        load_cmos(model);
}

#if defined(GDOX_ENABLE_PHYSICAL_CDROM)
static void stop_physical_cdrom_after_error(void)
{
    char device_id[256];
    strncpy_fit(device_id, geardox->GetMedia()->GetPhysicalCdRomDeviceId(), sizeof(device_id));

    if (!unload_media())
        return;

    Error("Physical CD-ROM media error on %s, disc ejected", device_id);
}
#endif

static void reset_rewind_timing(void)
{
    rewind_last_counter = 0;
    rewind_pop_accumulator = 0.0;
}

static int get_rewind_pop_budget(void)
{
    Uint64 now = SDL_GetPerformanceCounter();

    if (rewind_last_counter == 0)
    {
        rewind_last_counter = now;
        return 0;
    }

    double elapsed = (double)(now - rewind_last_counter) / (double)SDL_GetPerformanceFrequency();
    rewind_last_counter = now;

    if (elapsed < 0.0)
        elapsed = 0.0;
    else if (elapsed > 0.25)
        elapsed = 0.25;

    int frames_per_snapshot = rewind_get_frames_per_snapshot();

    if (frames_per_snapshot < 1)
        frames_per_snapshot = 1;

    double snapshots_per_second = (60.0 * (double)config_rewind.speed) / (double)frames_per_snapshot;
    rewind_pop_accumulator += elapsed * snapshots_per_second;

    int to_pop = (int)rewind_pop_accumulator;

    if (to_pop > 0)
        rewind_pop_accumulator -= (double)to_pop;

    return to_pop;
}
