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

#include "io.h"

IO::IO()
{
    InitPointer(m_scheduler);
    InitPointer(m_audio);
    InitPointer(m_pic);
    InitPointer(m_pit);
    InitPointer(m_video);
    InitPointer(m_memory);
    InitPointer(m_fdc);
    InitPointer(m_input);
    memset(m_decoded_ports, 0xFF, sizeof(m_decoded_ports));
}

IO::~IO()
{
}

void IO::Init(Scheduler* scheduler, Audio* audio, PIC* pic, PIT* pit, Video* video, Memory* memory, FDC* fdc,
    Input* input)
{
    m_scheduler = scheduler;
    m_audio = audio;
    m_pic = pic;
    m_pit = pit;
    m_video = video;
    m_memory = memory;
    m_fdc = fdc;
    m_input = input;
    Reset();
}

void IO::Reset()
{
}

u8 IO::Read8(u16 port, GDOX_Bus_Access_Context& context)
{
    return ReadPort(port, context);
}

u8 IO::ReadPort(u16 port, GDOX_Bus_Access_Context& context)
{
    UNUSED(port);
    UNUSED(context);
    Debug("Unknown IO read at %04X", port);
    return 0xFF;
}

// What a read would return, without its side effects, for the debugger
// Returns false for ports the board does not decode
bool IO::Peek(u16 port, u64 clocks, u8& value) const
{
    UNUSED(port);
    UNUSED(clocks);
    value = 0xFF;
    return false;
}

// Machine clock until which reads of the port repeat the last one without side effects, 0 when unknown
// One bit per port, from what the board decodes on this model
void IO::UpdateDecodedPorts()
{
    memset(m_decoded_ports, 0, sizeof(m_decoded_ports));

    for (u32 port = 0; port < 0x10000; port++)
    {
        u8 value = 0;

        if (Peek((u16)port, 0, value))
            m_decoded_ports[port >> 3] |= (u8)(1 << (port & 7));
    }
}

const u8* IO::GetDecodedPorts() const
{
    return m_decoded_ports;
}

u64 IO::GetReadStableUntil(u16 port, int width, u64 clocks) const
{
    UNUSED(port);
    UNUSED(width);
    UNUSED(clocks);
    return 0;
}

void IO::Write8(u16 port, u8 value, GDOX_Bus_Access_Context& context)
{
    UNUSED(port);
    UNUSED(value);
    UNUSED(context);
    Debug("Unknown IO write at %04X, value=%02X", port, value);
}
