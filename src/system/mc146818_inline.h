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


#ifndef MC146818_INLINE_H
#define MC146818_INLINE_H

#include "mc146818.h"

INLINE void MC146818::HandleEvent(u64 clocks)
{
    Synchronize(clocks);
    UpdateIRQ();
    UpdateNextEvent();
}

INLINE bool MC146818::Decodes(u16 port) const
{
    return port == 0x0070 || port == 0x0071;
}

INLINE u8 MC146818::GetIndex() const
{
    return m_state.index;
}

INLINE u8* MC146818::GetCMOS()
{
    return m_state.ram;
}

INLINE u8 MC146818::ReadRAM(u8 index) const
{
    return m_state.ram[index & 0x7F];
}

INLINE void MC146818::WriteRAM(u8 index, u8 value)
{
    m_state.ram[index & 0x7F] = value;
}

INLINE MC146818::MC146818_State* MC146818::GetState()
{
    return &m_state;
}

INLINE bool MC146818::IsDividerRunning() const
{
    return (m_state.ram[k_mc146818_status_a] & k_mc146818_a_divider) == k_mc146818_a_divider_normal;
}

INLINE bool MC146818::IsCounting() const
{
    return IsDividerRunning() && (m_state.ram[k_mc146818_status_b] & k_mc146818_b_set) == 0;
}

// From the rise of update in progress to update ended
INLINE bool MC146818::IsUpdating(u64 clocks) const
{
    return IsCounting() && (m_state.update_end_clocks != GDOX_NO_EVENT ||
        clocks + k_mc146818_uip_clocks >= m_state.update_clocks);
}

// Chain ticks between periodic edges, 0 when the rate select is off
INLINE u32 MC146818::GetPeriodTicks() const
{
    u32 rate = m_state.ram[k_mc146818_status_a] & k_mc146818_a_rate;

    if (rate == 0)
        return 0;

    return rate <= 2 ? 1U << (rate + 6) : 1U << (rate - 1);
}

INLINE u8 MC146818::GetStatusC() const
{
    u8 flags = m_state.ram[k_mc146818_status_c] & k_mc146818_c_flags;
    return flags | ((flags & m_state.ram[k_mc146818_status_b]) != 0 ? k_mc146818_c_irq : 0x00);
}

INLINE bool MC146818::MatchesAlarm(u8 alarm, u8 value) const
{
    return (alarm & k_mc146818_alarm_any) == k_mc146818_alarm_any || alarm == value;
}

// The clock of a tick of the 32768 Hz chain, split so it never leaves 64 bits
INLINE u64 MC146818::GetTickClocks(u64 ticks) const
{
    u64 seconds = ticks / k_mc146818_chain_rate;
    u64 rest = ticks % k_mc146818_chain_rate;
    return m_state.divider_clocks + seconds * GDOX_MACHINE_CLOCK_RATE +
        (rest * GDOX_MACHINE_CLOCK_RATE + k_mc146818_chain_rate - 1) / k_mc146818_chain_rate;
}

INLINE u8 MC146818::Encode(u8 value) const
{
    if ((m_state.ram[k_mc146818_status_b] & k_mc146818_b_binary) != 0)
        return value;

    return (u8)(((value / 10) << 4) | (value % 10));
}

INLINE u8 MC146818::Decode(u8 value) const
{
    if ((m_state.ram[k_mc146818_status_b] & k_mc146818_b_binary) != 0)
        return value;

    return (u8)(((value >> 4) & 0x0F) * 10 + (value & 0x0F));
}

#endif /* MC146818_INLINE_H */
