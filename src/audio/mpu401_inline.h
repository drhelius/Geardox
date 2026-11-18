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

#ifndef MPU401_INLINE_H
#define MPU401_INLINE_H

#include "mpu401.h"

static const u16 k_mpu401_switch_bender = 1 << 0;
static const u16 k_mpu401_switch_thru = 1 << 1;
static const u16 k_mpu401_switch_data_in_stop = 1 << 2;
static const u16 k_mpu401_switch_measure_end = 1 << 3;
static const u16 k_mpu401_switch_conductor = 1 << 4;
static const u16 k_mpu401_switch_real_time_affection = 1 << 5;
static const u16 k_mpu401_switch_clock_to_host = 1 << 7;

INLINE bool MPU401::Clock(u32 clocks)
{
    u8 count = m_state.count;
    u64 period = GetPeriod();
    u64 phase = m_state.phase + (u64)clocks * m_rate;

    while (phase >= period)
    {
        u64 rate = m_rate;
        phase -= period;
        m_state.phase = phase;
        Tick();

        if (m_rate != rate)
            phase = (phase * m_rate) / rate;
    }

    m_state.phase = phase;
    return m_state.count != count;
}

INLINE bool MPU401::IsClockRunning() const
{
    return m_intelligent && !m_state.uart && (m_state.playing || (m_state.recording != MPU401_RECORD_OFF) ||
        m_state.running || ((m_state.switches & k_mpu401_switch_clock_to_host) != 0));
}

INLINE bool MPU401::IsIRQAsserted() const
{
    return m_state.count != 0;
}

INLINE bool MPU401::IsIntelligent() const
{
    return m_intelligent;
}

INLINE MPU401::MPU401_State* MPU401::GetState()
{
    return &m_state;
}

INLINE u64 MPU401::GetPeriod() const
{
    return 60ULL * 64ULL * GDOX_MACHINE_CLOCK_RATE;
}

#endif /* MPU401_INLINE_H */
