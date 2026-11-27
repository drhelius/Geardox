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

#include "net_services.h"
#include "net_crc.h"
#include "network_port.h"

static const int k_ethernet_header = 14;
static const u16 k_type_ip = 0x0800;
static const u16 k_type_arp = 0x0806;
static const u8 k_ip_icmp = 1;
static const u8 k_ip_udp = 17;
static const u16 k_dhcp_server_port = 67;
static const u16 k_dhcp_client_port = 68;
static const u32 k_dhcp_cookie = 0x63825363;
static const int k_dhcp_fixed = 236;
static const int k_dhcp_minimum = 300;

enum NetServices_DHCP
{
    DHCP_DISCOVER = 1,
    DHCP_OFFER = 2,
    DHCP_REQUEST = 3,
    DHCP_DECLINE = 4,
    DHCP_ACK = 5,
    DHCP_NAK = 6,
    DHCP_RELEASE = 7,
    DHCP_INFORM = 8
};

static inline u16 read16(const u8* data)
{
    return (u16)((data[0] << 8) | data[1]);
}

static inline u32 read32(const u8* data)
{
    return ((u32)data[0] << 24) | ((u32)data[1] << 16) | ((u32)data[2] << 8) | data[3];
}

static inline void write16(u8* data, u16 value)
{
    data[0] = (u8)(value >> 8);
    data[1] = (u8)value;
}

static inline void write32(u8* data, u32 value)
{
    data[0] = (u8)(value >> 24);
    data[1] = (u8)(value >> 16);
    data[2] = (u8)(value >> 8);
    data[3] = (u8)value;
}

NetServices::NetServices()
{
    Reset();
}

NetServices::~NetServices()
{
}

void NetServices::Reset()
{
    memset(m_leases, 0, sizeof(m_leases));
    memset(&m_stats, 0, sizeof(m_stats));
    m_lease_count = 0;
    m_ip_id = 1;
}

const u8* NetServices::GetAddress() const
{
    return k_net_server_mac;
}

u32 NetServices::GetIP() const
{
    return k_net_server_ip;
}

int NetServices::Process(const u8* frame, int size, u8* reply)
{
    if (size < k_ethernet_header)
        return 0;

    bool broadcast = (frame[0] & 0x01) != 0;

    if (!broadcast && (memcmp(frame, k_net_server_mac, 6) != 0))
        return 0;

    u16 type = read16(frame + 12);

    if (type == k_type_arp)
        return ProcessARP(frame, size, reply);

    if (type == k_type_ip)
        return ProcessIP(frame, size, reply);

    return 0;
}

int NetServices::ProcessARP(const u8* frame, int size, u8* reply)
{
    const u8* arp = frame + k_ethernet_header;

    if ((size < k_ethernet_header + 28) || (read16(arp) != 1) || (read16(arp + 2) != k_type_ip) || (arp[4] != 6) ||
        (arp[5] != 4) || (read16(arp + 6) != 1) || (read32(arp + 24) != k_net_server_ip))
        return 0;

    memcpy(reply, frame + 6, 6);
    memcpy(reply + 6, k_net_server_mac, 6);
    write16(reply + 12, k_type_arp);

    u8* out = reply + k_ethernet_header;
    write16(out, 1);
    write16(out + 2, k_type_ip);
    out[4] = 6;
    out[5] = 4;
    write16(out + 6, 2);
    memcpy(out + 8, k_net_server_mac, 6);
    write32(out + 14, k_net_server_ip);
    memcpy(out + 18, arp + 8, 10);

    int length = k_ethernet_header + 28;
    memset(reply + length, 0, 60 - length);
    m_stats.arp++;
    return 60;
}

int NetServices::ProcessIP(const u8* frame, int size, u8* reply)
{
    const u8* ip = frame + k_ethernet_header;

    if ((size < k_ethernet_header + 20) || ((ip[0] >> 4) != 4))
        return 0;

    int header = (ip[0] & 0x0F) * 4;
    int total = read16(ip + 2);

    if ((header < 20) || (total < header) || (k_ethernet_header + total > size))
        return 0;

    if ((read16(ip + 6) & 0x3FFF) != 0)
        return 0;

    u32 destination = read32(ip + 16);

    if ((ip[9] == k_ip_icmp) && (destination == k_net_server_ip))
        return ProcessEcho(frame, k_ethernet_header + total, header, reply);

    if ((ip[9] == k_ip_udp) && ((destination == k_net_server_ip) || (destination == 0xFFFFFFFF)))
        return ProcessDHCP(frame, k_ethernet_header + total, header, reply);

    return 0;
}

int NetServices::ProcessEcho(const u8* frame, int size, int header, u8* reply)
{
    const u8* ip = frame + k_ethernet_header;
    const u8* icmp = ip + header;
    int length = size - k_ethernet_header - header;

    if ((length < 8) || (icmp[0] != 8) || (net_checksum(icmp, length) != 0))
        return 0;

    if (k_ethernet_header + 20 + length > NETWORK_FRAME_MAX)
        return 0;

    u8* out = reply + k_ethernet_header + 20;
    memcpy(out, icmp, length);
    out[0] = 0;
    out[2] = 0;
    out[3] = 0;
    write16(out + 2, net_checksum(out, length));

    m_stats.echo++;
    return BuildIP(reply, frame + 6, read32(ip + 12), k_ip_icmp, length);
}

int NetServices::ProcessDHCP(const u8* frame, int size, int header, u8* reply)
{
    const u8* udp = frame + k_ethernet_header + header;
    int udp_length = size - k_ethernet_header - header;

    if ((udp_length < 8) || (read16(udp) != k_dhcp_client_port) || (read16(udp + 2) != k_dhcp_server_port))
        return 0;

    const u8* bootp = udp + 8;
    int length = MIN((int)read16(udp + 4), udp_length) - 8;

    if ((length < k_dhcp_fixed + 4) || (bootp[0] != 1) || (bootp[1] != 1) || (bootp[2] != 6) ||
        (read32(bootp + k_dhcp_fixed) != k_dhcp_cookie))
        return 0;

    int type = 0;
    u32 requested = 0;
    u32 server = 0;

    for (int i = k_dhcp_fixed + 4; i < length;)
    {
        u8 option = bootp[i];

        if (option == 255)
            break;

        if (option == 0)
        {
            i++;
            continue;
        }

        if (i + 1 >= length)
            break;

        int option_length = bootp[i + 1];
        const u8* value = bootp + i + 2;

        if (i + 2 + option_length > length)
            break;

        if ((option == 53) && (option_length >= 1))
            type = value[0];
        else if ((option == 50) && (option_length >= 4))
            requested = read32(value);
        else if ((option == 54) && (option_length >= 4))
            server = read32(value);

        i += 2 + option_length;
    }

    const u8* client = bootp + 28;
    u32 ciaddr = read32(bootp + 12);
    int answer = 0;
    int lease = -1;

    switch (type)
    {
        case DHCP_DISCOVER:
            lease = FindLease(client, true);
            answer = lease >= 0 ? DHCP_OFFER : 0;
            break;
        case DHCP_REQUEST:
        {
            if ((server != 0) && (server != k_net_server_ip))
                return 0;

            lease = FindLease(client, true);
            u32 wanted = requested != 0 ? requested : ciaddr;

            if ((lease >= 0) && ((wanted == 0) || (wanted == m_leases[lease].ip)))
            {
                m_leases[lease].bound = true;
                answer = DHCP_ACK;
            }
            else
                answer = DHCP_NAK;
            break;
        }
        case DHCP_INFORM:
            answer = DHCP_ACK;
            break;
        case DHCP_RELEASE:
        case DHCP_DECLINE:
            lease = FindLease(client, false);

            if (lease >= 0)
                m_leases[lease].bound = false;

            return 0;
        default:
            return 0;
    }

    if (answer == 0)
        return 0;

    u8* out = reply + k_ethernet_header + 20 + 8;
    memset(out, 0, k_dhcp_minimum);
    out[0] = 2;
    out[1] = 1;
    out[2] = 6;
    memcpy(out + 4, bootp + 4, 4);
    memcpy(out + 10, bootp + 10, 2);

    if (type == DHCP_INFORM)
        memcpy(out + 12, bootp + 12, 4);
    else if (answer != DHCP_NAK)
        write32(out + 16, m_leases[lease].ip);

    write32(out + 20, k_net_server_ip);
    memcpy(out + 28, client, 16);
    write32(out + k_dhcp_fixed, k_dhcp_cookie);

    u8* option = out + k_dhcp_fixed + 4;
    *option++ = 53;
    *option++ = 1;
    *option++ = (u8)answer;
    *option++ = 54;
    *option++ = 4;
    write32(option, k_net_server_ip);
    option += 4;

    if (answer != DHCP_NAK)
    {
        *option++ = 1;
        *option++ = 4;
        write32(option, k_net_netmask);
        option += 4;

        if (type != DHCP_INFORM)
        {
            *option++ = 51;
            *option++ = 4;
            write32(option, k_net_lease_seconds);
            option += 4;
            *option++ = 58;
            *option++ = 4;
            write32(option, k_net_lease_seconds / 2);
            option += 4;
            *option++ = 59;
            *option++ = 4;
            write32(option, k_net_lease_seconds * 7 / 8);
            option += 4;
        }
    }

    *option++ = 255;

    int bootp_length = MAX((int)(option - out), k_dhcp_minimum);
    u8* reply_udp = reply + k_ethernet_header + 20;
    write16(reply_udp, k_dhcp_server_port);
    write16(reply_udp + 2, k_dhcp_client_port);
    write16(reply_udp + 4, (u16)(8 + bootp_length));
    write16(reply_udp + 6, 0);

    static const u8 k_broadcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    m_stats.dhcp++;
    int result = BuildIP(reply, k_broadcast, 0xFFFFFFFF, k_ip_udp, 8 + bootp_length);

    u32 sum = 0;
    sum += (k_net_server_ip >> 16) + (k_net_server_ip & 0xFFFF);
    sum += 0xFFFF + 0xFFFF;
    sum += k_ip_udp;
    sum += (u32)(8 + bootp_length);
    u16 checksum = net_checksum(reply_udp, 8 + bootp_length, sum);
    write16(reply_udp + 6, checksum == 0 ? 0xFFFF : checksum);
    return result;
}

int NetServices::FindLease(const u8* address, bool create)
{
    for (int i = 0; i < m_lease_count; i++)
    {
        if (memcmp(m_leases[i].address, address, 6) == 0)
            return i;
    }

    if (!create || (m_lease_count >= NET_SERVICES_LEASES))
        return -1;

    NetServices_Lease& lease = m_leases[m_lease_count];
    memcpy(lease.address, address, 6);
    lease.ip = k_net_lease_first + (u32)m_lease_count;
    lease.bound = false;
    return m_lease_count++;
}

int NetServices::BuildIP(u8* reply, const u8* destination_mac, u32 destination_ip, u8 protocol, int payload)
{
    memcpy(reply, destination_mac, 6);
    memcpy(reply + 6, k_net_server_mac, 6);
    write16(reply + 12, k_type_ip);

    u8* ip = reply + k_ethernet_header;
    ip[0] = 0x45;
    ip[1] = 0;
    write16(ip + 2, (u16)(20 + payload));
    write16(ip + 4, m_ip_id++);
    write16(ip + 6, 0);
    ip[8] = 64;
    ip[9] = protocol;
    write16(ip + 10, 0);
    write32(ip + 12, k_net_server_ip);
    write32(ip + 16, destination_ip);
    write16(ip + 10, net_checksum(ip, 20));

    int length = k_ethernet_header + 20 + payload;

    if (length < 60)
    {
        memset(reply + length, 0, 60 - length);
        length = 60;
    }

    return length;
}

const NetServices::NetServices_Lease* NetServices::GetLeases(int* count) const
{
    *count = m_lease_count;
    return m_leases;
}

const NetServices::NetServices_Stats* NetServices::GetStats() const
{
    return &m_stats;
}
