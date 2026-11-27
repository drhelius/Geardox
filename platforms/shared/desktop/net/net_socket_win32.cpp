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

#include "net_socket.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <mstcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool started = false;

static bool set_non_blocking(SOCKET s)
{
    u_long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
}

static void set_address(struct sockaddr_in* address, uint32_t ip, uint16_t port)
{
    memset(address, 0, sizeof(*address));
    address->sin_family = AF_INET;
    address->sin_addr.s_addr = htonl(ip);
    address->sin_port = htons(port);
}

static bool would_block(void)
{
    int error = WSAGetLastError();
    return (error == WSAEWOULDBLOCK) || (error == WSAEINTR) || (error == WSAECONNRESET) || (error == WSAEMSGSIZE);
}

bool net_socket_startup(void)
{
    if (started)
        return true;

    WSADATA data;
    started = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    return started;
}

void net_socket_cleanup(void)
{
    if (started)
        WSACleanup();

    started = false;
}

bool net_socket_resolve(const char* host, uint32_t* ip)
{
    struct in_addr literal;

    if (inet_pton(AF_INET, host, &literal) == 1)
    {
        *ip = ntohl(literal.s_addr);
        return true;
    }

    struct addrinfo hints;
    struct addrinfo* result = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if ((getaddrinfo(host, NULL, &hints, &result) != 0) || (result == NULL))
        return false;

    *ip = ntohl(((struct sockaddr_in*)result->ai_addr)->sin_addr.s_addr);
    freeaddrinfo(result);
    return true;
}

void net_socket_format(uint32_t ip, char* text, int size)
{
    snprintf(text, size, "%u.%u.%u.%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
}

net_socket net_socket_listen(uint16_t port)
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);

    if (s == INVALID_SOCKET)
        return NET_SOCKET_INVALID;

    BOOL exclusive = TRUE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&exclusive, sizeof(exclusive));
    struct sockaddr_in address;
    set_address(&address, INADDR_ANY, port);

    if ((bind(s, (struct sockaddr*)&address, sizeof(address)) != 0) || (listen(s, 8) != 0) || !set_non_blocking(s))
    {
        closesocket(s);
        return NET_SOCKET_INVALID;
    }

    return (net_socket)s;
}

net_socket net_socket_accept(net_socket listener, uint32_t* ip, uint16_t* port)
{
    struct sockaddr_in address;
    int length = sizeof(address);
    SOCKET s = accept((SOCKET)listener, (struct sockaddr*)&address, &length);

    if (s == INVALID_SOCKET)
        return NET_SOCKET_INVALID;

    if (!set_non_blocking(s))
    {
        closesocket(s);
        return NET_SOCKET_INVALID;
    }

    BOOL on = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&on, sizeof(on));
    *ip = ntohl(address.sin_addr.s_addr);
    *port = ntohs(address.sin_port);
    return (net_socket)s;
}

net_socket net_socket_connect(uint32_t ip, uint16_t port)
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);

    if (s == INVALID_SOCKET)
        return NET_SOCKET_INVALID;

    if (!set_non_blocking(s))
    {
        closesocket(s);
        return NET_SOCKET_INVALID;
    }

    BOOL on = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&on, sizeof(on));
    struct sockaddr_in address;
    set_address(&address, ip, port);

    if ((connect(s, (struct sockaddr*)&address, sizeof(address)) != 0) && (WSAGetLastError() != WSAEWOULDBLOCK))
    {
        closesocket(s);
        return NET_SOCKET_INVALID;
    }

    return (net_socket)s;
}

int net_socket_connected(net_socket socket)
{
    fd_set writable;
    fd_set failed;
    FD_ZERO(&writable);
    FD_ZERO(&failed);
    FD_SET((SOCKET)socket, &writable);
    FD_SET((SOCKET)socket, &failed);
    struct timeval wait = { 0, 0 };

    if (select(0, NULL, &writable, &failed, &wait) == SOCKET_ERROR)
        return -1;

    if (FD_ISSET((SOCKET)socket, &failed))
        return -1;

    return FD_ISSET((SOCKET)socket, &writable) ? 1 : 0;
}

int net_socket_send(net_socket socket, const void* data, int size)
{
    int sent = send((SOCKET)socket, (const char*)data, size, 0);

    if (sent == SOCKET_ERROR)
        return WSAGetLastError() == WSAEWOULDBLOCK ? NET_SOCKET_WOULD_BLOCK : NET_SOCKET_ERROR;

    return sent;
}

int net_socket_receive(net_socket socket, void* data, int size)
{
    int received = recv((SOCKET)socket, (char*)data, size, 0);

    if (received == SOCKET_ERROR)
        return WSAGetLastError() == WSAEWOULDBLOCK ? NET_SOCKET_WOULD_BLOCK : NET_SOCKET_ERROR;

    return received == 0 ? NET_SOCKET_ERROR : received;
}

net_socket net_socket_udp(uint16_t port)
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);

    if (s == INVALID_SOCKET)
        return NET_SOCKET_INVALID;

    struct sockaddr_in address;
    set_address(&address, INADDR_ANY, port);

    if ((bind(s, (struct sockaddr*)&address, sizeof(address)) != 0) || !set_non_blocking(s))
    {
        closesocket(s);
        return NET_SOCKET_INVALID;
    }

    BOOL off = FALSE;
    DWORD returned = 0;
    WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof(off), NULL, 0, &returned, NULL, NULL);
    return (net_socket)s;
}

int net_socket_send_to(net_socket socket, uint32_t ip, uint16_t port, const void* data, int size)
{
    struct sockaddr_in address;
    set_address(&address, ip, port);
    int sent = sendto((SOCKET)socket, (const char*)data, size, 0, (struct sockaddr*)&address, sizeof(address));

    if (sent == SOCKET_ERROR)
        return would_block() ? NET_SOCKET_WOULD_BLOCK : NET_SOCKET_ERROR;

    return sent;
}

int net_socket_receive_from(net_socket socket, void* data, int size, uint32_t* ip, uint16_t* port)
{
    struct sockaddr_in address;
    int length = sizeof(address);
    int received = recvfrom((SOCKET)socket, (char*)data, size, 0, (struct sockaddr*)&address, &length);

    if (received == SOCKET_ERROR)
        return would_block() ? NET_SOCKET_WOULD_BLOCK : NET_SOCKET_ERROR;

    *ip = ntohl(address.sin_addr.s_addr);
    *port = ntohs(address.sin_port);
    return received;
}

void net_socket_close(net_socket socket)
{
    if (socket != NET_SOCKET_INVALID)
        closesocket((SOCKET)socket);
}

int net_socket_local_addresses(uint32_t* addresses, int max)
{
    ULONG size = 16384;
    IP_ADAPTER_ADDRESSES* list = (IP_ADAPTER_ADDRESSES*)malloc(size);

    if (list == NULL)
        return 0;

    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG result = GetAdaptersAddresses(AF_INET, flags, NULL, list, &size);

    if (result == ERROR_BUFFER_OVERFLOW)
    {
        free(list);
        list = (IP_ADAPTER_ADDRESSES*)malloc(size);

        if (list == NULL)
            return 0;

        result = GetAdaptersAddresses(AF_INET, flags, NULL, list, &size);
    }

    int count = 0;

    for (IP_ADAPTER_ADDRESSES* adapter = list; (result == NO_ERROR) && (adapter != NULL) && (count < max);
        adapter = adapter->Next)
    {
        if ((adapter->OperStatus != IfOperStatusUp) || (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK))
            continue;

        for (IP_ADAPTER_UNICAST_ADDRESS* item = adapter->FirstUnicastAddress; (item != NULL) && (count < max);
            item = item->Next)
        {
            if (item->Address.lpSockaddr->sa_family != AF_INET)
                continue;

            uint32_t ip = ntohl(((struct sockaddr_in*)item->Address.lpSockaddr)->sin_addr.s_addr);

            if ((ip >> 16) != 0xA9FE)
                addresses[count++] = ip;
        }
    }

    free(list);
    return count;
}
