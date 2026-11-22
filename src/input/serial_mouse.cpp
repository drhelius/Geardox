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


#include "serial_mouse.h"
#include "input.h"
#include "../common/trace_logger.h"
#include "../common/state_serializer.h"

SerialMouse::SerialMouse()
{
    InitPointer(m_input);
    m_trace_logger = TraceLogger::GetDisabled();
    memset(&m_state, 0, sizeof(m_state));
}

SerialMouse::~SerialMouse()
{
}

void SerialMouse::Init(Input* input)
{
    m_input = input;
    Reset();
}

void SerialMouse::SetTraceLogger(TraceLogger* trace_logger)
{
    m_trace_logger = TraceLogger::Resolve(trace_logger);
}

void SerialMouse::Reset()
{
    memset(&m_state, 0, sizeof(m_state));
    m_state.ident_clocks = GDOX_NO_EVENT;
    m_state.byte_clocks = GDOX_NO_EVENT;
    m_state.poll_clocks = GDOX_NO_EVENT;
}

// RTS going up after a drop is the Microsoft driver's reset, answered with 'M'. While it's down the mouse is silent
void SerialMouse::SetRTS(bool rts, u64 clocks)
{
    if (rts == m_state.rts)
        return;

    m_state.rts = rts;
    m_state.ready = false;

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_INPUT, TRACE_INPUT_MOUSE_RESET)))
    {
        GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_INPUT, TRACE_INPUT_MOUSE_RESET);
        entry->input.device = TRACE_INPUT_DEVICE_SERIAL_MOUSE;
        entry->input.data[0] = rts ? 1 : 0;
    }

    m_state.queue_count = 0;
    m_state.byte_clocks = GDOX_NO_EVENT;
    m_state.poll_clocks = GDOX_NO_EVENT;
    m_state.ident_clocks = rts ? clocks + k_serial_mouse_ident_clocks : GDOX_NO_EVENT;
}

// Returns true with each byte as its last stop bit ends
bool SerialMouse::HandleEvent(u64 clocks, u8& value)
{
    if (m_state.byte_clocks <= clocks)
    {
        value = m_state.queue[0];
        m_state.queue_count--;
        memmove(m_state.queue, m_state.queue + 1, m_state.queue_count);
        m_state.byte_clocks = GDOX_NO_EVENT;
        StartByte(clocks);
        return true;
    }

    if (m_state.ident_clocks <= clocks)
    {
        u8 ident = 'M';

        m_state.ident_clocks = GDOX_NO_EVENT;
        m_state.ready = true;
        m_state.buttons = m_input->GetMouseButtons() & (GDOX_MOUSE_LEFT | GDOX_MOUSE_RIGHT);
        m_input->ClearMouseInput();
        m_state.queue[0] = ident;
        m_state.queue_count = 1;

        if (unlikely(m_trace_logger->IsEventEnabled(TRACE_INPUT, TRACE_INPUT_MOUSE_PACKET)))
            TracePacket(&ident, 1, TRACE_INPUT_FLAG_IDENT);

        StartByte(clocks);
        return false;
    }

    if (m_state.poll_clocks <= clocks)
    {
        m_state.poll_clocks = GDOX_NO_EVENT;
        StartByte(clocks);
    }

    return false;
}

// Reports go out back to back while the mouse moves, a button change at once, the rest of a fast move next time
void SerialMouse::Poll(u64 clocks)
{
    s32 x = 0;
    s32 y = 0;
    u8 buttons = m_input->GetMouseButtons() & (GDOX_MOUSE_LEFT | GDOX_MOUSE_RIGHT);

    m_input->TakeMouseDelta(clocks, 127, x, y);

    if (x != 0 || y != 0 || buttons != m_state.buttons)
    {
        m_state.queue[0] = (u8)(0x40 | ((buttons & GDOX_MOUSE_LEFT) != 0 ? 0x20 : 0x00) |
            ((buttons & GDOX_MOUSE_RIGHT) != 0 ? 0x10 : 0x00) | ((y >> 4) & 0x0C) | ((x >> 6) & 0x03));
        m_state.queue[1] = (u8)(x & 0x3F);
        m_state.queue[2] = (u8)(y & 0x3F);
        m_state.queue_count = 3;
        m_state.buttons = buttons;

        if (unlikely(m_trace_logger->IsEventEnabled(TRACE_INPUT, TRACE_INPUT_MOUSE_PACKET)))
            TracePacket(m_state.queue, 3, 0);
    }
}

// A free line takes the next report at once, an idle mouse looks again a byte later
void SerialMouse::StartByte(u64 clocks)
{
    if (m_state.queue_count == 0 && m_state.ready)
        Poll(clocks);

    if (m_state.queue_count != 0)
        m_state.byte_clocks = clocks + k_serial_mouse_byte_clocks;
    else if (m_state.ready)
        m_state.poll_clocks = clocks + k_serial_mouse_byte_clocks;
}

void SerialMouse::TracePacket(const u8* data, u8 count, u16 flags)
{
    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_INPUT, TRACE_INPUT_MOUSE_PACKET);
    entry->flags |= flags;
    entry->input.device = TRACE_INPUT_DEVICE_SERIAL_MOUSE;
    entry->input.count = (u8)MIN((int)count, 8);
    memcpy(entry->input.data, data, entry->input.count);
}

void SerialMouse::Serialize(StateSerializer& serializer)
{
    G_SERIALIZE_ARRAY(serializer, m_state.queue, SERIAL_MOUSE_QUEUE_SIZE);
    G_SERIALIZE(serializer, m_state.queue_count);
    G_SERIALIZE(serializer, m_state.buttons);
    G_SERIALIZE(serializer, m_state.rts);
    G_SERIALIZE(serializer, m_state.ready);
    G_SERIALIZE(serializer, m_state.ident_clocks);
    G_SERIALIZE(serializer, m_state.byte_clocks);
    G_SERIALIZE(serializer, m_state.poll_clocks);
}

void SerialMouse::SanitizeState()
{
    m_state.queue_count = (u8)MIN((int)m_state.queue_count, SERIAL_MOUSE_QUEUE_SIZE);
    m_state.buttons &= GDOX_MOUSE_LEFT | GDOX_MOUSE_RIGHT;

    if (m_state.queue_count == 0)
        m_state.byte_clocks = GDOX_NO_EVENT;
}
