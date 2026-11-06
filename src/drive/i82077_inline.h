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

#ifndef I82077_INLINE_H
#define I82077_INLINE_H

#include "i82077.h"

INLINE u8 I82077::GetDOR() const
{
    return m_state.dor;
}

INLINE u8 I82077::GetRate() const
{
    return m_state.rate;
}

// INT and DRQ reach the bus only through the DMA gate
INLINE bool I82077::IsInterruptOut() const
{
    return m_state.interrupt && (m_state.dor & k_i82077_dor_gate) != 0;
}

INLINE bool I82077::IsRequestOut() const
{
    return m_state.request && !m_state.non_dma && (m_state.dor & k_i82077_dor_gate) != 0;
}

INLINE u64 I82077::GetEventClocks() const
{
    u64 next = MIN(m_state.event_clocks, m_state.poll_clocks);

    for (int i = 0; i < I82077_DRIVES; i++)
        next = MIN(next, m_state.seeks[i].clocks);

    return next;
}

INLINE u32 I82077::GetRateKbps() const
{
    static const u32 k_rates[4] = { 500, 300, 250, 1000 };
    return k_rates[m_state.rate & 0x03];
}

// SRT counts down from 16 in units of 0.5 ms at 1 Mbps, longer in proportion at slower rates
INLINE u64 I82077::GetStepClocks() const
{
    return ((u64)GDOX_MACHINE_CLOCK_RATE * (16 - m_state.srt)) / (2 * GetRateKbps());
}

// HLT counts 1 ms at 1 Mbps, 0 is 128
INLINE u64 I82077::GetHeadLoadClocks() const
{
    return ((u64)GDOX_MACHINE_CLOCK_RATE * (m_state.hlt != 0 ? m_state.hlt : 128)) / GetRateKbps();
}

// HUT counts 8 ms at 1 Mbps, 0 is 128
INLINE u64 I82077::GetHeadUnloadClocks() const
{
    return ((u64)GDOX_MACHINE_CLOCK_RATE * (m_state.hut != 0 ? m_state.hut : 16) * 8) / GetRateKbps();
}

INLINE bool I82077::IsWrite() const
{
    return m_state.command == I82077_COMMAND_WRITE || m_state.command == I82077_COMMAND_WRITE_DELETED ||
        m_state.command == I82077_COMMAND_FORMAT;
}

INLINE u32 I82077::GetFIFOCapacity() const
{
    return m_state.fifo_disabled ? 1 : I82077_FIFO_SIZE;
}

INLINE u32 I82077::GetThreshold() const
{
    return m_state.fifo_disabled ? 1 : (u32)m_state.fifo_threshold + 1;
}

INLINE I82077::I82077_State* I82077::GetState()
{
    return &m_state;
}

#endif /* I82077_INLINE_H */
