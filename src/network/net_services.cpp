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

static u16 ip_checksum(const u8* data, int size, u32 sum = 0)
{
    for (int i = 0; i + 1 < size; i += 2)
        sum += read_u16_be(data + i);

    if (size & 1)
        sum += (u32)(data[size - 1] << 8);

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return (u16)~sum;
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

    u16 type = read_u16_be(frame + 12);

    if (type == k_type_arp)
        return ProcessARP(frame, size, reply);

    if (type == k_type_ip)
        return ProcessIP(frame, size, reply);

    return 0;
}

int NetServices::ProcessARP(const u8* frame, int size, u8* reply)
{
    const u8* arp = frame + k_ethernet_header;

    if ((size < k_ethernet_header + 28) || (read_u16_be(arp) != 1) || (read_u16_be(arp + 2) != k_type_ip) || (arp[4] != 6) ||
        (arp[5] != 4) || (read_u16_be(arp + 6) != 1) || (read_u32_be(arp + 24) != k_net_server_ip))
        return 0;

    memcpy(reply, frame + 6, 6);
    memcpy(reply + 6, k_net_server_mac, 6);
    write_u16_be(reply + 12, k_type_arp);

    u8* out = reply + k_ethernet_header;
    write_u16_be(out, 1);
    write_u16_be(out + 2, k_type_ip);
    out[4] = 6;
    out[5] = 4;
    write_u16_be(out + 6, 2);
    memcpy(out + 8, k_net_server_mac, 6);
    write_u32_be(out + 14, k_net_server_ip);
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
    int total = read_u16_be(ip + 2);

    if ((header < 20) || (total < header) || (k_ethernet_header + total > size))
        return 0;

    if ((read_u16_be(ip + 6) & 0x3FFF) != 0)
        return 0;

    u32 destination = read_u32_be(ip + 16);

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

    if ((length < 8) || (icmp[0] != 8) || (ip_checksum(icmp, length) != 0))
        return 0;

    if (k_ethernet_header + 20 + length > NETWORK_FRAME_MAX)
        return 0;

    u8* out = reply + k_ethernet_header + 20;
    memcpy(out, icmp, length);
    out[0] = 0;
    out[2] = 0;
    out[3] = 0;
    write_u16_be(out + 2, ip_checksum(out, length));

    m_stats.echo++;
    return BuildIP(reply, frame + 6, read_u32_be(ip + 12), k_ip_icmp, length);
}

int NetServices::ProcessDHCP(const u8* frame, int size, int header, u8* reply)
{
    const u8* udp = frame + k_ethernet_header + header;
    int udp_length = size - k_ethernet_header - header;

    if ((udp_length < 8) || (read_u16_be(udp) != k_dhcp_client_port) || (read_u16_be(udp + 2) != k_dhcp_server_port))
        return 0;

    const u8* bootp = udp + 8;
    int length = MIN((int)read_u16_be(udp + 4), udp_length) - 8;

    if ((length < k_dhcp_fixed + 4) || (bootp[0] != 1) || (bootp[1] != 1) || (bootp[2] != 6) ||
        (read_u32_be(bootp + k_dhcp_fixed) != k_dhcp_cookie))
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
            requested = read_u32_be(value);
        else if ((option == 54) && (option_length >= 4))
            server = read_u32_be(value);

        i += 2 + option_length;
    }

    const u8* client = bootp + 28;
    u32 ciaddr = read_u32_be(bootp + 12);
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
        write_u32_be(out + 16, m_leases[lease].ip);

    write_u32_be(out + 20, k_net_server_ip);
    memcpy(out + 28, client, 16);
    write_u32_be(out + k_dhcp_fixed, k_dhcp_cookie);

    u8* option = out + k_dhcp_fixed + 4;
    *option++ = 53;
    *option++ = 1;
    *option++ = (u8)answer;
    *option++ = 54;
    *option++ = 4;
    write_u32_be(option, k_net_server_ip);
    option += 4;

    if (answer != DHCP_NAK)
    {
        *option++ = 1;
        *option++ = 4;
        write_u32_be(option, k_net_netmask);
        option += 4;

        if (type != DHCP_INFORM)
        {
            *option++ = 51;
            *option++ = 4;
            write_u32_be(option, k_net_lease_seconds);
            option += 4;
            *option++ = 58;
            *option++ = 4;
            write_u32_be(option, k_net_lease_seconds / 2);
            option += 4;
            *option++ = 59;
            *option++ = 4;
            write_u32_be(option, k_net_lease_seconds * 7 / 8);
            option += 4;
        }
    }

    *option++ = 255;

    int bootp_length = MAX((int)(option - out), k_dhcp_minimum);
    u8* reply_udp = reply + k_ethernet_header + 20;
    write_u16_be(reply_udp, k_dhcp_server_port);
    write_u16_be(reply_udp + 2, k_dhcp_client_port);
    write_u16_be(reply_udp + 4, (u16)(8 + bootp_length));
    write_u16_be(reply_udp + 6, 0);

    static const u8 k_broadcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    m_stats.dhcp++;
    int result = BuildIP(reply, k_broadcast, 0xFFFFFFFF, k_ip_udp, 8 + bootp_length);

    u32 sum = 0;
    sum += (k_net_server_ip >> 16) + (k_net_server_ip & 0xFFFF);
    sum += 0xFFFF + 0xFFFF;
    sum += k_ip_udp;
    sum += (u32)(8 + bootp_length);
    u16 checksum = ip_checksum(reply_udp, 8 + bootp_length, sum);
    write_u16_be(reply_udp + 6, checksum == 0 ? 0xFFFF : checksum);
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
    write_u16_be(reply + 12, k_type_ip);

    u8* ip = reply + k_ethernet_header;
    ip[0] = 0x45;
    ip[1] = 0;
    write_u16_be(ip + 2, (u16)(20 + payload));
    write_u16_be(ip + 4, m_ip_id++);
    write_u16_be(ip + 6, 0);
    ip[8] = 64;
    ip[9] = protocol;
    write_u16_be(ip + 10, 0);
    write_u32_be(ip + 12, k_net_server_ip);
    write_u32_be(ip + 16, destination_ip);
    write_u16_be(ip + 10, ip_checksum(ip, 20));

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
