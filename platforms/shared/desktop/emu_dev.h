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

#ifndef EMU_DEV_H
#define EMU_DEV_H

#include <string>
#include "geardox.h"

struct Dev_Session_Params
{
    bool active = false;
    bool debug_target = false;
    std::string run_command;
    bool stop_at_entry = false;
    bool exit_on_exit = false;
    int timeout = 0;
    bool boot_max_speed = false;
    bool max_speed = false;
    bool speed_set = false;
    bool e9 = false;
    bool magic_breakpoint = false;
    bool no_write_back = false;
    bool gdb = false;
    int gdb_port = 0;
    bool gdb_wait = false;
    bool headless = false;
};

struct Dev_Program_Info
{
    int state;
    u8 load_flags;
    std::string dos_path;
    std::string host_path;
    std::string symbol_path;
    int symbol_count;
    u16 psp;
    u16 entry_cs;
    u16 entry_ip;
    u16 stack_ss;
    u16 stack_sp;
    u16 load_segment;
    bool based;
    u32 base;
    u16 code_selector;
    u16 data_selector;
    int code_size;
    int exit_code;
    int exit_type;
    u32 runs;
};

typedef void (*Dev_Event_Listener)(void* context, u8 type);

#ifdef EMU_DEV_IMPORT
    #define EXTERN
#else
    #define EXTERN extern
#endif

EXTERN void emu_dev_init(void);
EXTERN void emu_dev_destroy(void);
EXTERN void emu_dev_start_session(const Dev_Session_Params& params);
EXTERN bool emu_dev_is_session(void);
EXTERN const Dev_Session_Params& emu_dev_get_session(void);
EXTERN void emu_dev_update(void);
EXTERN void emu_dev_reset_output(void);
EXTERN void emu_dev_prepare_run(const char* exe, const char* args);
EXTERN bool emu_dev_run_program(const char* command, bool stop_at_entry);
EXTERN bool emu_dev_relaunch(void);
EXTERN bool emu_dev_quit_requested(void);
EXTERN int emu_dev_get_exit_code(void);
EXTERN bool emu_dev_wants_max_speed(void);
EXTERN void emu_dev_get_program(Dev_Program_Info* info);
EXTERN u64 emu_dev_get_output_count(void);
EXTERN std::string emu_dev_get_output(u64 since, u32 max, u64* next);
EXTERN int emu_dev_load_program_symbols(const char* path);
EXTERN bool emu_dev_dos_to_host(const char* dos_path, std::string* host_path);
EXTERN void emu_dev_set_listener(void* context, Dev_Event_Listener listener);
EXTERN void emu_dev_split_output(void);

#undef EMU_DEV_IMPORT
#undef EXTERN
#endif /* EMU_DEV_H */
