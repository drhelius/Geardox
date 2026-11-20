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

#ifndef I82077_H
#define I82077_H

#include <iostream>
#include "../common/common.h"

#define I82077_DRIVES 4
#define I82077_FIFO_SIZE 16
#define I82077_SENSE_SIZE 8
#define I82077_PARAMETERS 9
#define I82077_RESULTS 10
#define I82077_FORMAT_IDS 256

class FDC;
class StateSerializer;
class TraceLogger;
struct FloppyDisk_Sector;

class I82077
{
public:
    enum I82077_Phase
    {
        I82077_PHASE_RESET = 0,
        I82077_PHASE_COMMAND,
        I82077_PHASE_EXECUTION,
        I82077_PHASE_RESULT
    };

    enum I82077_Step
    {
        I82077_STEP_IDLE = 0,
        I82077_STEP_START,
        I82077_STEP_SEEK,
        I82077_STEP_INDEX,
        I82077_STEP_SEARCH,
        I82077_STEP_DATA,
        I82077_STEP_CRC,
        I82077_STEP_FORMAT,
        I82077_STEP_FORMAT_END,
        I82077_STEP_DRAIN
    };

    enum I82077_Command
    {
        I82077_COMMAND_NONE = 0,
        I82077_COMMAND_READ,
        I82077_COMMAND_READ_DELETED,
        I82077_COMMAND_WRITE,
        I82077_COMMAND_WRITE_DELETED,
        I82077_COMMAND_READ_TRACK,
        I82077_COMMAND_VERIFY,
        I82077_COMMAND_FORMAT,
        I82077_COMMAND_READ_ID,
        I82077_COMMAND_RECALIBRATE,
        I82077_COMMAND_SEEK,
        I82077_COMMAND_RELATIVE_SEEK,
        I82077_COMMAND_SENSE_INTERRUPT,
        I82077_COMMAND_SENSE_DRIVE,
        I82077_COMMAND_SPECIFY,
        I82077_COMMAND_CONFIGURE,
        I82077_COMMAND_VERSION,
        I82077_COMMAND_DUMPREG,
        I82077_COMMAND_PERPENDICULAR,
        I82077_COMMAND_LOCK,
        I82077_COMMAND_INVALID
    };

    enum I82077_Seek_Type
    {
        I82077_SEEK_NONE = 0,
        I82077_SEEK_NORMAL,
        I82077_SEEK_RELATIVE,
        I82077_SEEK_RECALIBRATE,
        I82077_SEEK_IMPLIED
    };

    struct I82077_Seek
    {
        u64 clocks;
        u16 steps;
        u8 target;
        u8 type;
        bool inward;
        bool failed;
    };

    struct I82077_State
    {
        u8 dor;
        u8 rate;
        u8 precompensation;
        u8 phase;
        u8 step;
        u8 command;
        u8 parameters[I82077_PARAMETERS];
        u8 parameter_count;
        u8 parameter_length;
        u8 results[I82077_RESULTS];
        u8 result_count;
        u8 result_index;
        u8 data_port;
        u8 pcn[I82077_DRIVES];
        u8 drive_busy;
        u8 srt;
        u8 hut;
        u8 hlt;
        bool non_dma;
        bool implied_seek;
        bool fifo_disabled;
        bool poll_disabled;
        u8 fifo_threshold;
        u8 precompensation_track;
        bool lock;
        u8 perpendicular;
        u8 last_sector_count;
        u8 fifo[I82077_FIFO_SIZE];
        u8 fifo_head;
        u8 fifo_count;
        u8 sense_st0[I82077_SENSE_SIZE];
        u8 sense_pcn[I82077_SENSE_SIZE];
        u8 sense_count;
        I82077_Seek seeks[I82077_DRIVES];
        u64 event_clocks;
        u64 deadline_clocks;
        u64 poll_clocks;
        u64 head_unload_clocks;
        u64 data_clocks;
        bool interrupt;
        bool request;
        bool terminal_count;
        bool abort;
        bool reading;
        bool transferring;
        bool stop_after_sector;
        bool mfm;
        bool multi_track;
        bool skip;
        u8 st0;
        u8 st1;
        u8 st2;
        u8 drive;
        u8 side;
        u8 cylinder;
        u8 head;
        u8 sector;
        u8 size_code;
        u8 last_sector;
        u8 data_length;
        u16 verify_count;
        s16 found;
        u8 track_index;
        u8 id_seen;
        u32 header;
        u32 data_offset;
        u32 data_index;
        u32 sector_bytes;
        u32 transfer_bytes;
        u16 format_index;
        u16 format_count;
        u8 format_filler;
        u8 format_ids[I82077_FORMAT_IDS];
    };

public:
    I82077();
    ~I82077();
    void Init(FDC* fdc);
    void SetTraceLogger(TraceLogger* trace_logger);
    void SetDelayDivider(u32 divider);
    void Reset();
    u8 Read(int reg, u64 clocks);
    u8 Peek(int reg) const;
    void Write(int reg, u8 value, u64 clocks);
    void Run(u64 clocks);
    u64 GetEventClocks() const;
    bool ReadDMA(u16& value);
    bool WriteDMA(u16 value);
    void TerminalCount();
    void Resume(u64 clocks);
    u8 GetDOR() const;
    u8 GetRate() const;
    u8 GetMSR() const;
    bool IsInterruptOut() const;
    bool IsRequestOut() const;
    I82077_State* GetState();
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream);

private:
    void ResetCore();
    void LeaveReset(u64 clocks);
    void WriteDOR(u8 value, u64 clocks);
    void WriteDSR(u8 value, u64 clocks);
    void WriteData(u8 value, u64 clocks);
    u8 ReadData(u64 clocks);
    I82077_Command DecodeCommand(u8 value) const;
    void StartCommand(u64 clocks);
    void StartData(u64 clocks);
    void StartSeek(int drive, I82077_Seek_Type type, u8 target, u16 steps, bool inward, u64 clocks);
    void AnswerSense();
    void AnswerDumpreg();
    void WriteConfigure();
    void SetResults(u8 count);
    void EnterCommandPhase();
    void QueueSense(u8 st0, u8 pcn);
    void RunPoll();
    void RunSeek(int drive, u64 clocks);
    void FinishSeek(int drive, u64 clocks);
    void RunExecution(u64 clocks);
    void RunStart(u64 clocks);
    void BeginWork(u64 clocks);
    void BeginSearch(u64 clocks);
    void RunSearch(u64 clocks);
    void BeginSector(const FloppyDisk_Sector& sector, u32 gap, u64 clocks);
    void RunData(u64 clocks);
    void RunSectorEnd(u64 clocks);
    void RunFormat(u64 clocks);
    void FinishFormat(u64 clocks);
    void Abnormal(u8 st1, u8 st2);
    void AdvanceResult();
    void Finish(u64 clocks);
    void EnterResult(u64 clocks);
    int GetTrackLayout(FloppyDisk_Sector* sectors, u32* id_ends, u32* data_starts, u32* track_bytes) const;
    bool MatchesID(const FloppyDisk_Sector& sector) const;
    bool IsWrite() const;
    bool IsMarkWanted(const FloppyDisk_Sector& sector) const;
    u32 GetFIFOCapacity() const;
    u32 GetThreshold() const;
    bool PushFIFO(u8 value);
    bool PopFIFO(u8& value);
    void UpdateRequest();
    void SetInterrupt(bool active);
    u32 GetRateKbps() const;
    u64 GetStepClocks() const;
    u64 GetHeadLoadClocks() const;
    u64 GetHeadUnloadClocks() const;
    NO_INLINE void TraceDOR(u8 previous);
    NO_INLINE void TraceCommand(u8 first, u8 count);
    NO_INLINE void TraceResults(u8 count);
    NO_INLINE void TraceRate(u8 reg, u8 value);
    NO_INLINE void TraceSector(const FloppyDisk_Sector& sector);
    void Serialize(StateSerializer& serializer);
    void SanitizeState();

private:
    FDC* m_fdc;
    TraceLogger* m_trace_logger;
    bool m_port_logged;
    u32 m_delay_divider;
    I82077_State m_state;
};

static const u8 k_i82077_dor_reset = 0x04;
static const u8 k_i82077_dor_gate = 0x08;
static const u8 k_i82077_msr_rqm = 0x80;
static const u8 k_i82077_msr_dio = 0x40;
static const u8 k_i82077_msr_non_dma = 0x20;
static const u8 k_i82077_msr_busy = 0x10;
static const u8 k_i82077_st0_abnormal = 0x40;
static const u8 k_i82077_st0_invalid = 0x80;
static const u8 k_i82077_st0_polling = 0xC0;
static const u8 k_i82077_st0_seek_end = 0x20;
static const u8 k_i82077_st0_equipment = 0x10;
static const u8 k_i82077_st1_end_of_track = 0x80;
static const u8 k_i82077_st1_data_error = 0x20;
static const u8 k_i82077_st1_overrun = 0x10;
static const u8 k_i82077_st1_no_data = 0x04;
static const u8 k_i82077_st1_not_writable = 0x02;
static const u8 k_i82077_st1_missing_mark = 0x01;
static const u8 k_i82077_st2_control_mark = 0x40;
static const u8 k_i82077_st2_data_crc = 0x20;
static const u8 k_i82077_st2_wrong_cylinder = 0x10;
static const u8 k_i82077_st2_bad_cylinder = 0x02;
static const u8 k_i82077_st2_missing_data = 0x01;
static const u8 k_i82077_st3_write_protect = 0x40;
static const u8 k_i82077_st3_fixed = 0x28;
static const u8 k_i82077_st3_track0 = 0x10;
static const u8 k_i82077_version = 0x90;
static const u8 k_i82077_invalid_result = 0x80;
static const u16 k_i82077_recalibrate_steps = 79;
static const u64 k_i82077_poll_clocks = GDOX_MACHINE_CLOCK_RATE / 4000;
static const u64 k_i82077_command_clocks = ((u64)GDOX_MACHINE_CLOCK_RATE * 12) / 1000000;

#include "i82077_inline.h"

#endif /* I82077_H */
