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

#define GUI_DEBUG_VIDEO_IMPORT
#include "gui_debug_video.h"

#include "imgui.h"
#include "geardox.h"
#include "system/scheduler.h"
#include "video/video.h"
#include "../config.h"
#include "../emu.h"
#include "../gui.h"
#include "../gui_colors.h"
#include "gui_debug_constants.h"
#include "gui_debug_widgets.h"

static void setup_grid_columns(void);
static void draw_grid_label(const char* label);
static void draw_flag(bool on);
static void write_register(Video::Video_Register_File file, u16 address, u8 value);
static void sequencer_write_callback(u16 address, u8 value, void* user_data);
static void graphics_write_callback(u16 address, u8 value, void* user_data);
static void attribute_write_callback(u16 address, u8 value, void* user_data);
static void crtc_write_callback(u16 address, u8 value, void* user_data);
static void misc_write_callback(u16 address, u8 value, void* user_data);
static void draw_register_file(const char* title, const char* prefix, const char* const* names, const u8* values, int count,
    RegisterWriteCallback8 callback);
static void draw_palette_indices(void);
static void draw_color(int index, u32 color, const u8* components);
static ImVec4 to_imgui_color(u32 color);

static const char* mode_name(Video* video)
{
    if (video->IsTextMode())
        return "TEXT";

    switch (video->GetShiftFormat())
    {
        case Video::VIDEO_FORMAT_PLANAR:
            return video->IsPacked() ? "PLANAR, 8-BIT PIXELS" : "PLANAR";
        case Video::VIDEO_FORMAT_CGA:
            return video->IsPacked() ? "CGA, 8-BIT PIXELS" : "CGA";
        default:
            return video->IsPacked() ? "256 COLORS" : "256 SHIFT, 4-BIT PIXELS";
    }
}

void gui_debug_window_vga(void)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::SetNextWindowPos(ImVec2(156, 172), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(387, 470), ImGuiCond_FirstUseEver);
    ImGui::Begin("VGA", &config_debug.show_vga);

    ImGui::PushFont(gui_default_font);

    GeardoxCore* core = emu_get_core();
    Video* video = core->GetVideo();
    Video::Video_State* state = video->GetState();
    const u8* crtc = state->crtc;
    bool running = state->running;
    u64 clocks = core->GetScheduler()->GetClocks();
    double dot_clock = state->frame_dot_clock / 1000000.0;
    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;

    ImGui::TextColored(cyan, "DISPLAY"); ImGui::Separator();

    if (ImGui::BeginTable("##vga_display", 4, flags))
    {
        setup_grid_columns();

        ImGui::TableNextRow();
        draw_grid_label("RUNNING");
        ImGui::TextColored(running ? green : gray, "%s", running ? "ON" : "OFF");
        draw_grid_label("BLANK");
        ImGui::TextColored(video->IsScreenBlank() ? yellow : gray, "%s", video->IsScreenBlank() ? "YES" : "NO");

        ImGui::TableNextRow();
        draw_grid_label("MODE");
        ImGui::TextColored(blue, "%s", mode_name(video));
        draw_grid_label("BLOCK");
        ImGui::TextColored(white, "%s", (state->misc & 0x01) != 0 ? "3Dx COLOR" : "3Bx MONO");

        ImGui::TableNextRow();
        draw_grid_label("FRAME");
        ImGui::TextColored(white, "%dx%d", video->GetFrameWidth(), video->GetFrameHeight());
        draw_grid_label("DISPLAY");
        ImGui::TextColored(white, "%dx%d", video->GetDisplayWidth(), video->GetDisplayHeight());

        ImGui::TableNextRow();
        draw_grid_label("DOT CLOCK");

        if (running)
            ImGui::TextColored(orange, "%.3f MHz", dot_clock);
        else
            ImGui::TextColored(gray, "--");

        draw_grid_label("LINE RATE");

        if (running)
            ImGui::TextColored(orange, "%.2f kHz", dot_clock * 1000.0 / state->frame_line_dots);
        else
            ImGui::TextColored(gray, "--");

        ImGui::TableNextRow();
        draw_grid_label("LINE");
        ImGui::TextColored(white, "%u DOTS", state->frame_line_dots);
        draw_grid_label("REFRESH");

        if (running)
            ImGui::TextColored(orange, "%.2f Hz", 1000.0 / video->GetFrameTime());
        else
            ImGui::TextColored(gray, "--");

        ImGui::TableNextRow();
        draw_grid_label("LINES");
        ImGui::TextColored(white, "%u", state->frame_lines);
        draw_grid_label("ACTIVE");
        ImGui::TextColored(white, "%u", state->display_lines);

        ImGui::TableNextRow();
        draw_grid_label("RETRACE");
        ImGui::TextColored(white, "%u-%u", state->retrace_start, state->retrace_end);
        draw_grid_label("OVERSCAN");
        ImGui::ColorButton("##overscan", to_imgui_color(video->GetOverscanColor()), ImGuiColorEditFlags_NoTooltip |
            ImGuiColorEditFlags_NoDragDrop, ImVec2(ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight()));

        ImGui::EndTable();
    }

    ImGui::NewLine(); ImGui::TextColored(cyan, "BEAM"); ImGui::Separator();

    if (ImGui::BeginTable("##vga_beam", 4, flags))
    {
        setup_grid_columns();
        u8 status = video->GetSyncStatus(clocks);

        ImGui::TableNextRow();
        draw_grid_label("LINE");
        ImGui::TextColored(white, "%u", video->GetBeamLine(clocks));
        draw_grid_label("DOT");
        ImGui::TextColored(white, "%u", video->GetBeamDot(clocks));

        ImGui::TableNextRow();
        draw_grid_label("DISPLAY EN");
        draw_flag((status & 0x01) == 0);
        draw_grid_label("RETRACE");
        draw_flag((status & 0x08) != 0);

        ImGui::TableNextRow();
        draw_grid_label("VERT IRQ");
        ImGui::TextColored(state->vertical_interrupt ? yellow : gray, "%s", state->vertical_interrupt ? "PENDING" : "CLEAR");
        draw_grid_label("IRQ OUT");
        draw_flag((crtc[k_video_cr_retrace_end] & 0x20) == 0);

        ImGui::EndTable();
    }

    ImGui::NewLine(); ImGui::TextColored(cyan, "SCANOUT"); ImGui::Separator();

    if (ImGui::BeginTable("##vga_scanout", 4, flags))
    {
        setup_grid_columns();

        ImGui::TableNextRow();
        draw_grid_label("START");
        ImGui::TextColored(white, "$%04X", video->GetStartAddress());
        draw_grid_label("LATCHED");
        ImGui::TextColored(white, "$%04X", state->start_address);

        ImGui::TableNextRow();
        draw_grid_label("ADDRESS");
        ImGui::TextColored(white, "$%04X", state->row_address & 0xFFFF);
        draw_grid_label("ROW SCAN");
        ImGui::TextColored(white, "%u", state->row_scan);

        ImGui::TableNextRow();
        draw_grid_label("LINE CMP");
        ImGui::TextColored(white, "%u", video->GetLineCompare());
        draw_grid_label("OFFSET");
        ImGui::TextColored(white, "%u", crtc[k_video_cr_offset]);

        ImGui::TableNextRow();
        draw_grid_label("PIXEL PAN");
        ImGui::TextColored(white, "%u", state->attribute[k_video_ar_panning]);
        draw_grid_label("BYTE PAN");
        ImGui::TextColored(white, "%u", (crtc[k_video_cr_preset_row] >> 5) & 0x03);

        ImGui::TableNextRow();
        draw_grid_label("CURSOR");
        ImGui::TextColored(white, "$%04X", video->GetCursorAddress());
        draw_grid_label("SCANS");
        ImGui::TextColored(white, "%u-%u", crtc[k_video_cr_cursor_start] & 0x1F, crtc[k_video_cr_cursor_end] & 0x1F);

        ImGui::EndTable();
    }

    ImGui::NewLine(); ImGui::TextColored(cyan, "VIDEO MEMORY"); ImGui::Separator();

    if (ImGui::BeginTable("##vga_memory", 4, flags))
    {
        static const char* const k_windows[4] = { "A0000 128K", "A0000 64K", "B0000 32K", "B8000 32K" };

        setup_grid_columns();
        u8 memory_mode = state->sequencer[k_video_sr_memory_mode];

        ImGui::TableNextRow();
        draw_grid_label("WINDOW");
        ImGui::TextColored((state->misc & 0x02) != 0 ? white : gray, "%s",
            k_windows[(state->graphics[k_video_gr_misc] >> 2) & 0x03]);
        draw_grid_label("LATCH");
        ImGui::TextColored(white, "$%08X", state->latch);

        ImGui::TableNextRow();
        draw_grid_label("CHAIN-4");
        draw_flag((memory_mode & 0x08) != 0);
        draw_grid_label("ODD/EVEN");
        draw_flag((memory_mode & 0x04) == 0);

        ImGui::TableNextRow();
        draw_grid_label("WRITE MODE");
        ImGui::TextColored(white, "%u", state->graphics[k_video_gr_mode] & 0x03);
        draw_grid_label("READ MODE");
        ImGui::TextColored(white, "%u", (state->graphics[k_video_gr_mode] >> 3) & 0x01);

        ImGui::TableNextRow();
        draw_grid_label("MAP MASK");
        ImGui::TextColored(white, "$%X", state->sequencer[k_video_sr_map_mask]);
        draw_grid_label("DIRECT");
        draw_flag(video->IsDirectMapped());

        ImGui::EndTable();
    }

    ImGui::PopFont();

    ImGui::End();
    ImGui::PopStyleVar();
}

void gui_debug_window_vga_registers(void)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::SetNextWindowPos(ImVec2(197, 139), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(760, 470), ImGuiCond_FirstUseEver);
    ImGui::Begin("VGA Registers", &config_debug.show_vga_registers);

    ImGui::PushFont(gui_default_font);

    Video::Video_State* state = emu_get_core()->GetVideo()->GetState();

    ImGui::TextColored(violet, "MISC"); ImGui::SameLine();
    EditableRegister8(NULL, NULL, 0, state->misc, misc_write_callback, NULL, EditableRegisterFlags_ShowBinary);
    ImGui::TextColored(violet, "INDEX"); ImGui::SameLine();
    ImGui::TextColored(gray, "SR"); ImGui::SameLine();
    ImGui::TextColored(white, "$%02X", state->sequencer_index); ImGui::SameLine();
    ImGui::TextColored(gray, " GR"); ImGui::SameLine();
    ImGui::TextColored(white, "$%02X", state->graphics_index); ImGui::SameLine();
    ImGui::TextColored(gray, " CR"); ImGui::SameLine();
    ImGui::TextColored(white, "$%02X", state->crtc_index); ImGui::SameLine();
    ImGui::TextColored(gray, " AR"); ImGui::SameLine();
    ImGui::TextColored(white, "$%02X %s", state->attribute_index, state->attribute_data ? "DATA" : "INDEX");
    ImGui::Separator();

    if (ImGui::BeginTable("##vga_register_files", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit |
        ImGuiTableFlags_NoHostExtendX))
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        draw_register_file("SEQUENCER", "SR", k_debug_sequencer_register_names, state->sequencer, VIDEO_SEQUENCER_COUNT,
            sequencer_write_callback);
        ImGui::NewLine();
        draw_register_file("GRAPHICS", "GR", k_debug_graphics_register_names, state->graphics, VIDEO_GRAPHICS_COUNT,
            graphics_write_callback);
        ImGui::TableNextColumn();
        draw_register_file("CRTC", "CR", k_debug_crtc_register_names, state->crtc, VIDEO_CRTC_COUNT, crtc_write_callback);
        ImGui::TableNextColumn();
        draw_register_file("ATTRIBUTE", "AR", k_debug_attribute_register_names, state->attribute, VIDEO_ATTRIBUTE_COUNT,
            attribute_write_callback);
        ImGui::EndTable();
    }

    ImGui::PopFont();

    ImGui::End();
    ImGui::PopStyleVar();
}

void gui_debug_window_palettes(void)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::SetNextWindowPos(ImVec2(279, 173), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 491), ImGuiCond_FirstUseEver);
    ImGui::Begin("Palettes", &config_debug.show_palettes);

    ImGui::PushFont(gui_default_font);

    Video* video = emu_get_core()->GetVideo();
    Video::Video_State* state = video->GetState();
    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;

    ImGui::TextColored(violet, "PEL MASK"); ImGui::SameLine();
    ImGui::TextColored(white, "$%02X", state->pel_mask); ImGui::SameLine();
    ImGui::TextColored(violet, " WRITE"); ImGui::SameLine();
    ImGui::TextColored(white, "$%02X", state->dac_write_index); ImGui::SameLine();
    ImGui::TextColored(violet, " READ"); ImGui::SameLine();
    ImGui::TextColored(white, "$%02X", state->dac_read_index);

    ImGui::PopFont();

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(2.0f, 2.0f));

    if (ImGui::BeginTabBar("##palette_tabs"))
    {
        if (ImGui::BeginTabItem("ATTRIBUTE"))
        {
            ImGui::PushFont(gui_default_font);

            if (ImGui::BeginTable("##palette_attribute", 17, flags))
            {
                ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("00").x);

                for (int i = 0; i < 16; i++)
                    ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());

                draw_palette_indices();
                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                for (int i = 0; i < 16; i++)
                {
                    ImGui::TableNextColumn();
                    draw_color(i, video->GetAttributeColor(i), NULL);
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(violet, "AR");

                for (int i = 0; i < 16; i++)
                {
                    ImGui::TableNextColumn();
                    ImGui::TextColored(white, "%02X", state->attribute[i]);
                }

                ImGui::EndTable();
            }

            ImGui::PopFont();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("DAC"))
        {
            ImGui::PushFont(gui_default_font);

            if (ImGui::BeginTable("##palette_dac", 17, flags))
            {
                ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("00").x);

                for (int i = 0; i < 16; i++)
                    ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());

                draw_palette_indices();

                for (int row = 0; row < 16; row++)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextColored(orange, "%02X", row * 16);

                    for (int i = 0; i < 16; i++)
                    {
                        int index = row * 16 + i;
                        ImGui::TableNextColumn();
                        draw_color(index, video->GetDACColor(index), state->dac[index]);
                    }
                }

                ImGui::EndTable();
            }

            ImGui::PopFont();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::PopStyleVar();

    ImGui::End();
    ImGui::PopStyleVar();
}

static void setup_grid_columns(void)
{
    float character = ImGui::CalcTextSize("0").x;

    ImGui::TableSetupColumn(" ", ImGuiTableColumnFlags_WidthFixed, character * 11);
    ImGui::TableSetupColumn(" ", ImGuiTableColumnFlags_WidthFixed, character * 16);
    ImGui::TableSetupColumn(" ", ImGuiTableColumnFlags_WidthFixed, character * 10);
    ImGui::TableSetupColumn(" ", ImGuiTableColumnFlags_WidthFixed, character * 14);
}

static void draw_grid_label(const char* label)
{
    ImGui::TableNextColumn();
    ImGui::TextColored(violet, "%s", label);
    ImGui::TableNextColumn();
}

static void draw_flag(bool on)
{
    ImGui::TextColored(on ? green : gray, "%s", on ? "ON" : "OFF");
}

static void write_register(Video::Video_Register_File file, u16 address, u8 value)
{
    GeardoxCore* core = emu_get_core();
    core->GetVideo()->WriteRegister(file, (u8)address, value, core->GetScheduler()->GetClocks());
}

static void sequencer_write_callback(u16 address, u8 value, void* user_data)
{
    UNUSED(user_data);
    write_register(Video::VIDEO_REGISTERS_SEQUENCER, address, value);
}

static void graphics_write_callback(u16 address, u8 value, void* user_data)
{
    UNUSED(user_data);
    write_register(Video::VIDEO_REGISTERS_GRAPHICS, address, value);
}

static void attribute_write_callback(u16 address, u8 value, void* user_data)
{
    UNUSED(user_data);
    write_register(Video::VIDEO_REGISTERS_ATTRIBUTE, address, value);
}

static void crtc_write_callback(u16 address, u8 value, void* user_data)
{
    UNUSED(user_data);
    write_register(Video::VIDEO_REGISTERS_CRTC, address, value);
}

static void misc_write_callback(u16 address, u8 value, void* user_data)
{
    UNUSED(user_data);
    write_register(Video::VIDEO_REGISTERS_MISC, address, value);
}

static void draw_register_file(const char* title, const char* prefix, const char* const* names, const u8* values, int count,
    RegisterWriteCallback8 callback)
{
    ImGui::TextColored(cyan, "%s", title);

    for (int i = 0; i < count; i++)
    {
        ImGui::PushID(title);
        ImGui::TextColored(cyan, "%s%02X", prefix, i); ImGui::SameLine();
        ImGui::TextColored(violet, "%-11s", names[i]); ImGui::SameLine();
        EditableRegister8(NULL, NULL, (u16)i, values[i], callback, NULL, EditableRegisterFlags_None);
        ImGui::PopID();
    }
}

static void draw_palette_indices(void)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();

    for (int i = 0; i < 16; i++)
    {
        ImGui::TableNextColumn();
        ImGui::TextColored(orange, " %X", i);
    }
}

static void draw_color(int index, u32 color, const u8* components)
{
    float size = ImGui::GetFrameHeight();

    ImGui::PushID(index);
    ImGui::ColorButton("##color", to_imgui_color(color), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
        ImVec2(size, size));

    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::TextColored(cyan, "INDEX $%02X", index);

        if (IsValidPointer(components))
            ImGui::Text("R $%02X  G $%02X  B $%02X", components[0], components[1], components[2]);

        ImGui::Text("RGB888 #%06X", color & 0xFFFFFF);
        ImGui::EndTooltip();
    }

    ImGui::PopID();
}

static ImVec4 to_imgui_color(u32 color)
{
    return ImVec4(((color >> 16) & 0xFF) / 255.0f, ((color >> 8) & 0xFF) / 255.0f, (color & 0xFF) / 255.0f, 1.0f);
}
