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

#ifndef NET_SESSION_H
#define NET_SESSION_H

#include <string>
#include "geardox.h"
#include "network/net_switch.h"
#include "net_socket.h"

#define NET_SESSION_PROTOCOL 1
#define NET_SESSION_DEFAULT_PORT 28183
#define NET_SESSION_NAME_SIZE 32
#define NET_SESSION_MESSAGE_MAX 4096
#define NET_SESSION_BUFFER_SIZE 65536
#define NET_SESSION_EVENTS 64
#define NET_SESSION_SERIAL_CHANNELS 2

enum Net_Session_Mode
{
    NET_SESSION_OFF = 0,
    NET_SESSION_HOST,
    NET_SESSION_JOIN
};

enum Net_Session_State
{
    NET_SESSION_IDLE = 0,
    NET_SESSION_CONNECTING,
    NET_SESSION_CONNECTED,
    NET_SESSION_FAILED
};

enum Net_Session_Event_Type
{
    NET_SESSION_EVENT_JOINED = 0,
    NET_SESSION_EVENT_LEFT,
    NET_SESSION_EVENT_CONNECTED,
    NET_SESSION_EVENT_ENDED,
    NET_SESSION_EVENT_FAILED,
    NET_SESSION_EVENT_DUPLICATE,
    NET_SESSION_EVENT_SERIAL
};

struct Net_Session_Peer
{
    bool present;
    char name[NET_SESSION_NAME_SIZE + 1];
    int rtt_ms;
};

struct Net_Session_Event
{
    Net_Session_Event_Type type;
    int machine;
    char text[160];
};

struct Net_Session_Serial
{
    int channel;
    int from;
    int kind;
    int size;
    u8 data[1024];
};

class NetSession
{
public:
    NetSession();
    ~NetSession();
    bool Host(const char* address, u16 port, const char* name, const char* password, int machines, std::string* error);
    bool Join(const char* host, u16 port, const char* name, const char* password, std::string* error);
    void Leave();
    void Update(NetworkPort* local, u64 now_ms);
    Net_Session_Mode GetMode() const;
    Net_Session_State GetState() const;
    int GetMachine() const;
    int GetMachineCount() const;
    int GetMaxMachines() const;
    u16 GetPort() const;
    const char* GetHostText() const;
    const char* GetError() const;
    const Net_Session_Peer* GetPeer(int machine) const;
    bool TakeEvent(Net_Session_Event* event);
    NetSwitch* GetSwitch();
    u32 GetDroppedFrames() const;
    bool SendSerial(int channel, int to, int kind, const u8* data, int size);
    void QueueSerial(int channel, int from, int kind, const u8* data, int size);
    bool TakeSerial(Net_Session_Serial* serial);

private:
    struct Net_Session_Link
    {
        net_socket tcp;
        bool connected;
        bool welcomed;
        u8 nonce[16];
        u8 in[NET_SESSION_BUFFER_SIZE];
        int in_size;
        u8 out[NET_SESSION_BUFFER_SIZE];
        int out_size;
        u64 last_heard;
        u64 last_ping;
        u64 opened;
        u32 udp_ip;
        u16 udp_port;
        bool udp_known;
        u32 udp_sequence;
        u32 last_sequence;
        bool sequence_known;
        u32 token;
        u8 fragment[2][NETWORK_FRAME_MAX];
        int fragment_size[2];
        u32 fragment_sequence;
        int fragment_parts;
        int fragment_mask;
    };

    static void SwitchOutput(void* context, int port, const u8* data, int size);
    void Fail(const char* text);
    void ResetLink(Net_Session_Link& link);
    void CloseLink(int machine, bool notify);
    void UpdateHost(NetworkPort* local, u64 now_ms);
    void UpdateJoin(NetworkPort* local, u64 now_ms);
    void AcceptClients(u64 now_ms);
    bool ReadLink(Net_Session_Link& link, int machine, u64 now_ms);
    void DropLink(int machine, const char* text);
    bool FlushLink(Net_Session_Link& link);
    bool QueueMessage(Net_Session_Link& link, u8 type, const u8* payload, int size);
    bool HandleHostMessage(int machine, u8 type, const u8* payload, int size, u64 now_ms);
    bool HandleJoinMessage(u8 type, const u8* payload, int size, u64 now_ms);
    void ReceiveDatagrams(NetworkPort* local, u64 now_ms);
    void HandleDatagram(Net_Session_Link& link, int machine, const u8* data, int size, NetworkPort* local);
    void SendFrame(Net_Session_Link& link, u8 sender, const u8* data, int size);
    void SendKeepalive(Net_Session_Link& link, u8 sender);
    void SendPeers();
    void AddEvent(Net_Session_Event_Type type, int machine, const char* text);
    void AddDuplicateEvent(int first, int second, const u8* address);
    void RouteSerial(int from, int channel, int to, int kind, const u8* data, int size);
    void DeliverSerial(int target, int channel, int from, int kind, const u8* data, int size);
    void Proof(const u8* nonce, u8* proof) const;
    int FindFreeMachine() const;

private:
    Net_Session_Mode m_mode;
    Net_Session_State m_state;
    std::string m_error;
    std::string m_host_text;
    std::string m_name;
    std::string m_password;
    net_socket m_listener;
    net_socket m_udp;
    u16 m_port;
    u32 m_host_ip;
    int m_machine;
    int m_max_machines;
    NetSwitch* m_switch;
    NetworkPort* m_local;
    Net_Session_Link* m_links[NET_SWITCH_PORTS];
    Net_Session_Link* m_pending[NET_SWITCH_MACHINES];
    Net_Session_Peer m_peers[NET_SWITCH_PORTS];
    Net_Session_Event m_events[NET_SESSION_EVENTS];
    int m_event_count;
    NetworkSerialQueue m_serial;
    bool m_serial_warned;
    u64 m_last_keepalive;
    u32 m_dropped;
    u8 m_datagram[2048];
};

#endif /* NET_SESSION_H */
