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

#ifndef NET_SWITCH_H
#define NET_SWITCH_H

#include "../common/common.h"
#include "network_port.h"

#define NET_SWITCH_MACHINES 8
#define NET_SWITCH_PORTS (NET_SWITCH_MACHINES + 1)
#define NET_SWITCH_SERVICES 0
#define NET_SWITCH_ADDRESSES 64

class NetServices;

class NetSwitch
{
public:
    typedef void (*NetSwitch_Output)(void* context, int port, const u8* data, int size);

    struct NetSwitch_Duplicate
    {
        u8 address[6];
        int first;
        int second;
    };

    struct NetSwitch_Stats
    {
        u32 frames[NET_SWITCH_PORTS];
        u32 floods;
        u32 dropped;
    };

public:
    NetSwitch();
    ~NetSwitch();
    void Reset();
    void SetOutput(NetSwitch_Output output, void* context);
    NetServices* GetServices();
    void OpenPort(int port);
    void ClosePort(int port);
    bool IsPortOpen(int port) const;
    void Input(int port, const u8* data, int size);
    bool TakeDuplicate(NetSwitch_Duplicate* duplicate);
    const NetSwitch_Stats* GetStats() const;

private:
    struct NetSwitch_Entry
    {
        u8 address[6];
        int port;
    };

    int Lookup(const u8* address) const;
    void Learn(const u8* address, int port);
    void Send(int port, const u8* data, int size);

private:
    NetServices* m_services;
    NetSwitch_Output m_output;
    void* m_output_context;
    bool m_open[NET_SWITCH_PORTS];
    NetSwitch_Entry m_table[NET_SWITCH_ADDRESSES];
    int m_table_count;
    int m_table_next;
    bool m_duplicate_pending;
    NetSwitch_Duplicate m_duplicate;
    NetSwitch_Stats m_stats;
    u8 m_reply[NETWORK_FRAME_MAX];
};

static inline int net_serial_target(int from, int to, const bool* present)
{
    int machines = 0;
    int other = -1;

    for (int i = 1; i < NET_SWITCH_PORTS; i++)
    {
        if (!present[i])
            continue;

        machines++;

        if (i != from)
            other = i;
    }

    if (to == 0)
        return machines == 2 ? other : -1;

    return (to < NET_SWITCH_PORTS) && present[to] && (to != from) ? to : -1;
}

#endif /* NET_SWITCH_H */
