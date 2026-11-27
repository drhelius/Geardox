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

static const GuiColor& network_connected_color = green;
static const GuiColor& network_waiting_color = amber;
static bool open_add_card = false;
static int pending_action = 0;
static char address_text[256] = "";
static char name_text[64] = "";
static char password_text[64] = "";
static bool texts_loaded = false;

static void start_action(int action);
static void draw_state(NetSession* session);
static void draw_machines(NetSession* session);
static void draw_settings(bool active);
static void draw_host_addresses(void);
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

    if (ImGui::MenuItem("Host", NULL, false, !active))
        start_action(1);

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Other machines join this one on the port below.\nFor play over the internet, forward this "
            "port (TCP and UDP)\nin the router to this computer.");

    if (ImGui::MenuItem("Join", NULL, false, !active))
        start_action(2);

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Joins the machine hosting at the address and port below.");

    if (ImGui::MenuItem("Leave", NULL, false, active))
        emu_net_leave();

    ImGui::Separator();
    draw_state(session);
    draw_machines(session);
    ImGui::Separator();
    draw_settings(active);

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

static void draw_state(NetSession* session)
{
    switch (session->GetState())
    {
        case NET_SESSION_CONNECTING:
            ImGui::TextColored(network_waiting_color, "Joining %s", session->GetHostText());
            break;
        case NET_SESSION_CONNECTED:
            if (session->GetMode() == NET_SESSION_HOST)
                ImGui::TextColored(network_connected_color, "Hosting on port %u", session->GetPort());
            else
                ImGui::TextColored(network_connected_color, "Joined %s", session->GetHostText());

            ImGui::TextDisabled("Machine %d of %d", session->GetMachine(), session->GetMachineCount());

            if (!emu_net_has_device())
                ImGui::TextColored(network_waiting_color, "This machine has no network card");
            break;
        case NET_SESSION_FAILED:
            ImGui::TextColored(red, "%s", session->GetError());
            break;
        default:
            ImGui::TextColored(red, "Not connected");
            break;
    }
}

static void draw_machines(NetSession* session)
{
    if (session->GetState() != NET_SESSION_CONNECTED)
        return;

    if (!ImGui::BeginMenu("Machines"))
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
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(own ? network_connected_color : white, "%d", i);
            ImGui::TableNextColumn();
            ImGui::TextColored(own ? network_connected_color : white, "%s", own ? config_network.name.empty() ?
                peer->name : config_network.name.c_str() : peer->name);
            ImGui::TableNextColumn();

            if (own)
                ImGui::TextDisabled("this machine");
            else if (peer->rtt_ms >= 0)
                ImGui::TextDisabled("%d ms", peer->rtt_ms);
            else
                ImGui::TextDisabled("-");
        }

        ImGui::EndTable();
    }

    ImGui::EndMenu();
}

static void draw_settings(bool active)
{
    ImGui::BeginDisabled(active);

    ImGui::Text("Address:");
    ImGui::SameLine(90.0f);
    ImGui::SetNextItemWidth(170.0f);

    if (ImGui::InputTextWithHint("##network_address", "127.0.0.1", address_text, sizeof(address_text),
        ImGuiInputTextFlags_AutoSelectAll))
        config_network.address = address_text;

    ImGui::Text("Port:");
    ImGui::SameLine(90.0f);
    ImGui::SetNextItemWidth(60.0f);

    if (ImGui::InputInt("##network_port", &config_network.port, 0, 0))
        config_network.port = CLAMP(config_network.port, 1, 65535);

    if (ImGui::BeginMenu("Recent", config_network.recent[0].length() > 0))
    {
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

    ImGui::Separator();

    ImGui::Text("Name:");
    ImGui::SameLine(90.0f);
    ImGui::SetNextItemWidth(170.0f);

    if (ImGui::InputTextWithHint("##network_name", GDOX_TITLE, name_text, sizeof(name_text),
        ImGuiInputTextFlags_AutoSelectAll))
        config_network.name = name_text;

    ImGui::Text("Password:");
    ImGui::SameLine(90.0f);
    ImGui::SetNextItemWidth(170.0f);

    if (ImGui::InputTextWithHint("##network_password", "none", password_text, sizeof(password_text),
        ImGuiInputTextFlags_Password | ImGuiInputTextFlags_AutoSelectAll))
        config_network.password = password_text;

    if (ImGui::BeginMenu("Max Machines"))
    {
        char label[16];
        snprintf(label, sizeof(label), "%d", config_network.machines);
        float text_width = ImGui::CalcTextSize("8").x;
        ImGui::PushItemWidth(gui_get_combo_width(text_width));

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
            ImGui::SetTooltip("The most machines a hosted session lets in, this one included.");

        ImGui::PopItemWidth();
        ImGui::EndMenu();
    }

    ImGui::EndDisabled();
    draw_host_addresses();
}

static void draw_host_addresses(void)
{
    if (!ImGui::BeginMenu("Host Addresses"))
        return;

    u32 addresses[NET_SOCKET_ADDRESSES];
    int count = emu_net_get_local_addresses(addresses, NET_SOCKET_ADDRESSES);

    if (count == 0)
        ImGui::TextDisabled("No network");

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
    }

    ImGui::Separator();
    ImGui::TextDisabled("Over the internet, give the router's public address");
    ImGui::EndMenu();
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
