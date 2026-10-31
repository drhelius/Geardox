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

#define GUI_DEBUG_SYSTEM_IMPORT
#include "gui_debug_system.h"

#include "imgui.h"
#include "geardox.h"
#include "system/io.h"
#include "system/pic.h"
#include "system/pit.h"
#include "system/scheduler.h"
#include "../config.h"
#include "../emu.h"
#include "../gui.h"
#include "../gui_colors.h"
#include "../utils.h"
#include "gui_debug_constants.h"
#include "gui_debug_i386_tables.h"

static const char* const k_pit_mode_short_names[6] =
{
    "0 INT ON TC", "1 ONE-SHOT", "2 RATE GEN", "3 SQUARE", "4 SW STROBE", "5 HW STROBE"
};

static void setup_fixed_columns(const int* widths, int count);
static void draw_grid_label(const char* label);
static void draw_flag(const char* label, bool on);
static void draw_model_flag(const char* label, bool present, bool on, const char* on_text, const char* off_text);
static void draw_vector_tooltip(u8 vector);
static void draw_pic_column(const I8259::I8259_State* chip, bool master, int row);

void gui_debug_window_pic(void)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::SetNextWindowPos(ImVec2(212, 244), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(410, 597), ImGuiCond_FirstUseEver);
    ImGui::Begin("Interrupts", &config_debug.show_pic);

    ImGui::PushFont(gui_default_font);

    PIC* pic = emu_get_core()->GetPIC();
    I8259::I8259_State* master = pic->GetMaster()->GetState();
    I8259::I8259_State* slave = pic->GetSlave()->GetState();
    float character = ImGui::CalcTextSize("0").x;

    ImGui::TextColored(cyan, "IRQ LINES"); ImGui::Separator();

    ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit |
        ImGuiTableFlags_NoHostExtendX;

    if (ImGui::BeginTable("##irqs", 7, flags))
    {
        ImGui::TableSetupColumn("IRQ");
        ImGui::TableSetupColumn("SOURCE");
        ImGui::TableSetupColumn("LEVEL");
        ImGui::TableSetupColumn("REQ");
        ImGui::TableSetupColumn("SERV");
        ImGui::TableSetupColumn("MASK");
        ImGui::TableSetupColumn("VECTOR");
        ImGui::TableHeadersRow();

        for (int irq = 0; irq < 16; irq++)
        {
            const I8259::I8259_State* chip = irq < 8 ? master : slave;
            u8 bit = (u8)(1 << (irq & 7));
            u8 vector = (u8)((chip->icw2 & 0xF8) | (irq & 7));

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(orange, "%d", irq);
            ImGui::TableNextColumn();
            ImGui::TextColored(brown, "%s", k_debug_irq_sources[irq]);
            ImGui::TableNextColumn();
            ImGui::TextColored((chip->input_levels & bit) ? green : gray, "%s", (chip->input_levels & bit) ? "HI" : "LO");
            ImGui::TableNextColumn();
            ImGui::TextColored((chip->irr & bit) ? yellow : gray, "%d", (chip->irr & bit) ? 1 : 0);
            ImGui::TableNextColumn();
            ImGui::TextColored((chip->isr & bit) ? green : gray, "%d", (chip->isr & bit) ? 1 : 0);
            ImGui::TableNextColumn();
            ImGui::TextColored((chip->imr & bit) ? red : gray, "%d", (chip->imr & bit) ? 1 : 0);
            ImGui::TableNextColumn();
            ImGui::TextColored(white, "$%02X", vector);

            if (ImGui::IsItemHovered())
                draw_vector_tooltip(vector);
        }

        ImGui::EndTable();
    }

    ImGui::NewLine(); ImGui::TextColored(cyan, "CONTROLLERS"); ImGui::Separator();

    if (ImGui::BeginTable("##pics", 3, flags))
    {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, character * 11);
        ImGui::TableSetupColumn("MASTER", ImGuiTableColumnFlags_WidthFixed, character * 21);
        ImGui::TableSetupColumn("SLAVE", ImGuiTableColumnFlags_WidthFixed, character * 21);
        ImGui::TableHeadersRow();

        static const char* rows[] = { "ICW1", "ICW2", "ICW3", "ICW4", "INIT", "READ REG", "SPEC MASK", "POLL",
            "PRIORITY", "AUTO ROTATE", "INT OUTPUT" };

        for (int row = 0; row < (int)(sizeof(rows) / sizeof(rows[0])); row++)
        {
            ImGui::TableNextRow();
            draw_grid_label(rows[row]);
            draw_pic_column(master, true, row);
            ImGui::TableNextColumn();
            draw_pic_column(slave, false, row);
        }

        ImGui::EndTable();
    }

    ImGui::PopFont();

    ImGui::End();
    ImGui::PopStyleVar();
}

void gui_debug_window_pit(void)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::SetNextWindowPos(ImVec2(253, 111), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(616, 348), ImGuiCond_FirstUseEver);
    ImGui::Begin("Timers", &config_debug.show_pit);

    ImGui::PushFont(gui_default_font);

    GeardoxCore* core = emu_get_core();
    PIT* pit = core->GetPIT();
    PIT::PIT_State* state = pit->GetState();
    u64 clocks = core->GetScheduler()->GetClocks();
    u8 board = pit->Peek(0x0060, clocks);
    float character = ImGui::CalcTextSize("0").x;
    u8 timer2 = 0;
    u8 period_low = 0;
    u8 period_high = 0;
    u8 free_low = 0;
    u8 free_high = 0;
    bool has_timer2 = core->GetIO()->Peek(0x0068, clocks, timer2);
    bool has_free_run = core->GetIO()->Peek(0x0026, clocks, free_low) && core->GetIO()->Peek(0x0027, clocks, free_high);
    core->GetIO()->Peek(0x006A, clocks, period_low);
    core->GetIO()->Peek(0x006B, clocks, period_high);
    bool timer2_irq = has_timer2 && ((timer2 & (k_pit_timer2_disable | k_pit_timer2_pending)) == k_pit_timer2_pending);

    ImGui::TextColored(cyan, "BOARD (0060)"); ImGui::Separator();

    if (ImGui::BeginTable("##pit_board", 6, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX))
    {
        static const int widths[6] = { 10, 4, 10, 4, 5, 4 };
        bool irq0 = ((board & state->timer_enable & 0x03) != 0) || timer2_irq;

        setup_fixed_columns(widths, 6);

        ImGui::TableNextRow();
        draw_flag("TM0 ENABLE", (state->timer_enable & 0x01) != 0);
        draw_flag("TM1 ENABLE", (state->timer_enable & 0x02) != 0);
        draw_flag("SOUND", state->sound || state->sound_memory);

        ImGui::TableNextRow();
        draw_flag("LATCH 0", (board & 0x01) != 0);
        draw_flag("LATCH 1", (board & 0x02) != 0);
        draw_grid_label("IRQ0");
        ImGui::TextColored(irq0 ? yellow : gray, "%s", irq0 ? "HIGH" : "LOW");

        ImGui::EndTable();
    }

    ImGui::NewLine(); ImGui::TextColored(cyan, "LATER TIMERS (0026, 0068-006B)"); ImGui::Separator();

    if (ImGui::BeginTable("##pit_timer2", 6, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX))
    {
        static const int widths[6] = { 10, 5, 10, 9, 8, 4 };
        u32 period = (u32)((period_high << 8) | period_low);

        setup_fixed_columns(widths, 6);

        ImGui::TableNextRow();
        draw_model_flag("TM2 ENABLE", has_timer2, (timer2 & k_pit_timer2_disable) == 0, "ON", "OFF");
        draw_model_flag("PENDING", has_timer2, (timer2 & k_pit_timer2_pending) != 0, "ON", "OFF");
        draw_model_flag("OVERRUN", has_timer2, (timer2 & k_pit_timer2_overrun) != 0, "ON", "OFF");

        ImGui::TableNextRow();
        draw_grid_label("PERIOD");

        if (has_timer2)
            ImGui::TextColored(white, "$%04X", period);
        else
            ImGui::TextColored(gray, "--");

        draw_grid_label("INTERVAL");

        if (has_timer2)
            ImGui::TextColored(white, "%.3f ms", (period == 0 ? 65536 : period) / 1000.0);
        else
            ImGui::TextColored(gray, "--");

        draw_grid_label("STATUS");

        if (has_timer2)
            ImGui::TextColored(white, "$%02X", timer2);
        else
            ImGui::TextColored(gray, "--");

        ImGui::TableNextRow();
        draw_grid_label("FREE RUN");

        if (has_free_run)
            ImGui::TextColored(white, "$%04X", (free_high << 8) | free_low);
        else
            ImGui::TextColored(gray, "--");

        ImGui::EndTable();
    }

    ImGui::NewLine(); ImGui::TextColored(cyan, "COUNTERS"); ImGui::Separator();

    ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit |
        ImGuiTableFlags_NoHostExtendX;

    if (ImGui::BeginTable("##counters", 11, flags))
    {
        static const char* headers[11] = { "CH", "PORT", "USE", "CLOCK", "MODE", "ACCESS", "BCD", "RELOAD", "COUNT", "OUT",
            "PERIOD" };
        static const int widths[11] = { 2, 4, 14, 7, 11, 7, 3, 6, 5, 3, 10 };

        for (int i = 0; i < 11; i++)
            ImGui::TableSetupColumn(headers[i], ImGuiTableColumnFlags_WidthFixed, character * widths[i]);

        ImGui::TableHeadersRow();

        for (int channel = 0; channel < 6; channel++)
        {
            I8253* chip = pit->GetPIT(channel / 3);
            const I8253::I8253_Counter& counter = chip->GetState()->counters[channel % 3];
            u64 tick = pit->GetTick(channel, clocks);
            double rate = channel == k_pit_serial_channel ? 1228800.0 : 307200.0;
            bool programmed = counter.programmed;
            ImVec4 color = programmed ? white : gray;

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(orange, "%d", channel);
            ImGui::TableNextColumn();
            ImGui::TextColored(cyan, "%04X", (channel < 3 ? 0x0040 : 0x0050) + (channel % 3) * 2);
            ImGui::TableNextColumn();
            ImGui::TextColored(brown, "%s", k_debug_pit_uses[channel]);
            ImGui::TableNextColumn();
            ImGui::TextColored(orange, "%s", channel == k_pit_serial_channel ? "1.2288M" : "307.2K");
            ImGui::TableNextColumn();

            if (programmed)
            {
                ImGui::TextColored(blue, "%s", k_pit_mode_short_names[counter.mode % 6]);

                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", k_debug_pit_mode_names[counter.mode % 6]);
            }
            else
                ImGui::TextColored(gray, "--");

            ImGui::TableNextColumn();
            ImGui::TextColored(color, "%s", programmed ? k_debug_pit_access_names[counter.access & 3] : "--");
            ImGui::TableNextColumn();
            ImGui::TextColored(programmed && counter.bcd ? green : gray, "%s", counter.bcd ? "ON" : "OFF");
            ImGui::TableNextColumn();
            ImGui::TextColored(color, "$%04X", counter.reload);
            ImGui::TableNextColumn();

            if (programmed)
                ImGui::TextColored(white, "$%04X", chip->PeekCount(channel % 3, tick));
            else
                ImGui::TextColored(gray, "--");

            ImGui::TableNextColumn();
            bool out = chip->PeekOutput(channel % 3, tick);
            ImGui::TextColored(programmed && out ? green : gray, "%d", out ? 1 : 0);
            ImGui::TableNextColumn();

            u32 count = counter.bcd ? ((counter.reload >> 12) & 0x0F) * 1000 + ((counter.reload >> 8) & 0x0F) * 100 +
                ((counter.reload >> 4) & 0x0F) * 10 + (counter.reload & 0x0F) : counter.reload;

            if (count == 0)
                count = counter.bcd ? 10000 : 0x10000;

            double frequency = rate / count;

            if (!programmed || !counter.counting || counter.mode == 1 || counter.mode == 5)
                ImGui::TextColored(gray, "--");
            else if ((counter.mode == 2 || counter.mode == 3) && frequency >= 1000.0)
                ImGui::TextColored(white, "%.2f kHz", frequency / 1000.0);
            else if (counter.mode == 2 || counter.mode == 3)
                ImGui::TextColored(white, "%.2f Hz", frequency);
            else
                ImGui::TextColored(white, "%.3f ms", (count * 1000.0) / rate);
        }

        ImGui::EndTable();
    }

    ImGui::PopFont();

    ImGui::End();
    ImGui::PopStyleVar();
}

void gui_debug_window_system_control(void)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::SetNextWindowPos(ImVec2(176, 112), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(247, 150), ImGuiCond_FirstUseEver);
    ImGui::Begin("System Control", &config_debug.show_system_control);

    ImGui::PushFont(gui_default_font);

    GeardoxCore* core = emu_get_core();
    const GDOX_Machine_Config& machine = core->GetMachineConfig();
    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;
    static const int widths[2] = { 10, 22 };
    float space = ImGui::CalcTextSize(" ").x;

    ImGui::TextColored(cyan, "MACHINE"); ImGui::Separator();

    if (ImGui::BeginTable("##system_machine", 2, flags))
    {
        setup_fixed_columns(widths, 2);

        ImGui::TableNextRow();
        draw_grid_label("MODEL");
        ImGui::TextColored(white, "%s", k_machine_profiles[machine.model].name);

        ImGui::TableNextRow();
        draw_grid_label("CPU");
        ImGui::TextColored(white, "%s", k_machine_cpus[machine.cpu].name); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(orange, "%.0f MHz", machine.cpu_clock_rate / 1000000.0);

        ImGui::TableNextRow();
        draw_grid_label("RAM");
        ImGui::TextColored(white, "%u KB", machine.ram_size / 1024);

        ImGui::TableNextRow();
        draw_grid_label("FLOPPIES");
        ImGui::TextColored(white, "%d", machine.floppy_drives);

        ImGui::EndTable();
    }

    ImGui::PopFont();

    ImGui::End();
    ImGui::PopStyleVar();
}

static void setup_fixed_columns(const int* widths, int count)
{
    float character = ImGui::CalcTextSize("0").x;

    for (int i = 0; i < count; i++)
        ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * widths[i]);
}

static void draw_grid_label(const char* label)
{
    ImGui::TableNextColumn();
    ImGui::TextColored(violet, "%s", label);
    ImGui::TableNextColumn();
}

static void draw_flag(const char* label, bool on)
{
    draw_grid_label(label);
    ImGui::TextColored(on ? green : gray, "%s", on ? "ON" : "OFF");
}

static void draw_model_flag(const char* label, bool present, bool on, const char* on_text, const char* off_text)
{
    draw_grid_label(label);

    if (present)
        ImGui::TextColored(on ? green : gray, "%s", on ? on_text : off_text);
    else
        ImGui::TextColored(gray, "--");
}

static void draw_vector_tooltip(u8 vector)
{
    GuiDebugDescriptor gate;
    char name[16];
    char description[64];
    gui_debug_i386_vector_name(vector, name, sizeof(name), description, sizeof(description));

    ImGui::BeginTooltip();
    ImGui::TextColored(cyan, "VECTOR $%02X", vector);
    ImGui::Text("%s", description);

    if (!gui_debug_i386_read_table_entry(GuiDebugDescriptorTable_IDT, vector, gate))
        ImGui::TextColored(gray, "Gate unavailable");
    else if (gui_debug_i386_idt_is_ivt())
        ImGui::Text("IVT %04X:%04X (linear $%08X)", gate.gate_selector, gate.gate_offset, gate.base);
    else
    {
        char type[32];
        gui_debug_i386_descriptor_type(gate, type, sizeof(type));
        ImGui::Text("%s %04X:%08X%s", type, gate.gate_selector, gate.gate_offset, gate.present ? "" : " NOT PRESENT");
    }

    ImGui::EndTooltip();
}

static void draw_pic_column(const I8259::I8259_State* chip, bool master, int row)
{
    float space = ImGui::CalcTextSize(" ").x;

    switch (row)
    {
        case 0:
            ImGui::TextColored(white, "$%02X", chip->icw1); ImGui::SameLine(0.0f, space);
            ImGui::TextColored(blue, "%s %s", (chip->icw1 & k_i8259_icw1_ltim) ? "LEVEL" : "EDGE",
                (chip->icw1 & k_i8259_icw1_sngl) ? "SINGLE" : "CASCADE");
            break;
        case 1:
            ImGui::TextColored(white, "$%02X", chip->icw2); ImGui::SameLine(0.0f, space);
            ImGui::TextColored(violet, "BASE"); ImGui::SameLine(0.0f, space);
            ImGui::TextColored(white, "$%02X", chip->icw2 & 0xF8);
            break;
        case 2:
            ImGui::TextColored(white, "$%02X", chip->icw3); ImGui::SameLine(0.0f, space);

            if (master)
                ImGui::TextColored(violet, "SLAVE MASK");
            else
            {
                ImGui::TextColored(violet, "ID"); ImGui::SameLine(0.0f, space);
                ImGui::TextColored(white, "%d", chip->icw3 & 0x07);
            }

            break;
        case 3:
            ImGui::TextColored(white, "$%02X", chip->icw4); ImGui::SameLine(0.0f, space);
            ImGui::TextColored((chip->icw4 & k_i8259_icw4_upm) ? green : gray, "uPM"); ImGui::SameLine(0.0f, space);
            ImGui::TextColored((chip->icw4 & k_i8259_icw4_aeoi) ? green : gray, "AEOI"); ImGui::SameLine(0.0f, space);
            ImGui::TextColored((chip->icw4 & 0x08) ? green : gray, "BUF"); ImGui::SameLine(0.0f, space);
            ImGui::TextColored((chip->icw4 & k_i8259_icw4_sfnm) ? green : gray, "SFNM");
            break;
        case 4:
            if (chip->init_step == I8259::I8259_INIT_READY)
                ImGui::TextColored(green, "READY");
            else
                ImGui::TextColored(yellow, "WAIT %s", k_debug_pic_init_names[chip->init_step & 3]);

            break;
        case 5:
            ImGui::TextColored(blue, "%s", chip->read_isr ? "ISR" : "IRR");
            break;
        case 6:
            ImGui::TextColored(chip->special_mask ? green : gray, "%s", chip->special_mask ? "ON" : "OFF");
            break;
        case 7:
            ImGui::TextColored(chip->poll_pending ? yellow : gray, "%s", chip->poll_pending ? "ARMED" : "OFF");
            break;
        case 8:
            ImGui::TextColored(white, "IR%d", (chip->lowest_priority + 1) & 7); ImGui::SameLine(0.0f, space);
            ImGui::TextColored(gray, "FIRST");
            break;
        case 9:
            ImGui::TextColored(chip->rotate_on_aeoi ? green : gray, "%s", chip->rotate_on_aeoi ? "ON" : "OFF");
            break;
        default:
            ImGui::TextColored(chip->int_output ? yellow : gray, "%s", chip->int_output ? "HIGH" : "LOW");
            break;
    }
}
