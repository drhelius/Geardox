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
#include "system/dma.h"
#include "system/io.h"
#include "system/mc146818.h"
#include "system/pic.h"
#include "system/pit.h"
#include "system/scheduler.h"
#include "system/system_control.h"
#include "system/debug_console.h"
#include "../config.h"
#include "../emu.h"
#include "../gui.h"
#include "../gui_colors.h"
#include "../utils.h"
#include "gui_debug_constants.h"
#include "gui_debug_i386_tables.h"
#include "gui_debug_memory.h"
#include "gui_debug_widgets.h"

static const char* const k_pit_mode_short_names[6] =
{
    "0 INT ON TC", "1 ONE-SHOT", "2 RATE GEN", "3 SQUARE", "4 SW STROBE", "5 HW STROBE"
};

static void setup_fixed_columns(const int* widths, int count);
static void draw_grid_label(const char* label);
static void draw_flag(const char* label, bool on);
static void draw_bits4(u8 value);
static void draw_dma_chip(const I8237::I8237_State* chip, const char* title, const char* id);
static void goto_physical(u32 address);
static void draw_vector_tooltip(u8 vector);
static void draw_pic_column(const I8259::I8259_State* chip, bool master, int row);
static void draw_alarm_byte(u8 value);
static void rtc_write_callback(u16 address, u8 value, void* user_data);

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
    ImGui::SetNextWindowSize(ImVec2(586, 236), ImGuiCond_FirstUseEver);
    ImGui::Begin("Timers", &config_debug.show_pit);

    ImGui::PushFont(gui_default_font);

    GeardoxCore* core = emu_get_core();
    PIT* pit = core->GetPIT();
    PIT::PIT_State* state = pit->GetState();
    u64 clocks = core->GetScheduler()->GetClocks();
    float character = ImGui::CalcTextSize("0").x;
    bool out2 = pit->GetChannel2Output(clocks);

    ImGui::TextColored(cyan, "SPEAKER (0061)"); ImGui::Separator();

    if (ImGui::BeginTable("##pit_speaker", 6, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX))
    {
        static const int widths[6] = { 8, 4, 8, 4, 8, 4 };

        setup_fixed_columns(widths, 6);

        ImGui::TableNextRow();
        draw_flag("GATE 2", state->speaker_gate);
        draw_flag("DATA", state->speaker_data);
        draw_grid_label("SPEAKER");
        ImGui::TextColored(state->speaker_data && out2 ? yellow : gray, "%s", state->speaker_data && out2 ? "HIGH" : "LOW");

        ImGui::TableNextRow();
        draw_grid_label("OUT2");
        ImGui::TextColored(out2 ? green : gray, "%d", out2 ? 1 : 0);
        draw_grid_label("REFRESH");
        ImGui::TextColored(white, "%d", pit->GetRefreshToggle(clocks) ? 1 : 0);

        ImGui::EndTable();
    }

    ImGui::NewLine(); ImGui::TextColored(cyan, "COUNTERS"); ImGui::Separator();

    ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit |
        ImGuiTableFlags_NoHostExtendX;

    if (ImGui::BeginTable("##counters", 11, flags))
    {
        static const char* headers[11] = { "CH", "PORT", "USE", "GATE", "MODE", "ACCESS", "BCD", "RELOAD", "COUNT", "OUT",
            "PERIOD" };
        static const int widths[11] = { 2, 4, 12, 4, 11, 7, 3, 6, 5, 3, 10 };
        double rate = (double)GDOX_MASTER_CLOCK_RATE / 12.0;
        I8253* chip = pit->GetPIT();
        u64 tick = pit->GetTick(clocks);

        for (int i = 0; i < 11; i++)
            ImGui::TableSetupColumn(headers[i], ImGuiTableColumnFlags_WidthFixed, character * widths[i]);

        ImGui::TableHeadersRow();

        for (int channel = 0; channel < I8253_COUNTER_COUNT; channel++)
        {
            const I8253::I8253_Counter& counter = chip->GetState()->counters[channel];
            bool programmed = counter.programmed;
            ImVec4 color = programmed ? white : gray;

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(orange, "%d", channel);
            ImGui::TableNextColumn();
            ImGui::TextColored(cyan, "%04X", 0x0040 + channel);
            ImGui::TableNextColumn();
            ImGui::TextColored(brown, "%s", k_debug_pit_uses[channel]);
            ImGui::TableNextColumn();
            ImGui::TextColored(counter.gate ? green : gray, "%s", counter.gate ? "HI" : "LO");
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
                ImGui::TextColored(white, "$%04X", chip->PeekCount(channel, tick));
            else
                ImGui::TextColored(gray, "--");

            ImGui::TableNextColumn();
            bool out = chip->PeekOutput(channel, tick);
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

void gui_debug_window_dma(void)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::SetNextWindowPos(ImVec2(294, 178), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(620, 432), ImGuiCond_FirstUseEver);
    ImGui::Begin("DMA", &config_debug.show_dma);

    ImGui::PushFont(gui_default_font);

    DMA* dma = emu_get_core()->GetDMA();
    float character = ImGui::CalcTextSize("0").x;

    draw_dma_chip(dma->GetController(0)->GetState(), "CHIP 1 (0000-000F)", "##dma_chip1");
    ImGui::NewLine();
    draw_dma_chip(dma->GetController(1)->GetState(), "CHIP 2 (00C0-00DF)", "##dma_chip2");

    ImGui::NewLine(); ImGui::TextColored(cyan, "CHANNELS"); ImGui::Separator();

    ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit |
        ImGuiTableFlags_NoHostExtendX;

    if (ImGui::BeginTable("##dma_channels", 8, flags))
    {
        static const char* headers[8] = { "CH", "DEVICE", "MODE", "ADDRESS", "COUNT", "MASK", "REQ", "TC" };
        static const int widths[8] = { 2, 8, 24, 7, 11, 4, 3, 2 };

        for (int i = 0; i < 8; i++)
            ImGui::TableSetupColumn(headers[i], ImGuiTableColumnFlags_WidthFixed, character * widths[i]);

        ImGui::TableHeadersRow();

        for (int channel = 0; channel < DMA_CHANNELS; channel++)
        {
            const I8237::I8237_State* chip = dma->GetController(channel >> 2)->GetState();
            const I8237::I8237_Channel& item = chip->channels[channel & 0x03];
            u8 bit = (u8)(1 << (channel & 0x03));
            u8 mode = item.mode;
            bool masked = (chip->mask & bit) != 0;
            bool request = ((chip->request_levels | chip->software_requests) & bit) != 0;
            bool terminal = (chip->status_tc & bit) != 0;
            u32 address = dma->GetPhysicalAddress(channel);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(orange, "%d", channel);
            ImGui::TableNextColumn();
            ImGui::TextColored(brown, "%s", k_debug_dma_devices[channel]);
            ImGui::TableNextColumn();
            ImGui::TextColored(white, "%-7s %-7s %-4s %s", k_debug_dma_direction_names[(mode >> 2) & 3],
                k_debug_dma_service_names[(mode >> 6) & 3], (mode & 0x10) ? "AUTO" : "", (mode & 0x20) ? "DEC" : "INC");

            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::TextColored(cyan, "MODE $%02X", mode);
                ImGui::TextColored(violet, "DIRECTION"); ImGui::SameLine();
                ImGui::TextColored(white, "%s", k_debug_dma_direction_names[(mode >> 2) & 3]);
                ImGui::TextColored(violet, "UNIT     "); ImGui::SameLine();
                ImGui::TextColored(white, "%s", channel >= 4 ? "WORD" : "BYTE");
                ImGui::TextColored(violet, "SERVICE  "); ImGui::SameLine();
                ImGui::TextColored(white, "%s", k_debug_dma_service_names[(mode >> 6) & 3]);
                ImGui::TextColored(violet, "AUTO INIT"); ImGui::SameLine();
                ImGui::TextColored((mode & 0x10) ? green : gray, "%s", (mode & 0x10) ? "ON" : "OFF");
                ImGui::TextColored(violet, "ADDRESS  "); ImGui::SameLine();
                ImGui::TextColored(white, "%s", (mode & 0x20) ? "DECREMENT" : "INCREMENT");
                ImGui::EndTooltip();
            }

            ImGui::TableNextColumn();
            ImGui::TextColored(cyan, "$%06X", address);

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("PAGE $%02X\nCURRENT $%04X\nBASE    $%04X",
                    dma->GetState()->pages[k_dma_channel_pages[channel]], item.current_address, item.base_address);

            if (ImGui::IsItemClicked())
                goto_physical(address);

            ImGui::TableNextColumn();
            ImGui::TextColored(white, "$%04X/$%04X", item.current_count, item.base_count);
            ImGui::TableNextColumn();
            ImGui::TextColored(masked ? red : gray, "%d", masked ? 1 : 0);
            ImGui::TableNextColumn();
            ImGui::TextColored(request ? yellow : gray, "%d", request ? 1 : 0);
            ImGui::TableNextColumn();
            ImGui::TextColored(terminal ? green : gray, "%d", terminal ? 1 : 0);
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
    ImGui::SetNextWindowSize(ImVec2(247, 300), ImGuiCond_FirstUseEver);
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

    SystemControl* system_control = core->GetSystemControl();
    SystemControl::SystemControl_State* state = system_control->GetState();
    u64 clocks = core->GetScheduler()->GetClocks();

    ImGui::NewLine(); ImGui::TextColored(cyan, "BOARD"); ImGui::Separator();

    if (ImGui::BeginTable("##system_board", 2, flags))
    {
        setup_fixed_columns(widths, 2);

        ImGui::TableNextRow();
        draw_grid_label("A20");
        ImGui::TextColored(system_control->IsA20Enabled() ? green : gray, "%s",
            system_control->IsA20Enabled() ? "ON" : "OFF"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(gray, "(92h:$%02X)", state->port_92);

        ImGui::TableNextRow();
        draw_grid_label("NMI");
        ImGui::TextColored(state->nmi_mask ? gray : green, "%s", state->nmi_mask ? "MASKED" : "ENABLED");

        ImGui::TableNextRow();
        draw_grid_label("PORT 61h");
        ImGui::TextColored(white, "$%02X", system_control->Peek(0x0061, clocks));

        ImGui::TableNextRow();
        draw_grid_label("POST CODE");
        ImGui::TextColored(orange, "$%02X", state->post_code);

        ImGui::TableNextRow();
        draw_grid_label("FPU ERROR");
        ImGui::TextColored(state->fpu_error ? yellow : gray, "%s", state->fpu_error ? "IRQ13" : "OFF");

        ImGui::TableNextRow();
        draw_grid_label("CPU RESETS");
        ImGui::TextColored(white, "%u", state->reset_count);

        if (state->reset_count != 0)
        {
            u8 cause = state->last_reset_cause;
            ImGui::SameLine(0.0f, space);
            ImGui::TextColored(gray, "(%s)", (cause & k_system_control_reset_shutdown) != 0 ? "SHUTDOWN" :
                (cause & k_system_control_reset_keyboard) != 0 ? "8042" : "PORT 92h");
        }

        ImGui::EndTable();
    }

    ImGui::PopFont();

    ImGui::End();
    ImGui::PopStyleVar();
}

void gui_debug_window_rtc(void)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::SetNextWindowPos(ImVec2(135, 245), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340, 470), ImGuiCond_FirstUseEver);
    ImGui::Begin("RTC", &config_debug.show_rtc);

    ImGui::PushFont(gui_default_font);

    GeardoxCore* core = emu_get_core();
    u64 clocks = core->GetScheduler()->GetClocks();
    MC146818 rtc(*core->GetRTC());
    rtc.Synchronize(clocks);
    MC146818::MC146818_State* state = rtc.GetState();
    const u8* ram = state->ram;
    u8 status_b = ram[k_mc146818_status_b];
    u8 status_c = rtc.GetStatusC();
    u32 period = rtc.GetPeriodTicks();
    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;
    static const int widths[2] = { 9, 26 };
    float space = ImGui::CalcTextSize(" ").x;

    ImGui::TextColored(cyan, "DATE AND TIME"); ImGui::Separator();

    if (ImGui::BeginTable("##rtc_date", 2, flags))
    {
        setup_fixed_columns(widths, 2);

        ImGui::TableNextRow();
        draw_grid_label("DATE");
        ImGui::TextColored(white, "%02d%02d-%02d-%02d", state->century, state->year, state->month, state->day);
        ImGui::SameLine(0.0f, space);
        ImGui::TextColored(blue, "%s", k_debug_weekday_names[(state->weekday + 6) % 7]);

        ImGui::TableNextRow();
        draw_grid_label("TIME");
        ImGui::TextColored(white, "%02d:%02d:%02d", state->hours, state->minutes, state->seconds);
        ImGui::SameLine(0.0f, space);
        ImGui::TextColored(blue, "%s", (status_b & k_mc146818_b_24_hour) != 0 ? "24H" : "12H");
        ImGui::SameLine(0.0f, space);
        ImGui::TextColored(blue, "%s", (status_b & k_mc146818_b_binary) != 0 ? "BINARY" : "BCD");

        ImGui::TableNextRow();
        draw_grid_label("ALARM");
        draw_alarm_byte(ram[k_mc146818_hours_alarm]);
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextColored(gray, ":");
        ImGui::SameLine(0.0f, 0.0f);
        draw_alarm_byte(ram[k_mc146818_minutes_alarm]);
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextColored(gray, ":");
        ImGui::SameLine(0.0f, 0.0f);
        draw_alarm_byte(ram[k_mc146818_seconds_alarm]);

        ImGui::EndTable();
    }

    ImGui::NewLine(); ImGui::TextColored(cyan, "STATUS"); ImGui::Separator();

    if (ImGui::BeginTable("##rtc_status", 2, flags))
    {
        setup_fixed_columns(widths, 2);

        ImGui::TableNextRow();
        draw_grid_label("DIVIDER");
        bool running = rtc.IsDividerRunning();
        ImGui::TextColored(running ? green : gray, "%s", running ? "RUNNING" : "STOPPED");
        ImGui::SameLine(0.0f, space);
        ImGui::TextColored(rtc.IsUpdating(clocks) ? yellow : gray, "UIP");
        ImGui::SameLine(0.0f, space);
        ImGui::TextColored((status_b & k_mc146818_b_set) != 0 ? yellow : gray, "SET");

        ImGui::TableNextRow();
        draw_grid_label("PERIODIC");

        if (period != 0)
            ImGui::TextColored(orange, "%.2f Hz", (double)k_mc146818_chain_rate / period);
        else
            ImGui::TextColored(gray, "OFF");

        ImGui::TableNextRow();
        draw_grid_label("ENABLED");
        ImGui::TextColored((status_b & k_mc146818_b_periodic) != 0 ? green : gray, "PIE");
        ImGui::SameLine(0.0f, space);
        ImGui::TextColored((status_b & k_mc146818_b_alarm) != 0 ? green : gray, "AIE");
        ImGui::SameLine(0.0f, space);
        ImGui::TextColored((status_b & k_mc146818_b_update) != 0 ? green : gray, "UIE");

        ImGui::TableNextRow();
        draw_grid_label("FLAGS");
        ImGui::TextColored((status_c & k_mc146818_c_periodic) != 0 ? yellow : gray, "PF ");
        ImGui::SameLine(0.0f, space);
        ImGui::TextColored((status_c & k_mc146818_c_alarm) != 0 ? yellow : gray, "AF ");
        ImGui::SameLine(0.0f, space);
        ImGui::TextColored((status_c & k_mc146818_c_update) != 0 ? yellow : gray, "UF ");
        ImGui::SameLine(0.0f, space);
        ImGui::TextColored((status_c & k_mc146818_c_irq) != 0 ? yellow : gray, "IRQ8");

        ImGui::TableNextRow();
        draw_grid_label("INDEX");
        ImGui::TextColored(white, "$%02X", state->index);

        ImGui::EndTable();
    }

    ImGui::NewLine(); ImGui::TextColored(cyan, "REGISTERS"); ImGui::Separator();

    for (int i = 0; i <= k_mc146818_status_d; i++)
    {
        ImGui::TextColored(cyan, "%02X", i); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(violet, "%-10s", k_debug_rtc_register_names[i]); ImGui::SameLine(0.0f, space);
        EditableRegister8(NULL, NULL, (u16)i, i == k_mc146818_status_c ? status_c : ram[i], rtc_write_callback,
            core->GetRTC(), EditableRegisterFlags_ShowBinary);
    }

    ImGui::PopFont();

    ImGui::End();
    ImGui::PopStyleVar();
}

void gui_debug_window_bios_log(void)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::SetNextWindowPos(ImVec2(180, 120), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(560, 360), ImGuiCond_FirstUseEver);
    ImGui::Begin("BIOS Log", &config_debug.show_bios_log);

    static char text[DEBUG_CONSOLE_SIZE + 1];
    static u64 written = (u64)-1;
    static bool follow = true;
    DebugConsole* console = emu_get_core()->GetDebugConsole();
    bool updated = console->GetWrittenCount() != written;

    if (updated)
    {
        console->CopyText(text, sizeof(text));
        written = console->GetWrittenCount();
    }

    ImGui::TextColored(console->IsEnabled() ? green : gray, "%s", console->IsEnabled() ? "PORT 402h ON" : "PORT 402h OFF");
    ImGui::SameLine();
    ImGui::TextColored(gray, "%u bytes", console->GetLength());
    ImGui::SameLine();
    ImGui::Checkbox("Follow", &follow);
    ImGui::SameLine();

    if (ImGui::Button("Copy"))
        ImGui::SetClipboardText(text);

    ImGui::Separator();
    ImGui::PushFont(gui_default_font);
    ImGui::BeginChild("##bios_log", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(text);

    if (follow && updated)
        ImGui::SetScrollHereY(1.0f);

    ImGui::EndChild();
    ImGui::PopFont();

    ImGui::End();
    ImGui::PopStyleVar();
}

static void draw_alarm_byte(u8 value)
{
    if ((value & k_mc146818_alarm_any) == k_mc146818_alarm_any)
        ImGui::TextColored(gray, "**");
    else
        ImGui::TextColored(white, "%02X", value);
}

static void rtc_write_callback(u16 address, u8 value, void* user_data)
{
    MC146818* rtc = (MC146818*)user_data;
    u64 clocks = emu_get_core()->GetScheduler()->GetClocks();
    u8 saved = rtc->GetIndex();

    rtc->Write(0x0070, (u8)address, clocks);
    rtc->Write(0x0071, value, clocks);
    rtc->Write(0x0070, saved, clocks);
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

static void draw_bits4(u8 value)
{
    ImGui::TextColored(white, "$%X", value & 0x0F); ImGui::SameLine(0.0f, ImGui::CalcTextSize(" ").x);
    ImGui::TextColored(gray, "(%d%d%d%d)", (value >> 3) & 1, (value >> 2) & 1, (value >> 1) & 1, value & 1);
}

static void draw_dma_chip(const I8237::I8237_State* chip, const char* title, const char* id)
{
    ImGuiTableFlags grid = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;
    float space = ImGui::CalcTextSize(" ").x;
    bool disabled = (chip->command & k_i8237_command_disable) != 0;

    ImGui::TextColored(cyan, "%s", title); ImGui::Separator();

    if (ImGui::BeginTable(id, 4, grid))
    {
        static const int widths[4] = { 14, 14, 12, 12 };

        setup_fixed_columns(widths, 4);

        ImGui::TableNextRow();
        draw_grid_label("COMMAND");
        ImGui::TextColored(white, "$%02X", chip->command);
        draw_grid_label("STATE");
        ImGui::TextColored(disabled ? red : green, "%s", disabled ? "DISABLED" : "ENABLED");

        ImGui::TableNextRow();
        draw_grid_label("MASK");
        draw_bits4(chip->mask);
        draw_grid_label("SOFTWARE REQ");
        draw_bits4(chip->software_requests);

        ImGui::TableNextRow();
        draw_grid_label("REQUEST LEVELS");
        draw_bits4(chip->request_levels);
        draw_grid_label("TERMINAL CNT");
        draw_bits4(chip->status_tc);

        ImGui::TableNextRow();
        draw_grid_label("FLIP-FLOP");
        ImGui::TextColored(chip->flip_flop ? yellow : gray, "%s", chip->flip_flop ? "HIGH" : "LOW");

        ImGui::EndTable();
    }

    for (int i = 0, shown = 0; i < 8; i++)
    {
        if ((chip->command & (1 << i)) == 0)
            continue;

        if (shown++ > 0)
            ImGui::SameLine(0.0f, space);

        ImGui::TextColored(i == 2 ? red : yellow, "%s", k_debug_dma_command_names[i]);
    }
}

static void goto_physical(u32 address)
{
    GDOX_Debug_Memory_Address target = { };
    target.space = GDOX_DEBUG_MEMORY_PHYSICAL;
    target.address = address;
    target.segment_register = -1;
    target.region = -1;
    gui_debug_memory_goto(target);
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
