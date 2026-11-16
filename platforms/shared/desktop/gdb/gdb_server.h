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

#ifndef GDB_SERVER_H
#define GDB_SERVER_H

#include <string>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include "geardox.h"
#include "../mcp/mcp_transport.h"

#define GDB_SERVER_PACKET_SIZE 0x4000
#define GDB_SERVER_INTERRUPT "\x03"

class GdbServer
{
public:
    GdbServer();
    ~GdbServer();
    bool Start(int port);
    void Stop();
    bool IsRunning() const;
    bool IsConnected() const;
    int GetPort() const;
    unsigned int GetConnection() const;
    bool Pop(std::string& packet, int timeout_ms);
    bool Send(const std::string& payload);
    void SetNoAck(bool no_ack);

private:
    void Run();
    void Serve(socket_t client);
    void Receive(const char* data, int size);
    bool SendRaw(const std::string& data);
    void Push(const std::string& packet);
    void CloseClient();

private:
    std::thread m_thread;
    std::atomic<bool> m_running;
    std::atomic<bool> m_connected;
    std::atomic<bool> m_no_ack;
    std::atomic<unsigned int> m_connection;
    socket_t m_listen;
    socket_t m_client;
    int m_port;
    std::mutex m_queue_mutex;
    std::condition_variable m_queue_cv;
    std::deque<std::string> m_queue;
    std::mutex m_send_mutex;
    std::string m_last_sent;
    std::string m_input;
};

#endif /* GDB_SERVER_H */
