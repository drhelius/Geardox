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

#include "mpu401.h"
#include "midi_out.h"
#include "../common/state_serializer.h"
#include "../common/trace_logger.h"

static const u8 k_mpu401_command_reset = 0xFF;
static const u8 k_mpu401_command_uart = 0x3F;
static const u8 k_mpu401_ack = 0xFE;
static const u8 k_mpu401_no_data = 0x80;
static const u8 k_mpu401_version = 0x15;
static const u8 k_mpu401_revision = 0x01;
static const u8 k_mpu401_overflow = 0xF8;
static const u8 k_mpu401_measure_end = 0xF9;
static const u8 k_mpu401_end = 0xFC;
static const u8 k_mpu401_clock = 0xFD;
static const u8 k_mpu401_max_timing = 240;
static const u16 k_mpu401_one_way_all_notes_off = 1 << 0;
static const u16 k_mpu401_one_way_no_real_time = 1 << 2;
static const u8 k_mpu401_send_none = 0;
static const u8 k_mpu401_send_voice = 1;
static const u8 k_mpu401_send_system = 2;
static const u8 k_mpu401_tempo_min[7] = { 32, 16, 16, 8, 8, 8, 8 };
static const u8 k_mpu401_tempo_max[7] = { 240, 240, 240, 240, 208, 179, 179 };
static const u8 k_mpu401_voice_lengths[8] = { 3, 3, 3, 3, 2, 2, 3, 0 };

MPU401::MPU401()
{
    InitPointer(m_midi_out);
    InitPointer(m_trace_logger);
    m_intelligent = true;
    m_rate = 0;
    Reset();
}

MPU401::~MPU401()
{
}

void MPU401::Init(MidiOut* midi_out)
{
    m_midi_out = midi_out;
}

void MPU401::SetTraceLogger(TraceLogger* trace_logger)
{
    m_trace_logger = trace_logger;
}

void MPU401::SetIntelligent(bool intelligent)
{
    m_intelligent = intelligent;
}

void MPU401::Reset()
{
    PowerUp();
    m_state.last_data = 0;
}

void MPU401::PowerUp()
{
    u8 last_data = m_state.last_data;

    memset(&m_state, 0, sizeof(m_state));
    m_state.last_data = last_data;
    m_state.switches = k_mpu401_switch_thru | k_mpu401_switch_measure_end | k_mpu401_switch_real_time_affection;
    m_state.tempo = 100;
    m_state.relative = 0x40;
    m_state.timebase = 120;
    m_state.metro_clocks = 12;
    m_state.metro_measure = 8;
    m_state.cth_rate = 0xF0;
    m_state.channels = 0xFFFF;

    for (int i = 0; i < MPU401_TABLES; i++)
    {
        m_state.tables[i].channel = (u8)i;
        m_state.tables[i].on = true;
    }

    UpdateRate();
}

// Reading the data port takes the byte and clears the interrupt with it
u8 MPU401::Read(u16 port)
{
    if ((port & 0x01) != 0)
        return Peek(port);

    if (m_state.count != 0)
    {
        m_state.last_data = m_state.queue[m_state.head];
        m_state.head = (m_state.head + 1) % MPU401_QUEUE_SIZE;
        m_state.count--;
    }

    return m_state.last_data;
}

// Bit 7 low means a byte for the host, bit 6 low means ready for one, and nothing ever keeps it from being ready
u8 MPU401::Peek(u16 port) const
{
    if ((port & 0x01) == 0)
        return m_state.count != 0 ? m_state.queue[m_state.head] : m_state.last_data;

    return (u8)((m_state.count != 0 ? 0x00 : k_mpu401_no_data) | 0x3F);
}

void MPU401::Write(u16 port, u8 value)
{
    if ((port & 0x01) == 0)
    {
        if (unlikely(IsValidPointer(m_trace_logger) && m_trace_logger->IsEventEnabled(TRACE_MIDI, TRACE_MIDI_DATA)))
        {
            GDOX_Trace_Entry entry = {};
            entry.type = TRACE_MIDI;
            entry.event = TRACE_MIDI_DATA;
            entry.midi.value = value;
            entry.midi.source = TRACE_MIDI_SOURCE_MPU;
            m_trace_logger->TraceLog(entry);
        }

        if (m_state.uart)
        {
            if (IsValidPointer(m_midi_out))
                m_midi_out->Write(value);
        }
        else if (m_intelligent)
        {
            WriteData(value);
            Deliver();
        }

        return;
    }

    if (unlikely(IsValidPointer(m_trace_logger) && m_trace_logger->IsEventEnabled(TRACE_MIDI, TRACE_MIDI_COMMAND)))
    {
        GDOX_Trace_Entry entry = {};
        entry.type = TRACE_MIDI;
        entry.event = TRACE_MIDI_COMMAND;
        entry.midi.value = value;
        entry.midi.source = TRACE_MIDI_SOURCE_MPU;
        m_trace_logger->TraceLog(entry);
    }

    if (m_intelligent && !m_state.uart)
    {
        Command(value, false);
        Deliver();
        return;
    }

    WriteUartOnly(value);
}

// Roland has no ACK on leaving UART mode. Creative's detection reads one back from FFh, and that's what software does
void MPU401::WriteUartOnly(u8 value)
{
    if (value == k_mpu401_command_reset)
    {
        if (m_intelligent)
            PowerUp();
        else
        {
            m_state.uart = false;
            m_state.head = 0;
            m_state.count = 0;
        }

        Acknowledge();
    }
    else if (!m_state.uart)
    {
        m_state.uart = value == k_mpu401_command_uart;
        Acknowledge();
    }
}

void MPU401::Command(u8 value, bool conductor)
{
    if (value == k_mpu401_command_reset)
    {
        NotesOff();
        PowerUp();

        if (!conductor)
            Acknowledge();

        return;
    }

    if (!conductor)
        Acknowledge();

    if (value <= 0x2F)
        TimingCommand(value);
    else if (value == k_mpu401_command_uart)
    {
        NotesOff();
        m_state.uart = true;
        m_state.playing = false;
        m_state.recording = MPU401_RECORD_OFF;
        m_state.running = false;
        m_state.requests = 0;
        m_state.transfer = MPU401_TRANSFER_NONE;
        m_state.send = k_mpu401_send_none;
        m_state.parameter_for = 0;
    }
    else if (value <= 0x3F)
        m_state.one_way |= (u16)(1 << (value - 0x30));
    else if (value <= 0x7F)
    {
        MPU401_Table& table = m_state.tables[(value - 0x40) >> 4];
        table.channel = value & 0x0F;
        table.on = true;
        table.held = 0;
        memset(table.keys, 0, sizeof(table.keys));
    }
    else if (value <= 0x82)
        m_state.sync = value - 0x80;
    else if (value <= 0x85)
        m_state.metronome = value - 0x83;
    else if (value <= 0x97)
    {
        u16 bit = (u16)(1 << ((value - 0x86) >> 1));

        if ((value & 0x01) != 0)
            m_state.switches |= bit;
        else
            m_state.switches &= ~bit;
    }
    else if (value <= 0x9F)
        m_state.tables[(value - 0x98) >> 1].on = (value & 0x01) != 0;
    else if (value <= 0xAF)
    {
        if (conductor)
            return;

        if (value <= 0xA7)
            Queue(m_state.tracks[value & 0x07].counter);
        else if (value == 0xAB)
        {
            Queue(m_state.record_count);
            m_state.record_count = 0;
        }
        else if (value == 0xAC)
            Queue(k_mpu401_version);
        else if (value == 0xAD)
            Queue(k_mpu401_revision);
        else if (value == 0xAF)
            Queue(m_state.tempo);
    }
    else if (value == 0xB1)
    {
        m_state.relative = 0x40;
        UpdateRate();
    }
    else if ((value == 0xB8) || (value == 0xB9))
    {
        if (value == 0xB9)
            NotesOff();

        for (int i = 0; i < MPU401_TRACKS; i++)
        {
            MPU401_Track& track = m_state.tracks[i];
            u8 status = track.status;
            memset(&track, 0, sizeof(track));

            if (value == 0xB9)
                track.status = status;
        }

        if ((m_state.transfer == MPU401_TRANSFER_TRACK) || (value == 0xB8))
            m_state.transfer = MPU401_TRANSFER_NONE;

        m_state.requests &= ~0x00FF;

        if (value == 0xB8)
        {
            memset(&m_state.conductor, 0, sizeof(m_state.conductor));
            m_state.conductor_send = 0;
            m_state.conductor_on = (m_state.switches & k_mpu401_switch_conductor) != 0;
            m_state.active = m_state.track_select;
            m_state.all_end = false;
            m_state.saved_cth_quarters = 0;
            m_state.saved_midi_clocks = 0;
            m_state.saved_measure_count = 0;
            m_state.requests &= ~((1 << MPU401_REQUEST_CONDUCTOR) | (1 << MPU401_REQUEST_ALL_END));
        }
    }
    else if (value == 0xBA)
        m_state.record_count = 0;
    else if ((value >= 0xC2) && (value <= 0xC8))
    {
        m_state.timebase = (u8)(48 + (value - 0xC2) * 24);
        UpdateRate();
    }
    else if ((value >= 0xD0) && ((value <= 0xD7) || (value == 0xDF)))
    {
        if (conductor)
            m_state.conductor_send = value;
        else
            OpenSend(value);
    }
    else if ((value & 0xF0) == 0xE0)
    {
        if (!conductor)
            m_state.parameter_for = value;
    }
}

void MPU401::TimingCommand(u8 value)
{
    int record = (value >> 4) & 0x03;
    int play = (value >> 2) & 0x03;
    int midi = value & 0x03;

    if (play == 3)
        return;

    if (midi >= 2)
        StartTiming(midi == 3);

    if (play == 2)
        StartPlay();
    else if (play == 1)
        StopPlay();

    if (record == 2)
        m_state.recording = m_state.running ? MPU401_RECORD_ON : MPU401_RECORD_STANDBY;
    else if ((record == 1) && (m_state.recording != MPU401_RECORD_OFF))
    {
        m_state.recording = MPU401_RECORD_OFF;
        Raise(MPU401_REQUEST_DATA_END);
    }

    if (midi == 1)
        StopTiming();
}

void MPU401::Parameter(u8 command, u8 value)
{
    switch (command)
    {
        case 0xE0:
            m_state.tempo = value;
            UpdateRate();
            break;
        case 0xE1:
            m_state.relative = value;
            UpdateRate();
            break;
        case 0xE2:
            m_state.graduation = value;
            break;
        case 0xE4:
            m_state.metro_clocks = value;
            break;
        case 0xE6:
            m_state.metro_measure = value;
            break;
        case 0xE7:
            m_state.cth_rate = value;
            m_state.cth_quarters = 0;
            break;
        case 0xEC:
            m_state.track_select = value;
            break;
        case 0xED:
            m_state.send_play_counter = value;
            break;
        case 0xEE:
            m_state.channels = (u16)((m_state.channels & 0xFF00) | value);
            break;
        case 0xEF:
            m_state.channels = (u16)((m_state.channels & 0x00FF) | (value << 8));
            break;
        default:
            break;
    }
}

void MPU401::WriteData(u8 value)
{
    if (m_state.parameter_for != 0)
    {
        u8 command = m_state.parameter_for;
        m_state.parameter_for = 0;
        Parameter(command, value);
    }
    else if (m_state.send != k_mpu401_send_none)
        WriteSend(value);
    else if (m_state.transfer == MPU401_TRANSFER_TRACK)
        WriteTrack(value);
    else if (m_state.transfer == MPU401_TRANSFER_CONDUCTOR)
        WriteConductor(value);
}

void MPU401::OpenSend(u8 command)
{
    m_state.send = command == 0xDF ? k_mpu401_send_system : k_mpu401_send_voice;
    m_state.send_track = command & 0x07;
    m_state.send_count = 0;
    m_state.send_length = 0;
}

void MPU401::WriteSend(u8 value)
{
    if (m_state.send == k_mpu401_send_voice)
    {
        if (m_state.send_count == 0)
        {
            u8 status = value >= 0x80 ? value : m_state.tracks[m_state.send_track].status;

            if ((status < 0x80) || (status >= 0xF0))
            {
                m_state.send = k_mpu401_send_none;
                return;
            }

            m_state.send_message[0] = status;
            m_state.send_length = k_mpu401_voice_lengths[(status >> 4) & 0x07];
            m_state.send_count = 1;

            if (value >= 0x80)
                return;
        }

        m_state.send_message[m_state.send_count++] = value & 0x7F;

        if (m_state.send_count >= m_state.send_length)
        {
            m_state.tracks[m_state.send_track].status = m_state.send_message[0];
            Output(m_state.send_track, m_state.send_message, m_state.send_length);
            m_state.send = k_mpu401_send_none;
        }

        return;
    }

    if (m_state.send_count == 0)
    {
        switch (value)
        {
            case 0xF0:
                m_state.send_length = 0;
                break;
            case 0xF2:
                m_state.send_length = 3;
                break;
            case 0xF3:
                m_state.send_length = 2;
                break;
            case 0xF6:
                OutputByte(value);
                m_state.send = k_mpu401_send_none;
                return;
            default:
                m_state.send = k_mpu401_send_none;
                return;
        }

        OutputByte(value);
        m_state.send_count = 1;
        return;
    }

    if (m_state.send_length == 0)
    {
        if ((value < 0x80) || (value >= 0xF8))
        {
            OutputByte(value);
            return;
        }

        OutputByte(0xF7);
        m_state.send = k_mpu401_send_none;
        return;
    }

    OutputByte(value);
    m_state.send_count++;

    if (m_state.send_count >= m_state.send_length)
        m_state.send = k_mpu401_send_none;
}

void MPU401::WriteTrack(u8 value)
{
    int index = m_state.transfer_track;
    MPU401_Track& track = m_state.tracks[index];
    bool done = false;

    if (m_state.transfer_stage == 0)
    {
        if (value >= 0xF0)
        {
            track.counter = k_mpu401_max_timing;
            track.kind = MPU401_KIND_NONE;
            done = true;
        }
        else
        {
            track.counter = value;
            m_state.transfer_stage = 1;
        }
    }
    else if (m_state.transfer_stage == 1)
    {
        if (value >= 0xF0)
        {
            track.kind = MPU401_KIND_MARK;
            track.message[0] = value;
            track.length = 1;
            done = true;
        }
        else if (value >= 0x80)
        {
            track.kind = MPU401_KIND_MIDI;
            track.message[0] = value;
            track.length = k_mpu401_voice_lengths[(value >> 4) & 0x07];
            track.status = value;
            m_state.transfer_stage = 2;
        }
        else if (track.status == 0)
        {
            track.kind = MPU401_KIND_NONE;
            done = true;
        }
        else
        {
            track.kind = MPU401_KIND_MIDI;
            track.message[0] = track.status;
            track.message[1] = value;
            track.length = k_mpu401_voice_lengths[(track.status >> 4) & 0x07];
            m_state.transfer_stage = 3;
            done = track.length <= 2;
        }
    }
    else
    {
        track.message[m_state.transfer_stage - 1] = value & 0x7F;
        m_state.transfer_stage++;
        done = (m_state.transfer_stage - 1) >= track.length;
    }

    if (!done)
        return;

    m_state.transfer = MPU401_TRANSFER_NONE;
    track.waiting = false;

    if ((track.counter == 0) && m_state.playing && ((m_state.active & (1 << index)) != 0))
        PlayTrack(index);
}

void MPU401::WriteConductor(u8 value)
{
    MPU401_Track& conductor = m_state.conductor;
    bool done = false;

    if (m_state.transfer_stage == 0)
    {
        if (value >= 0xF0)
        {
            conductor.counter = k_mpu401_max_timing;
            conductor.kind = MPU401_KIND_NONE;
            done = true;
        }
        else
        {
            conductor.counter = value;
            m_state.transfer_stage = 1;
        }
    }
    else if (m_state.transfer_stage == 1)
    {
        conductor.message[0] = value;
        conductor.kind = value >= 0xF8 ? MPU401_KIND_MARK : MPU401_KIND_COMMAND;
        done = (value & 0xF0) != 0xE0;
        m_state.transfer_stage = 2;
    }
    else
    {
        conductor.message[1] = value;
        done = true;
    }

    if (!done)
        return;

    m_state.transfer = MPU401_TRANSFER_NONE;
    conductor.waiting = false;

    if ((conductor.counter == 0) && m_state.playing && m_state.conductor_on)
        PlayConductor();
}

// Cleared tracks ask for data on the first clock, not inside the read of the ACK: a host that sets up its handler
// after the ACK would miss a request that came with it
void MPU401::StartPlay()
{
    m_state.playing = true;

    for (int i = 0; i < MPU401_TRACKS; i++)
    {
        MPU401_Track& track = m_state.tracks[i];

        if ((m_state.active & (1 << i)) == 0)
            continue;

        if (track.waiting && ((m_state.transfer != MPU401_TRANSFER_TRACK) || (m_state.transfer_track != i)))
            Raise(i);
    }

    if (m_state.conductor_on && m_state.conductor.waiting && (m_state.transfer != MPU401_TRANSFER_CONDUCTOR))
        Raise(MPU401_REQUEST_CONDUCTOR);
}

void MPU401::StopPlay()
{
    m_state.playing = false;
    m_state.requests &= ~(0x00FF | (1 << MPU401_REQUEST_CONDUCTOR) | (1 << MPU401_REQUEST_ALL_END));
    NotesOff();
}

void MPU401::StartTiming(bool restore)
{
    if ((m_state.one_way & k_mpu401_one_way_no_real_time) == 0)
        OutputByte(restore ? 0xFB : 0xFA);

    m_state.running = true;

    if (restore)
    {
        m_state.cth_quarters = m_state.saved_cth_quarters;
        m_state.midi_clocks = m_state.saved_midi_clocks;
        m_state.measure_count = m_state.saved_measure_count;
    }
    else
    {
        m_state.cth_quarters = 0;
        m_state.midi_clocks = 0;
        m_state.measure_count = 0;
    }

    if (m_state.recording == MPU401_RECORD_STANDBY)
        m_state.recording = MPU401_RECORD_ON;
}

void MPU401::StopTiming()
{
    if ((m_state.one_way & k_mpu401_one_way_no_real_time) == 0)
        OutputByte(0xFC);

    m_state.running = false;
    m_state.saved_cth_quarters = m_state.cth_quarters;
    m_state.saved_midi_clocks = m_state.midi_clocks;
    m_state.saved_measure_count = m_state.measure_count;
}

void MPU401::Tick()
{
    if (m_state.playing)
    {
        for (int i = 0; i < MPU401_TRACKS; i++)
        {
            if ((m_state.active & (1 << i)) != 0)
                TickTrack(i);
        }

        if (m_state.conductor_on)
            TickConductor();
    }

    if ((m_state.switches & k_mpu401_switch_clock_to_host) != 0)
    {
        u16 rate = m_state.cth_rate != 0 ? m_state.cth_rate : 256;
        m_state.cth_quarters += 4;

        if (m_state.cth_quarters >= rate)
        {
            m_state.cth_quarters -= rate;
            Raise(MPU401_REQUEST_CLOCK);
        }
    }

    if (m_state.running)
    {
        if (++m_state.midi_clocks >= m_state.timebase / 24)
        {
            m_state.midi_clocks = 0;

            if ((m_state.one_way & k_mpu401_one_way_no_real_time) == 0)
                OutputByte(0xF8);
        }

        if (++m_state.measure_count >= GetMeasureTicks())
        {
            m_state.measure_count = 0;

            if ((m_state.recording == MPU401_RECORD_ON) && ((m_state.switches & k_mpu401_switch_measure_end) != 0))
                Raise(MPU401_REQUEST_MEASURE);
        }
    }

    if (m_state.recording == MPU401_RECORD_ON)
    {
        if (++m_state.record_count >= k_mpu401_max_timing)
        {
            m_state.record_count = 0;
            Raise(MPU401_REQUEST_OVERFLOW);
        }
    }

    Deliver();
}

void MPU401::TickTrack(int track)
{
    MPU401_Track& data = m_state.tracks[track];

    if (data.waiting)
        return;

    if ((data.counter == 0) || (--data.counter == 0))
        PlayTrack(track);
}

void MPU401::TickConductor()
{
    MPU401_Track& conductor = m_state.conductor;

    if (conductor.waiting)
        return;

    if ((conductor.counter == 0) || (--conductor.counter == 0))
        PlayConductor();
}

void MPU401::PlayTrack(int track)
{
    MPU401_Track& data = m_state.tracks[track];

    if (data.kind == MPU401_KIND_MIDI)
        Output(track, data.message, data.length);
    else if (data.kind == MPU401_KIND_MARK)
    {
        if (data.message[0] == k_mpu401_end)
        {
            data.kind = MPU401_KIND_NONE;
            m_state.active &= (u8)~(1 << track);
            CheckAllEnd();
            return;
        }

        if (data.message[0] == k_mpu401_measure_end)
            m_state.measure_count = 0;
    }

    data.kind = MPU401_KIND_NONE;
    data.length = 0;
    data.waiting = true;
    Raise(track);
}

void MPU401::PlayConductor()
{
    MPU401_Track& conductor = m_state.conductor;
    u8 kind = conductor.kind;
    u8 command = conductor.message[0];
    u8 parameter = conductor.message[1];

    conductor.kind = MPU401_KIND_NONE;

    if (kind == MPU401_KIND_MARK)
    {
        if (command == k_mpu401_end)
        {
            m_state.conductor_on = false;
            CheckAllEnd();
            return;
        }

        if (command == k_mpu401_measure_end)
            m_state.measure_count = 0;
    }

    conductor.waiting = true;
    Raise(MPU401_REQUEST_CONDUCTOR);

    if (kind != MPU401_KIND_COMMAND)
        return;

    if ((command & 0xF0) == 0xE0)
        Parameter(command, parameter);
    else
        Command(command, true);
}

void MPU401::CheckAllEnd()
{
    if ((m_state.active != 0) || m_state.conductor_on || m_state.all_end)
        return;

    m_state.all_end = true;
    Raise(MPU401_REQUEST_ALL_END);
}

// Four tables supervise notes per key across tracks: a key another track holds retriggers, and its NOTE OFF waits
// for the last track to let go (MPU p24)
void MPU401::Output(int track, const u8* message, int length)
{
    u8 status = message[0];
    u8 type = status & 0xF0;

    if ((type == 0x80) || (type == 0x90))
    {
        u8 channel = status & 0x0F;

        for (int i = 0; i < MPU401_TABLES; i++)
        {
            MPU401_Table& table = m_state.tables[i];

            if (!table.on || (table.channel != channel))
                continue;

            u8 key = message[1] & 0x7F;
            u8 bit = (u8)(1 << track);
            u8 held = table.keys[key];

            if ((type == 0x90) && (message[2] != 0))
            {
                if ((held & ~bit) != 0)
                {
                    OutputByte(0x80 | channel);
                    OutputByte(key);
                    OutputByte(0x00);
                }

                if (held == 0)
                    table.held++;

                table.keys[key] = held | bit;
                break;
            }

            table.keys[key] = held & ~bit;

            if (table.keys[key] != 0)
                return;

            if (held == 0)
                break;

            table.held--;

            for (int j = 0; j < length; j++)
                OutputByte(message[j]);

            if ((table.held == 0) && ((m_state.one_way & k_mpu401_one_way_all_notes_off) == 0))
            {
                OutputByte(0xB0 | channel);
                OutputByte(0x7B);
                OutputByte(0x00);
            }

            return;
        }
    }

    for (int i = 0; i < length; i++)
        OutputByte(message[i]);
}

void MPU401::OutputByte(u8 value)
{
    if (IsValidPointer(m_midi_out))
        m_midi_out->Write(value);
}

void MPU401::NotesOff()
{
    for (int i = 0; i < MPU401_TABLES; i++)
    {
        MPU401_Table& table = m_state.tables[i];

        if (!table.on || (table.held == 0))
            continue;

        for (int key = 0; key < 128; key++)
        {
            if (table.keys[key] == 0)
                continue;

            OutputByte(0x80 | table.channel);
            OutputByte((u8)key);
            OutputByte(0x00);
        }

        if ((m_state.one_way & k_mpu401_one_way_all_notes_off) == 0)
        {
            OutputByte(0xB0 | table.channel);
            OutputByte(0x7B);
            OutputByte(0x00);
        }
    }

    ClearTables();
}

void MPU401::ClearTables()
{
    for (int i = 0; i < MPU401_TABLES; i++)
    {
        m_state.tables[i].held = 0;
        memset(m_state.tables[i].keys, 0, sizeof(m_state.tables[i].keys));
    }
}

void MPU401::Raise(int request)
{
    m_state.requests |= (u16)(1 << request);
}

// One message at a time: a request only enters an empty queue with no transfer open (MPU p10, p55)
bool MPU401::Deliver()
{
    if ((m_state.count != 0) || (m_state.transfer != MPU401_TRANSFER_NONE) || !m_intelligent || m_state.uart)
        return false;

    u16 pending = m_state.requests;

    if (!m_state.playing)
        pending &= ~(0x00FF | (1 << MPU401_REQUEST_CONDUCTOR));

    if (pending == 0)
        return false;

    int request = 0;

    while ((pending & (1 << request)) == 0)
        request++;

    m_state.requests &= (u16)~(1 << request);

    if (request < MPU401_TRACKS)
    {
        Queue((u8)(0xF0 + request));
        m_state.transfer = MPU401_TRANSFER_TRACK;
        m_state.transfer_track = (u8)request;
        m_state.transfer_stage = 0;
        return true;
    }

    switch (request)
    {
        case MPU401_REQUEST_OVERFLOW:
            Queue(k_mpu401_overflow);
            break;
        case MPU401_REQUEST_CONDUCTOR:
            Queue(0xF9);
            m_state.transfer = MPU401_TRANSFER_CONDUCTOR;
            m_state.transfer_stage = 0;

            if (m_state.conductor_send != 0)
            {
                OpenSend(m_state.conductor_send);
                m_state.conductor_send = 0;
            }
            break;
        case MPU401_REQUEST_MEASURE:
            Queue(m_state.record_count);
            Queue(k_mpu401_measure_end);
            m_state.record_count = 0;
            break;
        case MPU401_REQUEST_DATA_END:
            Queue(m_state.record_count);
            Queue(k_mpu401_end);
            m_state.record_count = 0;
            break;
        case MPU401_REQUEST_ALL_END:
            Queue(k_mpu401_end);
            break;
        default:
            Queue(k_mpu401_clock);
            break;
    }

    return true;
}

void MPU401::Queue(u8 value)
{
    if (m_state.count >= MPU401_QUEUE_SIZE)
        return;

    m_state.queue[(m_state.head + m_state.count) % MPU401_QUEUE_SIZE] = value;
    m_state.count++;

    if (unlikely(IsValidPointer(m_trace_logger) && m_trace_logger->IsEventEnabled(TRACE_MIDI, TRACE_MIDI_MESSAGE)))
        TraceMessage(value);
}

void MPU401::Acknowledge()
{
    Queue(k_mpu401_ack);
}

// The tempo table bounds the product of tempo and relative tempo per timebase (MPU p35, p43)
void MPU401::UpdateRate()
{
    int index = CLAMP(m_state.timebase / 24 - 2, 0, 6);
    u32 product = (u32)m_state.tempo * m_state.relative;
    product = CLAMP(product, (u32)k_mpu401_tempo_min[index] * 64, (u32)k_mpu401_tempo_max[index] * 64);
    m_rate = (u64)product * m_state.timebase;
}

u32 MPU401::GetEffectiveTempo() const
{
    return m_state.timebase != 0 ? (u32)(m_rate / m_state.timebase / 64) : 0;
}

u32 MPU401::GetMeasureTicks() const
{
    u32 clocks = m_state.metro_clocks != 0 ? m_state.metro_clocks : 256;
    u32 beats = m_state.metro_measure != 0 ? m_state.metro_measure : 256;
    return (m_state.timebase / 24) * clocks * beats;
}

// Only what the host can see needs an event: requests, clock to host and the record counter. MIDI out plays from
// the ticks whenever the audio catches up
u64 MPU401::GetClocksToEvent() const
{
    if (!IsClockRunning() || (m_rate == 0))
        return GDOX_NO_EVENT;

    u32 ticks = 0xFFFFFFFF;

    if (m_state.playing)
    {
        for (int i = 0; i < MPU401_TRACKS; i++)
        {
            const MPU401_Track& track = m_state.tracks[i];

            if (((m_state.active & (1 << i)) != 0) && !track.waiting)
                ticks = MIN(ticks, (u32)MAX(track.counter, (u8)1));
        }

        if (m_state.conductor_on && !m_state.conductor.waiting)
            ticks = MIN(ticks, (u32)MAX(m_state.conductor.counter, (u8)1));
    }

    if ((m_state.switches & k_mpu401_switch_clock_to_host) != 0)
    {
        u32 rate = m_state.cth_rate != 0 ? m_state.cth_rate : 256;
        ticks = MIN(ticks, (rate - MIN((u32)m_state.cth_quarters, rate - 1) + 3) / 4);
    }

    if (m_state.recording == MPU401_RECORD_ON)
    {
        ticks = MIN(ticks, (u32)(k_mpu401_max_timing - MIN(m_state.record_count, (u8)(k_mpu401_max_timing - 1))));

        if (m_state.running && ((m_state.switches & k_mpu401_switch_measure_end) != 0))
        {
            u32 measure = GetMeasureTicks();
            ticks = MIN(ticks, measure - MIN(m_state.measure_count, measure - 1));
        }
    }

    if (ticks == 0xFFFFFFFF)
        return GDOX_NO_EVENT;

    u64 target = (u64)ticks * GetPeriod();
    u64 phase = MIN(m_state.phase, target);
    return (target - phase + m_rate - 1) / m_rate;
}

void MPU401::TraceMessage(u8 value)
{
    GDOX_Trace_Entry entry = {};
    entry.type = TRACE_MIDI;
    entry.event = TRACE_MIDI_MESSAGE;
    entry.midi.value = value;
    entry.midi.source = TRACE_MIDI_SOURCE_MPU;
    m_trace_logger->TraceLog(entry);
}

void MPU401::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
}

void MPU401::LoadState(std::istream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
    SanitizeState();
}

void MPU401::Serialize(StateSerializer& serializer)
{
    G_SERIALIZE(serializer, m_state.uart);
    G_SERIALIZE_ARRAY(serializer, m_state.queue, MPU401_QUEUE_SIZE);
    G_SERIALIZE(serializer, m_state.head);
    G_SERIALIZE(serializer, m_state.count);
    G_SERIALIZE(serializer, m_state.last_data);
    G_SERIALIZE(serializer, m_state.playing);
    G_SERIALIZE(serializer, m_state.recording);
    G_SERIALIZE(serializer, m_state.running);
    G_SERIALIZE(serializer, m_state.all_end);
    G_SERIALIZE(serializer, m_state.one_way);
    G_SERIALIZE(serializer, m_state.switches);
    G_SERIALIZE(serializer, m_state.sync);
    G_SERIALIZE(serializer, m_state.metronome);
    G_SERIALIZE(serializer, m_state.tempo);
    G_SERIALIZE(serializer, m_state.relative);
    G_SERIALIZE(serializer, m_state.graduation);
    G_SERIALIZE(serializer, m_state.timebase);
    G_SERIALIZE(serializer, m_state.phase);
    G_SERIALIZE(serializer, m_state.cth_rate);
    G_SERIALIZE(serializer, m_state.cth_quarters);
    G_SERIALIZE(serializer, m_state.midi_clocks);
    G_SERIALIZE(serializer, m_state.metro_clocks);
    G_SERIALIZE(serializer, m_state.metro_measure);
    G_SERIALIZE(serializer, m_state.measure_count);
    G_SERIALIZE(serializer, m_state.record_count);
    G_SERIALIZE(serializer, m_state.saved_cth_quarters);
    G_SERIALIZE(serializer, m_state.saved_midi_clocks);
    G_SERIALIZE(serializer, m_state.saved_measure_count);

    for (int i = 0; i <= MPU401_TRACKS; i++)
    {
        MPU401_Track& track = i < MPU401_TRACKS ? m_state.tracks[i] : m_state.conductor;
        G_SERIALIZE(serializer, track.counter);
        G_SERIALIZE_ARRAY(serializer, track.message, 3);
        G_SERIALIZE(serializer, track.length);
        G_SERIALIZE(serializer, track.kind);
        G_SERIALIZE(serializer, track.status);
        G_SERIALIZE(serializer, track.waiting);
    }

    G_SERIALIZE(serializer, m_state.conductor_on);
    G_SERIALIZE(serializer, m_state.conductor_send);
    G_SERIALIZE(serializer, m_state.track_select);
    G_SERIALIZE(serializer, m_state.active);
    G_SERIALIZE(serializer, m_state.requests);
    G_SERIALIZE(serializer, m_state.send_play_counter);
    G_SERIALIZE(serializer, m_state.channels);
    G_SERIALIZE(serializer, m_state.parameter_for);
    G_SERIALIZE(serializer, m_state.transfer);
    G_SERIALIZE(serializer, m_state.transfer_track);
    G_SERIALIZE(serializer, m_state.transfer_stage);
    G_SERIALIZE(serializer, m_state.send);
    G_SERIALIZE(serializer, m_state.send_track);
    G_SERIALIZE(serializer, m_state.send_count);
    G_SERIALIZE(serializer, m_state.send_length);
    G_SERIALIZE_ARRAY(serializer, m_state.send_message, 3);

    for (int i = 0; i < MPU401_TABLES; i++)
    {
        G_SERIALIZE(serializer, m_state.tables[i].channel);
        G_SERIALIZE(serializer, m_state.tables[i].on);
        G_SERIALIZE(serializer, m_state.tables[i].held);
        G_SERIALIZE_ARRAY(serializer, m_state.tables[i].keys, 128);
    }
}

void MPU401::SanitizeState()
{
    m_state.head %= MPU401_QUEUE_SIZE;
    m_state.count = MIN(m_state.count, (u8)MPU401_QUEUE_SIZE);
    m_state.recording = MIN(m_state.recording, (u8)MPU401_RECORD_ON);
    m_state.sync = MIN(m_state.sync, (u8)2);
    m_state.metronome = MIN(m_state.metronome, (u8)2);

    if ((m_state.timebase < 48) || (m_state.timebase > 192) || ((m_state.timebase % 24) != 0))
        m_state.timebase = 120;

    UpdateRate();
    m_state.phase %= GetPeriod();
    m_state.cth_quarters = MIN(m_state.cth_quarters, (u16)255);
    m_state.midi_clocks = MIN(m_state.midi_clocks, (u8)(m_state.timebase / 24 - 1));
    m_state.measure_count %= GetMeasureTicks();
    m_state.record_count = MIN(m_state.record_count, (u8)(k_mpu401_max_timing - 1));
    m_state.saved_cth_quarters = MIN(m_state.saved_cth_quarters, (u16)255);
    m_state.saved_midi_clocks = MIN(m_state.saved_midi_clocks, (u8)(m_state.timebase / 24 - 1));
    m_state.saved_measure_count %= GetMeasureTicks();

    for (int i = 0; i <= MPU401_TRACKS; i++)
    {
        MPU401_Track& track = i < MPU401_TRACKS ? m_state.tracks[i] : m_state.conductor;
        track.counter = MIN(track.counter, k_mpu401_max_timing);
        track.length = MIN(track.length, (u8)3);
        track.kind = MIN(track.kind, (u8)MPU401_KIND_COMMAND);
    }

    m_state.transfer = MIN(m_state.transfer, (u8)MPU401_TRANSFER_CONDUCTOR);
    m_state.transfer_track &= MPU401_TRACKS - 1;
    m_state.transfer_stage = MIN(m_state.transfer_stage, (u8)4);
    m_state.send = MIN(m_state.send, k_mpu401_send_system);
    m_state.send_track &= MPU401_TRACKS - 1;
    m_state.send_length = MIN(m_state.send_length, (u8)3);
    m_state.send_count = MIN(m_state.send_count, m_state.send_length == 0 ? (u8)1 : m_state.send_length);

    if ((m_state.parameter_for != 0) && ((m_state.parameter_for & 0xF0) != 0xE0))
        m_state.parameter_for = 0;

    for (int i = 0; i < MPU401_TABLES; i++)
    {
        MPU401_Table& table = m_state.tables[i];
        table.channel &= 0x0F;
        table.held = 0;

        for (int key = 0; key < 128; key++)
        {
            if (table.keys[key] != 0)
                table.held++;
        }
    }
}
