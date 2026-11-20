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

#ifndef IDE_DISK_H
#define IDE_DISK_H

#include <iostream>
#include "../common/common.h"
#include "hard_disk_image.h"

#define IDE_DISK_MAX_BLOCK 16
#define IDE_DISK_BUFFER_SIZE (IDE_DISK_MAX_BLOCK * 512)

class StateSerializer;
class TraceLogger;

enum Ide_Phase
{
    IDE_PHASE_IDLE = 0,
    IDE_PHASE_READ,
    IDE_PHASE_DATA_IN,
    IDE_PHASE_WRITE_READY,
    IDE_PHASE_DATA_OUT,
    IDE_PHASE_WRITE,
    IDE_PHASE_FORMAT,
    IDE_PHASE_COMPLETE,
    IDE_PHASE_DIAGNOSTIC,
    IDE_PHASE_RESET_HELD,
    IDE_PHASE_RESET,
    IDE_PHASE_COUNT
};

class IdeDisk
{
public:
    struct IdeDisk_State
    {
        u8 status;
        u8 error;
        u8 sector_count;
        u8 sector_number;
        u8 cylinder_low;
        u8 cylinder_high;
        u8 device_head;
        bool interrupt;
        u8 command;
        u8 phase;
        u8 result;
        bool positioned;
        u32 lba;
        u32 remaining;
        u32 block;
        u32 position;
        u32 length;
        u64 event_clocks;
        u32 cylinders;
        u32 heads;
        u32 sectors;
        u32 current_cylinders;
        u32 current_heads;
        u32 current_sectors;
        u32 multiple;
        u32 head_cylinder;
        u32 ahead_lba;
        u32 ahead_end;
        u64 ahead_clocks;
        u8 buffer[IDE_DISK_BUFFER_SIZE];
    };

public:
    IdeDisk();
    ~IdeDisk();
    void Init(int id);
    void SetTraceLogger(TraceLogger* trace_logger);
    void SetDelayDivider(u32 divider);
    void Reset();
    bool Attach(const char* path, bool write_protected, const char* working_path = NULL);
    void Detach();
    bool IsAttached() const;
    HardDiskImage* GetImage();
    u8 GetStatus() const;
    u8 ReadRegister(int offset) const;
    void WriteRegister(int offset, u8 value);
    void Execute(u8 command, u8 features, u64 clocks);
    bool ReadData(u16& value, u64 clocks);
    bool WriteData(u16 value, u64 clocks);
    void HoldReset();
    void ReleaseReset(u64 clocks);
    u64 GetEventClocks() const;
    void RunEvent();
    bool IsInterrupting() const;
    void ClearInterrupt();
    u8 GetTranslation() const;
    u32 GetAccessCount() const;
    IdeDisk_State* GetState();
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream, int version = GDOX_SAVESTATE_VERSION);

private:
    u32 GetBlocks() const;
    void SetDefaults(bool reset);
    void DetectGeometry();
    bool ReadPartitionGeometry(u32* heads, u32* sectors);
    void SetCurrentGeometry(u32 heads, u32 sectors);
    bool ExecuteMedia(u8 command, u64 clocks);
    void Complete(u64 clocks, u8 error = 0);
    void Schedule(u8 phase, u64 clocks);
    void Finish(u8 error);
    bool GetAddress(bool track, u32* lba) const;
    void SetAddress(u32 lba);
    void StartRead(u64 clocks);
    void LoadBlock();
    void EndDataIn(u64 clocks);
    void StartWrite(u64 clocks);
    void StoreBlock();
    void FormatTrack();
    void BuildIdentify();
    u64 GetSeekClocks(u32 cylinder) const;
    u64 GetPositionClocks(u32 lba, u64 clocks);
    u64 GetReadClocks(u32 lba, u32 count, u64 clocks, u64 overhead);
    NO_INLINE void Trace(u8 event, u8 command);
    void Serialize(StateSerializer& serializer, int version);
    void SanitizeState();

private:
    HardDiskImage m_image;
    TraceLogger* m_trace_logger;
    u8 m_trace_features;
    IdeDisk_State m_state;
    int m_id;
    u32 m_access_count;
    u32 m_delay_divider;
};

static const u8 k_ide_status_bsy = 0x80;
static const u8 k_ide_status_drdy = 0x40;
static const u8 k_ide_status_dsc = 0x10;
static const u8 k_ide_status_drq = 0x08;
static const u8 k_ide_status_err = 0x01;

static const u8 k_ide_error_unc = 0x40;
static const u8 k_ide_error_idnf = 0x10;
static const u8 k_ide_error_abrt = 0x04;

static const u8 k_ide_head_lba = 0x40;
static const u8 k_ide_command_diagnostic = 0x90;
static const u8 k_ide_command_format = 0x50;
static const u8 k_ide_command_identify = 0xEC;
static const u32 k_ide_max_lba = 0x0FFFFFFF;
static const u32 k_ide_max_cylinders = 16383;

#include "ide_disk_inline.h"

#endif /* IDE_DISK_H */
