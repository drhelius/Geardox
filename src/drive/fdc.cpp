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

#include "fdc.h"
#include "../common/state_serializer.h"

FDC::FDC()
{
    InitPointer(m_pic);
    InitPointer(m_scheduler);
    InitPointer(m_trace_logger);
    InitPointer(m_read_context);
    InitPointer(m_read_callback);
    memset(&m_state, 0, sizeof(m_state));
    memset(m_state.disk_changed, 1, sizeof(m_state.disk_changed));
    memset(m_access_count, 0, sizeof(m_access_count));
    memset(m_write_count, 0, sizeof(m_write_count));
    m_internal_drives = FDC_DRIVES;
}

FDC::~FDC()
{
}

void FDC::Init(PIC* pic, Scheduler* scheduler)
{
    m_pic = pic;
    m_scheduler = scheduler;

    Reset();
}

void FDC::SetTraceLogger(TraceLogger* trace_logger)
{
    m_trace_logger = trace_logger;
}

void FDC::SetReadCallback(void* context, GDOX_Read_Callback callback)
{
    m_read_context = context;
    m_read_callback = callback;
}

void FDC::NotifyRead()
{
    if (IsValidPointer(m_read_callback))
        m_read_callback(m_read_context);
}

// Disks stay in their drives
void FDC::Reset()
{
    memset(&m_state, 0, sizeof(m_state));

    for (int i = 0; i < FDC_DRIVES; i++)
        m_state.disk_changed[i] = !m_disks[i].IsInserted();
}

void FDC::SetInternalDrives(int drives)
{
    m_internal_drives = CLAMP(drives, 0, FDC_DRIVES);
}

bool FDC::InsertDisk(int drive, const u8* data, u32 size, bool write_protected, u32 base_crc, u32 data_crc, const u8* source, u32 source_size)
{
    if (drive < 0 || drive >= FDC_DRIVES ||
        !m_disks[drive].Insert(data, size, write_protected, base_crc, data_crc, source, source_size))
        return false;

    ChangeDisk(drive);
    return true;
}

void FDC::EjectDisk(int drive)
{
    if (drive < 0 || drive >= FDC_DRIVES)
        return;

    m_disks[drive].Eject();
    ChangeDisk(drive);
}

void FDC::SwapDisks()
{
    m_disks[0].Swap(m_disks[1]);
    ChangeDisk(0);
    ChangeDisk(1);
}

void FDC::ChangeDisk(int drive)
{
    m_state.disk_changed[drive] = true;
}

void FDC::SaveState(std::ostream& stream, bool media)
{
    StateSerializer serializer(stream);
    Serialize(serializer, GDOX_SAVESTATE_VERSION);

    for (int i = 0; i < FDC_DRIVES; i++)
        m_disks[i].SaveState(stream, media);
}

void FDC::LoadState(std::istream& stream, int version)
{
    StateSerializer serializer(stream);
    Serialize(serializer, version);

    for (int i = 0; i < FDC_DRIVES; i++)
        m_disks[i].LoadState(stream, version);

    SanitizeState();
}

void FDC::Serialize(StateSerializer& serializer, int version)
{
    UNUSED(version);
    G_SERIALIZE_ARRAY(serializer, m_state.cylinders, FDC_DRIVES);
    G_SERIALIZE_ARRAY(serializer, m_state.disk_changed, FDC_DRIVES);
}

void FDC::SanitizeState()
{
    for (int i = 0; i < FDC_DRIVES; i++)
        m_state.cylinders[i] = (u8)MIN(m_state.cylinders[i], k_fdc_last_cylinder);
}
