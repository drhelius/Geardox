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

#include "gdb_server.h"
#include <chrono>

#if !defined(_WIN32)
    #include <netinet/tcp.h>
#endif


GdbServer::GdbServer()
{
    m_running = false;
    m_connected = false;
    m_no_ack = false;
    m_connection = 0;
    m_listen = INVALID_SOCKET_VALUE;
    m_client = INVALID_SOCKET_VALUE;
    m_port = 0;
}

GdbServer::~GdbServer()
{
    Stop();
}

bool GdbServer::Start(int port)
{
    if (m_running)
        return true;

#ifdef _WIN32
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif

    m_listen = socket(AF_INET, SOCK_STREAM, 0);

    if (m_listen == INVALID_SOCKET_VALUE)
    {
        Error("[GDB] Failed to create socket");
        return false;
    }

    int opt = 1;
    setsockopt(m_listen, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if ((bind(m_listen, (struct sockaddr*)&addr, sizeof(addr)) < 0) || (listen(m_listen, 1) < 0))
    {
        Error("[GDB] Failed to listen on 127.0.0.1:%d", port);
        SOCKET_CLOSE(m_listen);
        m_listen = INVALID_SOCKET_VALUE;
        return false;
    }

    m_port = port;
    m_running = true;
    m_thread = std::thread(&GdbServer::Run, this);
    Log("[GDB] Remote server listening on 127.0.0.1:%d", port);
    return true;
}

void GdbServer::Stop()
{
    if (!m_running)
        return;

    m_running = false;
    CloseClient();

    if (m_listen != INVALID_SOCKET_VALUE)
    {
        SOCKET_SHUTDOWN(m_listen);
        SOCKET_CLOSE(m_listen);
        m_listen = INVALID_SOCKET_VALUE;
    }

    if (m_thread.joinable())
        m_thread.join();

    m_queue_cv.notify_all();
    Log("[GDB] Remote server stopped");
}

bool GdbServer::IsRunning() const
{
    return m_running;
}

bool GdbServer::IsConnected() const
{
    return m_connected;
}

int GdbServer::GetPort() const
{
    return m_port;
}

unsigned int GdbServer::GetConnection() const
{
    return m_connection;
}

bool GdbServer::Pop(std::string& packet, int timeout_ms)
{
    std::unique_lock<std::mutex> lock(m_queue_mutex);

    if (m_queue.empty() && (timeout_ms > 0))
        m_queue_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms));

    if (m_queue.empty())
        return false;

    packet = m_queue.front();
    m_queue.pop_front();
    return true;
}

bool GdbServer::Send(const std::string& payload)
{
    std::string frame = "$";
    u8 checksum = 0;

    for (size_t i = 0; i < payload.size(); i++)
    {
        char c = payload[i];

        if ((c == '$') || (c == '#') || (c == '}') || (c == '*'))
        {
            frame += '}';
            checksum += (u8)'}';
            c ^= 0x20;
        }

        frame += c;
        checksum += (u8)c;
    }

    frame += '#';
    hex_encode(&checksum, 1, &frame, false);

    {
        std::lock_guard<std::mutex> lock(m_send_mutex);
        m_last_sent = frame;
    }

    return SendRaw(frame);
}

void GdbServer::SetNoAck(bool no_ack)
{
    m_no_ack = no_ack;
}

void GdbServer::Run()
{
    while (m_running)
    {
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(m_listen, &read_set);
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;

        if (select((int)m_listen + 1, &read_set, NULL, NULL, &timeout) <= 0)
            continue;

        socket_t client = accept(m_listen, NULL, NULL);

        if (client == INVALID_SOCKET_VALUE)
            continue;

        int opt = 1;
        setsockopt(client, IPPROTO_TCP, TCP_NODELAY, (const char*)&opt, sizeof(opt));
        Serve(client);
    }
}

void GdbServer::Serve(socket_t client)
{
    {
        std::lock_guard<std::mutex> lock(m_send_mutex);
        m_client = client;
        m_last_sent.clear();
    }

    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        m_queue.clear();
    }

    m_input.clear();
    m_no_ack = false;
    m_connected = true;
    m_connection++;
    Log("[GDB] Client connected");

    char buffer[4096];

    while (m_running)
    {
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(client, &read_set);
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;
        int ready = select((int)client + 1, &read_set, NULL, NULL, &timeout);

        if (ready == 0)
            continue;

        if (ready < 0)
            break;

        int received = (int)recv(client, buffer, sizeof(buffer), 0);

        if (received <= 0)
            break;

        Receive(buffer, received);
    }

    m_connected = false;
    CloseClient();
    m_queue_cv.notify_all();
    Log("[GDB] Client disconnected");
}

void GdbServer::Receive(const char* data, int size)
{
    for (int i = 0; i < size; i++)
    {
        char c = data[i];

        if (m_input.empty())
        {
            if (c == 0x03)
                Push(GDB_SERVER_INTERRUPT);
            else if (c == '-')
            {
                std::string last;

                {
                    std::lock_guard<std::mutex> lock(m_send_mutex);
                    last = m_last_sent;
                }

                if (!last.empty())
                    SendRaw(last);
            }
            else if (c == '$')
                m_input += c;

            continue;
        }

        m_input += c;
        size_t hash = m_input.find('#');

        if ((hash == std::string::npos) || (m_input.size() < hash + 3))
        {
            if (m_input.size() > GDB_SERVER_PACKET_SIZE * 2)
                m_input.clear();

            continue;
        }

        u8 checksum = 0;
        std::string payload;

        for (size_t j = 1; j < hash; j++)
        {
            checksum += (u8)m_input[j];

            if ((m_input[j] == '}') && (j + 1 < hash))
            {
                j++;
                checksum += (u8)m_input[j];
                payload += (char)(m_input[j] ^ 0x20);
            }
            else
                payload += m_input[j];
        }

        char high = m_input[hash + 1];
        char low = m_input[hash + 2];
        bool valid = is_hex_digit(high) && is_hex_digit(low) && (((as_hex(high) << 4) | as_hex(low)) == checksum);
        m_input.clear();

        if (!m_no_ack)
            SendRaw(valid ? "+" : "-");

        if (valid)
            Push(payload);
    }
}

bool GdbServer::SendRaw(const std::string& data)
{
    std::lock_guard<std::mutex> lock(m_send_mutex);

    if (m_client == INVALID_SOCKET_VALUE)
        return false;

    size_t sent = 0;

    while (sent < data.size())
    {
        int result = (int)send(m_client, data.data() + sent, (int)(data.size() - sent), 0);

        if (result <= 0)
            return false;

        sent += (size_t)result;
    }

    return true;
}

void GdbServer::Push(const std::string& packet)
{
    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        m_queue.push_back(packet);
    }

    m_queue_cv.notify_all();
}

void GdbServer::CloseClient()
{
    std::lock_guard<std::mutex> lock(m_send_mutex);

    if (m_client == INVALID_SOCKET_VALUE)
        return;

    SOCKET_SHUTDOWN(m_client);
    SOCKET_CLOSE(m_client);
    m_client = INVALID_SOCKET_VALUE;
}
