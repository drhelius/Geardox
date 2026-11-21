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
#include "drive/ide_cdrom.h"
#include "drive/ide_controller.h"
#include "system/scheduler.h"
#include "system/machine_profiles.h"
#include "gui_debug_constants.h"
#include "../config.h"
#include "../emu.h"
#include "../gui.h"
#include "../gui_colors.h"

static void draw_drive_label(const char* label);
static void draw_drive_flag(const char* label, bool value, const char* on, const char* off, const ImVec4& color);
static void draw_drive_position(u32 lba, bool valid);
static const char* get_sense_key_name(u8 key);
static const char* get_audio_status_name(u8 status);

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

void gui_debug_window_cdrom_drive(void)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::SetNextWindowPos(ImVec2(210, 190), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(330, 640), ImGuiCond_FirstUseEver);
    ImGui::Begin("CD-ROM Drive", &config_debug.show_cdrom_drive);

    ImGui::PushFont(gui_default_font);

    GeardoxCore* core = emu_get_core();
    IdeCdRom* drive = core->GetCDROM();
    CdRomMedia* media = core->GetCDROMMedia();
    CdRomAudio* audio = core->GetCDROMAudio();

    if (!IsValidPointer(drive) || !drive->IsAttached())
    {
        ImGui::TextColored(gray, "No CD-ROM drive fitted");
        ImGui::PopFont();
        ImGui::End();
        ImGui::PopStyleVar();
        return;
    }

    IdeCdRom::IdeCdRom_State* state = drive->GetState();
    CdRomAudio::CdRomAudio_State* audio_state = audio->GetState();
    bool has_disc = drive->HasDisc();
    bool ready = drive->IsReady(core->GetScheduler()->GetClocks());
    float character = ImGui::CalcTextSize("0").x;
    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;
    int position = core->GetIDE()->GetCdRomSlot() + 1;

    ImGui::TextColored(cyan, "DRIVE"); ImGui::Separator();

    if (ImGui::BeginTable("##cdrom_drive", 2, flags))
    {
        ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 11);
        ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 20);

        draw_drive_label("POSITION");
        ImGui::TextColored(orange, "%s", k_cdrom_positions[CLAMP(position, 0, GDOX_CDROM_POSITION_COUNT - 1)].name);

        draw_drive_label("SPEED");
        ImGui::TextColored(white, "%dx", drive->GetSpeed());

        draw_drive_flag("TRAY", state->tray_open, "OPEN", "CLOSED", orange);
        draw_drive_flag("DISC", has_disc, "PRESENT", "NONE", green);

        draw_drive_label("STATE");

        if (state->tray_open || !has_disc)
            ImGui::TextColored(gray, "NOT READY");
        else if (ready)
            ImGui::TextColored(green, "READY");
        else
            ImGui::TextColored(yellow, "SPINNING UP");

        draw_drive_flag("MOTOR", state->motor_on, "ON", "OFF", green);

        draw_drive_label("ATTENTION");

        if (state->unit_attention != 0)
            ImGui::TextColored(yellow, "PENDING 06/%02X/00", state->unit_attention);
        else
            ImGui::TextColored(gray, "NONE");

        draw_drive_flag("PREVENT", state->prevent, "LOCKED", "ALLOWED", orange);

        draw_drive_label("BUSY");
        ImGui::TextColored(drive->IsBusy() ? blue : gray, "%s", drive->IsBusy() ?
            k_debug_cdrom_phase_names[state->phase % IDE_CDROM_PHASE_COUNT] : "IDLE");

        ImGui::EndTable();
    }

    ImGui::NewLine(); ImGui::TextColored(cyan, "HEAD"); ImGui::Separator();

    if (ImGui::BeginTable("##cdrom_head", 3, flags))
    {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, character * 11);
        ImGui::TableSetupColumn("LBA", ImGuiTableColumnFlags_WidthFixed, character * 8);
        ImGui::TableSetupColumn("MSF", ImGuiTableColumnFlags_WidthFixed, character * 8);
        ImGui::TableHeadersRow();

        ImGui::TableNextRow();
        draw_drive_label("HEAD");
        draw_drive_position(state->head_lba, has_disc);

        ImGui::TableNextRow();
        draw_drive_label("AHEAD FROM");
        draw_drive_position(state->ahead_lba, has_disc && (state->ahead_end > state->ahead_lba));

        ImGui::TableNextRow();
        draw_drive_label("AHEAD TO");
        draw_drive_position(state->ahead_end, has_disc && (state->ahead_end > state->ahead_lba));

        ImGui::EndTable();
    }

    s32 track = has_disc ? media->FindTrackFromLBA(state->head_lba, true) : -1;

    ImGui::TextColored(violet, "TRACK      "); ImGui::SameLine();

    if (track >= 0)
        ImGui::TextColored(orange, "%02d", track + 1);
    else
        ImGui::TextColored(gray, "--");

    ImGui::NewLine(); ImGui::TextColored(cyan, "LAST PACKET"); ImGui::Separator();

    ImGui::TextColored(white, "$%02X %s", state->packet[0], GetCdRomPacketName(state->packet[0]));

    char bytes[IDE_CDROM_PACKET_SIZE * 3];
    hex_encode(state->packet, IDE_CDROM_PACKET_SIZE, bytes, sizeof(bytes), true, ' ');

    ImGui::TextColored(gray, "%s", bytes);

    ImGui::NewLine(); ImGui::TextColored(cyan, "SENSE"); ImGui::Separator();

    if (ImGui::BeginTable("##cdrom_sense", 2, flags))
    {
        ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 11);
        ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 20);

        draw_drive_label("KEY");
        ImGui::TextColored(state->sense_key != 0 ? orange : gray, "$%X %s", state->sense_key,
            get_sense_key_name(state->sense_key));

        draw_drive_label("ASC/ASCQ");
        ImGui::TextColored(state->sense_asc != 0 ? white : gray, "$%02X/$%02X", state->sense_asc, state->sense_ascq);

        ImGui::EndTable();
    }

    ImGui::NewLine(); ImGui::TextColored(cyan, "AUDIO PAGE 0Eh"); ImGui::Separator();

    if (ImGui::BeginTable("##cdrom_page", 3, flags))
    {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, character * 11);
        ImGui::TableSetupColumn("CHANNEL", ImGuiTableColumnFlags_WidthFixed, character * 8);
        ImGui::TableSetupColumn("VOLUME", ImGuiTableColumnFlags_WidthFixed, character * 8);
        ImGui::TableHeadersRow();

        ImGui::TableNextRow();
        draw_drive_label("SOTC");
        ImGui::TextColored(state->sotc ? orange : gray, "%s", state->sotc ? "ON" : "OFF");

        for (int i = 0; i < 2; i++)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(violet, "PORT %d", i);
            ImGui::TableNextColumn();
            ImGui::TextColored(white, "$%02X", state->port_channel[i]);
            ImGui::TableNextColumn();
            ImGui::TextColored(white, "$%02X", state->port_volume[i]);
        }

        ImGui::EndTable();
    }

    ImGui::NewLine(); ImGui::TextColored(cyan, "AUDIO"); ImGui::Separator();

    if (ImGui::BeginTable("##cdrom_audio", 2, flags))
    {
        static const char* const play_states[3] = { "IDLE", "PLAYING", "PAUSED" };
        bool active = audio_state->play_state != CdRomAudio::CDROM_AUDIO_IDLE;
        GDOX_CdRomMSF msf;
        LbaToMsf(audio_state->current_lba + 150, &msf);

        ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 11);
        ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 20);

        draw_drive_label("STATUS");
        ImGui::TextColored(white, "$%02X %s", drive->PeekAudioStatus(), get_audio_status_name(drive->PeekAudioStatus()));

        draw_drive_label("PLAY");
        ImGui::TextColored(active ? blue : gray, "%s", play_states[audio_state->play_state % 3]);

        draw_drive_label("RANGE");

        if (active)
            ImGui::TextColored(white, "%u - %u", audio_state->start_lba, audio_state->end_lba);
        else
            ImGui::TextColored(gray, "--");

        draw_drive_label("POSITION");

        if (active)
        {
            ImGui::TextColored(white, "%u ", audio_state->current_lba); ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextColored(orange, "%02u:%02u:%02u", msf.minutes, msf.seconds, msf.frames);
        }
        else
            ImGui::TextColored(gray, "--");

        ImGui::EndTable();
    }

    ImGui::PopFont();

    ImGui::End();
    ImGui::PopStyleVar();
}

u32 gui_debug_cdrom_head(void)
{
    GeardoxCore* core = emu_get_core();
    CdRomAudio* audio = core->GetCDROMAudio();

    if (audio->IsPlaying() || audio->IsPaused())
        return audio->GetCurrentLBA();

    IdeCdRom* drive = core->GetCDROM();

    if (IsValidPointer(drive) && drive->IsAttached())
        return drive->GetHeadLBA();

    return core->GetCDROMMedia()->GetCurrentSector();
}

static void draw_drive_label(const char* label)
{
    ImGui::TableNextColumn();
    ImGui::TextColored(violet, "%s", label);
    ImGui::TableNextColumn();
}

static void draw_drive_flag(const char* label, bool value, const char* on, const char* off, const ImVec4& color)
{
    draw_drive_label(label);
    ImGui::TextColored(value ? color : gray, "%s", value ? on : off);
}

static void draw_drive_position(u32 lba, bool valid)
{
    ImGui::TableNextColumn();

    if (!valid)
    {
        ImGui::TextColored(gray, "--");
        ImGui::TableNextColumn();
        ImGui::TextColored(gray, "--");
        return;
    }

    GDOX_CdRomMSF msf;
    LbaToMsf(lba + 150, &msf);
    ImGui::TextColored(white, "%u", lba);
    ImGui::TableNextColumn();
    ImGui::TextColored(orange, "%02u:%02u:%02u", msf.minutes, msf.seconds, msf.frames);
}

static const char* get_sense_key_name(u8 key)
{
    switch (key)
    {
        case 0x00: return "NO SENSE";
        case 0x02: return "NOT READY";
        case 0x03: return "MEDIUM ERROR";
        case 0x04: return "HARDWARE ERROR";
        case 0x05: return "ILLEGAL REQUEST";
        case 0x06: return "UNIT ATTENTION";
        case 0x0B: return "ABORTED COMMAND";
        default: return "OTHER";
    }
}

static const char* get_audio_status_name(u8 status)
{
    switch (status)
    {
        case IDE_CDROM_AUDIO_PLAYING: return "PLAYING";
        case IDE_CDROM_AUDIO_PAUSED: return "PAUSED";
        case IDE_CDROM_AUDIO_COMPLETED: return "COMPLETED";
        case IDE_CDROM_AUDIO_ERROR: return "ERROR";
        case IDE_CDROM_AUDIO_NONE: return "NONE";
        default: return "INVALID";
    }
}
