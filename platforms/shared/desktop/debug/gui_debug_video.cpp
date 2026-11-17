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
#include "system/machine_profiles.h"
#include "system/scheduler.h"
#include "video/video.h"
#include "video/mc6845.h"
#include "../config.h"
#include "../emu.h"
#include "../gui.h"
#include "../gui_colors.h"
#include "gui_debug_constants.h"
#include "gui_debug_widgets.h"

static void setup_grid_columns(void);
static void draw_grid_label(const char* label);
static void draw_flag(bool on);
static void sequencer_write_callback(u16 address, u8 value, void* user_data);
static void graphics_write_callback(u16 address, u8 value, void* user_data);
static void attribute_write_callback(u16 address, u8 value, void* user_data);
static void crtc_write_callback(u16 address, u8 value, void* user_data);
static void misc_write_callback(u16 address, u8 value, void* user_data);
static void draw_register_file(const char* title, const char* prefix, const char* const* names, const u8* values, int count,
    RegisterWriteCallback8 callback, Video* video, int first = 0);
static void draw_cirrus(Video* video);
static void draw_cirrus_blt(Video* video);
static void draw_cirrus_cursor(Video* video);
static void draw_ega(Video* video);
static void draw_cga(Video* video);
static void mc6845_write_callback(u16 address, u8 value, void* user_data);
static void cga_write_callback(u16 address, u8 value, void* user_data);
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

    if (video->IsCGA())
    {
        draw_cga(video);
        ImGui::PopFont();
        ImGui::End();
        ImGui::PopStyleVar();
        return;
    }

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

    if (video->IsCirrus())
        draw_cirrus(video);

    if (video->IsEGA())
        draw_ega(video);

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

    Video* video = emu_get_core()->GetVideo();
    Video::Video_State* state = video->GetState();

    if (video->IsCGA())
    {
        MC6845::MC6845_State* crtc = video->GetMC6845()->GetState();
        u8 cga[2] = { state->cga_mode, state->cga_color };

        ImGui::TextColored(violet, "6845 INDEX"); ImGui::SameLine();
        ImGui::TextColored(white, "$%02X", crtc->index);
        ImGui::Separator();

        if (ImGui::BeginTable("##cga_register_files", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit |
            ImGuiTableFlags_NoHostExtendX))
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            draw_register_file("6845", "R", k_debug_mc6845_register_names, crtc->registers, MC6845_REGISTER_COUNT,
                mc6845_write_callback, video);
            ImGui::TableNextColumn();
            draw_register_file("CARD (3D8h, 3D9h)", "P", k_debug_cga_register_names, cga, 2, cga_write_callback, video);
            ImGui::EndTable();
        }

        ImGui::PopFont();
        ImGui::End();
        ImGui::PopStyleVar();
        return;
    }

    ImGui::TextColored(violet, "MISC"); ImGui::SameLine();
    EditableRegister8(NULL, NULL, 0, state->misc, misc_write_callback, video, EditableRegisterFlags_ShowBinary);
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
            sequencer_write_callback, video);
        ImGui::NewLine();
        draw_register_file("GRAPHICS", "GR", k_debug_graphics_register_names, state->graphics, VIDEO_GRAPHICS_COUNT,
            graphics_write_callback, video);
        ImGui::TableNextColumn();
        draw_register_file("CRTC", "CR", k_debug_crtc_register_names, state->crtc, VIDEO_CRTC_COUNT, crtc_write_callback, video);
        ImGui::TableNextColumn();
        draw_register_file("ATTRIBUTE", "AR", k_debug_attribute_register_names, state->attribute, VIDEO_ATTRIBUTE_COUNT,
            attribute_write_callback, video);
        ImGui::EndTable();
    }

    if (video->IsCirrus() && ImGui::BeginTable("##vga_cirrus_register_files", 3, ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX))
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        draw_register_file("CIRRUS SEQUENCER", "SR", k_debug_cirrus_sequencer_register_names, state->sequencer, 27,
            sequencer_write_callback, video, 0x05);
        ImGui::TableNextColumn();
        draw_register_file("CIRRUS GRAPHICS", "GR", k_debug_cirrus_graphics_register_names, state->graphics, 9,
            graphics_write_callback, video, 0x09);
        ImGui::NewLine();
        draw_register_file("CIRRUS CRTC", "CR", k_debug_cirrus_crtc_register_names, state->crtc, 5, crtc_write_callback,
            video, 0x19);
        draw_register_file(NULL, "CR", k_debug_cirrus_id_register_names, state->crtc, 3, crtc_write_callback, video, 0x25);
        ImGui::TableNextColumn();
        draw_register_file("CIRRUS BITBLT", "GR", k_debug_cirrus_blt_register_names, state->graphics, 26,
            graphics_write_callback, video, 0x20);
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

    if (video->IsCGA())
    {
        ImGui::TextColored(violet, "COLOR"); ImGui::SameLine();
        ImGui::TextColored(white, "$%02X", state->cga_color); ImGui::SameLine();
        ImGui::TextColored(violet, " OUTPUT"); ImGui::SameLine();
        ImGui::TextColored(white, "%s", video->IsCGAComposite() ? "COMPOSITE" : "RGB");
    }
    else if (video->IsEGA())
    {
        ImGui::TextColored(violet, "MONITOR"); ImGui::SameLine();
        ImGui::TextColored(white, "%s", video->IsEGA350Lines() ? "350 LINES, 64 COLORS" : "200 LINES, 16 COLORS");
    }
    else
    {
        ImGui::TextColored(violet, "PEL MASK"); ImGui::SameLine();
        ImGui::TextColored(white, "$%02X", state->pel_mask); ImGui::SameLine();
        ImGui::TextColored(violet, " ADDRESS"); ImGui::SameLine();
        ImGui::TextColored(white, "$%02X", state->dac_address); ImGui::SameLine();
        ImGui::TextColored(violet, " INDEX"); ImGui::SameLine();
        ImGui::TextColored(white, "$%02X", state->dac_index);
    }

    ImGui::PopFont();

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(2.0f, 2.0f));

    if (video->IsCGA() && ImGui::BeginTabBar("##cga_palette_tabs"))
    {
        if (ImGui::BeginTabItem("IRGB"))
        {
            ImGui::PushFont(gui_default_font);

            if (ImGui::BeginTable("##palette_cga", 17, flags))
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
                    draw_color(i, video->GetCGAColor(i), NULL);
                }

                ImGui::EndTable();
            }

            ImGui::PopFont();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
    else if (!video->IsCGA() && ImGui::BeginTabBar("##palette_tabs"))
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

        if (ImGui::BeginTabItem(video->IsEGA() ? "MONITOR" : "DAC"))
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
                        draw_color(index, video->GetDACColor(index), video->IsEGA() ? NULL : state->dac[index]);
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

static void sequencer_write_callback(u16 address, u8 value, void* user_data)
{
    Video* video = (Video*)user_data;
    u64 clocks = emu_get_core()->GetScheduler()->GetClocks();
    u8 saved = video->GetState()->sequencer_index;

    video->Write(0x3C4, (u8)address, clocks);
    video->Write(0x3C5, value, clocks);
    video->Write(0x3C4, saved, clocks);
}

static void graphics_write_callback(u16 address, u8 value, void* user_data)
{
    Video* video = (Video*)user_data;
    u64 clocks = emu_get_core()->GetScheduler()->GetClocks();
    u8 saved = video->GetState()->graphics_index;

    video->Write(0x3CE, (u8)address, clocks);
    video->Write(0x3CF, value, clocks);
    video->Write(0x3CE, saved, clocks);
}

// The flip-flop and the palette access bit stay as the program left them
static void attribute_write_callback(u16 address, u8 value, void* user_data)
{
    Video* video = (Video*)user_data;
    Video::Video_State* state = video->GetState();
    u64 clocks = emu_get_core()->GetScheduler()->GetClocks();
    u8 saved = state->attribute_index;
    bool data = state->attribute_data;

    state->attribute_data = false;
    video->Write(0x3C0, (u8)address, clocks);
    video->Write(0x3C0, value, clocks);
    video->Write(0x3C0, saved, clocks);
    state->attribute_data = data;
}

static void crtc_write_callback(u16 address, u8 value, void* user_data)
{
    Video* video = (Video*)user_data;
    Video::Video_State* state = video->GetState();
    u64 clocks = emu_get_core()->GetScheduler()->GetClocks();
    u16 port = (state->misc & 0x01) != 0 ? 0x3D4 : 0x3B4;
    u8 saved = state->crtc_index;

    video->Write(port, (u8)address, clocks);
    video->Write((u16)(port + 1), value, clocks);
    video->Write(port, saved, clocks);
}

static void mc6845_write_callback(u16 address, u8 value, void* user_data)
{
    Video* video = (Video*)user_data;
    u64 clocks = emu_get_core()->GetScheduler()->GetClocks();
    u8 saved = video->GetMC6845()->GetIndex();

    video->Write(0x3D4, (u8)address, clocks);
    video->Write(0x3D5, value, clocks);
    video->Write(0x3D4, saved, clocks);
}

static void cga_write_callback(u16 address, u8 value, void* user_data)
{
    ((Video*)user_data)->Write((u16)(0x3D8 + address), value, emu_get_core()->GetScheduler()->GetClocks());
}

static void misc_write_callback(u16 address, u8 value, void* user_data)
{
    UNUSED(address);
    ((Video*)user_data)->Write(0x3C2, value, emu_get_core()->GetScheduler()->GetClocks());
}

static void draw_register_file(const char* title, const char* prefix, const char* const* names, const u8* values, int count,
    RegisterWriteCallback8 callback, Video* video, int first)
{
    if (IsValidPointer(title))
        ImGui::TextColored(cyan, "%s", title);

    for (int i = 0; i < count; i++)
    {
        ImGui::PushID(prefix);
        ImGui::TextColored(cyan, "%s%02X", prefix, first + i); ImGui::SameLine();
        ImGui::TextColored(violet, "%-11s", names[i]); ImGui::SameLine();
        EditableRegister8(NULL, NULL, (u16)(first + i), values[first + i], callback, video, EditableRegisterFlags_None);
        ImGui::PopID();
    }
}

static void draw_cirrus(Video* video)
{
    Video::Video_State* state = video->GetState();
    u8 extended = state->sequencer[k_video_sr_extended];
    u32 aperture = video->GetApertureBase();
    int selected = (state->misc >> 2) & 0x03;
    u32 pitch = video->GetOffsetRegister() * 8;
    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;

    ImGui::NewLine(); ImGui::TextColored(cyan, "CIRRUS"); ImGui::Separator();
    ImGui::TextColored(violet, "CARD"); ImGui::SameLine();
    ImGui::TextColored(white, "%s, %u KB", k_video_cards[video->GetCard()].name, video->GetVRAMSize() / 1024);

    if (ImGui::BeginTable("##vga_cirrus", 4, flags))
    {
        setup_grid_columns();

        ImGui::TableNextRow();
        draw_grid_label("EXTENSIONS");
        ImGui::TextColored(video->IsUnlocked() ? green : gray, "%s", video->IsUnlocked() ? "UNLOCKED" : "LOCKED");
        draw_grid_label("EXT MODE");
        draw_flag(video->IsExtendedMode());

        ImGui::TableNextRow();
        draw_grid_label("SR07");
        ImGui::TextColored(white, "$%02X", extended);
        draw_grid_label("PIXEL PATH");
        ImGui::TextColored(white, "%u", (extended >> 1) & 0x07);

        ImGui::TableNextRow();
        draw_grid_label("APERTURE");

        if (aperture != 0)
            ImGui::TextColored(white, "$%08X", aperture);
        else
            ImGui::TextColored(gray, "OFF");

        draw_grid_label("APERT MB");
        ImGui::TextColored(white, "%u", extended >> 4);

        for (int i = 0; i < 4; i += 2)
        {
            char name[16];

            ImGui::TableNextRow();
            snprintf(name, sizeof(name), "VCLK%d", i);
            draw_grid_label(name);
            ImGui::TextColored(selected == i ? orange : white, "%.3f MHz", video->GetVideoClock(i) / 1000000.0);
            snprintf(name, sizeof(name), "VCLK%d", i + 1);
            draw_grid_label(name);
            ImGui::TextColored(selected == i + 1 ? orange : white, "%.3f MHz", video->GetVideoClock(i + 1) / 1000000.0);
        }

        ImGui::TableNextRow();
        draw_grid_label("BANK 0");
        ImGui::TextColored(white, "$%05X", video->GetBankOffset(0));
        draw_grid_label("BANK 1");
        ImGui::TextColored(video->IsDualPage() ? white : gray, "$%05X", video->GetBankOffset(1));

        ImGui::TableNextRow();
        draw_grid_label("DUAL PAGE");
        draw_flag(video->IsDualPage());
        draw_grid_label("WIDE COUNT");
        draw_flag(video->GetCounterMask() != 0xFFFF);

        ImGui::TableNextRow();
        draw_grid_label("HIDDEN DAC");
        ImGui::TextColored(white, "$%02X", state->hidden_dac);
        draw_grid_label("FORMAT");
        ImGui::TextColored(blue, "%s", k_debug_extended_format_names[video->GetExtendedFormat()]);

        ImGui::TableNextRow();
        draw_grid_label("EXT START");
        ImGui::TextColored(white, "$%05X", video->GetStartAddress());
        draw_grid_label("EXT OFFSET");
        ImGui::TextColored(white, "%u (%u B)", video->GetOffsetRegister(), pitch);

        ImGui::EndTable();
    }

    ImGui::TextColored(violet, "MODE"); ImGui::SameLine();

    if (!state->running)
        ImGui::TextColored(gray, "--");
    else if (video->IsExtendedMode())
        ImGui::TextColored(white, "%dx%d, %u BPP, %u BYTES PER ROW, %.2f Hz", video->GetFrameWidth(),
            video->GetFrameHeight(), video->GetExtendedBytesPerPixel() * 8, pitch, 1000.0 / video->GetFrameTime());
    else
        ImGui::TextColored(white, "%dx%d, %s, %.2f Hz", video->GetFrameWidth(), video->GetFrameHeight(), mode_name(video),
            1000.0 / video->GetFrameTime());

    draw_cirrus_blt(video);
    draw_cirrus_cursor(video);
}

static void draw_cirrus_blt(Video* video)
{
    Video::Video_State* state = video->GetState();
    const u8* graphics = state->graphics;
    u8 status = graphics[k_video_gr_blt_status];
    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;

    ImGui::NewLine(); ImGui::TextColored(cyan, "BITBLT"); ImGui::Separator();

    if (!ImGui::BeginTable("##vga_cirrus_blt", 4, flags))
        return;

    setup_grid_columns();

    ImGui::TableNextRow();
    draw_grid_label("STATE");

    if (video->IsBltWaiting())
        ImGui::TextColored(yellow, "WAITING CPU");
    else if ((status & 0x01) != 0)
        ImGui::TextColored(orange, "BUSY");
    else
        ImGui::TextColored(gray, "IDLE");

    draw_grid_label("GR31");
    ImGui::TextColored(white, "$%02X", status);

    ImGui::TableNextRow();
    draw_grid_label("MODE");
    ImGui::TextColored(white, "$%02X", graphics[k_video_gr_blt_mode]);
    draw_grid_label("ROP");
    ImGui::TextColored(white, "$%02X", graphics[k_video_gr_blt_rop]);

    ImGui::TableNextRow();
    draw_grid_label("SIZE");
    ImGui::TextColored(white, "%ux%u", video->GetBltWidth(), video->GetBltHeight());
    draw_grid_label("PIXEL");
    ImGui::TextColored(white, "%u BYTES", video->GetBltPixelBytes());

    ImGui::TableNextRow();
    draw_grid_label("SOURCE");
    ImGui::TextColored(white, "$%06X", video->GetBltAddress(k_video_gr_blt_src));
    draw_grid_label("DEST");
    ImGui::TextColored(white, "$%06X", video->GetBltAddress(k_video_gr_blt_dst));

    ImGui::TableNextRow();
    draw_grid_label("SRC PITCH");
    ImGui::TextColored(white, "%u", video->GetBltPitch(k_video_gr_blt_src_pitch));
    draw_grid_label("DST PITCH");
    ImGui::TextColored(white, "%u", video->GetBltPitch(k_video_gr_blt_dst_pitch));

    ImGui::TableNextRow();
    draw_grid_label("LINE");
    ImGui::TextColored(video->IsBltWaiting() ? white : gray, "%u", state->blt_line);
    draw_grid_label("BYTES FED");
    ImGui::TextColored(video->IsBltWaiting() ? white : gray, "%u/%u", state->blt_fed, state->blt_count);

    ImGui::TableNextRow();
    draw_grid_label("STARTED");
    ImGui::TextColored(white, "%u", video->GetBltCount());
    draw_grid_label("HANDLERS");
    draw_flag(video->IsBltMapped());

    ImGui::TableNextRow();
    draw_grid_label("FRAME COPY");
    ImGui::TextColored(white, "%u", video->GetBltFrameCount(Video::VIDEO_BLT_SCREEN));
    draw_grid_label("FRAME PAT");
    ImGui::TextColored(white, "%u", video->GetBltFrameCount(Video::VIDEO_BLT_PATTERN));

    ImGui::TableNextRow();
    draw_grid_label("FRAME EXP");
    ImGui::TextColored(white, "%u", video->GetBltFrameCount(Video::VIDEO_BLT_EXPAND));
    draw_grid_label("FRAME CPU");
    ImGui::TextColored(white, "%u", video->GetBltFrameCount(Video::VIDEO_BLT_SYSTEM));

    ImGui::TableNextRow();
    draw_grid_label("REMAPS");
    ImGui::TextColored(white, "%u", video->GetBltFrameRemaps());

    ImGui::EndTable();
}

static void draw_cirrus_cursor(Video* video)
{
    Video::Video_State* state = video->GetState();
    u8 attributes = state->sequencer[k_video_sr_cursor_attributes];
    u32 size = video->GetCursorSize();
    u32 base = video->GetCursorPatternAddress();
    u32 mask = video->GetVRAMSize() - 1;
    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;

    ImGui::NewLine(); ImGui::TextColored(cyan, "HARDWARE CURSOR"); ImGui::Separator();

    if (ImGui::BeginTable("##vga_cirrus_cursor", 4, flags))
    {
        setup_grid_columns();

        ImGui::TableNextRow();
        draw_grid_label("ENABLED");
        draw_flag((attributes & 0x01) != 0);
        draw_grid_label("SIZE");
        ImGui::TextColored(white, "%ux%u", size, size);

        ImGui::TableNextRow();
        draw_grid_label("X");
        ImGui::TextColored(white, "%u", state->cursor_x);
        draw_grid_label("Y");
        ImGui::TextColored(white, "%u", state->cursor_y);

        ImGui::TableNextRow();
        draw_grid_label("PATTERN");
        ImGui::TextColored(white, "$%06X", base);
        draw_grid_label("COLORS");

        for (int i = 0; i < 2; i++)
        {
            char id[16];
            snprintf(id, sizeof(id), "##cursor_color%d", i);

            if (i != 0)
                ImGui::SameLine();

            ImGui::ColorButton(id, to_imgui_color(video->GetCursorColor(i)), ImGuiColorEditFlags_NoTooltip |
                ImGuiColorEditFlags_NoDragDrop, ImVec2(ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight()));
        }

        ImGui::EndTable();
    }

    float scale = size == 64 ? 2.0f : 4.0f;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImU32 transparent = ImGui::GetColorU32(ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
    ImU32 invert = ImGui::GetColorU32(ImVec4(1.0f, 0.0f, 1.0f, 1.0f));
    ImU32 color0 = ImGui::GetColorU32(to_imgui_color(video->GetCursorColor(0)));
    ImU32 color1 = ImGui::GetColorU32(to_imgui_color(video->GetCursorColor(1)));

    for (u32 line = 0; line < size; line++)
    {
        u32 plane0 = size == 64 ? base + (line * 16) : base + (line * 4);
        u32 plane1 = size == 64 ? plane0 + 8 : plane0 + 128;

        for (u32 pixel = 0; pixel < size; pixel++)
        {
            u8 bit = (u8)(0x80 >> (pixel & 0x07));
            bool b0 = (state->vram[(plane0 + (pixel >> 3)) & mask] & bit) != 0;
            bool b1 = (state->vram[(plane1 + (pixel >> 3)) & mask] & bit) != 0;
            ImU32 color = b0 && b1 ? color1 : (b1 ? color0 : (b0 ? invert : transparent));
            ImVec2 p0(origin.x + (pixel * scale), origin.y + (line * scale));
            draw_list->AddRectFilled(p0, ImVec2(p0.x + scale, p0.y + scale), color);
        }
    }

    ImGui::Dummy(ImVec2(size * scale, size * scale));
}

static void draw_ega(Video* video)
{
    static const char* const k_mux[4] = { "R, B", "b, G", "r, g", "NONE" };
    Video::Video_State* state = video->GetState();
    u8 switches = video->GetEGASwitches();
    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;

    ImGui::NewLine(); ImGui::TextColored(cyan, "EGA"); ImGui::Separator();
    ImGui::TextColored(violet, "CARD"); ImGui::SameLine();
    ImGui::TextColored(white, "%s, %u KB", k_video_cards[video->GetCard()].name, video->GetVRAMSize() / 1024);

    if (ImGui::BeginTable("##vga_ega", 4, flags))
    {
        setup_grid_columns();

        ImGui::TableNextRow();
        draw_grid_label("MONITOR");
        ImGui::TextColored(white, "%s", video->IsEGA350Lines() ? "350 LINES" : "200 LINES");
        draw_grid_label("SWITCHES");
        ImGui::TextColored(white, "%X (%d%d%d%d)", switches, (switches >> 3) & 1, (switches >> 2) & 1, (switches >> 1) & 1,
            switches & 1);

        ImGui::TableNextRow();
        draw_grid_label("SENSE");
        ImGui::TextColored(white, "SWITCH %d", 4 - ((state->misc >> 2) & 0x03));
        draw_grid_label("STATUS MUX");
        ImGui::TextColored(white, "%s", k_mux[(state->attribute[k_video_ar_plane_enable] >> 4) & 0x03]);

        ImGui::TableNextRow();
        draw_grid_label("GR POS 1");
        ImGui::TextColored(white, "%u", state->ega_position[0]);
        draw_grid_label("GR POS 2");
        ImGui::TextColored(white, "%u", state->ega_position[1]);

        ImGui::EndTable();
    }
}

static void draw_cga(Video* video)
{
    static const char* const k_cursor_modes[4] = { "BLINK", "OFF", "BLINK", "BLINK" };
    Video::Video_State* state = video->GetState();
    MC6845* mc6845 = video->GetMC6845();
    const u8* r = mc6845->GetState()->registers;
    u8 mode = state->cga_mode;
    u8 color = state->cga_color;
    bool running = state->running;
    u64 clocks = emu_get_core()->GetScheduler()->GetClocks();
    double dot_clock = state->frame_dot_clock / 1000000.0;
    const char* name = (mode & 0x02) == 0 ? ((mode & 0x01) != 0 ? "80X25 TEXT" : "40X25 TEXT") :
        ((mode & 0x10) != 0 ? "640X200" : "320X200");
    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;

    ImGui::TextColored(violet, "CARD"); ImGui::SameLine();
    ImGui::TextColored(white, "%s, %u KB", k_video_cards[video->GetCard()].name, video->GetVRAMSize() / 1024);
    ImGui::NewLine(); ImGui::TextColored(cyan, "DISPLAY"); ImGui::Separator();

    if (ImGui::BeginTable("##cga_display", 4, flags))
    {
        setup_grid_columns();

        ImGui::TableNextRow();
        draw_grid_label("RUNNING");
        ImGui::TextColored(running ? green : gray, "%s", running ? "ON" : "OFF");
        draw_grid_label("VIDEO");
        draw_flag((mode & 0x08) != 0);

        ImGui::TableNextRow();
        draw_grid_label("MODE");
        ImGui::TextColored(blue, "%s", name);
        draw_grid_label("OUTPUT");
        ImGui::TextColored(white, "%s", video->IsCGAComposite() ? "COMPOSITE" : "RGB");

        ImGui::TableNextRow();
        draw_grid_label("FRAME");
        ImGui::TextColored(white, "%dx%d", video->GetFrameWidth(), video->GetFrameHeight());
        draw_grid_label("DISPLAY");
        ImGui::TextColored(white, "%dx%d", video->GetDisplayWidth(), video->GetDisplayHeight());

        ImGui::TableNextRow();
        draw_grid_label("LINE RATE");

        if (running)
            ImGui::TextColored(orange, "%.2f kHz", dot_clock * 1000.0 / state->frame_line_dots);
        else
            ImGui::TextColored(gray, "--");

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
        draw_grid_label("LINE");
        ImGui::TextColored(white, "%u DOTS", state->frame_line_dots);
        draw_grid_label("RETRACE");
        ImGui::TextColored(white, "%u-%u", state->retrace_start, state->retrace_end);

        ImGui::EndTable();
    }

    ImGui::NewLine(); ImGui::TextColored(cyan, "BEAM"); ImGui::Separator();

    if (ImGui::BeginTable("##cga_beam", 4, flags))
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
        draw_grid_label("ADDRESS");
        ImGui::TextColored(white, "$%04X", state->row_address & 0x3FFF);
        draw_grid_label("ROW SCAN");
        ImGui::TextColored(white, "%u", state->row_scan);

        ImGui::EndTable();
    }

    ImGui::NewLine(); ImGui::TextColored(cyan, "6845"); ImGui::Separator();

    if (ImGui::BeginTable("##cga_6845", 4, flags))
    {
        setup_grid_columns();

        ImGui::TableNextRow();
        draw_grid_label("H TOTAL");
        ImGui::TextColored(white, "%u CHARS", r[k_mc6845_horizontal_total] + 1);
        draw_grid_label("H DISPLAY");
        ImGui::TextColored(white, "%u CHARS", r[k_mc6845_horizontal_displayed]);

        ImGui::TableNextRow();
        draw_grid_label("H SYNC");
        ImGui::TextColored(white, "%u, %u WIDE", r[k_mc6845_horizontal_sync], r[k_mc6845_sync_width]);
        draw_grid_label("ROW");
        ImGui::TextColored(white, "%u LINES", r[k_mc6845_max_scan] + 1);

        ImGui::TableNextRow();
        draw_grid_label("V TOTAL");
        ImGui::TextColored(white, "%u ROWS + %u", r[k_mc6845_vertical_total] + 1, r[k_mc6845_vertical_adjust]);
        draw_grid_label("V DISPLAY");
        ImGui::TextColored(white, "%u ROWS", r[k_mc6845_vertical_displayed]);

        ImGui::TableNextRow();
        draw_grid_label("V SYNC");
        ImGui::TextColored(white, "ROW %u", r[k_mc6845_vertical_sync]);
        draw_grid_label("START");
        ImGui::TextColored(white, "$%04X", mc6845->GetStartAddress());

        ImGui::TableNextRow();
        draw_grid_label("CURSOR");
        ImGui::TextColored(white, "$%04X", mc6845->GetCursorAddress());
        draw_grid_label("SCANS");
        ImGui::TextColored(white, "%u-%u %s", r[k_mc6845_cursor_start] & 0x1F, r[k_mc6845_cursor_end],
            k_cursor_modes[(r[k_mc6845_cursor_start] >> 5) & 0x03]);

        ImGui::EndTable();
    }

    ImGui::NewLine(); ImGui::TextColored(cyan, "MODE $%02X, COLOR $%02X", mode, color); ImGui::Separator();

    if (ImGui::BeginTable("##cga_registers", 4, flags))
    {
        setup_grid_columns();

        ImGui::TableNextRow();
        draw_grid_label("80 COLUMNS");
        draw_flag((mode & 0x01) != 0);
        draw_grid_label("GRAPHICS");
        draw_flag((mode & 0x02) != 0);

        ImGui::TableNextRow();
        draw_grid_label("B&W");
        draw_flag((mode & 0x04) != 0);
        draw_grid_label("640X200");
        draw_flag((mode & 0x10) != 0);

        ImGui::TableNextRow();
        draw_grid_label("BLINK");
        draw_flag((mode & 0x20) != 0);
        draw_grid_label("COLOR");
        ImGui::ColorButton("##cga_color", to_imgui_color(video->GetCGAColor(color & 0x0F)), ImGuiColorEditFlags_NoTooltip |
            ImGuiColorEditFlags_NoDragDrop, ImVec2(ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight()));

        ImGui::TableNextRow();
        draw_grid_label("INTENSE");
        draw_flag((color & 0x10) != 0);
        draw_grid_label("PALETTE");
        ImGui::TextColored(white, "%u", (color >> 5) & 0x01);

        ImGui::EndTable();
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
