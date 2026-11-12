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

#ifndef I8254_INLINE_H
#define I8254_INLINE_H

#include "i8254.h"

INLINE bool I8254::GetOutput(int index, u64 tick)
{
    return GetCounterOutput(m_state.counters[index], tick);
}

INLINE bool I8254::PeekOutput(int index, u64 tick) const
{
    I8254_Counter counter = m_state.counters[index];
    return GetCounterOutput(counter, tick);
}

INLINE bool I8254::GetCounterOutput(I8254_Counter& counter, u64 tick) const
{
    if (!counter.counting)
        return !counter.programmed || counter.mode != 0;

    // A low gate forces OUT high in modes 2 and 3
    if (!counter.gate && (counter.mode == 2 || counter.mode == 3))
        return true;

    tick = GetGateTick(counter, tick);
    CommitReload(counter, tick);
    return GetLoadedOutput(counter, tick);
}

// OUT at a gate tick, once a reload due by then has been committed
INLINE bool I8254::GetLoadedOutput(const I8254_Counter& counter, u64 tick) const
{
    if (tick < counter.load)
        return counter.mode != 0;

    u64 elapsed = tick - counter.load + counter.phase;

    switch (counter.mode)
    {
        case 0:
        case 1:
            return elapsed >= counter.count;
        case 2:
            return elapsed % counter.count != counter.count - 1;
        case 3:
            return elapsed % counter.count < (counter.count + 1) / 2;
        case 4:
        case 5:
            return elapsed != counter.count;
        default:
            return true;
    }
}

INLINE u64 I8254::GetNextRisingEdge(int index, u64 tick) const
{
    const I8254_Counter& counter = m_state.counters[index];

    if (!counter.counting || (!counter.gate && counter.mode != 1 && counter.mode != 5))
        return k_i8254_no_edge;

    u64 edge = GetEdgeAfter(counter.mode, counter.load, counter.count, counter.phase, tick);

    // A pending reload takes over the schedule from its boundary on
    if (counter.reload_pending && edge > counter.pending_load)
        edge = GetEdgeAfter(counter.mode, counter.pending_load, counter.pending_count, counter.pending_phase,
            MAX(tick, counter.pending_load - 1));

    return edge;
}

INLINE I8254::I8254_State* I8254::GetState()
{
    return &m_state;
}

INLINE const I8254::I8254_State* I8254::GetState() const
{
    return &m_state;
}

INLINE void I8254::CommitReload(I8254_Counter& counter, u64 tick) const
{
    if (!counter.reload_pending || tick < counter.pending_load)
        return;

    counter.load = counter.pending_load;
    counter.count = counter.pending_count;
    counter.phase = counter.pending_phase;
    counter.reload_pending = false;
}

INLINE u64 I8254::GetEdgeAfter(u8 mode, u64 load, u32 count, u32 phase, u64 tick) const
{
    switch (mode)
    {
        case 0:
        case 1:
            return load + count > tick ? load + count : k_i8254_no_edge;
        case 4:
        case 5:
            return load + count + 1 > tick ? load + count + 1 : k_i8254_no_edge;
        case 2:
        case 3:
        {
            // A divisor of one never lets OUT change
            if (count < 2)
                return k_i8254_no_edge;

            if (tick < load)
                return load + count - phase;

            u64 periods = (tick - load + phase) / count + 1;
            return load + periods * count - phase;
        }
        default:
            return k_i8254_no_edge;
    }
}

#endif /* I8254_INLINE_H */
