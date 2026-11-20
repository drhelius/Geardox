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
static const int k_gus_history = 128;
static const float k_gus_db_per_step = 0.0235f;

static float* scope_buffer = NULL;
static float gus_history[ULTRASOUND_VOICES][k_gus_history];
static int gus_history_position = 0;

static void draw_scope(const char* id, const s16* data, int stride, int count, float scale, ImVec2 size, const ImVec4& color);
static void draw_stereo_scope(const char* id, int source, float scale, ImVec2 size);
static bool draw_mute_button(const char* id, bool muted, const char* tooltip);
static void draw_grid_label(const char* label);
static void draw_position(u32 lba, u32 pregap, bool valid);
static void draw_opl_global(OPL* opl);
static void draw_opl_channel(Audio* audio, OPL* opl, int chip, int channel);
static void draw_dsp_transfer(SoundBlaster* blaster);
static void draw_mixer(SoundBlaster* blaster);
static void draw_mpu(Audio* audio);
static void draw_midi_synth(Audio* audio);
static void draw_gus_global(UltraSound* gus);
static void draw_gus_voices(UltraSound* gus);
static void draw_gus_flag(const char* label, bool on);
static void draw_gus_irq_flag(bool enabled, bool pending);

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
    ImGui::SetNextWindowSize(ImVec2(360, 640), ImGuiCond_FirstUseEver);
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

    if (audio->HasMPU())
    {
        ImGui::PushFont(gui_default_font);
        draw_mpu(audio);
        ImGui::PopFont();
    }

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

static void draw_mpu(Audio* audio)
{
    MPU401* mpu = audio->GetMPU401();
    MPU401::MPU401_State* state = mpu->GetState();
    bool intelligent = mpu->IsIntelligent();
    float space = ImGui::CalcTextSize(" ").x;
    float character = ImGui::CalcTextSize("0").x;
    int irq = audio->IsMPUStandalone() ? 9 : emu_get_core()->GetMachineConfig().sound_irq;
    static const char* const k_records[3] = { "OFF", "STANDBY", "ON" };

    ImGui::NewLine();
    ImGui::TextColored(cyan, "MPU-401");
    ImGui::Separator();

    if (ImGui::BeginTable("##mpu", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX))
    {
        ImGui::TableNextRow();
        draw_grid_label("MODE");
        ImGui::TextColored(blue, "%s", intelligent ? "INTELLIGENT" : "UART ONLY");
        draw_grid_label("UART");
        ImGui::TextColored(state->uart ? green : gray, "%s", state->uart ? "ON" : "OFF");

        ImGui::TableNextRow();
        draw_grid_label("FIFO");
        ImGui::TextColored(state->count != 0 ? yellow : gray, "%d", state->count);
        draw_grid_label("IRQ");
        ImGui::TextColored(white, "%d", irq);

        if (intelligent)
        {
            ImGui::TableNextRow();
            draw_grid_label("PLAY");
            ImGui::TextColored(state->playing ? green : gray, "%s", state->playing ? "ON" : "OFF");
            draw_grid_label("RECORD");
            ImGui::TextColored(state->recording != MPU401::MPU401_RECORD_OFF ? green : gray, "%s",
                k_records[state->recording < 3 ? state->recording : 0]);

            ImGui::TableNextRow();
            draw_grid_label("RUNNING");
            ImGui::TextColored(state->running ? green : gray, "%s", state->running ? "ON" : "OFF");
            draw_grid_label("TIMEBASE");
            ImGui::TextColored(white, "%d", state->timebase);

            ImGui::TableNextRow();
            draw_grid_label("TEMPO");
            ImGui::TextColored(white, "%u BPM", mpu->GetEffectiveTempo());
            draw_grid_label("RAW");
            ImGui::TextColored(white, "%d", state->tempo); ImGui::SameLine(0.0f, space);
            ImGui::TextColored(white, "$%02X", state->relative);

            ImGui::TableNextRow();
            draw_grid_label("CLOCK->HOST");
            bool clock_to_host = (state->switches & k_mpu401_switch_clock_to_host) != 0;
            ImGui::TextColored(clock_to_host ? green : gray, "%s", clock_to_host ? "ON" : "OFF");
            draw_grid_label("RATE");
            ImGui::TextColored(white, "$%02X", state->cth_rate);
        }

        ImGui::EndTable();
    }

    draw_midi_synth(audio);

    if (!intelligent)
        return;

    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX | ImGuiTableFlags_RowBg;

    if (ImGui::BeginTable("##mpu_tracks", 5, flags))
    {
        ImGui::TableSetupColumn("TRACK", ImGuiTableColumnFlags_WidthFixed, character * 10);
        ImGui::TableSetupColumn("ACTIVE", ImGuiTableColumnFlags_WidthFixed, character * 7);
        ImGui::TableSetupColumn("COUNT", ImGuiTableColumnFlags_WidthFixed, character * 6);
        ImGui::TableSetupColumn("WAIT", ImGuiTableColumnFlags_WidthFixed, character * 5);
        ImGui::TableSetupColumn("STATUS", ImGuiTableColumnFlags_WidthFixed, character * 7);
        ImGui::TableHeadersRow();

        for (int i = 0; i < MPU401_TRACKS + 1; i++)
        {
            bool conductor = i == MPU401_TRACKS;
            MPU401::MPU401_Track& track = conductor ? state->conductor : state->tracks[i];
            bool active = conductor ? state->conductor_on : (state->active & (1 << i)) != 0;
            char label[16];

            if (conductor)
                snprintf(label, sizeof(label), "CONDUCTOR");
            else
                snprintf(label, sizeof(label), "%d", i + 1);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(violet, "%s", label);
            ImGui::TableNextColumn();
            ImGui::TextColored(active ? green : gray, "%s", active ? "ON" : "OFF");
            ImGui::TableNextColumn();
            ImGui::TextColored(active ? white : gray, "$%02X", track.counter);
            ImGui::TableNextColumn();
            ImGui::TextColored(track.waiting ? yellow : gray, "%s", track.waiting ? "YES" : "NO");
            ImGui::TableNextColumn();

            if (conductor)
                ImGui::TextColored(gray, "--");
            else
                ImGui::TextColored(track.status != 0 ? white : gray, "$%02X", track.status);
        }

        ImGui::EndTable();
    }
}

void gui_debug_window_ultrasound(void)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::SetNextWindowPos(ImVec2(240, 160), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(700, 760), ImGuiCond_FirstUseEver);
    ImGui::Begin("UltraSound", &config_debug.show_ultrasound);

    Audio* audio = emu_get_core()->GetAudio();

    if (!audio->HasGUS())
    {
        ImGui::TextColored(gray, "The Gravis UltraSound is off");
        ImGui::End();
        ImGui::PopStyleVar();
        return;
    }

    UltraSound* gus = audio->GetUltraSound();
    bool muted = audio->IsSourceMuted(Audio::AUDIO_SOURCE_GUS);

    if (draw_mute_button("##gus_mute", muted, "Mute GUS"))
        audio->SetSourceMute(Audio::AUDIO_SOURCE_GUS, !muted);

    ImGui::SameLine();
    draw_stereo_scope("##gus_mix", Audio::AUDIO_SOURCE_GUS, 1.0f / 32768.0f, ImVec2(200, 50));

    for (int i = 0; i < ULTRASOUND_VOICES; i++)
        gus_history[i][gus_history_position] = CLAMP((float)gus->GetVoiceOutput(i) / 16384.0f, -1.0f, 1.0f);

    gus_history_position = (gus_history_position + 1) % k_gus_history;

    ImGui::PushFont(gui_default_font);
    draw_gus_global(gus);
    draw_gus_voices(gus);
    ImGui::PopFont();

    ImGui::End();
    ImGui::PopStyleVar();
}

static void draw_gus_global(UltraSound* gus)
{
    UltraSound::UltraSound_State* state = gus->GetState();
    float space = ImGui::CalcTextSize(" ").x;
    u8 status = gus->GetIRQStatus();
    GDOX_Machine_Config config = emu_get_core()->GetMachineConfig();
    int dram_channel = gus->GetDRAMChannel();
    int adc_channel = gus->GetADCChannel();
    int midi_irq = (state->irq_latch & 0x40) != 0 ? gus->GetIRQ(0) : gus->GetIRQ(1);
    u32 dram_address = state->dram_address & (ULTRASOUND_DRAM_SIZE - 1);

    ImGui::NewLine();
    ImGui::TextColored(cyan, "GF1 AT 240h, IRQ %d, DMA %d", get_gus_irq(config), get_gus_dma(config));
    ImGui::Separator();

    if (ImGui::BeginTable("##gus_global", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX))
    {
        ImGui::TableNextRow();
        draw_grid_label("RESET 4Ch");
        ImGui::TextColored(white, "$%02X", state->reset); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((state->reset & 0x01) != 0 ? green : gray, "RUN"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((state->reset & 0x02) != 0 ? green : gray, "DAC"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((state->reset & 0x04) != 0 ? green : gray, "IRQ");
        draw_grid_label("VOICES");
        ImGui::TextColored(white, "%d", gus->GetActiveVoices()); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(white, "%u Hz", gus->GetFrameRate());

        ImGui::TableNextRow();
        draw_grid_label("MIX 2X0");
        ImGui::TextColored(white, "$%02X", state->mix_control); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(gus->IsOutputEnabled() ? green : gray, "OUT"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((state->mix_control & 0x08) != 0 ? green : gray, "LATCH"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((state->mix_control & 0x40) != 0 ? yellow : gray, "IRQSEL");
        draw_grid_label("STATUS 2X6");
        ImGui::TextColored(white, "$%02X", status); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((status & 0x80) != 0 ? yellow : gray, "DMA"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((status & 0x40) != 0 ? yellow : gray, "VOL"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((status & 0x20) != 0 ? yellow : gray, "WAVE"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((status & 0x0C) != 0 ? yellow : gray, "TMR"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((status & 0x03) != 0 ? yellow : gray, "MIDI");

        ImGui::TableNextRow();
        draw_grid_label("IRQ LATCH");
        ImGui::TextColored(white, "$%02X", state->irq_latch); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(white, "GF1 %d", gus->GetIRQ(0)); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(white, "MIDI %d", midi_irq); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((state->irq_latch & 0x40) != 0 ? green : gray, "COMBINE");
        draw_grid_label("DMA LATCH");
        ImGui::TextColored(white, "$%02X", state->dma_latch); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(white, "DRAM %d", dram_channel); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(white, "ADC %d", adc_channel); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((state->dma_latch & 0x40) != 0 ? green : gray, "COMBINE");

        ImGui::TableNextRow();
        draw_grid_label("VOICE IRQ 8Fh");
        ImGui::TextColored(state->voice_irq_latched ? yellow : gray, "$%02X", state->voice_irq);
        draw_grid_label("SELECT");
        ImGui::TextColored(white, "V%02d R$%02X", state->voice_select, state->register_select);

        ImGui::TableNextRow();
        draw_grid_label("DRAM DMA 41h");
        ImGui::TextColored(white, "$%02X", state->dma_control); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((state->dma_control & 0x01) != 0 ? green : gray, "RUN"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((state->dma_control & 0x02) != 0 ? blue : gray, "READ"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((state->dma_control & 0x04) != 0 ? blue : gray, "16"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((state->dma_control & 0x20) != 0 ? green : gray, "IRQ"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((state->dma_control & 0x80) != 0 ? green : gray, "INV"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(state->dma_tc ? yellow : gray, "TC");
        draw_grid_label("DMA 42h");
        ImGui::TextColored(white, "$%04X0", state->dma_address); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(white, "PTR $%05X", state->dma_pointer & (ULTRASOUND_DRAM_SIZE - 1));

        ImGui::TableNextRow();
        draw_grid_label("DRAM 43h/44h");
        ImGui::TextColored(white, "$%05X", dram_address);
        draw_grid_label("SAMPLING 49h");
        ImGui::TextColored(white, "$%02X", state->sample_control); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((state->sample_control & 0x01) != 0 ? green : gray, "RUN"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((state->sample_control & 0x20) != 0 ? green : gray, "IRQ"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(state->sample_tc ? yellow : gray, "TC"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(white, "RATE $%02X", state->sample_rate);

        for (int i = 0; i < 2; i++)
        {
            char label[16];
            snprintf(label, sizeof(label), "TIMER %d", i + 1);

            ImGui::TableNextRow();
            draw_grid_label(label);
            ImGui::TextColored(white, "$%02X/$%02X", state->timer_count[i], state->timer_preload[i]);
            ImGui::SameLine(0.0f, space);
            ImGui::TextColored((state->adlib_control & (1 << i)) != 0 ? green : gray, "RUN");
            ImGui::SameLine(0.0f, space);
            ImGui::TextColored((state->adlib_control & (i == 0 ? 0x40 : 0x20)) != 0 ? yellow : gray, "MASK");
            ImGui::SameLine(0.0f, space);
            ImGui::TextColored((state->adlib_status & (i == 0 ? 0x40 : 0x20)) != 0 ? yellow : gray, "FLAG");
            ImGui::SameLine(0.0f, space);
            ImGui::TextColored((state->timer_control & (i == 0 ? 0x04 : 0x08)) != 0 ? green : gray, "IRQ");
            ImGui::SameLine(0.0f, space);
            ImGui::TextColored(state->timer_irq[i] ? yellow : gray, "PEND");

            if (i == 0)
            {
                draw_grid_label("45h");
                ImGui::TextColored(white, "$%02X", state->timer_control);
            }
            else
            {
                draw_grid_label("ADLIB");
                ImGui::TextColored(white, "S$%02X C$%02X ST$%02X", state->adlib_select, state->adlib_control,
                    state->adlib_status);
            }
        }

        ImGui::TableNextRow();
        draw_grid_label("MIDI");
        ImGui::TextColored(white, "$%02X", state->midi_control); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((state->midi_control & 0x03) == 0x03 ? yellow : gray, "RESET"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((state->midi_control & 0x60) == 0x20 ? green : gray, "TXIRQ"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored((state->midi_control & 0x80) != 0 ? green : gray, "RXIRQ");
        draw_grid_label("FIFO");
        ImGui::TextColored(state->midi_tx_clocks != 0 ? yellow : gray, "TX"); ImGui::SameLine(0.0f, space);
        ImGui::TextColored(state->midi_rx_full ? yellow : gray, "RX $%02X", state->midi_rx);

        ImGui::EndTable();
    }
}

static void draw_gus_flag(const char* label, bool on)
{
    ImGui::TextColored(on ? green : gray, "%s", label);
    ImGui::SameLine(0.0f, ImGui::CalcTextSize(" ").x);
}

static void draw_gus_irq_flag(bool enabled, bool pending)
{
    ImGui::TextColored(pending ? yellow : (enabled ? green : gray), "IR");
    ImGui::SameLine(0.0f, ImGui::CalcTextSize(" ").x);
}

static void draw_gus_voices(UltraSound* gus)
{
    UltraSound::UltraSound_State* state = gus->GetState();
    float space = ImGui::CalcTextSize(" ").x;
    float character = ImGui::CalcTextSize("0").x;
    int active = gus->GetActiveVoices();
    double frame_rate = (double)gus->GetFrameRate();
    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollX;

    ImGui::NewLine();
    ImGui::TextColored(cyan, "VOICES");
    ImGui::Separator();

    if (!ImGui::BeginTable("##gus_voices", 10, flags))
        return;

    ImGui::TableSetupColumn("V", ImGuiTableColumnFlags_WidthFixed, character * 3);
    ImGui::TableSetupColumn("ADDRESS", ImGuiTableColumnFlags_WidthFixed, character * 10);
    ImGui::TableSetupColumn("START", ImGuiTableColumnFlags_WidthFixed, character * 10);
    ImGui::TableSetupColumn("END", ImGuiTableColumnFlags_WidthFixed, character * 10);
    ImGui::TableSetupColumn("RATE", ImGuiTableColumnFlags_WidthFixed, character * 10);
    ImGui::TableSetupColumn("VOLUME", ImGuiTableColumnFlags_WidthFixed, character * 9);
    ImGui::TableSetupColumn("PAN", ImGuiTableColumnFlags_WidthFixed, character * 3);
    ImGui::TableSetupColumn("WAVE", ImGuiTableColumnFlags_WidthFixed, character * 16);
    ImGui::TableSetupColumn("RAMP", ImGuiTableColumnFlags_WidthFixed, character * 15);
    ImGui::TableSetupColumn("SCOPE", ImGuiTableColumnFlags_WidthFixed, 96.0f);
    ImGui::TableHeadersRow();

    for (int i = 0; i < ULTRASOUND_VOICES; i++)
    {
        UltraSound::UltraSound_Voice& voice = state->voices[i];
        bool in_use = i < active;
        bool wave_run = (voice.control & 0x03) == 0;
        bool ramp_run = (voice.volume_control & 0x03) == 0;
        int level = voice.volume >> 4;
        float db = (float)(level - 4095) * k_gus_db_per_step;
        ImVec4 text = in_use ? white : gray;

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(in_use ? violet : gray, "%02d", i);
        ImGui::TableNextColumn();
        ImGui::TextColored(text, "%05X.%03X", (voice.address >> 9) & 0xFFFFF, voice.address & 0x1FF);
        ImGui::TableNextColumn();
        ImGui::TextColored(text, "%05X.%03X", (voice.start >> 9) & 0xFFFFF, voice.start & 0x1FF);
        ImGui::TableNextColumn();
        ImGui::TextColored(text, "%05X.%03X", (voice.end >> 9) & 0xFFFFF, voice.end & 0x1FF);
        ImGui::TableNextColumn();
        ImGui::TextColored(wave_run && in_use ? orange : gray, "%.1f Hz",
            (double)voice.frequency / 1024.0 * frame_rate);
        ImGui::TableNextColumn();

        if (level == 0)
            ImGui::TextColored(gray, "-inf dB");
        else
            ImGui::TextColored(text, "%.1f dB", db);

        ImGui::TableNextColumn();
        ImGui::TextColored(text, "%X", voice.pan & 0x0F);
        ImGui::TableNextColumn();
        u8 control = voice.control;
        u8 ramp = voice.volume_control;
        ImGui::TextColored(wave_run && in_use ? green : gray, "%s", wave_run ? "RUN" : "STOP");
        ImGui::SameLine(0.0f, space);
        ImGui::TextColored((control & 0x04) != 0 ? blue : gray, "%s", (control & 0x04) != 0 ? "16" : "8");
        ImGui::SameLine(0.0f, space);
        draw_gus_flag("LP", (control & 0x08) != 0);
        draw_gus_flag("BI", (control & 0x10) != 0);
        draw_gus_irq_flag((control & 0x20) != 0, (control & 0x80) != 0);
        ImGui::TextColored((control & 0x40) != 0 ? yellow : gray, "%s", (control & 0x40) != 0 ? "<" : ">");
        ImGui::TableNextColumn();
        ImGui::TextColored(ramp_run && in_use ? green : gray, "%s", ramp_run ? ((ramp & 0x40) != 0 ? "DN" : "UP") : "--");
        ImGui::SameLine(0.0f, space);
        draw_gus_flag("LP", (ramp & 0x08) != 0);
        draw_gus_flag("BI", (ramp & 0x10) != 0);
        draw_gus_irq_flag((ramp & 0x20) != 0, (ramp & 0x80) != 0);
        ImGui::TextColored(text, "$%02X", voice.ramp_rate);
        ImGui::TableNextColumn();

        char id[16];
        snprintf(id, sizeof(id), "##gus_scope%d", i);
        ImGui::PushStyleColor(ImGuiCol_PlotLines, in_use ? accent : gray);
        ImGui::PlotLines(id, gus_history[i], k_gus_history, gus_history_position, NULL, -1.0f, 1.0f,
            ImVec2(96.0f, character * 1.4f));
        ImGui::PopStyleColor();
    }

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

    static const char* sources[Audio::AUDIO_SOURCE_COUNT] = { "FM", "PCM", "CD-DA", "Buzzer", "GUS", "MIDI" };

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

static void draw_scope(const char* id, const s16* data, int stride, int count, float scale, ImVec2 size, const ImVec4& color)
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

static void draw_midi_synth(Audio* audio)
{
    MidiSynth::MidiSynth_Status status;
    audio->GetMidiSynth()->GetStatus(&status);
    static const char* const k_engines[GDOX_MIDI_ENGINE_COUNT] = { "PORT", "MT-32", "SOUNDFONT" };

    ImGui::NewLine();
    ImGui::TextColored(cyan, "MIDI SYNTH");
    ImGui::Separator();

    bool muted = audio->IsSourceMuted(Audio::AUDIO_SOURCE_MIDI);

    if (draw_mute_button("##midi_mute", muted, "Mute MIDI"))
        audio->SetSourceMute(Audio::AUDIO_SOURCE_MIDI, !muted);

    ImGui::SameLine();
    draw_stereo_scope("##midi_mix", Audio::AUDIO_SOURCE_MIDI, 1.0f / 32768.0f, ImVec2(200, 50));

    if (!ImGui::BeginTable("##midi_synth", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX))
        return;

    bool builtin = status.engine != GDOX_MIDI_ENGINE_NONE;

    ImGui::TableNextRow();
    draw_grid_label("ENGINE");
    ImGui::TextColored(builtin ? green : gray, "%s", k_engines[status.engine]);
    draw_grid_label("VOICES");
    ImGui::TextColored(status.voices > 0 ? yellow : gray, "%d/%d", status.voices, status.max_voices);

    ImGui::TableNextRow();
    draw_grid_label("RENDER");
    ImGui::TextColored(white, "%.2f ms", status.render_ms);
    draw_grid_label("DROPPED");
    ImGui::TextColored(status.dropped > 0 ? red : gray, "%u", status.dropped);

    ImGui::TableNextRow();
    draw_grid_label("MT-32");
    ImGui::TextColored(status.mt32_open ? white : gray, "%s", status.mt32_open ? status.mt32_name : "-");
    draw_grid_label("SOUNDFONT");
    ImGui::TextColored(status.soundfont_open ? white : gray, "%s", status.soundfont_open ?
        status.soundfont_path : "-");

    ImGui::EndTable();
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
