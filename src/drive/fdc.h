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

#ifndef FDC_H
#define FDC_H

#include <iostream>
#include "../common/common.h"
#include "floppy_disk.h"

#define FDC_DRIVES GDOX_MAX_FLOPPY_DRIVES

class PIC;
class Scheduler;
class StateSerializer;
class TraceLogger;

class FDC
{
public:
    struct FDC_State
    {
        u8 cylinders[FDC_DRIVES];
        bool disk_changed[FDC_DRIVES];
    };

public:
    FDC();
    ~FDC();
    void Init(PIC* pic, Scheduler* scheduler);
    void SetTraceLogger(TraceLogger* trace_logger);
    void Reset();
    void SetInternalDrives(int drives);
    bool InsertDisk(int drive, const u8* data, u32 size, bool write_protected, u32 base_crc, u32 data_crc = 0, const u8* source = NULL, u32 source_size = 0);
    void EjectDisk(int drive);
    void SwapDisks();
    FloppyDisk* GetDisk(int drive);
    FDC_State* GetState();
    void SaveState(std::ostream& stream, bool media = true);
    void LoadState(std::istream& stream, int version = GDOX_SAVESTATE_VERSION);

    u32 GetAccessCount(int drive) const;
    void SetReadCallback(void* context, GDOX_Read_Callback callback);
    void NotifyRead();
    u32 GetWriteCount(int drive) const;

private:
    void ChangeDisk(int drive);
    void Serialize(StateSerializer& serializer, int version);
    void SanitizeState();

private:
    PIC* m_pic;
    Scheduler* m_scheduler;
    TraceLogger* m_trace_logger;
    FloppyDisk m_disks[FDC_DRIVES];
    FDC_State m_state;
    u32 m_access_count[FDC_DRIVES];
    u32 m_write_count[FDC_DRIVES];
    void* m_read_context;
    GDOX_Read_Callback m_read_callback;
    int m_internal_drives;
};

static const u8 k_fdc_last_cylinder = 82;

#include "fdc_inline.h"

#endif /* FDC_H */
