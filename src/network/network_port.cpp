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

NetworkSerialQueue::NetworkSerialQueue()
{
    Clear();
}

void NetworkSerialQueue::Clear()
{
    m_head = 0;
    m_size = 0;
    m_tail = 0;
    m_tail_valid = false;
}

bool NetworkSerialQueue::Push(int channel, int kind, int peer, const u8* data, int size)
{
    if ((size < 0) || (size > NETWORK_SERIAL_RECORD) || (m_size + size > NETWORK_SERIAL_QUEUE))
        return false;

    if ((kind == NETWORK_SERIAL_DATA) && m_tail_valid && (m_data[m_tail] == (u8)channel) &&
        (m_data[(m_tail + 1) % NETWORK_SERIAL_QUEUE] == (u8)kind) && (m_data[(m_tail + 2) % NETWORK_SERIAL_QUEUE] == (u8)peer))
    {
        int tail_size = m_data[(m_tail + 3) % NETWORK_SERIAL_QUEUE] | (m_data[(m_tail + 4) % NETWORK_SERIAL_QUEUE] << 8);

        if (tail_size + size <= NETWORK_SERIAL_RECORD)
        {
            Write(data, size);
            tail_size += size;
            m_data[(m_tail + 3) % NETWORK_SERIAL_QUEUE] = (u8)tail_size;
            m_data[(m_tail + 4) % NETWORK_SERIAL_QUEUE] = (u8)(tail_size >> 8);
            return true;
        }
    }

    if (m_size + 5 + size > NETWORK_SERIAL_QUEUE)
        return false;

    u8 header[5] = { (u8)channel, (u8)kind, (u8)peer, (u8)size, (u8)(size >> 8) };
    m_tail = (m_head + m_size) % NETWORK_SERIAL_QUEUE;
    m_tail_valid = true;
    Write(header, 5);

    if (size > 0)
        Write(data, size);

    return true;
}

bool NetworkSerialQueue::Pop(int* channel, int* kind, int* peer, u8* data, int* size)
{
    if (m_size < 5)
        return false;

    if (m_head == m_tail)
        m_tail_valid = false;

    u8 header[5];
    Read(header, 5);
    *channel = header[0];
    *kind = header[1];
    *peer = header[2];
    *size = header[3] | (header[4] << 8);

    if (*size > 0)
        Read(data, *size);

    return true;
}

bool NetworkSerialQueue::IsEmpty() const
{
    return m_size == 0;
}

int NetworkSerialQueue::GetFree() const
{
    return NETWORK_SERIAL_QUEUE - m_size;
}

void NetworkSerialQueue::Write(const u8* data, int size)
{
    for (int i = 0; i < size; i++)
        m_data[(m_head + m_size + i) % NETWORK_SERIAL_QUEUE] = data[i];

    m_size += size;
}

void NetworkSerialQueue::Read(u8* data, int size)
{
    for (int i = 0; i < size; i++)
        data[i] = m_data[(m_head + i) % NETWORK_SERIAL_QUEUE];

    m_head = (m_head + size) % NETWORK_SERIAL_QUEUE;
    m_size -= size;
}

NetworkPort::NetworkPort()
{
    memset(&m_stats, 0, sizeof(m_stats));
    m_speculative = false;
    m_attached = false;
    m_serial_attached[0] = false;
    m_serial_attached[1] = false;
}

NetworkPort::~NetworkPort()
{
}

void NetworkPort::Clear()
{
    m_received.Clear();
    m_sent.Clear();
    m_serial_sent.Clear();
    m_serial_received.Clear();
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

void NetworkPort::SetSerialAttached(int channel, bool attached)
{
    m_serial_attached[channel & 1] = attached;
}

bool NetworkPort::IsSerialAttached(int channel) const
{
    return m_serial_attached[channel & 1];
}

bool NetworkPort::HasSerial() const
{
    return m_serial_attached[0] || m_serial_attached[1];
}

bool NetworkPort::SendSerial(int channel, int kind, int peer, const u8* data, int size)
{
    if (m_speculative)
        return true;

    return m_serial_sent.Push(channel, kind, peer, data, size);
}

bool NetworkPort::TakeSentSerial(int* channel, int* kind, int* peer, u8* data, int* size)
{
    return m_serial_sent.Pop(channel, kind, peer, data, size);
}

bool NetworkPort::DeliverSerial(int channel, int kind, int peer, const u8* data, int size)
{
    if ((channel < 0) || (channel > 1) || !m_serial_attached[channel])
        return false;

    return m_serial_received.Push(channel, kind, peer, data, size);
}

bool NetworkPort::TakeReceivedSerial(int* channel, int* kind, int* peer, u8* data, int* size)
{
    if (m_speculative)
        return false;

    return m_serial_received.Pop(channel, kind, peer, data, size);
}

int NetworkPort::GetSerialSendFree() const
{
    return m_serial_sent.GetFree();
}

void NetworkPort::DeliverSerialDisconnect(int machine)
{
    u8 lines = 0;

    for (int channel = 0; channel < 2; channel++)
    {
        DeliverSerial(channel, NETWORK_SERIAL_HANGUP, machine, NULL, 0);
        DeliverSerial(channel, NETWORK_SERIAL_LINES, machine, &lines, 1);
    }
}

void NetworkPort::DeliverSerialResync()
{
    for (int channel = 0; channel < 2; channel++)
        DeliverSerial(channel, NETWORK_SERIAL_RESYNC, 0, NULL, 0);
}
