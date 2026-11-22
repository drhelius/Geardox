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


#include "pit.h"
#include "../audio/audio.h"
#include "../common/state_serializer.h"

PIT::PIT()
{
    InitPointer(m_pic);
    InitPointer(m_scheduler);
    InitPointer(m_audio);
    m_trace_logger = TraceLogger::GetDisabled();
    m_state.settled_tick = 0;
    m_state.speaker_gate = false;
    m_state.speaker_data = false;
}

PIT::~PIT()
{
}

void PIT::Init(PIC* pic, Scheduler* scheduler, Audio* audio)
{
    m_pic = pic;
    m_scheduler = scheduler;
    m_audio = audio;
    m_pit.Init();
    Reset();
}

void PIT::SetTraceLogger(TraceLogger* trace_logger)
{
    m_trace_logger = TraceLogger::Resolve(trace_logger);
}

// Gates 0 and 1 are tied high, gate 2 is port 61h bit 0
void PIT::Reset()
{
    m_pit.Reset();
    m_state.settled_tick = 0;
    m_state.speaker_gate = false;
    m_state.speaker_data = false;
    m_pit.SetGate(k_pit_buzzer_channel, false, 0);
    UpdateNextEvent();
}

u8 PIT::Read(u16 port, u64 clocks)
{
    int index = port & 0x03;

    // The control port is write only
    if (index == 3)
        return 0xFF;

    // Reading a counter can bring in a reload, and the speaker plays the old count up to here
    if (index == k_pit_buzzer_channel)
        m_audio->SynchronizeBuzzer(clocks);

    Synchronize(clocks);
    u8 value = m_pit.ReadCounter(index, GetTick(clocks));

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_TIMER, TRACE_TIMER_READ)))
        TraceRead(index, value);

    return value;
}

u8 PIT::Peek(u16 port, u64 clocks) const
{
    int index = port & 0x03;

    if (index == 3)
        return 0xFF;

    return m_pit.PeekCounter(index, GetTick(clocks));
}

void PIT::Write(u16 port, u8 value, u64 clocks)
{
    Synchronize(clocks);

    int index = port & 0x03;
    int counter = index == 3 ? value >> 6 : index;
    u64 tick = GetTick(clocks);

    // The speaker plays the previous count up to this write
    if (counter == k_pit_buzzer_channel || counter == 3)
        m_audio->SynchronizeBuzzer(clocks);

    if (index == 3)
    {
        bool low = counter == k_pit_irq0_channel && !m_pit.GetOutput(k_pit_irq0_channel, tick);
        m_pit.WriteControl(value, tick);

        // A control word sets OUT to its initial state at once, so a low OUT0 can rise here
        if (low && m_pit.GetOutput(k_pit_irq0_channel, tick))
        {
            m_pic->SetIRQLine(0, false);
            m_pic->SetIRQLine(0, true);
        }
    }
    else
        m_pit.WriteCounter(index, value, tick);

    if (unlikely(m_trace_logger->IsEnabled(TRACE_TIMER)))
        TraceWrite(port, value);

    UpdateNextEvent();
}

// Port 61h bits 0 and 1
void PIT::SetSpeaker(bool gate, bool data, u64 clocks)
{
    if (gate == m_state.speaker_gate && data == m_state.speaker_data)
        return;

    Synchronize(clocks);
    m_audio->SynchronizeBuzzer(clocks);
    m_state.speaker_gate = gate;
    m_state.speaker_data = data;
    m_pit.SetGate(k_pit_buzzer_channel, gate, GetTick(clocks));

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_TIMER, TRACE_TIMER_SPEAKER)))
        TraceSpeaker();
}

bool PIT::GetChannel2Output(u64 clocks) const
{
    return m_pit.PeekOutput(k_pit_buzzer_channel, GetTick(clocks));
}

// Port 61h bit 4 toggles at each refresh request, channel 1's rising OUT
bool PIT::GetRefreshToggle(u64 clocks) const
{
    return (GetRefreshEdges(GetTick(clocks)) & 1) != 0;
}

// Refresh requests from channel 1 once programmed as a rate or square wave, at the BIOS's 15 us before that
u64 PIT::GetRefreshEdges(u64 tick) const
{
    const I8254::I8254_Counter& counter = m_pit.GetState()->counters[k_pit_refresh_channel];

    if (counter.counting && (counter.mode == 2 || counter.mode == 3) && counter.count >= 2 && tick >= counter.load)
        return (tick - counter.load + counter.phase) / counter.count;

    return tick / k_pit_refresh_ticks;
}

// Machine clock until which port 61h reads the same: the next refresh toggle, the next tick while OUT2 can move
u64 PIT::GetSpeakerStableUntil(u64 clocks) const
{
    u64 tick = GetTick(clocks);
    const I8254::I8254_Counter& speaker = m_pit.GetState()->counters[k_pit_buzzer_channel];
    bool fixed = !speaker.counting || (!speaker.gate && speaker.mode != 1 && speaker.mode != 5);

    if (!fixed)
        return GetTickClocks(tick + 1);

    const I8254::I8254_Counter& counter = m_pit.GetState()->counters[k_pit_refresh_channel];
    u64 next = (tick / k_pit_refresh_ticks + 1) * k_pit_refresh_ticks;

    if (counter.counting && (counter.mode == 2 || counter.mode == 3) && counter.count >= 2)
    {
        if (tick < counter.load)
            next = counter.load;
        else
            next = counter.load - counter.phase + (GetRefreshEdges(tick) + 1) * counter.count;
    }

    return GetTickClocks(MAX(next, tick + 1));
}

void PIT::UpdateNextEvent()
{
    u64 edge = m_pit.GetNextRisingEdge(k_pit_irq0_channel, m_state.settled_tick);
    m_scheduler->Schedule(SCHEDULER_EVENT_PIT, edge == k_i8254_no_edge ? GDOX_NO_EVENT : GetTickClocks(edge));
}

void PIT::FillTrace(GDOX_Trace_Entry* entry, int counter)
{
    const I8254::I8254_Counter& speaker = m_pit.GetState()->counters[k_pit_buzzer_channel];
    entry->timer.enable = m_state.speaker_gate ? 1 : 0;
    entry->timer.sound = m_state.speaker_data ? 1 : 0;
    entry->timer.speaker_reload = speaker.reload;
    entry->timer.speaker_mode = speaker.mode;

    if (counter >= I8254_COUNTER_COUNT)
        return;

    const I8254::I8254_Counter& state = m_pit.GetState()->counters[counter];
    entry->timer.counter = (u8)counter;
    entry->timer.reload = state.reload;
    entry->timer.access = state.access;
    entry->timer.mode = state.mode;
    entry->timer.bcd = state.bcd ? 1 : 0;
    entry->timer.complete = !state.write_high && state.programmed ? 1 : 0;
}

void PIT::TraceTimeouts(u64 first, u64 last)
{
    int edges = 0;

    for (u64 edge = first; edge <= last && edges < 64; edge = m_pit.GetNextRisingEdge(k_pit_irq0_channel, edge))
    {
        GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_TIMER, TRACE_TIMER_TIMEOUT, GetTickClocks(edge));
        FillTrace(entry, k_pit_irq0_channel);
        entry->timer.value = 0x01;
        entry->timer.edges = (u8)++edges;

        if (edges > 1)
            entry->flags |= TRACE_TIMER_FLAG_COALESCED;
    }
}

void PIT::TraceWrite(u16 port, u8 value)
{
    int index = port & 0x03;
    u8 event = index == 3 ? TRACE_TIMER_CONTROL : TRACE_TIMER_COUNTER;

    if (!m_trace_logger->IsEventEnabled(TRACE_TIMER, event))
        return;

    int counter = event == TRACE_TIMER_CONTROL ? value >> 6 : index;
    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_TIMER, event);
    FillTrace(entry, counter);
    entry->timer.counter = (u8)counter;
    entry->timer.value = value;

    if (event == TRACE_TIMER_COUNTER && !m_pit.GetState()->counters[counter].programmed)
        entry->flags |= TRACE_ENTRY_IGNORED;
}

void PIT::TraceSpeaker()
{
    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_TIMER, TRACE_TIMER_SPEAKER);
    FillTrace(entry, k_pit_buzzer_channel);
    entry->timer.value = (u8)((m_state.speaker_gate ? 0x01 : 0x00) | (m_state.speaker_data ? 0x02 : 0x00));
}

void PIT::TraceRead(int index, u8 value)
{
    const I8254::I8254_Counter& state = m_pit.GetState()->counters[index];
    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_TIMER, TRACE_TIMER_READ);
    FillTrace(entry, index);
    entry->timer.value = value;
    bool high = state.count_latched ? state.latch_high : state.read_high;
    entry->timer.latch = state.access == 3 ? (high ? 1 : 2) : 0;
}

void PIT::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
    m_pit.SaveState(stream);
}

void PIT::LoadState(std::istream& stream, int version)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
    m_pit.LoadState(stream, version);
    SanitizeState();
}

void PIT::Serialize(StateSerializer& serializer)
{
    G_SERIALIZE(serializer, m_state.settled_tick);
    G_SERIALIZE(serializer, m_state.speaker_gate);
    G_SERIALIZE(serializer, m_state.speaker_data);
}

void PIT::SanitizeState()
{
    UpdateNextEvent();
}
