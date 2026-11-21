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

#include <SDL3/SDL.h>
#include <stdio.h>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif
#include <vector>
#include <string>

#define EMU_DEV_IMPORT
#include "emu_dev.h"

#include "emu.h"
#include "emu_game.h"
#include "config.h"
#include "display.h"
#include "gui.h"
#include "debug/gui_debug_disassembler.h"
#include "media/media_file.h"

static Dev_Session_Params session;
static bool session_started = false;
static bool saved_debug = false;
static int saved_write_back = 0;
static bool max_speed_applied = false;
static bool saved_ffwd = false;
static int saved_ffwd_speed = 0;
static bool saved_audio_sync = true;
static bool applied = false;
static bool applied_console = false;
static bool applied_control = false;
static bool applied_magic = false;
static u32 event_mark = 0;
static u64 output_mark = 0;
static bool quit = false;
static int exit_code = 0;
static Uint64 start_ticks = 0;
static Dev_Program_Info program;
static void* listener_context = NULL;
static Dev_Event_Listener listener = NULL;
static std::string pending_command;
static bool pending_stop = false;
static bool booting = false;
static FILE* program_output = NULL;
static bool auto_continue = false;
static u32 auto_continue_generation = 0;

static bool launch(void);
static void apply_settings(void);
static void process_events(void);
static void handle_event(const DevPort::DevPort_State* state, u8 type);
static void deliver_output(void);
static void update_speed(void);
static void update_continue(void);
static void request_continue(void);
static void set_max_speed(bool enabled);
static void load_symbols(void);
static std::string find_in_folder(const std::string& directory, const std::string& name);

void emu_dev_init(void)
{
    program = Dev_Program_Info();
    program.state = DEV_PORT_PROGRAM_NONE;
    program.load_flags = 0;
    program.symbol_count = 0;
    program.runs = 0;
    applied = false;
    event_mark = 0;
    output_mark = 0;
    quit = false;
    exit_code = 0;
}

void emu_dev_destroy(void)
{
    set_max_speed(false);

    if (!session_started)
        return;

    config_debug.debug = saved_debug;
    config_game.write_back = saved_write_back;
    session_started = false;
}

void emu_dev_start_session(const Dev_Session_Params& params)
{
    session = params;

    if (!session.active)
        return;

    session_started = true;
    saved_debug = config_debug.debug;
    saved_write_back = config_game.write_back;
    config_debug.debug = true;

    if (session.no_write_back)
        config_game.write_back = 1;

    pending_command = session.run_command;
    pending_stop = session.stop_at_entry;
    start_ticks = SDL_GetTicks();
    booting = true;
    request_continue();
    apply_settings();
}

bool emu_dev_is_session(void)
{
    return session.active;
}

const Dev_Session_Params& emu_dev_get_session(void)
{
    return session;
}

void emu_dev_update(void)
{
    GeardoxCore* core = emu_get_core();

    if (!IsValidPointer(core))
        return;

    apply_settings();
    update_continue();
    process_events();
    deliver_output();
    update_speed();

    if (session.active && (session.timeout > 0) && !quit &&
        (SDL_GetTicks() - start_ticks > (Uint64)session.timeout * 1000))
    {
        fprintf(stderr, "geardox: the program did not finish in %d seconds\n", session.timeout);
        quit = true;
        exit_code = 124;
    }
}

void emu_dev_reset_output(void)
{
    GeardoxCore* core = emu_get_core();

    if (!IsValidPointer(core))
        return;

    event_mark = core->GetDevPort()->GetState()->sequence;
    output_mark = core->GetDevPort()->GetConsole()->GetWrittenCount();
}

void emu_dev_prepare_run(const char* exe, const char* args)
{
    std::string command = pending_command;

    if (command.empty())
    {
        command = IsValidPointer(exe) ? exe : "";

        if (IsValidPointer(args) && (args[0] != 0))
            command += std::string(" ") + args;
    }

    if (pending_stop)
        command = "/S " + command;

    emu_get_core()->SetRunCommand(command.c_str());
}

bool emu_dev_run_program(const char* command, bool stop_at_entry)
{
    if (IsValidPointer(command))
        pending_command = command;

    pending_stop = stop_at_entry;
    return launch();
}

bool emu_dev_relaunch(void)
{
    pending_stop = session.stop_at_entry;
    return launch();
}

static bool launch(void)
{
    GameDisk_Found found;

    if (!emu_game_is_open() || !emu_game_get_found(&found))
        return false;

    if (!config_debug.debug)
        config_debug.debug = true;

    booting = true;
    request_continue();
    return gui_start_game(found, GDOX_GAME_TARGET_DEBUG);
}

bool emu_dev_quit_requested(void)
{
    return quit;
}

int emu_dev_get_exit_code(void)
{
    return exit_code;
}

bool emu_dev_wants_max_speed(void)
{
    return max_speed_applied;
}

void emu_dev_get_program(Dev_Program_Info* info)
{
    *info = program;
}

u64 emu_dev_get_output_count(void)
{
    GeardoxCore* core = emu_get_core();
    return IsValidPointer(core) ? core->GetDevPort()->GetConsole()->GetWrittenCount() : 0;
}

std::string emu_dev_get_output(u64 since, u32 max, u64* next)
{
    GeardoxCore* core = emu_get_core();
    std::string text;

    if (!IsValidPointer(core))
    {
        *next = since;
        return text;
    }

    DebugConsole* console = core->GetDevPort()->GetConsole();
    u64 written = console->GetWrittenCount();
    u64 oldest = written - console->GetLength();
    u64 first = since < oldest ? oldest : since;
    u32 count = first < written ? (u32)MIN(written - first, (u64)max) : 0;
    std::vector<char> buffer(count + 1);
    u32 copied = console->CopySince(first, &buffer[0], count + 1);
    text.assign(&buffer[0], copied);
    *next = first + copied;
    return text;
}

int emu_dev_load_program_symbols(const char* path)
{
    gui_debug_clear_program_symbols();
    program.symbol_path = IsValidPointer(path) ? path : "";
    program.symbol_count = 0;

    if (program.symbol_path.empty())
        return 0;

    int count = gui_debug_load_program_symbols(program.symbol_path.c_str(), program.load_segment, program.base);
    program.symbol_count = count > 0 ? count : 0;
    return count;
}

bool emu_dev_dos_to_host(const char* dos_path, std::string* host_path)
{
    GameDisk* disk = emu_game_get_disk();

    if (!emu_game_is_open() || !IsValidPointer(disk) || !IsValidPointer(dos_path))
        return false;

    std::string folder = std::string("C:\\") + disk->GetDescriptor().GetInfo().folder;
    std::string path = dos_path;

    while ((folder.size() > 3) && (folder[folder.size() - 1] == '\\'))
        folder.erase(folder.size() - 1);

    if (!starts_with_no_case(path, folder + "\\"))
        return false;

    std::string current = disk->GetSourcePath();
    std::string rest = path.substr(folder.size() + 1);

    while (!rest.empty())
    {
        size_t slash = rest.find('\\');
        std::string part = slash == std::string::npos ? rest : rest.substr(0, slash);
        std::string found = find_in_folder(current, part);

        if (found.empty())
            return false;

        current = path_join(current, found);
        rest = slash == std::string::npos ? std::string() : rest.substr(slash + 1);
    }

    *host_path = current;
    return true;
}

void emu_dev_split_output(void)
{
    fflush(stdout);
#if defined(_WIN32)
    int out = _dup(_fileno(stdout));

    if ((out < 0) || (_dup2(_fileno(stderr), _fileno(stdout)) < 0))
        return;

    program_output = _fdopen(out, "w");
#else
    int out = dup(STDOUT_FILENO);

    if ((out < 0) || (dup2(STDERR_FILENO, STDOUT_FILENO) < 0))
        return;

    program_output = fdopen(out, "w");
#endif
}

void emu_dev_set_listener(void* context, Dev_Event_Listener event_listener)
{
    listener_context = context;
    listener = event_listener;
}

static void apply_settings(void)
{
    GeardoxCore* core = emu_get_core();
    bool debug = config_debug.debug;
    bool console = debug && (config_debug.dev_e9_console || session.e9);
    bool control = debug;
    bool magic = debug && (config_debug.dev_magic_breakpoint || session.magic_breakpoint);

    if (applied && (console == applied_console) && (control == applied_control) && (magic == applied_magic))
        return;

    core->SetDevelopment(console, control, magic);
    applied = true;
    applied_console = console;
    applied_control = control;
    applied_magic = magic;
}

static void process_events(void)
{
    const DevPort::DevPort_State* state = emu_get_core()->GetDevPort()->GetState();

    if (state->sequence < event_mark)
        return;

    u32 first = state->sequence - event_mark > DEV_PORT_EVENT_COUNT ? state->sequence - DEV_PORT_EVENT_COUNT + 1 :
        event_mark + 1;

    for (u32 sequence = first; sequence <= state->sequence; sequence++)
    {
        const DevPort::DevPort_Event& event = state->events[sequence % DEV_PORT_EVENT_COUNT];

        if (event.sequence == sequence)
            handle_event(state, event.type);
    }

    event_mark = state->sequence;
}

static void handle_event(const DevPort::DevPort_State* state, u8 type)
{
    switch (type)
    {
        case DEV_PORT_EVENT_LOADED:
            booting = false;
            program.state = DEV_PORT_PROGRAM_RUNNING;
            program.load_flags = state->load_flags;
            program.dos_path = state->path;
            program.host_path.clear();
            program.psp = state->psp;
            program.entry_cs = state->entry_cs;
            program.entry_ip = state->entry_ip;
            program.stack_ss = state->stack_ss;
            program.stack_sp = state->stack_sp;
            program.load_segment = state->entry_cs == state->psp ? state->psp : (u16)(state->psp + 0x10);
            program.based = false;
            program.base = state->base;
            program.code_selector = state->code_selector;
            program.data_selector = state->data_selector;
            program.code_size = state->code_size;
            program.exit_code = -1;
            program.exit_type = -1;
            program.runs++;
            emu_dev_dos_to_host(state->path, &program.host_path);
            load_symbols();
            Log("Program loaded: %s, PSP %04X, entry %04X:%04X", state->path, state->psp, state->entry_cs,
                state->entry_ip);
            break;
        case DEV_PORT_EVENT_BASED:
            program.based = true;
            program.base = state->base;
            program.code_selector = state->code_selector;
            program.data_selector = state->data_selector;
            program.code_size = state->code_size;

            if (!program.symbol_path.empty())
                emu_dev_load_program_symbols(program.symbol_path.c_str());

            Log("Program base: %08X, code %04X, data %04X, %d-bit", state->base, state->code_selector,
                state->data_selector, state->code_size);
            break;
        case DEV_PORT_EVENT_EXITED:
            program.state = DEV_PORT_PROGRAM_EXITED;
            program.exit_code = state->exit_code;
            program.exit_type = state->exit_type;
            gui_debug_clear_program_symbols();
            Log("Program exited: code %d, type %d", state->exit_code, state->exit_type);

            if (session.active && session.exit_on_exit && !quit)
            {
                deliver_output();
                quit = true;
                exit_code = state->exit_code;
            }
            break;
        default:
            break;
    }

    if (IsValidPointer(listener))
        listener(listener_context, type);
}

static void deliver_output(void)
{
    DebugConsole* console = emu_get_core()->GetDevPort()->GetConsole();
    u64 written = console->GetWrittenCount();

    if (written <= output_mark)
        return;

    u64 next = output_mark;
    std::string text = emu_dev_get_output(output_mark, DEBUG_CONSOLE_SIZE, &next);
    output_mark = written;

    if (session.active && !text.empty())
    {
        FILE* out = IsValidPointer(program_output) ? program_output : stdout;
        fwrite(text.data(), 1, text.size(), out);
        fflush(out);
    }
}

static void request_continue(void)
{
    auto_continue = true;
    auto_continue_generation = emu_machine_generation;
}

static void update_continue(void)
{
    if (!auto_continue || (emu_machine_generation == auto_continue_generation) || !emu_is_powered_on())
        return;

    auto_continue = false;

    if (emu_is_debug_idle())
        emu_debug_continue();
}

static void update_speed(void)
{
    set_max_speed(session.active && (session.max_speed || (session.boot_max_speed && booting)));
}

static void set_max_speed(bool enabled)
{
    if (enabled == max_speed_applied)
        return;

    max_speed_applied = enabled;

    if (session.headless)
        return;

    if (enabled)
    {
        saved_ffwd = config_emulator.ffwd;
        saved_ffwd_speed = config_emulator.ffwd_speed;
        saved_audio_sync = config_audio.sync;
        config_emulator.ffwd = true;
        config_emulator.ffwd_speed = 4;
        config_audio.sync = false;
        display_disable_vsync();
    }
    else
    {
        config_emulator.ffwd = saved_ffwd;
        config_emulator.ffwd_speed = saved_ffwd_speed;
        config_audio.sync = saved_audio_sync;
        display_use_vsync_if_enabled();
        emu_audio_reset();
    }
}

static void load_symbols(void)
{
    static const char* const k_extensions[] = { ".map", ".sym", ".nm" };

    gui_debug_clear_program_symbols();
    program.symbol_path.clear();
    program.symbol_count = 0;

    if (program.host_path.empty())
        return;

    std::string directory = path_directory(program.host_path);
    std::string name = path_strip_extension(path_filename(program.host_path));

    for (size_t i = 0; i < sizeof(k_extensions) / sizeof(k_extensions[0]); i++)
    {
        std::string found = find_in_folder(directory, name + k_extensions[i]);

        if (!found.empty())
        {
            emu_dev_load_program_symbols(path_join(directory, found).c_str());
            return;
        }
    }
}

static std::string find_in_folder(const std::string& directory, const std::string& name)
{
    std::vector<std::string> folders;
    std::vector<std::string> files;

    if (!MediaFile::ListDirectory(directory.c_str(), folders, files))
        return std::string();

    for (size_t i = 0; i < files.size(); i++)
    {
        if (SDL_strcasecmp(files[i].c_str(), name.c_str()) == 0)
            return files[i];
    }

    for (size_t i = 0; i < folders.size(); i++)
    {
        if (SDL_strcasecmp(folders[i].c_str(), name.c_str()) == 0)
            return folders[i];
    }

    return std::string();
}
