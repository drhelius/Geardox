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

#define GUI_DEBUG_INPUT_IMPORT
#include "gui_debug_input.h"

#include "imgui.h"
#include "geardox.h"
#include "input/input.h"
#include "input/i8042.h"
#include "input/keyboard.h"
#include "input/ps2_mouse.h"
#include "input/serial_mouse.h"
#include "input/game_port.h"
#include "system/system_control.h"
#include "system/serial_ports.h"
#include "system/uart.h"
#include "system/parallel_port.h"
#include "system/machine_profiles.h"
#include "../config.h"
#include "../emu.h"
#include "../gui.h"
#include "../gui_colors.h"
#include "gui_debug_constants.h"

static void draw_grid_label(const char* label);
static void draw_mouse(GeardoxCore* core);
static void draw_joystick(GeardoxCore* core);
static void draw_uart(SerialPorts* ports, int index);
static void draw_flag(const char* name, bool on);

void gui_debug_window_mouse_joystick(void)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::SetNextWindowPos(ImVec2(240, 179), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 420), ImGuiCond_FirstUseEver);
    ImGui::Begin("Mouse and Joystick", &config_debug.show_mouse_joystick);

    ImGui::PushFont(gui_default_font);

    GeardoxCore* core = emu_get_core();
    draw_mouse(core);
    ImGui::NewLine();
    draw_joystick(core);

    ImGui::PopFont();

    ImGui::End();
    ImGui::PopStyleVar();
}

void gui_debug_window_serial_ports(void)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::SetNextWindowPos(ImVec2(260, 199), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(330, 470), ImGuiCond_FirstUseEver);
    ImGui::Begin("Serial Ports", &config_debug.show_serial_ports);

    ImGui::PushFont(gui_default_font);

    SerialPorts* ports = emu_get_core()->GetSerialPorts();

    for (int i = 0; i < SERIAL_PORTS; i++)
    {
        if (i > 0)
            ImGui::NewLine();

        draw_uart(ports, i);
    }

    ImGui::PopFont();

    ImGui::End();
    ImGui::PopStyleVar();
}

void gui_debug_window_parallel_port(void)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::SetNextWindowPos(ImVec2(280, 219), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(260, 150), ImGuiCond_FirstUseEver);
    ImGui::Begin("Parallel Port", &config_debug.show_parallel_port);

    ImGui::PushFont(gui_default_font);

    ParallelPort* port = emu_get_core()->GetParallelPort();
    float character = ImGui::CalcTextSize("0").x;
    float space = ImGui::CalcTextSize(" ").x;
    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;
    u8 status = port->Read(k_parallel_port_base + 1);
    u8 control = port->Read(k_parallel_port_base + 2);

    ImGui::TextColored(cyan, "LPT1 $%03X", k_parallel_port_base); ImGui::Separator();

    if (ImGui::BeginTable("##lpt", 2, flags))
    {
        ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 9);
        ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 26);

        ImGui::TableNextRow();
        draw_grid_label("DATA");
        ImGui::TextColored(white, "$%02X", port->GetState()->data);

        ImGui::TableNextRow();
        draw_grid_label("STATUS");
        ImGui::TextColored(white, "$%02X", status); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(gray, "NOTHING ATTACHED");

        ImGui::TableNextRow();
        draw_grid_label("CONTROL");
        ImGui::TextColored(white, "$%02X", control); ImGui::SameLine(0.0f, space);
        draw_flag("STB", (control & 0x01) != 0); ImGui::SameLine(0.0f, space);
        draw_flag("AFD", (control & 0x02) != 0); ImGui::SameLine(0.0f, space);
        draw_flag("INIT", (control & 0x04) == 0); ImGui::SameLine(0.0f, space);
        draw_flag("SEL", (control & 0x08) != 0); ImGui::SameLine(0.0f, space);
        draw_flag("IRQ", (control & 0x10) != 0);

        ImGui::EndTable();
    }

    ImGui::PopFont();

    ImGui::End();
    ImGui::PopStyleVar();
}

void gui_debug_window_keyboard(void)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::SetNextWindowPos(ImVec2(240, 179), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340, 470), ImGuiCond_FirstUseEver);
    ImGui::Begin("Keyboard", &config_debug.show_keyboard);

    ImGui::PushFont(gui_default_font);

    I8042* controller = emu_get_core()->GetI8042();
    I8042::I8042_State* state = controller->GetState();
    Keyboard* keyboard = emu_get_core()->GetKeyboard();
    Keyboard::Keyboard_State* keys = keyboard->GetState();
    SystemControl* system_control = emu_get_core()->GetSystemControl();
    float character = ImGui::CalcTextSize("0").x;
    float space = ImGui::CalcTextSize(" ").x;
    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;
    u8 command = state->ram[0];
    u8 status = controller->GetStatus();

    ImGui::TextColored(cyan, "8042"); ImGui::Separator();

    if (ImGui::BeginTable("##i8042", 2, flags))
    {
        ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 10);
        ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 28);

        ImGui::TableNextRow();
        draw_grid_label("STATUS");
        ImGui::TextColored(white, "$%02X", status); ImGui::SameLine(0.0f, space);
        draw_flag("OBF", (status & 0x01) != 0); ImGui::SameLine(0.0f, space);
        draw_flag("IBF", (status & 0x02) != 0); ImGui::SameLine(0.0f, space);
        draw_flag("SYS", (status & 0x04) != 0); ImGui::SameLine(0.0f, space);
        draw_flag("AUX", (status & 0x20) != 0); ImGui::SameLine(0.0f, space);
        draw_flag("TO", (status & 0x40) != 0);

        ImGui::TableNextRow();
        draw_grid_label("COMMAND");
        ImGui::TextColored(white, "$%02X", command); ImGui::SameLine(0.0f, space);
        draw_flag("IRQ1", (command & k_i8042_keyboard_interrupt) != 0); ImGui::SameLine(0.0f, space);
        draw_flag("IRQ12", (command & k_i8042_aux_interrupt) != 0); ImGui::SameLine(0.0f, space);
        draw_flag("XLAT", (command & k_i8042_translate_enabled) != 0);

        ImGui::TableNextRow();
        draw_grid_label("PORTS");
        draw_flag("KBD", (command & k_i8042_keyboard_disabled) == 0); ImGui::SameLine(0.0f, space);
        draw_flag("AUX", (command & k_i8042_aux_disabled) == 0);

        ImGui::TableNextRow();
        draw_grid_label("OUTPUT");
        ImGui::TextColored(state->output_full ? white : gray, "$%02X", state->output_buffer);
        ImGui::SameLine(0.0f, space);
        ImGui::TextColored(gray, "%s", state->output_aux ? "AUX" : "KBD");

        ImGui::TableNextRow();
        draw_grid_label("PENDING");

        if (state->pending_command != 0)
            ImGui::TextColored(orange, "$%02X", state->pending_command);
        else
            ImGui::TextColored(gray, "--");

        ImGui::TableNextRow();
        draw_grid_label("OUT PORT");
        ImGui::TextColored(white, "$%02X", controller->GetOutputPort());

        ImGui::TableNextRow();
        draw_grid_label("IN PORT");
        ImGui::TextColored(white, "$%02X", k_i8042_input_port);

        ImGui::TableNextRow();
        draw_grid_label("A20");
        draw_flag("GATE", system_control->IsA20Enabled()); ImGui::SameLine(0.0f, space);
        draw_flag("8042", system_control->IsKeyboardA20Enabled()); ImGui::SameLine(0.0f, space);
        draw_flag("92h", (system_control->GetState()->port_92 & 0x02) != 0);

        ImGui::EndTable();
    }

    ImGui::NewLine();
    ImGui::TextColored(cyan, "MF2 KEYBOARD"); ImGui::Separator();

    if (ImGui::BeginTable("##keyboard", 2, flags))
    {
        ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 10);
        ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 28);

        ImGui::TableNextRow();
        draw_grid_label("SCANNING");
        draw_flag(keys->self_test_clocks != GDOX_NO_EVENT ? "SELF TEST" : "ON", keys->scanning);

        ImGui::TableNextRow();
        draw_grid_label("SCAN SET");
        ImGui::TextColored(white, "%u", keys->scan_set);

        ImGui::TableNextRow();
        draw_grid_label("LEDS");
        draw_flag("NUM", (keys->leds & 0x02) != 0); ImGui::SameLine(0.0f, space);
        draw_flag("CAPS", (keys->leds & 0x04) != 0); ImGui::SameLine(0.0f, space);
        draw_flag("SCROLL", (keys->leds & 0x01) != 0);

        ImGui::TableNextRow();
        draw_grid_label("TYPEMATIC");
        ImGui::TextColored(white, "%u ms, %.1f/s", keyboard->GetTypematicDelay(),
            1000000.0f / (float)keyboard->GetTypematicPeriod());

        ImGui::TableNextRow();
        draw_grid_label("REPEAT");
        const char* repeat = keys->repeat_key != GDOX_KEY_NONE ? gui_debug_key_name(keys->repeat_key) : NULL;
        ImGui::TextColored(IsValidPointer(repeat) ? orange : gray, "%s", IsValidPointer(repeat) ? repeat : "--");

        ImGui::TableNextRow();
        draw_grid_label("BUFFER");
        ImGui::TextColored(keys->overrun ? yellow : white, "%u/%u%s", keys->buffer_count, KEYBOARD_BUFFER_SIZE,
            keys->overrun ? " OVERRUN" : "");

        for (int i = 0; i < keys->buffer_count; i++)
        {
            if ((i & 7) == 0)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TableNextColumn();
            }
            else
                ImGui::SameLine(0.0f, space);

            ImGui::TextColored(white, "%02X", keys->buffer[(keys->buffer_read + i) & (KEYBOARD_BUFFER_CAPACITY - 1)]);
        }

        ImGui::TableNextRow();
        draw_grid_label("HELD");
        bool held = false;

        for (int i = GDOX_KEY_NONE + 1; i < GDOX_KEY_COUNT; i++)
        {
            const char* name = keys->keys[i] != 0 ? gui_debug_key_name(i) : NULL;

            if (!IsValidPointer(name))
                continue;

            if (held)
                ImGui::SameLine(0.0f, space);

            ImGui::TextColored(green, "%s", name);
            held = true;
        }

        if (!held)
            ImGui::TextColored(gray, "--");

        ImGui::EndTable();
    }

    ImGui::PopFont();

    ImGui::End();
    ImGui::PopStyleVar();
}

static void draw_mouse(GeardoxCore* core)
{
    u8 type = core->GetMachineConfig().mouse;
    Input::Input_State* input = core->GetInput()->GetState();
    float character = ImGui::CalcTextSize("0").x;
    float space = ImGui::CalcTextSize(" ").x;
    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;

    ImGui::TextColored(cyan, "MOUSE"); ImGui::Separator();

    if (!ImGui::BeginTable("##mouse", 2, flags))
        return;

    ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 9);
    ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 24);

    ImGui::TableNextRow();
    draw_grid_label("TYPE");
    ImGui::TextColored(type == GDOX_MOUSE_NONE ? gray : orange, "%s", k_mice[type % GDOX_MOUSE_TYPE_COUNT].name);

    ImGui::TableNextRow();
    draw_grid_label("HOST");
    ImGui::TextColored(violet, "X"); ImGui::SameLine(0.0f, space);
    ImGui::TextColored(white, "%+5d", input->mouse_x); ImGui::SameLine(0.0f, space);
    ImGui::TextColored(violet, "Y"); ImGui::SameLine(0.0f, space);
    ImGui::TextColored(white, "%+5d", input->mouse_y);

    ImGui::TableNextRow();
    draw_grid_label("BUTTONS");
    draw_flag("L", (input->mouse_buttons & GDOX_MOUSE_LEFT) != 0); ImGui::SameLine(0.0f, space);
    draw_flag("M", (input->mouse_buttons & GDOX_MOUSE_MIDDLE) != 0); ImGui::SameLine(0.0f, space);
    draw_flag("R", (input->mouse_buttons & GDOX_MOUSE_RIGHT) != 0);

    if (type == GDOX_MOUSE_PS2)
    {
        static const u8 k_resolutions[4] = { 1, 2, 4, 8 };
        PS2Mouse::PS2Mouse_State* state = core->GetPS2Mouse()->GetState();

        ImGui::TableNextRow();
        draw_grid_label("MODE");
        ImGui::TextColored(white, "%s", state->wrap ? "WRAP" : (state->remote ? "REMOTE" : "STREAM"));
        ImGui::SameLine(0.0f, space);
        draw_flag("ON", state->enabled); ImGui::SameLine(0.0f, space);
        draw_flag("2:1", state->scaling); ImGui::SameLine(0.0f, space);
        draw_flag("CLK", state->clock_enabled);

        ImGui::TableNextRow();
        draw_grid_label("RATE");
        ImGui::TextColored(white, "%d/s", state->rate); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(white, "%d/mm", k_resolutions[state->resolution & 3]);

        ImGui::TableNextRow();
        draw_grid_label("COUNTS");
        ImGui::TextColored(violet, "X"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(white, "%+5d", state->x); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(violet, "Y"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(white, "%+5d", state->y);

        ImGui::TableNextRow();
        draw_grid_label("QUEUED");

        if (state->reply_count == 0)
            ImGui::TextColored(gray, "--");

        for (int i = 0; i < state->reply_count; i++)
        {
            ImGui::TextColored(white, "%02X", state->reply[i]);

            if (i + 1 < state->reply_count)
                ImGui::SameLine(0.0f, space);
        }
    }
    else if (type == GDOX_MOUSE_SERIAL)
    {
        SerialMouse::SerialMouse_State* state = core->GetSerialPorts()->GetMouse()->GetState();

        ImGui::TableNextRow();
        draw_grid_label("LINE");
        draw_flag("RTS", state->rts); ImGui::SameLine(0.0f, space);
        draw_flag("READY", state->ready);

        ImGui::TableNextRow();
        draw_grid_label("QUEUED");

        if (state->queue_count == 0)
            ImGui::TextColored(gray, "--");

        for (int i = 0; i < state->queue_count; i++)
        {
            ImGui::TextColored(white, "%02X", state->queue[i]);

            if (i + 1 < state->queue_count)
                ImGui::SameLine(0.0f, space);
        }
    }

    ImGui::EndTable();
}

static void draw_joystick(GeardoxCore* core)
{
    static const char* const k_axes[GDOX_JOYSTICK_AXIS_COUNT] = { "A-X", "A-Y", "B-X", "B-Y" };
    Input* input = core->GetInput();
    GamePort* port = core->GetGamePort();
    GamePort::GamePort_State* state = port->GetState();
    const GDOX_Joystick_State& joystick = input->GetJoystickState();
    GDOX_Joystick_Type type = input->GetJoystickType();
    u64 clocks = core->GetScheduler()->GetClocks();
    u8 value = port->Peek(clocks);
    float character = ImGui::CalcTextSize("0").x;
    float space = ImGui::CalcTextSize(" ").x;
    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;

    ImGui::TextColored(cyan, "JOYSTICK $201"); ImGui::Separator();

    if (!ImGui::BeginTable("##joystick", 2, flags))
        return;

    ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 9);
    ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 24);

    ImGui::TableNextRow();
    draw_grid_label("TYPE");
    ImGui::TextColored(type == GDOX_JOYSTICK_NONE ? gray : orange, "%s", k_joysticks[type % GDOX_JOYSTICK_TYPE_COUNT].name);

    ImGui::TableNextRow();
    draw_grid_label("PORT");
    ImGui::TextColored(white, "$%02X", value);

    for (int i = 0; i < GDOX_JOYSTICK_AXIS_COUNT; i++)
    {
        u64 end = state->pulse_end[i];

        ImGui::TableNextRow();
        draw_grid_label(k_axes[i]);
        ImGui::TextColored(white, "%+6d", joystick.axes[i]); ImGui::SameLine(0.0f, space);

        if (end == GDOX_NO_EVENT)
            ImGui::TextColored(gray, "OPEN");
        else if (end == 0)
            ImGui::TextColored(gray, "--");
        else
            ImGui::TextColored(white, "%4u us", (u32)(((end - state->fire_clocks) * 1000000) / GDOX_MACHINE_CLOCK_RATE));
    }

    ImGui::TableNextRow();
    draw_grid_label("BUTTONS");
    draw_flag("1", (joystick.buttons & GDOX_JOYSTICK_BUTTON_1) != 0); ImGui::SameLine(0.0f, space);
    draw_flag("2", (joystick.buttons & GDOX_JOYSTICK_BUTTON_2) != 0); ImGui::SameLine(0.0f, space);
    draw_flag("3", (joystick.buttons & GDOX_JOYSTICK_BUTTON_3) != 0); ImGui::SameLine(0.0f, space);
    draw_flag("4", (joystick.buttons & GDOX_JOYSTICK_BUTTON_4) != 0);

    ImGui::TableNextRow();
    draw_grid_label("HAT");
    draw_flag("UP", (joystick.hat & GDOX_JOYSTICK_HAT_UP) != 0); ImGui::SameLine(0.0f, space);
    draw_flag("DOWN", (joystick.hat & GDOX_JOYSTICK_HAT_DOWN) != 0); ImGui::SameLine(0.0f, space);
    draw_flag("LEFT", (joystick.hat & GDOX_JOYSTICK_HAT_LEFT) != 0); ImGui::SameLine(0.0f, space);
    draw_flag("RIGHT", (joystick.hat & GDOX_JOYSTICK_HAT_RIGHT) != 0);

    ImGui::EndTable();
}

static void draw_uart(SerialPorts* ports, int index)
{
    UART* uart = ports->GetUART(index);
    UART::UART_State* state = uart->GetState();
    float character = ImGui::CalcTextSize("0").x;
    float space = ImGui::CalcTextSize(" ").x;
    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;
    char table_id[32];
    u8 lsr = uart->GetLineStatus();
    u8 iir = uart->GetInterruptIdentification();
    u32 divisor = uart->GetDivisor();
    bool mouse = (index == 0) && ports->IsMouseAttached();

    ImGui::TextColored(cyan, "COM%d $%03X IRQ %d", index + 1, k_serial_ports_base[index], k_serial_ports_irq[index]);
    ImGui::Separator();
    snprintf(table_id, sizeof(table_id), "##uart_%d", index);

    if (!ImGui::BeginTable(table_id, 2, flags))
        return;

    ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 8);
    ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 28);

    ImGui::TableNextRow();
    draw_grid_label("DEVICE");
    ImGui::TextColored(mouse ? orange : gray, "%s", mouse ? "SERIAL MOUSE" : "NOTHING ATTACHED");

    ImGui::TableNextRow();
    draw_grid_label("LINE");
    ImGui::TextColored(white, "%u bps", k_uart_clock / (16 * divisor)); ImGui::SameLine(0.0f, space);
    ImGui::TextColored(white, "%d%c%s", 5 + (state->lcr & 0x03), (state->lcr & 0x08) == 0 ? 'N' :
        ((state->lcr & 0x10) != 0 ? 'E' : 'O'), (state->lcr & 0x04) != 0 ? "2" : "1");

    ImGui::TableNextRow();
    draw_grid_label("IER");
    ImGui::TextColored(white, "$%02X", state->ier); ImGui::SameLine(0.0f, space);
    draw_flag("RDA", (state->ier & 0x01) != 0); ImGui::SameLine(0.0f, space);
    draw_flag("THRE", (state->ier & 0x02) != 0); ImGui::SameLine(0.0f, space);
    draw_flag("LS", (state->ier & 0x04) != 0); ImGui::SameLine(0.0f, space);
    draw_flag("MS", (state->ier & 0x08) != 0);

    ImGui::TableNextRow();
    draw_grid_label("IIR");
    ImGui::TextColored(white, "$%02X", iir); ImGui::SameLine(0.0f, space);
    draw_flag("IRQ", ports->GetIRQ(index));

    ImGui::TableNextRow();
    draw_grid_label("FCR");
    ImGui::TextColored(white, "$%02X", state->fcr); ImGui::SameLine(0.0f, space);
    ImGui::TextColored(gray, "RX %2d TX %2d", state->rx_count, state->tx_count);

    ImGui::TableNextRow();
    draw_grid_label("LCR");
    ImGui::TextColored(white, "$%02X", state->lcr); ImGui::SameLine(0.0f, space);
    draw_flag("DLAB", (state->lcr & 0x80) != 0);

    ImGui::TableNextRow();
    draw_grid_label("MCR");
    ImGui::TextColored(white, "$%02X", state->mcr); ImGui::SameLine(0.0f, space);
    draw_flag("DTR", (state->mcr & k_uart_dtr) != 0); ImGui::SameLine(0.0f, space);
    draw_flag("RTS", (state->mcr & k_uart_rts) != 0); ImGui::SameLine(0.0f, space);
    draw_flag("OUT2", (state->mcr & k_uart_out2) != 0); ImGui::SameLine(0.0f, space);
    draw_flag("LOOP", (state->mcr & k_uart_loopback) != 0);

    ImGui::TableNextRow();
    draw_grid_label("LSR");
    ImGui::TextColored(white, "$%02X", lsr); ImGui::SameLine(0.0f, space);
    draw_flag("DR", (lsr & 0x01) != 0); ImGui::SameLine(0.0f, space);
    draw_flag("OE", (lsr & 0x02) != 0); ImGui::SameLine(0.0f, space);
    draw_flag("FE", (lsr & 0x08) != 0); ImGui::SameLine(0.0f, space);
    draw_flag("THRE", (lsr & 0x20) != 0); ImGui::SameLine(0.0f, space);
    draw_flag("TEMT", (lsr & 0x40) != 0);

    ImGui::TableNextRow();
    draw_grid_label("MSR");
    ImGui::TextColored(white, "$%02X", state->msr);

    ImGui::TableNextRow();
    draw_grid_label("SCR");
    ImGui::TextColored(white, "$%02X", state->scr);

    ImGui::EndTable();
}

static void draw_flag(const char* name, bool on)
{
    ImGui::TextColored(on ? green : gray, "%s", name);
}

static void draw_grid_label(const char* label)
{
    ImGui::TableNextColumn();
    ImGui::TextColored(violet, "%s", label);
    ImGui::TableNextColumn();
}
