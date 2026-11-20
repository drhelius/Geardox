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

#include "i82077.h"
#include "fdc.h"
#include "floppy_image.h"
#include "../common/state_serializer.h"
#include "../common/trace_logger.h"

static const u8 k_i82077_lengths[] = { 0, 9, 9, 9, 9, 9, 9, 6, 2, 2, 3, 3, 1, 2, 3, 4, 1, 1, 2, 1, 1 };

I82077::I82077()
{
    InitPointer(m_fdc);
    m_trace_logger = TraceLogger::GetDisabled();
    m_port_logged = false;
    m_delay_divider = 1;
    memset(&m_state, 0, sizeof(m_state));
}

void I82077::SetDelayDivider(u32 divider)
{
    m_delay_divider = divider;
}

I82077::~I82077()
{
}

void I82077::Init(FDC* fdc)
{
    m_fdc = fdc;
}

void I82077::SetTraceLogger(TraceLogger* trace_logger)
{
    m_trace_logger = IsValidPointer(trace_logger) ? trace_logger : TraceLogger::GetDisabled();
}

// The pin reset clears everything but what Specify programmed, and holds the chip until the DOR lets it go
void I82077::Reset()
{
    u8 srt = m_state.srt;
    u8 hut = m_state.hut;
    u8 hlt = m_state.hlt;
    bool non_dma = m_state.non_dma;

    memset(&m_state, 0, sizeof(m_state));
    m_state.srt = srt;
    m_state.hut = hut;
    m_state.hlt = hlt;
    m_state.non_dma = non_dma;
    m_state.rate = 0x02;
    ResetCore();
    m_state.phase = I82077_PHASE_RESET;
}

// Rate, precompensation, Specify and the DOR survive a software reset, Configure only with Lock
void I82077::ResetCore()
{
    if (!m_state.lock)
    {
        m_state.fifo_disabled = true;
        m_state.fifo_threshold = 0;
        m_state.precompensation_track = 0;
    }

    m_state.implied_seek = false;
    m_state.poll_disabled = false;
    m_state.perpendicular &= ~0x03;
    m_state.phase = I82077_PHASE_RESET;
    m_state.step = I82077_STEP_IDLE;
    m_state.command = I82077_COMMAND_NONE;
    m_state.parameter_count = 0;
    m_state.result_count = 0;
    m_state.result_index = 0;
    m_state.drive_busy = 0;
    m_state.fifo_head = 0;
    m_state.fifo_count = 0;
    m_state.sense_count = 0;
    m_state.event_clocks = GDOX_NO_EVENT;
    m_state.deadline_clocks = GDOX_NO_EVENT;
    m_state.poll_clocks = GDOX_NO_EVENT;
    m_state.request = false;
    m_state.interrupt = false;
    m_state.transferring = false;
    memset(m_state.pcn, 0, sizeof(m_state.pcn));

    for (int i = 0; i < I82077_DRIVES; i++)
    {
        m_state.seeks[i].type = I82077_SEEK_NONE;
        m_state.seeks[i].clocks = GDOX_NO_EVENT;
    }
}

void I82077::LeaveReset(u64 clocks)
{
    m_state.phase = I82077_PHASE_COMMAND;
    m_state.poll_clocks = clocks + k_i82077_poll_clocks;
}

u8 I82077::GetMSR() const
{
    u8 value = m_state.drive_busy;

    switch (m_state.phase)
    {
        case I82077_PHASE_COMMAND:
            value |= k_i82077_msr_rqm;

            if (m_state.parameter_count > 0)
                value |= k_i82077_msr_busy;
            break;
        case I82077_PHASE_EXECUTION:
            value |= k_i82077_msr_busy;

            if (m_state.non_dma && m_state.transferring)
            {
                value |= k_i82077_msr_non_dma;

                if (m_state.request)
                    value |= k_i82077_msr_rqm | (m_state.reading ? k_i82077_msr_dio : 0);
            }
            break;
        case I82077_PHASE_RESULT:
            value |= k_i82077_msr_rqm | k_i82077_msr_dio | k_i82077_msr_busy;
            break;
        default:
            break;
    }

    return value;
}

// SRA, SRB and the tape register belong to the other modes, and DIR is the drive's
u8 I82077::Peek(int reg) const
{
    switch (reg)
    {
        case 2:
            return m_state.dor;
        case 4:
            return GetMSR();
        case 5:
            return m_state.phase == I82077_PHASE_RESULT && m_state.result_index < m_state.result_count ?
                m_state.results[m_state.result_index] : m_state.data_port;
        default:
            return 0xFF;
    }
}

u8 I82077::Read(int reg, u64 clocks)
{
    return reg == 5 ? ReadData(clocks) : Peek(reg);
}

void I82077::Write(int reg, u8 value, u64 clocks)
{
    switch (reg)
    {
        case 2:
            WriteDOR(value, clocks);
            break;
        case 4:
            WriteDSR(value, clocks);

            if (unlikely(m_trace_logger->IsEventEnabled(TRACE_FDC, TRACE_FDC_RATE)))
                TraceRate(4, value);
            break;
        case 5:
            WriteData(value, clocks);
            break;
        case 7:
            m_state.rate = value & 0x03;

            if (unlikely(m_trace_logger->IsEventEnabled(TRACE_FDC, TRACE_FDC_RATE)))
                TraceRate(7, value);
            break;
        default:
            break;
    }
}

void I82077::WriteDOR(u8 value, u64 clocks)
{
    u8 previous = m_state.dor;

    m_state.dor = value;

    if ((previous & k_i82077_dor_reset) != 0 && (value & k_i82077_dor_reset) == 0)
        ResetCore();
    else if ((previous & k_i82077_dor_reset) == 0 && (value & k_i82077_dor_reset) != 0)
        LeaveReset(clocks);

    Resume(clocks);

    if (unlikely(m_trace_logger->IsEnabled(TRACE_FDC)))
        TraceDOR(previous);
}

// Bit 7 resets like the DOR and clears itself, the DOR's reset wins while held
void I82077::WriteDSR(u8 value, u64 clocks)
{
    m_state.rate = value & 0x03;
    m_state.precompensation = (value >> 2) & 0x07;

    if ((value & 0x80) != 0 && (m_state.dor & k_i82077_dor_reset) != 0)
    {
        ResetCore();
        LeaveReset(clocks);
    }
}

void I82077::WriteData(u8 value, u64 clocks)
{
    m_state.data_port = value;

    if (m_state.phase == I82077_PHASE_EXECUTION && m_state.non_dma && m_state.transferring && !m_state.reading)
    {
        PushFIFO(value);
        UpdateRequest();
        return;
    }

    if (m_state.phase != I82077_PHASE_COMMAND)
    {
        if (!m_port_logged)
        {
            Debug("FDC: write %02X to the data port in phase %d", value, m_state.phase);
            m_port_logged = true;
        }

        return;
    }

    if (m_state.parameter_count == 0)
    {
        m_state.command = DecodeCommand(value);
        m_state.parameter_length = k_i82077_lengths[m_state.command];
    }

    if (m_state.command == I82077_COMMAND_INVALID)
    {
        if (unlikely(m_trace_logger->IsEventEnabled(TRACE_FDC, TRACE_FDC_COMMAND)))
            TraceCommand(value, 1);

        m_state.results[0] = k_i82077_invalid_result;
        SetResults(1);
        return;
    }

    m_state.parameters[m_state.parameter_count++] = value;

    if (m_state.parameter_count == m_state.parameter_length)
    {
        if (unlikely(m_trace_logger->IsEventEnabled(TRACE_FDC, TRACE_FDC_COMMAND)))
            TraceCommand(m_state.parameters[0], m_state.parameter_length);

        StartCommand(clocks);
    }
}

u8 I82077::ReadData(u64 clocks)
{
    if (m_state.phase == I82077_PHASE_RESULT)
    {
        u8 value = m_state.results[m_state.result_index++];

        // The first result byte of a data command takes the interrupt down
        if (m_state.result_index == 1 && m_state.command <= I82077_COMMAND_READ_ID)
            SetInterrupt(false);

        m_state.data_port = value;

        if (m_state.result_index >= m_state.result_count)
            EnterCommandPhase();

        return value;
    }

    if (m_state.phase == I82077_PHASE_EXECUTION && m_state.non_dma && m_state.transferring && m_state.reading)
    {
        u8 value = m_state.data_port;

        if (PopFIFO(value))
            m_state.data_port = value;

        UpdateRequest();

        if (m_state.step == I82077_STEP_DRAIN && m_state.fifo_count == 0)
            EnterResult(clocks);

        return value;
    }

    if (!m_port_logged)
    {
        Debug("FDC: read of the data port in phase %d", m_state.phase);
        m_port_logged = true;
    }

    return m_state.data_port;
}

// A Sense Interrupt Status answers one queued drive at a time, and a seek's next answer brings the interrupt back
void I82077::EnterCommandPhase()
{
    m_state.phase = I82077_PHASE_COMMAND;
    m_state.parameter_count = 0;

    if (m_state.sense_count > 0 && (m_state.sense_st0[0] & k_i82077_st0_polling) != k_i82077_st0_polling)
        SetInterrupt(true);
}

I82077::I82077_Command I82077::DecodeCommand(u8 value) const
{
    switch (value & 0x1F)
    {
        case 0x06:
            return I82077_COMMAND_READ;
        case 0x0C:
            return I82077_COMMAND_READ_DELETED;
        case 0x16:
            return I82077_COMMAND_VERIFY;
        case 0x05:
            return (value & 0x20) == 0 ? I82077_COMMAND_WRITE : I82077_COMMAND_INVALID;
        case 0x09:
            return (value & 0x20) == 0 ? I82077_COMMAND_WRITE_DELETED : I82077_COMMAND_INVALID;
        default:
            break;
    }

    switch (value & 0xBF)
    {
        case 0x02:
            return I82077_COMMAND_READ_TRACK;
        case 0x0A:
            return I82077_COMMAND_READ_ID;
        case 0x0D:
            return I82077_COMMAND_FORMAT;
        case 0x8F:
            return I82077_COMMAND_RELATIVE_SEEK;
        default:
            break;
    }

    switch (value)
    {
        case 0x03:
            return I82077_COMMAND_SPECIFY;
        case 0x04:
            return I82077_COMMAND_SENSE_DRIVE;
        case 0x07:
            return I82077_COMMAND_RECALIBRATE;
        case 0x08:
            return I82077_COMMAND_SENSE_INTERRUPT;
        case 0x0E:
            return I82077_COMMAND_DUMPREG;
        case 0x0F:
            return I82077_COMMAND_SEEK;
        case 0x10:
            return I82077_COMMAND_VERSION;
        case 0x12:
            return I82077_COMMAND_PERPENDICULAR;
        case 0x13:
            return I82077_COMMAND_CONFIGURE;
        case 0x14:
        case 0x94:
            return I82077_COMMAND_LOCK;
        default:
            return I82077_COMMAND_INVALID;
    }
}

void I82077::SetResults(u8 count)
{
    m_state.phase = I82077_PHASE_RESULT;
    m_state.result_count = count;
    m_state.result_index = 0;

    if (unlikely(m_trace_logger->IsEnabled(TRACE_FDC)))
        TraceResults(count);
}

void I82077::StartCommand(u64 clocks)
{
    const u8* p = m_state.parameters;
    int drive = p[1] & 0x03;

    switch (m_state.command)
    {
        case I82077_COMMAND_SPECIFY:
            m_state.srt = p[1] >> 4;
            m_state.hut = p[1] & 0x0F;
            m_state.hlt = p[2] >> 1;
            m_state.non_dma = (p[2] & 0x01) != 0;
            EnterCommandPhase();
            break;
        case I82077_COMMAND_CONFIGURE:
            WriteConfigure();
            EnterCommandPhase();
            break;
        case I82077_COMMAND_PERPENDICULAR:
            m_state.perpendicular = (u8)((p[1] & 0x80) != 0 ? p[1] & 0x3F : (m_state.perpendicular & 0x3C) | (p[1] & 0x03));
            EnterCommandPhase();
            break;
        case I82077_COMMAND_SENSE_INTERRUPT:
            AnswerSense();
            break;
        case I82077_COMMAND_SENSE_DRIVE:
            m_state.results[0] = k_i82077_st3_fixed | (p[1] & 0x07) |
                (m_fdc->IsWriteProtected() ? k_i82077_st3_write_protect : 0) |
                (m_fdc->IsTrack0() ? k_i82077_st3_track0 : 0);
            SetResults(1);
            break;
        case I82077_COMMAND_VERSION:
            m_state.results[0] = k_i82077_version;
            SetResults(1);
            break;
        case I82077_COMMAND_DUMPREG:
            AnswerDumpreg();
            break;
        case I82077_COMMAND_LOCK:
            m_state.lock = (p[0] & 0x80) != 0;
            m_state.results[0] = m_state.lock ? 0x10 : 0x00;
            SetResults(1);
            break;
        case I82077_COMMAND_RECALIBRATE:
            m_state.pcn[drive] = 0;
            StartSeek(drive, I82077_SEEK_RECALIBRATE, 0, k_i82077_recalibrate_steps, false, clocks);
            EnterCommandPhase();
            break;
        case I82077_COMMAND_SEEK:
            StartSeek(drive, I82077_SEEK_NORMAL, p[2], 0, p[2] > m_state.pcn[drive], clocks);
            EnterCommandPhase();
            break;
        case I82077_COMMAND_RELATIVE_SEEK:
            StartSeek(drive, I82077_SEEK_RELATIVE, 0, p[2], (p[0] & 0x40) != 0, clocks);
            EnterCommandPhase();
            break;
        default:
            StartData(clocks);
            break;
    }
}

void I82077::WriteConfigure()
{
    u8 value = m_state.parameters[2];

    m_state.implied_seek = (value & 0x40) != 0;
    m_state.fifo_disabled = (value & 0x20) != 0;
    m_state.poll_disabled = (value & 0x10) != 0;
    m_state.fifo_threshold = value & 0x0F;
    m_state.precompensation_track = m_state.parameters[3];

    if (m_state.poll_disabled)
        m_state.poll_clocks = GDOX_NO_EVENT;
}

void I82077::AnswerSense()
{
    SetInterrupt(false);

    if (m_state.sense_count == 0)
    {
        m_state.results[0] = k_i82077_st0_invalid;
        SetResults(1);
        return;
    }

    m_state.results[0] = m_state.sense_st0[0];
    m_state.results[1] = m_state.sense_pcn[0];
    m_state.drive_busy &= ~(1 << (m_state.sense_st0[0] & 0x03));
    m_state.sense_count--;
    memmove(m_state.sense_st0, m_state.sense_st0 + 1, m_state.sense_count);
    memmove(m_state.sense_pcn, m_state.sense_pcn + 1, m_state.sense_count);
    SetResults(2);
}

void I82077::AnswerDumpreg()
{
    u8* r = m_state.results;

    memcpy(r, m_state.pcn, I82077_DRIVES);
    r[4] = (u8)((m_state.srt << 4) | m_state.hut);
    r[5] = (u8)((m_state.hlt << 1) | (m_state.non_dma ? 1 : 0));
    r[6] = m_state.last_sector_count;
    r[7] = (u8)((m_state.lock ? 0x80 : 0x00) | m_state.perpendicular);
    r[8] = (u8)((m_state.implied_seek ? 0x40 : 0x00) | (m_state.fifo_disabled ? 0x20 : 0x00) |
        (m_state.poll_disabled ? 0x10 : 0x00) | m_state.fifo_threshold);
    r[9] = m_state.precompensation_track;
    SetResults(10);
}

void I82077::QueueSense(u8 st0, u8 pcn)
{
    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_FDC, TRACE_FDC_SEEK_END)))
    {
        GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_FDC, TRACE_FDC_SEEK_END);
        entry->fdc.status = st0;
        entry->fdc.cylinder = pcn;
        entry->fdc.drive = (s8)(st0 & 0x03);
        entry->fdc.command = m_state.command;
    }

    if (m_state.sense_count >= I82077_SENSE_SIZE)
        return;

    m_state.sense_st0[m_state.sense_count] = st0;
    m_state.sense_pcn[m_state.sense_count] = pcn;
    m_state.sense_count++;
}

// One interrupt for the four status flags of the drives polled after a reset
void I82077::RunPoll()
{
    m_state.poll_clocks = GDOX_NO_EVENT;

    for (int i = 0; i < I82077_DRIVES; i++)
        QueueSense((u8)(k_i82077_st0_polling | i), m_state.pcn[i]);

    SetInterrupt(true);
}

void I82077::StartSeek(int drive, I82077_Seek_Type type, u8 target, u16 steps, bool inward, u64 clocks)
{
    I82077_Seek& seek = m_state.seeks[drive];

    seek.type = (u8)type;
    seek.target = target;
    seek.steps = steps;
    seek.inward = inward;
    seek.failed = false;
    seek.clocks = clocks + k_i82077_command_clocks;
    m_state.drive_busy |= (u8)(1 << drive);
}

// A step reaches whichever drive the DOR selects. PCN follows the pulses, not the head
void I82077::RunSeek(int drive, u64 clocks)
{
    I82077_Seek& seek = m_state.seeks[drive];
    u8& pcn = m_state.pcn[drive];

    switch (seek.type)
    {
        case I82077_SEEK_RECALIBRATE:
            if (m_fdc->IsTrack0())
            {
                FinishSeek(drive, clocks);
                return;
            }

            if (seek.steps == 0)
            {
                seek.failed = true;
                FinishSeek(drive, clocks);
                return;
            }

            seek.steps--;
            m_fdc->Step(false);
            break;
        case I82077_SEEK_RELATIVE:
            if (seek.steps == 0)
            {
                FinishSeek(drive, clocks);
                return;
            }

            if (!seek.inward && m_fdc->IsTrack0())
                seek.failed = true;

            seek.steps--;
            pcn = (u8)(pcn + (seek.inward ? 1 : -1));
            m_fdc->Step(seek.inward);
            break;
        default:
            if (pcn == seek.target)
            {
                FinishSeek(drive, clocks);
                return;
            }

            pcn = (u8)(pcn + (seek.inward ? 1 : -1));
            m_fdc->Step(seek.inward);
            break;
    }

    seek.clocks = clocks + GetStepClocks();
}

void I82077::FinishSeek(int drive, u64 clocks)
{
    I82077_Seek& seek = m_state.seeks[drive];
    u8 type = seek.type;
    bool failed = seek.failed;

    seek.type = I82077_SEEK_NONE;
    seek.clocks = GDOX_NO_EVENT;

    if (type == I82077_SEEK_IMPLIED)
    {
        m_state.drive_busy = UNSET_BIT(m_state.drive_busy, drive);
        m_state.st0 |= k_i82077_st0_seek_end;
        BeginWork(clocks);
        return;
    }

    u8 st0 = (u8)(k_i82077_st0_seek_end | drive);

    if (failed)
        st0 |= k_i82077_st0_abnormal | k_i82077_st0_equipment;

    QueueSense(st0, m_state.pcn[drive]);
    SetInterrupt(true);
}

void I82077::Run(u64 clocks)
{
    for (;;)
    {
        u64 next = GetEventClocks();

        if (next > clocks)
            return;

        if (next == m_state.poll_clocks)
        {
            RunPoll();
            continue;
        }

        bool seeking = false;

        for (int i = 0; i < I82077_DRIVES && !seeking; i++)
        {
            if (m_state.seeks[i].clocks == next)
            {
                RunSeek(i, next);
                seeking = true;
            }
        }

        if (!seeking)
            RunExecution(next);
    }
}

void I82077::RunExecution(u64 clocks)
{
    m_state.event_clocks = GDOX_NO_EVENT;

    switch (m_state.step)
    {
        case I82077_STEP_START:
            RunStart(clocks);
            break;
        case I82077_STEP_INDEX:
            if (m_state.command == I82077_COMMAND_FORMAT)
            {
                m_state.step = I82077_STEP_FORMAT;
                m_state.data_clocks = clocks;
                RunFormat(clocks);
            }
            else
                BeginSearch(clocks);
            break;
        case I82077_STEP_SEARCH:
            RunSearch(clocks);
            break;
        case I82077_STEP_DATA:
            RunData(clocks);
            break;
        case I82077_STEP_CRC:
            RunSectorEnd(clocks);
            break;
        case I82077_STEP_FORMAT:
            RunFormat(clocks);
            break;
        case I82077_STEP_FORMAT_END:
            FinishFormat(clocks);
            break;
        default:
            break;
    }
}

void I82077::StartData(u64 clocks)
{
    const u8* p = m_state.parameters;

    m_state.phase = I82077_PHASE_EXECUTION;
    m_state.step = I82077_STEP_START;
    m_state.event_clocks = clocks + k_i82077_command_clocks;
    m_state.multi_track = (p[0] & 0x80) != 0;
    m_state.mfm = (p[0] & 0x40) != 0;
    m_state.skip = (p[0] & 0x20) != 0;
    m_state.drive = p[1] & 0x03;
    m_state.side = (p[1] >> 2) & 0x01;
    m_state.st0 = 0;
    m_state.st1 = 0;
    m_state.st2 = 0;
    m_state.terminal_count = false;
    m_state.abort = false;
    m_state.stop_after_sector = false;
    m_state.track_index = 0;
    m_state.id_seen = 0;
    m_state.fifo_head = 0;
    m_state.fifo_count = 0;
    m_state.reading = !IsWrite();

    if (m_state.command == I82077_COMMAND_FORMAT)
    {
        m_state.size_code = p[2];
        m_state.format_count = p[3];
        m_state.format_filler = p[5];
        m_state.format_index = 0;
        m_state.last_sector_count = p[3];
    }
    else if (m_state.command != I82077_COMMAND_READ_ID)
    {
        m_state.cylinder = p[2];
        m_state.head = p[3];
        m_state.sector = p[4];
        m_state.size_code = p[5];
        m_state.last_sector = p[6];
        m_state.data_length = p[8];
        m_state.last_sector_count = p[6];
        m_state.verify_count = (p[1] & 0x80) != 0 ? (p[8] != 0 ? p[8] : 256) : 0;
    }
}

void I82077::RunStart(u64 clocks)
{
    if (IsWrite() && m_fdc->IsWriteProtected())
    {
        Abnormal(k_i82077_st1_not_writable, 0);
        Finish(clocks);
        return;
    }

    bool has_cylinder = m_state.command != I82077_COMMAND_FORMAT && m_state.command != I82077_COMMAND_READ_ID;

    if (m_state.implied_seek && has_cylinder && m_state.pcn[m_state.drive] != m_state.cylinder)
    {
        u8 target = m_state.cylinder;

        m_state.step = I82077_STEP_SEEK;
        StartSeek(m_state.drive, I82077_SEEK_IMPLIED, target, 0, target > m_state.pcn[m_state.drive], clocks);
        return;
    }

    BeginWork(clocks);
}

// The head loads if it had time to unload, then the disk has to come round
void I82077::BeginWork(u64 clocks)
{
    u64 start = clocks;

    if (clocks >= m_state.head_unload_clocks)
        start += GetHeadLoadClocks();

    if (m_state.command == I82077_COMMAND_FORMAT || m_state.command == I82077_COMMAND_READ_TRACK)
    {
        m_state.step = I82077_STEP_INDEX;
        m_state.event_clocks = m_fdc->HasIndex() ? m_fdc->GetNextIndex(start) : GDOX_NO_EVENT;
    }
    else
        BeginSearch(start);

    m_state.transferring = m_state.command != I82077_COMMAND_VERIFY && m_state.command != I82077_COMMAND_READ_ID;
    UpdateRequest();
}

// With no disk or motor the index pulses never come and the chip waits, until the drive turns or a reset
void I82077::Resume(u64 clocks)
{
    if (m_state.phase != I82077_PHASE_EXECUTION || m_state.event_clocks != GDOX_NO_EVENT || !m_fdc->HasIndex())
        return;

    if (m_state.step == I82077_STEP_SEARCH)
        BeginSearch(clocks);
    else if (m_state.step == I82077_STEP_INDEX)
        m_state.event_clocks = m_fdc->GetNextIndex(clocks);
}

// System 34 and 3740 fields, with what's left of the track shared between the sectors as gap 3
int I82077::GetTrackLayout(FloppyDisk_Sector* sectors, u32* id_ends, u32* data_starts, u32* track_bytes) const
{
    u32 preamble = m_state.mfm ? 146 : 73;
    u32 id = m_state.mfm ? 22 : 13;
    u32 to_data = m_state.mfm ? 38 : 18;
    int count = m_fdc->GetSectors(m_state.side, GetRateKbps(), sectors);
    u32 used = preamble;

    *track_bytes = m_fdc->GetTrackBytes(GetRateKbps(), m_state.mfm);

    for (int i = 0; i < count; i++)
        used += id + to_data + 2 + sectors[i].size;

    u32 gap = (count > 0 && used < *track_bytes) ? (*track_bytes - used) / count : 0;
    u32 position = preamble;

    for (int i = 0; i < count; i++)
    {
        id_ends[i] = position + id;
        data_starts[i] = id_ends[i] + to_data;
        position = data_starts[i] + sectors[i].size + 2 + gap;
    }

    return count;
}

bool I82077::MatchesID(const FloppyDisk_Sector& sector) const
{
    return sector.id[0] == m_state.cylinder && sector.id[1] == m_state.head && sector.id[2] == m_state.sector &&
        sector.id[3] == m_state.size_code;
}

// Two index pulses without what it is looking for end the search
void I82077::BeginSearch(u64 clocks)
{
    m_state.step = I82077_STEP_SEARCH;
    m_state.found = -1;

    if (!m_fdc->HasIndex())
    {
        m_state.event_clocks = GDOX_NO_EVENT;
        return;
    }

    m_state.deadline_clocks = m_fdc->GetNextIndex(m_fdc->GetNextIndex(clocks));

    FloppyDisk_Sector sectors[k_floppy_max_sectors];
    u32 id_ends[k_floppy_max_sectors];
    u32 data_starts[k_floppy_max_sectors];
    u32 track_bytes = 0;
    int count = GetTrackLayout(sectors, id_ends, data_starts, &track_bytes);
    u64 best = GDOX_NO_EVENT;

    m_state.id_seen = 0;

    for (int i = 0; i < count; i++)
    {
        if (sectors[i].fm == m_state.mfm || sectors[i].status == k_floppy_status_no_id)
            continue;

        m_state.id_seen |= 0x01;

        if (m_state.command == I82077_COMMAND_READ_TRACK)
        {
            if (i != m_state.track_index)
                continue;
        }
        else if (m_state.command != I82077_COMMAND_READ_ID && !MatchesID(sectors[i]))
        {
            if (sectors[i].id[0] != m_state.cylinder)
                m_state.id_seen |= sectors[i].id[0] == 0xFF ? 0x04 : 0x02;

            continue;
        }

        u64 when = m_fdc->GetPositionClocks(clocks, id_ends[i], track_bytes);

        if (when < best)
        {
            best = when;
            m_state.found = (s16)i;
        }
    }

    if (best > m_state.deadline_clocks)
        m_state.found = -1;

    m_state.event_clocks = m_state.found >= 0 ? best : m_state.deadline_clocks;
}

void I82077::Abnormal(u8 st1, u8 st2)
{
    m_state.st0 |= k_i82077_st0_abnormal;
    m_state.st1 |= st1;
    m_state.st2 |= st2;
}

bool I82077::IsMarkWanted(const FloppyDisk_Sector& sector) const
{
    return sector.deleted == (m_state.command == I82077_COMMAND_READ_DELETED);
}

void I82077::RunSearch(u64 clocks)
{
    if (m_state.found < 0)
    {
        if (m_state.command == I82077_COMMAND_READ_ID || (m_state.id_seen & 0x01) == 0)
            Abnormal(k_i82077_st1_missing_mark, 0);
        else
            Abnormal(k_i82077_st1_no_data, ((m_state.id_seen & 0x06) != 0 ? k_i82077_st2_wrong_cylinder : 0) |
                ((m_state.id_seen & 0x04) != 0 ? k_i82077_st2_bad_cylinder : 0));

        Finish(clocks);
        return;
    }

    FloppyDisk_Sector sectors[k_floppy_max_sectors];
    u32 id_ends[k_floppy_max_sectors];
    u32 data_starts[k_floppy_max_sectors];
    u32 track_bytes = 0;

    if (GetTrackLayout(sectors, id_ends, data_starts, &track_bytes) <= m_state.found)
    {
        BeginSearch(clocks);
        return;
    }

    const FloppyDisk_Sector& sector = sectors[m_state.found];

    if (m_state.command == I82077_COMMAND_READ_ID)
    {
        m_state.cylinder = sector.id[0];
        m_state.head = sector.id[1];
        m_state.sector = sector.id[2];
        m_state.size_code = sector.id[3];
        Finish(clocks);
        return;
    }

    if (m_state.command == I82077_COMMAND_READ_TRACK)
    {
        if (!MatchesID(sector))
            m_state.st1 |= k_i82077_st1_no_data;
    }
    else if (sector.status == k_floppy_status_id_crc)
    {
        Abnormal(k_i82077_st1_data_error, 0);
        Finish(clocks);
        return;
    }

    if (sector.status == k_floppy_status_no_data)
    {
        Abnormal(k_i82077_st1_missing_mark, k_i82077_st2_missing_data);
        Finish(clocks);
        return;
    }

    BeginSector(sector, data_starts[m_state.found] - id_ends[m_state.found], clocks);
}

// A mark of the other kind sets CM. With SK the sector is passed over, without it this is the last one read
void I82077::BeginSector(const FloppyDisk_Sector& sector, u32 gap, u64 clocks)
{
    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_FDC, TRACE_FDC_SECTOR)))
        TraceSector(sector);

    bool wrong_mark = !IsWrite() && m_state.command != I82077_COMMAND_READ_TRACK && !IsMarkWanted(sector);
    bool passed = wrong_mark && m_state.skip;

    if (wrong_mark)
    {
        m_state.st2 |= k_i82077_st2_control_mark;
        m_state.stop_after_sector = !m_state.skip;
    }

    u32 rate = GetRateKbps();

    m_state.header = sector.header;
    m_state.data_offset = sector.header + k_floppy_sector_header_size;
    m_state.sector_bytes = sector.size;
    m_state.transfer_bytes = sector.size;

    if (m_state.size_code == 0 && m_state.data_length < sector.size)
        m_state.transfer_bytes = m_state.data_length;

    m_state.data_index = 0;
    m_state.data_clocks = clocks + m_fdc->GetByteClocks(gap, rate, m_state.mfm);

    if (passed || m_state.command == I82077_COMMAND_VERIFY)
    {
        m_state.step = I82077_STEP_CRC;
        m_state.event_clocks = m_state.data_clocks + m_fdc->GetByteClocks(sector.size + 2, rate, m_state.mfm);
        return;
    }

    m_state.step = I82077_STEP_DATA;
    m_state.event_clocks = m_state.data_clocks + m_fdc->GetByteClocks(1, rate, m_state.mfm);
}

// One byte under the head. Past TC, or past an overrun, the rest of the sector goes by with no transfers
void I82077::RunData(u64 clocks)
{
    u32 rate = GetRateKbps();
    bool stopped = m_state.terminal_count || m_state.abort;

    if (m_state.reading)
    {
        if (!stopped && m_state.data_index < m_state.transfer_bytes)
        {
            if (!PushFIFO(m_fdc->ReadByte(m_state.data_offset + m_state.data_index)))
            {
                Abnormal(k_i82077_st1_overrun, 0);
                m_state.abort = true;
            }

            UpdateRequest();
        }
    }
    else
    {
        u8 value = 0;

        if (!PopFIFO(value) && !stopped)
        {
            Abnormal(k_i82077_st1_overrun, 0);
            m_state.abort = true;
        }

        UpdateRequest();
        m_fdc->WriteByte(m_state.data_offset + m_state.data_index, value);
    }

    m_state.data_index++;

    if (m_state.data_index >= m_state.sector_bytes)
    {
        m_state.step = I82077_STEP_CRC;
        m_state.event_clocks = clocks + m_fdc->GetByteClocks(2, rate, m_state.mfm);
        return;
    }

    if (m_state.reading && (m_state.terminal_count || m_state.abort))
    {
        m_state.step = I82077_STEP_CRC;
        m_state.event_clocks = clocks + m_fdc->GetByteClocks(m_state.sector_bytes - m_state.data_index + 2, rate, m_state.mfm);
        return;
    }

    m_state.event_clocks = m_state.data_clocks + m_fdc->GetByteClocks(m_state.data_index + 1, rate, m_state.mfm);
}

// C, H and R as the sheet's table has them, for an end on TC or EOT
void I82077::AdvanceResult()
{
    if (m_state.sector != m_state.last_sector)
    {
        m_state.sector++;
        return;
    }

    m_state.sector = 1;

    if (!m_state.multi_track)
    {
        m_state.cylinder++;
        return;
    }

    if ((m_state.head & 0x01) != 0)
        m_state.cylinder++;

    m_state.head ^= 0x01;
}

void I82077::RunSectorEnd(u64 clocks)
{
    if (IsWrite())
        m_fdc->SetSectorStatus(m_state.header, m_state.command == I82077_COMMAND_WRITE_DELETED, 0);
    else
    {
        FloppyDisk_Sector sectors[k_floppy_max_sectors];
        u32 id_ends[k_floppy_max_sectors];
        u32 data_starts[k_floppy_max_sectors];
        u32 track_bytes = 0;
        int count = GetTrackLayout(sectors, id_ends, data_starts, &track_bytes);

        if (m_state.found < count && sectors[m_state.found].status == k_floppy_status_data_crc)
        {
            Abnormal(k_i82077_st1_data_error, k_i82077_st2_data_crc);

            if (m_state.command != I82077_COMMAND_READ_TRACK)
            {
                Finish(clocks);
                return;
            }
        }
    }

    if (m_state.abort || m_state.stop_after_sector)
    {
        Finish(clocks);
        return;
    }

    if (m_state.terminal_count)
    {
        AdvanceResult();
        Finish(clocks);
        return;
    }

    if (m_state.command == I82077_COMMAND_READ_TRACK)
    {
        m_state.track_index++;

        if (m_state.sector == m_state.last_sector)
        {
            AdvanceResult();
            Abnormal(k_i82077_st1_end_of_track, 0);
            Finish(clocks);
            return;
        }

        m_state.sector++;
        BeginSearch(clocks);
        return;
    }

    if (m_state.verify_count > 0 && --m_state.verify_count == 0)
    {
        AdvanceResult();
        Finish(clocks);
        return;
    }

    if (m_state.sector != m_state.last_sector)
    {
        m_state.sector++;
        BeginSearch(clocks);
        return;
    }

    if (m_state.multi_track && m_state.side == 0)
    {
        m_state.side = 1;
        m_state.head ^= 0x01;
        m_state.sector = 1;
        BeginSearch(clocks);
        return;
    }

    AdvanceResult();

    if (m_state.command != I82077_COMMAND_VERIFY)
        Abnormal(k_i82077_st1_end_of_track, 0);

    Finish(clocks);
}

// The IDs come in four bytes per sector, spread over the track, and the track is written at the next index pulse
void I82077::RunFormat(u64 clocks)
{
    u32 rate = GetRateKbps();
    u32 count = MIN((u32)m_state.format_count, (u32)(I82077_FORMAT_IDS / 4));

    if (m_state.format_index >= count * 4 || ((m_state.terminal_count || m_state.abort) && m_state.fifo_count == 0))
    {
        m_state.step = I82077_STEP_FORMAT_END;
        m_state.event_clocks = m_fdc->GetNextIndex(clocks);
        return;
    }

    u8 value = 0;

    if (!PopFIFO(value))
    {
        Abnormal(k_i82077_st1_overrun, 0);
        m_state.abort = true;
    }

    UpdateRequest();
    m_state.format_ids[m_state.format_index++] = value;

    u32 track_bytes = m_fdc->GetTrackBytes(rate, m_state.mfm);
    u32 span = count > 0 ? (track_bytes - 146) / count : track_bytes;
    u32 sector = m_state.format_index / 4;
    u32 position = 146 + sector * span + (m_state.format_index & 3);

    m_state.event_clocks = m_state.data_clocks + m_fdc->GetByteClocks(position, rate, m_state.mfm);
}

void I82077::FinishFormat(u64 clocks)
{
    if (!m_state.abort)
        m_fdc->WriteTrack(m_state.side, m_state.format_ids, m_state.format_index / 4, m_state.size_code,
            m_state.format_filler, m_state.mfm);

    m_state.cylinder = 0;
    m_state.head = 0;
    m_state.sector = 0;
    Finish(clocks);
}

// Bytes left in the FIFO have to go first, or the result phase can't start
void I82077::Finish(u64 clocks)
{
    m_state.event_clocks = GDOX_NO_EVENT;

    if (m_state.reading && m_state.fifo_count > 0)
    {
        m_state.step = I82077_STEP_DRAIN;
        UpdateRequest();
        return;
    }

    EnterResult(clocks);
}

void I82077::EnterResult(u64 clocks)
{
    u8* r = m_state.results;

    m_state.step = I82077_STEP_IDLE;
    m_state.transferring = false;
    m_state.head_unload_clocks = clocks + GetHeadUnloadClocks();
    r[0] = (u8)(m_state.st0 | (m_state.side << 2) | m_state.drive);
    r[1] = m_state.st1;
    r[2] = m_state.st2;
    r[3] = m_state.cylinder;
    r[4] = m_state.head;
    r[5] = m_state.sector;
    r[6] = m_state.size_code;
    UpdateRequest();
    SetResults(7);
    SetInterrupt(true);
}

bool I82077::PushFIFO(u8 value)
{
    if (m_state.fifo_count >= GetFIFOCapacity())
        return false;

    m_state.fifo[(m_state.fifo_head + m_state.fifo_count) % I82077_FIFO_SIZE] = value;
    m_state.fifo_count++;
    return true;
}

bool I82077::PopFIFO(u8& value)
{
    if (m_state.fifo_count == 0)
        return false;

    value = m_state.fifo[m_state.fifo_head];
    m_state.fifo_head = (m_state.fifo_head + 1) % I82077_FIFO_SIZE;
    m_state.fifo_count--;
    return true;
}

// Reads ask for service once the FIFO holds 16 minus the threshold, and until it empties. Writes keep asking
// from entry until it fills, then again once the threshold's worth of room opens up. Each byte is an interrupt
// without DMA
void I82077::UpdateRequest()
{
    bool active = false;

    if (m_state.transferring)
    {
        u32 capacity = GetFIFOCapacity();

        if (m_state.reading)
        {
            u32 trigger = capacity > 1 ? MAX(capacity - GetThreshold(), 1U) : 1;
            active = m_state.request ? m_state.fifo_count > 0 : (m_state.fifo_count >= trigger ||
                (m_state.step != I82077_STEP_DATA && m_state.fifo_count > 0));
        }
        else if (!m_state.terminal_count && !m_state.abort)
            active = m_state.request ? m_state.fifo_count < capacity : capacity - m_state.fifo_count >= GetThreshold();
    }

    if (active == m_state.request)
        return;

    m_state.request = active;

    if (m_state.non_dma)
        SetInterrupt(active);
    else
        m_fdc->UpdateLines();
}

void I82077::SetInterrupt(bool active)
{
    m_state.interrupt = active;
    m_fdc->UpdateLines();
}

bool I82077::ReadDMA(u16& value)
{
    u8 byte = 0;

    if (!PopFIFO(byte))
        return false;

    value = byte;
    UpdateRequest();

    if (m_state.step == I82077_STEP_DRAIN && m_state.fifo_count == 0)
        EnterResult(m_fdc->GetClocks());

    return true;
}

bool I82077::WriteDMA(u16 value)
{
    if (!PushFIFO((u8)value))
        return false;

    UpdateRequest();
    return true;
}

// TC counts with DACK, so the chip only sees it during a transfer of its own
void I82077::TerminalCount()
{
    if (!m_state.transferring)
        return;

    m_state.terminal_count = true;
    UpdateRequest();

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_FDC, TRACE_FDC_TC)))
    {
        GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_FDC, TRACE_FDC_TC);
        entry->fdc.command = m_state.command;
        entry->fdc.drive = (s8)m_state.drive;
        entry->fdc.sector = m_state.sector;
    }
}

// A write that picks another drive or changes a motor is a selection, the rest are control writes
void I82077::TraceDOR(u8 previous)
{
    u8 event = ((previous ^ m_state.dor) & 0xF3) != 0 ? TRACE_FDC_DRIVE_SELECT : TRACE_FDC_DRIVE_CONTROL;

    if (!m_trace_logger->IsEventEnabled(TRACE_FDC, event))
        return;

    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_FDC, event);
    entry->fdc.value = m_state.dor;
    entry->fdc.data = previous;
    entry->fdc.drive = (s8)m_fdc->GetSelectedDrive();
    entry->fdc.cylinder = entry->fdc.drive >= 0 ? m_fdc->GetState()->cylinders[entry->fdc.drive] : 0;
}

void I82077::TraceCommand(u8 first, u8 count)
{
    const u8* p = m_state.parameters;
    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_FDC, TRACE_FDC_COMMAND);
    entry->fdc.command = m_state.command;
    entry->fdc.value = first;
    entry->fdc.drive = (s8)(p[1] & 0x03);
    entry->fdc.cylinder = p[2];
    entry->fdc.track = p[3];
    entry->fdc.sector = p[4];
    entry->fdc.data = p[5];
    entry->fdc.status = GetMSR();
    entry->fdc.rate = m_state.rate;
    entry->fdc.non_dma = m_state.non_dma ? 1 : 0;
    entry->fdc.count = MIN(count, (u8)10);
    memcpy(entry->fdc.bytes, p, entry->fdc.count);
    entry->fdc.bytes[0] = first;
}

void I82077::TraceResults(u8 count)
{
    bool data = m_state.command >= I82077_COMMAND_READ && m_state.command <= I82077_COMMAND_READ_ID;
    u8 event = data ? TRACE_FDC_END : TRACE_FDC_RESULT;

    if (!m_trace_logger->IsEventEnabled(TRACE_FDC, event))
        return;

    const u8* r = m_state.results;
    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_FDC, event);
    entry->fdc.command = m_state.command;
    entry->fdc.status = r[0];
    entry->fdc.value = r[1];
    entry->fdc.data = r[2];
    entry->fdc.drive = (s8)m_state.drive;
    entry->fdc.cylinder = r[3];
    entry->fdc.track = r[4];
    entry->fdc.sector = r[5];
    entry->fdc.count = MIN(count, (u8)10);
    memcpy(entry->fdc.bytes, r, entry->fdc.count);
}

void I82077::TraceRate(u8 reg, u8 value)
{
    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_FDC, TRACE_FDC_RATE);
    entry->fdc.kind = reg;
    entry->fdc.value = value;
    entry->fdc.rate = m_state.rate;
    entry->fdc.drive = (s8)m_fdc->GetSelectedDrive();
}

void I82077::TraceSector(const FloppyDisk_Sector& sector)
{
    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_FDC, TRACE_FDC_SECTOR);
    entry->fdc.command = m_state.command;
    entry->fdc.drive = (s8)m_state.drive;
    entry->fdc.cylinder = sector.id[0];
    entry->fdc.track = sector.id[1];
    entry->fdc.sector = sector.id[2];
    entry->fdc.data = sector.id[3];
    entry->fdc.status = sector.status;
    entry->fdc.kind = (u8)((sector.deleted ? 0x01 : 0x00) | (sector.fm ? 0x02 : 0x00));
    entry->fdc.moved = sector.size;
}

void I82077::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
}

void I82077::LoadState(std::istream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
    SanitizeState();
}

void I82077::Serialize(StateSerializer& serializer)
{
    G_SERIALIZE(serializer, m_state.dor);
    G_SERIALIZE(serializer, m_state.rate);
    G_SERIALIZE(serializer, m_state.precompensation);
    G_SERIALIZE(serializer, m_state.phase);
    G_SERIALIZE(serializer, m_state.step);
    G_SERIALIZE(serializer, m_state.command);
    G_SERIALIZE_ARRAY(serializer, m_state.parameters, I82077_PARAMETERS);
    G_SERIALIZE(serializer, m_state.parameter_count);
    G_SERIALIZE(serializer, m_state.parameter_length);
    G_SERIALIZE_ARRAY(serializer, m_state.results, I82077_RESULTS);
    G_SERIALIZE(serializer, m_state.result_count);
    G_SERIALIZE(serializer, m_state.result_index);
    G_SERIALIZE(serializer, m_state.data_port);
    G_SERIALIZE_ARRAY(serializer, m_state.pcn, I82077_DRIVES);
    G_SERIALIZE(serializer, m_state.drive_busy);
    G_SERIALIZE(serializer, m_state.srt);
    G_SERIALIZE(serializer, m_state.hut);
    G_SERIALIZE(serializer, m_state.hlt);
    G_SERIALIZE(serializer, m_state.non_dma);
    G_SERIALIZE(serializer, m_state.implied_seek);
    G_SERIALIZE(serializer, m_state.fifo_disabled);
    G_SERIALIZE(serializer, m_state.poll_disabled);
    G_SERIALIZE(serializer, m_state.fifo_threshold);
    G_SERIALIZE(serializer, m_state.precompensation_track);
    G_SERIALIZE(serializer, m_state.lock);
    G_SERIALIZE(serializer, m_state.perpendicular);
    G_SERIALIZE(serializer, m_state.last_sector_count);
    G_SERIALIZE_ARRAY(serializer, m_state.fifo, I82077_FIFO_SIZE);
    G_SERIALIZE(serializer, m_state.fifo_head);
    G_SERIALIZE(serializer, m_state.fifo_count);
    G_SERIALIZE_ARRAY(serializer, m_state.sense_st0, I82077_SENSE_SIZE);
    G_SERIALIZE_ARRAY(serializer, m_state.sense_pcn, I82077_SENSE_SIZE);
    G_SERIALIZE(serializer, m_state.sense_count);

    for (int i = 0; i < I82077_DRIVES; i++)
    {
        G_SERIALIZE(serializer, m_state.seeks[i].clocks);
        G_SERIALIZE(serializer, m_state.seeks[i].steps);
        G_SERIALIZE(serializer, m_state.seeks[i].target);
        G_SERIALIZE(serializer, m_state.seeks[i].type);
        G_SERIALIZE(serializer, m_state.seeks[i].inward);
        G_SERIALIZE(serializer, m_state.seeks[i].failed);
    }

    G_SERIALIZE(serializer, m_state.event_clocks);
    G_SERIALIZE(serializer, m_state.deadline_clocks);
    G_SERIALIZE(serializer, m_state.poll_clocks);
    G_SERIALIZE(serializer, m_state.head_unload_clocks);
    G_SERIALIZE(serializer, m_state.data_clocks);
    G_SERIALIZE(serializer, m_state.interrupt);
    G_SERIALIZE(serializer, m_state.request);
    G_SERIALIZE(serializer, m_state.terminal_count);
    G_SERIALIZE(serializer, m_state.abort);
    G_SERIALIZE(serializer, m_state.reading);
    G_SERIALIZE(serializer, m_state.transferring);
    G_SERIALIZE(serializer, m_state.stop_after_sector);
    G_SERIALIZE(serializer, m_state.mfm);
    G_SERIALIZE(serializer, m_state.multi_track);
    G_SERIALIZE(serializer, m_state.skip);
    G_SERIALIZE(serializer, m_state.st0);
    G_SERIALIZE(serializer, m_state.st1);
    G_SERIALIZE(serializer, m_state.st2);
    G_SERIALIZE(serializer, m_state.drive);
    G_SERIALIZE(serializer, m_state.side);
    G_SERIALIZE(serializer, m_state.cylinder);
    G_SERIALIZE(serializer, m_state.head);
    G_SERIALIZE(serializer, m_state.sector);
    G_SERIALIZE(serializer, m_state.size_code);
    G_SERIALIZE(serializer, m_state.last_sector);
    G_SERIALIZE(serializer, m_state.data_length);
    G_SERIALIZE(serializer, m_state.verify_count);
    G_SERIALIZE(serializer, m_state.found);
    G_SERIALIZE(serializer, m_state.track_index);
    G_SERIALIZE(serializer, m_state.id_seen);
    G_SERIALIZE(serializer, m_state.header);
    G_SERIALIZE(serializer, m_state.data_offset);
    G_SERIALIZE(serializer, m_state.data_index);
    G_SERIALIZE(serializer, m_state.sector_bytes);
    G_SERIALIZE(serializer, m_state.transfer_bytes);
    G_SERIALIZE(serializer, m_state.format_index);
    G_SERIALIZE(serializer, m_state.format_count);
    G_SERIALIZE(serializer, m_state.format_filler);
    G_SERIALIZE_ARRAY(serializer, m_state.format_ids, I82077_FORMAT_IDS);
}

void I82077::SanitizeState()
{
    m_state.rate &= 0x03;
    m_state.parameter_count = MIN(m_state.parameter_count, (u8)I82077_PARAMETERS);
    m_state.result_count = MIN(m_state.result_count, (u8)I82077_RESULTS);
    m_state.fifo_head %= I82077_FIFO_SIZE;
    m_state.fifo_count = MIN(m_state.fifo_count, (u8)I82077_FIFO_SIZE);
    m_state.sense_count = MIN(m_state.sense_count, (u8)I82077_SENSE_SIZE);
    m_state.format_index = MIN(m_state.format_index, (u16)I82077_FORMAT_IDS);

    if (m_state.phase > I82077_PHASE_RESULT)
        m_state.phase = I82077_PHASE_COMMAND;

    if (m_state.step > I82077_STEP_DRAIN)
        m_state.step = I82077_STEP_IDLE;

    if (m_state.command > I82077_COMMAND_INVALID)
        m_state.command = I82077_COMMAND_NONE;
}
