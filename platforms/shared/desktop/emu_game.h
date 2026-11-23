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

#ifndef EMU_GAME_H
#define EMU_GAME_H

#include <string>
#include <vector>
#include "geardox.h"
#include "config.h"
#include "drive/game_disk.h"

#ifdef EMU_GAME_IMPORT
    #define EXTERN
#else
    #define EXTERN extern
#endif

enum Emu_Game_Source
{
    Emu_Game_Source_User = 0,
    Emu_Game_Source_Descriptor,
    Emu_Game_Source_Change
};

EXTERN bool emu_game_is_content(const char* path);
EXTERN bool emu_game_find(const char* path, std::vector<GameDisk_Found>* found);
EXTERN bool emu_game_open(const GameDisk_Found& found, int target, int program = -1);
EXTERN void emu_game_prepare_thread(void);
EXTERN bool emu_game_start(void);
EXTERN bool emu_game_close(void);
EXTERN bool emu_game_is_open(void);
EXTERN const char* emu_game_get_name(void);
EXTERN const char* emu_game_get_error(void);
EXTERN void emu_game_set_target(int target);
EXTERN int emu_game_get_target(void);
EXTERN bool emu_game_update(void);
EXTERN config_Machine* emu_game_get_machine(void);
EXTERN const config_Machine* emu_game_get_base_machine(void);
EXTERN void emu_game_apply_machine(void);
EXTERN void emu_game_config_to_settings(const config_Machine& machine, GDOX_Machine_Settings* settings);
EXTERN int emu_game_get_source(const char* section, const char* key);
EXTERN bool emu_game_is_from_database(void);
EXTERN int emu_game_get_joystick(void);
EXTERN int emu_game_get_base_joystick(void);
EXTERN int emu_game_get_cga_output(void);
EXTERN int emu_game_get_base_cga_output(void);
EXTERN void emu_game_set_cga_output(int output);
EXTERN int emu_game_get_midi(void);
EXTERN int emu_game_get_base_midi(void);
EXTERN void emu_game_set_midi(int synth);
EXTERN int emu_game_get_midi_out(void);
EXTERN int emu_game_get_base_midi_out(void);
EXTERN void emu_game_set_midi_out(int out, const char* port);
EXTERN const char* emu_game_get_midi_port(void);
EXTERN const char* emu_game_get_base_midi_port(void);
EXTERN int emu_game_get_mt32_model(void);
EXTERN int emu_game_get_base_mt32_model(void);
EXTERN void emu_game_set_mt32_model(int model);
EXTERN int emu_game_get_volume(int level);
EXTERN int emu_game_get_base_volume(int level);
EXTERN int emu_game_get_mute(int level);
EXTERN int emu_game_get_base_mute(int level);
EXTERN void emu_game_set_level(int level, int volume, int mute, bool save);
EXTERN void emu_game_set_joystick(int joystick);
EXTERN const GDOX_Gamepad_Game* emu_game_get_gamepad(int pad);
EXTERN const GDOX_Gamepad_Game* emu_game_get_base_gamepad(int pad);
EXTERN void emu_game_set_gamepad(int pad, const GDOX_Gamepad_Game& gamepad);
EXTERN bool emu_game_has_overrides(void);
EXTERN bool emu_game_reset_settings(void);
EXTERN bool emu_game_save_descriptor(const char* path);
EXTERN bool emu_game_retry_write_back(void);
EXTERN bool emu_game_is_write_back_pending(void);
EXTERN const GameDisk_Result& emu_game_get_last_write_back(void);
EXTERN const char* emu_game_get_last_write_back_name(void);
EXTERN const char* emu_game_get_saves_path(void);
EXTERN bool emu_game_has_setup(void);
EXTERN int emu_game_get_program(void);
EXTERN bool emu_game_set_program(int program);
EXTERN bool emu_game_needs_restart(void);
EXTERN GameDisk* emu_game_get_disk(void);
EXTERN std::string emu_game_get_key_map_path(void);
EXTERN const char* emu_game_get_keys(void);
EXTERN int emu_game_get_system(void);
EXTERN int emu_game_get_base_system(void);
EXTERN bool emu_game_set_system(int system);
EXTERN bool emu_game_get_found(GameDisk_Found* found);
EXTERN void emu_game_list_programs(const char* folder, std::vector<std::string>* programs);
EXTERN bool emu_game_new_descriptor(const char* folder, const char* program, std::string* path);

#undef EMU_GAME_IMPORT
#undef EXTERN
#endif /* EMU_GAME_H */
