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

#include "net_session.h"
#include "sha1/sha1.h"
#include "common/random_utils.h"

enum Net_Session_Message
{
    NET_MESSAGE_CHALLENGE = 1,
    NET_MESSAGE_HELLO,
    NET_MESSAGE_WELCOME,
    NET_MESSAGE_REJECT,
    NET_MESSAGE_PEERS,
    NET_MESSAGE_PING,
    NET_MESSAGE_PONG,
    NET_MESSAGE_BYE,
    NET_MESSAGE_DUPLICATE,
    NET_MESSAGE_SERIAL
};

enum Net_Session_Reject
{
    NET_REJECT_VERSION = 1,
    NET_REJECT_PASSWORD,
    NET_REJECT_FULL
};

enum Net_Session_Datagram
{
    NET_DATAGRAM_FRAME = 1,
    NET_DATAGRAM_KEEPALIVE
};

static const u8 k_magic[4] = { 'G', 'D', 'X', 'N' };
static const int k_datagram_header = 14;
static const int k_datagram_payload = 1400;
static const u64 k_keepalive_ms = 1000;
static const u64 k_timeout_ms = 10000;
static const int k_nonce_size = 16;
static const int k_proof_size = 20;

static bool datagram_valid(const u8* data, int size)
{
    if ((size < k_datagram_header) || (size > k_datagram_header + k_datagram_payload) || (data[0] != 'G') ||
        (data[1] != 'N'))
        return false;

    if (data[2] == NET_DATAGRAM_KEEPALIVE)
        return true;

    int parts = data[13];
    return (data[2] == NET_DATAGRAM_FRAME) && (parts >= 1) && (parts <= 2) && (data[12] < parts) &&
        (size > k_datagram_header);
}

static void copy_name(char* name, const u8* data)
{
    memcpy(name, data, NET_SESSION_NAME_SIZE);
    name[NET_SESSION_NAME_SIZE] = 0;

    for (int i = 0; name[i] != 0; i++)
    {
        if ((u8)name[i] < 32)
            name[i] = ' ';
    }
}

NetSession::NetSession()
{
    m_mode = NET_SESSION_OFF;
    m_state = NET_SESSION_IDLE;
    m_listener = NET_SOCKET_INVALID;
    m_udp = NET_SOCKET_INVALID;
    m_port = 0;
    m_host_ip = 0;
    m_machine = 0;
    m_max_machines = NET_SWITCH_MACHINES;
    m_switch = new NetSwitch();
    m_switch->SetOutput(&NetSession::SwitchOutput, this);
    InitPointer(m_local);
    m_event_count = 0;
    m_serial.Clear();
    m_serial_warned = false;
    m_last_keepalive = 0;
    m_dropped = 0;
    memset(m_peers, 0, sizeof(m_peers));
    memset(m_events, 0, sizeof(m_events));

    for (int i = 0; i < NET_SWITCH_PORTS; i++)
        InitPointer(m_links[i]);

    for (int i = 0; i < NET_SWITCH_MACHINES; i++)
        InitPointer(m_pending[i]);
}

NetSession::~NetSession()
{
    Leave();
    SafeDelete(m_switch);
}

bool NetSession::Host(u16 port, const char* name, const char* password, int machines, std::string* error)
{
    Leave();

    if (!net_socket_startup())
    {
        *error = "The network isn't available";
        return false;
    }

    m_listener = net_socket_listen(port);

    if (m_listener == NET_SOCKET_INVALID)
    {
        *error = "Port " + std::to_string(port) + " is in use or blocked";
        return false;
    }

    m_udp = net_socket_udp(port);

    if (m_udp == NET_SOCKET_INVALID)
    {
        net_socket_close(m_listener);
        m_listener = NET_SOCKET_INVALID;
        *error = "UDP port " + std::to_string(port) + " is in use or blocked";
        return false;
    }

    m_mode = NET_SESSION_HOST;
    m_state = NET_SESSION_CONNECTED;
    m_port = port;
    m_name = name;
    m_password = password;
    m_machine = 1;
    m_max_machines = CLAMP(machines, 2, NET_SWITCH_MACHINES);
    m_error.clear();
    m_host_text = "port " + std::to_string(port);
    m_switch->Reset();
    m_switch->OpenPort(1);
    memset(m_peers, 0, sizeof(m_peers));
    m_peers[1].present = true;
    strncpy_fit(m_peers[1].name, name, sizeof(m_peers[1].name));
    m_dropped = 0;
    return true;
}

bool NetSession::Join(const char* host, u16 port, const char* name, const char* password, std::string* error)
{
    Leave();

    if (!net_socket_startup())
    {
        *error = "The network isn't available";
        return false;
    }

    u32 ip = 0;

    if (!net_socket_resolve(host, &ip))
    {
        *error = std::string("Can't find ") + host;
        return false;
    }

    Net_Session_Link* link = new Net_Session_Link();
    ResetLink(*link);
    link->tcp = net_socket_connect(ip, port);

    if (link->tcp == NET_SOCKET_INVALID)
    {
        delete link;
        *error = std::string("Can't connect to ") + host;
        return false;
    }

    m_udp = net_socket_udp(0);

    if (m_udp == NET_SOCKET_INVALID)
    {
        net_socket_close(link->tcp);
        delete link;
        *error = "Can't open a UDP socket";
        return false;
    }

    m_mode = NET_SESSION_JOIN;
    m_state = NET_SESSION_CONNECTING;
    m_port = port;
    m_host_ip = ip;
    m_name = name;
    m_password = password;
    m_machine = 0;
    m_error.clear();
    m_host_text = std::string(host) + ":" + std::to_string(port);
    link->udp_ip = ip;
    link->udp_port = port;
    link->udp_known = true;
    m_links[1] = link;
    memset(m_peers, 0, sizeof(m_peers));
    m_dropped = 0;
    return true;
}

void NetSession::Leave()
{
    if (m_mode == NET_SESSION_OFF)
        return;

    for (int i = 1; i < NET_SWITCH_PORTS; i++)
    {
        if (!IsValidPointer(m_links[i]))
            continue;

        if (m_links[i]->connected)
        {
            QueueMessage(*m_links[i], NET_MESSAGE_BYE, NULL, 0);
            FlushLink(*m_links[i]);
        }

        net_socket_close(m_links[i]->tcp);
        SafeDelete(m_links[i]);
    }

    for (int i = 0; i < NET_SWITCH_MACHINES; i++)
    {
        if (!IsValidPointer(m_pending[i]))
            continue;

        net_socket_close(m_pending[i]->tcp);
        SafeDelete(m_pending[i]);
    }

    net_socket_close(m_listener);
    net_socket_close(m_udp);
    m_listener = NET_SOCKET_INVALID;
    m_udp = NET_SOCKET_INVALID;
    m_mode = NET_SESSION_OFF;
    m_state = NET_SESSION_IDLE;
    m_machine = 0;
    m_switch->Reset();
    memset(m_peers, 0, sizeof(m_peers));
    m_serial.Clear();
    m_serial_warned = false;
}

void NetSession::Update(NetworkPort* local, u64 now_ms)
{
    m_local = local;

    if (m_mode == NET_SESSION_HOST)
        UpdateHost(local, now_ms);
    else if (m_mode == NET_SESSION_JOIN)
        UpdateJoin(local, now_ms);
}

void NetSession::UpdateHost(NetworkPort* local, u64 now_ms)
{
    AcceptClients(now_ms);

    for (int i = 0; i < NET_SWITCH_MACHINES; i++)
    {
        Net_Session_Link* link = m_pending[i];

        if (!IsValidPointer(link))
            continue;

        if (!ReadLink(*link, -1 - i, now_ms) || !IsValidPointer(m_pending[i]))
            continue;

        if (now_ms - link->opened > k_timeout_ms)
        {
            net_socket_close(link->tcp);
            SafeDelete(m_pending[i]);
        }
    }

    for (int i = 2; i < NET_SWITCH_PORTS; i++)
    {
        if (IsValidPointer(m_links[i]))
            ReadLink(*m_links[i], i, now_ms);
    }

    ReceiveDatagrams(local, now_ms);

    int size = 0;
    const u8* data = NULL;

    while (IsValidPointer(local) && ((data = local->PeekSent(&size)) != NULL))
    {
        u8 frame[NETWORK_FRAME_MAX];
        memcpy(frame, data, size);
        local->PopSent();
        m_switch->Input(1, frame, size);
    }

    NetSwitch::NetSwitch_Duplicate duplicate;

    if (m_switch->TakeDuplicate(&duplicate))
    {
        AddDuplicateEvent(duplicate.first, duplicate.second, duplicate.address);
        u8 payload[8];
        memcpy(payload, duplicate.address, 6);
        payload[6] = (u8)duplicate.first;
        payload[7] = (u8)duplicate.second;

        for (int i = 2; i < NET_SWITCH_PORTS; i++)
        {
            if (IsValidPointer(m_links[i]) && ((i == duplicate.first) || (i == duplicate.second)))
                QueueMessage(*m_links[i], NET_MESSAGE_DUPLICATE, payload, sizeof(payload));
        }
    }

    bool keepalive = now_ms - m_last_keepalive >= k_keepalive_ms;

    if (keepalive)
        m_last_keepalive = now_ms;

    for (int i = 2; i < NET_SWITCH_PORTS; i++)
    {
        Net_Session_Link* link = m_links[i];

        if (!IsValidPointer(link))
            continue;

        if (now_ms - link->last_heard > k_timeout_ms)
        {
            CloseLink(i, true);
            continue;
        }

        if (keepalive)
        {
            u8 payload[4];
            write_u32_be(payload, (u32)now_ms);
            QueueMessage(*link, NET_MESSAGE_PING, payload, sizeof(payload));
        }

        if (!FlushLink(*link))
            CloseLink(i, true);
    }
}

void NetSession::UpdateJoin(NetworkPort* local, u64 now_ms)
{
    Net_Session_Link* link = m_links[1];

    if (!IsValidPointer(link))
        return;

    if (link->opened == 0)
    {
        link->opened = now_ms;
        link->last_heard = now_ms;
    }

    if (!link->connected)
    {
        int connected = net_socket_connected(link->tcp);

        if (connected < 0)
        {
            Fail("Can't reach the host");
            return;
        }

        if (connected == 0)
        {
            if (now_ms - link->opened > k_timeout_ms)
                Fail("The host doesn't answer");

            return;
        }

        link->connected = true;
        link->last_heard = now_ms;
    }

    if (!ReadLink(*link, 1, now_ms))
        return;

    if (m_mode != NET_SESSION_JOIN)
        return;

    if (now_ms - link->last_heard > k_timeout_ms)
    {
        Fail(m_state == NET_SESSION_CONNECTED ? "Lost the host" : "The host doesn't answer");
        return;
    }

    if (m_state == NET_SESSION_CONNECTED)
    {
        ReceiveDatagrams(local, now_ms);

        int size = 0;
        const u8* data = NULL;

        while (IsValidPointer(local) && ((data = local->PeekSent(&size)) != NULL))
        {
            SendFrame(*link, (u8)m_machine, data, size);
            local->PopSent();
        }

        if (now_ms - m_last_keepalive >= k_keepalive_ms)
        {
            m_last_keepalive = now_ms;
            u8 payload[4];
            write_u32_be(payload, (u32)now_ms);
            QueueMessage(*link, NET_MESSAGE_PING, payload, sizeof(payload));
            SendKeepalive(*link, (u8)m_machine);
        }
    }
    else
    {
        int size = 0;

        while (IsValidPointer(local) && (local->PeekSent(&size) != NULL))
            local->PopSent();
    }

    if (!FlushLink(*link))
        Fail("Lost the host");
}

void NetSession::AcceptClients(u64 now_ms)
{
    while (true)
    {
        u32 ip = 0;
        u16 port = 0;
        net_socket socket = net_socket_accept(m_listener, &ip, &port);

        if (socket == NET_SOCKET_INVALID)
            return;

        int slot = -1;

        for (int i = 0; (i < NET_SWITCH_MACHINES) && (slot < 0); i++)
        {
            if (!IsValidPointer(m_pending[i]))
                slot = i;
        }

        if (slot < 0)
        {
            net_socket_close(socket);
            continue;
        }

        Net_Session_Link* link = new Net_Session_Link();
        ResetLink(*link);
        link->tcp = socket;
        link->connected = true;
        link->opened = now_ms;
        link->last_heard = now_ms;
        link->udp_ip = ip;

        for (int i = 0; i < k_nonce_size; i++)
            link->nonce[i] = (u8)random_u32();

        u8 payload[6 + k_nonce_size];
        memcpy(payload, k_magic, 4);
        write_u16_be(payload + 4, NET_SESSION_PROTOCOL);
        memcpy(payload + 6, link->nonce, k_nonce_size);
        QueueMessage(*link, NET_MESSAGE_CHALLENGE, payload, sizeof(payload));
        FlushLink(*link);
        m_pending[slot] = link;
    }
}

bool NetSession::ReadLink(Net_Session_Link& link, int machine, u64 now_ms)
{
    u8 payload[NET_SESSION_MESSAGE_MAX];

    while (true)
    {
        int space = NET_SESSION_BUFFER_SIZE - link.in_size;
        int received = space > 0 ? net_socket_receive(link.tcp, link.in + link.in_size, space) : 0;

        if (received == NET_SOCKET_ERROR)
        {
            DropLink(machine, m_state == NET_SESSION_CONNECTED ? "The host closed the session" :
                "The host refused the connection");
            return false;
        }

        if (received > 0)
            link.in_size += received;

        while (link.in_size >= 3)
        {
            int length = read_u16_be(link.in);

            if ((length < 1) || (length > NET_SESSION_MESSAGE_MAX))
            {
                DropLink(machine, "The host sent a bad message");
                return false;
            }

            if (link.in_size < 2 + length)
                break;

            u8 type = link.in[2];
            int size = length - 1;
            memcpy(payload, link.in + 3, size);
            memmove(link.in, link.in + 2 + length, link.in_size - 2 - length);
            link.in_size -= 2 + length;
            link.last_heard = now_ms;

            bool ok = m_mode == NET_SESSION_JOIN ? HandleJoinMessage(type, payload, size, now_ms) :
                HandleHostMessage(machine, type, payload, size, now_ms);

            if (!ok)
                return false;
        }

        if (received <= 0)
            return true;
    }
}

void NetSession::DropLink(int machine, const char* text)
{
    if (machine == 1)
        Fail(text);
    else if (machine > 1)
        CloseLink(machine, true);
    else
    {
        net_socket_close(m_pending[-1 - machine]->tcp);
        SafeDelete(m_pending[-1 - machine]);
    }
}

bool NetSession::FlushLink(Net_Session_Link& link)
{
    if (!link.connected)
        return true;

    while (link.out_size > 0)
    {
        int sent = net_socket_send(link.tcp, link.out, link.out_size);

        if (sent == NET_SOCKET_ERROR)
            return false;

        if (sent == 0)
            return true;

        memmove(link.out, link.out + sent, link.out_size - sent);
        link.out_size -= sent;
    }

    return true;
}

bool NetSession::QueueMessage(Net_Session_Link& link, u8 type, const u8* payload, int size)
{
    if ((size + 3 > NET_SESSION_BUFFER_SIZE - link.out_size) || (size + 1 > NET_SESSION_MESSAGE_MAX))
        return false;

    write_u16_be(link.out + link.out_size, (u16)(size + 1));
    link.out[link.out_size + 2] = type;

    if (size > 0)
        memcpy(link.out + link.out_size + 3, payload, size);

    link.out_size += size + 3;
    return true;
}

bool NetSession::HandleHostMessage(int machine, u8 type, const u8* payload, int size, u64 now_ms)
{
    if (machine < 0)
    {
        int slot = -1 - machine;
        Net_Session_Link* link = m_pending[slot];

        if ((type != NET_MESSAGE_HELLO) || (size < 6 + NET_SESSION_NAME_SIZE + k_proof_size) ||
            (memcmp(payload, k_magic, 4) != 0))
        {
            net_socket_close(link->tcp);
            SafeDelete(m_pending[slot]);
            return false;
        }

        u8 reason = 0;

        if (read_u16_be(payload + 4) != NET_SESSION_PROTOCOL)
            reason = NET_REJECT_VERSION;
        else if (!m_password.empty())
        {
            u8 proof[k_proof_size];
            Proof(link->nonce, proof);

            if (memcmp(proof, payload + 6 + NET_SESSION_NAME_SIZE, k_proof_size) != 0)
                reason = NET_REJECT_PASSWORD;
        }

        int free_machine = reason == 0 ? FindFreeMachine() : -1;

        if ((reason == 0) && (free_machine < 0))
            reason = NET_REJECT_FULL;

        if (reason != 0)
        {
            QueueMessage(*link, NET_MESSAGE_REJECT, &reason, 1);
            FlushLink(*link);
            net_socket_close(link->tcp);
            SafeDelete(m_pending[slot]);
            return false;
        }

        m_pending[slot] = NULL;
        m_links[free_machine] = link;
        link->welcomed = true;
        link->last_heard = now_ms;
        link->udp_known = false;
        m_peers[free_machine].present = true;
        m_peers[free_machine].rtt_ms = -1;
        copy_name(m_peers[free_machine].name, payload + 6);
        m_switch->OpenPort(free_machine);

        u8 welcome[6];
        welcome[0] = (u8)free_machine;
        link->token = random_u32() | 1;
        write_u32_be(welcome + 1, link->token);
        welcome[5] = (u8)m_max_machines;
        QueueMessage(*link, NET_MESSAGE_WELCOME, welcome, sizeof(welcome));

        char text[96];
        snprintf(text, sizeof(text), "%s joined as machine %d", m_peers[free_machine].name, free_machine);
        AddEvent(NET_SESSION_EVENT_JOINED, free_machine, text);
        SendPeers();
        return false;
    }

    Net_Session_Link* link = m_links[machine];

    switch (type)
    {
        case NET_MESSAGE_PING:
            QueueMessage(*link, NET_MESSAGE_PONG, payload, MIN(size, 4));
            return true;
        case NET_MESSAGE_PONG:
            if (size >= 4)
                m_peers[machine].rtt_ms = (int)((u32)now_ms - read_u32_be(payload));
            return true;
        case NET_MESSAGE_BYE:
            CloseLink(machine, true);
            return false;
        case NET_MESSAGE_SERIAL:
        {
            if ((size < 4) || !network_serial_valid(payload[0], payload[3], size - 4))
            {
                CloseLink(machine, true);
                return false;
            }

            RouteSerial(machine, payload[0], payload[1], payload[3], payload + 4, size - 4);
            return true;
        }
        default:
            CloseLink(machine, true);
            return false;
    }
}

bool NetSession::HandleJoinMessage(u8 type, const u8* payload, int size, u64 now_ms)
{
    Net_Session_Link* link = m_links[1];

    switch (type)
    {
        case NET_MESSAGE_CHALLENGE:
        {
            if ((size < 6 + k_nonce_size) || (memcmp(payload, k_magic, 4) != 0))
            {
                Fail("That isn't a Geardox session");
                return false;
            }

            if (read_u16_be(payload + 4) != NET_SESSION_PROTOCOL)
            {
                Fail("The host runs another Geardox version");
                return false;
            }

            u8 hello[6 + NET_SESSION_NAME_SIZE + k_proof_size];
            memset(hello, 0, sizeof(hello));
            memcpy(hello, k_magic, 4);
            write_u16_be(hello + 4, NET_SESSION_PROTOCOL);
            strncpy((char*)hello + 6, m_name.c_str(), NET_SESSION_NAME_SIZE);
            Proof(payload + 6, hello + 6 + NET_SESSION_NAME_SIZE);
            QueueMessage(*link, NET_MESSAGE_HELLO, hello, sizeof(hello));
            return true;
        }
        case NET_MESSAGE_WELCOME:
            if ((size < 6) || (payload[0] < 2) || (payload[0] > NET_SWITCH_MACHINES))
            {
                Fail("The host sent a bad message");
                return false;
            }

            m_machine = payload[0];
            link->token = read_u32_be(payload + 1);
            m_max_machines = payload[5];
            m_state = NET_SESSION_CONNECTED;
            m_last_keepalive = now_ms;
            SendKeepalive(*link, (u8)m_machine);
            AddEvent(NET_SESSION_EVENT_CONNECTED, m_machine, "");
            return true;
        case NET_MESSAGE_REJECT:
            Fail(size < 1 ? "The host refused to let this machine in" : payload[0] == NET_REJECT_PASSWORD ?
                "Wrong password" : payload[0] == NET_REJECT_FULL ? "The session is full" :
                payload[0] == NET_REJECT_VERSION ? "The host runs another Geardox version" :
                "The host refused to let this machine in");
            return false;
        case NET_MESSAGE_PEERS:
        {
            Net_Session_Peer peers[NET_SWITCH_PORTS];
            memset(peers, 0, sizeof(peers));
            int count = size > 0 ? payload[0] : 0;
            int entry = 3 + NET_SESSION_NAME_SIZE;

            for (int i = 0; (i < count) && (1 + (i + 1) * entry <= size); i++)
            {
                const u8* p = payload + 1 + i * entry;
                int number = p[0];

                if ((number < 1) || (number >= NET_SWITCH_PORTS))
                    continue;

                peers[number].present = true;
                peers[number].rtt_ms = (s16)read_u16_be(p + 1);
                copy_name(peers[number].name, p + 3);
            }

            for (int i = 1; i < NET_SWITCH_PORTS; i++)
            {
                if (peers[i].present && !m_peers[i].present && (i != m_machine) && (m_peers[m_machine].present))
                {
                    char text[96];
                    snprintf(text, sizeof(text), "%s joined as machine %d", peers[i].name, i);
                    AddEvent(NET_SESSION_EVENT_JOINED, i, text);
                }
                else if (!peers[i].present && m_peers[i].present)
                {
                    char text[96];
                    snprintf(text, sizeof(text), "%s left", m_peers[i].name);
                    AddEvent(NET_SESSION_EVENT_LEFT, i, text);
                }
            }

            int own_rtt = m_peers[1].rtt_ms;
            memcpy(m_peers, peers, sizeof(m_peers));
            m_peers[1].rtt_ms = own_rtt;
            return true;
        }
        case NET_MESSAGE_PING:
            QueueMessage(*link, NET_MESSAGE_PONG, payload, MIN(size, 4));
            return true;
        case NET_MESSAGE_PONG:
            if (size >= 4)
                m_peers[1].rtt_ms = (int)((u32)now_ms - read_u32_be(payload));
            return true;
        case NET_MESSAGE_BYE:
            AddEvent(NET_SESSION_EVENT_ENDED, 1, "The host ended the session");
            Leave();
            m_state = NET_SESSION_FAILED;
            m_error = "The host ended the session";
            return false;
        case NET_MESSAGE_DUPLICATE:
            if (size >= 8)
                AddDuplicateEvent(payload[6], payload[7], payload);

            return true;
        case NET_MESSAGE_SERIAL:
            if ((size >= 4) && network_serial_valid(payload[0], payload[3], size - 4))
                QueueSerial(payload[0], payload[2], payload[3], payload + 4, size - 4);
            return true;
        default:
            Fail("The host sent a bad message");
            return false;
    }
}

void NetSession::ReceiveDatagrams(NetworkPort* local, u64 now_ms)
{
    while (true)
    {
        u32 ip = 0;
        u16 port = 0;
        int size = net_socket_receive_from(m_udp, m_datagram, sizeof(m_datagram), &ip, &port);

        if (size <= 0)
            return;

        if (!datagram_valid(m_datagram, size))
            continue;

        int machine = m_datagram[3];
        u32 token = read_u32_be(m_datagram + 4);
        bool host = m_mode == NET_SESSION_HOST;

        if ((host ? (machine < 2) : (machine != 1)) || (machine >= NET_SWITCH_PORTS) || !IsValidPointer(m_links[machine]) ||
            (m_links[machine]->token != token))
            continue;

        Net_Session_Link& link = *m_links[machine];
        link.last_heard = now_ms;

        if (host)
        {
            link.udp_ip = ip;
            link.udp_port = port;
            link.udp_known = true;
        }

        HandleDatagram(link, machine, m_datagram, size, local);
    }
}

void NetSession::HandleDatagram(Net_Session_Link& link, int machine, const u8* data, int size, NetworkPort* local)
{
    if (data[2] != NET_DATAGRAM_FRAME)
        return;

    u32 sequence = read_u32_be(data + 8);
    int part = data[12];
    int parts = data[13];
    int length = size - k_datagram_header;

    if ((link.fragment_mask == 0) || (link.fragment_sequence != sequence) || (link.fragment_parts != parts))
    {
        link.fragment_mask = 0;
        link.fragment_sequence = sequence;
        link.fragment_parts = parts;
    }

    memcpy(link.fragment[part], data + k_datagram_header, length);
    link.fragment_size[part] = length;
    link.fragment_mask |= 1 << part;

    if (link.fragment_mask != (1 << parts) - 1)
        return;

    link.fragment_mask = 0;
    u8 frame[NETWORK_FRAME_MAX];
    int frame_size = 0;

    for (int i = 0; i < parts; i++)
    {
        if (frame_size + link.fragment_size[i] > NETWORK_FRAME_MAX)
            return;

        memcpy(frame + frame_size, link.fragment[i], link.fragment_size[i]);
        frame_size += link.fragment_size[i];
    }

    if (link.sequence_known && ((s32)(sequence - link.last_sequence) <= 0))
    {
        m_dropped++;
        return;
    }

    link.sequence_known = true;
    link.last_sequence = sequence;

    if (m_mode == NET_SESSION_HOST)
        m_switch->Input(machine, frame, frame_size);
    else if (IsValidPointer(local))
        local->Deliver(frame, frame_size);
}

void NetSession::SendFrame(Net_Session_Link& link, u8 sender, const u8* data, int size)
{
    if (!link.udp_known)
        return;

    u32 sequence = ++link.udp_sequence;
    int parts = size > k_datagram_payload ? 2 : 1;
    int first = parts == 2 ? (size + 1) / 2 : size;

    for (int part = 0; part < parts; part++)
    {
        int offset = part == 0 ? 0 : first;
        int length = part == 0 ? first : size - first;
        m_datagram[0] = 'G';
        m_datagram[1] = 'N';
        m_datagram[2] = NET_DATAGRAM_FRAME;
        m_datagram[3] = sender;
        write_u32_be(m_datagram + 4, link.token);
        write_u32_be(m_datagram + 8, sequence);
        m_datagram[12] = (u8)part;
        m_datagram[13] = (u8)parts;
        memcpy(m_datagram + k_datagram_header, data + offset, length);
        net_socket_send_to(m_udp, link.udp_ip, link.udp_port, m_datagram, k_datagram_header + length);
    }
}

void NetSession::SendKeepalive(Net_Session_Link& link, u8 sender)
{
    u8 datagram[k_datagram_header];
    memset(datagram, 0, sizeof(datagram));
    datagram[0] = 'G';
    datagram[1] = 'N';
    datagram[2] = NET_DATAGRAM_KEEPALIVE;
    datagram[3] = sender;
    write_u32_be(datagram + 4, link.token);
    net_socket_send_to(m_udp, link.udp_ip, link.udp_port, datagram, sizeof(datagram));
}

void NetSession::SendPeers()
{
    u8 payload[1 + NET_SWITCH_PORTS * (3 + NET_SESSION_NAME_SIZE)];
    int count = 0;
    int offset = 1;

    for (int i = 1; i < NET_SWITCH_PORTS; i++)
    {
        if (!m_peers[i].present)
            continue;

        payload[offset] = (u8)i;
        write_u16_be(payload + offset + 1, (u16)(s16)m_peers[i].rtt_ms);
        memset(payload + offset + 3, 0, NET_SESSION_NAME_SIZE);
        memcpy(payload + offset + 3, m_peers[i].name, strlen(m_peers[i].name));
        offset += 3 + NET_SESSION_NAME_SIZE;
        count++;
    }

    payload[0] = (u8)count;

    for (int i = 2; i < NET_SWITCH_PORTS; i++)
    {
        if (IsValidPointer(m_links[i]))
            QueueMessage(*m_links[i], NET_MESSAGE_PEERS, payload, offset);
    }
}

void NetSession::SwitchOutput(void* context, int port, const u8* data, int size)
{
    NetSession* session = (NetSession*)context;

    if (port == 1)
    {
        if (IsValidPointer(session->m_local))
            session->m_local->Deliver(data, size);

        return;
    }

    if ((port > 1) && (port < NET_SWITCH_PORTS) && IsValidPointer(session->m_links[port]))
        session->SendFrame(*session->m_links[port], 1, data, size);
}

void NetSession::Fail(const char* text)
{
    m_error = text;
    AddEvent(m_state == NET_SESSION_CONNECTED ? NET_SESSION_EVENT_ENDED : NET_SESSION_EVENT_FAILED, 1, text);
    Leave();
    m_state = NET_SESSION_FAILED;
}

void NetSession::ResetLink(Net_Session_Link& link)
{
    link.tcp = NET_SOCKET_INVALID;
    link.connected = false;
    link.welcomed = false;
    memset(link.nonce, 0, sizeof(link.nonce));
    link.in_size = 0;
    link.out_size = 0;
    link.last_heard = 0;
    link.last_ping = 0;
    link.opened = 0;
    link.udp_ip = 0;
    link.udp_port = 0;
    link.udp_known = false;
    link.udp_sequence = 0;
    link.last_sequence = 0;
    link.sequence_known = false;
    link.token = 0;
    link.fragment_size[0] = 0;
    link.fragment_size[1] = 0;
    link.fragment_sequence = 0;
    link.fragment_parts = 0;
    link.fragment_mask = 0;
}

void NetSession::CloseLink(int machine, bool notify)
{
    Net_Session_Link* link = m_links[machine];

    if (!IsValidPointer(link))
        return;

    if (link->connected)
    {
        QueueMessage(*link, NET_MESSAGE_BYE, NULL, 0);
        FlushLink(*link);
    }

    net_socket_close(link->tcp);
    SafeDelete(m_links[machine]);
    m_switch->ClosePort(machine);

    if (notify && m_peers[machine].present)
    {
        char text[96];
        snprintf(text, sizeof(text), "%s left", m_peers[machine].name);
        AddEvent(NET_SESSION_EVENT_LEFT, machine, text);
    }

    m_peers[machine].present = false;
    SendPeers();
}

void NetSession::AddDuplicateEvent(int first, int second, const u8* address)
{
    char mac[18];
    hex_encode(address, 6, mac, sizeof(mac), true, ':');
    std::string text = format_string("Machines %d and %d share the address %s", first, second, mac);
    AddEvent(NET_SESSION_EVENT_DUPLICATE, first, text.c_str());
}

void NetSession::AddEvent(Net_Session_Event_Type type, int machine, const char* text)
{
    if (m_event_count >= NET_SESSION_EVENTS)
    {
        memmove(m_events, m_events + 1, sizeof(Net_Session_Event) * (NET_SESSION_EVENTS - 1));
        m_event_count--;
    }

    Net_Session_Event& event = m_events[m_event_count++];
    event.type = type;
    event.machine = machine;
    strncpy_fit(event.text, text, sizeof(event.text));
}

void NetSession::Proof(const u8* nonce, u8* proof) const
{
    u8 input[k_nonce_size + 256];
    int length = MIN((int)m_password.size(), 256);
    memcpy(input, nonce, k_nonce_size);
    memcpy(input + k_nonce_size, m_password.c_str(), length);
    sha1::calc(input, k_nonce_size + length, proof);
}

int NetSession::FindFreeMachine() const
{
    for (int i = 2; i <= m_max_machines; i++)
    {
        if (!IsValidPointer(m_links[i]))
            return i;
    }

    return -1;
}

Net_Session_Mode NetSession::GetMode() const
{
    return m_mode;
}

Net_Session_State NetSession::GetState() const
{
    return m_state;
}

int NetSession::GetMachine() const
{
    return m_machine;
}

int NetSession::GetMachineCount() const
{
    int count = 0;

    for (int i = 1; i < NET_SWITCH_PORTS; i++)
    {
        if (m_peers[i].present)
            count++;
    }

    return count;
}

int NetSession::GetMaxMachines() const
{
    return m_max_machines;
}

u16 NetSession::GetPort() const
{
    return m_port;
}

const char* NetSession::GetHostText() const
{
    return m_host_text.c_str();
}

const char* NetSession::GetError() const
{
    return m_error.c_str();
}

const Net_Session_Peer* NetSession::GetPeer(int machine) const
{
    return (machine > 0) && (machine < NET_SWITCH_PORTS) ? &m_peers[machine] : NULL;
}

bool NetSession::TakeEvent(Net_Session_Event* event)
{
    if (m_event_count == 0)
        return false;

    *event = m_events[0];
    memmove(m_events, m_events + 1, sizeof(Net_Session_Event) * (m_event_count - 1));
    m_event_count--;
    return true;
}

NetSwitch* NetSession::GetSwitch()
{
    return m_switch;
}

u32 NetSession::GetDroppedFrames() const
{
    return m_dropped;
}

bool NetSession::SendSerial(int channel, int to, int kind, const u8* data, int size)
{
    if (m_state != NET_SESSION_CONNECTED)
    {
        if (kind == NETWORK_SERIAL_CALL)
            QueueSerial(channel, 0, NETWORK_SERIAL_HANGUP, NULL, 0);

        return false;
    }

    size = MIN(size, NETWORK_SERIAL_RECORD);

    if (m_mode == NET_SESSION_JOIN)
    {
        u8 payload[4 + NETWORK_SERIAL_RECORD];
        payload[0] = (u8)channel;
        payload[1] = (u8)to;
        payload[2] = (u8)m_machine;
        payload[3] = (u8)kind;
        memcpy(payload + 4, data, size);
        return IsValidPointer(m_links[1]) && QueueMessage(*m_links[1], NET_MESSAGE_SERIAL, payload, 4 + size);
    }

    RouteSerial(1, channel, to, kind, data, size);
    return true;
}

void NetSession::RouteSerial(int from, int channel, int to, int kind, const u8* data, int size)
{
    bool present[NET_SWITCH_PORTS];

    for (int i = 0; i < NET_SWITCH_PORTS; i++)
        present[i] = m_peers[i].present;

    int target = net_serial_target(from, to, present);

    if ((target < 0) && (to == 0) && (GetMachineCount() > 2) && !m_serial_warned)
    {
        m_serial_warned = true;
        AddEvent(NET_SESSION_EVENT_SERIAL, from, "A serial link pairs two machines: this session has more");
    }

    if (target < 0)
    {
        if (kind == NETWORK_SERIAL_CALL)
            DeliverSerial(from, channel, 0, NETWORK_SERIAL_HANGUP, NULL, 0);

        return;
    }

    DeliverSerial(target, channel, from, kind, data, size);
}

void NetSession::DeliverSerial(int target, int channel, int from, int kind, const u8* data, int size)
{
    if (!network_serial_valid(channel, kind, size))
        return;

    if (target == 1)
    {
        QueueSerial(channel, from, kind, data, size);
        return;
    }

    if ((target < 2) || (target >= NET_SWITCH_PORTS) || !IsValidPointer(m_links[target]))
        return;

    u8 payload[4 + NETWORK_SERIAL_RECORD];
    payload[0] = (u8)channel;
    payload[1] = (u8)target;
    payload[2] = (u8)from;
    payload[3] = (u8)kind;
    memcpy(payload + 4, data, size);
    QueueMessage(*m_links[target], NET_MESSAGE_SERIAL, payload, 4 + size);
}

void NetSession::QueueSerial(int channel, int from, int kind, const u8* data, int size)
{
    m_serial.Push(channel, kind, from, data, size);
}

bool NetSession::TakeSerial(Net_Session_Serial* serial)
{
    return m_serial.Pop(&serial->channel, &serial->kind, &serial->from, serial->data, &serial->size);
}
