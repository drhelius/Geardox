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

#include <math.h>
#include "ide_cdrom.h"
#include "ide_common.h"
#include "ide_disk.h"
#include "../cdrom/cdrom_media.h"
#include "../cdrom/cdrom_audio.h"
#include "../audio/audio.h"
#include "../common/trace_logger.h"
#include "../common/state_serializer.h"

// A 1995 4x drive with the Toshiba XM-5302B's figures. The sources give no command time, 50 us is a choice
static const u64 k_cdrom_packet_clocks = ((u64)GDOX_MACHINE_CLOCK_RATE * 10) / 1000000;
static const u64 k_cdrom_command_clocks = ((u64)GDOX_MACHINE_CLOCK_RATE * 50) / 1000000;
static const u64 k_cdrom_block_clocks = ((u64)GDOX_MACHINE_CLOCK_RATE * 5) / 1000000;
static const u64 k_cdrom_reset_clocks = (u64)GDOX_MACHINE_CLOCK_RATE / 1000;
static const u64 k_cdrom_spin_clocks = ((u64)GDOX_MACHINE_CLOCK_RATE * 9) / 10;
static const u32 k_cdrom_ahead_sectors = 128;
static const u32 k_cdrom_buffer_kb = 256;
static const u32 k_cdrom_sector_size = 2048;
static const u32 k_cdrom_raw_size = 2352;
static const u32 k_cdrom_seek_points = 16;

// Toshiba prints 40 ms of rotational delay where waiting for the sector averages 26 ms, the rest is taken as settling
static const u32 k_cdrom_settle_us = 14000;

// 321 ms x (distance / stroke) ^ 0.69: 210 and 450 ms of the XM-1102B at a third of the stroke and at all of it,
// scaled so a third of the stroke takes the XM-5302B's 150 ms
static const u32 k_cdrom_seek_us[k_cdrom_seek_points + 1] =
{
    0, 46962, 75960, 100634, 122862, 143432, 162771, 181142, 198724, 215644, 231996, 247854, 263276, 278308, 292991,
    307355, 321429
};

// ECMA-130: the spiral starts at a 25 mm radius with a 1.6 um pitch, read at 1.2 to 1.4 m/s at 1x
static const double k_cdrom_inner_radius = 0.025;
static const double k_cdrom_track_pitch = 1.6e-6;
static const double k_cdrom_velocity = 1.3;
static const double k_cdrom_stroke_tracks = 21250.0;
static const double k_cdrom_pi = 3.14159265358979323846;

static double cdrom_radius(u32 lba)
{
    double seconds = (double)lba / 75.0;
    return sqrt((k_cdrom_inner_radius * k_cdrom_inner_radius) +
        ((k_cdrom_track_pitch * k_cdrom_velocity * seconds) / k_cdrom_pi));
}

static double cdrom_tracks(u32 lba)
{
    return (cdrom_radius(lba) - k_cdrom_inner_radius) / k_cdrom_track_pitch;
}

IdeCdRom::IdeCdRom()
{
    InitPointer(m_cdrom_media);
    InitPointer(m_cdrom_audio);
    InitPointer(m_audio);
    m_trace_logger = TraceLogger::GetDisabled();
    m_trace_features = 0;
    memset(&m_state, 0, sizeof(m_state));
    m_state.event_clocks = GDOX_NO_EVENT;
    m_fitted = false;
    m_delay_divider = 1;
    m_id = 0;
    m_speed = 4;
    m_access_count = 0;
}

IdeCdRom::~IdeCdRom()
{
}

void IdeCdRom::Init(int id, CdRomMedia* cdrom_media, CdRomAudio* cdrom_audio, Audio* audio)
{
    m_id = id;
    m_cdrom_media = cdrom_media;
    m_cdrom_audio = cdrom_audio;
    m_audio = audio;
    Reset();
}

void IdeCdRom::SetTraceLogger(TraceLogger* trace_logger)
{
    m_trace_logger = IsValidPointer(trace_logger) ? trace_logger : TraceLogger::GetDisabled();
}

void IdeCdRom::SetFitted(bool fitted, int id, int speed)
{
    m_fitted = fitted;
    m_id = id;
    m_speed = MAX(speed, 1);
}

// Seeks, settling, rotational waits and spin-ups take a fraction of their time with fast drives, sectors keep their rate
void IdeCdRom::SetDelayDivider(u32 divider)
{
    m_delay_divider = MAX(divider, 1U);
}

// A hardware reset finds the disc ready, with the power on unit attention waiting
void IdeCdRom::Reset()
{
    memset(&m_state, 0, sizeof(m_state));
    m_state.event_clocks = GDOX_NO_EVENT;
    m_state.seek_clocks = GDOX_NO_EVENT;
    m_state.motor_on = true;
    m_state.audio_status = IDE_CDROM_AUDIO_NONE;
    m_state.unit_attention = 0x29;
    SetDefaultPages();
    SetSignature();
    UpdateGains();
}

void IdeCdRom::SetSignature()
{
    m_state.phase = IDE_CDROM_PHASE_IDLE;
    m_state.event_clocks = GDOX_NO_EVENT;
    m_state.position = 0;
    m_state.length = 0;
    m_state.block_start = 0;
    m_state.block_length = 0;
    m_state.remaining = 0;
    m_state.reason = 0x01;
    m_state.sector_number = 0x01;
    m_state.byte_count_low = 0x14;
    m_state.byte_count_high = 0xEB;
    m_state.device_head &= 0x10;
    m_state.error = 0x01;
    m_state.status = 0;
    m_state.drdy = false;
    m_state.interrupt = false;
}

// SFF-8020i's defaults: both ports at full volume. SFF doesn't print the channel bytes, these are Toshiba's
void IdeCdRom::SetDefaultPages()
{
    m_state.sotc = false;
    m_state.port_channel[0] = 0x01;
    m_state.port_channel[1] = 0x02;
    m_state.port_volume[0] = 0xFF;
    m_state.port_volume[1] = 0xFF;
}

bool IdeCdRom::HasDisc()
{
    return IsValidPointer(m_cdrom_media) && m_cdrom_media->IsReady();
}

bool IdeCdRom::IsReady(u64 clocks)
{
    return !m_state.tray_open && HasDisc() && (!m_state.spinning_up || clocks >= m_state.ready_clocks);
}

u8 IdeCdRom::ReadRegister(int offset) const
{
    switch (offset)
    {
        case 0:
            return m_state.phase == IDE_CDROM_PHASE_DATA_IN ? m_state.buffer[m_state.block_start + m_state.position] : 0xFF;
        case 1:
            return m_state.error;
        case 2:
            return m_state.reason;
        case 3:
            return m_state.sector_number;
        case 4:
            return m_state.byte_count_low;
        case 5:
            return m_state.byte_count_high;
        default:
            return m_state.device_head;
    }
}

void IdeCdRom::WriteRegister(int offset, u8 value)
{
    switch (offset)
    {
        case 2:
            m_state.reason = value;
            break;
        case 3:
            m_state.sector_number = value;
            break;
        case 4:
            m_state.byte_count_low = value;
            break;
        case 5:
            m_state.byte_count_high = value;
            break;
        default:
            m_state.device_head = value;
            break;
    }
}

void IdeCdRom::Schedule(u8 phase, u64 clocks)
{
    m_state.phase = phase;
    m_state.event_clocks = clocks;
}

void IdeCdRom::SetSense(u8 key, u8 asc, u8 ascq, bool ili)
{
    m_state.sense_key = key;
    m_state.sense_asc = asc;
    m_state.sense_ascq = ascq;
    m_state.sense_ili = ili;
    m_state.sense_valid = false;
    m_state.sense_info = 0;
}

void IdeCdRom::SetSenseInfo(u32 info)
{
    m_state.sense_valid = true;
    m_state.sense_info = info;
}

// DSC stays clear while an immediate PLAY or SEEK is still moving the pickup
u8 IdeCdRom::GetReadyBits() const
{
    return (u8)(k_ide_status_drdy | (m_state.seek_clocks == GDOX_NO_EVENT ? k_ide_status_dsc : 0));
}

void IdeCdRom::Complete(u64 clocks)
{
    Schedule(IDE_CDROM_PHASE_COMPLETE, clocks + k_cdrom_command_clocks);
}

void IdeCdRom::Fail(u64 clocks, u8 key, u8 asc, u8 ascq, bool ili)
{
    SetSense(key, asc, ascq, ili);
    m_state.check = true;
    Complete(clocks);
}

void IdeCdRom::Abort(u64 clocks)
{
    m_state.result = k_ide_error_abrt;
    m_state.check = true;
    m_state.packet_run = false;
    Complete(clocks);
}

// The status phase of a packet carries its sense key in Error, an ATA command only ABRT
void IdeCdRom::Finish()
{
    m_state.phase = IDE_CDROM_PHASE_IDLE;
    m_state.event_clocks = GDOX_NO_EVENT;
    m_state.position = 0;
    m_state.length = 0;
    m_state.block_start = 0;
    m_state.block_length = 0;
    m_state.remaining = 0;
    m_state.media_read = false;

    if (m_state.packet_run)
    {
        m_state.error = m_state.check ? (u8)((m_state.sense_key << 4) | (m_state.sense_ili ? 0x01 : 0) | m_state.result) : 0;
        m_state.reason = k_ide_cdrom_reason_cod | k_ide_cdrom_reason_io;
    }
    else
        m_state.error = m_state.result;

    m_state.status = (u8)((m_state.drdy ? GetReadyBits() : 0) | (m_state.check ? k_ide_cdrom_status_chk : 0));
    m_state.interrupt = true;
    if (unlikely(m_trace_logger->IsEnabled(TRACE_CDROM)))
        Trace(TRACE_CDROM_COMPLETE);
}

// Every ATA command but PACKET and SERVICE stops a play (SFF-8020i 2.6 Table 74)
void IdeCdRom::Execute(u8 command, u8 features, u64 clocks)
{
    if (!m_fitted)
        return;

    m_state.command = command;
    m_state.interrupt = false;
    m_state.position = 0;
    m_state.length = 0;
    m_state.block_start = 0;
    m_state.block_length = 0;
    m_state.remaining = 0;
    m_state.ata_data = false;
    m_state.read_cd = false;
    m_state.read_stop = false;
    m_state.media_read = false;
    m_state.packet_run = false;
    m_state.check = false;
    m_state.result = 0;
    m_state.event_clocks = GDOX_NO_EVENT;
    m_state.status = k_ide_status_bsy;
    if (unlikely(m_trace_logger->IsEnabled(TRACE_CDROM)))
        Trace(TRACE_CDROM_COMMAND);

    if ((command != k_ide_cdrom_command_packet) && (command != k_ide_cdrom_command_service))
        StopPlay(clocks);

    switch (command)
    {
        case 0x08:
            Schedule(IDE_CDROM_PHASE_RESET, clocks + k_cdrom_reset_clocks);
            break;
        case k_ide_command_diagnostic:
            Schedule(IDE_CDROM_PHASE_DIAGNOSTIC, clocks + k_cdrom_reset_clocks);
            break;
        case k_ide_cdrom_command_packet:
            StartPacket(features, clocks);
            break;
        case k_ide_cdrom_command_identify:
            m_state.drdy = true;
            Schedule(IDE_CDROM_PHASE_IDENTIFY, clocks + k_cdrom_command_clocks);
            break;
        case 0xE5:
            m_state.reason = 0xFF;
            Complete(clocks);
            break;
        case 0xE0:
        case 0xE1:
        case 0xE6:
            Complete(clocks);
            break;
        case 0xEF:
            if ((features != 0x03) && (features != 0x66) && (features != 0xCC))
                Abort(clocks);
            else
                Complete(clocks);
            break;
        case k_ide_command_identify:
        case 0x20:
        case 0x21:
        {
            bool drdy = m_state.drdy;
            u8 device = m_state.device_head;
            SetSignature();
            m_state.device_head = device;
            m_state.drdy = drdy;
            m_state.status = k_ide_status_bsy;
            Abort(clocks);
            break;
        }
        default:
            Abort(clocks);
            break;
    }
}

// An odd limit counts as one less, and the largest is FFFEh. Lite-On's 1994 driver leaves it at 0, taken as the largest,
// since ATA-4's later abort for it would fail that driver
void IdeCdRom::StartPacket(u8 features, u64 clocks)
{
    m_trace_features = features;

    if ((features & 0x03) != 0)
    {
        Abort(clocks);
        return;
    }

    u32 limit = m_state.byte_count_low | ((u32)m_state.byte_count_high << 8);
    m_state.limit = limit == 0 ? 0xFFFEU : MAX(MIN(limit, 0xFFFEU) & ~1U, 2U);
    m_state.drdy = true;
    Schedule(IDE_CDROM_PHASE_PACKET_WAIT, clocks + k_cdrom_packet_clocks);
}

void IdeCdRom::RunEvent()
{
    if (m_state.seek_clocks <= m_state.event_clocks)
    {
        m_state.seek_clocks = GDOX_NO_EVENT;

        if (((m_state.status & k_ide_status_bsy) == 0) && m_state.drdy)
            m_state.status |= k_ide_status_dsc;

        return;
    }

    u64 clocks = m_state.event_clocks;
    m_state.event_clocks = GDOX_NO_EVENT;

    switch (m_state.phase)
    {
        case IDE_CDROM_PHASE_PACKET_WAIT:
            m_state.phase = IDE_CDROM_PHASE_PACKET;
            m_state.position = 0;
            m_state.reason = k_ide_cdrom_reason_cod;
            m_state.status = GetReadyBits() | k_ide_status_drq;
            break;
        case IDE_CDROM_PHASE_OUT_WAIT:
            StartOutBlock();
            break;
        case IDE_CDROM_PHASE_IDENTIFY:
            BuildIdentify();
            m_state.ata_data = true;
            m_state.length = 512;
            m_state.block_start = 0;
            m_state.byte_count_low = 0x00;
            m_state.byte_count_high = 0x02;
            StartBlock();
            break;
        case IDE_CDROM_PHASE_REPLY:
            StartBlock();
            break;
        case IDE_CDROM_PHASE_READ:
            StageSectors(clocks);
            break;
        case IDE_CDROM_PHASE_COMPLETE:
            Finish();
            break;
        case IDE_CDROM_PHASE_RESET:
            SetSignature();
            break;
        case IDE_CDROM_PHASE_DIAGNOSTIC:
            SetSignature();
            m_state.interrupt = (m_id & 1) == 0;
            break;
        default:
            break;
    }
}

// Each block is a DRQ with its byte count and an interrupt. IDENTIFY PACKET DEVICE sets 512 there before its block,
// which no document asks for but Lite-On's driver reads to size the transfer
void IdeCdRom::StartBlock()
{
    u32 left = m_state.length - m_state.block_start;
    m_state.block_length = m_state.ata_data ? left : MIN(left, m_state.limit);
    m_state.position = 0;
    m_state.phase = IDE_CDROM_PHASE_DATA_IN;

    if (!m_state.ata_data)
    {
        m_state.byte_count_low = (u8)m_state.block_length;
        m_state.byte_count_high = (u8)(m_state.block_length >> 8);
        m_state.reason = k_ide_cdrom_reason_io;
    }

    m_state.status = GetReadyBits() | k_ide_status_drq;
    m_state.interrupt = true;
}

// MODE SELECT's data goes out in blocks of the limit, each after a moment of BSY
void IdeCdRom::StartOutBlock()
{
    m_state.block_length = MIN(m_state.length - m_state.block_start, m_state.limit);
    m_state.position = 0;
    m_state.phase = IDE_CDROM_PHASE_DATA_OUT;
    m_state.byte_count_low = (u8)m_state.block_length;
    m_state.byte_count_high = (u8)(m_state.block_length >> 8);
    m_state.reason = 0;
    m_state.status = GetReadyBits() | k_ide_status_drq;
    m_state.interrupt = true;
}

// Reading a block's last word sets BSY at once, SeaBIOS reads Alternate Status right after it
void IdeCdRom::NextBlock(u64 clocks)
{
    if (m_state.ata_data)
    {
        EndDataIn(clocks);
        return;
    }

    m_state.status = k_ide_status_bsy;

    if (m_state.block_start < m_state.length)
        Schedule(IDE_CDROM_PHASE_REPLY, clocks + k_cdrom_block_clocks);
    else if (m_state.remaining != 0)
        Schedule(IDE_CDROM_PHASE_READ, MAX(clocks + k_cdrom_block_clocks, GetArrivalClocks(m_state.lba)));
    else
        EndRead(clocks);
}

// READ CD of any sector type stops where the disc changes between data and audio, and the sector there is an error
void IdeCdRom::EndRead(u64 clocks)
{
    if (m_state.read_stop)
    {
        m_state.read_stop = false;
        SetSense(k_ide_cdrom_sense_illegal, 0x64, 0);
        SetSenseInfo(m_state.lba);
        m_state.check = true;
    }

    Schedule(IDE_CDROM_PHASE_COMPLETE, clocks + k_cdrom_block_clocks);
}

// An ATA data-in command ends when its last word is read, with no interrupt
void IdeCdRom::EndDataIn(u64 clocks)
{
    UNUSED(clocks);
    m_state.phase = IDE_CDROM_PHASE_IDLE;
    m_state.length = 0;
    m_state.position = 0;
    m_state.block_start = 0;
    m_state.block_length = 0;
    m_state.ata_data = false;
    m_state.status = GetReadyBits();
    if (unlikely(m_trace_logger->IsEnabled(TRACE_CDROM)))
        Trace(TRACE_CDROM_COMPLETE);
}

bool IdeCdRom::ReadData(u16& value, u64 clocks)
{
    if (m_state.phase != IDE_CDROM_PHASE_DATA_IN)
        return false;

    u32 offset = m_state.block_start + m_state.position;
    value = offset + 1 < IDE_CDROM_BUFFER_SIZE ? read_u16_le(m_state.buffer + offset) : m_state.buffer[offset];
    m_state.position += 2;

    if (m_state.position < m_state.block_length)
        return false;

    m_state.block_start += m_state.block_length;
    NextBlock(clocks);
    return true;
}

bool IdeCdRom::WriteData(u16 value, u64 clocks)
{
    if (m_state.phase == IDE_CDROM_PHASE_PACKET)
    {
        m_state.packet[m_state.position] = (u8)value;
        m_state.packet[m_state.position + 1] = (u8)(value >> 8);
        m_state.position += 2;

        if (m_state.position < IDE_CDROM_PACKET_SIZE)
            return false;

        m_state.status = k_ide_status_bsy;
        m_state.phase = IDE_CDROM_PHASE_IDLE;
        ExecutePacket(clocks);
        return true;
    }

    if (m_state.phase != IDE_CDROM_PHASE_DATA_OUT)
        return false;

    u32 offset = m_state.block_start + m_state.position;

    if (offset + 1 < IDE_CDROM_BUFFER_SIZE)
    {
        m_state.buffer[offset] = (u8)value;
        m_state.buffer[offset + 1] = (u8)(value >> 8);
    }

    m_state.position += 2;

    if (m_state.position < m_state.block_length)
        return false;

    m_state.block_start += m_state.block_length;
    m_state.status = k_ide_status_bsy;

    if (m_state.block_start < m_state.length)
        Schedule(IDE_CDROM_PHASE_OUT_WAIT, clocks + k_cdrom_block_clocks);
    else
        ModeSelect(clocks);

    return true;
}

// SRST leaves modes and a play alone, aborts what's at DRQ and puts the signature back
void IdeCdRom::HoldReset()
{
    if (!m_fitted)
        return;

    m_state.phase = IDE_CDROM_PHASE_RESET_HELD;
    m_state.event_clocks = GDOX_NO_EVENT;
    m_state.position = 0;
    m_state.length = 0;
    m_state.block_start = 0;
    m_state.block_length = 0;
    m_state.remaining = 0;
    m_state.interrupt = false;
    m_state.status = k_ide_status_bsy;
}

void IdeCdRom::ReleaseReset(u64 clocks)
{
    if (m_fitted)
        Schedule(IDE_CDROM_PHASE_RESET, clocks + k_cdrom_reset_clocks);

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_CDROM, TRACE_CDROM_RESET)) && m_fitted)
    {
        GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_CDROM, TRACE_CDROM_RESET);
        entry->cdrom.flags = (u8)m_id;
    }
}

// A unit attention goes to the first command that can report it, which clears it. UDVD2 never asks for sense
// REQUEST SENSE reports it as its sense data, as SCSI-2 and Toshiba have it. Lite-On's driver waits for that
// A disc that just went in is becoming ready first, and reports its change once it is
void IdeCdRom::ExecutePacket(u64 clocks)
{
    u8 op = m_state.packet[0];
    m_state.packet_run = true;
    if (unlikely(m_trace_logger->IsEnabled(TRACE_CDROM)))
        Trace(TRACE_CDROM_PACKET);
    SyncAudio(clocks);

    bool spinning = m_state.spinning_up && (clocks < m_state.ready_clocks);

    if (op != 0x03)
        SetSense(k_ide_cdrom_sense_none, 0, 0);
    else if ((m_state.unit_attention != 0) && !spinning)
    {
        SetSense(k_ide_cdrom_sense_attention, m_state.unit_attention, 0);
        m_state.unit_attention = 0;
    }

    if ((m_state.unit_attention != 0) && !spinning && (op != 0x03) && (op != 0x12))
    {
        u8 asc = m_state.unit_attention;
        m_state.unit_attention = 0;
        Fail(clocks, k_ide_cdrom_sense_attention, asc, 0);
        return;
    }

    switch (op)
    {
        case 0x00:
            if (CheckReady(clocks))
                Complete(clocks);
            break;
        case 0x03:
            CommandRequestSense(clocks);
            break;
        case 0x12:
            CommandInquiry(clocks);
            break;
        case 0x1B:
            CommandStartStop(clocks);
            break;
        case 0x1E:
            m_state.prevent = (m_state.packet[4] & 0x01) != 0;
            Complete(clocks);
            break;
        case 0x25:
            CommandReadCapacity(clocks);
            break;
        case 0x28:
            CommandRead(read_u32_be(m_state.packet + 2), read_u16_be(m_state.packet + 7), clocks);
            break;
        case 0xA8:
            CommandRead(read_u32_be(m_state.packet + 2), read_u32_be(m_state.packet + 6), clocks);
            break;
        case 0x2B:
            CommandSeek(clocks);
            break;
        case 0x42:
            CommandReadSubChannel(clocks);
            break;
        case 0x43:
            CommandReadTOC(clocks);
            break;
        case 0x44:
            CommandReadHeader(clocks);
            break;
        case 0x45:
        case 0xA5:
        {
            u32 start = read_u32_be(m_state.packet + 2);
            u32 length = op == 0x45 ? read_u16_be(m_state.packet + 7) : read_u32_be(m_state.packet + 6);
            CommandPlay(start == 0xFFFFFFFF, (s64)(s32)start, length, true, clocks);
            break;
        }
        case 0x47:
        {
            const u8* p = m_state.packet;
            bool current = (p[3] == 0xFF) && (p[4] == 0xFF) && (p[5] == 0xFF);
            GDOX_CdRomMSF from = { p[3], p[4], p[5] };
            GDOX_CdRomMSF to = { p[6], p[7], p[8] };
            s64 start = (s64)MsfToLba(&from) - k_cdrom_lead_in;
            s64 end = (s64)MsfToLba(&to) - k_cdrom_lead_in;
            CommandPlay(current, start, MAX(end, (s64)0), false, clocks);
            break;
        }
        case 0x48:
            CommandPlayTrack(clocks);
            break;
        case 0x4B:
            CommandPauseResume(clocks);
            break;
        case 0x4E:
            StopPlay(clocks);
            if (CheckReady(clocks))
                Complete(clocks);
            break;
        case 0x55:
        {
            u32 length = read_u16_be(m_state.packet + 7);

            if (length == 0)
            {
                Complete(clocks);
                break;
            }

            m_state.length = MIN(length, (u32)IDE_CDROM_BUFFER_SIZE);
            m_state.block_start = 0;
            Schedule(IDE_CDROM_PHASE_OUT_WAIT, clocks + k_cdrom_command_clocks);
            break;
        }
        case 0x5A:
            CommandModeSense(clocks);
            break;
        case 0xB9:
        {
            const u8* p = m_state.packet;
            GDOX_CdRomMSF from = { p[3], p[4], p[5] };
            GDOX_CdRomMSF to = { p[6], p[7], p[8] };
            u32 start = MsfToLba(&from);
            u32 end = MsfToLba(&to);
            start = start >= k_cdrom_lead_in ? start - k_cdrom_lead_in : 0;
            end = end >= k_cdrom_lead_in ? end - k_cdrom_lead_in : 0;

            if (end < start)
            {
                Fail(clocks, k_ide_cdrom_sense_illegal, 0x24, 0);
                break;
            }

            CommandReadCD(start, end - start, (p[1] >> 2) & 0x07, p[9], p[10] & 0x07, clocks);
            break;
        }
        case 0xBE:
        {
            const u8* p = m_state.packet;
            u32 count = ((u32)p[6] << 16) | ((u32)p[7] << 8) | p[8];
            CommandReadCD(read_u32_be(p + 2), count, (p[1] >> 2) & 0x07, p[9], p[10] & 0x07, clocks);
            break;
        }
        case 0xBB:
            StopPlay(clocks);
            Complete(clocks);
            break;
        case 0xBD:
            CommandMechanismStatus(clocks);
            break;
        default:
            Fail(clocks, k_ide_cdrom_sense_illegal, 0x20, 0);
            break;
    }
}

// A disc that just went in reads as becoming ready until it has spun up
bool IdeCdRom::CheckReady(u64 clocks)
{
    if (m_state.tray_open || !HasDisc())
    {
        Fail(clocks, k_ide_cdrom_sense_not_ready, 0x3A, 0);
        return false;
    }

    if (m_state.spinning_up)
    {
        if (clocks < m_state.ready_clocks)
        {
            Fail(clocks, k_ide_cdrom_sense_not_ready, 0x04, 0x01);
            return false;
        }

        m_state.spinning_up = false;
        if (unlikely(m_trace_logger->IsEventEnabled(TRACE_CDROM, TRACE_CDROM_MEDIA)))
            if (unlikely(m_trace_logger->IsEventEnabled(TRACE_CDROM, TRACE_CDROM_MEDIA)))
        TraceMedia(TRACE_CDROM_MEDIA_READY);
    }

    return true;
}

void IdeCdRom::Reply(u32 length, u32 allocation, u64 clocks)
{
    length = MIN(length, allocation);

    if (length == 0)
    {
        Complete(clocks);
        return;
    }

    m_state.length = length;
    m_state.block_start = 0;
    Schedule(IDE_CDROM_PHASE_REPLY, clocks + k_cdrom_command_clocks);
}

// With no error REQUEST SENSE gives the audio status in ASCQ, and it doesn't use up a completed play
void IdeCdRom::CommandRequestSense(u64 clocks)
{
    u8* b = m_state.buffer;
    memset(b, 0, 18);
    b[0] = (u8)(0x70 | (m_state.sense_valid ? 0x80 : 0));
    b[2] = (u8)(m_state.sense_key | (m_state.sense_ili ? 0x20 : 0));
    write_u32_be(b + 3, m_state.sense_info);
    b[7] = 10;
    b[12] = m_state.sense_asc;
    b[13] = m_state.sense_ascq;

    if ((m_state.sense_key == 0) && (m_state.sense_asc == 0))
    {
        u8 audio = PeekAudioStatus();
        b[13] = audio != IDE_CDROM_AUDIO_NONE ? audio : 0;
    }

    Reply(18, m_state.packet[4], clocks);
}

void IdeCdRom::CommandInquiry(u64 clocks)
{
    static const char vendor[] = "GEARDOX ";
    static const char product[] = "CD-ROM          ";
    static const char revision[] = "1.00";
    u8* b = m_state.buffer;
    memset(b, 0, 36);
    b[0] = 0x05;
    b[1] = 0x80;
    b[3] = 0x21;
    b[4] = 31;
    memcpy(b + 8, vendor, 8);
    memcpy(b + 16, product, 16);
    memcpy(b + 32, revision, 4);
    Reply(36, m_state.packet[4], clocks);
}

// Software eject obeys the prevent bit, a close spins up a disc it finds
void IdeCdRom::CommandStartStop(u64 clocks)
{
    bool immediate = (m_state.packet[1] & 0x01) != 0;
    bool start = (m_state.packet[4] & 0x01) != 0;
    bool load = (m_state.packet[4] & 0x02) != 0;
    StopPlay(clocks);

    if (load && !start)
    {
        if (m_state.prevent)
        {
            Fail(clocks, k_ide_cdrom_sense_not_ready, 0x53, 0x02);
            return;
        }

        m_state.tray_open = true;
        m_state.spinning_up = false;
        m_state.ahead_end = m_state.ahead_lba;
        if (unlikely(m_trace_logger->IsEventEnabled(TRACE_CDROM, TRACE_CDROM_MEDIA)))
            if (unlikely(m_trace_logger->IsEventEnabled(TRACE_CDROM, TRACE_CDROM_MEDIA)))
        TraceMedia(TRACE_CDROM_MEDIA_TRAY_OPEN);
        Complete(clocks);
        return;
    }

    if (load)
    {
        CloseTray(clocks);
        Schedule(IDE_CDROM_PHASE_COMPLETE, (immediate || !m_state.spinning_up) ? clocks + k_cdrom_command_clocks :
            m_state.ready_clocks);
        return;
    }

    if (!start)
    {
        m_state.motor_on = false;
        m_state.ahead_end = m_state.ahead_lba;
        if (unlikely(m_trace_logger->IsEventEnabled(TRACE_CDROM, TRACE_CDROM_MEDIA)))
            if (unlikely(m_trace_logger->IsEventEnabled(TRACE_CDROM, TRACE_CDROM_MEDIA)))
        TraceMedia(TRACE_CDROM_MEDIA_STOP);
        Complete(clocks);
        return;
    }

    if (!CheckReady(clocks))
        return;

    u64 ready = GetSpinClocks(clocks);
    Schedule(IDE_CDROM_PHASE_COMPLETE, immediate ? clocks + k_cdrom_command_clocks : ready + k_cdrom_command_clocks);
}

void IdeCdRom::CommandReadCapacity(u64 clocks)
{
    if (!CheckReady(clocks))
        return;

    write_u32_be(m_state.buffer, GetLeadOut() - 1);
    write_u32_be(m_state.buffer + 4, k_cdrom_sector_size);
    Reply(8, 8, clocks);
}

void IdeCdRom::CommandRead(u32 lba, u32 count, u64 clocks)
{
    StopPlay(clocks);

    if (!CheckReady(clocks))
        return;

    if (count == 0)
    {
        Complete(clocks);
        return;
    }

    u32 lead_out = GetLeadOut();

    if ((lba >= lead_out) || (count > lead_out - lba))
    {
        Fail(clocks, k_ide_cdrom_sense_illegal, 0x21, 0);
        SetSenseInfo(MAX(lba, lead_out));
        return;
    }

    if (GetDataEnd(lba) < lba + count)
    {
        Fail(clocks, k_ide_cdrom_sense_illegal, 0x64, 0, true);
        SetSenseInfo(GetDataEnd(lba));
        return;
    }

    m_state.read_cd = false;
    CommandReadCD(lba, count, 0xFF, 0, 0, clocks);
}

// Sectors of the read-ahead window keep arriving at media rate, anything else seeks and waits for the sector to pass
void IdeCdRom::CommandReadCD(u32 lba, u32 count, u8 type, u8 flags, u8 sub, u64 clocks)
{
    bool cooked = type == 0xFF;

    if (!cooked)
    {
        StopPlay(clocks);

        if (!CheckReady(clocks))
            return;

        u32 lead_out = GetLeadOut();

        if (((flags >> 1) & 0x03) == 0x03 || (sub != 0 && sub != 0x02))
        {
            Fail(clocks, k_ide_cdrom_sense_illegal, 0x24, 0);
            return;
        }

        if ((lba >= lead_out) || (count > lead_out - lba))
        {
            Fail(clocks, k_ide_cdrom_sense_illegal, 0x21, 0);
            SetSenseInfo(MAX(lba, lead_out));
            return;
        }

        if (count == 0)
        {
            Complete(clocks);
            return;
        }

        bool first_audio = false;
        m_state.read_stop = false;

        for (u32 next = lba; next < lba + count;)
        {
            s32 track = GetTrackIndex(next);
            bool audio = (track >= 0) && (m_cdrom_media->GetTrackType((u8)track) == GDOX_CDROM_AUDIO_TRACK);
            bool mode2 = (track >= 0) && TrackTypeIsMode2(m_cdrom_media->GetTrackType((u8)track));

            if (next == lba)
                first_audio = audio;

            if ((type == 3) || (type == 5) || (type >= 6) || (type == 1 && !audio) || (type == 2 && (audio || mode2)) ||
                (type == 4 && !mode2))
            {
                Fail(clocks, k_ide_cdrom_sense_illegal, 0x64, 0);
                SetSenseInfo(next);
                return;
            }

            if ((type == 0) && (audio != first_audio))
            {
                count = next - lba;
                m_state.read_stop = true;
                break;
            }

            next = track >= 0 ? m_cdrom_media->GetTracks()[(size_t)track].end_lba + 1 : next + 1;
        }

        m_state.read_cd = true;
        m_state.read_flags = flags;
        m_state.read_type = type;
        m_state.read_sub = sub;

        if (GetSectorBytes(lba) == 0 && flags == 0 && sub == 0)
        {
            Complete(clocks);
            return;
        }
    }

    u32 lead_out = GetLeadOut();
    u64 start = GetSpinClocks(clocks) + k_cdrom_command_clocks;
    bool hit = (lba >= m_state.ahead_lba) && (lba + count <= m_state.ahead_end) && (m_state.ahead_end > m_state.ahead_lba);

    if (hit)
    {
        m_state.ahead_clocks += (u64)(lba - m_state.ahead_lba) * GetSectorClocks();
        m_state.ahead_lba = lba;
    }
    else
    {
        m_state.ahead_clocks = GetPositionClocks(lba, start);
        m_state.ahead_lba = lba;
    }

    m_state.ahead_end = MIN(lba + count + k_cdrom_ahead_sectors, lead_out);
    m_state.lba = lba;
    m_state.remaining = count;
    m_state.media_read = true;
    m_access_count++;
    Schedule(IDE_CDROM_PHASE_READ, MAX(start, GetArrivalClocks(lba)));
}

// A block holds the whole sectors that have arrived and fit the limit, or one sector in pieces when it doesn't fit
// A sector the READ CD fields leave empty takes no room
void IdeCdRom::StageSectors(u64 clocks)
{
    u32 size = MAX(GetSectorBytes(m_state.lba), 1U);
    u32 capacity = m_state.limit >= size ? (m_state.limit / size) * size : size;
    capacity = MIN(capacity, (u32)IDE_CDROM_BUFFER_SIZE);
    u32 first = m_state.lba;
    m_state.length = 0;
    m_state.block_start = 0;

    while ((m_state.remaining != 0) && (GetArrivalClocks(m_state.lba) <= clocks))
    {
        size = GetSectorBytes(m_state.lba);

        if ((m_state.length != 0) && (m_state.length + size > capacity))
            break;

        if ((size != 0) && !BuildSector(m_state.lba, m_state.buffer + m_state.length))
        {
            m_state.remaining = 0;
            Fail(clocks, 0x03, 0x11, 0x00);
            return;
        }

        m_state.length += size;
        m_state.lba++;
        m_state.remaining--;
        m_access_count++;
    }

    m_state.head_lba = m_state.lba;

    if (m_state.length == 0)
    {
        if (m_state.remaining == 0)
            EndRead(clocks);
        else
            Schedule(IDE_CDROM_PHASE_READ, GetArrivalClocks(m_state.lba));
        return;
    }

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_CDROM, TRACE_CDROM_DATA)))
        TraceData(first, m_state.lba);

    StartBlock();
}

u32 IdeCdRom::GetSectorBytes(u32 lba) const
{
    if (!m_state.read_cd)
        return k_cdrom_sector_size;

    s32 track = m_cdrom_media->FindTrackFromLBA(lba, true);
    bool audio = (track >= 0) && (m_cdrom_media->GetTrackType((u8)track) == GDOX_CDROM_AUDIO_TRACK);
    bool mode2 = (track >= 0) && TrackTypeIsMode2(m_cdrom_media->GetTrackType((u8)track));
    u8 flags = m_state.read_flags;
    u32 size = 0;

    if (audio)
        size += (flags & 0x10) != 0 ? k_cdrom_raw_size : 0;
    else
    {
        size += (flags & 0x80) != 0 ? 12 : 0;
        size += ((flags >> 5) & 0x03) != 0 && ((flags >> 5) & 0x03) != 0x02 ? 4 : 0;
        size += mode2 && (((flags >> 5) & 0x03) >= 0x02) ? 8 : 0;
        size += (flags & 0x10) != 0 ? k_cdrom_sector_size : 0;
        size += (flags & 0x08) != 0 ? (mode2 ? 280 : 288) : 0;
    }

    u8 errors = (flags >> 1) & 0x03;
    size += errors == 1 ? 294 : (errors == 2 ? 296 : 0);
    size += m_state.read_sub == 0x02 ? 16 : 0;
    return size;
}

// Audio has no sync, header or EDC, only its 2352 bytes. An image has no C2 errors to report
bool IdeCdRom::BuildSector(u32 lba, u8* buffer)
{
    if (!m_state.read_cd)
        return m_cdrom_media->ReadSector(lba, buffer);

    s32 track = m_cdrom_media->FindTrackFromLBA(lba, true);
    bool audio = (track >= 0) && (m_cdrom_media->GetTrackType((u8)track) == GDOX_CDROM_AUDIO_TRACK);
    bool mode2 = (track >= 0) && TrackTypeIsMode2(m_cdrom_media->GetTrackType((u8)track));
    u8 flags = m_state.read_flags;
    u32 size = GetSectorBytes(lba);
    u8* out = buffer;
    memset(buffer, 0, size);

    if (audio)
    {
        if ((flags & 0x10) != 0)
        {
            s16 samples[k_cdrom_raw_size / 2];

            if (!m_cdrom_media->IsPlayableAudioSector(lba) || !m_cdrom_media->ReadSamples(lba, 0, samples, k_cdrom_raw_size / 2))
                memset(samples, 0, sizeof(samples));

            for (u32 i = 0; i < k_cdrom_raw_size / 2; i++)
            {
                out[i * 2] = (u8)samples[i];
                out[(i * 2) + 1] = (u8)((u16)samples[i] >> 8);
            }

            out += k_cdrom_raw_size;
        }
    }
    else
    {
        u8 raw[k_cdrom_raw_size];

        if (!m_cdrom_media->ReadRawSector2352(lba, raw))
            return false;

        u8 headers = (flags >> 5) & 0x03;

        if ((flags & 0x80) != 0)
        {
            memcpy(out, raw, 12);
            out += 12;
        }

        if ((headers == 0x01) || (headers == 0x03))
        {
            memcpy(out, raw + 12, 4);
            out += 4;
        }

        if (mode2 && (headers >= 0x02))
        {
            memcpy(out, raw + 16, 8);
            out += 8;
        }

        u32 data_offset = mode2 ? 24 : 16;

        if ((flags & 0x10) != 0)
        {
            memcpy(out, raw + data_offset, k_cdrom_sector_size);
            out += k_cdrom_sector_size;
        }

        if ((flags & 0x08) != 0)
        {
            u32 ecc_size = mode2 ? 280 : 288;
            memcpy(out, raw + data_offset + k_cdrom_sector_size, ecc_size);
            out += ecc_size;
        }
    }

    u8 errors = (flags >> 1) & 0x03;
    out += errors == 1 ? 294 : (errors == 2 ? 296 : 0);

    if (m_state.read_sub == 0x02)
        BuildQ(lba, out, true);

    return true;
}

void IdeCdRom::CommandSeek(u64 clocks)
{
    StopPlay(clocks);

    if (!CheckReady(clocks))
        return;

    u32 lba = read_u32_be(m_state.packet + 2);

    if (lba >= GetLeadOut())
    {
        Fail(clocks, k_ide_cdrom_sense_illegal, 0x21, 0);
        return;
    }

    u64 arrival = GetPositionClocks(lba, GetSpinClocks(clocks) + k_cdrom_command_clocks);
    m_state.ahead_lba = lba;
    m_state.ahead_clocks = arrival;
    m_state.ahead_end = MIN(lba + k_cdrom_ahead_sectors, GetLeadOut());
    m_state.seek_clocks = arrival;
    m_access_count++;
    Complete(clocks);
}

// A 13h or 14h is reported once here, then the status reads 15h
void IdeCdRom::CommandReadSubChannel(u64 clocks)
{
    bool msf = (m_state.packet[1] & 0x02) != 0;
    bool subq = (m_state.packet[2] & 0x40) != 0;
    u8 format = m_state.packet[3];
    u32 allocation = read_u16_be(m_state.packet + 7);

    if (!CheckReady(clocks))
        return;

    u8* b = m_state.buffer;
    memset(b, 0, 24);
    b[1] = PeekAudioStatus();

    if ((b[1] == IDE_CDROM_AUDIO_COMPLETED) || (b[1] == IDE_CDROM_AUDIO_ERROR))
        m_state.audio_status = IDE_CDROM_AUDIO_NONE;

    u32 length = 4;

    if (subq)
    {
        switch (format)
        {
            case 0x01:
            {
                bool audio = IsValidPointer(m_cdrom_audio) && (m_cdrom_audio->IsPlaying() || m_cdrom_audio->IsPaused());
                u32 lba = audio ? m_cdrom_audio->GetCurrentLBA() : m_state.head_lba;
                s32 track = GetTrackIndex(lba);

                if (track < 0)
                    track = m_cdrom_media->GetTrackCount() - 1;

                const CdRomImage::Track& entry = m_cdrom_media->GetTracks()[(size_t)track];
                s64 relative = (s64)lba - entry.start_lba;
                b[4] = 0x01;
                b[5] = (u8)(0x10 | GetControl(track));
                b[6] = (u8)(track + 1);
                b[7] = relative < 0 ? 0 : 1;
                WriteAddress(b + 8, lba, msf);

                if (msf)
                {
                    GDOX_CdRomMSF time;
                    LbaToMsf((u32)(relative < 0 ? -relative : relative), &time);
                    b[13] = time.minutes;
                    b[14] = time.seconds;
                    b[15] = time.frames;
                }
                else
                    write_u32_be(b + 12, (u32)(s32)relative);

                length = 16;
                break;
            }
            case 0x02:
                b[4] = 0x02;
                length = 24;
                break;
            case 0x03:
            {
                u8 track = m_state.packet[6];

                if ((track == 0) || (track > m_cdrom_media->GetTrackCount()))
                {
                    Fail(clocks, k_ide_cdrom_sense_illegal, 0x24, 0);
                    return;
                }

                b[4] = 0x03;
                b[5] = (u8)(0x30 | GetControl(track - 1));
                b[6] = track;
                length = 24;
                break;
            }
            default:
                Fail(clocks, k_ide_cdrom_sense_illegal, 0x24, 0);
                return;
        }
    }

    write_u16_be(b + 2, (u16)(length - 4));
    Reply(length, allocation, clocks);
}

// The format is in byte 2, or for SFF-8020 1.2 drivers in byte 9 when byte 2 is zero
void IdeCdRom::CommandReadTOC(u64 clocks)
{
    bool msf = (m_state.packet[1] & 0x02) != 0;
    u8 format = m_state.packet[2] & 0x0F;
    u8 first = m_state.packet[6];
    u32 allocation = read_u16_be(m_state.packet + 7);

    if (format == 0)
        format = (m_state.packet[9] >> 6) & 0x03;

    if (!CheckReady(clocks))
        return;

    const std::vector<CdRomImage::Track>& tracks = m_cdrom_media->GetTracks();
    u32 count = (u32)tracks.size();
    u32 lead_out = GetLeadOut();
    u8* b = m_state.buffer;
    u32 length = 4;
    memset(b, 0, 2048);

    switch (format)
    {
        case 0x00:
        {
            if ((first > count) && (first != 0xAA))
            {
                Fail(clocks, k_ide_cdrom_sense_illegal, 0x24, 0);
                return;
            }

            b[2] = 1;
            b[3] = (u8)count;

            for (u32 i = 0; i < count && first != 0xAA; i++)
            {
                if (i + 1 < first)
                    continue;

                b[length + 1] = (u8)(0x10 | GetControl((int)i));
                b[length + 2] = (u8)(i + 1);
                WriteAddress(b + length + 4, tracks[i].start_lba, msf);
                length += 8;
            }

            b[length + 1] = (u8)(0x10 | GetControl((int)count - 1));
            b[length + 2] = 0xAA;
            WriteAddress(b + length + 4, lead_out, msf);
            length += 8;
            break;
        }
        case 0x01:
            b[2] = 1;
            b[3] = 1;
            b[5] = (u8)(0x10 | GetControl(0));
            b[6] = 1;
            WriteAddress(b + 8, tracks[0].start_lba, msf);
            length = 12;
            break;
        case 0x02:
        {
            b[2] = 1;
            b[3] = 1;

            for (u32 i = 0; i < count + 3; i++)
            {
                u8* d = b + length;
                u32 point_lba = 0;
                d[0] = 1;

                if (i == 0)
                {
                    d[1] = (u8)(0x10 | GetControl(0));
                    d[3] = 0xA0;
                    d[8] = 1;
                }
                else if (i == 1)
                {
                    d[1] = (u8)(0x10 | GetControl((int)count - 1));
                    d[3] = 0xA1;
                    d[8] = (u8)count;
                }
                else if (i == 2)
                {
                    d[1] = (u8)(0x10 | GetControl((int)count - 1));
                    d[3] = 0xA2;
                    point_lba = lead_out + k_cdrom_lead_in;
                }
                else
                {
                    d[1] = (u8)(0x10 | GetControl((int)i - 3));
                    d[3] = (u8)(i - 2);
                    point_lba = tracks[i - 3].start_lba + k_cdrom_lead_in;
                }

                if (i >= 2)
                {
                    GDOX_CdRomMSF time;
                    LbaToMsf(point_lba, &time);
                    d[8] = time.minutes;
                    d[9] = time.seconds;
                    d[10] = time.frames;
                }

                length += 11;
            }
            break;
        }
        default:
            Fail(clocks, k_ide_cdrom_sense_illegal, 0x24, 0);
            return;
    }

    write_u16_be(b, (u16)(length - 2));
    Reply(length, allocation, clocks);
}

void IdeCdRom::CommandReadHeader(u64 clocks)
{
    StopPlay(clocks);

    if (!CheckReady(clocks))
        return;

    u32 lba = read_u32_be(m_state.packet + 2);

    if (lba >= GetLeadOut())
    {
        Fail(clocks, k_ide_cdrom_sense_illegal, 0x21, 0);
        return;
    }

    if (GetDataEnd(lba) <= lba)
    {
        Fail(clocks, k_ide_cdrom_sense_illegal, 0x64, 0);
        return;
    }

    memset(m_state.buffer, 0, 8);
    m_state.buffer[0] = 0x01;
    WriteAddress(m_state.buffer + 4, lba, (m_state.packet[1] & 0x02) != 0);
    Reply(8, read_u16_be(m_state.packet + 7), clocks);
}

// With a length the end is the start plus the length, otherwise it's an exclusive LBA
// The play stops at the first data sector or at the lead-out, as Toshiba's drives do, rather than refuse the range
void IdeCdRom::CommandPlay(bool current, s64 start, s64 end, bool length, u64 clocks)
{
    if (length && (end == 0))
    {
        Complete(clocks);
        return;
    }

    bool audio = IsValidPointer(m_cdrom_audio) && (m_cdrom_audio->IsPlaying() || m_cdrom_audio->IsPaused());
    u32 position = audio ? m_cdrom_audio->GetCurrentLBA() : m_state.head_lba;
    StopPlay(clocks);

    if (!CheckReady(clocks))
        return;

    start = current ? (s64)position : MAX(start, (s64)0);

    if (length)
        end += start;

    if (start == end)
    {
        Complete(clocks);
        return;
    }

    if (start > end)
    {
        Fail(clocks, k_ide_cdrom_sense_illegal, 0x24, 0);
        return;
    }

    u32 lead_out = GetLeadOut();

    if (start >= (s64)lead_out)
    {
        Fail(clocks, k_ide_cdrom_sense_illegal, 0x21, 0);
        return;
    }

    s32 track = GetTrackIndex((u32)start);

    if ((track < 0) || (m_cdrom_media->GetTrackType((u8)track) != GDOX_CDROM_AUDIO_TRACK))
    {
        Fail(clocks, k_ide_cdrom_sense_illegal, 0x64, 0);
        return;
    }

    u32 stop = (u32)MIN(end, (s64)lead_out);

    if (m_state.sotc)
        stop = MIN(stop, m_cdrom_media->GetTracks()[(size_t)track].end_lba + 1);

    for (u32 next = (u32)start; next < stop;)
    {
        s32 t = GetTrackIndex(next);

        if ((t < 0) || (m_cdrom_media->GetTrackType((u8)t) != GDOX_CDROM_AUDIO_TRACK))
        {
            stop = next;
            break;
        }

        next = m_cdrom_media->GetTracks()[(size_t)t].end_lba + 1;
    }

    u64 arrival = GetPositionClocks((u32)start, GetSpinClocks(clocks) + k_cdrom_command_clocks);
    m_state.ahead_end = m_state.ahead_lba;
    m_cdrom_audio->Play((u32)start, stop, false, arrival - clocks, 0);
    m_state.seek_clocks = arrival;
    m_state.audio_active = true;
    m_state.audio_status = IDE_CDROM_AUDIO_NONE;
    m_access_count++;
    Complete(clocks);
}

void IdeCdRom::CommandPlayTrack(u64 clocks)
{
    if (!CheckReady(clocks))
        return;

    u32 count = m_cdrom_media->GetTrackCount();
    u8 first = m_state.packet[4];
    u8 last = m_state.packet[7];

    if ((first == 0) || (first > count) || (last < first))
    {
        Fail(clocks, k_ide_cdrom_sense_illegal, 0x24, 0);
        return;
    }

    const std::vector<CdRomImage::Track>& tracks = m_cdrom_media->GetTracks();
    u32 start = tracks[first - 1].start_lba;
    u32 end = last >= count ? GetLeadOut() : tracks[last - 1].end_lba + 1;
    CommandPlay(false, start, end, false, clocks);
}

void IdeCdRom::CommandPauseResume(u64 clocks)
{
    if (!CheckReady(clocks))
        return;

    bool resume = (m_state.packet[8] & 0x01) != 0;
    bool playing = m_cdrom_audio->IsPlaying();
    bool paused = m_cdrom_audio->IsPaused();

    if (!playing && !paused)
    {
        Fail(clocks, k_ide_cdrom_sense_aborted, 0xB9, 0);
        return;
    }

    if (resume && paused)
        m_cdrom_audio->Resume();
    else if (!resume && playing)
        m_cdrom_audio->Pause();

    Complete(clocks);
}

void IdeCdRom::CommandModeSense(u64 clocks)
{
    u8 control = m_state.packet[2] >> 6;
    u8 page = m_state.packet[2] & 0x3F;
    u32 allocation = read_u16_be(m_state.packet + 7);

    if (control == 3)
    {
        Fail(clocks, k_ide_cdrom_sense_illegal, 0x39, 0);
        return;
    }

    if ((page != 0x01) && (page != 0x0D) && (page != 0x0E) && (page != 0x2A) && (page != 0x3F))
    {
        Fail(clocks, k_ide_cdrom_sense_illegal, 0x24, 0);
        return;
    }

    u8* b = m_state.buffer;
    memset(b, 0, 128);

    if (m_state.tray_open)
        b[2] = 0x71;
    else if (!HasDisc())
        b[2] = 0x70;
    else
    {
        bool data = false;
        bool audio = false;

        for (u32 i = 0; i < m_cdrom_media->GetTrackCount(); i++)
        {
            if (m_cdrom_media->GetTrackType((u8)i) == GDOX_CDROM_AUDIO_TRACK)
                audio = true;
            else
                data = true;
        }

        b[2] = (u8)(data && audio ? 0x03 : (audio ? 0x02 : 0x01));
    }

    u32 length = 8;
    static const u8 pages[4] = { 0x01, 0x0D, 0x0E, 0x2A };

    for (int i = 0; i < 4; i++)
    {
        if ((page == 0x3F) || (page == pages[i]))
            length += BuildModePage(pages[i], control, b + length);
    }

    write_u16_be(b, (u16)(length - 2));
    Reply(length, allocation, clocks);
}

// Only page 0Eh's SOTC, channels and volumes can change
u32 IdeCdRom::BuildModePage(u8 page, u8 control, u8* buffer)
{
    bool changeable = control == 1;
    bool defaults = control == 2;

    switch (page)
    {
        case 0x01:
            buffer[0] = 0x01;
            buffer[1] = 0x06;
            return 8;
        case 0x0D:
            buffer[0] = 0x0D;
            buffer[1] = 0x06;

            if (!changeable)
            {
                buffer[5] = 60;
                buffer[7] = 75;
            }
            return 8;
        case 0x0E:
            buffer[0] = 0x0E;
            buffer[1] = 0x0E;

            if (changeable)
            {
                buffer[2] = 0x02;
                buffer[8] = 0x0F;
                buffer[9] = 0xFF;
                buffer[10] = 0x0F;
                buffer[11] = 0xFF;
            }
            else
            {
                buffer[2] = (u8)(0x04 | (!defaults && m_state.sotc ? 0x02 : 0));
                buffer[7] = 75;
                buffer[8] = defaults ? 0x01 : m_state.port_channel[0];
                buffer[9] = defaults ? 0xFF : m_state.port_volume[0];
                buffer[10] = defaults ? 0x02 : m_state.port_channel[1];
                buffer[11] = defaults ? 0xFF : m_state.port_volume[1];
            }
            return 16;
        default:
        {
            buffer[0] = 0x2A;
            buffer[1] = 0x12;

            if (!changeable)
            {
                u16 speed = (u16)(176 * m_speed);
                buffer[4] = 0x01;
                buffer[5] = 0x03;
                buffer[6] = (u8)(0x01 | (m_state.prevent ? 0x02 : 0) | 0x08 | (0x01 << 5));
                buffer[7] = 0x03;
                write_u16_be(buffer + 8, speed);
                write_u16_be(buffer + 10, 256);
                write_u16_be(buffer + 12, (u16)k_cdrom_buffer_kb);
                write_u16_be(buffer + 14, speed);
            }
            return 20;
        }
    }
}

// Every page has to be one the drive has, and page 2Ah can only be written back as it reads (SFF-8020i 2.6, 05/26/00)
void IdeCdRom::ModeSelect(u64 clocks)
{
    const u8* b = m_state.buffer;
    u32 length = m_state.length;
    m_state.length = 0;
    m_state.block_start = 0;

    if (length < 8)
    {
        Fail(clocks, k_ide_cdrom_sense_illegal, 0x1A, 0);
        return;
    }

    u32 start = 8 + read_u16_be(b + 6);

    for (u32 position = start; position + 2 <= length; position += 2 + b[position + 1])
    {
        u8 page = b[position] & 0x3F;
        u32 size = b[position + 1];
        bool bad = position + 2 + size > length;

        if (page == 0x0E)
            bad = bad || (size != 0x0E);
        else if (page == 0x2A)
        {
            u8 current[20] = { };
            BuildModePage(0x2A, 0, current);
            bad = bad || (size != current[1]) || (memcmp(b + position + 2, current + 2, size) != 0);
        }
        else if ((page != 0x01) && (page != 0x0D))
            bad = true;

        if (bad)
        {
            Fail(clocks, k_ide_cdrom_sense_illegal, 0x26, 0);
            return;
        }
    }

    for (u32 position = start; position + 2 <= length; position += 2 + b[position + 1])
    {
        if ((b[position] & 0x3F) != 0x0E)
            continue;

        m_state.sotc = (b[position + 2] & 0x02) != 0;
        m_state.port_channel[0] = b[position + 8] & 0x0F;
        m_state.port_volume[0] = b[position + 9];
        m_state.port_channel[1] = b[position + 10] & 0x0F;
        m_state.port_volume[1] = b[position + 11];
        UpdateGains();
    }

    Complete(clocks);
}

void IdeCdRom::CommandMechanismStatus(u64 clocks)
{
    bool playing = IsValidPointer(m_cdrom_audio) && m_cdrom_audio->IsPlaying();
    u32 lba = playing ? m_cdrom_audio->GetCurrentLBA() : m_state.head_lba;
    u8* b = m_state.buffer;
    memset(b, 0, 8);
    b[1] = (u8)((playing ? 0x20 : 0) | (m_state.tray_open ? 0x10 : 0));
    b[2] = (u8)(lba >> 16);
    b[3] = (u8)(lba >> 8);
    b[4] = (u8)lba;
    Reply(8, read_u16_be(m_state.packet + 8), clocks);
}

// Strings have their first character in the high byte of each word
void IdeCdRom::BuildIdentify()
{
    static const char serial[] = "GDX00000000000000002";
    static const char revision[] = "1.00    ";
    static const char model[] = "GEARDOX CD-ROM                          ";
    u16 words[256] = { };

    words[0] = 0x85C0;

    ide_pack_string(words + 10, serial, 10);

    ide_pack_string(words + 23, revision, 4);
    ide_pack_string(words + 27, model, 20);

    words[49] = 0x0200;
    words[51] = 0x0200;

    for (int i = 0; i < 256; i++)
    {
        m_state.buffer[i * 2] = (u8)words[i];
        m_state.buffer[(i * 2) + 1] = (u8)(words[i] >> 8);
    }
}

// The formatted Q of READ CD, in BCD as it is on the disc. No CRC
void IdeCdRom::BuildQ(u32 lba, u8* q, bool bcd)
{
    s32 track = GetTrackIndex(lba);

    if (track < 0)
        track = m_cdrom_media->GetTrackCount() - 1;

    const CdRomImage::Track& entry = m_cdrom_media->GetTracks()[(size_t)track];
    s64 relative = (s64)lba - entry.start_lba;
    GDOX_CdRomMSF rel;
    GDOX_CdRomMSF abs;
    LbaToMsf((u32)(relative < 0 ? -relative : relative), &rel);
    LbaToMsf(lba + k_cdrom_lead_in, &abs);
    u8 values[9] = { (u8)(track + 1), (u8)(relative < 0 ? 0 : 1), rel.minutes, rel.seconds, rel.frames, 0,
        abs.minutes, abs.seconds, abs.frames };
    q[0] = (u8)((GetControl(track) << 4) | 0x01);

    for (int i = 0; i < 9; i++)
        q[1 + i] = bcd ? DecToBcd(values[i]) : values[i];
}

u32 IdeCdRom::GetLeadOut()
{
    return m_cdrom_media->GetSectorCount();
}

// The first sector from here on that isn't Mode 1 data
u32 IdeCdRom::GetDataEnd(u32 lba)
{
    u32 lead_out = GetLeadOut();

    while (lba < lead_out)
    {
        s32 track = m_cdrom_media->FindTrackFromLBA(lba, false);

        if ((track < 0) || (m_cdrom_media->GetTrackType((u8)track) == GDOX_CDROM_AUDIO_TRACK))
            return lba;

        lba = m_cdrom_media->GetTracks()[(size_t)track].end_lba + 1;
    }

    return lead_out;
}

// A pregap belongs to the track it leads into
s32 IdeCdRom::GetTrackIndex(u32 lba)
{
    return m_cdrom_media->FindTrackFromLBA(lba, true);
}

u8 IdeCdRom::GetControl(int track)
{
    if ((track < 0) || (track >= m_cdrom_media->GetTrackCount()))
        return 0;

    const CdRomImage::Track& entry = m_cdrom_media->GetTracks()[(size_t)track];
    return (u8)((entry.control_flags & 0x0F) | (entry.type != GDOX_CDROM_AUDIO_TRACK ? k_cdrom_control_data : 0));
}

void IdeCdRom::WriteAddress(u8* buffer, u32 lba, bool msf)
{
    if (!msf)
    {
        write_u32_be(buffer, lba);
        return;
    }

    GDOX_CdRomMSF time;
    LbaToMsf(lba + k_cdrom_lead_in, &time);
    buffer[0] = 0;
    buffer[1] = time.minutes;
    buffer[2] = time.seconds;
    buffer[3] = time.frames;
}

// The mixer plays the disc, so it has to catch up before anything about the play is answered
void IdeCdRom::SyncAudio(u64 clocks)
{
    if (!IsValidPointer(m_audio) || !IsValidPointer(m_cdrom_audio))
        return;

    m_audio->Synchronize(clocks);

    if (m_state.audio_active && !m_cdrom_audio->IsPlaying() && !m_cdrom_audio->IsPaused())
    {
        m_state.audio_active = false;
        m_state.audio_status = IDE_CDROM_AUDIO_COMPLETED;
        m_state.head_lba = m_cdrom_audio->GetCurrentLBA();
    }
}

// What a stopped play leaves is 15h, no source gives it and 13h is for a play that completed
void IdeCdRom::StopPlay(u64 clocks)
{
    SyncAudio(clocks);

    if (!IsValidPointer(m_cdrom_audio))
        return;

    if (m_cdrom_audio->IsPlaying() || m_cdrom_audio->IsPaused())
    {
        m_state.head_lba = m_cdrom_audio->GetCurrentLBA();
        m_cdrom_audio->Stop();
        m_state.audio_status = IDE_CDROM_AUDIO_NONE;
    }

    m_state.audio_active = false;
}

u8 IdeCdRom::PeekAudioStatus() const
{
    if (IsValidPointer(m_cdrom_audio))
    {
        if (m_cdrom_audio->IsPlaying())
            return IDE_CDROM_AUDIO_PLAYING;

        if (m_cdrom_audio->IsPaused())
            return IDE_CDROM_AUDIO_PAUSED;

        if (m_state.audio_active)
            return IDE_CDROM_AUDIO_COMPLETED;
    }

    return m_state.audio_status;
}

// Each port takes its channels at volume / 256, both channels mixed at half each
void IdeCdRom::GetAudioGains(s32* gains) const
{
    for (int port = 0; port < 2; port++)
    {
        u8 channels = m_state.port_channel[port] & 0x03;
        s32 volume = m_state.port_volume[port] * 128;

        if (channels == 0x03)
            volume /= 2;

        gains[(port * 2) + 0] = (channels & 0x01) != 0 ? volume : 0;
        gains[(port * 2) + 1] = (channels & 0x02) != 0 ? volume : 0;
    }
}

void IdeCdRom::UpdateGains()
{
    if (!IsValidPointer(m_audio))
        return;

    s32 gains[4];
    GetAudioGains(gains);
    m_audio->SetCDDAGains(gains);
}

// Audio plays at 1x whatever the data speed
u64 IdeCdRom::GetSectorClocks() const
{
    return (u64)GDOX_MACHINE_CLOCK_RATE / (75 * (u64)m_speed);
}

// Seek, then wait for the sector to come round at the speed the spiral turns under the head there
u64 IdeCdRom::GetPositionClocks(u32 lba, u64 clocks)
{
    double from = cdrom_tracks(m_state.head_lba);
    double to = cdrom_tracks(lba);
    double distance = MIN(fabs(to - from) / k_cdrom_stroke_tracks, 1.0);
    u32 scaled = (u32)(distance * k_cdrom_seek_points * 256.0);
    u32 point = MIN(scaled >> 8, k_cdrom_seek_points - 1);
    u32 fraction = MIN(scaled - (point << 8), 256U);
    u32 us = k_cdrom_seek_us[point] + (((k_cdrom_seek_us[point + 1] - k_cdrom_seek_us[point]) * fraction) >> 8);

    if (fabs(to - from) >= 1.0)
        us += k_cdrom_settle_us;
    u64 seeked = clocks + ((((u64)us * GDOX_MACHINE_CLOCK_RATE) / 1000000) / m_delay_divider);
    double turn_seconds = (2.0 * k_cdrom_pi * cdrom_radius(lba)) / (k_cdrom_velocity * m_speed);
    u64 turn = MAX((u64)(turn_seconds * GDOX_MACHINE_CLOCK_RATE), (u64)1);
    u64 angle = (u64)((to - floor(to)) * (double)turn);
    m_state.head_lba = lba;
    return seeked + (((angle + turn - (seeked % turn)) % turn) / m_delay_divider);
}

// A stopped motor spins up on the next access without an error
u64 IdeCdRom::GetSpinClocks(u64 clocks)
{
    if (!m_state.motor_on)
    {
        m_state.motor_on = true;
        m_state.ready_clocks = clocks + (k_cdrom_spin_clocks / m_delay_divider);
    }

    return MAX(clocks, m_state.ready_clocks);
}

u64 IdeCdRom::GetArrivalClocks(u32 lba) const
{
    return m_state.ahead_clocks + ((u64)(lba - m_state.ahead_lba) * GetSectorClocks());
}

// The tray closes on the disc, which spins up and then reports the change once
void IdeCdRom::InsertDisc(u64 clocks)
{
    if (IsValidPointer(m_cdrom_audio))
        m_cdrom_audio->Stop(TRACE_CDDA_STOP_INSERT);

    m_state.audio_active = false;
    m_state.audio_status = IDE_CDROM_AUDIO_NONE;
    m_state.tray_open = false;
    m_state.motor_on = true;
    m_state.spinning_up = true;
    m_state.ready_clocks = clocks + (k_cdrom_spin_clocks / m_delay_divider);
    m_state.unit_attention = 0x28;
    m_state.head_lba = 0;
    m_state.ahead_end = m_state.ahead_lba;
    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_CDROM, TRACE_CDROM_MEDIA)))
        TraceMedia(TRACE_CDROM_MEDIA_INSERT);
}

// A host eject works even when software prevents removal, as the emergency eject hole does
void IdeCdRom::EjectDisc(u64 clocks)
{
    UNUSED(clocks);

    if (IsValidPointer(m_cdrom_audio))
        m_cdrom_audio->Stop(TRACE_CDDA_STOP_EJECT);

    m_state.audio_active = false;
    m_state.audio_status = IDE_CDROM_AUDIO_NONE;
    m_state.tray_open = true;
    m_state.spinning_up = false;
    m_state.ahead_end = m_state.ahead_lba;
    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_CDROM, TRACE_CDROM_MEDIA)))
        TraceMedia(TRACE_CDROM_MEDIA_EJECT);

    if (m_state.media_read && (m_state.phase != IDE_CDROM_PHASE_IDLE) && (m_state.phase != IDE_CDROM_PHASE_COMPLETE))
    {
        SetSense(k_ide_cdrom_sense_not_ready, 0x3A, 0);
        m_state.check = true;
        Finish();
    }
}

void IdeCdRom::CloseTray(u64 clocks)
{
    if (!m_state.tray_open)
        return;

    m_state.tray_open = false;
    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_CDROM, TRACE_CDROM_MEDIA)))
        TraceMedia(TRACE_CDROM_MEDIA_TRAY_CLOSE);

    if (!HasDisc())
        return;

    m_state.motor_on = true;
    m_state.spinning_up = true;
    m_state.ready_clocks = clocks + (k_cdrom_spin_clocks / m_delay_divider);
    m_state.unit_attention = 0x28;
    m_state.head_lba = 0;
}

void IdeCdRom::Trace(u8 event)
{
    u8 command = m_state.packet_run ? m_state.packet[0] : m_state.command;
    bool polling = m_state.packet_run && (command == 0x00 || command == 0x4A || command == 0xBD);

    if (polling && event != TRACE_CDROM_COMMAND)
        event = TRACE_CDROM_POLL;

    if (!m_trace_logger->IsEventEnabled(TRACE_CDROM, event))
        return;

    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_CDROM, event);
    entry->cdrom.flags = (u8)m_id;
    entry->cdrom.command = command;
    entry->cdrom.value = m_state.status;
    entry->cdrom.features = m_trace_features;
    entry->cdrom.limit = (u16)(m_state.byte_count_low | ((u16)m_state.byte_count_high << 8));

    if (event == TRACE_CDROM_PACKET || event == TRACE_CDROM_POLL)
        memcpy(entry->cdrom.bytes, m_state.packet, IDE_CDROM_PACKET_SIZE);

    if (event == TRACE_CDROM_COMPLETE || (event == TRACE_CDROM_POLL && m_state.interrupt))
    {
        entry->cdrom.sense[0] = m_state.check ? m_state.sense_key : 0;
        entry->cdrom.sense[1] = m_state.check ? m_state.sense_asc : 0;
        entry->cdrom.sense[2] = m_state.check ? m_state.sense_ascq : 0;
        entry->cdrom.bytes[3] = m_state.error;
        entry->cdrom.bytes[4] = m_state.packet_run ? 1 : 0;
        entry->cdrom.bytes[5] = 1;

        if (m_state.packet_run && command == 0x03 && !m_state.check)
        {
            entry->cdrom.sense[0] = m_state.buffer[2] & 0x0F;
            entry->cdrom.sense[1] = m_state.buffer[12];
            entry->cdrom.sense[2] = m_state.buffer[13];
            entry->cdrom.bytes[6] = 1;
        }
    }
    else if (event == TRACE_CDROM_COMMAND)
        entry->cdrom.bytes[0] = m_state.reason;
}

void IdeCdRom::TraceData(u32 first, u32 end)
{
    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_CDROM, TRACE_CDROM_DATA);
    entry->cdrom.flags = (u8)m_id;
    entry->cdrom.command = m_state.packet[0];
    entry->cdrom.lba = first;
    entry->cdrom.end_lba = end;
    entry->cdrom.size = m_state.length;
}

void IdeCdRom::TraceMedia(u8 kind)
{
    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_CDROM, TRACE_CDROM_MEDIA);
    entry->cdrom.flags = (u8)m_id;
    entry->cdrom.value = kind;
    entry->cdrom.lba = m_state.head_lba;
}

void IdeCdRom::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer, GDOX_SAVESTATE_VERSION);
}

void IdeCdRom::LoadState(std::istream& stream, int version)
{
    StateSerializer serializer(stream);
    Serialize(serializer, version);
    SanitizeState();
}

void IdeCdRom::Serialize(StateSerializer& serializer, int version)
{
    UNUSED(version);
    G_SERIALIZE(serializer, m_state.status);
    G_SERIALIZE(serializer, m_state.error);
    G_SERIALIZE(serializer, m_state.reason);
    G_SERIALIZE(serializer, m_state.sector_number);
    G_SERIALIZE(serializer, m_state.byte_count_low);
    G_SERIALIZE(serializer, m_state.byte_count_high);
    G_SERIALIZE(serializer, m_state.device_head);
    G_SERIALIZE(serializer, m_state.interrupt);
    G_SERIALIZE(serializer, m_state.drdy);
    G_SERIALIZE(serializer, m_state.command);
    G_SERIALIZE(serializer, m_state.phase);
    G_SERIALIZE_ARRAY(serializer, m_state.packet, IDE_CDROM_PACKET_SIZE);
    G_SERIALIZE(serializer, m_state.limit);
    G_SERIALIZE(serializer, m_state.position);
    G_SERIALIZE(serializer, m_state.block_start);
    G_SERIALIZE(serializer, m_state.block_length);
    G_SERIALIZE(serializer, m_state.length);
    G_SERIALIZE(serializer, m_state.lba);
    G_SERIALIZE(serializer, m_state.remaining);
    G_SERIALIZE(serializer, m_state.read_flags);
    G_SERIALIZE(serializer, m_state.read_type);
    G_SERIALIZE(serializer, m_state.read_sub);
    G_SERIALIZE(serializer, m_state.read_cd);
    G_SERIALIZE(serializer, m_state.read_stop);
    G_SERIALIZE(serializer, m_state.media_read);
    G_SERIALIZE(serializer, m_state.ata_data);
    G_SERIALIZE(serializer, m_state.packet_run);
    G_SERIALIZE(serializer, m_state.check);
    G_SERIALIZE(serializer, m_state.result);
    G_SERIALIZE(serializer, m_state.event_clocks);
    G_SERIALIZE(serializer, m_state.sense_key);
    G_SERIALIZE(serializer, m_state.sense_asc);
    G_SERIALIZE(serializer, m_state.sense_ascq);
    G_SERIALIZE(serializer, m_state.sense_ili);
    G_SERIALIZE(serializer, m_state.sense_valid);
    G_SERIALIZE(serializer, m_state.sense_info);
    G_SERIALIZE(serializer, m_state.unit_attention);
    G_SERIALIZE(serializer, m_state.tray_open);
    G_SERIALIZE(serializer, m_state.motor_on);
    G_SERIALIZE(serializer, m_state.spinning_up);
    G_SERIALIZE(serializer, m_state.ready_clocks);
    G_SERIALIZE(serializer, m_state.seek_clocks);
    G_SERIALIZE(serializer, m_state.prevent);
    G_SERIALIZE(serializer, m_state.head_lba);
    G_SERIALIZE(serializer, m_state.ahead_lba);
    G_SERIALIZE(serializer, m_state.ahead_end);
    G_SERIALIZE(serializer, m_state.ahead_clocks);
    G_SERIALIZE(serializer, m_state.audio_active);
    G_SERIALIZE(serializer, m_state.audio_status);
    G_SERIALIZE(serializer, m_state.sotc);
    G_SERIALIZE_ARRAY(serializer, m_state.port_channel, 2);
    G_SERIALIZE_ARRAY(serializer, m_state.port_volume, 2);
    G_SERIALIZE_ARRAY(serializer, m_state.buffer, IDE_CDROM_BUFFER_SIZE);
}

void IdeCdRom::SanitizeState()
{
    m_state.length = MIN(m_state.length, (u32)IDE_CDROM_BUFFER_SIZE);
    m_state.block_start = MIN(m_state.block_start, m_state.length);
    m_state.block_length = MIN(m_state.block_length, m_state.length - m_state.block_start);
    m_state.position = MIN(m_state.position, m_state.block_length);
    m_state.limit = MIN(m_state.limit, 0xFFFEU);
    m_state.ahead_end = MAX(m_state.ahead_end, m_state.ahead_lba);

    if ((m_state.phase == IDE_CDROM_PHASE_PACKET) && (m_state.position > IDE_CDROM_PACKET_SIZE - 2))
        m_state.position = 0;

    if (m_state.phase >= IDE_CDROM_PHASE_COUNT)
    {
        m_state.phase = IDE_CDROM_PHASE_IDLE;
        m_state.event_clocks = GDOX_NO_EVENT;
    }

    UpdateGains();
}
