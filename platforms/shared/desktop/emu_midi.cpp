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

#define EMU_MIDI_IMPORT
#include "emu_midi.h"
#include <SDL3/SDL.h>
#include "config.h"
#include "emu.h"
#include "emu_game.h"
#include "gui_notifications.h"
#include "midi_port.h"
#include "midi_queue.h"

static const u64 k_emu_midi_retry_ns = 2000000000ULL;
static const int k_emu_midi_drain_ms = 500;

static bool emu_midi_stopped = false;
static bool emu_midi_rewinding = false;
static bool emu_midi_notified = false;
static u64 emu_midi_last_try = 0;

static MidiOut* get_midi(void);
static void update_port(MidiOut* midi, bool force);
static void sync(MidiOut* midi);
static void silence(MidiOut* midi);
static void notify(void);

void emu_midi_init(void)
{
    midi_queue_init();
}

void emu_midi_destroy(void)
{
    MidiOut* midi = get_midi();

    if (IsValidPointer(midi) && midi_queue_is_open())
    {
        silence(midi);
        midi_queue_drain(k_emu_midi_drain_ms);
    }

    midi_queue_destroy();
}

// Called just before a frame runs: the port follows the game, and the synth catches up with what changed since the
// last frame, a reset, a stop or a rewind, before any of the frame's own messages
void emu_midi_begin_frame(void)
{
    MidiOut* midi = get_midi();

    if (!IsValidPointer(midi))
        return;

    update_port(midi, false);
    bool open = midi_queue_is_open();

    if (midi->TakeReset())
    {
        emu_midi_notified = false;

        if (open)
            sync(midi);
    }

    if ((emu_midi_stopped || emu_midi_rewinding) && open)
        sync(midi);

    emu_midi_stopped = false;
    emu_midi_rewinding = false;

    MidiOut::MidiOut_Capture capture = MidiOut::MIDI_OUT_CAPTURE_OFF;

    if (open)
        capture = config_emulator.ffwd || !config_audio.enable || config_audio.midi_mute ?
            MidiOut::MIDI_OUT_CAPTURE_SILENT : MidiOut::MIDI_OUT_CAPTURE_ON;

    MidiOut::MidiOut_Capture current = midi->GetCapture();

    if ((current == MidiOut::MIDI_OUT_CAPTURE_ON) && (capture == MidiOut::MIDI_OUT_CAPTURE_SILENT))
        silence(midi);
    else if ((current == MidiOut::MIDI_OUT_CAPTURE_SILENT) && (capture == MidiOut::MIDI_OUT_CAPTURE_ON))
        sync(midi);

    midi->SetCapture(capture);
}

// Called after the frame's samples are queued, so its messages play with them
void emu_midi_end_frame(int sample_count)
{
    MidiOut* midi = get_midi();

    if (!IsValidPointer(midi))
        return;

    midi_queue_frame(midi, sample_count / 2);

    if (midi->HasPlayed() && !midi_queue_is_open() && !emu_midi_notified)
        notify();
}

// Paused, stopped in the debugger, powered off: nothing plays until the next frame brings the synth back in step
void emu_midi_idle(void)
{
    if (emu_midi_stopped)
        return;

    emu_midi_stopped = true;
    MidiOut* midi = get_midi();

    if (IsValidPointer(midi) && midi_queue_is_open())
        silence(midi);
}

void emu_midi_rewind(void)
{
    MidiOut* midi = get_midi();

    if (!IsValidPointer(midi))
        return;

    if (!emu_midi_rewinding)
    {
        emu_midi_rewinding = true;

        if (midi_queue_is_open())
            silence(midi);
    }

    midi->SetCapture(MidiOut::MIDI_OUT_CAPTURE_OFF);
}

void emu_midi_state_loaded(void)
{
    MidiOut* midi = get_midi();

    if (IsValidPointer(midi) && midi_queue_is_open())
        sync(midi);
}

void emu_midi_update_port(void)
{
    MidiOut* midi = get_midi();

    if (IsValidPointer(midi))
        update_port(midi, true);
}

int emu_midi_get_synth(void)
{
    int synth = emu_game_get_midi();
    return synth >= 0 ? synth : CLAMP(config_audio.midi_default, 0, GDOX_MIDI_SYNTH_COUNT - 1);
}

const char* emu_midi_get_synth_port(void)
{
    return emu_midi_get_synth() == GDOX_MIDI_SYNTH_MT32 ? config_audio.midi_mt32_port.c_str() :
        config_audio.midi_gm_port.c_str();
}

const char* emu_midi_get_wanted_port(void)
{
    const char* port = emu_game_get_midi_port();
    return port[0] != 0 ? port : emu_midi_get_synth_port();
}

const char* emu_midi_get_port(void)
{
    return midi_queue_is_open() ? midi_queue_get_port() : "";
}

static MidiOut* get_midi(void)
{
    GeardoxCore* core = emu_get_core();
    return IsValidPointer(core) ? core->GetAudio()->GetMidiOut() : NULL;
}

// A port that went away, or a synth started after Geardox, is tried again every two seconds. A newly opened synth
// knows nothing, so it gets everything the machine has sent
static void update_port(MidiOut* midi, bool force)
{
    const char* wanted = emu_midi_get_wanted_port();
    u64 now = SDL_GetTicksNS();
    bool due = force || (emu_midi_last_try == 0) || ((now - emu_midi_last_try) >= k_emu_midi_retry_ns);

    if (due)
        emu_midi_last_try = now;

    if (due && midi_queue_is_open())
        midi_queue_check();

    if (midi_queue_is_open())
    {
        bool lost = midi_queue_is_lost();

        if (!lost && (wanted[0] != 0) && MidiPort::IsSamePort(wanted, midi_queue_get_port()))
            return;

        if (lost)
            Log("MIDI Out: %s stopped taking messages", midi_queue_get_port());
        else
        {
            silence(midi);
            midi_queue_drain(k_emu_midi_drain_ms);
        }

        midi_queue_close();
        midi->ForgetSynth();
        midi->SetCapture(MidiOut::MIDI_OUT_CAPTURE_OFF);
    }

    if ((wanted[0] == 0) || !due || !midi_queue_open(wanted))
        return;

    midi->ForgetSynth();
    sync(midi);
}

static void sync(MidiOut* midi)
{
    midi->Sync();
    midi_queue_flush(midi);
}

static void silence(MidiOut* midi)
{
    midi->Silence();
    midi_queue_flush(midi);
}

static void notify(void)
{
    emu_midi_notified = true;
    const char* wanted = emu_midi_get_wanted_port();
    const char* synth = k_midi_synths[emu_midi_get_synth()].name;
    char message[256];

    if (!MidiPort::IsSupported())
        return;

    if (wanted[0] == 0)
    {
        snprintf(message, sizeof(message), "This game is playing %s music, and no %s port is set", synth, synth);
        gui_notify(gui_NotificationWarning, ICON_MD_MUSIC_NOTE, message, "Choose one in Audio > MIDI Out.", "midi");
    }
    else
    {
        snprintf(message, sizeof(message), "This game is playing %s music, and %s isn't there", synth, wanted);
        gui_notify(gui_NotificationWarning, ICON_MD_MUSIC_NOTE, message,
            "Start the synth, or choose another port in Audio > MIDI Out.", "midi");
    }
}
