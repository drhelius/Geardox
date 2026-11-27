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

#include "net_switch.h"
#include "net_services.h"

NetSwitch::NetSwitch()
{
    m_services = new NetServices();
    m_output = NULL;
    m_output_context = NULL;
    Reset();
}

NetSwitch::~NetSwitch()
{
    SafeDelete(m_services);
}

void NetSwitch::Reset()
{
    for (int i = 0; i < NET_SWITCH_PORTS; i++)
        m_open[i] = false;

    m_open[NET_SWITCH_SERVICES] = true;
    memset(m_table, 0, sizeof(m_table));
    memset(&m_stats, 0, sizeof(m_stats));
    memset(&m_duplicate, 0, sizeof(m_duplicate));
    m_table_count = 0;
    m_table_next = 0;
    m_duplicate_pending = false;
    m_services->Reset();
}

void NetSwitch::SetOutput(NetSwitch_Output output, void* context)
{
    m_output = output;
    m_output_context = context;
}

NetServices* NetSwitch::GetServices()
{
    return m_services;
}

void NetSwitch::OpenPort(int port)
{
    if ((port > 0) && (port < NET_SWITCH_PORTS))
        m_open[port] = true;
}

void NetSwitch::ClosePort(int port)
{
    if ((port <= 0) || (port >= NET_SWITCH_PORTS))
        return;

    m_open[port] = false;

    for (int i = 0; i < m_table_count; i++)
    {
        if (m_table[i].port == port)
            m_table[i].port = -1;
    }
}

bool NetSwitch::IsPortOpen(int port) const
{
    return (port >= 0) && (port < NET_SWITCH_PORTS) && m_open[port];
}

void NetSwitch::Input(int port, const u8* data, int size)
{
    if (!IsPortOpen(port) || (size < NETWORK_FRAME_MIN) || (size > NETWORK_FRAME_MAX))
    {
        m_stats.dropped++;
        return;
    }

    m_stats.frames[port]++;

    const u8* destination = data;
    const u8* source = data + 6;

    if (!(source[0] & 0x01))
        Learn(source, port);

    if (!(destination[0] & 0x01))
    {
        int target = Lookup(destination);

        if (target == NET_SWITCH_SERVICES)
        {
            int length = m_services->Process(data, size, m_reply);

            if (length > 0)
                Input(NET_SWITCH_SERVICES, m_reply, length);

            return;
        }

        if ((target > 0) && (target != port))
        {
            Send(target, data, size);
            return;
        }

        if (target == port)
            return;
    }

    m_stats.floods++;

    for (int i = 1; i < NET_SWITCH_PORTS; i++)
    {
        if ((i != port) && m_open[i])
            Send(i, data, size);
    }

    if (port != NET_SWITCH_SERVICES)
    {
        int length = m_services->Process(data, size, m_reply);

        if (length > 0)
            Input(NET_SWITCH_SERVICES, m_reply, length);
    }
}

bool NetSwitch::TakeDuplicate(NetSwitch_Duplicate* duplicate)
{
    if (!m_duplicate_pending)
        return false;

    *duplicate = m_duplicate;
    m_duplicate_pending = false;
    return true;
}

const NetSwitch::NetSwitch_Stats* NetSwitch::GetStats() const
{
    return &m_stats;
}

int NetSwitch::Lookup(const u8* address) const
{
    if (memcmp(address, m_services->GetAddress(), 6) == 0)
        return NET_SWITCH_SERVICES;

    for (int i = 0; i < m_table_count; i++)
    {
        if (memcmp(m_table[i].address, address, 6) == 0)
            return m_table[i].port;
    }

    return -1;
}

void NetSwitch::Learn(const u8* address, int port)
{
    if (port == NET_SWITCH_SERVICES)
        return;

    for (int i = 0; i < m_table_count; i++)
    {
        NetSwitch_Entry& entry = m_table[i];

        if (memcmp(entry.address, address, 6) != 0)
            continue;

        if ((entry.port > 0) && (entry.port != port) && m_open[entry.port])
        {
            memcpy(m_duplicate.address, address, 6);
            m_duplicate.first = entry.port;
            m_duplicate.second = port;
            m_duplicate_pending = true;
        }

        entry.port = port;
        return;
    }

    int slot = m_table_count < NET_SWITCH_ADDRESSES ? m_table_count++ : m_table_next;
    m_table_next = (m_table_next + 1) % NET_SWITCH_ADDRESSES;
    memcpy(m_table[slot].address, address, 6);
    m_table[slot].port = port;
}

void NetSwitch::Send(int port, const u8* data, int size)
{
    if (IsValidPointer(m_output))
        m_output(m_output_context, port, data, size);
}
