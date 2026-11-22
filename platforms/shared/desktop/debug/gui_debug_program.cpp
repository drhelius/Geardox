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

#define GUI_DEBUG_PROGRAM_IMPORT
#include "gui_debug_program.h"
#include <stdio.h>
#include <string>
#include <vector>
#include "imgui.h"
#include "geardox.h"
#include "../config.h"
#include "../emu.h"
#include "../emu_dev.h"
#include "../emu_game.h"
#include "../gui.h"
#include "../gui_colors.h"
#include "../gui_filedialogs.h"
#include "../utils.h"
#include "gui_debug_disassembler.h"
#include "gui_debug_memory.h"
#include "gui_debug_widgets.h"

static std::string console_text;
static std::vector<std::string> console_lines;
static u64 console_shown = (u64)-1;
static u64 console_start = 0;
static bool console_follow = true;

static void draw_menu(const Dev_Program_Info& program);
static void draw_program(const Dev_Program_Info& program);
static void draw_development(void);
static void draw_console(void);
static void refresh_console(void);
static void draw_switch(const char* label, bool on, const char* on_text, const char* off_text, const char* tooltip);

void gui_debug_window_program(void)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::SetNextWindowPos(ImVec2(200, 140), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(460, 560), ImGuiCond_FirstUseEver);
    ImGui::Begin("Program", &config_debug.show_program, ImGuiWindowFlags_MenuBar);

    Dev_Program_Info program;
    emu_dev_get_program(&program);
    refresh_console();
    draw_menu(program);

    ImGui::PushFont(gui_default_font);
    draw_program(program);
    draw_development();
    draw_console();
    ImGui::PopFont();

    ImGui::End();
    ImGui::PopStyleVar();
}

bool gui_debug_program_save_output(const char* file_path)
{
    FILE* file = fopen_utf8(file_path, "w");

    if (!IsValidPointer(file))
        return false;

    for (size_t i = 0; i < console_lines.size(); i++)
        fprintf(file, "%s\n", console_lines[i].c_str());

    fclose(file);
    return true;
}

static void draw_menu(const Dev_Program_Info& program)
{
    if (!ImGui::BeginMenuBar())
        return;

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("Save Console As...", NULL, false, !console_lines.empty()))
            gui_file_dialog_save_program_output();

        if (ImGui::MenuItem("Copy Console", NULL, false, !console_lines.empty()))
            ImGui::SetClipboardText(console_text.c_str());

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Program"))
    {
        bool open = emu_game_is_open();

        if (ImGui::MenuItem("Run Again", NULL, false, open))
            emu_dev_relaunch();

        gui_debug_tooltip("Rebuilds the game's disk from its folder and runs its program through GDXDBG.");

        if (ImGui::MenuItem("Run Again, Stop at Entry", NULL, false, open))
            emu_dev_run_program(NULL, true);

        ImGui::Separator();

        if (ImGui::MenuItem("Load Program Symbols...", NULL, false, program.state == DEV_PORT_PROGRAM_RUNNING))
            gui_file_dialog_load_program_symbols();

        gui_debug_tooltip("A map or symbol file of the running program, moved to where DOS loaded it.");

        if (ImGui::MenuItem("Go to Entry", NULL, false, program.runs > 0))
            gui_debug_goto_address(((u32)program.entry_cs << 4) + program.entry_ip);

        ImGui::Separator();

        if (ImGui::MenuItem("Clear Console", NULL, false, !console_lines.empty()))
        {
            console_start = emu_dev_get_output_count();
            console_shown = (u64)-1;
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Settings"))
    {
        ImGui::MenuItem("Follow Output", NULL, &console_follow);
        ImGui::Separator();
        ImGui::MenuItem("E9h Console", NULL, &config_debug.dev_e9_console);

        gui_debug_tooltip("Port E9h, where a program writes characters to the program console.");

        ImGui::MenuItem("Magic Breakpoint", NULL, &config_debug.dev_magic_breakpoint);

        gui_debug_tooltip("XCHG BX,BX stops in the debugger.");

        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

static void draw_program(const Dev_Program_Info& program)
{
    static const char* const k_terminations[4] = { "NORMAL", "CTRL-BREAK", "CRITICAL ERROR", "RESIDENT" };
    float character = ImGui::CalcTextSize("0").x;
    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;

    ImGui::TextColored(cyan, "PROGRAM"); ImGui::Separator();

    if (program.runs == 0)
    {
        ImGui::TextColored(gray, "NO PROGRAM");
        ImGui::TextColored(gray, "Run one through GDXDBG: Program > Run Again,");
        ImGui::TextColored(gray, "--run or --target debug.");
        return;
    }

    if (!ImGui::BeginTable("##program", 2, flags))
        return;

    ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 10);
    ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 34);

    gui_debug_row_label("STATE");

    if (program.state == DEV_PORT_PROGRAM_RUNNING)
        ImGui::TextColored(green, "RUNNING");
    else if (program.exit_type == 0xFF)
        ImGui::TextColored(red, "NOT LOADED");
    else
        ImGui::TextColored(amber, "EXITED");

    ImGui::SameLine();
    ImGui::TextColored(gray, "RUN %u", program.runs);

    gui_debug_row_label("PATH");
    ImGui::TextColored(white, "%s", program.dos_path.c_str());

    gui_debug_row_label("HOST");

    if (program.host_path.empty())
        ImGui::TextColored(gray, "NOT IN THE GAME'S FOLDER");
    else
    {
        ImGui::TextColored(white, "%s", get_filename(program.host_path.c_str()));

        gui_debug_tooltip(program.host_path.c_str());
    }

    gui_debug_row_label("FORMAT");

    if (program.load_flags & DEV_PORT_LOAD_COFF)
        ImGui::TextColored(orange, "DJGPP COFF");
    else if (program.entry_cs == program.psp)
        ImGui::TextColored(orange, "COM");
    else
        ImGui::TextColored(orange, "MZ EXE");

    gui_debug_row_label("PSP");
    ImGui::TextColored(white, "$%04X", program.psp);

    if (ImGui::IsItemClicked())
        gui_debug_memory_goto(GDOX_DEBUG_MEMORY_LINEAR, (u32)program.psp << 4);

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Program Segment Prefix at linear $%08X\nClick to open it in the memory editor",
            (u32)program.psp << 4);

    ImGui::SameLine();
    ImGui::TextColored(violet, " LOAD"); ImGui::SameLine();
    ImGui::TextColored(white, "$%04X", program.load_segment);

    gui_debug_row_label("ENTRY");
    u32 entry = ((u32)program.entry_cs << 4) + program.entry_ip;
    ImGui::TextColored(white, "%04X:%04X", program.entry_cs, program.entry_ip);

    if (ImGui::IsItemClicked())
        gui_debug_goto_address(entry);

    gui_debug_tooltip("Click to show it in the disassembler");

    ImGui::SameLine();
    ImGui::TextColored(gray, "$%08X", entry);

    gui_debug_row_label("STACK");
    ImGui::TextColored(white, "%04X:%04X", program.stack_ss, program.stack_sp);

    if (ImGui::IsItemClicked())
        gui_debug_memory_goto(GDOX_DEBUG_MEMORY_LINEAR, ((u32)program.stack_ss << 4) + program.stack_sp);

    gui_debug_row_label("BASE");
    ImGui::TextColored(white, "$%08X", program.base);

    if (ImGui::IsItemClicked())
        gui_debug_memory_goto(GDOX_DEBUG_MEMORY_LINEAR, program.base);

    gui_debug_tooltip("Program symbols with plain addresses and gdb's program mode count from here");

    ImGui::SameLine();
    ImGui::TextColored(program.based ? green : gray, "%s", program.based ? "REPORTED" : "LOAD SEGMENT");
    ImGui::SameLine();
    ImGui::TextColored(gray, "%d-BIT", program.code_size);

    if (program.based)
    {
        gui_debug_row_label("SELECTORS");
        ImGui::TextColored(violet, "CS"); ImGui::SameLine();
        ImGui::TextColored(white, "$%04X", program.code_selector); ImGui::SameLine();
        ImGui::TextColored(violet, " DS"); ImGui::SameLine();
        ImGui::TextColored(white, "$%04X", program.data_selector);
    }

    gui_debug_row_label("SYMBOLS");

    if (program.symbol_path.empty())
        ImGui::TextColored(gray, "NONE");
    else
    {
        ImGui::TextColored(white, "%d", program.symbol_count);
        ImGui::SameLine();
        ImGui::TextColored(gray, "%s", get_filename(program.symbol_path.c_str()));

        gui_debug_tooltip(program.symbol_path.c_str());
    }

    if (program.state == DEV_PORT_PROGRAM_EXITED)
    {
        gui_debug_row_label("EXIT CODE");
        ImGui::TextColored(white, "%d", program.exit_code);
        ImGui::SameLine();

        if ((program.exit_type >= 0) && (program.exit_type < 4))
            ImGui::TextColored(program.exit_type == 0 ? green : program.exit_type == 3 ? cyan : yellow, "%s",
                k_terminations[program.exit_type]);
        else if (program.exit_type == 0xFF)
            ImGui::TextColored(red, "NOT LOADED");
    }

    ImGui::EndTable();
}

static void draw_development(void)
{
    DevPort* port = emu_get_core()->GetDevPort();
    float character = ImGui::CalcTextSize("0").x;
    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;

    ImGui::NewLine(); ImGui::TextColored(cyan, "DEVELOPMENT"); ImGui::Separator();

    if (!ImGui::BeginTable("##development", 2, flags))
        return;

    ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 10);
    ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 34);

    draw_switch("PORT E9H", port->IsConsoleEnabled(), "ON", "OFF", "The E9h console: Settings > E9h Console or --e9");
    draw_switch("PORT 520H", port->IsControlEnabled(), "ON", "OFF", "GDXDBG and gdxdbg.h talk to Geardox here");
    draw_switch("MAGIC BRK", emu_get_core()->GetI386()->IsMagicBreakpoint(), "XCHG BX,BX", "OFF",
        "Settings > Magic Breakpoint or --magic-break");

    gui_debug_row_label("GDB");

    if (!emu_gdb_is_running())
        ImGui::TextColored(gray, "OFF");
    else
    {
        ImGui::TextColored(emu_gdb_is_connected() ? amber : green, "%s", emu_gdb_is_connected() ? "CONNECTED" : "LISTENING");
        ImGui::SameLine();
        ImGui::TextColored(gray, "127.0.0.1:%d", emu_gdb_get_port());
    }

    ImGui::EndTable();
}

static void draw_console(void)
{
    ImGui::NewLine(); ImGui::TextColored(cyan, "CONSOLE");
    ImGui::SameLine();
    ImGui::TextColored(gray, "%u LINES", (u32)console_lines.size());
    ImGui::Separator();

    bool updated = false;
    static size_t shown_lines = 0;

    if (shown_lines != console_lines.size())
    {
        shown_lines = console_lines.size();
        updated = true;
    }

    if (!ImGui::BeginChild("##program_console", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar))
    {
        ImGui::EndChild();
        return;
    }

    if (console_lines.empty())
        ImGui::TextColored(gray, "NO OUTPUT");
    else
    {
        ImGuiListClipper clipper;
        clipper.Begin((int)console_lines.size());

        while (clipper.Step())
        {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
                ImGui::TextUnformatted(console_lines[i].c_str());
        }

        if (console_follow && updated)
            ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();
}

static void refresh_console(void)
{
    u64 written = emu_dev_get_output_count();

    if (written < console_start)
        console_start = 0;

    if (written == console_shown)
        return;

    u64 next = 0;
    console_text = emu_dev_get_output(console_start, DEBUG_CONSOLE_SIZE, &next);
    console_shown = written;
    console_lines.clear();

    std::string line;

    for (size_t i = 0; i < console_text.size(); i++)
    {
        char c = console_text[i];

        if (c == '\n')
        {
            console_lines.push_back(line);
            line.clear();
        }
        else if (c != '\r')
            line += c;
    }

    if (!line.empty())
        console_lines.push_back(line);
}

static void draw_switch(const char* label, bool on, const char* on_text, const char* off_text, const char* tooltip)
{
    gui_debug_row_label(label);
    ImGui::TextColored(on ? green : gray, "%s", on ? on_text : off_text);

    gui_debug_tooltip(tooltip);
}

