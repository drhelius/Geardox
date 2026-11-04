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
#include "../common/state_serializer.h"
#include "../common/trace_logger.h"

static const u8 k_mc146818_days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

MC146818::MC146818()
{
    InitPointer(m_trace_logger);
    m_start_time = 0;
    memset(&m_state, 0, sizeof(m_state));
}

MC146818::~MC146818()
{
}

void MC146818::Init()
{
    Reset(0);
}

void MC146818::SetTraceLogger(TraceLogger* trace_logger)
{
    m_trace_logger = trace_logger;
}

void MC146818::SetStartTime(s64 start_time)
{
    m_start_time = start_time;
}

// Power on starts from the host local time, or the fixed start time in UTC, in 24 hour BCD mode
// The battery backed RAM above the clock keeps its contents
void MC146818::Reset(u64 clocks)
{
    m_state.index = 0;
    m_state.ram[k_mc146818_status_a] = k_mc146818_a_divider_normal | 0x06;
    m_state.ram[k_mc146818_status_b] = k_mc146818_b_24_hour;
    m_state.ram[k_mc146818_status_c] = 0x00;
    m_state.ram[k_mc146818_status_d] = k_mc146818_d_vrt;
    m_state.update_clocks = clocks + k_mc146818_second_clocks;

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
}

bool MC146818::Decodes(u16 port) const
{
    return port == 0x0070 || port == 0x0071;
}

u8 MC146818::Read(u16 port, u64 clocks)
{
    if (port != 0x0071)
        return 0xFF;

    Synchronize(clocks);
    u8 value = Peek(port, clocks);

    // Reading status C clears its flags
    if (m_state.index == k_mc146818_status_c)
        m_state.ram[k_mc146818_status_c] = 0x00;

    if (unlikely(IsValidPointer(m_trace_logger) && m_trace_logger->IsEventEnabled(TRACE_SYSTEM, TRACE_SYSTEM_RTC_READ)))
        TraceEvent(TRACE_SYSTEM_RTC_READ, value);

    return value;
}

// Counters catch up on a copy, so the value shown is the one a read would return
u8 MC146818::Peek(u16 port, u64 clocks) const
{
    if (port != 0x0071)
        return 0xFF;

    if (m_state.index > k_mc146818_status_d || clocks < m_state.update_clocks || !IsCounting())
    {
        u8 value = m_state.ram[m_state.index];

        if (m_state.index == k_mc146818_status_a && IsCounting() &&
            clocks + k_mc146818_uip_clocks >= m_state.update_clocks)
            value |= k_mc146818_a_uip;

        return value;
    }

    MC146818 synchronized(*this);
    InitPointer(synchronized.m_trace_logger);
    synchronized.Synchronize(clocks);
    return synchronized.Peek(port, clocks);
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

    if (unlikely(IsValidPointer(m_trace_logger) && m_trace_logger->IsEventEnabled(TRACE_SYSTEM, TRACE_SYSTEM_RTC_WRITE)))
        TraceEvent(TRACE_SYSTEM_RTC_WRITE, value);
}

// The clock registers change at each update, status A at the update in progress window, the RAM only with writes
u64 MC146818::GetReadStableUntil(u16 port, u64 clocks) const
{
    if (port != 0x0071 || m_state.index == k_mc146818_status_c)
        return 0;

    if (m_state.index > k_mc146818_status_d || !IsCounting())
        return GDOX_NO_EVENT;

    if (m_state.index == k_mc146818_status_a && clocks + k_mc146818_uip_clocks < m_state.update_clocks)
        return m_state.update_clocks - k_mc146818_uip_clocks;

    return m_state.update_clocks;
}

// One update per second while the divider runs and SET is clear
void MC146818::Synchronize(u64 clocks)
{
    if (!IsCounting())
    {
        if (clocks >= m_state.update_clocks)
            m_state.update_clocks = clocks + k_mc146818_second_clocks;

        return;
    }

    if (clocks < m_state.update_clocks)
        return;

    while (clocks >= m_state.update_clocks)
    {
        Tick();
        m_state.update_clocks += k_mc146818_second_clocks;
    }

    UpdateClockRegisters();
}

u8 MC146818::GetIndex() const
{
    return m_state.index;
}

u8* MC146818::GetCMOS()
{
    return m_state.ram;
}

u8 MC146818::ReadRAM(u8 index) const
{
    return m_state.ram[index & 0x7F];
}

void MC146818::WriteRAM(u8 index, u8 value)
{
    m_state.ram[index & 0x7F] = value;
}

void MC146818::ResetCMOS()
{
    memset(m_state.ram + 0x0E, 0, GDOX_CMOS_SIZE - 0x0E);
    UpdateClockRegisters();
}

bool MC146818::LoadCMOSFile(const u8* data, size_t size)
{
    if (size == GDOX_CMOS_FILE_SIZE)
    {
        if ((data[0] | (data[1] << 8)) != GDOX_CMOS_FILE_MAGIC)
            return false;

        data += GDOX_CMOS_FILE_HEADER_SIZE;
    }
    else if (size != GDOX_CMOS_SIZE)
        return false;

    memcpy(m_state.ram + 0x0E, data + 0x0E, GDOX_CMOS_SIZE - 0x0E);
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

MC146818::MC146818_State* MC146818::GetState()
{
    return &m_state;
}

bool MC146818::IsCounting() const
{
    return (m_state.ram[k_mc146818_status_a] & k_mc146818_a_divider) == k_mc146818_a_divider_normal &&
        (m_state.ram[k_mc146818_status_b] & k_mc146818_b_set) == 0;
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

u8 MC146818::Encode(u8 value) const
{
    if ((m_state.ram[k_mc146818_status_b] & k_mc146818_b_binary) != 0)
        return value;

    return (u8)(((value / 10) << 4) | (value % 10));
}

u8 MC146818::Decode(u8 value) const
{
    if ((m_state.ram[k_mc146818_status_b] & k_mc146818_b_binary) != 0)
        return value;

    return (u8)(((value >> 4) & 0x0F) * 10 + (value & 0x0F));
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
            m_state.seconds = (u8)(Decode(value) % 60);
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
            bool counting = IsCounting();
            m_state.ram[index] = value & 0x7F;

            // A divider restart runs half a second before the first update
            if (!counting && IsCounting())
                m_state.update_clocks = clocks + k_mc146818_second_clocks / 2;

            return;
        }
        case k_mc146818_status_b:
        {
            bool counting = IsCounting();
            m_state.ram[index] = value;

            // Clearing SET starts a new second
            if (!counting && IsCounting())
                m_state.update_clocks = clocks + k_mc146818_second_clocks;

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

void MC146818::TraceEvent(u8 event, u8 value)
{
    GDOX_Trace_Entry entry = {};
    entry.type = TRACE_SYSTEM;
    entry.event = event;
    entry.system.port = 0x0071;
    entry.system.address = m_state.index;
    entry.system.value = value;
    m_trace_logger->TraceLog(entry);
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
    G_SERIALIZE(serializer, m_state.update_clocks);
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
}
