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

#define GUI_NETWORK_IMPORT
#include "gui_network.h"

#include "imgui.h"
#include "gui.h"
#include "gui_colors.h"
#include "gui_actions.h"
#include "gui_notifications.h"
#include "application.h"
#include "config.h"
#include "emu.h"
#include "emu_game.h"
#include "emu_net.h"
#include "net/net_session.h"
#include "net/net_socket.h"

static const GuiColor& network_connected_color = cornflower;
static const GuiColor& network_joining_color = violet;
static const GuiColor& network_stopped_color = red;
static bool open_add_card = false;
static int pending_action = 0;
static char address_text[256] = "";
static char name_text[64] = "";
static char password_text[64] = "";
static bool texts_loaded = false;

static void start_action(int action);
static void menu_machines(NetSession* session);
static void menu_host(bool active);
static void menu_join(bool active);
static void menu_name(bool active);
static void menu_host_addresses(void);
static void menu_recent(void);
static void menu_max_machines(void);
static void draw_port(void);
static void draw_password(void);
static void draw_status(NetSession* session);
static void load_texts(void);

void gui_network_menu(void)
{
    if (!ImGui::BeginMenu("Network"))
        return;

    gui_in_use = true;
    load_texts();

    NetSession* session = emu_net_get_session();
    bool active = emu_net_is_active();

#if defined(__APPLE__)
    if (ImGui::MenuItem("New " GDOX_TITLE " Window", "", false, application_can_launch_new_instance()))
        application_launch_new_instance();

    ImGui::Separator();
#endif

    if (ImGui::MenuItem("Host Session", NULL, false, !active))
        start_action(1);

    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::Text("Other machines join this one on the host port.");
        ImGui::Text("Over the internet, forward that port (TCP and UDP)");
        ImGui::Text("in the router to this computer.");
        ImGui::EndTooltip();
    }

    if (ImGui::MenuItem("Join Session", NULL, false, !active))
        start_action(2);

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Joins the machine hosting at the join address and port.");

    if (ImGui::MenuItem("Leave Session", NULL, false, active))
        emu_net_leave();

    ImGui::Separator();
    menu_machines(session);
    ImGui::Separator();
    menu_host(active);
    menu_join(active);
    menu_name(active);
    ImGui::Separator();
    draw_status(session);

    ImGui::EndMenu();
}

void gui_network_popups(void)
{
    if (open_add_card)
    {
        gui_dialog_in_use = true;
        ImGui::OpenPopup("Network Card");
        open_add_card = false;
    }

    gui_center_next_window();

    if (ImGui::BeginPopupModal("Network Card", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("This machine has no network card.");
        ImGui::Text("Add the NE2000 to %s and reset?", emu_game_is_open() ? emu_game_get_name() : "this machine");
        ImGui::NewLine();
        ImGui::Separator();

        if (ImGui::Button("Add and Reset", ImVec2(140, 0)))
        {
            gui_close_popup();
            config_Machine* machine = emu_game_get_machine();
            machine->network_card = GDOX_NETWORK_CARD_NE2000;
            emu_apply_machine_settings();

            if (pending_action == 1 ? emu_net_host() : emu_net_join())
                gui_action_reset();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(140, 0)))
            gui_close_popup();

        ImGui::SetItemDefaultFocus();
        ImGui::EndPopup();
    }
}

bool gui_network_get_status(char* text, int size)
{
    return emu_net_get_bar_text(text, size);
}

void gui_network_get_status_color(ImVec4* color)
{
    *color = emu_net_is_connected() ? network_connected_color : network_joining_color;
}

static void start_action(int action)
{
    config_network.address = address_text;
    config_network.name = name_text;
    config_network.password = password_text;

    if (!emu_is_empty() && !emu_net_has_device())
    {
        pending_action = action;
        open_add_card = true;
        return;
    }

    if (action == 1)
        emu_net_host();
    else
        emu_net_join();
}

static void menu_machines(NetSession* session)
{
    if (!ImGui::BeginMenu("Machines", session->GetState() == NET_SESSION_CONNECTED))
        return;

    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;

    if (ImGui::BeginTable("##network_machines", 3, flags))
    {
        for (int i = 1; i <= NET_SWITCH_MACHINES; i++)
        {
            const Net_Session_Peer* peer = session->GetPeer(i);

            if (!IsValidPointer(peer) || !peer->present)
                continue;

            bool own = i == session->GetMachine();
            const char* name = own && !config_network.name.empty() ? config_network.name.c_str() : peer->name;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(own ? network_connected_color : white, "%d", i);
            ImGui::TableNextColumn();
            ImGui::TextColored(own ? network_connected_color : white, "%s", name);
            ImGui::TableNextColumn();

            if (own)
                ImGui::TextDisabled("This machine");
            else if (peer->rtt_ms >= 0)
                ImGui::TextDisabled("%d ms", peer->rtt_ms);
            else
                ImGui::TextDisabled("-");
        }

        ImGui::EndTable();
    }

    ImGui::EndMenu();
}

static void menu_host(bool active)
{
    if (!ImGui::BeginMenu("Host"))
        return;

    ImGui::BeginDisabled(active);
    draw_port();
    draw_password();
    menu_max_machines();
    ImGui::EndDisabled();
    ImGui::Separator();
    menu_host_addresses();

    ImGui::EndMenu();
}

static void menu_join(bool active)
{
    if (!ImGui::BeginMenu("Join"))
        return;

    ImGui::BeginDisabled(active);
    ImGui::Text("Address:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("255.255.255.255").x * 1.6f);

    if (ImGui::InputTextWithHint("##network_address", "127.0.0.1", address_text, sizeof(address_text),
        ImGuiInputTextFlags_AutoSelectAll))
        config_network.address = address_text;

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The host's address, or its name.");

    draw_port();
    draw_password();
    ImGui::Separator();
    menu_recent();
    ImGui::EndDisabled();

    ImGui::EndMenu();
}

static void menu_name(bool active)
{
    if (!ImGui::BeginMenu("Name"))
        return;

    ImGui::BeginDisabled(active);
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("WWWWWWWWWWWWWWWW").x);

    if (ImGui::InputTextWithHint("##network_name", GDOX_TITLE, name_text, sizeof(name_text),
        ImGuiInputTextFlags_AutoSelectAll))
        config_network.name = name_text;

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("How the other machines list this one.");

    ImGui::EndDisabled();
    ImGui::EndMenu();
}

static void menu_host_addresses(void)
{
    if (!ImGui::BeginMenu("Addresses"))
        return;

    u32 addresses[NET_SOCKET_ADDRESSES];
    int count = emu_net_get_local_addresses(addresses, NET_SOCKET_ADDRESSES);

    for (int i = 0; i < count; i++)
    {
        char ip[32];
        char item[48];
        net_socket_format(addresses[i], ip, sizeof(ip));
        snprintf(item, sizeof(item), "%s:%d", ip, config_network.port);

        if (ImGui::MenuItem(item))
        {
            ImGui::SetClipboardText(item);
            gui_notify(gui_NotificationInfo, ICON_MD_CONTENT_COPY, "Copied", item, "network",
                gui_NotificationDurationShort);
        }

        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Copies this address for the joining machines.");
            ImGui::Text("Over the internet, they need the router's public address.");
            ImGui::EndTooltip();
        }
    }

    if (count == 0)
        ImGui::TextColored(network_stopped_color, "No network");

    ImGui::EndMenu();
}

static void menu_recent(void)
{
    if (!ImGui::BeginMenu("Recent Hosts", config_network.recent[0].length() > 0))
        return;

    for (int i = 0; i < config_max_recent_hosts; i++)
    {
        if (config_network.recent[i].length() == 0)
            break;

        if (ImGui::MenuItem(config_network.recent[i].c_str()))
        {
            std::string recent = config_network.recent[i];
            size_t colon = recent.rfind(':');
            std::string host = colon == std::string::npos ? recent : recent.substr(0, colon);
            strncpy_fit(address_text, host.c_str(), sizeof(address_text));
            config_network.address = host;

            if (colon != std::string::npos)
                config_network.port = CLAMP(atoi(recent.substr(colon + 1).c_str()), 1, 65535);
        }
    }

    ImGui::EndMenu();
}

static void menu_max_machines(void)
{
    if (!ImGui::BeginMenu("Max Machines"))
        return;

    char label[16];
    snprintf(label, sizeof(label), "%d", config_network.machines);
    ImGui::PushItemWidth(gui_get_combo_width(ImGui::CalcTextSize("8").x));

    if (ImGui::BeginCombo("##network_machines_limit", label))
    {
        for (int i = 2; i <= NET_SWITCH_MACHINES; i++)
        {
            snprintf(label, sizeof(label), "%d", i);

            if (ImGui::Selectable(label, config_network.machines == i))
                config_network.machines = i;
        }

        ImGui::EndCombo();
    }
    else if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The most machines the session lets in, this one included.");

    ImGui::PopItemWidth();
    ImGui::EndMenu();
}

static void draw_port(void)
{
    ImGui::Text("Port:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("000000").x + (ImGui::GetStyle().FramePadding.x * 2.0f));

    if (ImGui::InputInt("##network_port", &config_network.port, 0, 0))
        config_network.port = CLAMP(config_network.port, 1, 65535);

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("TCP and UDP, the same on the host and the joining machines.");
}

static void draw_password(void)
{
    ImGui::Text("Password:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("WWWWWWWWWWWW").x);

    if (ImGui::InputTextWithHint("##network_password", "None", password_text, sizeof(password_text),
        ImGuiInputTextFlags_Password | ImGuiInputTextFlags_AutoSelectAll))
        config_network.password = password_text;

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The host's password; joining machines need the same.");
}

static void draw_status(NetSession* session)
{
    switch (session->GetState())
    {
        case NET_SESSION_CONNECTING:
            ImGui::TextColored(network_joining_color, "Joining %s", session->GetHostText());
            break;
        case NET_SESSION_CONNECTED:
            if (!emu_net_has_device())
                ImGui::TextColored(network_stopped_color, "This machine has no network card");
            else if (session->GetMode() == NET_SESSION_HOST)
                ImGui::TextColored(network_connected_color, "Hosting on port %u, %d of %d machines", session->GetPort(),
                    session->GetMachineCount(), session->GetMaxMachines());
            else
                ImGui::TextColored(network_connected_color, "Machine %d of %d on %s", session->GetMachine(),
                    session->GetMachineCount(), session->GetHostText());
            break;
        case NET_SESSION_FAILED:
            ImGui::TextColored(network_stopped_color, "%s", session->GetError());
            break;
        default:
            ImGui::TextColored(network_stopped_color, "No session");
            break;
    }
}

static void load_texts(void)
{
    if (texts_loaded)
        return;

    strncpy_fit(address_text, config_network.address.c_str(), sizeof(address_text));
    strncpy_fit(name_text, config_network.name.c_str(), sizeof(name_text));
    strncpy_fit(password_text, config_network.password.c_str(), sizeof(password_text));
    texts_loaded = true;
}
