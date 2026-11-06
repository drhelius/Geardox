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

#ifndef IDE_CONTROLLER_H
#define IDE_CONTROLLER_H

#include <iostream>
#include "../common/common.h"
#include "ide_disk.h"

#define IDE_CHANNELS 2
#define IDE_CHANNEL_DISKS 2
#define IDE_DISKS GDOX_MAX_HARD_DISKS

class PIC;
class Scheduler;
class StateSerializer;
class TraceLogger;

class IdeController
{
public:
    struct IdeController_State
    {
        u8 control[IDE_CHANNELS];
        u8 features[IDE_CHANNELS];
        u8 selected[IDE_CHANNELS];
    };

public:
    IdeController();
    ~IdeController();
    void Init(PIC* pic, Scheduler* scheduler);
    void SetTraceLogger(TraceLogger* trace_logger);
    void Reset();
    bool Decodes(u16 port) const;
    bool IsDataPort(u16 port) const;
    u8 Read(u16 port, u64 clocks);
    u8 Peek(u16 port) const;
    void Write(u16 port, u8 value, u64 clocks);
    u16 ReadData(u16 port, u64 clocks);
    void WriteData(u16 port, u16 value, u64 clocks);
    u64 GetReadStableUntil(u16 port) const;
    void Synchronize(u64 clocks);
    void HandleEvent(u64 clocks);
    IdeDisk* GetDisk(int id);
    IdeController_State* GetState();
    bool IsInterruptOut(int channel) const;
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream, int version = GDOX_SAVESTATE_VERSION);

private:
    static int GetChannel(u16 port);
    static bool IsControlPort(u16 port);
    IdeDisk& GetSelected(int channel);
    const IdeDisk& GetSelected(int channel) const;
    void Run(u64 clocks);
    void WriteControl(int channel, u8 value, u64 clocks);
    void WriteCommand(int channel, u8 command, u64 clocks);
    void UpdateIRQ(int channel);
    void UpdateNextEvent();
    void Serialize(StateSerializer& serializer, int version);
    void SanitizeState();

private:
    PIC* m_pic;
    Scheduler* m_scheduler;
    IdeDisk m_disks[IDE_DISKS];
    IdeController_State m_state;
    bool m_irq_out[IDE_CHANNELS];
    u64 m_event_clocks;
};

static const u8 k_ide_control_srst = 0x04;
static const u8 k_ide_control_nien = 0x02;
static const int k_ide_irq[IDE_CHANNELS] = { 14, 15 };

#include "ide_controller_inline.h"

#endif /* IDE_CONTROLLER_H */
