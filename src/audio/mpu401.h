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

#ifndef MPU401_H
#define MPU401_H

#include <iostream>
#include "../common/common.h"

#define MPU401_QUEUE_SIZE 16
#define MPU401_TRACKS 8
#define MPU401_TABLES 4

class MidiOut;
class StateSerializer;
class TraceLogger;

class MPU401
{
public:
    enum MPU401_Kind
    {
        MPU401_KIND_NONE = 0,
        MPU401_KIND_MIDI,
        MPU401_KIND_MARK,
        MPU401_KIND_COMMAND
    };

    enum MPU401_Record
    {
        MPU401_RECORD_OFF = 0,
        MPU401_RECORD_STANDBY,
        MPU401_RECORD_ON
    };

    enum MPU401_Transfer
    {
        MPU401_TRANSFER_NONE = 0,
        MPU401_TRANSFER_TRACK,
        MPU401_TRANSFER_CONDUCTOR
    };

    enum MPU401_Request
    {
        MPU401_REQUEST_OVERFLOW = 8,
        MPU401_REQUEST_CONDUCTOR = 9,
        MPU401_REQUEST_MEASURE = 10,
        MPU401_REQUEST_DATA_END = 11,
        MPU401_REQUEST_ALL_END = 12,
        MPU401_REQUEST_CLOCK = 13
    };

    struct MPU401_Track
    {
        u8 counter;
        u8 message[3];
        u8 length;
        u8 kind;
        u8 status;
        bool waiting;
    };

    struct MPU401_Table
    {
        u8 channel;
        bool on;
        u8 held;
        u8 keys[128];
    };

    struct MPU401_State
    {
        bool uart;
        u8 queue[MPU401_QUEUE_SIZE];
        u8 head;
        u8 count;
        u8 last_data;
        bool playing;
        u8 recording;
        bool running;
        bool all_end;
        u16 one_way;
        u16 switches;
        u8 sync;
        u8 metronome;
        u8 tempo;
        u8 relative;
        u8 graduation;
        u8 timebase;
        u64 phase;
        u8 cth_rate;
        u16 cth_quarters;
        u8 midi_clocks;
        u8 metro_clocks;
        u8 metro_measure;
        u32 measure_count;
        u8 record_count;
        u16 saved_cth_quarters;
        u8 saved_midi_clocks;
        u32 saved_measure_count;
        MPU401_Track tracks[MPU401_TRACKS];
        MPU401_Track conductor;
        bool conductor_on;
        u8 conductor_send;
        u8 track_select;
        u8 active;
        u16 requests;
        u8 send_play_counter;
        u16 channels;
        u8 parameter_for;
        u8 transfer;
        u8 transfer_track;
        u8 transfer_stage;
        u8 send;
        u8 send_track;
        u8 send_count;
        u8 send_length;
        u8 send_message[3];
        MPU401_Table tables[MPU401_TABLES];
    };

public:
    MPU401();
    ~MPU401();
    void Init(MidiOut* midi_out);
    void SetTraceLogger(TraceLogger* trace_logger);
    void SetIntelligent(bool intelligent);
    void Reset();
    u8 Read(u16 port);
    u8 Peek(u16 port) const;
    void Write(u16 port, u8 value);
    bool Clock(u32 clocks);
    bool Deliver();
    bool IsClockRunning() const;
    u64 GetClocksToEvent() const;
    bool IsIRQAsserted() const;
    bool IsIntelligent() const;
    u32 GetEffectiveTempo() const;
    MPU401_State* GetState();
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream);

private:
    void PowerUp();
    void WriteUartOnly(u8 value);
    void Command(u8 value, bool conductor);
    void TimingCommand(u8 value);
    void Parameter(u8 command, u8 value);
    void WriteData(u8 value);
    void WriteSend(u8 value);
    void WriteTrack(u8 value);
    void WriteConductor(u8 value);
    void OpenSend(u8 command);
    void StartPlay();
    void StopPlay();
    void StartTiming(bool restore);
    void StopTiming();
    void Tick();
    void TickTrack(int track);
    void TickConductor();
    void PlayTrack(int track);
    void PlayConductor();
    void CheckAllEnd();
    void Output(int track, const u8* message, int length);
    void OutputByte(u8 value);
    void NotesOff();
    void ClearTables();
    void Raise(int request);
    void Queue(u8 value);
    void Acknowledge();
    void UpdateRate();
    u32 GetMeasureTicks() const;
    u64 GetPeriod() const;
    void TraceMessage(u8 value);
    void Serialize(StateSerializer& serializer);
    void SanitizeState();

private:
    MPU401_State m_state;
    MidiOut* m_midi_out;
    TraceLogger* m_trace_logger;
    bool m_intelligent;
    u64 m_rate;
};

static const u16 k_mpu401_port = 0x0330;
static const int k_mpu401_irq = 2;

#include "mpu401_inline.h"

#endif /* MPU401_H */
