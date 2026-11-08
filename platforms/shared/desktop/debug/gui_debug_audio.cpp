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
#include "drive/ide_cdrom.h"
#include "cdrom/cdrom_media.h"
#include "../config.h"
#include "../emu.h"
#include "../gui.h"
#include "../gui_colors.h"

static const float k_scope_samples_per_pixel = 2.5f;
static const char* const k_opl_envelope_names[4] = { "ATTACK", "DECAY", "SUSTAIN", "RELEASE" };
static const char* const k_opl_channel_types[4] = { "2-OP", "4-OP", "4-OP PAIR", "DRUM" };
static const char* const k_dsp_format_names[6] = { "PCM 8", "PCM 16", "ADPCM 2", "ADPCM 3", "ADPCM 4", "SILENCE" };

static float* scope_buffer = NULL;

static void draw_scope(const char* id, const s16* data, int stride, int count, float scale, ImVec2 size,
    const ImVec4& color);
static void draw_stereo_scope(const char* id, int source, float scale, ImVec2 size);
static bool draw_mute_button(const char* id, bool muted, const char* tooltip);
static void draw_grid_label(const char* label);
static void draw_position(u32 lba, u32 pregap, bool valid);
static void draw_opl_global(OPL* opl);
static void draw_opl_channel(Audio* audio, OPL* opl, int chip, int channel);
static void draw_dsp_transfer(SoundBlaster* blaster);
static void draw_mixer(SoundBlaster* blaster);

void gui_debug_audio_init(void)
{
    scope_buffer = new float[GDOX_AUDIO_BUFFER_SIZE / 2];
}

void gui_debug_audio_destroy(void)
{
    SafeDeleteArray(scope_buffer);
}

void gui_debug_window_opl(void)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::SetNextWindowPos(ImVec2(202, 174), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(462, 528), ImGuiCond_FirstUseEver);
    ImGui::Begin("OPL", &config_debug.show_opl);

    Audio* audio = emu_get_core()->GetAudio();
    int chips = audio->GetOPLChips();
    bool muted = audio->IsSourceMuted(Audio::AUDIO_SOURCE_FM);

    if (draw_mute_button("##fm_mute", muted, "Mute FM"))
        audio->SetSourceMute(Audio::AUDIO_SOURCE_FM, !muted);

    ImGui::SameLine();
    draw_stereo_scope("##fm_mix", Audio::AUDIO_SOURCE_FM, 1.0f / 32768.0f, ImVec2(200, 50));

    if (chips == 0)
        ImGui::TextColored(gray, "This card has no FM chip");

    for (int chip = 0; chip < chips; chip++)
    {
        OPL* opl = audio->GetOPL(chip);
        int channels = opl->GetModel() == OPL::OPL_MODEL_OPL3 ? OPL_CHANNEL_COUNT : OPL2_CHANNEL_COUNT;

        ImGui::PushFont(gui_default_font);
        draw_opl_global(opl);
        ImGui::PopFont();

        char id[24];
        snprintf(id, sizeof(id), "##opl_channels%d", chip);

        if (ImGui::BeginTabBar(id, ImGuiTabBarFlags_FittingPolicyScroll))
        {
            for (int channel = 0; channel < channels; channel++)
            {
                char tab[8];
                snprintf(tab, sizeof(tab), "CH%d", channel + 1);

                if (ImGui::BeginTabItem(tab))
                {
                    draw_opl_channel(audio, opl, chip, channel);
                    ImGui::EndTabItem();
                }
            }

            ImGui::EndTabBar();
        }
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

static void draw_opl_global(OPL* opl)
{
    OPL::OPL_State* state = opl->GetState();
    float space = ImGui::CalcTextSize(" ").x;

    ImGui::TextColored(cyan, "%s", opl->GetModel() == OPL::OPL_MODEL_OPL3 ? "YMF262 OPL3" : "YM3812 OPL2");
    ImGui::Separator();

    if (!ImGui::BeginTable("##opl_global", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX))
        return;

    ImGui::TableNextRow();
    draw_grid_label("STATUS");
    ImGui::TextColored(white, "$%02X", opl->Peek(0));
    draw_grid_label("ADDRESS");
    ImGui::TextColored(white, "$%03X", state->address);

    for (int i = 0; i < OPL_TIMERS; i++)
    {
        char label[16];
        snprintf(label, sizeof(label), "TIMER %d", i + 1);

        ImGui::TableNextRow();
        draw_grid_label(label);
        ImGui::TextColored(white, "$%02X", state->timer_reload[i]); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(state->timer_start[i] ? green : gray, "RUN"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(state->timer_mask[i] ? yellow : gray, "MASK"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(state->timer_flag[i] ? yellow : gray, "FLAG");
        draw_grid_label(i == 0 ? "NEW" : "RHYTHM");

        if (i == 0)
            ImGui::TextColored(state->new_mode ? green : gray, "%s", state->new_mode ? "ON" : "OFF");
        else
            ImGui::TextColored((state->rhythm & k_opl_rhythm_enable) ? green : gray, "$%02X", state->rhythm);
    }

    ImGui::EndTable();
}

static void draw_opl_channel(Audio* audio, OPL* opl, int chip, int channel)
{
    OPL::OPL_State* state = opl->GetState();
    OPL::OPL_Channel& ch = state->channels[channel];
    bool muted = opl->IsChannelMuted(channel);
    char id[32];

    snprintf(id, sizeof(id), "##opl_mute%d_%d", chip, channel);

    if (draw_mute_button(id, muted, "Mute Channel"))
        opl->SetChannelMute(channel, !muted);

    ImGui::SameLine();
    snprintf(id, sizeof(id), "##opl_scope%d_%d", chip, channel);
    draw_scope(id, audio->GetFMChannelBuffer(chip * OPL2_CHANNEL_COUNT + channel), 1, audio->GetFrameSamples() / 2,
        1.0f / 4096.0f, ImVec2(160, 50), white);

    ImGui::PushFont(gui_default_font);

    double frequency = ch.f_number * (double)k_opl_native_sample_rate * pow(2.0, (double)ch.block - 20.0);

    if (ImGui::BeginTable("##opl_channel", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX))
    {
        ImGui::TableNextRow();
        draw_grid_label("F-NUMBER");
        ImGui::TextColored(white, "$%03X", ch.f_number);
        draw_grid_label("BLOCK");
        ImGui::TextColored(white, "%d", ch.block);

        ImGui::TableNextRow();
        draw_grid_label("NOTE");
        ImGui::TextColored(white, "%.2f Hz", frequency);
        draw_grid_label("TYPE");
        ImGui::TextColored(blue, "%s", k_opl_channel_types[ch.type & 0x03]);

        ImGui::TableNextRow();
        draw_grid_label("CONNECT");
        ImGui::TextColored(white, "%d", ch.connection);
        draw_grid_label("FEEDBACK");
        ImGui::TextColored(white, "%d", ch.feedback);

        ImGui::TableNextRow();
        draw_grid_label("PAN");
        ImGui::TextColored(ch.pan_left ? green : gray, "L"); ImGui::SameLine();
        ImGui::TextColored(ch.pan_right ? green : gray, "R");
        draw_grid_label("OUTPUT");
        ImGui::TextColored(white, "%d", opl->GetChannelOutput(channel));

        ImGui::EndTable();
    }

    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX | ImGuiTableFlags_RowBg;

    if (ImGui::BeginTable("##opl_operators", 3, flags))
    {
        ImGui::TableSetupColumn("OPERATOR");
        ImGui::TableSetupColumn("1");
        ImGui::TableSetupColumn("2");
        ImGui::TableHeadersRow();

        static const char* const rows[10] = { "KEY", "ENVELOPE", "LEVEL", "AR DR SL RR", "TL KSL", "MULT", "WAVE", "AM VIB",
            "EG KSR", "PHASE" };

        for (int row = 0; row < 10; row++)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(violet, "%s", rows[row]);

            for (int i = 0; i < 2; i++)
            {
                OPL::OPL_Operator& op = state->operators[ch.operators[i]];
                ImGui::TableNextColumn();

                switch (row)
                {
                    case 0: ImGui::TextColored(op.key ? green : gray, "%s", op.key ? "ON" : "OFF"); break;
                    case 1: ImGui::TextColored(blue, "%s", k_opl_envelope_names[op.envelope_state & 0x03]); break;
                    case 2: ImGui::TextColored(white, "$%03X", op.envelope); break;
                    case 3: ImGui::TextColored(white, "%X %X %X %X", op.attack_rate, op.decay_rate,
                        op.sustain_level & 0x0F, op.release_rate); break;
                    case 4: ImGui::TextColored(white, "$%02X %d", op.total_level, op.key_scale_level); break;
                    case 5: ImGui::TextColored(white, "%d", op.multiple); break;
                    case 6: ImGui::TextColored(white, "%d", op.waveform); break;
                    case 7: ImGui::TextColored(white, "%d %d", op.tremolo, op.vibrato); break;
                    case 8: ImGui::TextColored(white, "%d %d", op.sustain, op.key_scale_rate); break;
                    default: ImGui::TextColored(white, "$%03X", op.phase_output & 0x3FF); break;
                }
            }
        }

        ImGui::EndTable();
    }

    ImGui::PopFont();
}

void gui_debug_window_sound_blaster(void)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::SetNextWindowPos(ImVec2(220, 200), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360, 440), ImGuiCond_FirstUseEver);
    ImGui::Begin("Sound Blaster", &config_debug.show_sound_blaster);

    Audio* audio = emu_get_core()->GetAudio();
    SoundBlaster* blaster = audio->GetSoundBlaster();
    bool muted = audio->IsSourceMuted(Audio::AUDIO_SOURCE_PCM);

    if (draw_mute_button("##pcm_mute", muted, "Mute PCM"))
        audio->SetSourceMute(Audio::AUDIO_SOURCE_PCM, !muted);

    ImGui::SameLine();
    draw_stereo_scope("##pcm_mix", Audio::AUDIO_SOURCE_PCM, 1.0f / 32768.0f, ImVec2(200, 50));

    if (blaster->HasDSP())
    {
        ImGui::PushFont(gui_default_font);
        draw_dsp_transfer(blaster);

        if (blaster->HasMixer())
            draw_mixer(blaster);

        ImGui::PopFont();
    }
    else
        ImGui::TextColored(gray, "This card has no DSP");

    ImGui::End();
    ImGui::PopStyleVar();
}

static void draw_dsp_transfer(SoundBlaster* blaster)
{
    SoundBlaster::SoundBlaster_State* state = blaster->GetState();
    SoundBlaster::SoundBlaster_Transfer& transfer = state->transfer;
    float space = ImGui::CalcTextSize(" ").x;

    ImGui::TextColored(cyan, "DSP %d.%02d", blaster->GetVersionMajor(), blaster->GetVersionMinor());
    ImGui::Separator();

    if (!ImGui::BeginTable("##dsp", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX))
        return;

    ImGui::TableNextRow();
    draw_grid_label("IRQ");
    ImGui::TextColored(white, "%d", state->irq_line); ImGui::SameLine(0.0f, space);
    ImGui::TextColored(state->irq_8 ? yellow : gray, "8"); ImGui::SameLine(0.0f, space);
    ImGui::TextColored(state->irq_16 ? yellow : gray, "16"); ImGui::SameLine(0.0f, space);
    ImGui::TextColored(state->irq_mpu ? yellow : gray, "MPU");
    draw_grid_label("DMA");
    ImGui::TextColored(white, "%d %d", state->dma_8, state->dma_16);

    ImGui::TableNextRow();
    draw_grid_label("SPEAKER");
    ImGui::TextColored(state->speaker ? green : gray, "%s", state->speaker ? "ON" : "OFF");
    draw_grid_label("RATE");
    ImGui::TextColored(white, "%u Hz", blaster->GetSampleRate());

    ImGui::TableNextRow();
    draw_grid_label("TRANSFER");
    ImGui::TextColored(transfer.active ? green : gray, "%s", transfer.active ? (transfer.paused ? "PAUSED" : "RUNNING") : "IDLE");
    draw_grid_label("FORMAT");
    ImGui::TextColored(blue, "%s", k_dsp_format_names[MIN(transfer.format, (u8)5)]);

    ImGui::TableNextRow();
    draw_grid_label("BLOCK");
    ImGui::TextColored(white, "%u", transfer.length);
    draw_grid_label("LEFT");
    ImGui::TextColored(white, "%u", transfer.remaining);

    ImGui::TableNextRow();
    draw_grid_label("MODE");
    ImGui::TextColored(transfer.auto_init ? green : gray, "AUTO"); ImGui::SameLine(0.0f, space);
    ImGui::TextColored(transfer.high_speed ? green : gray, "HIGH");
    draw_grid_label("");
    ImGui::TextColored(blaster->IsStereo() ? green : gray, "STEREO"); ImGui::SameLine(0.0f, space);
    ImGui::TextColored(transfer.input ? green : gray, "INPUT");

    ImGui::TableNextRow();
    draw_grid_label("LAST CMD");
    ImGui::TextColored(white, "$%02X", state->command);
    draw_grid_label("MIDI");
    ImGui::TextColored(state->midi_uart ? green : gray, "%s", state->midi_uart ? "UART" : "OFF");

    ImGui::EndTable();
}

static void draw_mixer(SoundBlaster* blaster)
{
    SoundBlaster::SoundBlaster_State* state = blaster->GetState();

    ImGui::NewLine();
    ImGui::TextColored(cyan, "MIXER");
    ImGui::Separator();

    if (!ImGui::BeginTable("##mixer", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX))
        return;

    ImGui::TableNextRow();
    draw_grid_label("VOICE");
    ImGui::TextColored(white, "%.2f %.2f", blaster->GetGain(SoundBlaster::SOUND_BLASTER_SOURCE_VOICE, false) / 32768.0,
        blaster->GetGain(SoundBlaster::SOUND_BLASTER_SOURCE_VOICE, true) / 32768.0);
    draw_grid_label("FM");
    ImGui::TextColored(white, "%.2f %.2f", blaster->GetGain(SoundBlaster::SOUND_BLASTER_SOURCE_FM, false) / 32768.0,
        blaster->GetGain(SoundBlaster::SOUND_BLASTER_SOURCE_FM, true) / 32768.0);

    ImGui::TableNextRow();
    draw_grid_label("INDEX");
    ImGui::TextColored(white, "$%02X", state->mixer_index);
    draw_grid_label("FILTER");
    ImGui::TextColored(white, "%.0f Hz", blaster->GetFilterCutoff());

    ImGui::EndTable();
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

    IdeCdRom* drive = core->GetCDROM();

    if (IsValidPointer(drive) && drive->IsAttached())
    {
        s32 gains[4];
        drive->GetAudioGains(gains);

        ImGui::NewLine(); ImGui::TextColored(cyan, "DRIVE GAINS"); ImGui::Separator();

        if (ImGui::BeginTable("##cdda_gains", 3, flags))
        {
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, character * 9);
            ImGui::TableSetupColumn("LEFT", ImGuiTableColumnFlags_WidthFixed, character * 7);
            ImGui::TableSetupColumn("RIGHT", ImGuiTableColumnFlags_WidthFixed, character * 7);
            ImGui::TableHeadersRow();

            for (int i = 0; i < 2; i++)
            {
                char label[8];
                snprintf(label, sizeof(label), "PORT %d", i);
                ImGui::TableNextRow();
                draw_grid_label(label);
                ImGui::TextColored(gains[i * 2] != 0 ? white : gray, "%d", gains[i * 2]);
                ImGui::TableNextColumn();
                ImGui::TextColored(gains[(i * 2) + 1] != 0 ? white : gray, "%d", gains[(i * 2) + 1]);
            }

            ImGui::EndTable();
        }
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
