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

#ifndef NET_SERVICES_H
#define NET_SERVICES_H

#include "../common/common.h"

#define NET_SERVICES_LEASES 32

class NetServices
{
public:
    struct NetServices_Lease
    {
        u8 address[6];
        u32 ip;
        bool bound;
        u32 used;
    };

    struct NetServices_Stats
    {
        u32 arp;
        u32 echo;
        u32 dhcp;
    };

public:
    NetServices();
    ~NetServices();
    void Reset();
    const u8* GetAddress() const;
    u32 GetIP() const;
    int Process(const u8* frame, int size, u8* reply);
    const NetServices_Lease* GetLeases(int* count) const;
    const NetServices_Stats* GetStats() const;

private:
    int ProcessARP(const u8* frame, int size, u8* reply);
    int ProcessIP(const u8* frame, int size, u8* reply);
    int ProcessEcho(const u8* frame, int size, int header, u8* reply);
    int ProcessDHCP(const u8* frame, int size, int header, u8* reply);
    int FindLease(const u8* address, bool create);
    int BuildIP(u8* reply, const u8* destination_mac, u32 destination_ip, u8 protocol, int payload);

private:
    NetServices_Lease m_leases[NET_SERVICES_LEASES];
    int m_lease_count;
    u32 m_lease_clock;
    NetServices_Stats m_stats;
    u16 m_ip_id;
};

static const u8 k_net_server_mac[6] = { 0x02, 0x47, 0x44, 0x58, 0x00, 0x01 };
static const u32 k_net_server_ip = 0x0A440001;
static const u32 k_net_netmask = 0xFFFFFF00;
static const u32 k_net_lease_first = 0x0A440064;
static const u32 k_net_lease_seconds = 3600;

#endif /* NET_SERVICES_H */
