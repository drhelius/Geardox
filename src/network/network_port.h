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

#ifndef NETWORK_PORT_H
#define NETWORK_PORT_H

#include "../common/common.h"

#define NETWORK_FRAME_MIN 14
#define NETWORK_FRAME_MAX 1518
#define NETWORK_QUEUE_FRAMES 1024
#define NETWORK_QUEUE_BYTES (1024 * 1024)
#define NETWORK_SERIAL_QUEUE 16384
#define NETWORK_SERIAL_RECORD 1024

enum Network_Serial_Kind
{
    NETWORK_SERIAL_DATA = 1,
    NETWORK_SERIAL_LINES,
    NETWORK_SERIAL_CALL,
    NETWORK_SERIAL_ANSWER,
    NETWORK_SERIAL_HANGUP
};

class NetworkSerialQueue
{
public:
    NetworkSerialQueue();
    void Clear();
    bool Push(int channel, int kind, int peer, const u8* data, int size);
    bool Pop(int* channel, int* kind, int* peer, u8* data, int* size);
    bool IsEmpty() const;
    int GetFree() const;

private:
    void Write(const u8* data, int size);
    void Read(u8* data, int size);

private:
    u8 m_data[NETWORK_SERIAL_QUEUE];
    int m_head;
    int m_size;
};

class NetworkFrameQueue
{
public:
    NetworkFrameQueue();
    ~NetworkFrameQueue();
    void Clear();
    bool Push(const u8* data, int size, u64 clocks);
    const u8* Front(int* size, u64* clocks = NULL) const;
    void Pop();
    int GetCount() const;
    u32 GetBytes() const;
    u32 GetDropped() const;
    void ResetDropped();

private:
    u8* m_data;
    u16 m_sizes[NETWORK_QUEUE_FRAMES];
    u64 m_clocks[NETWORK_QUEUE_FRAMES];
    int m_head;
    int m_count;
    u32 m_bytes;
    u32 m_dropped;
};

class NetworkPort
{
public:
    struct NetworkPort_Stats
    {
        u32 sent;
        u32 received;
        u32 dropped_sent;
        u32 dropped_received;
        u32 oversized;
    };

public:
    NetworkPort();
    ~NetworkPort();
    void Clear();
    void SetSpeculative(bool speculative);
    bool IsSpeculative() const;
    void SetAttached(bool attached);
    bool IsAttached() const;
    bool Deliver(const u8* data, int size);
    const u8* PeekReceived(int* size) const;
    void PopReceived();
    bool HasReceived() const;
    void Send(const u8* data, int size, u64 clocks);
    const u8* PeekSent(int* size, u64* clocks = NULL) const;
    void PopSent();
    int GetReceivedCount() const;
    int GetSentCount() const;
    const NetworkPort_Stats* GetStats() const;
    void SetSerialAttached(int channel, bool attached);
    bool IsSerialAttached(int channel) const;
    bool HasSerial() const;
    bool SendSerial(int channel, int kind, int peer, const u8* data, int size);
    bool TakeSentSerial(int* channel, int* kind, int* peer, u8* data, int* size);
    bool DeliverSerial(int channel, int kind, int peer, const u8* data, int size);
    bool TakeReceivedSerial(int* channel, int* kind, int* peer, u8* data, int* size);
    int GetSerialSendFree() const;

private:
    NetworkFrameQueue m_received;
    NetworkFrameQueue m_sent;
    NetworkPort_Stats m_stats;
    bool m_speculative;
    bool m_attached;
    bool m_serial_attached[2];
    NetworkSerialQueue m_serial_sent;
    NetworkSerialQueue m_serial_received;
};

#endif /* NETWORK_PORT_H */
