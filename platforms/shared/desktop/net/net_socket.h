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

#ifndef NET_SOCKET_H
#define NET_SOCKET_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

#define NET_SOCKET_INVALID ((intptr_t)-1)
#define NET_SOCKET_WOULD_BLOCK 0
#define NET_SOCKET_ERROR -1
#define NET_SOCKET_ADDRESSES 8

typedef intptr_t net_socket;

bool net_socket_startup(void);
void net_socket_cleanup(void);
bool net_socket_resolve(const char* host, uint32_t* ip);
net_socket net_socket_listen(uint16_t port);
net_socket net_socket_accept(net_socket listener, uint32_t* ip, uint16_t* port);
net_socket net_socket_connect(uint32_t ip, uint16_t port);
int net_socket_connected(net_socket socket);
int net_socket_send(net_socket socket, const void* data, int size);
int net_socket_receive(net_socket socket, void* data, int size);
net_socket net_socket_udp(uint16_t port);
int net_socket_send_to(net_socket socket, uint32_t ip, uint16_t port, const void* data, int size);
int net_socket_receive_from(net_socket socket, void* data, int size, uint32_t* ip, uint16_t* port);
void net_socket_close(net_socket socket);
int net_socket_local_addresses(uint32_t* addresses, int max);

inline void net_socket_format(uint32_t ip, char* text, int size)
{
    snprintf(text, size, "%u.%u.%u.%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
}

inline std::string net_socket_split_port(const std::string& text, int* port)
{
    size_t colon = text.rfind(':');

    if (colon == std::string::npos)
        return text;

    int parsed = atoi(text.substr(colon + 1).c_str());

    if ((parsed > 0) && (parsed < 65536))
        *port = parsed;

    return text.substr(0, colon);
}

#endif /* NET_SOCKET_H */
