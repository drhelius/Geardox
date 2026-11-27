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

#define EMU_NET_IMPORT
#include "emu_net.h"

#include <SDL3/SDL.h>
#include "emu.h"
#include "config.h"
#include "gui_notifications.h"
#include "net/net_session.h"
#include "net/net_socket.h"
#include "network/net_switch.h"

static NetSession* session = NULL;
static NetSwitch* local_switch = NULL;
static NetworkPort* local_port = NULL;
static bool state_notice = false;
static int command_line_mode = 0;
static std::string command_line_host;
static int command_line_port = 0;

static void local_output(void* context, int port, const u8* data, int size);
static void move_serial(NetworkPort* port);
static void notify_events(void);
static std::string session_name(void);

void emu_net_init(void)
{
    net_socket_startup();
    session = new NetSession();
    local_switch = new NetSwitch();
    local_switch->SetOutput(local_output, NULL);
    local_switch->OpenPort(1);
    emu_net_start_command_line();
}

void emu_net_destroy(void)
{
    SafeDelete(session);
    SafeDelete(local_switch);
    net_socket_cleanup();
}

void emu_net_update(bool loading)
{
    if (!IsValidPointer(session))
        return;

    NetworkPort* port = loading ? NULL : emu_get_core()->GetNetworkPort();
    u64 now = SDL_GetTicks();

    if (session->GetMode() != NET_SESSION_OFF)
        session->Update(port, now);
    else if (!loading)
    {
        local_port = port;
        int size = 0;
        const u8* data = NULL;

        while ((data = port->PeekSent(&size)) != NULL)
        {
            u8 frame[NETWORK_FRAME_MAX];
            memcpy(frame, data, size);
            port->PopSent();
            local_switch->Input(1, frame, size);
        }
    }

    if (!loading)
        move_serial(port);

    notify_events();
}

static void move_serial(NetworkPort* port)
{
    if (!port->HasSerial())
        return;

    int channel = 0;
    int kind = 0;
    int peer = 0;
    int size = 0;
    u8 data[NETWORK_SERIAL_RECORD];

    while (port->TakeSentSerial(&channel, &kind, &peer, data, &size))
    {
        if (session->GetMode() != NET_SESSION_OFF)
            session->SendSerial(channel, peer, kind, data, size);
        else if (kind == NETWORK_SERIAL_CALL)
            port->DeliverSerial(channel, NETWORK_SERIAL_HANGUP, 0, NULL, 0);
    }

    Net_Session_Serial serial;

    while (session->TakeSerial(&serial))
        port->DeliverSerial(serial.channel, serial.kind, serial.from, serial.data, serial.size);
}

bool emu_net_host(void)
{
    std::string error;
    state_notice = false;

    if (!session->Host((u16)config_network.port, session_name().c_str(), config_network.password.c_str(),
        config_network.machines, &error))
    {
        gui_notify(gui_NotificationError, ICON_MD_LAN, "Unable to host", error.c_str(), "network");
        return false;
    }

    local_switch->Reset();
    local_switch->OpenPort(1);
    char text[64];
    snprintf(text, sizeof(text), "Hosting on port %d", config_network.port);
    gui_notify(gui_NotificationSuccess, ICON_MD_LAN, text, NULL, "network");
    return true;
}

bool emu_net_join(void)
{
    std::string host = config_network.address.empty() ? "127.0.0.1" : config_network.address;
    int port = config_network.port;
    size_t colon = host.rfind(':');

    if (colon != std::string::npos)
    {
        int parsed = atoi(host.substr(colon + 1).c_str());

        if ((parsed > 0) && (parsed < 65536))
            port = parsed;

        host = host.substr(0, colon);
    }

    std::string error;
    state_notice = false;

    if (!session->Join(host.c_str(), (u16)port, session_name().c_str(), config_network.password.c_str(), &error))
    {
        gui_notify(gui_NotificationError, ICON_MD_LAN, "Unable to join", error.c_str(), "network");
        return false;
    }

    config_push_recent_host(host + ":" + std::to_string(port));
    return true;
}

void emu_net_leave(void)
{
    if (!IsValidPointer(session) || (session->GetMode() == NET_SESSION_OFF))
        return;

    session->Leave();
    gui_notify(gui_NotificationInfo, ICON_MD_LAN, "Left the session", NULL, "network");
}

bool emu_net_is_active(void)
{
    return IsValidPointer(session) && (session->GetMode() != NET_SESSION_OFF);
}

bool emu_net_is_connected(void)
{
    return IsValidPointer(session) && (session->GetState() == NET_SESSION_CONNECTED);
}

bool emu_net_has_device(void)
{
    return emu_get_core()->GetNE2000()->IsEnabled() || emu_get_core()->GetNetworkPort()->HasSerial();
}

NetSession* emu_net_get_session(void)
{
    return session;
}

void emu_net_state_loaded(void)
{
    emu_get_core()->GetNetworkPort()->Clear();

    if (!emu_net_is_connected() || state_notice)
        return;

    state_notice = true;
    gui_notify(gui_NotificationWarning, ICON_MD_LAN, "This machine went back in time",
        "The other machines go on, which most network games don't survive", "network",
        gui_NotificationDurationLong);
}

void emu_net_set_command_line(int mode, const char* host, int port)
{
    command_line_mode = mode;
    command_line_host = IsValidPointer(host) ? host : "";
    command_line_port = port;
}

void emu_net_start_command_line(void)
{
    if (command_line_mode == 0)
        return;

    if (command_line_port > 0)
        config_network.port = command_line_port;

    if (command_line_mode == 1)
        emu_net_host();
    else
    {
        config_network.address = command_line_host;
        emu_net_join();
    }

    command_line_mode = 0;
}

int emu_net_get_local_addresses(u32* addresses, int max)
{
    return net_socket_local_addresses(addresses, max);
}

bool emu_net_get_bar_text(char* text, int size)
{
    if (!emu_net_is_active())
        return false;

    if (session->GetState() == NET_SESSION_CONNECTING)
        snprintf(text, size, "NET: JOINING");
    else if (session->GetMode() == NET_SESSION_HOST)
        snprintf(text, size, "NET: HOST %d/%d", session->GetMachineCount(), session->GetMaxMachines());
    else
        snprintf(text, size, "NET: %d/%d", session->GetMachine(), session->GetMachineCount());

    return true;
}

static void local_output(void* context, int port, const u8* data, int size)
{
    UNUSED(context);

    if ((port == 1) && IsValidPointer(local_port))
        local_port->Deliver(data, size);
}

static void notify_events(void)
{
    Net_Session_Event event;

    while (session->TakeEvent(&event))
    {
        switch (event.type)
        {
            case NET_SESSION_EVENT_JOINED:
                gui_notify(gui_NotificationInfo, ICON_MD_LAN, event.text, NULL, "network");
                break;
            case NET_SESSION_EVENT_LEFT:
                gui_notify(gui_NotificationInfo, ICON_MD_LAN, event.text, NULL, "network");
                break;
            case NET_SESSION_EVENT_CONNECTED:
            {
                char text[64];
                snprintf(text, sizeof(text), "Joined as machine %d", event.machine);
                gui_notify(gui_NotificationSuccess, ICON_MD_LAN, text, session->GetHostText(), "network");
                break;
            }
            case NET_SESSION_EVENT_ENDED:
                gui_notify(gui_NotificationWarning, ICON_MD_LINK_OFF, "The session ended", event.text, "network");
                break;
            case NET_SESSION_EVENT_FAILED:
                gui_notify(gui_NotificationError, ICON_MD_LINK_OFF, "Unable to join", event.text, "network");
                break;
            case NET_SESSION_EVENT_DUPLICATE:
                gui_notify(gui_NotificationWarning, ICON_MD_LAN, "Two machines share one network address", event.text,
                    "network", gui_NotificationDurationLong);
                break;
            case NET_SESSION_EVENT_SERIAL:
                gui_notify(gui_NotificationWarning, ICON_MD_CABLE, "The serial link is unconnected", event.text, "network",
                    gui_NotificationDurationLong);
                break;
        }
    }
}

static std::string session_name(void)
{
    if (!config_network.name.empty())
        return config_network.name;

    return "Geardox";
}
