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

#include "ide_disk.h"
#include "ide_common.h"
#include "../common/trace_logger.h"
#include "../common/state_serializer.h"

// The WD Caviar AC2120: 3605 rpm, 48 sectors a track in its outer zone, 0.3 ms of overhead, a 32 KB read-ahead buffer
static const u64 k_ide_turn_clocks = ((u64)GDOX_MACHINE_CLOCK_RATE * 60) / 3605;
static const u32 k_ide_track_sectors = 48;
static const u64 k_ide_sector_clocks = k_ide_turn_clocks / k_ide_track_sectors;
static const u64 k_ide_overhead_clocks = ((u64)GDOX_MACHINE_CLOCK_RATE * 3) / 10000;
static const u64 k_ide_block_clocks = ((u64)GDOX_MACHINE_CLOCK_RATE * 5) / 1000000;
static const u64 k_ide_reset_clocks = (u64)GDOX_MACHINE_CLOCK_RATE / 1000;
static const u32 k_ide_ahead_sectors = 64;
static const u32 k_ide_track_seek_us = 6000;
static const u32 k_ide_seek_points = 16;

// 22 ms x (distance / stroke) ^ 0.8 above a track to track seek, for an average of 14.7 ms and a maximum of 28 ms
static const u32 k_ide_seek_us[k_ide_seek_points + 1] =
{
    0, 2394, 4168, 5765, 7257, 8676, 10038, 11355, 12636, 13884, 15105, 16302, 17477, 18633, 19771, 20893, 22000
};

IdeDisk::IdeDisk()
{
    m_trace_logger = TraceLogger::GetDisabled();
    m_trace_features = 0;
    memset(&m_state, 0, sizeof(m_state));
    m_state.event_clocks = GDOX_NO_EVENT;
    m_id = 0;
    m_access_count = 0;
    m_delay_divider = 1;
    m_image = new HardDiskImage();
}

IdeDisk::~IdeDisk()
{
    SafeDelete(m_image);
}

void IdeDisk::Init(int id)
{
    m_id = id;
    Reset();
}

void IdeDisk::SetTraceLogger(TraceLogger* trace_logger)
{
    m_trace_logger = TraceLogger::Resolve(trace_logger);
}

// The image only changes at a reset, so the geometry stays put between them
void IdeDisk::Reset()
{
    memset(&m_state, 0, sizeof(m_state));
    m_state.event_clocks = GDOX_NO_EVENT;
    m_state.block = 1;

    if (IsAttached())
        DetectGeometry();

    SetDefaults(true);
}

bool IdeDisk::Attach(const char* path, bool write_protected, const char* working_path)
{
    Reset();

    if (!m_image->Open(path, write_protected, working_path))
        return false;

    Log("Hard disk image %s: %u sectors%s", path, m_image->GetBlocks(), m_image->IsWriteProtected() ? ", write protected" : "");
    return true;
}

bool IdeDisk::Attach(HardDiskSource* source, const char* path)
{
    Reset();

    if (!m_image->Open(source, path))
        return false;

    Log("Hard disk from folder %s: %u sectors", path, m_image->GetBlocks());
    return true;
}

bool IdeDisk::Attach(HardDiskImage* image)
{
    Reset();

    if (!IsValidPointer(image) || !image->IsOpen())
    {
        SafeDelete(image);
        return false;
    }

    SafeDelete(m_image);
    m_image = image;
    Log("Hard disk image %s: %u sectors%s", m_image->GetPath(), m_image->GetBlocks(),
        m_image->IsWriteProtected() ? ", write protected" : "");
    return true;
}

void IdeDisk::Detach()
{
    m_image->Close();
    Reset();
}

bool IdeDisk::IsAttached() const
{
    return m_image->IsOpen();
}

u32 IdeDisk::GetBlocks() const
{
    return MIN(m_image->GetBlocks(), k_ide_max_lba);
}

// Ends a diagnostic with code 01h, an absent disk reads 00h. A reset also brings back the default translation
void IdeDisk::SetDefaults(bool reset)
{
    m_state.phase = IDE_PHASE_IDLE;
    m_state.position = 0;
    m_state.length = 0;
    m_state.remaining = 0;
    m_state.status = IsAttached() ? (u8)(k_ide_status_drdy | k_ide_status_dsc) : 0;
    m_state.error = 0x01;
    m_state.sector_count = 1;
    m_state.sector_number = 1;
    m_state.cylinder_low = 0;
    m_state.cylinder_high = 0;
    m_state.device_head = 0;
    m_state.interrupt = false;

    if (reset)
    {
        m_state.multiple = 0;
        m_state.current_cylinders = m_state.cylinders;
        m_state.current_heads = m_state.heads;
        m_state.current_sectors = m_state.sectors;
    }
}

// Image metadata first, then what the first partition was laid out for, then the most a CHS drive can address
void IdeDisk::DetectGeometry()
{
    u32 heads = 0;
    u32 sectors = 0;

    if (!m_image->GetGeometry(&heads, &sectors) || heads > 16 || sectors > 63)
    {
        if (!ReadPartitionGeometry(&heads, &sectors))
        {
            heads = 16;
            sectors = 63;
        }
    }

    m_state.heads = heads;
    m_state.sectors = sectors;
    m_state.cylinders = CLAMP(GetBlocks() / (heads * sectors), 1U, k_ide_max_cylinders);
}

// A partition ends at the end of a cylinder, so its last CHS gives the geometry
// Modern tools align to 1 MB and end anywhere, so the CHS and the LBA fields have to agree under it
bool IdeDisk::ReadPartitionGeometry(u32* heads, u32* sectors)
{
    u8 mbr[k_hard_disk_block_size];

    if (!m_image->ReadBlock(0, mbr) || mbr[510] != 0x55 || mbr[511] != 0xAA)
        return false;

    for (int i = 0; i < 4; i++)
    {
        const u8* entry = mbr + 0x1BE + (i * 16);
        u32 last_head = entry[5];
        u32 last_sector = entry[6] & 0x3F;
        u32 last_cylinder = entry[7] | ((u32)(entry[6] & 0xC0) << 2);
        u32 first_head = entry[1];
        u32 first_sector = entry[2] & 0x3F;
        u32 first_cylinder = entry[3] | ((u32)(entry[2] & 0xC0) << 2);
        u32 first_lba = read_u32_le(entry + 8);
        u32 count = read_u32_le(entry + 12);

        if (entry[4] == 0 || count == 0)
            continue;

        u32 track_count = last_head + 1;

        if (last_sector == 0 || first_sector == 0 || last_head >= 16 ||
            ((first_cylinder * track_count + first_head) * last_sector) + first_sector - 1 != first_lba ||
            ((last_cylinder * track_count + last_head) * last_sector) + last_sector - 1 != first_lba + count - 1)
            return false;

        *heads = track_count;
        *sectors = last_sector;
        return true;
    }

    return false;
}

void IdeDisk::SetCurrentGeometry(u32 heads, u32 sectors)
{
    m_state.current_heads = heads;
    m_state.current_sectors = sectors;
    m_state.current_cylinders = sectors != 0 ? MIN(GetBlocks() / (heads * sectors), 65535U) : 0;
}

u8 IdeDisk::ReadRegister(int offset) const
{
    switch (offset)
    {
        case 0:
            return m_state.phase == IDE_PHASE_DATA_IN ? m_state.buffer[m_state.position] : 0xFF;
        case 1:
            return m_state.error;
        case 2:
            return m_state.sector_count;
        case 3:
            return m_state.sector_number;
        case 4:
            return m_state.cylinder_low;
        case 5:
            return m_state.cylinder_high;
        default:
            return m_state.device_head;
    }
}

void IdeDisk::WriteRegister(int offset, u8 value)
{
    switch (offset)
    {
        case 2:
            m_state.sector_count = value;
            break;
        case 3:
            m_state.sector_number = value;
            break;
        case 4:
            m_state.cylinder_low = value;
            break;
        case 5:
            m_state.cylinder_high = value;
            break;
        default:
            m_state.device_head = (u8)(value | 0xA0);
            break;
    }
}

void IdeDisk::Schedule(u8 phase, u64 clocks)
{
    m_state.phase = phase;
    m_state.event_clocks = clocks;
}

void IdeDisk::Execute(u8 command, u8 features, u64 clocks)
{
    m_trace_features = features;

    if (!IsAttached())
    {
        if (unlikely(m_trace_logger->IsEventEnabled(TRACE_IDE, TRACE_IDE_IGNORED)))
            Trace(TRACE_IDE_IGNORED, command);

        return;
    }

    m_state.command = command;
    m_state.positioned = false;
    m_state.result = 0;
    m_state.position = 0;
    m_state.length = 0;
    m_state.remaining = 0;
    m_state.error = 0;
    m_state.interrupt = false;
    m_state.status = k_ide_status_bsy | k_ide_status_drdy | k_ide_status_dsc;

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_IDE, TRACE_IDE_COMMAND)))
        Trace(TRACE_IDE_COMMAND, command);

    if (ExecuteMedia(command, clocks))
        return;

    switch (command)
    {
        case k_ide_command_diagnostic:
            m_state.status = k_ide_status_bsy;
            Schedule(IDE_PHASE_DIAGNOSTIC, clocks + k_ide_reset_clocks);
            break;
        case 0x91:
            SetCurrentGeometry((m_state.device_head & 0x0F) + 1U, m_state.sector_count);
            Complete(clocks);
            break;
        case 0xC6:
            if (m_state.sector_count > IDE_DISK_MAX_BLOCK || (m_state.sector_count & (m_state.sector_count - 1)) != 0)
            {
                m_state.multiple = 0;
                Complete(clocks, k_ide_error_abrt);
                break;
            }

            m_state.multiple = m_state.sector_count;
            Complete(clocks);
            break;
        case k_ide_command_identify:
            Schedule(IDE_PHASE_READ, clocks + k_ide_overhead_clocks);
            break;
        case 0xEF:
            Complete(clocks, features == 0x02 || features == 0x03 || features == 0x55 || features == 0x82 ||
                features == 0xAA ? 0 : k_ide_error_abrt);
            break;
        case 0xE5:
            m_state.sector_count = 0xFF;
            Complete(clocks);
            break;
        case 0xE0:
        case 0xE1:
        case 0xE2:
        case 0xE3:
        case 0xE6:
            Complete(clocks);
            break;
        default:
            Complete(clocks, k_ide_error_abrt);
            break;
    }
}

bool IdeDisk::ExecuteMedia(u8 command, u64 clocks)
{
    bool multiple = command == 0xC4 || command == 0xC5;
    bool read = command == 0x20 || command == 0x21 || command == 0xC4;
    bool write = command == 0x30 || command == 0x31 || command == 0xC5;
    bool verify = command == 0x40 || command == 0x41;
    bool seek = (command & 0xF0) == 0x70;
    bool recalibrate = (command & 0xF0) == 0x10;
    bool format = command == k_ide_command_format;

    if (!read && !write && !verify && !seek && !recalibrate && !format)
        return false;

    u32 count = m_state.sector_count != 0 ? m_state.sector_count : 256;

    if (recalibrate)
    {
        u64 end = clocks + k_ide_overhead_clocks + GetSeekClocks(0);
        m_state.head_cylinder = 0;
        m_state.ahead_end = m_state.ahead_lba;
        m_state.cylinder_low = 0;
        m_state.cylinder_high = 0;
        m_state.sector_number = (m_state.device_head & k_ide_head_lba) != 0 ? 0 : 1;
        m_state.device_head &= 0xF0;
        Schedule(IDE_PHASE_COMPLETE, end);
        return true;
    }

    // ATA has no way to say a disk is read only, an abort is the closest. DOS calls it a write fault
    if ((multiple && m_state.multiple == 0) || ((write || format) && m_image->IsWriteProtected()))
    {
        Complete(clocks, k_ide_error_abrt);
        return true;
    }

    if (!GetAddress(seek || format, &m_state.lba))
    {
        Complete(clocks, k_ide_error_idnf);
        return true;
    }

    if (seek)
    {
        u32 cylinder = m_state.lba / (m_state.heads * m_state.sectors);
        u64 end = clocks + k_ide_overhead_clocks + GetSeekClocks(cylinder);
        m_state.head_cylinder = cylinder;
        m_state.ahead_end = m_state.ahead_lba;
        Schedule(IDE_PHASE_COMPLETE, end);
    }
    else if (verify)
    {
        u32 available = MIN(count, GetBlocks() - m_state.lba);
        u64 end = GetReadClocks(m_state.lba, available, clocks, k_ide_overhead_clocks);
        SetAddress(m_state.lba + available - (available < count ? 0 : 1));
        m_state.sector_count = (u8)(count - available);
        m_state.result = available < count ? k_ide_error_idnf : 0;
        Schedule(IDE_PHASE_COMPLETE, end);
    }
    else
    {
        m_state.remaining = format ? 1 : count;
        m_state.block = multiple ? m_state.multiple : 1;

        if (read)
            StartRead(clocks);
        else
            Schedule(IDE_PHASE_WRITE_READY, clocks + k_ide_block_clocks);
    }

    return true;
}

void IdeDisk::Complete(u64 clocks, u8 error)
{
    m_state.result = error;
    Schedule(IDE_PHASE_COMPLETE, clocks + k_ide_overhead_clocks);
}

void IdeDisk::Finish(u8 error)
{
    m_state.phase = IDE_PHASE_IDLE;
    m_state.position = 0;
    m_state.length = 0;
    m_state.remaining = 0;
    m_state.error = error;
    m_state.status = (u8)(k_ide_status_drdy | k_ide_status_dsc | (error != 0 ? k_ide_status_err : 0));
    m_state.interrupt = true;
    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_IDE, TRACE_IDE_END)))
        Trace(TRACE_IDE_END, m_state.command);
}

// Bit 6 of Device/Head picks LBA or CHS. A seek or a format only needs a track, so any sector will do
bool IdeDisk::GetAddress(bool track, u32* lba) const
{
    u32 cylinder = m_state.cylinder_low | ((u32)m_state.cylinder_high << 8);

    if ((m_state.device_head & k_ide_head_lba) != 0)
    {
        *lba = m_state.sector_number | (cylinder << 8) | ((u32)(m_state.device_head & 0x0F) << 24);
        return *lba < GetBlocks();
    }

    u32 head = m_state.device_head & 0x0F;
    u32 sector = track ? 1 : m_state.sector_number;

    if (sector == 0 || sector > m_state.current_sectors || head >= m_state.current_heads)
        return false;

    u64 address = (((u64)cylinder * m_state.current_heads + head) * m_state.current_sectors) + sector - 1;
    *lba = (u32)address;
    return address < GetBlocks();
}

// A host can switch to CHS between blocks with no translation set, which leaves only the LBA to show
void IdeDisk::SetAddress(u32 lba)
{
    if ((m_state.device_head & k_ide_head_lba) != 0 || m_state.current_sectors == 0)
    {
        m_state.sector_number = (u8)lba;
        m_state.cylinder_low = (u8)(lba >> 8);
        m_state.cylinder_high = (u8)(lba >> 16);
        m_state.device_head = (u8)((m_state.device_head & 0xF0) | ((lba >> 24) & 0x0F));
        return;
    }

    u32 track = lba / m_state.current_sectors;
    u32 cylinder = track / m_state.current_heads;
    m_state.sector_number = (u8)((lba % m_state.current_sectors) + 1);
    m_state.cylinder_low = (u8)cylinder;
    m_state.cylinder_high = (u8)(cylinder >> 8);
    m_state.device_head = (u8)((m_state.device_head & 0xF0) | (track % m_state.current_heads));
}

void IdeDisk::StartRead(u64 clocks)
{
    u32 blocks = GetBlocks();
    u32 count = MIN(m_state.remaining, m_state.block);
    u32 available = m_state.lba < blocks ? MIN(count, blocks - m_state.lba) : 0;
    u64 overhead = m_state.positioned ? k_ide_block_clocks : k_ide_overhead_clocks;
    m_state.positioned = true;
    m_state.status = k_ide_status_bsy | k_ide_status_drdy | k_ide_status_dsc;

    if (available == 0)
    {
        SetAddress(m_state.lba);
        m_state.sector_count = (u8)m_state.remaining;
        m_state.result = k_ide_error_idnf;
        Schedule(IDE_PHASE_COMPLETE, clocks + overhead);
        return;
    }

    Schedule(IDE_PHASE_READ, GetReadClocks(m_state.lba, available, clocks, overhead));
}

void IdeDisk::LoadBlock()
{
    if (m_state.command == k_ide_command_identify)
    {
        BuildIdentify();
        m_state.length = k_hard_disk_block_size;
    }
    else
    {
        u32 count = MIN(MIN(m_state.remaining, m_state.block), GetBlocks() - m_state.lba);

        for (u32 i = 0; i < count; i++)
        {
            if (!m_image->ReadBlock(m_state.lba + i, m_state.buffer + (i * k_hard_disk_block_size)))
            {
                SetAddress(m_state.lba + i);
                m_state.sector_count = (u8)(m_state.remaining - i);
                Finish(k_ide_error_unc);
                return;
            }
        }

        m_access_count += count;
        m_state.lba += count;
        m_state.remaining -= count;
        m_state.length = count * k_hard_disk_block_size;
        SetAddress(m_state.lba - 1);
        m_state.sector_count = (u8)m_state.remaining;
    }

    m_state.phase = IDE_PHASE_DATA_IN;
    m_state.position = 0;
    m_state.status = k_ide_status_drdy | k_ide_status_dsc | k_ide_status_drq;
    m_state.interrupt = true;
}

// With more to go BSY comes back, otherwise DRQ drops and that's the end, with no interrupt
void IdeDisk::EndDataIn(u64 clocks)
{
    if (m_state.remaining != 0)
    {
        StartRead(clocks);
        return;
    }

    m_state.phase = IDE_PHASE_IDLE;
    m_state.length = 0;
    m_state.position = 0;
    m_state.status = k_ide_status_drdy | k_ide_status_dsc;
    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_IDE, TRACE_IDE_END)))
        Trace(TRACE_IDE_END, m_state.command);
}

// DRQ comes first, as the AT BIOS only polls for it briefly, so the overhead is paid here
// The first sector of a command has to be found, the following ones go at media rate
void IdeDisk::StartWrite(u64 clocks)
{
    bool format = m_state.command == k_ide_command_format;
    u64 count = format ? m_state.current_sectors : m_state.length / k_hard_disk_block_size;
    u64 start = m_state.positioned ? clocks : GetPositionClocks(m_state.lba, clocks + k_ide_overhead_clocks);
    m_state.positioned = true;
    m_state.status = k_ide_status_bsy | k_ide_status_drdy | k_ide_status_dsc;
    m_state.ahead_end = m_state.ahead_lba;
    Schedule(format ? IDE_PHASE_FORMAT : IDE_PHASE_WRITE, start + (count * k_ide_sector_clocks));
}

void IdeDisk::StoreBlock()
{
    u32 blocks = GetBlocks();
    u32 count = m_state.length / k_hard_disk_block_size;

    for (u32 i = 0; i < count; i++)
    {
        u8 error = m_state.lba + i >= blocks ? k_ide_error_idnf :
            !m_image->WriteBlock(m_state.lba + i, m_state.buffer + (i * k_hard_disk_block_size)) ? k_ide_error_abrt : 0;

        if (error != 0)
        {
            SetAddress(m_state.lba + i);
            m_state.sector_count = (u8)(m_state.remaining - i);
            Finish(error);
            return;
        }
    }

    m_state.lba += count;
    m_state.remaining -= count;
    SetAddress(m_state.lba - 1);
    m_state.sector_count = (u8)m_state.remaining;

    if (m_state.remaining == 0)
    {
        Finish(0);
        return;
    }

    m_state.phase = IDE_PHASE_DATA_OUT;
    m_state.position = 0;
    m_state.length = MIN(m_state.remaining, m_state.block) * k_hard_disk_block_size;
    m_state.status = k_ide_status_drdy | k_ide_status_dsc | k_ide_status_drq;
    m_state.interrupt = true;
}

// The sector table isn't kept, an image has nothing to mark. Data fields are left at zero
void IdeDisk::FormatTrack()
{
    u32 count = MIN(m_state.current_sectors, GetBlocks() - m_state.lba);
    u8 error = 0;

    memset(m_state.buffer, 0, k_hard_disk_block_size);

    for (u32 i = 0; i < count && error == 0; i++)
    {
        if (!m_image->WriteBlock(m_state.lba + i, m_state.buffer))
            error = k_ide_error_abrt;
    }

    Finish(error);
}

void IdeDisk::RunEvent()
{
    m_state.event_clocks = GDOX_NO_EVENT;

    switch (m_state.phase)
    {
        case IDE_PHASE_READ:
            LoadBlock();
            break;
        case IDE_PHASE_WRITE_READY:
            m_state.phase = IDE_PHASE_DATA_OUT;
            m_state.position = 0;
            m_state.length = m_state.command == k_ide_command_format ? k_hard_disk_block_size :
                MIN(m_state.remaining, m_state.block) * k_hard_disk_block_size;
            m_state.status = k_ide_status_drdy | k_ide_status_dsc | k_ide_status_drq;
            break;
        case IDE_PHASE_WRITE:
            StoreBlock();
            break;
        case IDE_PHASE_FORMAT:
            FormatTrack();
            break;
        case IDE_PHASE_COMPLETE:
            Finish(m_state.result);
            break;
        case IDE_PHASE_DIAGNOSTIC:
            SetDefaults(false);
            m_state.interrupt = (m_id & 1) == 0;
            break;
        case IDE_PHASE_RESET:
            SetDefaults(true);
            break;
        default:
            break;
    }
}

bool IdeDisk::ReadData(u16& value, u64 clocks)
{
    if (m_state.phase != IDE_PHASE_DATA_IN)
        return false;

    value = read_u16_le(m_state.buffer + m_state.position);
    m_state.position += 2;

    if (m_state.position < m_state.length)
        return false;

    EndDataIn(clocks);
    return true;
}

bool IdeDisk::WriteData(u16 value, u64 clocks)
{
    if (m_state.phase != IDE_PHASE_DATA_OUT)
        return false;

    write_u16_le(m_state.buffer + m_state.position, value);
    m_state.position += 2;

    if (m_state.position < m_state.length)
        return false;

    StartWrite(clocks);
    return true;
}

void IdeDisk::HoldReset()
{
    if (!IsAttached())
        return;

    m_state.phase = IDE_PHASE_RESET_HELD;
    m_state.event_clocks = GDOX_NO_EVENT;
    m_state.position = 0;
    m_state.length = 0;
    m_state.remaining = 0;
    m_state.interrupt = false;
    m_state.status = k_ide_status_bsy;

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_IDE, TRACE_IDE_RESET)))
        Trace(TRACE_IDE_RESET, m_state.command);
}

void IdeDisk::ReleaseReset(u64 clocks)
{
    if (IsAttached())
        Schedule(IDE_PHASE_RESET, clocks + k_ide_reset_clocks);

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_IDE, TRACE_IDE_RESET_RELEASE)) && IsAttached())
        Trace(TRACE_IDE_RESET_RELEASE, m_state.command);
}

// Strings have their first character in the high byte of each word
void IdeDisk::BuildIdentify()
{
    static const char serial[] = "GDX00000000000000001";
    static const char revision[] = "1.0     ";
    static const char model[] = "GEARDOX HARD DISK                       ";

    u16 words[256] = { };
    u32 blocks = GetBlocks();
    u32 capacity = m_state.current_cylinders * m_state.current_heads * m_state.current_sectors;

    words[0] = 0x0040;
    words[1] = (u16)m_state.cylinders;
    words[3] = (u16)m_state.heads;
    words[4] = (u16)(k_hard_disk_block_size * m_state.sectors);
    words[5] = k_hard_disk_block_size;
    words[6] = (u16)m_state.sectors;

    ide_pack_string(words + 10, serial, 10);

    words[20] = 3;
    words[21] = 64;
    words[22] = 4;

    ide_pack_string(words + 23, revision, 4);
    ide_pack_string(words + 27, model, 20);

    words[47] = 0x8000 | IDE_DISK_MAX_BLOCK;
    words[49] = 0x0200;
    words[51] = 0x0200;
    words[53] = 0x0001;
    words[54] = (u16)m_state.current_cylinders;
    words[55] = (u16)m_state.current_heads;
    words[56] = (u16)m_state.current_sectors;
    words[57] = (u16)capacity;
    words[58] = (u16)(capacity >> 16);
    words[59] = (u16)(m_state.multiple != 0 ? 0x0100 | m_state.multiple : 0);
    words[60] = (u16)blocks;
    words[61] = (u16)(blocks >> 16);

    for (int i = 0; i < 256; i++)
        write_u16_le(m_state.buffer + (i * 2), words[i]);
}

// Seeks and rotational waits take a fraction of their time with fast drives, transfers keep the media rate
void IdeDisk::SetDelayDivider(u32 divider)
{
    m_delay_divider = MAX(divider, 1U);
}

// The stroke is the image's own, so a small disk seeks like a big one
u64 IdeDisk::GetSeekClocks(u32 cylinder) const
{
    u32 head = m_state.head_cylinder;
    u32 distance = cylinder > head ? cylinder - head : head - cylinder;

    if (distance == 0)
        return 0;

    u32 scaled = (u32)(((u64)distance * k_ide_seek_points * 256) / MAX(m_state.cylinders, 1U));
    u32 point = MIN(scaled >> 8, k_ide_seek_points - 1);
    u32 fraction = MIN(scaled - (point << 8), 256U);
    u32 us = k_ide_track_seek_us + k_ide_seek_us[point] + (((k_ide_seek_us[point + 1] - k_ide_seek_us[point]) * fraction) >> 8);

    return (((u64)us * GDOX_MACHINE_CLOCK_RATE) / 1000000) / m_delay_divider;
}

// Seek, then wait for the sector to come round. Its angle is its LBA within a track of the outer zone
u64 IdeDisk::GetPositionClocks(u32 lba, u64 clocks)
{
    u32 cylinder = lba / (m_state.heads * m_state.sectors);
    u64 seeked = clocks + GetSeekClocks(cylinder);
    u64 angle = ((lba % k_ide_track_sectors) * k_ide_turn_clocks) / k_ide_track_sectors;
    m_state.head_cylinder = cylinder;

    return seeked + (((angle + k_ide_turn_clocks - (seeked % k_ide_turn_clocks)) % k_ide_turn_clocks) / m_delay_divider);
}

// Sectors of the last read and the ones after it keep arriving at media rate. Anything else is a miss
u64 IdeDisk::GetReadClocks(u32 lba, u32 count, u64 clocks, u64 overhead)
{
    u64 start = clocks + overhead;
    u64 ready;

    if (lba >= m_state.ahead_lba && (lba + count) <= m_state.ahead_end)
    {
        m_state.ahead_clocks += (lba - m_state.ahead_lba) * k_ide_sector_clocks;
        ready = MAX(start, m_state.ahead_clocks + (count * k_ide_sector_clocks));
        m_state.head_cylinder = lba / (m_state.heads * m_state.sectors);
    }
    else
    {
        m_state.ahead_clocks = GetPositionClocks(lba, start);
        ready = m_state.ahead_clocks + (count * k_ide_sector_clocks);
    }

    m_state.ahead_lba = lba;
    m_state.ahead_end = lba + count + k_ide_ahead_sectors;
    return ready;
}

void IdeDisk::Trace(u8 event, u8 command)
{
    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_IDE, event);
    entry->ide.id = (u8)m_id;
    entry->ide.command = command;
    entry->ide.status = m_state.status;
    entry->ide.error = m_state.error;
    entry->ide.count = m_state.sector_count;
    entry->ide.sector = m_state.sector_number;
    entry->ide.head = m_state.device_head;
    entry->ide.cylinder = (u16)(m_state.cylinder_low | (m_state.cylinder_high << 8));
    entry->ide.features = m_trace_features;
    entry->ide.lba_mode = (m_state.device_head & 0x40) != 0 ? 1 : 0;
}

void IdeDisk::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer, GDOX_SAVESTATE_VERSION);
}

void IdeDisk::LoadState(std::istream& stream, int version)
{
    StateSerializer serializer(stream);
    Serialize(serializer, version);
    SanitizeState();
}

void IdeDisk::Serialize(StateSerializer& serializer, int version)
{
    UNUSED(version);
    G_SERIALIZE(serializer, m_state.status);
    G_SERIALIZE(serializer, m_state.error);
    G_SERIALIZE(serializer, m_state.sector_count);
    G_SERIALIZE(serializer, m_state.sector_number);
    G_SERIALIZE(serializer, m_state.cylinder_low);
    G_SERIALIZE(serializer, m_state.cylinder_high);
    G_SERIALIZE(serializer, m_state.device_head);
    G_SERIALIZE(serializer, m_state.interrupt);
    G_SERIALIZE(serializer, m_state.command);
    G_SERIALIZE(serializer, m_state.phase);
    G_SERIALIZE(serializer, m_state.result);
    G_SERIALIZE(serializer, m_state.positioned);
    G_SERIALIZE(serializer, m_state.lba);
    G_SERIALIZE(serializer, m_state.remaining);
    G_SERIALIZE(serializer, m_state.block);
    G_SERIALIZE(serializer, m_state.position);
    G_SERIALIZE(serializer, m_state.length);
    G_SERIALIZE(serializer, m_state.event_clocks);
    G_SERIALIZE(serializer, m_state.cylinders);
    G_SERIALIZE(serializer, m_state.heads);
    G_SERIALIZE(serializer, m_state.sectors);
    G_SERIALIZE(serializer, m_state.current_cylinders);
    G_SERIALIZE(serializer, m_state.current_heads);
    G_SERIALIZE(serializer, m_state.current_sectors);
    G_SERIALIZE(serializer, m_state.multiple);
    G_SERIALIZE(serializer, m_state.head_cylinder);
    G_SERIALIZE(serializer, m_state.ahead_lba);
    G_SERIALIZE(serializer, m_state.ahead_end);
    G_SERIALIZE(serializer, m_state.ahead_clocks);
    G_SERIALIZE_ARRAY(serializer, m_state.buffer, IDE_DISK_BUFFER_SIZE);
}

void IdeDisk::SanitizeState()
{
    m_state.length = MIN(m_state.length, (u32)IDE_DISK_BUFFER_SIZE);
    m_state.position = MIN(m_state.position, m_state.length);
    m_state.block = CLAMP(m_state.block, 1U, (u32)IDE_DISK_MAX_BLOCK);
    m_state.multiple = MIN(m_state.multiple, (u32)IDE_DISK_MAX_BLOCK);

    if (m_state.phase >= IDE_PHASE_COUNT)
    {
        m_state.phase = IDE_PHASE_IDLE;
        m_state.event_clocks = GDOX_NO_EVENT;
    }

    if (IsAttached() && (m_state.heads == 0 || m_state.sectors == 0))
        DetectGeometry();
}
