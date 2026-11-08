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
#include "debug_console.h"
#include "dma.h"
#include "firmware_config.h"
#include "../audio/audio.h"
#include "../drive/fdc.h"
#include "mc146818.h"
#include "../input/i8042.h"
#include "memory.h"
#include "pic.h"
#include "pit.h"
#include "parallel_port.h"
#include "scheduler.h"
#include "serial_ports.h"
#include "system_control.h"
#include "../input/game_port.h"
#include "../video/video.h"

IO::IO()
{
    InitPointer(m_scheduler);
    InitPointer(m_audio);
    InitPointer(m_pic);
    InitPointer(m_pit);
    InitPointer(m_video);
    InitPointer(m_memory);
    InitPointer(m_fdc);
    InitPointer(m_ide);
    InitPointer(m_system_control);
    InitPointer(m_rtc);
    InitPointer(m_firmware_config);
    InitPointer(m_debug_console);
    InitPointer(m_i8042);
    InitPointer(m_dma);
    InitPointer(m_serial_ports);
    InitPointer(m_parallel_port);
    InitPointer(m_game_port);
    memset(m_decoded_ports, 0xFF, sizeof(m_decoded_ports));
}

IO::~IO()
{
}

void IO::Init(Scheduler* scheduler, Audio* audio, PIC* pic, PIT* pit, Video* video, Memory* memory, FDC* fdc,
    IdeController* ide, SystemControl* system_control, MC146818* rtc, FirmwareConfig* firmware_config,
    DebugConsole* debug_console, I8042* i8042, DMA* dma, SerialPorts* serial_ports, ParallelPort* parallel_port,
    GamePort* game_port)
{
    m_scheduler = scheduler;
    m_audio = audio;
    m_pic = pic;
    m_pit = pit;
    m_video = video;
    m_memory = memory;
    m_fdc = fdc;
    m_ide = ide;
    m_system_control = system_control;
    m_rtc = rtc;
    m_firmware_config = firmware_config;
    m_debug_console = debug_console;
    m_i8042 = i8042;
    m_dma = dma;
    m_serial_ports = serial_ports;
    m_parallel_port = parallel_port;
    m_game_port = game_port;
    Reset();
}

void IO::Reset()
{
}

u8 IO::Read8(u16 port, GDOX_Bus_Access_Context& context)
{
    u64 next_event = m_scheduler->GetNextEventClocks();
    bool interrupt = m_pic->IsInterruptPending();
    u8 value = ReadPort(port, context);

    if (m_scheduler->GetNextEventClocks() != next_event || m_pic->IsInterruptPending() != interrupt)
        context.end_batch = true;

    return value;
}

u16 IO::ReadIDEData(u16 port, GDOX_Bus_Access_Context& context)
{
    u64 next_event = m_scheduler->GetNextEventClocks();
    bool interrupt = m_pic->IsInterruptPending();
    u16 value = m_ide->ReadData(port, context.clocks);

    if (m_scheduler->GetNextEventClocks() != next_event || m_pic->IsInterruptPending() != interrupt)
        context.end_batch = true;

    return value;
}

void IO::WriteIDEData(u16 port, u16 value, GDOX_Bus_Access_Context& context)
{
    u64 next_event = m_scheduler->GetNextEventClocks();
    bool interrupt = m_pic->IsInterruptPending();
    m_ide->WriteData(port, value, context.clocks);

    if (m_scheduler->GetNextEventClocks() != next_event || m_pic->IsInterruptPending() != interrupt)
        context.end_batch = true;
}

// Board ports decode all 16 address lines, ISA cards only the low 10, so they repeat every 400h
u8 IO::ReadPort(u16 port, GDOX_Bus_Access_Context& context)
{
    switch (port)
    {
        case 0x0020:
            // PIC master command
        case 0x0021:
            // PIC master data
        case 0x00A0:
            // PIC slave command
        case 0x00A1:
            // PIC slave data
            return m_pic->Read(port);
        case 0x0040:
            // PIT counter 0
        case 0x0041:
            // PIT counter 1
        case 0x0042:
            // PIT counter 2
        case 0x0043:
            // PIT control
            return m_pit->Read(port, context.clocks);
        case 0x0060:
            // Keyboard controller data
        case 0x0064:
            // Keyboard controller status
            return m_i8042->Read(port, context.clocks);
        case 0x0061:
            // Speaker, refresh toggle and NMI sources
        case 0x0080:
            // POST code
        case 0x0092:
            // Fast A20 and reset
            return m_system_control->Read(port, context.clocks);
        case 0x0070:
            // CMOS index, write only
        case 0x0071:
            // CMOS data
            return m_rtc->Read(port, context.clocks);
        case 0x0402:
            // Debug console
            if (m_debug_console->Decodes(port))
                return m_debug_console->Read();
            break;
        case 0x0511:
            // Firmware config data
            if (m_firmware_config->Decodes(port))
                return m_firmware_config->Read(port);
            break;
        default:
            if (m_video->Decodes(port & k_io_isa_mask))
                return m_video->Read(port & k_io_isa_mask, context.clocks);

            if (m_fdc->Decodes(port & k_io_isa_mask))
                return m_fdc->Read(port & k_io_isa_mask, context.clocks);

            if (m_ide->Decodes(port & k_io_isa_mask))
                return m_ide->Read(port & k_io_isa_mask, context.clocks);

            if (m_dma->Decodes(port))
                return m_dma->Read(port);

            if (m_audio->Decodes(port & k_io_isa_mask))
                return m_audio->Read(port & k_io_isa_mask, context.clocks);

            if (m_serial_ports->Decodes(port & k_io_isa_mask))
                return m_serial_ports->Read(port & k_io_isa_mask, context.clocks);

            if (m_game_port->Decodes(port & k_io_isa_mask))
                return m_game_port->Read(context.clocks);

            if (m_parallel_port->Decodes(port & k_io_isa_mask))
                return m_parallel_port->Read(port & k_io_isa_mask);
            break;
    }

    Debug("Unknown IO read at %04X", port);
    return 0xFF;
}

// What a read would return, without its side effects, for the debugger
// Returns false for ports the board does not decode
bool IO::Peek(u16 port, u64 clocks, u8& value) const
{
    value = 0xFF;

    switch (port)
    {
        case 0x0020:
        case 0x0021:
        case 0x00A0:
        case 0x00A1:
            value = m_pic->Peek(port);
            return true;
        case 0x0040:
        case 0x0041:
        case 0x0042:
        case 0x0043:
            value = m_pit->Peek(port, clocks);
            return true;
        case 0x0060:
        case 0x0064:
            value = m_i8042->Peek(port);
            return true;
        case 0x0070:
        case 0x0071:
            value = m_rtc->Peek(port, clocks);
            return true;
        case 0x0402:
            value = m_debug_console->Read();
            return m_debug_console->Decodes(port);
        case 0x0510:
        case 0x0511:
            value = m_firmware_config->Peek(port);
            return m_firmware_config->Decodes(port);
        default:
            if (m_video->Decodes(port & k_io_isa_mask))
            {
                value = m_video->Peek(port & k_io_isa_mask, clocks);
                return true;
            }

            if (m_fdc->Decodes(port & k_io_isa_mask))
            {
                value = m_fdc->Peek(port & k_io_isa_mask);
                return true;
            }

            if (m_ide->Decodes(port & k_io_isa_mask))
            {
                value = m_ide->Peek(port & k_io_isa_mask);
                return true;
            }

            if (m_dma->Decodes(port))
            {
                value = m_dma->Peek(port);
                return true;
            }

            if (m_audio->Decodes(port & k_io_isa_mask))
            {
                value = m_audio->Peek(port & k_io_isa_mask, clocks);
                return true;
            }

            if (m_serial_ports->Decodes(port & k_io_isa_mask))
            {
                value = m_serial_ports->Peek(port & k_io_isa_mask);
                return true;
            }

            if (m_game_port->Decodes(port & k_io_isa_mask))
            {
                value = m_game_port->Peek(clocks);
                return true;
            }

            if (m_parallel_port->Decodes(port & k_io_isa_mask))
            {
                value = m_parallel_port->Read(port & k_io_isa_mask);
                return true;
            }

            if (!m_system_control->Decodes(port))
                return false;

            value = m_system_control->Peek(port, clocks);
            return true;
    }
}

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

// Machine clock until which reads of the port repeat the last one without side effects, 0 when unknown
u64 IO::GetReadStableUntil(u16 port, int width, u64 clocks) const
{
    if (width != 8)
        return 0;

    switch (port)
    {
        case 0x0064:
            // Keyboard controller status
            return m_i8042->GetReadStableUntil(port);
        case 0x0061:
            // Refresh toggle and OUT2
        case 0x0080:
            // POST code
        case 0x0092:
            // Fast A20 and reset
            return m_system_control->GetReadStableUntil(port, clocks);
        case 0x0071:
            // CMOS data
            return m_rtc->GetReadStableUntil(port, clocks);
        case 0x0402:
            // Debug console readback
            return m_debug_console->Decodes(port) ? GDOX_NO_EVENT : 0;
        case 0x03BA:
            // VGA input status 1, monochrome block
        case 0x03DA:
            // VGA input status 1, color block
            return m_video->GetSyncStableUntil(clocks);
        default:
            if (m_fdc->Decodes(port & k_io_isa_mask))
                return m_fdc->GetReadStableUntil(port & k_io_isa_mask);

            if (m_ide->Decodes(port & k_io_isa_mask))
                return m_ide->GetReadStableUntil(port & k_io_isa_mask);

            if (m_audio->Decodes(port & k_io_isa_mask))
                return m_audio->GetReadStableUntil(port & k_io_isa_mask);

            if (m_serial_ports->Decodes(port & k_io_isa_mask))
                return m_serial_ports->GetReadStableUntil(port & k_io_isa_mask);

            if (m_game_port->Decodes(port & k_io_isa_mask))
                return m_game_port->GetReadStableUntil(clocks);

            if (m_parallel_port->Decodes(port & k_io_isa_mask))
                return GDOX_NO_EVENT;

            return m_dma->Decodes(port) ? m_dma->GetReadStableUntil(port) : 0;
    }
}

// A20, the fast reset and the DMA end the CPU batch after the write, a new transfer shouldn't wait for the slice
void IO::Write8(u16 port, u8 value, GDOX_Bus_Access_Context& context)
{
    u32 generation = m_memory->GetMapGeneration();

    switch (port)
    {
        case 0x0020:
            // PIC master command
        case 0x0021:
            // PIC master data
        case 0x00A0:
            // PIC slave command
        case 0x00A1:
            // PIC slave data
            m_pic->Write(port, value);
            break;
        case 0x0040:
            // PIT counter 0
        case 0x0041:
            // PIT counter 1
        case 0x0042:
            // PIT counter 2
        case 0x0043:
            // PIT control
            m_pit->Write(port, value, context.clocks);
            break;
        case 0x0060:
            // Keyboard controller data
        case 0x0064:
            // Keyboard controller command
            m_i8042->Write(port, value, context.clocks);
            context.end_batch = true;
            break;
        case 0x0070:
            // NMI mask and CMOS index
            m_rtc->Write(port, value, context.clocks);
            m_system_control->Write(port, value, context.clocks);
            break;
        case 0x0071:
            // CMOS data
            m_rtc->Write(port, value, context.clocks);
            break;
        case 0x0402:
            // Debug console
            if (m_debug_console->Decodes(port))
                m_debug_console->Write(value);
            break;
        case 0x0510:
            // Firmware config selector
            m_firmware_config->Write(port, value);
            break;
        case 0x0061:
            // Speaker gate and data
        case 0x0080:
            // POST code
        case 0x0092:
            // Fast A20 and reset
        case 0x00F0:
            // FPU error clear
        case 0x00F1:
            // Coprocessor reset
            m_system_control->Write(port, value, context.clocks);
            break;
        default:
            if (m_video->Decodes(port & k_io_isa_mask))
                m_video->Write(port & k_io_isa_mask, value, context.clocks);
            else if (m_fdc->Decodes(port & k_io_isa_mask))
            {
                m_fdc->Write(port & k_io_isa_mask, value, context.clocks);
                context.end_batch = true;
            }
            else if (m_ide->Decodes(port & k_io_isa_mask))
            {
                m_ide->Write(port & k_io_isa_mask, value, context.clocks);
                context.end_batch = true;
            }
            else if (m_dma->Decodes(port))
            {
                m_dma->Write(port, value);
                context.end_batch = true;
            }
            else if (m_audio->Decodes(port & k_io_isa_mask))
            {
                m_audio->Write(port & k_io_isa_mask, value, context.clocks);
                context.end_batch = true;
            }
            else if (m_serial_ports->Decodes(port & k_io_isa_mask))
            {
                m_serial_ports->Write(port & k_io_isa_mask, value, context.clocks);
                context.end_batch = true;
            }
            else if (m_game_port->Decodes(port & k_io_isa_mask))
                m_game_port->Write(context.clocks);
            else if (m_parallel_port->Decodes(port & k_io_isa_mask))
                m_parallel_port->Write(port & k_io_isa_mask, value);
            else
                Debug("Unknown IO write at %04X, value=%02X", port, value);
            break;
    }

    if ((m_memory->GetMapGeneration() != generation) || m_system_control->IsCPUResetPending())
        context.end_batch = true;
}
