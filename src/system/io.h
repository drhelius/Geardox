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

#ifndef IO_H
#define IO_H

#include "../common/common.h"

class Audio;
class DebugConsole;
class DMA;
class FDC;
class FirmwareConfig;
class I8042;
class IdeController;
class MC146818;
class GamePort;
class Memory;
class ParallelPort;
class PciBus;
class PIC;
class Piix;
class PIT;
class Video;
class Scheduler;
class SerialPorts;
class SystemControl;

class IO
{
public:
    IO();
    ~IO();
    void Init(Scheduler* scheduler, Audio* audio, PIC* pic, PIT* pit, Video* video, Memory* memory, FDC* fdc,
        IdeController* ide, SystemControl* system_control, MC146818* rtc, FirmwareConfig* firmware_config,
        DebugConsole* debug_console, I8042* i8042, DMA* dma, SerialPorts* serial_ports, ParallelPort* parallel_port,
        GamePort* game_port, PciBus* pci_bus, Piix* piix);
    void Reset();
    u8 Read8(u16 port, GDOX_Bus_Access_Context& context);
    u16 Read16(u16 port, GDOX_Bus_Access_Context& context);
    u32 Read32(u16 port, GDOX_Bus_Access_Context& context);
    void Write8(u16 port, u8 value, GDOX_Bus_Access_Context& context);
    void Write16(u16 port, u16 value, GDOX_Bus_Access_Context& context);
    void Write32(u16 port, u32 value, GDOX_Bus_Access_Context& context);
    bool Peek(u16 port, u64 clocks, u8& value) const;
    u64 GetReadStableUntil(u16 port, int width, u64 clocks) const;
    void UpdateDecodedPorts();
    const u8* GetDecodedPorts() const;

private:
    u8 ReadPort(u16 port, GDOX_Bus_Access_Context& context);
    u16 ReadIDEData(u16 port, GDOX_Bus_Access_Context& context);
    void WriteIDEData(u16 port, u16 value, GDOX_Bus_Access_Context& context);

private:
    Scheduler* m_scheduler;
    Audio* m_audio;
    PIC* m_pic;
    PIT* m_pit;
    Video* m_video;
    Memory* m_memory;
    FDC* m_fdc;
    IdeController* m_ide;
    SystemControl* m_system_control;
    MC146818* m_rtc;
    FirmwareConfig* m_firmware_config;
    DebugConsole* m_debug_console;
    I8042* m_i8042;
    SerialPorts* m_serial_ports;
    ParallelPort* m_parallel_port;
    GamePort* m_game_port;
    PciBus* m_pci_bus;
    Piix* m_piix;
    DMA* m_dma;
    u8 m_decoded_ports[0x2000];
};

static const u16 k_io_isa_mask = 0x03FF;

#include "io_inline.h"

#endif /* IO_H */
