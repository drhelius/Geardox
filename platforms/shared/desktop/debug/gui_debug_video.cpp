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
#include "video/voodoo.h"
#include "video/voodoo_dac.h"
#include "video/voodoo_trex.h"
#include "../config.h"
#include "../emu.h"
#include "../gui.h"
#include "../gui_colors.h"
#include "../ogl_renderer.h"
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
static void draw_voodoo_status(Voodoo* voodoo);
static void draw_voodoo_pipeline(Voodoo* voodoo);
static void draw_voodoo_buffers(Voodoo* voodoo);
static void draw_voodoo_hex(const char* label, u32 value);

static const char* const k_voodoo_functions[8] = { "NEVER", "<", "==", "<=", ">", "!=", ">=", "ALWAYS" };
static const char* const k_voodoo_formats[16] = { "RGB332", "YIQ422", "A8", "I8", "AI44", "P8", "RSVD6", "RSVD7",
    "ARGB8332", "AYIQ8422", "RGB565", "ARGB1555", "ARGB4444", "AI88", "AP88", "RSVD15" };
static const char* const k_voodoo_blend[16] = { "ZERO", "SRC_A", "COLOR", "DST_A", "ONE", "1-SRC_A", "1-COLOR",
    "1-DST_A", "RSVD8", "RSVD9", "RSVD10", "RSVD11", "RSVD12", "RSVD13", "RSVD14", "SAT/PREFOG" };
static const char* const k_voodoo_rgb_select[4] = { "ITERATED", "TEXTURE", "COLOR1", "LFB" };

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

void gui_debug_window_voodoo(void)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::SetNextWindowPos(ImVec2(180, 120), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(560, 620), ImGuiCond_FirstUseEver);
    ImGui::Begin("Voodoo", &config_debug.show_voodoo);

    Video* video = emu_get_core()->GetVideo();

    if (!video->HasVoodoo())
    {
        ImGui::TextColored(gray, "The 3dfx Voodoo is off");
        ImGui::End();
        ImGui::PopStyleVar();
        return;
    }

    Voodoo* voodoo = video->GetVoodoo();
    voodoo->SyncRender();

    if (ImGui::BeginTabBar("##voodoo_tabs"))
    {
        if (ImGui::BeginTabItem("STATUS"))
        {
            ImGui::PushFont(gui_default_font);
            draw_voodoo_status(voodoo);
            ImGui::PopFont();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("PIPELINE"))
        {
            ImGui::PushFont(gui_default_font);
            draw_voodoo_pipeline(voodoo);
            ImGui::PopFont();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("BUFFERS"))
        {
            draw_voodoo_buffers(voodoo);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

static void draw_voodoo_hex(const char* label, u32 value)
{
    draw_grid_label(label);
    ImGui::TextColored(white, "$%08X", value);
}

static void draw_voodoo_status(Voodoo* voodoo)
{
    const Voodoo::Voodoo_State* state = voodoo->GetState();
    const Voodoo::Voodoo_Video* timing = voodoo->GetVideo();
    const u32* regs = state->regs;
    u64 clocks = emu_get_core()->GetScheduler()->GetClocks();
    u32 status = voodoo->GetStatus(clocks);
    u64 busy = voodoo->GetBusyClocks();
    float space = ImGui::CalcTextSize(" ").x;
    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;

    ImGui::TextColored(cyan, "BOARD"); ImGui::Separator();

    if (ImGui::BeginTable("##voodoo_board", 4, flags))
    {
        setup_grid_columns();

        ImGui::TableNextRow();
        draw_grid_label("WINDOW");
        ImGui::TextColored(white, "$%08X", state->pci_base);
        draw_grid_label("PCI CMD");
        ImGui::TextColored(white, "$%02X", state->pci_command); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((state->pci_command & 0x02) != 0 ? green : gray, "MEM");

        ImGui::TableNextRow();
        draw_grid_label("INITENABLE");
        ImGui::TextColored(white, "$%02X", state->init_enable); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((state->init_enable & k_voodoo_enable_init_writes) != 0 ? green : gray, "INIT");
        ImGui::SameLine(0.0f, space);
        ImGui::TextColored((state->init_enable & k_voodoo_enable_fifo_writes) != 0 ? green : gray, "FIFO");
        ImGui::SameLine(0.0f, space);
        ImGui::TextColored((state->init_enable & k_voodoo_enable_dac_read) != 0 ? yellow : gray, "DAC");
        draw_grid_label("OUTPUT");
        ImGui::TextColored(voodoo->IsDisplayed() ? green : gray, "%s", voodoo->IsDisplayed() ? "VOODOO" : "VGA");

        ImGui::TableNextRow();
        draw_grid_label("STATUS");
        ImGui::TextColored(white, "$%08X", status);
        draw_grid_label("FLAGS");
        ImGui::TextColored((status & k_voodoo_status_vretrace) == 0 ? yellow : gray, "VRT"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((status & k_voodoo_status_fbi_busy) != 0 ? orange : gray, "FBI"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((status & k_voodoo_status_trex_busy) != 0 ? orange : gray, "TREX"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((status & k_voodoo_status_busy) != 0 ? orange : gray, "BUSY");

        ImGui::TableNextRow();
        draw_grid_label("FIFO");
        ImGui::TextColored(white, "%u", voodoo->GetQueueCount()); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((regs[k_voodoo_fbi_init0] & k_voodoo_init0_mem_fifo) != 0 ? green : gray, "MEMORY");
        draw_grid_label("BUSY");
        ImGui::TextColored(busy > clocks ? orange : gray, "%llu clk", (unsigned long long)(busy > clocks ? busy - clocks : 0));

        ImGui::TableNextRow();
        draw_grid_label("SWAPS");
        ImGui::TextColored(white, "%u", state->swaps_pending); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(state->swap_waiting ? yellow : gray, "WAITING");
        draw_grid_label("BUFFERS");
        ImGui::TextColored(white, "FRONT %u BACK %u", state->front_buffer, state->back_buffer);

        ImGui::TableNextRow();
        draw_grid_label("THREAD");
        ImGui::TextColored(voodoo->IsThreaded() ? green : gray, "%s", voodoo->IsThreaded() ? "ON" : "OFF");
        draw_grid_label("FRAMES");
        ImGui::TextColored(white, "%u", state->frame_count);

        ImGui::EndTable();
    }

    ImGui::NewLine();
    ImGui::TextColored(cyan, "VIDEO AND CLOCKS"); ImGui::Separator();

    if (ImGui::BeginTable("##voodoo_video", 4, flags))
    {
        setup_grid_columns();

        ImGui::TableNextRow();
        draw_grid_label("RUNNING");
        ImGui::TextColored(timing->running ? green : gray, "%s", timing->running ? "ON" : "OFF");
        draw_grid_label("SIZE");
        ImGui::TextColored(white, "%ux%u", timing->width, timing->height);

        ImGui::TableNextRow();
        draw_grid_label("TOTAL");
        ImGui::TextColored(white, "%ux%u", timing->h_total, timing->v_total);
        draw_grid_label("REFRESH");

        if (timing->running)
            ImGui::TextColored(orange, "%.2f Hz", 1000.0f / voodoo->GetFrameTime());
        else
            ImGui::TextColored(gray, "--");

        ImGui::TableNextRow();
        draw_grid_label("PIXEL CLK");
        ImGui::TextColored(orange, "%.3f MHz", timing->clock / 1000000.0);
        draw_grid_label("GRAPH CLK");
        ImGui::TextColored(orange, "%.3f MHz", voodoo->GetFbiClock() / 1000000.0);

        ImGui::TableNextRow();
        draw_grid_label("ROW PIXELS");
        ImGui::TextColored(white, "%u", voodoo->GetRowPixels());
        draw_grid_label("ROWS");
        ImGui::TextColored(white, "%u", voodoo->GetBufferRows());

        ImGui::TableNextRow();
        draw_voodoo_hex("fbiInit0", regs[k_voodoo_fbi_init0]);
        draw_voodoo_hex("fbiInit1", regs[k_voodoo_fbi_init1]);
        ImGui::TableNextRow();
        draw_voodoo_hex("fbiInit2", regs[k_voodoo_fbi_init2]);
        draw_voodoo_hex("fbiInit3", regs[k_voodoo_fbi_init3]);
        ImGui::TableNextRow();
        draw_voodoo_hex("fbiInit4", regs[k_voodoo_fbi_init4]);
        draw_voodoo_hex("backPorch", regs[k_voodoo_back_porch]);
        ImGui::TableNextRow();
        draw_voodoo_hex("hSync", regs[k_voodoo_h_sync]);
        draw_voodoo_hex("vSync", regs[k_voodoo_v_sync]);
        ImGui::TableNextRow();
        draw_voodoo_hex("videoDim", regs[k_voodoo_video_dimensions]);
        draw_grid_label("DAC CLK");
        ImGui::TextColored(orange, "%.3f MHz", voodoo->GetDac()->GetVideoClock() / 1000000.0);

        ImGui::EndTable();
    }

    ImGui::NewLine();
    ImGui::TextColored(cyan, "COUNTERS AND LAST TRIANGLE"); ImGui::Separator();

    if (ImGui::BeginTable("##voodoo_counters", 4, flags))
    {
        setup_grid_columns();

        ImGui::TableNextRow();
        draw_grid_label("PIXELS IN");
        ImGui::TextColored(white, "%u", state->counters[0] & 0xFFFFFF);
        draw_grid_label("PIXELS OUT");
        ImGui::TextColored(white, "%u", state->counters[4] & 0xFFFFFF);

        ImGui::TableNextRow();
        draw_grid_label("CHROMA FAIL");
        ImGui::TextColored(white, "%u", state->counters[1] & 0xFFFFFF);
        draw_grid_label("Z FAIL");
        ImGui::TextColored(white, "%u", state->counters[2] & 0xFFFFFF);

        ImGui::TableNextRow();
        draw_grid_label("A FAIL");
        ImGui::TextColored(white, "%u", state->counters[3] & 0xFFFFFF);
        draw_grid_label("W START");
        ImGui::TextColored(white, "%.6f", (double)state->params.start_w / 4294967296.0);

        const s32* v = state->params.vertex;
        const char* const names[3] = { "VERTEX A", "VERTEX B", "VERTEX C" };

        for (int i = 0; i < 3; i++)
        {
            if ((i & 1) == 0)
                ImGui::TableNextRow();

            draw_grid_label(names[i]);
            ImGui::TextColored(white, "%.4f, %.4f", v[i * 2] / 16.0, v[i * 2 + 1] / 16.0);
        }

        draw_grid_label("RGBA START");
        ImGui::TextColored(white, "%.1f %.1f %.1f %.1f", (s32)state->params.start[0] / 4096.0,
            (s32)state->params.start[1] / 4096.0, (s32)state->params.start[2] / 4096.0, (s32)state->params.start[3] / 4096.0);

        ImGui::EndTable();
    }
}

static void draw_voodoo_pipeline(Voodoo* voodoo)
{
    const u32* regs = voodoo->GetState()->regs;
    const VoodooTrex::VoodooTrex_State* trex = voodoo->GetTrex()->GetState();
    u32 path = regs[k_voodoo_fbz_color_path];
    u32 fbz = regs[k_voodoo_fbz_mode];
    u32 alpha = regs[k_voodoo_alpha_mode];
    u32 fog = regs[k_voodoo_fog_mode];
    u32 lfb = regs[k_voodoo_lfb_mode];
    u32 mode = trex->texture_mode;
    float space = ImGui::CalcTextSize(" ").x;
    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;

    ImGui::TextColored(cyan, "FBI"); ImGui::Separator();

    if (ImGui::BeginTable("##voodoo_fbi", 2, flags))
    {
        float character = ImGui::CalcTextSize("0").x;
        ImGui::TableSetupColumn(" ", ImGuiTableColumnFlags_WidthFixed, character * 13);
        ImGui::TableSetupColumn(" ", ImGuiTableColumnFlags_WidthFixed, character * 52);

        ImGui::TableNextRow();
        draw_grid_label("fbzColorPath");
        ImGui::TextColored(white, "$%07X", path); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(blue, "RGB %s A %s", k_voodoo_rgb_select[path & 0x03], k_voodoo_rgb_select[(path >> 2) & 0x03]);
        ImGui::SameLine(0.0f, space);
        ImGui::TextColored((path & k_voodoo_fbz_cp_texture) != 0 ? green : gray, "TEX"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((path & k_voodoo_fbz_cp_param_adjust) != 0 ? green : gray, "SUBPIX");

        ImGui::TableNextRow();
        draw_grid_label("fbzMode");
        ImGui::TextColored(white, "$%06X", fbz); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((fbz & k_voodoo_fbz_clip) != 0 ? green : gray, "CLIP"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((fbz & k_voodoo_fbz_chroma) != 0 ? green : gray, "CHROMA"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((fbz & k_voodoo_fbz_stipple) != 0 ? green : gray, "STIPPLE"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((fbz & k_voodoo_fbz_dither) != 0 ? green : gray, "%s",
            (fbz & k_voodoo_fbz_dither_2x2) != 0 ? "DITHER2" : "DITHER4"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((fbz & k_voodoo_fbz_rgb_write) != 0 ? green : gray, "RGB"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((fbz & k_voodoo_fbz_aux_write) != 0 ? green : gray, "AUX");

        ImGui::TableNextRow();
        draw_grid_label("depth");
        ImGui::TextColored((fbz & k_voodoo_fbz_depth) != 0 ? green : gray, "%s %s",
            (fbz & k_voodoo_fbz_wbuffer) != 0 ? "W" : "Z", k_voodoo_functions[(fbz >> 5) & 0x07]);
        ImGui::SameLine(0.0f, space);
        ImGui::TextColored((fbz & k_voodoo_fbz_depth_bias) != 0 ? green : gray, "BIAS"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((fbz & k_voodoo_fbz_alpha_planes) != 0 ? green : gray, "ALPHA PLANES"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((fbz & k_voodoo_fbz_y_origin) != 0 ? green : gray, "Y FLIP"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(white, "DRAW %s", ((fbz >> 14) & 0x03) == 0 ? "FRONT" : ((fbz >> 14) & 0x03) == 1 ? "BACK" : "NONE");

        ImGui::TableNextRow();
        draw_grid_label("alphaMode");
        ImGui::TextColored(white, "$%08X", alpha); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((alpha & k_voodoo_alpha_test) != 0 ? green : gray, "TEST %s $%02X",
            k_voodoo_functions[(alpha >> 1) & 0x07], alpha >> 24);

        ImGui::TableNextRow();
        draw_grid_label("blend");
        ImGui::TextColored((alpha & k_voodoo_alpha_blend) != 0 ? green : gray, "RGB %s %s  A %s %s",
            k_voodoo_blend[(alpha >> 8) & 0x0F], k_voodoo_blend[(alpha >> 12) & 0x0F], k_voodoo_blend[(alpha >> 16) & 0x0F],
            k_voodoo_blend[(alpha >> 20) & 0x0F]);

        ImGui::TableNextRow();
        draw_grid_label("fogMode");
        ImGui::TextColored(white, "$%02X", fog); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((fog & 0x01) != 0 ? green : gray, "FOG"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(white, "%s", (fog & 0x10) != 0 ? "Z" : (fog & 0x08) != 0 ? "ALPHA" : "TABLE");

        ImGui::TableNextRow();
        draw_grid_label("lfbMode");
        ImGui::TextColored(white, "$%05X", lfb); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(white, "FORMAT %u WRITE %s READ %s", lfb & 0x0F, ((lfb >> 4) & 0x03) == 0 ? "FRONT" : "BACK",
            ((lfb >> 6) & 0x03) == 0 ? "FRONT" : ((lfb >> 6) & 0x03) == 1 ? "BACK" : "AUX");
        ImGui::SameLine(0.0f, space);
        ImGui::TextColored((lfb & k_voodoo_lfb_pipeline) != 0 ? green : gray, "PIPELINE");

        ImGui::TableNextRow();
        draw_grid_label("clip");
        ImGui::TextColored(white, "X %u-%u Y %u-%u", (regs[k_voodoo_clip_left_right] >> 16) & 0x3FF,
            regs[k_voodoo_clip_left_right] & 0x3FF, (regs[k_voodoo_clip_low_y_high_y] >> 16) & 0x3FF,
            regs[k_voodoo_clip_low_y_high_y] & 0x3FF);

        ImGui::TableNextRow();
        draw_grid_label("colors");
        ImGui::TextColored(white, "C0 $%08X C1 $%08X ZA $%08X", regs[k_voodoo_color0], regs[k_voodoo_color1],
            regs[k_voodoo_za_color]);

        ImGui::TableNextRow();
        draw_grid_label("keys");
        ImGui::TextColored(white, "CHROMA $%06X FOG $%06X STIPPLE $%08X", regs[k_voodoo_chroma_key] & 0xFFFFFF,
            regs[k_voodoo_fog_color] & 0xFFFFFF, regs[k_voodoo_stipple]);

        ImGui::EndTable();
    }

    ImGui::NewLine();
    ImGui::TextColored(cyan, "TREX"); ImGui::Separator();

    if (ImGui::BeginTable("##voodoo_trex", 2, flags))
    {
        float character = ImGui::CalcTextSize("0").x;
        ImGui::TableSetupColumn(" ", ImGuiTableColumnFlags_WidthFixed, character * 13);
        ImGui::TableSetupColumn(" ", ImGuiTableColumnFlags_WidthFixed, character * 52);

        ImGui::TableNextRow();
        draw_grid_label("textureMode");
        ImGui::TextColored(white, "$%08X", mode); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(blue, "%s", k_voodoo_formats[(mode >> 8) & 0x0F]); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((mode & 0x01) != 0 ? green : gray, "PERSP"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((mode & 0x02) != 0 ? green : gray, "MIN"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((mode & 0x04) != 0 ? green : gray, "MAG"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((mode & 0x40) != 0 ? green : gray, "CLAMPS"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((mode & 0x80) != 0 ? green : gray, "CLAMPT");

        ImGui::TableNextRow();
        draw_grid_label("tLOD");
        ImGui::TextColored(white, "$%08X", trex->tlod); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(white, "MIN %.2f MAX %.2f BIAS %.2f", (trex->tlod & 0x3F) / 4.0, ((trex->tlod >> 6) & 0x3F) / 4.0,
            (s8)(((trex->tlod >> 12) & 0x3F) << 2) / 16.0);

        ImGui::TableNextRow();
        draw_grid_label("tDetail");
        ImGui::TextColored(white, "$%05X", trex->tdetail);

        ImGui::TableNextRow();
        draw_grid_label("texBaseAddr");
        ImGui::TextColored(white, "$%05X $%05X $%05X $%05X", trex->tex_base[0], trex->tex_base[1], trex->tex_base[2],
            trex->tex_base[3]);

        ImGui::TableNextRow();
        draw_grid_label("trexInit");
        ImGui::TextColored(white, "$%08X $%08X", trex->trex_init[0], trex->trex_init[1]);

        ImGui::EndTable();
    }
}

static void draw_voodoo_buffers(Voodoo* voodoo)
{
    ImGui::PushItemWidth(100.0f);
    ImGui::Combo("Buffer##voodoo_buffer", &config_debug.voodoo_buffer, "Front\0Back\0Aux\0");
    ImGui::SameLine();
    ImGui::Combo("Zoom##voodoo_zoom", &config_debug.voodoo_zoom, "1x\0" "2x\0");
    ImGui::PopItemWidth();
    ImGui::Separator();

    if ((emu_debug_voodoo_width <= 0) || (emu_debug_voodoo_height <= 0))
    {
        ImGui::TextColored(gray, "No buffer layout yet");
        return;
    }

    const Voodoo::Voodoo_State* state = voodoo->GetState();
    int buffer = config_debug.voodoo_buffer == 0 ? state->front_buffer : config_debug.voodoo_buffer == 1 ?
        state->back_buffer : 2;
    float zoom = config_debug.voodoo_zoom == 1 ? 2.0f : 1.0f;
    ImVec2 size((float)emu_debug_voodoo_width * zoom, (float)emu_debug_voodoo_height * zoom);
    ImVec2 uv((float)emu_debug_voodoo_width / EMU_DEBUG_FRAMEBUFFER_WIDTH,
        (float)emu_debug_voodoo_height / EMU_DEBUG_FRAMEBUFFER_HEIGHT);

    ImGui::TextColored(violet, "BUFFER %d", buffer); ImGui::SameLine();
    ImGui::TextColored(white, "%dx%d at $%06X", emu_debug_voodoo_width, emu_debug_voodoo_height,
        voodoo->GetBufferBase(buffer) * 2);

    ImGui::BeginChild("##voodoo_image", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::Image((ImTextureID)(intptr_t)ogl_renderer_emu_debug_voodoo, size, ImVec2(0, 0), uv);
    ImGui::EndChild();
}
