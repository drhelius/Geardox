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


#ifndef PIT_INLINE_H
#define PIT_INLINE_H

#include "pit.h"
#include "pic.h"
#include "scheduler.h"
#include "../common/trace_logger.h"

// IRQ0 follows OUT0: the PIC latches each rising edge
INLINE void PIT::Synchronize(u64 clocks)
{
    u64 tick = GetTick(clocks);

    if (tick <= m_state.settled_tick)
        return;

    bool edge = m_pit.GetNextRisingEdge(k_pit_irq0_channel, m_state.settled_tick) <= tick;
    m_state.settled_tick = tick;

    if (!edge)
        return;

    if (unlikely(IsValidPointer(m_trace_logger) && m_trace_logger->IsEventEnabled(TRACE_TIMER, TRACE_TIMER_TIMEOUT)))
        TraceTimeout();

    m_pic->SetIRQLine(0, false);
    m_pic->SetIRQLine(0, true);
    UpdateNextEvent();
}

INLINE void PIT::HandleEvent(u64 clocks)
{
    Synchronize(clocks);
    UpdateNextEvent();
}

// The speaker sounds OUT2 while port 61h bit 1 lets it through
INLINE bool PIT::IsBuzzerEnabled() const
{
    return m_state.speaker_data;
}

INLINE bool PIT::GetBuzzerOutput(u64 tick)
{
    return m_pit.GetOutput(k_pit_buzzer_channel, tick);
}

INLINE I8253* PIT::GetPIT()
{
    return &m_pit;
}

INLINE PIT::PIT_State* PIT::GetState()
{
    return &m_state;
}

INLINE u64 PIT::GetTick(u64 clocks) const
{
    return clocks / k_pit_tick_clocks;
}

INLINE u64 PIT::GetTickClocks(u64 tick) const
{
    return tick * k_pit_tick_clocks;
}

#endif /* PIT_INLINE_H */
