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

#define GUI_DEBUG_FRAMEBUFFERS_IMPORT
#include "gui_debug_framebuffers.h"

#include "imgui.h"
#include "geardox.h"
#include "video/video.h"
#include "../config.h"
#include "../emu.h"
#include "../gui.h"
#include "../gui_colors.h"
#include "../ogl_renderer.h"
#include "gui_debug_constants.h"
#include "gui_debug_memory.h"

static const float k_vram_viewer_zoom[3] = { 1.0f, 2.0f, 3.0f };
static const char k_vram_viewer_zoom_items[] = "1x\0" "2x\0" "3x\0";

static void draw_controls(void);
static void draw_image(const Emu_Debug_Buffer_Info& info);
static void draw_visible_area(const Emu_Debug_Buffer_Info& info, ImVec2 position, float zoom);
static void goto_plane(int plane, u32 offset);

void gui_debug_window_vram_viewer(void)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::SetNextWindowPos(ImVec2(120, 240), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(680, 520), ImGuiCond_FirstUseEver);
    ImGui::Begin("VRAM Viewer", &config_debug.show_vram_viewer);

    if (ImGui::BeginTabBar("##vram_viewer_tabs"))
    {
        static const char* const k_tabs[Emu_Debug_Buffer_Count] = { "PLANE 0", "PLANE 1", "PLANE 2", "PLANE 3", "PLANAR",
            "256 COLORS", "TEXT" };

        for (int tab = 0; tab < Emu_Debug_Buffer_Count; tab++)
        {
            if (!ImGui::BeginTabItem(k_tabs[tab]))
                continue;

            if (config_debug.vram_viewer_tab != tab)
            {
                config_debug.vram_viewer_tab = tab;
                emu_debug_update();
            }

            draw_controls();
            ImGui::Separator();

            ImGui::PushFont(gui_default_font);
            draw_image(emu_debug_framebuffer_info);
            ImGui::PopFont();

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

static void draw_controls(void)
{
    ImGuiStyle& style = ImGui::GetStyle();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Zoom"); ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("0x").x + ImGui::GetFrameHeight() + style.FramePadding.x * 2.0f);
    ImGui::Combo("##vram_viewer_zoom", &config_debug.vram_viewer_zoom, k_vram_viewer_zoom_items); ImGui::SameLine();
    ImGui::TextUnformatted(" Pitch"); ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("0000").x + (ImGui::GetFrameHeight() + style.ItemInnerSpacing.x) * 2.0f +
        style.FramePadding.x * 2.0f);

    if (ImGui::InputInt("##vram_viewer_pitch", &config_debug.vram_viewer_pitch, 2, 16))
    {
        config_debug.vram_viewer_pitch = CLAMP(config_debug.vram_viewer_pitch, 0, 512);
        emu_debug_update();
    }

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("CRTC addresses per row, 0 follows the offset register");

    ImGui::SameLine();
    ImGui::Checkbox("Show Visible Area", &config_debug.vram_viewer_show_window);
}

static void draw_image(const Emu_Debug_Buffer_Info& info)
{
    ImGui::BeginChild("##vram_viewer_image", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);

    if (info.width <= 0 || info.height <= 0)
    {
        ImGui::TextColored(gray, "NO DATA");
        ImGui::EndChild();
        return;
    }

    float zoom = k_vram_viewer_zoom[CLAMP(config_debug.vram_viewer_zoom, 0, 2)];
    ImVec2 position = ImGui::GetCursorScreenPos();
    ImVec2 size((float)info.width * zoom, (float)info.height * zoom);

    ImGui::Image((ImTextureID)(intptr_t)ogl_renderer_emu_debug_framebuffer, size, ImVec2(0, 0),
        ImVec2((float)info.width / EMU_DEBUG_FRAMEBUFFER_WIDTH, (float)info.height / EMU_DEBUG_FRAMEBUFFER_HEIGHT));

    if (config_debug.vram_viewer_show_window)
        draw_visible_area(info, position, zoom);

    if (ImGui::IsItemHovered())
    {
        ImVec2 mouse = ImGui::GetIO().MousePos;
        int x = CLAMP((int)((mouse.x - position.x) / zoom), 0, info.width - 1);
        int y = CLAMP((int)((mouse.y - position.y) / zoom), 0, info.height - 1);
        Video* video = emu_get_core()->GetVideo();
        u32 address = (u32)(y / info.cell_height) * (u32)info.pitch + (u32)(x / info.cell_width);
        bool plane_view = config_debug.vram_viewer_tab <= Emu_Debug_Buffer_Plane3;
        u32 offset = plane_view ? address & 0xFFFF : video->GetCRTCOffset(address, 0);
        int plane = plane_view ? config_debug.vram_viewer_tab :
            (config_debug.vram_viewer_tab == Emu_Debug_Buffer_256 ? x & 0x03 : 0);
        u32 color = ((u32*)emu_debug_framebuffer)[y * EMU_DEBUG_FRAMEBUFFER_WIDTH + x];
        float swatch = ImGui::GetTextLineHeight();
        ImVec4 pixel = ImVec4((color & 0xFF) / 255.0f, ((color >> 8) & 0xFF) / 255.0f, ((color >> 16) & 0xFF) / 255.0f, 1.0f);

        ImGui::BeginTooltip();
        ImGui::TextColored(violet, "X,Y    "); ImGui::SameLine();
        ImGui::TextColored(white, "%d,%d", x, y);
        ImGui::TextColored(violet, "ADDRESS"); ImGui::SameLine();
        ImGui::TextColored(cyan, "$%04X", address & 0xFFFF);
        ImGui::TextColored(violet, "PLANE  "); ImGui::SameLine();
        ImGui::TextColored(cyan, "%d:$%04X", plane, offset);
        ImGui::TextColored(violet, "VALUE  "); ImGui::SameLine();
        ImGui::TextColored(white, "$%02X", video->PeekPlane(plane, offset));
        ImGui::TextColored(violet, "COLOR  "); ImGui::SameLine();
        ImGui::ColorButton("##pixel", pixel, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
            ImVec2(swatch, swatch)); ImGui::SameLine();
        ImGui::TextColored(white, "#%02X%02X%02X", color & 0xFF, (color >> 8) & 0xFF, (color >> 16) & 0xFF);
        ImGui::EndTooltip();

        if (ImGui::IsMouseClicked(0))
            goto_plane(plane, offset);
    }

    ImGui::EndChild();
}

static void draw_visible_area(const Emu_Debug_Buffer_Info& info, ImVec2 position, float zoom)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 rect_min(position.x + info.window_x * zoom, position.y + info.window_y * zoom);
    ImVec2 rect_max(rect_min.x + info.window_width * zoom, rect_min.y + info.window_height * zoom);

    draw_list->PushClipRect(position, ImVec2(position.x + info.width * zoom, position.y + info.height * zoom), true);
    draw_list->AddRect(rect_min, rect_max, ImColor(yellow), 0.0f, ImDrawFlags_None, 2.0f);
    draw_list->PopClipRect();
}

static void goto_plane(int plane, u32 offset)
{
    GDOX_Debug_Memory_Address target = { };
    target.space = GDOX_DEBUG_MEMORY_REGION;
    target.region = GDOX_DEBUG_REGION_VGA_PLANE_0 + plane;
    target.address = offset;
    target.segment_register = -1;
    gui_debug_memory_goto(target);
}
