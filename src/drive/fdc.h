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
#include "i82077.h"

#define FDC_DRIVES GDOX_MAX_FLOPPY_DRIVES

class DMA;
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
    void Init(PIC* pic, Scheduler* scheduler, DMA* dma);
    void SetTraceLogger(TraceLogger* trace_logger);
    void Reset();
    void SetInternalDrives(int drives);
    void SetDriveType(int drive, u8 type);
    u8 GetDriveType(int drive) const;
    bool Decodes(u16 port) const;
    u8 Read(u16 port, u64 clocks);
    u8 Peek(u16 port) const;
    void Write(u16 port, u8 value, u64 clocks);
    u64 GetReadStableUntil(u16 port) const;
    void Synchronize(u64 clocks);
    void HandleEvent(u64 clocks);
    bool InsertDisk(int drive, const u8* data, u32 size, bool write_protected, u32 base_crc, u32 data_crc = 0, const u8* source = NULL, u32 source_size = 0);
    void EjectDisk(int drive);
    void SwapDisks();
    FloppyDisk* GetDisk(int drive);
    I82077* GetController();
    FDC_State* GetState();
    void SaveState(std::ostream& stream, bool media = true);
    void LoadState(std::istream& stream, int version = GDOX_SAVESTATE_VERSION);

    u32 GetAccessCount(int drive) const;
    void SetReadCallback(void* context, GDOX_Read_Callback callback);
    void NotifyRead();
    u32 GetWriteCount(int drive) const;

    // The drive pins, for the chip
    int GetSelectedDrive() const;
    bool HasIndex() const;
    bool IsTrack0() const;
    bool IsWriteProtected() const;
    void Step(bool inward);
    int GetSectors(int side, u32 rate, FloppyDisk_Sector* sectors);
    u8 ReadByte(u32 offset);
    void WriteByte(u32 offset, u8 value);
    void SetSectorStatus(u32 header, bool deleted, u8 status);
    void WriteTrack(int side, const u8* ids, int count, u8 size_code, u8 filler, bool mfm);
    u64 GetNextIndex(u64 clocks) const;
    u64 GetPositionClocks(u64 clocks, u32 position, u32 track_bytes) const;
    u64 GetByteClocks(u32 count, u32 rate, bool mfm) const;
    u32 GetTrackBytes(u32 rate, bool mfm) const;
    u64 GetClocks() const;
    void UpdateLines();

private:
    void ChangeDisk(int drive);
    u32 GetRPM(int drive) const;
    bool IsRateReadable(int drive, u32 rate) const;
    int GetMediaTrack(int drive, int side) const;
    void UpdateNextEvent();
    static bool DMAReadCallback(void* device, u16& value, bool word);
    static bool DMAWriteCallback(void* device, u16 value, bool word);
    static void DMAEndCallback(void* device);
    void Serialize(StateSerializer& serializer, int version);
    void SanitizeState();

private:
    PIC* m_pic;
    Scheduler* m_scheduler;
    DMA* m_dma;
    TraceLogger* m_trace_logger;
    I82077 m_chip;
    FloppyDisk m_disks[FDC_DRIVES];
    FDC_State m_state;
    u8 m_drive_types[FDC_DRIVES];
    bool m_irq_out;
    bool m_drq_out;
    u32 m_access_count[FDC_DRIVES];
    u32 m_write_count[FDC_DRIVES];
    void* m_read_context;
    GDOX_Read_Callback m_read_callback;
    int m_internal_drives;
};

struct FDC_Drive_Info
{
    u32 rpm;
    u8 last_cylinder;
    bool narrow;
    u8 rates;
};

static const u8 k_fdc_rate_250 = 0x01;
static const u8 k_fdc_rate_300 = 0x02;
static const u8 k_fdc_rate_500 = 0x04;
static const u8 k_fdc_rate_1000 = 0x08;

static const FDC_Drive_Info k_fdc_drives[GDOX_FLOPPY_TYPE_COUNT] =
{
    { 300, 0, false, 0 },
    { 300, 41, true, k_fdc_rate_250 },
    { 360, 82, false, k_fdc_rate_300 | k_fdc_rate_500 },
    { 300, 82, false, k_fdc_rate_250 },
    { 300, 82, false, k_fdc_rate_250 | k_fdc_rate_500 },
    { 300, 82, false, k_fdc_rate_250 | k_fdc_rate_500 | k_fdc_rate_1000 }
};

static const u8 k_fdc_last_cylinder = 82;
static const int k_fdc_irq = 6;
static const int k_fdc_dma_channel = 2;
static const u64 k_fdc_turn = (u64)GDOX_MACHINE_CLOCK_RATE * 60;

#include "fdc_inline.h"

#endif /* FDC_H */
