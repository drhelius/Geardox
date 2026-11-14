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

class MidiOut;
class StateSerializer;
class TraceLogger;

class MPU401
{
public:
    struct MPU401_State
    {
        bool uart;
        u8 queue[MPU401_QUEUE_SIZE];
        u8 head;
        u8 count;
        u8 last_data;
    };

public:
    MPU401();
    ~MPU401();
    void Init(MidiOut* midi_out);
    void SetTraceLogger(TraceLogger* trace_logger);
    void Reset();
    u8 Read(u16 port);
    u8 Peek(u16 port) const;
    void Write(u16 port, u8 value);
    bool IsIRQAsserted() const;
    MPU401_State* GetState();
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream);

private:
    void Acknowledge();
    void Serialize(StateSerializer& serializer);
    void SanitizeState();

private:
    MPU401_State m_state;
    MidiOut* m_midi_out;
    TraceLogger* m_trace_logger;
};

static const u16 k_mpu401_port = 0x0330;
static const int k_mpu401_irq = 2;

INLINE bool MPU401::IsIRQAsserted() const
{
    return m_state.count != 0;
}

INLINE MPU401::MPU401_State* MPU401::GetState()
{
    return &m_state;
}

#endif /* MPU401_H */
