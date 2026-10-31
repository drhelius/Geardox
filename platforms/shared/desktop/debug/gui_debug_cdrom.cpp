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

#define GUI_DEBUG_CDROM_IMPORT
#include "gui_debug_cdrom.h"

#include "imgui.h"
#include "geardox.h"
#include "cdrom/cdrom_audio.h"
#include "cdrom/cdrom_media.h"
#include "../config.h"
#include "../emu.h"
#include "../gui.h"
#include "../gui_colors.h"

void gui_debug_window_cdrom_toc(void)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::SetNextWindowPos(ImVec2(248, 176), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_FirstUseEver);
    ImGui::Begin("CD-ROM TOC", &config_debug.show_cdrom_toc);

    ImGui::PushFont(gui_default_font);

    GeardoxCore* core = emu_get_core();
    CdRomMedia* media = core->GetCDROMMedia();
    bool ready = media->IsReady();

    if (!ready)
    {
        ImGui::TextColored(gray, "No CD-ROM inserted");
        ImGui::PopFont();
        ImGui::End();
        ImGui::PopStyleVar();
        return;
    }

    const std::vector<CdRomImage::Track>& tracks = media->GetTracks();
    s32 current_track = media->FindTrackFromLBA(gui_debug_cdrom_head(), true);
    int audio_tracks = 0;
    GDOX_CdRomMSF length = media->GetCdRomLength();

    for (size_t i = 0; i < tracks.size(); i++)
        audio_tracks += tracks[i].type == GDOX_CDROM_AUDIO_TRACK ? 1 : 0;

    ImGui::TextColored(violet, "MEDIA   "); ImGui::SameLine();
    ImGui::TextColored(white, "%s", media->GetFileName());
    ImGui::TextColored(violet, "TRACKS  "); ImGui::SameLine();
    ImGui::TextColored(white, "%d", (int)tracks.size()); ImGui::SameLine();
    ImGui::TextColored(green, " (AUDIO %d)", audio_tracks); ImGui::SameLine();
    ImGui::TextColored(yellow, " (DATA %d)", (int)tracks.size() - audio_tracks);
    ImGui::TextColored(violet, "LENGTH  "); ImGui::SameLine();
    ImGui::TextColored(white, "%02u:%02u:%02u", length.minutes, length.seconds, length.frames); ImGui::SameLine();
    ImGui::TextColored(violet, "  SECTORS"); ImGui::SameLine();
    ImGui::TextColored(white, "%u", media->GetSectorCount());
    ImGui::Separator();

    ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
        ImGuiTableFlags_BordersV | ImGuiTableFlags_SizingFixedFit;

    if (ImGui::BeginTable("##cdrom_toc", 9, flags))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#");
        ImGui::TableSetupColumn("TYPE");
        ImGui::TableSetupColumn("START MSF");
        ImGui::TableSetupColumn("END MSF");
        ImGui::TableSetupColumn("LENGTH");
        ImGui::TableSetupColumn("START LBA");
        ImGui::TableSetupColumn("END LBA");
        ImGui::TableSetupColumn("SECTORS");
        ImGui::TableSetupColumn("PREGAP");
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < tracks.size(); i++)
        {
            const CdRomImage::Track& track = tracks[i];
            GDOX_CdRomMSF start;
            GDOX_CdRomMSF end;
            GDOX_CdRomMSF duration;
            LbaToMsf(track.start_lba + 150, &start);
            LbaToMsf(track.end_lba + 150, &end);
            LbaToMsf(track.sector_count, &duration);
            bool current = (s32)i == current_track;

            ImGui::TableNextRow();

            if (current)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(dark_blue));

            ImGui::TableNextColumn();
            ImGui::TextColored(current ? orange : white, "%02d", (int)i + 1);
            ImGui::TableNextColumn();
            ImGui::TextColored(track.type == GDOX_CDROM_AUDIO_TRACK ? green : yellow, "%s", TrackTypeName(track.type));
            ImGui::TableNextColumn();
            ImGui::Text("%02u:%02u:%02u", start.minutes, start.seconds, start.frames);
            ImGui::TableNextColumn();
            ImGui::Text("%02u:%02u:%02u", end.minutes, end.seconds, end.frames);
            ImGui::TableNextColumn();
            ImGui::Text("%02u:%02u:%02u", duration.minutes, duration.seconds, duration.frames);
            ImGui::TableNextColumn();
            ImGui::Text("%u", track.start_lba);
            ImGui::TableNextColumn();
            ImGui::Text("%u", track.end_lba);
            ImGui::TableNextColumn();
            ImGui::Text("%u", track.sector_count);
            ImGui::TableNextColumn();

            if (track.has_lead_in)
                ImGui::Text("%u", track.start_lba - track.lead_in_lba);
            else
                ImGui::TextColored(gray, "--");
        }

        ImGui::EndTable();
    }

    ImGui::PopFont();

    ImGui::End();
    ImGui::PopStyleVar();
}

u32 gui_debug_cdrom_head(void)
{
    CdRomAudio* audio = emu_get_core()->GetCDROMAudio();

    if (audio->IsPlaying() || audio->IsPaused())
        return audio->GetCurrentLBA();

    return emu_get_core()->GetCDROMMedia()->GetCurrentSector();
}
