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

#define GUI_DEBUG_AUDIO_IMPORT
#include "gui_debug_audio.h"

#include <math.h>
#include "imgui.h"
#include "implot.h"
#include "fonts/IconsMaterialDesign.h"
#include "geardox.h"
#include "audio/audio.h"
#include "cdrom/cdrom_audio.h"
#include "cdrom/cdrom_media.h"
#include "../config.h"
#include "../emu.h"
#include "../gui.h"
#include "../gui_colors.h"

static const float k_scope_samples_per_pixel = 2.5f;

static float* scope_buffer = NULL;

static void draw_scope(const char* id, const s16* data, int stride, int count, float scale, ImVec2 size,
    const ImVec4& color);
static void draw_stereo_scope(const char* id, int source, float scale, ImVec2 size);
static bool draw_mute_button(const char* id, bool muted, const char* tooltip);
static void draw_grid_label(const char* label);
static void draw_position(u32 lba, u32 pregap, bool valid);

void gui_debug_audio_init(void)
{
    scope_buffer = new float[GDOX_AUDIO_BUFFER_SIZE / 2];
}

void gui_debug_audio_destroy(void)
{
    SafeDeleteArray(scope_buffer);
}

void gui_debug_window_sound_control(void)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::SetNextWindowPos(ImVec2(125, 175), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(253, 120), ImGuiCond_FirstUseEver);
    ImGui::Begin("Sound Control", &config_debug.show_sound_control);

    ImGui::PushFont(gui_default_font);

    Audio* audio = emu_get_core()->GetAudio();
    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;

    ImGui::TextColored(cyan, "DEBUGGER MUTES"); ImGui::Separator();

    ImGui::PopFont();

    static const char* sources[Audio::AUDIO_SOURCE_COUNT] = { "FM", "PCM", "CD-DA", "Buzzer" };

    if (ImGui::BeginTable("##debugger_mutes", 2, flags))
    {
        ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("0").x * 11);

        for (int i = 0; i < Audio::AUDIO_SOURCE_COUNT; i++)
        {
            bool muted = audio->IsSourceMuted(i);

            ImGui::TableNextColumn();

            if (ImGui::Checkbox(sources[i], &muted))
                audio->SetSourceMute(i, muted);
        }

        ImGui::EndTable();
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

void gui_debug_window_cdrom_audio(void)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::SetNextWindowPos(ImVec2(166, 242), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(249, 371), ImGuiCond_FirstUseEver);
    ImGui::Begin("CD Audio", &config_debug.show_cdrom_audio);

    GeardoxCore* core = emu_get_core();
    Audio* audio = core->GetAudio();
    CdRomAudio* cdrom_audio = core->GetCDROMAudio();
    CdRomAudio::CdRomAudio_State* state = cdrom_audio->GetState();
    CdRomMedia* media = core->GetCDROMMedia();
    bool muted = audio->IsSourceMuted(Audio::AUDIO_SOURCE_CDDA);
    bool active = state->play_state != CdRomAudio::CDROM_AUDIO_IDLE;

    if (draw_mute_button("##cdda_mute", muted, "Mute CD-DA"))
        audio->SetSourceMute(Audio::AUDIO_SOURCE_CDDA, !muted);

    ImGui::SameLine();
    draw_stereo_scope("##cdda_mix", Audio::AUDIO_SOURCE_CDDA, 1.0f / 32768.0f, ImVec2(95, 50));

    ImGui::PushFont(gui_default_font);

    static const char* states[3] = { "IDLE", "PLAYING", "PAUSED" };
    float character = ImGui::CalcTextSize("0").x;
    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;
    s32 track = media->IsReady() && active ? media->FindTrackFromLBA(state->current_lba, true) : -1;

    ImGui::TextColored(cyan, "PLAYBACK"); ImGui::Separator();

    if (ImGui::BeginTable("##cdda_playback", 2, flags))
    {
        ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 9);
        ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 8);

        ImGui::TableNextRow();
        draw_grid_label("STATE");
        ImGui::TextColored(active ? blue : gray, "%s", states[state->play_state % 3]);

        ImGui::TableNextRow();
        draw_grid_label("END");
        ImGui::TextColored(active ? white : gray, "%s", state->repeat ? "REPEAT" : "STOP");

        ImGui::TableNextRow();
        draw_grid_label("TRACK");

        if (track >= 0)
            ImGui::TextColored(orange, "%02d", track + 1);
        else
            ImGui::TextColored(gray, "--");

        ImGui::EndTable();
    }

    ImGui::NewLine(); ImGui::TextColored(cyan, "POSITION"); ImGui::Separator();

    if (ImGui::BeginTable("##cdda_position", 3, flags))
    {
        static const char* rows[3] = { "START", "STOP", "CURRENT" };
        u32 values[3] = { state->start_lba, state->end_lba, state->current_lba };

        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, character * 9);
        ImGui::TableSetupColumn("LBA", ImGuiTableColumnFlags_WidthFixed, character * 7);
        ImGui::TableSetupColumn("MSF", ImGuiTableColumnFlags_WidthFixed, character * 8);
        ImGui::TableHeadersRow();

        for (int i = 0; i < 3; i++)
        {
            ImGui::TableNextRow();
            draw_grid_label(rows[i]);
            draw_position(values[i], 150, active);
        }

        u32 track_start = track >= 0 ? media->GetTracks()[track].start_lba : 0;

        ImGui::TableNextRow();
        draw_grid_label("TRACK POS");
        draw_position(state->current_lba - MIN(state->current_lba, track_start), 0, track >= 0);

        ImGui::EndTable();
    }

    ImGui::PopFont();

    ImGui::End();
    ImGui::PopStyleVar();
}

static void draw_scope(const char* id, const s16* data, int stride, int count, float scale, ImVec2 size,
    const ImVec4& color)
{
    count = MIN(count, GDOX_AUDIO_BUFFER_SIZE / 2);

    for (int i = 0; i < count; i++)
        scope_buffer[i] = CLAMP((float)data[i * stride] * scale, -1.0f, 1.0f);

    // A fixed density keeps the wave shape at any scope width, wider scopes just show more samples
    int window = CLAMP((int)(size.x * k_scope_samples_per_pixel), 1, MAX(count, 1));
    int half = window / 2;
    int trigger = half;

    for (int i = MAX(half, 1); i + window - half <= count; i++)
    {
        if (scope_buffer[i - 1] < 0.0f && scope_buffer[i] >= 0.0f)
        {
            trigger = i;
            break;
        }
    }

    int x_min = trigger - half;
    int x_max = x_min + window;
    ImPlotAxisFlags flags = ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoLabel |
        ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_Lock | ImPlotAxisFlags_NoTickMarks;
    ImPlotSpec spec;
    spec.LineColor = color;
    spec.LineWeight = 1.0f;

    ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(1, 1));

    if (ImPlot::BeginPlot(id, size, ImPlotFlags_CanvasOnly))
    {
        ImPlot::SetupAxes("x", "y", flags, flags);
        ImPlot::SetupAxesLimits(x_min, x_max, -1.0f, 1.0f, ImPlotCond_Always);

        if (count > 0)
            ImPlot::PlotLine("Wave", scope_buffer, count, 1.0, 0.0, spec);

        ImPlot::EndPlot();
    }

    ImPlot::PopStyleVar();
}

static void draw_stereo_scope(const char* id, int source, float scale, ImVec2 size)
{
    Audio* audio = emu_get_core()->GetAudio();
    const s16* buffer = audio->GetSourceBuffer(source);
    int count = audio->GetFrameSamples() / 2;
    char left_id[32];
    char right_id[32];
    snprintf(left_id, sizeof(left_id), "%s_left", id);
    snprintf(right_id, sizeof(right_id), "%s_right", id);

    draw_scope(left_id, buffer, 2, count, scale, size, accent);
    ImGui::SameLine();
    draw_scope(right_id, buffer + 1, 2, count, scale, size, accent);
}

static bool draw_mute_button(const char* id, bool muted, const char* tooltip)
{
    char label[32];
    snprintf(label, sizeof(label), "%s%s", muted ? ICON_MD_MUSIC_OFF : ICON_MD_MUSIC_NOTE, id);

    ImGui::PushFont(gui_material_icons_font);
    ImGui::PushStyleColor(ImGuiCol_Text, muted ? mid_gray : white);
    bool pressed = ImGui::Button(label);
    ImGui::PopStyleColor();
    ImGui::PopFont();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::PushFont(gui_roboto_font);
        ImGui::SetTooltip("%s", tooltip);
        ImGui::PopFont();
    }

    return pressed;
}

static void draw_grid_label(const char* label)
{
    ImGui::TableNextColumn();
    ImGui::TextColored(violet, "%s", label);
    ImGui::TableNextColumn();
}

// Fills the LBA and MSF columns, MSF counts the pregap for absolute positions
static void draw_position(u32 lba, u32 pregap, bool valid)
{
    if (!valid)
    {
        ImGui::TextColored(gray, "--");
        ImGui::TableNextColumn();
        ImGui::TextColored(gray, "--");
        return;
    }

    GDOX_CdRomMSF msf;
    LbaToMsf(lba + pregap, &msf);
    ImGui::TextColored(white, "%u", lba);
    ImGui::TableNextColumn();
    ImGui::TextColored(orange, "%02u:%02u:%02u", msf.minutes, msf.seconds, msf.frames);
}
