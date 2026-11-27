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

#include "libretro_net.h"
#include "geardox.h"
#include "network/net_switch.h"

enum Libretro_Net_Packet
{
    LIBRETRO_NET_PACKET_FRAME = 1,
    LIBRETRO_NET_PACKET_SERIAL,
    LIBRETRO_NET_PACKET_PEERS
};

static const char k_protocol_version[] = "geardox-net-1";

static GeardoxCore* net_core = NULL;
static libretro_net_event_t net_event = NULL;
static retro_netpacket_send_t net_send = NULL;
static NetSwitch* net_switch = NULL;
static bool net_active = false;
static bool net_host = false;
static int net_machine = 0;
static int net_machine_count = 0;
static int net_clients[NET_SWITCH_PORTS];
static u8 net_packet[NETWORK_FRAME_MAX + 1];

static void RETRO_CALLCONV net_start(uint16_t client_id, retro_netpacket_send_t send_fn,
    retro_netpacket_poll_receive_t poll_receive_fn);
static void RETRO_CALLCONV net_receive(const void* buf, size_t len, uint16_t client_id);
static void RETRO_CALLCONV net_stop(void);
static void RETRO_CALLCONV net_poll(void);
static bool RETRO_CALLCONV net_connected(uint16_t client_id);
static void RETRO_CALLCONV net_disconnected(uint16_t client_id);
static void switch_output(void* context, int port, const u8* data, int size);
static void send_peers(void);
static int find_port(uint16_t client_id);
static void move_serial(NetworkPort* port);
static void route_serial(int from, int channel, int to, int kind, const u8* data, int size);
static void deliver_serial(int target, int channel, int from, int kind, const u8* data, int size);

void libretro_net_init(retro_environment_t environ_cb, GeardoxCore* core, libretro_net_event_t event_cb)
{
    net_core = core;
    net_event = event_cb;
    net_switch = new NetSwitch();
    net_switch->SetOutput(switch_output, NULL);
    net_switch->OpenPort(1);

    for (int i = 0; i < NET_SWITCH_PORTS; i++)
        net_clients[i] = -1;

    static const struct retro_netpacket_callback callbacks = { net_start, net_receive, net_stop, net_poll,
        net_connected, net_disconnected, k_protocol_version };
    environ_cb(RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE, (void*)&callbacks);
}

void libretro_net_deinit(void)
{
    SafeDelete(net_switch);
    net_active = false;
    net_send = NULL;
    net_core = NULL;
}

void libretro_net_begin_run(int av_enable)
{
    net_core->GetNetworkPort()->SetSpeculative((av_enable & RETRO_AV_ENABLE_AUDIO) == 0);
}

void libretro_net_end_run(void)
{
    NetworkPort* port = net_core->GetNetworkPort();
    port->SetSpeculative(false);

    if (net_active)
        return;

    int channel = 0;
    int kind = 0;
    int peer = 0;
    int length = 0;
    u8 record[NETWORK_SERIAL_RECORD];

    while (port->TakeSentSerial(&channel, &kind, &peer, record, &length))
    {
        if (kind == NETWORK_SERIAL_CALL)
            port->DeliverSerial(channel, NETWORK_SERIAL_HANGUP, 0, NULL, 0);
    }

    int size = 0;
    const u8* data = NULL;

    while ((data = port->PeekSent(&size)) != NULL)
    {
        memcpy(net_packet, data, size);
        port->PopSent();
        net_switch->Input(1, net_packet, size);
    }
}

bool libretro_net_is_active(void)
{
    return net_active;
}

bool libretro_net_is_host(void)
{
    return net_active && net_host;
}

int libretro_net_get_machine(void)
{
    return net_machine;
}

int libretro_net_get_machine_count(void)
{
    return net_machine_count;
}

static void RETRO_CALLCONV net_start(uint16_t client_id, retro_netpacket_send_t send_fn,
    retro_netpacket_poll_receive_t poll_receive_fn)
{
    UNUSED(poll_receive_fn);
    net_send = send_fn;
    net_active = true;
    net_host = client_id == 0;
    net_machine = net_host ? 1 : 0;
    net_machine_count = net_host ? 1 : 0;
    net_switch->Reset();
    net_switch->OpenPort(1);

    for (int i = 0; i < NET_SWITCH_PORTS; i++)
        net_clients[i] = -1;

    if (IsValidPointer(net_event))
        net_event(LIBRETRO_NET_STARTED, net_machine);
}

static void RETRO_CALLCONV net_receive(const void* buf, size_t len, uint16_t client_id)
{
    const u8* data = (const u8*)buf;

    if (!net_active || (len < 1) || (len > sizeof(net_packet)))
        return;

    switch (data[0])
    {
        case LIBRETRO_NET_PACKET_FRAME:
            if (net_host)
            {
                int port = find_port(client_id);

                if (port > 0)
                {
                    memcpy(net_packet, data + 1, len - 1);
                    net_switch->Input(port, net_packet, (int)len - 1);
                }
            }
            else
                net_core->GetNetworkPort()->Deliver(data + 1, (int)len - 1);
            break;
        case LIBRETRO_NET_PACKET_SERIAL:
            if (len >= 5)
            {
                if (net_host)
                {
                    int port = find_port(client_id);

                    if (port > 0)
                        route_serial(port, data[1], data[2], data[4], data + 5, (int)len - 5);
                }
                else
                    net_core->GetNetworkPort()->DeliverSerial(data[1], data[4], data[3], data + 5, (int)len - 5);
            }
            break;
        case LIBRETRO_NET_PACKET_PEERS:
            if (!net_host && (len >= 3))
            {
                net_machine = data[1];
                net_machine_count = data[2];
            }
            break;
        default:
            break;
    }
}

static void RETRO_CALLCONV net_stop(void)
{
    net_active = false;
    net_host = false;
    net_send = NULL;
    net_machine = 0;
    net_machine_count = 0;
    net_switch->Reset();
    net_switch->OpenPort(1);

    if (IsValidPointer(net_event))
        net_event(LIBRETRO_NET_STOPPED, 0);
}

static void RETRO_CALLCONV net_poll(void)
{
    if (!net_active || !IsValidPointer(net_send))
        return;

    NetworkPort* port = net_core->GetNetworkPort();
    int size = 0;
    const u8* data = NULL;

    move_serial(port);

    while ((data = port->PeekSent(&size)) != NULL)
    {
        if (net_host)
        {
            memcpy(net_packet, data, size);
            port->PopSent();
            net_switch->Input(1, net_packet, size);
            continue;
        }

        net_packet[0] = LIBRETRO_NET_PACKET_FRAME;
        memcpy(net_packet + 1, data, size);
        port->PopSent();
        net_send(RETRO_NETPACKET_RELIABLE, net_packet, (size_t)size + 1, 0);
    }
}

static bool RETRO_CALLCONV net_connected(uint16_t client_id)
{
    if (!net_host)
        return true;

    for (int i = 2; i < NET_SWITCH_PORTS; i++)
    {
        if (net_clients[i] >= 0)
            continue;

        net_clients[i] = client_id;
        net_switch->OpenPort(i);
        net_machine_count++;
        send_peers();

        if (IsValidPointer(net_event))
            net_event(LIBRETRO_NET_JOINED, i);

        return true;
    }

    if (IsValidPointer(net_event))
        net_event(LIBRETRO_NET_REFUSED, 0);

    return false;
}

static void RETRO_CALLCONV net_disconnected(uint16_t client_id)
{
    int port = find_port(client_id);

    if (port <= 0)
        return;

    net_clients[port] = -1;
    net_switch->ClosePort(port);
    net_machine_count--;
    send_peers();

    if (IsValidPointer(net_event))
        net_event(LIBRETRO_NET_LEFT, port);
}

static void switch_output(void* context, int port, const u8* data, int size)
{
    UNUSED(context);

    if (port == 1)
    {
        net_core->GetNetworkPort()->Deliver(data, size);
        return;
    }

    if (!net_active || !IsValidPointer(net_send) || (port < 2) || (port >= NET_SWITCH_PORTS) || (net_clients[port] < 0))
        return;

    u8 packet[NETWORK_FRAME_MAX + 1];
    packet[0] = LIBRETRO_NET_PACKET_FRAME;
    memcpy(packet + 1, data, size);
    net_send(RETRO_NETPACKET_RELIABLE, packet, (size_t)size + 1, (uint16_t)net_clients[port]);
}

static void send_peers(void)
{
    if (!IsValidPointer(net_send))
        return;

    for (int i = 2; i < NET_SWITCH_PORTS; i++)
    {
        if (net_clients[i] < 0)
            continue;

        u8 packet[3] = { LIBRETRO_NET_PACKET_PEERS, (u8)i, (u8)net_machine_count };
        net_send(RETRO_NETPACKET_RELIABLE, packet, sizeof(packet), (uint16_t)net_clients[i]);
    }
}

static void move_serial(NetworkPort* port)
{
    int channel = 0;
    int kind = 0;
    int peer = 0;
    int size = 0;
    u8 data[NETWORK_SERIAL_RECORD];

    while (port->TakeSentSerial(&channel, &kind, &peer, data, &size))
    {
        if (net_host)
        {
            route_serial(1, channel, peer, kind, data, size);
            continue;
        }

        u8 packet[5 + NETWORK_SERIAL_RECORD];
        packet[0] = LIBRETRO_NET_PACKET_SERIAL;
        packet[1] = (u8)channel;
        packet[2] = (u8)peer;
        packet[3] = (u8)net_machine;
        packet[4] = (u8)kind;
        memcpy(packet + 5, data, size);
        net_send(RETRO_NETPACKET_RELIABLE, packet, (size_t)size + 5, 0);
    }
}

static void route_serial(int from, int channel, int to, int kind, const u8* data, int size)
{
    int target = -1;

    if ((to == 0) && (net_machine_count == 2))
        target = from == 1 ? -1 : 1;

    for (int i = 2; (to == 0) && (net_machine_count == 2) && (i < NET_SWITCH_PORTS) && (target < 0); i++)
    {
        if ((net_clients[i] >= 0) && (i != from))
            target = i;
    }

    if ((to > 0) && (to != from) && ((to == 1) || ((to < NET_SWITCH_PORTS) && (net_clients[to] >= 0))))
        target = to;

    if (target < 0)
    {
        if (kind == NETWORK_SERIAL_CALL)
            deliver_serial(from, channel, 0, NETWORK_SERIAL_HANGUP, NULL, 0);

        return;
    }

    deliver_serial(target, channel, from, kind, data, size);
}

static void deliver_serial(int target, int channel, int from, int kind, const u8* data, int size)
{
    if (target == 1)
    {
        net_core->GetNetworkPort()->DeliverSerial(channel, kind, from, data, size);
        return;
    }

    if ((target < 2) || (target >= NET_SWITCH_PORTS) || (net_clients[target] < 0) || !IsValidPointer(net_send))
        return;

    u8 packet[5 + NETWORK_SERIAL_RECORD];
    packet[0] = LIBRETRO_NET_PACKET_SERIAL;
    packet[1] = (u8)channel;
    packet[2] = (u8)target;
    packet[3] = (u8)from;
    packet[4] = (u8)kind;
    memcpy(packet + 5, data, size);
    net_send(RETRO_NETPACKET_RELIABLE, packet, (size_t)size + 5, (uint16_t)net_clients[target]);
}

static int find_port(uint16_t client_id)
{
    for (int i = 2; i < NET_SWITCH_PORTS; i++)
    {
        if (net_clients[i] == client_id)
            return i;
    }

    return -1;
}
