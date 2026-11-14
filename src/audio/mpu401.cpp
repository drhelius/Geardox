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

MPU401::MPU401()
{
    InitPointer(m_midi_out);
    InitPointer(m_trace_logger);
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

void MPU401::Reset()
{
    memset(&m_state, 0, sizeof(m_state));
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

// Roland has no ACK on leaving UART mode. Creative's detection reads one back from FFh, and that's what software does
void MPU401::Write(u16 port, u8 value)
{
    if ((port & 0x01) == 0)
    {
        if (m_state.uart && unlikely(IsValidPointer(m_trace_logger) && m_trace_logger->IsEnabled(TRACE_MIDI)))
        {
            GDOX_Trace_Entry entry = {};
            entry.type = TRACE_MIDI;
            entry.event = TRACE_MIDI_DATA;
            entry.midi.value = value;
            entry.midi.source = TRACE_MIDI_SOURCE_MPU;
            m_trace_logger->TraceLog(entry);
        }

        if (m_state.uart && IsValidPointer(m_midi_out))
            m_midi_out->Write(value);

        return;
    }

    if (value == k_mpu401_command_reset)
    {
        m_state.uart = false;
        m_state.head = 0;
        m_state.count = 0;
        Acknowledge();
    }
    else if (!m_state.uart)
    {
        m_state.uart = value == k_mpu401_command_uart;
        Acknowledge();
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
}

void MPU401::Acknowledge()
{
    if (m_state.count >= MPU401_QUEUE_SIZE)
        return;

    m_state.queue[(m_state.head + m_state.count) % MPU401_QUEUE_SIZE] = k_mpu401_ack;
    m_state.count++;
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
}

void MPU401::SanitizeState()
{
    m_state.head %= MPU401_QUEUE_SIZE;
    m_state.count = MIN(m_state.count, (u8)MPU401_QUEUE_SIZE);
}
