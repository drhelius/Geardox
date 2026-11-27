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

#include "network_port.h"

NetworkFrameQueue::NetworkFrameQueue()
{
    InitPointer(m_data);
    m_head = 0;
    m_count = 0;
    m_bytes = 0;
    m_dropped = 0;
    memset(m_sizes, 0, sizeof(m_sizes));
    memset(m_clocks, 0, sizeof(m_clocks));
}

NetworkFrameQueue::~NetworkFrameQueue()
{
    SafeDeleteArray(m_data);
}

void NetworkFrameQueue::Clear()
{
    m_head = 0;
    m_count = 0;
    m_bytes = 0;
}

bool NetworkFrameQueue::Push(const u8* data, int size, u64 clocks)
{
    if ((size <= 0) || (size > NETWORK_FRAME_MAX))
        return false;

    if (!IsValidPointer(m_data))
    {
        m_data = new u8[NETWORK_QUEUE_FRAMES * NETWORK_FRAME_MAX];

        if (!IsValidPointer(m_data))
            return false;
    }

    bool kept = true;

    while ((m_count > 0) && ((m_count >= NETWORK_QUEUE_FRAMES) || (m_bytes + (u32)size > NETWORK_QUEUE_BYTES)))
    {
        Pop();
        m_dropped++;
        kept = false;
    }

    int slot = (m_head + m_count) % NETWORK_QUEUE_FRAMES;
    memcpy(m_data + slot * NETWORK_FRAME_MAX, data, size);
    m_sizes[slot] = (u16)size;
    m_clocks[slot] = clocks;
    m_count++;
    m_bytes += (u32)size;
    return kept;
}

const u8* NetworkFrameQueue::Front(int* size, u64* clocks) const
{
    if (m_count == 0)
    {
        *size = 0;
        return NULL;
    }

    *size = m_sizes[m_head];

    if (IsValidPointer(clocks))
        *clocks = m_clocks[m_head];

    return m_data + m_head * NETWORK_FRAME_MAX;
}

void NetworkFrameQueue::Pop()
{
    if (m_count == 0)
        return;

    m_bytes -= m_sizes[m_head];
    m_head = (m_head + 1) % NETWORK_QUEUE_FRAMES;
    m_count--;
}

int NetworkFrameQueue::GetCount() const
{
    return m_count;
}

u32 NetworkFrameQueue::GetBytes() const
{
    return m_bytes;
}

u32 NetworkFrameQueue::GetDropped() const
{
    return m_dropped;
}

void NetworkFrameQueue::ResetDropped()
{
    m_dropped = 0;
}

NetworkPort::NetworkPort()
{
    memset(&m_stats, 0, sizeof(m_stats));
    m_speculative = false;
    m_attached = false;
}

NetworkPort::~NetworkPort()
{
}

void NetworkPort::Clear()
{
    m_received.Clear();
    m_sent.Clear();
}

void NetworkPort::SetSpeculative(bool speculative)
{
    m_speculative = speculative;
}

bool NetworkPort::IsSpeculative() const
{
    return m_speculative;
}

void NetworkPort::SetAttached(bool attached)
{
    m_attached = attached;

    if (!attached)
        Clear();
}

bool NetworkPort::IsAttached() const
{
    return m_attached;
}

bool NetworkPort::Deliver(const u8* data, int size)
{
    if (!m_attached)
        return false;

    if ((size < NETWORK_FRAME_MIN) || (size > NETWORK_FRAME_MAX))
    {
        m_stats.oversized++;
        return false;
    }

    if (!m_received.Push(data, size, 0))
        m_stats.dropped_received = m_received.GetDropped();

    return true;
}

const u8* NetworkPort::PeekReceived(int* size) const
{
    if (m_speculative)
    {
        *size = 0;
        return NULL;
    }

    return m_received.Front(size);
}

void NetworkPort::PopReceived()
{
    if (m_speculative || (m_received.GetCount() == 0))
        return;

    m_received.Pop();
    m_stats.received++;
}

bool NetworkPort::HasReceived() const
{
    return !m_speculative && (m_received.GetCount() > 0);
}

void NetworkPort::Send(const u8* data, int size, u64 clocks)
{
    if (m_speculative || !m_attached)
        return;

    if ((size < NETWORK_FRAME_MIN) || (size > NETWORK_FRAME_MAX))
    {
        m_stats.oversized++;
        return;
    }

    if (!m_sent.Push(data, size, clocks))
        m_stats.dropped_sent = m_sent.GetDropped();

    m_stats.sent++;
}

const u8* NetworkPort::PeekSent(int* size, u64* clocks) const
{
    return m_sent.Front(size, clocks);
}

void NetworkPort::PopSent()
{
    m_sent.Pop();
}

int NetworkPort::GetReceivedCount() const
{
    return m_received.GetCount();
}

int NetworkPort::GetSentCount() const
{
    return m_sent.GetCount();
}

const NetworkPort::NetworkPort_Stats* NetworkPort::GetStats() const
{
    return &m_stats;
}
