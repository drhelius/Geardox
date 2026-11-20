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

#ifndef IDE_CDROM_H
#define IDE_CDROM_H

#include <iostream>
#include "../common/common.h"

#define IDE_CDROM_BUFFER_SIZE 65536
#define IDE_CDROM_PACKET_SIZE 12

class Audio;
class CdRomAudio;
class CdRomMedia;
class StateSerializer;
class TraceLogger;

enum IdeCdRom_Phase
{
    IDE_CDROM_PHASE_IDLE = 0,
    IDE_CDROM_PHASE_PACKET_WAIT,
    IDE_CDROM_PHASE_PACKET,
    IDE_CDROM_PHASE_REPLY,
    IDE_CDROM_PHASE_READ,
    IDE_CDROM_PHASE_DATA_IN,
    IDE_CDROM_PHASE_OUT_WAIT,
    IDE_CDROM_PHASE_DATA_OUT,
    IDE_CDROM_PHASE_IDENTIFY,
    IDE_CDROM_PHASE_COMPLETE,
    IDE_CDROM_PHASE_RESET_HELD,
    IDE_CDROM_PHASE_RESET,
    IDE_CDROM_PHASE_DIAGNOSTIC,
    IDE_CDROM_PHASE_COUNT
};

enum IdeCdRom_Audio_Status
{
    IDE_CDROM_AUDIO_INVALID = 0x00,
    IDE_CDROM_AUDIO_PLAYING = 0x11,
    IDE_CDROM_AUDIO_PAUSED = 0x12,
    IDE_CDROM_AUDIO_COMPLETED = 0x13,
    IDE_CDROM_AUDIO_ERROR = 0x14,
    IDE_CDROM_AUDIO_NONE = 0x15
};

class IdeCdRom
{
public:
    struct IdeCdRom_State
    {
        u8 status;
        u8 error;
        u8 reason;
        u8 sector_number;
        u8 byte_count_low;
        u8 byte_count_high;
        u8 device_head;
        bool interrupt;
        bool drdy;
        u8 command;
        u8 phase;
        u8 packet[IDE_CDROM_PACKET_SIZE];
        u32 limit;
        u32 position;
        u32 block_start;
        u32 block_length;
        u32 length;
        u32 lba;
        u32 remaining;
        u8 read_flags;
        u8 read_type;
        u8 read_sub;
        bool read_cd;
        bool read_stop;
        bool media_read;
        bool ata_data;
        bool packet_run;
        bool check;
        u8 result;
        u64 event_clocks;
        u8 sense_key;
        u8 sense_asc;
        u8 sense_ascq;
        bool sense_ili;
        bool sense_valid;
        u32 sense_info;
        u8 unit_attention;
        bool tray_open;
        bool motor_on;
        bool spinning_up;
        u64 ready_clocks;
        u64 seek_clocks;
        bool prevent;
        u32 head_lba;
        u32 ahead_lba;
        u32 ahead_end;
        u64 ahead_clocks;
        bool audio_active;
        u8 audio_status;
        bool sotc;
        u8 port_channel[2];
        u8 port_volume[2];
        u8 buffer[IDE_CDROM_BUFFER_SIZE];
    };

public:
    IdeCdRom();
    ~IdeCdRom();
    void Init(int id, CdRomMedia* cdrom_media, CdRomAudio* cdrom_audio, Audio* audio);
    void SetTraceLogger(TraceLogger* trace_logger);
    void SetFitted(bool fitted, int id, int speed);
    void SetDelayDivider(u32 divider);
    void Reset();
    bool IsAttached() const;
    int GetSpeed() const;
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
    void InsertDisc(u64 clocks);
    void EjectDisc(u64 clocks);
    void CloseTray(u64 clocks);
    bool IsTrayOpen() const;
    bool HasDisc();
    bool IsReady(u64 clocks);
    u8 PeekAudioStatus() const;
    u32 GetHeadLBA() const;
    u32 GetAccessCount() const;
    bool IsBusy() const;
    void GetAudioGains(s32* gains) const;
    IdeCdRom_State* GetState();
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream, int version = GDOX_SAVESTATE_VERSION);

private:
    void SetSignature();
    void SetDefaultPages();
    void Schedule(u8 phase, u64 clocks);
    void SetSense(u8 key, u8 asc, u8 ascq, bool ili = false);
    void SetSenseInfo(u32 info);
    u8 GetReadyBits() const;
    void StartOutBlock();
    void Complete(u64 clocks);
    void Fail(u64 clocks, u8 key, u8 asc, u8 ascq, bool ili = false);
    void Finish();
    void Abort(u64 clocks);
    void StartPacket(u8 features, u64 clocks);
    void ExecutePacket(u64 clocks);
    bool CheckReady(u64 clocks);
    void Reply(u32 length, u32 allocation, u64 clocks);
    void StartBlock();
    void NextBlock(u64 clocks);
    void EndDataIn(u64 clocks);
    void StageSectors(u64 clocks);
    void EndRead(u64 clocks);
    u32 GetSectorBytes(u32 lba) const;
    bool BuildSector(u32 lba, u8* buffer);
    void BuildIdentify();
    void BuildQ(u32 lba, u8* q, bool bcd);
    u32 GetLeadOut();
    u32 GetDataEnd(u32 lba);
    s32 GetTrackIndex(u32 lba);
    u8 GetControl(int track);
    void WriteAddress(u8* buffer, u32 lba, bool msf);
    void CommandRequestSense(u64 clocks);
    void CommandInquiry(u64 clocks);
    void CommandStartStop(u64 clocks);
    void CommandRead(u32 lba, u32 count, u64 clocks);
    void CommandReadCD(u32 lba, u32 count, u8 type, u8 flags, u8 sub, u64 clocks);
    void CommandReadCapacity(u64 clocks);
    void CommandSeek(u64 clocks);
    void CommandReadSubChannel(u64 clocks);
    void CommandReadTOC(u64 clocks);
    void CommandReadHeader(u64 clocks);
    void CommandPlay(bool current, s64 start, s64 end, bool length, u64 clocks);
    void CommandPlayTrack(u64 clocks);
    void CommandPauseResume(u64 clocks);
    void CommandModeSense(u64 clocks);
    void ModeSelect(u64 clocks);
    void CommandMechanismStatus(u64 clocks);
    u32 BuildModePage(u8 page, u8 control, u8* buffer);
    void SyncAudio(u64 clocks);
    void StopPlay(u64 clocks);
    void UpdateGains();
    u64 GetSectorClocks() const;
    u64 GetPositionClocks(u32 lba, u64 clocks);
    u64 GetSpinClocks(u64 clocks);
    u64 GetArrivalClocks(u32 lba) const;
    NO_INLINE void Trace(u8 event);
    NO_INLINE void TraceData(u32 first, u32 end);
    NO_INLINE void TraceMedia(u8 kind);
    void Serialize(StateSerializer& serializer, int version);
    void SanitizeState();

private:
    CdRomMedia* m_cdrom_media;
    CdRomAudio* m_cdrom_audio;
    u8 m_trace_features;
    Audio* m_audio;
    TraceLogger* m_trace_logger;
    IdeCdRom_State m_state;
    bool m_fitted;
    int m_id;
    int m_speed;
    u32 m_access_count;
    u32 m_delay_divider;
};

static const u8 k_ide_cdrom_status_chk = 0x01;
static const u8 k_ide_cdrom_reason_cod = 0x01;
static const u8 k_ide_cdrom_reason_io = 0x02;
static const u8 k_ide_cdrom_command_packet = 0xA0;
static const u8 k_ide_cdrom_command_identify = 0xA1;
static const u8 k_ide_cdrom_command_service = 0xA2;

static const u8 k_ide_cdrom_sense_none = 0x00;
static const u8 k_ide_cdrom_sense_not_ready = 0x02;
static const u8 k_ide_cdrom_sense_illegal = 0x05;
static const u8 k_ide_cdrom_sense_attention = 0x06;
static const u8 k_ide_cdrom_sense_aborted = 0x0B;

INLINE const char* GetCdRomPacketName(u8 opcode)
{
    switch (opcode)
    {
        case 0x00: return "TEST UNIT READY";
        case 0x03: return "REQUEST SENSE";
        case 0x12: return "INQUIRY";
        case 0x1B: return "START STOP UNIT";
        case 0x1E: return "PREVENT ALLOW";
        case 0x25: return "READ CAPACITY";
        case 0x28: return "READ(10)";
        case 0x2B: return "SEEK";
        case 0x42: return "READ SUB-CHANNEL";
        case 0x43: return "READ TOC";
        case 0x44: return "READ HEADER";
        case 0x45: return "PLAY AUDIO(10)";
        case 0x47: return "PLAY AUDIO MSF";
        case 0x48: return "PLAY TRACK INDEX";
        case 0x4B: return "PAUSE RESUME";
        case 0x4E: return "STOP PLAY";
        case 0x55: return "MODE SELECT";
        case 0x5A: return "MODE SENSE";
        case 0xA5: return "PLAY AUDIO(12)";
        case 0xA8: return "READ(12)";
        case 0xB9: return "READ CD MSF";
        case 0xBB: return "SET CD SPEED";
        case 0xBD: return "MECHANISM STATUS";
        case 0xBE: return "READ CD";
        default: return "UNKNOWN";
    }
}

INLINE const char* GetCdRomAtaName(u8 command)
{
    switch (command)
    {
        case 0x08: return "DEVICE RESET";
        case 0x90: return "DIAGNOSTIC";
        case 0xA0: return "PACKET";
        case 0xA1: return "IDENTIFY PACKET";
        case 0xE0: return "STANDBY";
        case 0xE1: return "IDLE";
        case 0xE5: return "CHECK POWER";
        case 0xE6: return "SLEEP";
        case 0xEC: return "IDENTIFY";
        case 0xEF: return "SET FEATURES";
        default: return "ATA";
    }
}

#include "ide_cdrom_inline.h"

#endif /* IDE_CDROM_H */
