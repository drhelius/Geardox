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


#include <time.h>
#include "mc146818.h"
#include "pic.h"
#include "scheduler.h"
#include "../common/state_serializer.h"
#include "../common/trace_logger.h"

static const u8 k_mc146818_days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

MC146818::MC146818()
{
    InitPointer(m_pic);
    InitPointer(m_scheduler);
    m_trace_logger = TraceLogger::GetDisabled();
    m_start_time = 0;
    memset(&m_state, 0, sizeof(m_state));
    m_state.ram[k_mc146818_status_a] = k_mc146818_a_divider_normal | 0x06;
    m_state.ram[k_mc146818_status_b] = k_mc146818_b_24_hour;
}

MC146818::~MC146818()
{
}

void MC146818::Init(PIC* pic, Scheduler* scheduler)
{
    m_pic = pic;
    m_scheduler = scheduler;
    Reset(m_scheduler->GetClocks());
}

void MC146818::SetTraceLogger(TraceLogger* trace_logger)
{
    m_trace_logger = IsValidPointer(trace_logger) ? trace_logger : TraceLogger::GetDisabled();
}

void MC146818::SetStartTime(s64 start_time)
{
    m_start_time = start_time;
}

// Power on starts from the host local time, or the fixed start time in UTC
// RESET clears the interrupt enables and flags, the rest of the battery backed chip keeps its contents
void MC146818::Reset(u64 clocks)
{
    m_state.index = 0;
    m_state.ram[k_mc146818_status_b] &= (u8)~k_mc146818_b_reset_mask;
    m_state.ram[k_mc146818_status_c] = 0x00;
    m_state.ram[k_mc146818_status_d] = k_mc146818_d_vrt;
    m_state.irq = false;
    m_state.divider_clocks = clocks;
    m_state.update_clocks = clocks + k_mc146818_second_clocks;
    m_state.update_end_clocks = GDOX_NO_EVENT;
    UpdatePeriodic(clocks);

    time_t now = m_start_time != 0 ? (time_t)m_start_time : time(NULL);
    struct tm local;

    if (get_host_time(now, m_start_time != 0, &local))
    {
        m_state.seconds = (u8)MIN(local.tm_sec, 59);
        m_state.minutes = (u8)local.tm_min;
        m_state.hours = (u8)local.tm_hour;
        m_state.weekday = (u8)(local.tm_wday + 1);
        m_state.day = (u8)local.tm_mday;
        m_state.month = (u8)(local.tm_mon + 1);
        m_state.year = (u8)((local.tm_year + 1900) % 100);
        m_state.century = (u8)((local.tm_year + 1900) / 100);
    }
    else
    {
        m_state.seconds = 0;
        m_state.minutes = 0;
        m_state.hours = 0;
        m_state.weekday = 2;
        m_state.day = 1;
        m_state.month = 1;
        m_state.year = 90;
        m_state.century = 19;
    }

    UpdateClockRegisters();
    UpdateIRQ();
    UpdateNextEvent();
}

u8 MC146818::Read(u16 port, u64 clocks)
{
    if (port != 0x0071)
        return 0xFF;

    Synchronize(clocks);
    u8 value = Peek(port, clocks);

    // Reading status C clears its flags and drops the line
    if (m_state.index == k_mc146818_status_c)
    {
        m_state.ram[k_mc146818_status_c] = 0x00;
        UpdateIRQ();
        UpdateNextEvent();
    }

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_SYSTEM, TRACE_SYSTEM_RTC_READ)))
        TraceEvent(TRACE_SYSTEM_RTC_READ, value, value);

    return value;
}

// Counters catch up on a copy, so the value shown is the one a read would return
u8 MC146818::Peek(u16 port, u64 clocks) const
{
    if (port != 0x0071)
        return 0xFF;

    if (clocks >= GetNextChange())
    {
        MC146818 synchronized(*this);
        synchronized.m_trace_logger = TraceLogger::GetDisabled();
        synchronized.Synchronize(clocks);
        return synchronized.Peek(port, clocks);
    }

    if (m_state.index == k_mc146818_status_c)
        return GetStatusC();

    if (m_state.index == k_mc146818_status_a && IsUpdating(clocks))
        return m_state.ram[k_mc146818_status_a] | k_mc146818_a_uip;

    return m_state.ram[m_state.index];
}

void MC146818::Write(u16 port, u8 value, u64 clocks)
{
    Synchronize(clocks);

    if (port == 0x0070)
    {
        m_state.index = value & 0x7F;
        return;
    }

    if (port != 0x0071)
        return;

    WriteRegister(m_state.index, value, clocks);

    if (m_state.index == k_mc146818_status_a || m_state.index == k_mc146818_status_b)
    {
        UpdateIRQ();
        UpdateNextEvent();
    }

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_SYSTEM, TRACE_SYSTEM_RTC_WRITE)))
        TraceEvent(TRACE_SYSTEM_RTC_WRITE, m_state.ram[m_state.index], value);
}

// The clock registers change at each update, status A at the edges of update in progress, the RAM only with writes
// Status C changes with a read until its flags are clear, and then at the next flag
u64 MC146818::GetReadStableUntil(u16 port, u64 clocks) const
{
    if (port != 0x0071)
        return 0;

    if (m_state.index == k_mc146818_status_c)
        return m_state.ram[k_mc146818_status_c] == 0x00 ? GetNextChange() : 0;

    if (m_state.index > k_mc146818_status_d || !IsCounting())
        return GDOX_NO_EVENT;

    if (m_state.index == k_mc146818_status_a)
    {
        if (m_state.update_end_clocks != GDOX_NO_EVENT)
            return m_state.update_end_clocks;

        if (clocks + k_mc146818_uip_clocks < m_state.update_clocks)
            return m_state.update_clocks - k_mc146818_uip_clocks;
    }

    return m_state.update_clocks;
}

// Only the chip's own state moves here, so the debugger can run it on a copy
// The chain free runs while the divider does: SET only holds the updates
void MC146818::Synchronize(u64 clocks)
{
    if (!IsDividerRunning())
        return;

    if (clocks >= m_state.periodic_clocks)
    {
        m_state.ram[k_mc146818_status_c] |= k_mc146818_c_periodic;
        m_state.periodic_clocks = GetNextPeriodic(clocks);
    }

    if (clocks < MIN(m_state.update_clocks, m_state.update_end_clocks))
        return;

    bool ticked = false;

    while (clocks >= MIN(m_state.update_clocks, m_state.update_end_clocks))
    {
        if (m_state.update_end_clocks <= m_state.update_clocks)
        {
            m_state.ram[k_mc146818_status_c] |= k_mc146818_c_update;
            m_state.update_end_clocks = GDOX_NO_EVENT;
            continue;
        }

        if (IsCounting())
        {
            Tick();
            CheckAlarm();
            m_state.update_end_clocks = m_state.update_clocks + k_mc146818_update_clocks;
            ticked = true;
        }

        m_state.update_clocks += k_mc146818_second_clocks;
    }

    if (ticked)
        UpdateClockRegisters();
}

void MC146818::ResetCMOS()
{
    memset(m_state.ram + k_mc146818_ram_start, 0, GDOX_CMOS_SIZE - k_mc146818_ram_start);
    UpdateClockRegisters();
}

bool MC146818::LoadCMOSFile(const u8* data, size_t size)
{
    if (size == GDOX_CMOS_FILE_SIZE)
    {
        if (read_u16_le(data) != GDOX_CMOS_FILE_MAGIC)
            return false;

        data += GDOX_CMOS_FILE_HEADER_SIZE;
    }
    else if (size != GDOX_CMOS_SIZE)
        return false;

    memcpy(m_state.ram + k_mc146818_ram_start, data + k_mc146818_ram_start, GDOX_CMOS_SIZE - k_mc146818_ram_start);
    return true;
}

void MC146818::SaveCMOSFile(u8* data) const
{
    data[0] = GDOX_CMOS_FILE_MAGIC & 0xFF;
    data[1] = GDOX_CMOS_FILE_MAGIC >> 8;
    data[2] = GDOX_CMOS_FILE_VERSION & 0xFF;
    data[3] = GDOX_CMOS_FILE_VERSION >> 8;
    memcpy(data + GDOX_CMOS_FILE_HEADER_SIZE, m_state.ram, GDOX_CMOS_SIZE);
}

void MC146818::Tick()
{
    if (++m_state.seconds < 60)
        return;

    m_state.seconds = 0;

    if (++m_state.minutes < 60)
        return;

    m_state.minutes = 0;

    if (++m_state.hours < 24)
        return;

    m_state.hours = 0;
    m_state.weekday = m_state.weekday >= 7 ? 1 : m_state.weekday + 1;

    int year = m_state.century * 100 + m_state.year;
    bool leap = (year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0));
    int month = CLAMP((int)m_state.month, 1, 12);
    int days = k_mc146818_days[month - 1] + ((month == 2 && leap) ? 1 : 0);

    if (++m_state.day <= days)
        return;

    m_state.day = 1;

    if (++m_state.month <= 12)
        return;

    m_state.month = 1;

    if (++m_state.year < 100)
        return;

    m_state.year = 0;
    m_state.century++;
}

void MC146818::CheckAlarm()
{
    const u8* ram = m_state.ram;

    if (MatchesAlarm(ram[k_mc146818_seconds_alarm], Encode(m_state.seconds)) &&
        MatchesAlarm(ram[k_mc146818_minutes_alarm], Encode(m_state.minutes)) &&
        MatchesAlarm(ram[k_mc146818_hours_alarm], EncodeHours(m_state.hours)))
        m_state.ram[k_mc146818_status_c] |= k_mc146818_c_alarm;
}

// The first periodic edge after a clock, taps on the chain so rates keep their phase
u64 MC146818::GetNextPeriodic(u64 clocks) const
{
    u32 period = GetPeriodTicks();

    if (period == 0 || !IsDividerRunning())
        return GDOX_NO_EVENT;

    u64 elapsed = clocks > m_state.divider_clocks ? clocks - m_state.divider_clocks : 0;
    u64 ticks = (elapsed / GDOX_MACHINE_CLOCK_RATE) * k_mc146818_chain_rate +
        ((elapsed % GDOX_MACHINE_CLOCK_RATE) * k_mc146818_chain_rate) / GDOX_MACHINE_CLOCK_RATE;
    u64 edge = (ticks / period + 1) * period;

    if (GetTickClocks(edge) <= clocks)
        edge += period;

    return GetTickClocks(edge);
}

// The next clock anything in the chip moves
u64 MC146818::GetNextChange() const
{
    if (!IsDividerRunning())
        return GDOX_NO_EVENT;

    return MIN(MIN(m_state.update_clocks, m_state.update_end_clocks), m_state.periodic_clocks);
}

// The chain leaves reset with its last stage set, so the first update comes half a second later
void MC146818::StartDivider(u64 clocks)
{
    m_state.divider_clocks = clocks - MIN(clocks, k_mc146818_second_clocks / 2);
    m_state.update_clocks = clocks + k_mc146818_second_clocks / 2;
    m_state.update_end_clocks = GDOX_NO_EVENT;
}

void MC146818::UpdatePeriodic(u64 clocks)
{
    m_state.periodic_clocks = GetNextPeriodic(clocks);
}

void MC146818::UpdateIRQ()
{
    bool irq = (GetStatusC() & k_mc146818_c_irq) != 0;

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_SYSTEM, TRACE_SYSTEM_RTC_IRQ)) && irq && !m_state.irq)
        TraceEvent(TRACE_SYSTEM_RTC_IRQ, GetStatusC(), GetStatusC());

    m_state.irq = irq;
    m_pic->SetIRQLine(k_mc146818_irq, irq);
}

// A source only needs an event while it can move the line: enabled, with its flag clear
void MC146818::UpdateNextEvent()
{
    u64 next = GDOX_NO_EVENT;

    if (IsDividerRunning())
    {
        u8 pending = m_state.ram[k_mc146818_status_b] & (u8)~m_state.ram[k_mc146818_status_c];

        if ((pending & k_mc146818_b_periodic) != 0)
            next = m_state.periodic_clocks;

        if ((pending & (k_mc146818_b_alarm | k_mc146818_b_update)) != 0)
            next = MIN(next, MIN(m_state.update_clocks, m_state.update_end_clocks));
    }

    m_scheduler->Schedule(SCHEDULER_EVENT_RTC, next);
}

void MC146818::UpdateClockRegisters()
{
    m_state.ram[k_mc146818_seconds] = Encode(m_state.seconds);
    m_state.ram[k_mc146818_minutes] = Encode(m_state.minutes);
    m_state.ram[k_mc146818_hours] = EncodeHours(m_state.hours);
    m_state.ram[k_mc146818_weekday] = Encode(m_state.weekday);
    m_state.ram[k_mc146818_day] = Encode(m_state.day);
    m_state.ram[k_mc146818_month] = Encode(m_state.month);
    m_state.ram[k_mc146818_year] = Encode(m_state.year);
    m_state.ram[k_mc146818_century] = (u8)(((m_state.century / 10) << 4) | (m_state.century % 10));
}

// In 12 hour mode bit 7 marks PM and hours run 12, 1 to 11
u8 MC146818::EncodeHours(u8 hours) const
{
    if ((m_state.ram[k_mc146818_status_b] & k_mc146818_b_24_hour) != 0)
        return Encode(hours);

    u8 hour = hours % 12 == 0 ? 12 : hours % 12;
    return (u8)(Encode(hour) | (hours >= 12 ? 0x80 : 0x00));
}

u8 MC146818::DecodeHours(u8 value) const
{
    if ((m_state.ram[k_mc146818_status_b] & k_mc146818_b_24_hour) != 0)
        return (u8)(Decode(value) % 24);

    u8 hour = (u8)(Decode(value & 0x7F) % 12);
    return (u8)(hour + ((value & 0x80) != 0 ? 12 : 0));
}

void MC146818::WriteRegister(u8 index, u8 value, u64 clocks)
{
    switch (index)
    {
        case k_mc146818_seconds:
            m_state.seconds = (u8)(Decode(value & 0x7F) % 60);
            break;
        case k_mc146818_minutes:
            m_state.minutes = (u8)(Decode(value) % 60);
            break;
        case k_mc146818_hours:
            m_state.hours = DecodeHours(value);
            break;
        case k_mc146818_weekday:
            m_state.weekday = Decode(value);
            break;
        case k_mc146818_day:
            m_state.day = Decode(value);
            break;
        case k_mc146818_month:
            m_state.month = Decode(value);
            break;
        case k_mc146818_year:
            m_state.year = (u8)(Decode(value) % 100);
            break;
        case k_mc146818_century:
            m_state.century = (u8)(((value >> 4) & 0x0F) * 10 + (value & 0x0F));
            break;
        case k_mc146818_status_a:
        {
            bool running = IsDividerRunning();
            m_state.ram[index] = value & 0x7F;

            if (!running && IsDividerRunning())
                StartDivider(clocks);

            UpdatePeriodic(clocks);
            return;
        }
        case k_mc146818_status_b:
        {
            // SET going high stops an update under way and turns update ended off
            if ((value & (u8)~m_state.ram[index] & k_mc146818_b_set) != 0)
            {
                value &= (u8)~k_mc146818_b_update;
                m_state.update_end_clocks = GDOX_NO_EVENT;
            }

            m_state.ram[index] = value;
            UpdateClockRegisters();
            return;
        }
        case k_mc146818_status_c:
        case k_mc146818_status_d:
            return;
        default:
            m_state.ram[index] = value;

            if (index > k_mc146818_status_d)
                return;

            break;
    }

    if (index <= k_mc146818_year || index == k_mc146818_century)
        UpdateClockRegisters();
}

void MC146818::TraceEvent(u8 event, u8 value, u8 raw)
{
    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_SYSTEM, event);
    entry->system.port = 0x0071;
    entry->system.address = m_state.index;
    entry->system.value = value;
    entry->system.raw = raw;
    entry->system.previous = m_state.ram[k_mc146818_status_b];

    if (event == TRACE_SYSTEM_RTC_WRITE && m_state.index >= 0x10 && m_state.index <= 0x2D)
        entry->flags |= TRACE_SYSTEM_FLAG_CHECKSUM;

    if (event == TRACE_SYSTEM_RTC_WRITE && value != raw)
        entry->flags |= TRACE_ENTRY_IGNORED;
}

void MC146818::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
}

void MC146818::LoadState(std::istream& stream, int version)
{
    UNUSED(version);
    StateSerializer serializer(stream);
    Serialize(serializer);
    SanitizeState();
}

void MC146818::Serialize(StateSerializer& serializer)
{
    G_SERIALIZE_ARRAY(serializer, m_state.ram, GDOX_CMOS_SIZE);
    G_SERIALIZE(serializer, m_state.index);
    G_SERIALIZE(serializer, m_state.seconds);
    G_SERIALIZE(serializer, m_state.minutes);
    G_SERIALIZE(serializer, m_state.hours);
    G_SERIALIZE(serializer, m_state.weekday);
    G_SERIALIZE(serializer, m_state.day);
    G_SERIALIZE(serializer, m_state.month);
    G_SERIALIZE(serializer, m_state.year);
    G_SERIALIZE(serializer, m_state.century);
    G_SERIALIZE(serializer, m_state.irq);
    G_SERIALIZE(serializer, m_state.divider_clocks);
    G_SERIALIZE(serializer, m_state.update_clocks);
    G_SERIALIZE(serializer, m_state.update_end_clocks);
    G_SERIALIZE(serializer, m_state.periodic_clocks);
}

void MC146818::SanitizeState()
{
    m_state.index &= 0x7F;
    m_state.seconds %= 60;
    m_state.minutes %= 60;
    m_state.hours %= 24;
    m_state.year %= 100;
    m_state.month = (u8)CLAMP((int)m_state.month, 1, 12);
    m_state.day = (u8)CLAMP((int)m_state.day, 1, 31);
    UpdateIRQ();
    UpdateNextEvent();
}
