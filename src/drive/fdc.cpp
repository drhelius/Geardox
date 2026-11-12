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

#include <new>
#include "fdc.h"
#include "floppy_image.h"
#include "../common/state_serializer.h"
#include "../system/dma.h"
#include "../system/pic.h"
#include "../system/scheduler.h"

FDC::FDC()
{
    InitPointer(m_pic);
    InitPointer(m_scheduler);
    InitPointer(m_dma);
    InitPointer(m_trace_logger);
    InitPointer(m_read_context);
    InitPointer(m_read_callback);
    memset(&m_state, 0, sizeof(m_state));
    memset(m_state.disk_changed, 1, sizeof(m_state.disk_changed));
    memset(m_access_count, 0, sizeof(m_access_count));
    memset(m_write_count, 0, sizeof(m_write_count));
    memset(m_drive_types, 0, sizeof(m_drive_types));
    m_irq_out = false;
    m_drq_out = false;
    m_internal_drives = FDC_DRIVES;
}

FDC::~FDC()
{
}

void FDC::Init(PIC* pic, Scheduler* scheduler, DMA* dma)
{
    m_pic = pic;
    m_scheduler = scheduler;
    m_dma = dma;
    m_chip.Init(this);

    GDOX_DMA_Endpoint endpoint = { this, DMAReadCallback, DMAWriteCallback, DMAEndCallback, NULL };
    m_dma->SetEndpoint(k_fdc_dma_channel, endpoint);

    Reset();
}

void FDC::SetTraceLogger(TraceLogger* trace_logger)
{
    m_trace_logger = trace_logger;
    m_chip.SetTraceLogger(trace_logger);
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
        m_state.disk_changed[i] = true;

    m_chip.Reset();
    UpdateLines();
    UpdateNextEvent();
}

void FDC::SetInternalDrives(int drives)
{
    m_internal_drives = CLAMP(drives, 0, FDC_DRIVES);
}

void FDC::SetDriveType(int drive, u8 type)
{
    if (drive >= 0 && drive < FDC_DRIVES)
        m_drive_types[drive] = MIN(type, (u8)(GDOX_FLOPPY_TYPE_COUNT - 1));
}

u8 FDC::Read(u16 port, u64 clocks)
{
    Synchronize(clocks);

    u8 value = Peek(port);

    if ((port & 0x07) == 5)
    {
        value = m_chip.Read(5, clocks);
        UpdateNextEvent();
    }

    return value;
}

// The change line of the selected drive is the only bit of 3F7h this chip drives
u8 FDC::Peek(u16 port) const
{
    if ((port & 0x07) != 7)
        return m_chip.Peek(port & 0x07);

    int drive = GetSelectedDrive();

    return (u8)(0x7F | ((drive >= 0 && m_state.disk_changed[drive]) ? 0x80 : 0x00));
}

void FDC::Write(u16 port, u8 value, u64 clocks)
{
    Synchronize(clocks);
    m_chip.Write(port & 0x07, value, clocks);
    UpdateLines();
    UpdateNextEvent();
}

u64 FDC::GetClocks() const
{
    return m_scheduler->GetClocks();
}

void FDC::Synchronize(u64 clocks)
{
    m_chip.Run(clocks);
}

void FDC::HandleEvent(u64 clocks)
{
    Synchronize(clocks);
    UpdateNextEvent();
}

void FDC::UpdateNextEvent()
{
    m_scheduler->Schedule(SCHEDULER_EVENT_FDC, m_chip.GetEventClocks());
}

void FDC::UpdateLines()
{
    bool irq = m_chip.IsInterruptOut();
    bool drq = m_chip.IsRequestOut();

    if (irq != m_irq_out)
    {
        m_irq_out = irq;
        m_pic->SetIRQLine(k_fdc_irq, irq);
    }

    if (drq != m_drq_out)
    {
        m_drq_out = drq;
        m_dma->SetRequest(k_fdc_dma_channel, drq);
    }
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

    if (IsValidPointer(m_scheduler))
    {
        m_chip.Resume(m_scheduler->GetClocks());
        UpdateNextEvent();
    }
}

// A pulse clears the change line if a disk is in. The head stops at the end of its travel
void FDC::Step(bool inward)
{
    int drive = GetSelectedDrive();

    if (drive < 0)
        return;

    u8& cylinder = m_state.cylinders[drive];

    if (inward && cylinder < k_fdc_drives[m_drive_types[drive]].last_cylinder)
        cylinder++;
    else if (!inward && cylinder > 0)
        cylinder--;

    if (m_disks[drive].IsInserted())
        m_state.disk_changed[drive] = false;
}

// What passes under the head has to match the rate selected, and the drive has to be able to run at it
bool FDC::IsRateReadable(int drive, u32 rate) const
{
    const FDC_Drive_Info& info = k_fdc_drives[m_drive_types[drive]];
    const FloppyDisk& disk = m_disks[drive];
    u8 bit = rate == 250 ? k_fdc_rate_250 : rate == 300 ? k_fdc_rate_300 : rate == 500 ? k_fdc_rate_500 : k_fdc_rate_1000;
    u32 media_rate = disk.GetMedia() == FLOPPY_MEDIA_ED ? 1000 : disk.GetMedia() == FLOPPY_MEDIA_2HD ? 500 : 250;

    return (info.rates & bit) != 0 && (media_rate * info.rpm) / disk.GetRPM() == rate;
}

// A 96 TPI head over a 48 TPI disk only finds a track every second cylinder, and the other way round it skips half
int FDC::GetMediaTrack(int drive, int side) const
{
    int cylinder = m_state.cylinders[drive];
    bool narrow_disk = m_disks[drive].GetMedia() == FLOPPY_MEDIA_2D;
    bool narrow_drive = k_fdc_drives[m_drive_types[drive]].narrow;

    if (narrow_disk && !narrow_drive)
    {
        if ((cylinder & 1) != 0)
            return -1;

        cylinder /= 2;
    }
    else if (!narrow_disk && narrow_drive)
        cylinder *= 2;

    int track = (cylinder * 2) + side;

    return track < k_floppy_tracks ? track : -1;
}

int FDC::GetSectors(int side, u32 rate, FloppyDisk_Sector* sectors)
{
    int drive = GetSelectedDrive();

    if (drive < 0 || !m_disks[drive].IsInserted() || !IsRateReadable(drive, rate))
        return 0;

    int track = GetMediaTrack(drive, side);

    return track >= 0 ? m_disks[drive].GetSectors(track, sectors) : 0;
}

u8 FDC::ReadByte(u32 offset)
{
    int drive = GetSelectedDrive();

    if (drive < 0)
        return 0xFF;

    m_access_count[drive]++;
    return m_disks[drive].ReadWeakByte(offset, 0xFF);
}

void FDC::WriteByte(u32 offset, u8 value)
{
    int drive = GetSelectedDrive();

    if (drive < 0)
        return;

    m_write_count[drive]++;
    m_disks[drive].WriteByte(offset, value);
}

void FDC::SetSectorStatus(u32 header, bool deleted, u8 status)
{
    int drive = GetSelectedDrive();

    if (drive >= 0)
        m_disks[drive].SetSectorStatus(header, deleted, status);
}

// A sector run in the image's own layout, every sector full of the filler byte
void FDC::WriteTrack(int side, const u8* ids, int count, u8 size_code, u8 filler, bool mfm)
{
    int drive = GetSelectedDrive();
    int track = drive >= 0 ? GetMediaTrack(drive, side) : -1;

    if (track < 0)
        return;

    u32 sector_size = 128U << (size_code & 0x07);
    u32 size = (u32)count * (k_floppy_sector_header_size + sector_size);
    u8* run = new (std::nothrow) u8[MAX(size, 1U)];

    if (!IsValidPointer(run))
        return;

    memset(run, 0, size);

    for (int i = 0; i < count; i++)
    {
        u8* header = run + i * (k_floppy_sector_header_size + sector_size);

        memcpy(header, ids + i * 4, 4);
        write_u16_le(header + 4, (u16)count);
        header[6] = mfm ? 0x00 : k_floppy_density_fm;
        write_u16_le(header + 14, (u16)sector_size);
        memset(header + k_floppy_sector_header_size, filler, sector_size);
    }

    m_disks[drive].ReplaceTrack(track, run, size, GetRPM(drive));
    m_write_count[drive]++;
    SafeDeleteArray(run);
}

// The spindle is a function of the clock, an index pulse is the start of a turn
u64 FDC::GetNextIndex(u64 clocks) const
{
    int drive = GetSelectedDrive();
    u32 rpm = drive >= 0 ? GetRPM(drive) : 300;
    u64 turns = (clocks * rpm) / k_fdc_turn + 1;

    return (turns * k_fdc_turn + rpm - 1) / rpm;
}

u64 FDC::GetPositionClocks(u64 clocks, u32 position, u32 track_bytes) const
{
    int drive = GetSelectedDrive();
    u32 rpm = drive >= 0 ? GetRPM(drive) : 300;
    u64 angle = (clocks * rpm) % k_fdc_turn;
    u64 target = ((u64)position * k_fdc_turn) / MAX(track_bytes, 1U);
    u64 delta = (target + k_fdc_turn - angle) % k_fdc_turn;

    return clocks + (delta + rpm - 1) / rpm;
}

u32 FDC::GetTrackBytes(u32 rate, bool mfm) const
{
    int drive = GetSelectedDrive();
    u32 rpm = drive >= 0 ? GetRPM(drive) : 300;

    return (rate * 1000 * 60) / (rpm * (mfm ? 8 : 16));
}

bool FDC::DMAReadCallback(void* device, u16& value, bool word)
{
    UNUSED(word);
    return ((FDC*)device)->m_chip.ReadDMA(value);
}

bool FDC::DMAWriteCallback(void* device, u16 value, bool word)
{
    UNUSED(word);
    return ((FDC*)device)->m_chip.WriteDMA(value);
}

void FDC::DMAEndCallback(void* device)
{
    ((FDC*)device)->m_chip.TerminalCount();
}

void FDC::SaveState(std::ostream& stream, bool media)
{
    StateSerializer serializer(stream);
    Serialize(serializer, GDOX_SAVESTATE_VERSION);
    m_chip.SaveState(stream);

    for (int i = 0; i < FDC_DRIVES; i++)
        m_disks[i].SaveState(stream, media);
}

void FDC::LoadState(std::istream& stream, int version)
{
    StateSerializer serializer(stream);
    Serialize(serializer, version);
    m_chip.LoadState(stream);

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

    m_irq_out = m_chip.IsInterruptOut();
    m_drq_out = m_chip.IsRequestOut();
    UpdateNextEvent();
}
