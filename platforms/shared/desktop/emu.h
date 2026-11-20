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

#ifndef EMU_H
#define EMU_H

#include "geardox.h"

#ifdef EMU_IMPORT
    #define EXTERN
#else
    #define EXTERN extern
#endif

#define EMU_DEBUG_FRAMEBUFFER_WIDTH 1024
#define EMU_DEBUG_FRAMEBUFFER_HEIGHT 1024

enum Emu_Debug_Buffer
{
    Emu_Debug_Buffer_Plane0 = 0,
    Emu_Debug_Buffer_Plane1,
    Emu_Debug_Buffer_Plane2,
    Emu_Debug_Buffer_Plane3,
    Emu_Debug_Buffer_Planar,
    Emu_Debug_Buffer_256,
    Emu_Debug_Buffer_Text,
    Emu_Debug_Buffer_Linear,
    Emu_Debug_Buffer_Count
};

enum Emu_Debug_Linear_Format
{
    Emu_Debug_Linear_Auto = 0,
    Emu_Debug_Linear_8,
    Emu_Debug_Linear_15,
    Emu_Debug_Linear_16,
    Emu_Debug_Linear_24,
    Emu_Debug_Linear_Count
};

struct Emu_Debug_Buffer_Info
{
    int width;
    int height;
    int pitch;
    int cell_width;
    int cell_height;
    int window_x;
    int window_y;
    int window_width;
    int window_height;
    int format;
    int bytes_per_pixel;
    u32 start;
};

enum Debug_Command
{
    Debug_Command_Continue,
    Debug_Command_Step,
    Debug_Command_StepOver,
    Debug_Command_StepFrame,
    Debug_Command_None
};

enum Directory_Location
{
    Directory_Location_Default = 0,
    Directory_Location_ROM = 1,
    Directory_Location_Custom = 2
};

EXTERN u8* emu_frame_buffer;
EXTERN GDOX_SaveState_Header emu_savestates[5];
EXTERN GDOX_SaveState_Screenshot emu_savestates_screenshots[5];
EXTERN u32 emu_savestates_generation;
EXTERN Debug_Command emu_debug_command;
EXTERN bool emu_debug_pc_changed;
EXTERN int emu_debug_step_frames_pending;
EXTERN u8* emu_debug_framebuffer;
EXTERN Emu_Debug_Buffer_Info emu_debug_framebuffer_info;
EXTERN bool emu_debug_disable_breakpoints;
EXTERN u64 emu_frame_counter;
EXTERN u64 emu_video_frame_counter;
EXTERN bool emu_frame_stale;
EXTERN u32 emu_machine_generation;
EXTERN bool emu_video_recording_frame_pending;
EXTERN u64 emu_debug_breakpoint_stops;
EXTERN bool emu_audio_sync;
EXTERN bool emu_audio_rate_control;
EXTERN float emu_audio_ratio;

EXTERN bool emu_init(void);
EXTERN void emu_destroy(void);
EXTERN void emu_update(void);
EXTERN void emu_load_media_async(const char* file_path);
EXTERN void emu_load_hard_disk_source_async(const char* path, bool rebuild);
EXTERN void emu_load_game_async(void);
EXTERN void emu_load_physical_cdrom_async(const char* device_id);
EXTERN bool emu_is_media_loading(void);
EXTERN bool emu_finish_media_loading(void);
EXTERN void emu_set_joystick_state(const GDOX_Joystick_State& state);
EXTERN bool emu_key_pressed(GDOX_Keys key);
EXTERN void emu_key_released(GDOX_Keys key);
EXTERN void emu_host_key_pressed(int usage);
EXTERN void emu_host_key_released(int usage);
EXTERN void emu_release_all_host_keys(void);
EXTERN void emu_host_hotkey_used(void);
EXTERN int emu_get_built_in_key(int usage);
EXTERN void emu_gamepad_update(int pad, const GDOX_Gamepad_State& state, const GDOX_Gamepad_Mapping& mapping);
EXTERN bool emu_has_pointer(void);
EXTERN bool emu_key_mapper_is_open(void);
EXTERN void emu_key_mapper_toggle(void);
EXTERN void emu_key_mapper_set_pointer(int x, int y);
EXTERN void emu_key_mapper_set_buttons(u8 buttons);
EXTERN bool emu_key_mapping_is_enabled(void);
EXTERN void emu_key_mapping_toggle(void);
EXTERN const char* emu_key_map_get_path(void);
EXTERN void emu_key_map_save(void);
EXTERN void emu_set_mouse_delta(int x, int y);
EXTERN void emu_set_mouse_buttons(u8 buttons);
EXTERN void emu_clear_mouse(void);
EXTERN void emu_pause(void);
EXTERN void emu_resume(void);
EXTERN bool emu_is_paused(void);
EXTERN bool emu_is_debug_idle(void);
EXTERN bool emu_is_empty(void);
EXTERN bool emu_is_powered_on(void);
EXTERN bool emu_power_on(void);
EXTERN void emu_power_off(void);
EXTERN void emu_reset(void);
EXTERN bool emu_eject_media(void);
EXTERN bool emu_cdrom_playlist_load(const char* path, bool* floppies);
EXTERN void emu_cdrom_playlist_clear(void);
EXTERN bool emu_cdrom_playlist_select(const char* path);
EXTERN int emu_cdrom_playlist_get_count(void);
EXTERN int emu_cdrom_playlist_get_index(void);
EXTERN const char* emu_cdrom_playlist_get_path(int index);
EXTERN const char* emu_cdrom_playlist_get_name(int index);
EXTERN void emu_set_preload_cdrom(bool enabled);
EXTERN void emu_set_dynarec(bool enabled);
EXTERN void emu_set_turbo(bool turbo);
EXTERN void emu_apply_bios_settings(void);
EXTERN bool emu_is_dynarec_enabled(void);
EXTERN void emu_set_native_resolution(bool enabled);
EXTERN void emu_apply_machine_settings(void);

EXTERN void emu_audio_mute(bool mute);
EXTERN void emu_audio_update_levels(void);
EXTERN float emu_audio_get_volume(int level);
EXTERN bool emu_audio_get_mute(int level);
EXTERN void emu_audio_reset(void);
EXTERN void emu_audio_restart(void);
EXTERN bool emu_is_audio_open(void);

EXTERN bool emu_save_state_slot(int index);
EXTERN bool emu_load_state_slot(int index);
EXTERN bool emu_save_state_file(const char* file_path);
EXTERN bool emu_load_state_file(const char* file_path);
EXTERN void update_savestates_data(void);

EXTERN void emu_get_runtime(GDOX_Runtime_Info& runtime);
EXTERN const u8* emu_get_rgba_frame(int width, int height);
EXTERN double emu_get_frame_rate(void);
EXTERN void emu_get_info(char* info, int buffer_size);
EXTERN GeardoxCore* emu_get_core(void);

EXTERN void emu_debug_step_over(void);
EXTERN void emu_debug_step_into(void);
EXTERN bool emu_debug_step_out(void);
EXTERN void emu_debug_step_frame(void);
EXTERN void emu_debug_step_frames(int frames);
EXTERN void emu_debug_break(void);
EXTERN void emu_debug_continue(void);
EXTERN void emu_debug_state_restored(void);
EXTERN void emu_debug_update(void);
EXTERN void emu_debug_decode_buffer(int buffer, int pitch, int format, int start, u8* output, Emu_Debug_Buffer_Info& info);
EXTERN int emu_get_debug_buffer_png(int buffer, int pitch, int format, int start, unsigned char** out_buffer,
    Emu_Debug_Buffer_Info& info);

EXTERN void emu_update_joystick_type(void);
EXTERN int emu_get_joystick_type(void);
EXTERN void emu_update_cga_output(void);
EXTERN bool emu_save_screenshot(const char* file_path);
EXTERN int emu_get_screenshot_png(unsigned char** out_buffer);
EXTERN bool emu_start_vgm_recording(const char* file_path);
EXTERN void emu_stop_vgm_recording(void);
EXTERN bool emu_is_vgm_recording(void);
EXTERN bool emu_start_video_recording(const char* file_path);
EXTERN void emu_stop_video_recording(void);
EXTERN bool emu_is_video_recording(void);

EXTERN void emu_mcp_set_transport(int mode, int tcp_port, const char* tcp_address);
EXTERN void emu_mcp_start(void);
EXTERN void emu_mcp_stop(void);
EXTERN bool emu_mcp_is_running(void);
EXTERN int emu_mcp_get_transport_mode(void);
EXTERN const char* emu_mcp_get_http_address(void);
EXTERN int emu_mcp_get_http_port(void);
EXTERN void emu_mcp_pump_commands(void);
EXTERN bool emu_gdb_start(int port, bool wait);
EXTERN void emu_gdb_stop(void);
EXTERN bool emu_gdb_is_running(void);
EXTERN bool emu_gdb_is_connected(void);
EXTERN int emu_gdb_get_port(void);
EXTERN void emu_reset_rewind_timing(void);

#undef EMU_IMPORT
#undef EXTERN
#endif /* EMU_H */
