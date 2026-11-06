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

#include "ide_controller.h"
#include "../system/pic.h"
#include "../system/scheduler.h"
#include "../common/state_serializer.h"

IdeController::IdeController()
{
    InitPointer(m_pic);
    InitPointer(m_scheduler);
    memset(&m_state, 0, sizeof(m_state));
    memset(m_irq_out, 0, sizeof(m_irq_out));
    m_event_clocks = GDOX_NO_EVENT;
}

IdeController::~IdeController()
{
}

void IdeController::Init(PIC* pic, Scheduler* scheduler)
{
    m_pic = pic;
    m_scheduler = scheduler;

    for (int i = 0; i < IDE_DISKS; i++)
        m_disks[i].Init(i);

    Reset();
}

void IdeController::SetTraceLogger(TraceLogger* trace_logger)
{
    for (int i = 0; i < IDE_DISKS; i++)
        m_disks[i].SetTraceLogger(trace_logger);
}

void IdeController::Reset()
{
    memset(&m_state, 0, sizeof(m_state));

    for (int i = 0; i < IDE_DISKS; i++)
        m_disks[i].Reset();

    for (int i = 0; i < IDE_CHANNELS; i++)
        UpdateIRQ(i);

    UpdateNextEvent();
}

void IdeController::Run(u64 clocks)
{
    for (int i = 0; i < IDE_DISKS; i++)
    {
        while (m_disks[i].GetEventClocks() <= clocks)
            m_disks[i].RunEvent();
    }

    for (int i = 0; i < IDE_CHANNELS; i++)
        UpdateIRQ(i);

    UpdateNextEvent();
}

void IdeController::UpdateNextEvent()
{
    m_event_clocks = GDOX_NO_EVENT;

    for (int i = 0; i < IDE_DISKS; i++)
        m_event_clocks = MIN(m_event_clocks, m_disks[i].GetEventClocks());

    m_scheduler->Schedule(SCHEDULER_EVENT_IDE, m_event_clocks);
}

// INTRQ only goes out for the selected disk, and nIEN holds it back without clearing it
void IdeController::UpdateIRQ(int channel)
{
    bool irq = GetSelected(channel).IsInterrupting() && (m_state.control[channel] & k_ide_control_nien) == 0;

    if (irq != m_irq_out[channel])
    {
        m_irq_out[channel] = irq;
        m_pic->SetIRQLine(k_ide_irq[channel], irq);
    }
}

u8 IdeController::Read(u16 port, u64 clocks)
{
    Synchronize(clocks);

    if (!IsControlPort(port) && (port & 0x07) == 0)
        return (u8)ReadData(port, clocks);

    u8 value = Peek(port);

    if (!IsControlPort(port) && (port & 0x07) == 7)
    {
        int channel = GetChannel(port);
        GetSelected(channel).ClearInterrupt();
        UpdateIRQ(channel);
    }

    return value;
}

// An absent disk reads 00h in Status, and everything else comes from the other one
u8 IdeController::Peek(u16 port) const
{
    int channel = GetChannel(port);
    const IdeDisk& selected = GetSelected(channel);

    if (IsControlPort(port) || (port & 0x07) == 7 || (selected.GetStatus() & k_ide_status_bsy) != 0)
        return selected.GetStatus();

    int other = (channel * IDE_CHANNEL_DISKS) + (m_state.selected[channel] ^ 1);

    return (selected.IsAttached() ? selected : m_disks[other]).ReadRegister(port & 0x07);
}

void IdeController::Write(u16 port, u8 value, u64 clocks)
{
    Synchronize(clocks);

    int channel = GetChannel(port);
    int offset = port & 0x07;

    if (IsControlPort(port))
        WriteControl(channel, value, clocks);
    else if (offset == 0)
        WriteData(port, value, clocks);
    else if ((GetSelected(channel).GetStatus() & k_ide_status_bsy) != 0)
        return;
    else if (offset == 1)
        m_state.features[channel] = value;
    else if (offset == 7)
        WriteCommand(channel, value, clocks);
    else
    {
        m_disks[channel * IDE_CHANNEL_DISKS].WriteRegister(offset, value);
        m_disks[(channel * IDE_CHANNEL_DISKS) + 1].WriteRegister(offset, value);

        if (offset == 6)
            m_state.selected[channel] = (value >> 4) & 1;
    }

    UpdateIRQ(channel);
    UpdateNextEvent();
}

// SRST holds both disks busy and leaves disk 0 selected, the diagnostic starts when it's cleared
void IdeController::WriteControl(int channel, u8 value, u64 clocks)
{
    bool held = (m_state.control[channel] & k_ide_control_srst) != 0;
    m_state.control[channel] = value;

    if ((value & k_ide_control_srst) != 0 && !held)
    {
        m_state.selected[channel] = 0;

        for (int i = 0; i < IDE_CHANNEL_DISKS; i++)
            m_disks[(channel * IDE_CHANNEL_DISKS) + i].HoldReset();
    }
    else if ((value & k_ide_control_srst) == 0 && held)
    {
        for (int i = 0; i < IDE_CHANNEL_DISKS; i++)
            m_disks[(channel * IDE_CHANNEL_DISKS) + i].ReleaseReset(clocks);
    }
}

// Only the selected disk acts on a command, but a diagnostic reaches both and ignores the DEV bit
void IdeController::WriteCommand(int channel, u8 command, u64 clocks)
{
    if (command == k_ide_command_diagnostic)
    {
        m_state.selected[channel] = 0;

        for (int i = 0; i < IDE_CHANNEL_DISKS; i++)
            m_disks[(channel * IDE_CHANNEL_DISKS) + i].Execute(command, m_state.features[channel], clocks);
    }
    else
        GetSelected(channel).Execute(command, m_state.features[channel], clocks);
}

u16 IdeController::ReadData(u16 port, u64 clocks)
{
    Synchronize(clocks);

    u16 value = 0xFFFF;

    if (GetSelected(GetChannel(port)).ReadData(value, clocks))
        UpdateNextEvent();

    return value;
}

void IdeController::WriteData(u16 port, u16 value, u64 clocks)
{
    Synchronize(clocks);

    if (GetSelected(GetChannel(port)).WriteData(value, clocks))
        UpdateNextEvent();
}

void IdeController::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer, GDOX_SAVESTATE_VERSION);

    for (int i = 0; i < IDE_DISKS; i++)
        m_disks[i].SaveState(stream);
}

void IdeController::LoadState(std::istream& stream, int version)
{
    StateSerializer serializer(stream);
    Serialize(serializer, version);

    for (int i = 0; i < IDE_DISKS; i++)
        m_disks[i].LoadState(stream, version);

    SanitizeState();
}

void IdeController::Serialize(StateSerializer& serializer, int version)
{
    UNUSED(version);
    G_SERIALIZE_ARRAY(serializer, m_state.control, IDE_CHANNELS);
    G_SERIALIZE_ARRAY(serializer, m_state.features, IDE_CHANNELS);
    G_SERIALIZE_ARRAY(serializer, m_state.selected, IDE_CHANNELS);
}

void IdeController::SanitizeState()
{
    for (int i = 0; i < IDE_CHANNELS; i++)
    {
        m_state.selected[i] &= 1;
        m_irq_out[i] = GetSelected(i).IsInterrupting() && (m_state.control[i] & k_ide_control_nien) == 0;
    }

    UpdateNextEvent();
}
