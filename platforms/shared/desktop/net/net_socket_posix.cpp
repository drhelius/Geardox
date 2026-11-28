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
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>

static bool set_non_blocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    return (flags >= 0) && (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
}

static void no_sigpipe(int fd)
{
#if defined(SO_NOSIGPIPE)
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#else
    (void)fd;
#endif
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
    return (errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR);
}

bool net_socket_startup(void)
{
    return true;
}

void net_socket_cleanup(void)
{
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

net_socket net_socket_listen(uint32_t ip, uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0)
        return NET_SOCKET_INVALID;

    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    struct sockaddr_in address;
    set_address(&address, ip, port);

    if ((bind(fd, (struct sockaddr*)&address, sizeof(address)) != 0) || (listen(fd, 8) != 0) || !set_non_blocking(fd))
    {
        close(fd);
        return NET_SOCKET_INVALID;
    }

    return fd;
}

net_socket net_socket_accept(net_socket listener, uint32_t* ip, uint16_t* port)
{
    struct sockaddr_in address;
    socklen_t length = sizeof(address);
    int fd = accept((int)listener, (struct sockaddr*)&address, &length);

    if (fd < 0)
        return NET_SOCKET_INVALID;

    if (!set_non_blocking(fd))
    {
        close(fd);
        return NET_SOCKET_INVALID;
    }

    int on = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
    no_sigpipe(fd);
    *ip = ntohl(address.sin_addr.s_addr);
    *port = ntohs(address.sin_port);
    return fd;
}

net_socket net_socket_connect(uint32_t ip, uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0)
        return NET_SOCKET_INVALID;

    if (!set_non_blocking(fd))
    {
        close(fd);
        return NET_SOCKET_INVALID;
    }

    int on = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
    no_sigpipe(fd);
    struct sockaddr_in address;
    set_address(&address, ip, port);

    if ((connect(fd, (struct sockaddr*)&address, sizeof(address)) != 0) && (errno != EINPROGRESS))
    {
        close(fd);
        return NET_SOCKET_INVALID;
    }

    return fd;
}

int net_socket_connected(net_socket socket)
{
    struct sockaddr_in address;
    socklen_t length = sizeof(address);

    if (getpeername((int)socket, (struct sockaddr*)&address, &length) == 0)
        return 1;

    int error = 0;
    socklen_t error_length = sizeof(error);

    if ((getsockopt((int)socket, SOL_SOCKET, SO_ERROR, &error, &error_length) != 0) || ((error != 0) &&
        (error != EINPROGRESS) && (error != EALREADY)))
        return -1;

    return 0;
}

int net_socket_send(net_socket socket, const void* data, int size)
{
#if defined(MSG_NOSIGNAL)
    int flags = MSG_NOSIGNAL;
#else
    int flags = 0;
#endif
    ssize_t sent = send((int)socket, data, (size_t)size, flags);

    if (sent < 0)
        return would_block() ? NET_SOCKET_WOULD_BLOCK : NET_SOCKET_ERROR;

    return (int)sent;
}

int net_socket_receive(net_socket socket, void* data, int size)
{
    ssize_t received = recv((int)socket, data, (size_t)size, 0);

    if (received < 0)
        return would_block() ? NET_SOCKET_WOULD_BLOCK : NET_SOCKET_ERROR;

    return received == 0 ? NET_SOCKET_ERROR : (int)received;
}

net_socket net_socket_udp(uint32_t ip, uint16_t port)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (fd < 0)
        return NET_SOCKET_INVALID;

    struct sockaddr_in address;
    set_address(&address, ip, port);

    if ((bind(fd, (struct sockaddr*)&address, sizeof(address)) != 0) || !set_non_blocking(fd))
    {
        close(fd);
        return NET_SOCKET_INVALID;
    }

    return fd;
}

int net_socket_send_to(net_socket socket, uint32_t ip, uint16_t port, const void* data, int size)
{
    struct sockaddr_in address;
    set_address(&address, ip, port);
    ssize_t sent = sendto((int)socket, data, (size_t)size, 0, (struct sockaddr*)&address, sizeof(address));

    if (sent < 0)
        return would_block() ? NET_SOCKET_WOULD_BLOCK : NET_SOCKET_ERROR;

    return (int)sent;
}

int net_socket_receive_from(net_socket socket, void* data, int size, uint32_t* ip, uint16_t* port)
{
    struct sockaddr_in address;
    socklen_t length = sizeof(address);
    ssize_t received = recvfrom((int)socket, data, (size_t)size, 0, (struct sockaddr*)&address, &length);

    if (received < 0)
        return would_block() || (errno == ECONNREFUSED) ? NET_SOCKET_WOULD_BLOCK : NET_SOCKET_ERROR;

    *ip = ntohl(address.sin_addr.s_addr);
    *port = ntohs(address.sin_port);
    return (int)received;
}

void net_socket_close(net_socket socket)
{
    if (socket != NET_SOCKET_INVALID)
        close((int)socket);
}

int net_socket_local_addresses(uint32_t* addresses, int max)
{
    struct ifaddrs* list = NULL;

    if (getifaddrs(&list) != 0)
        return 0;

    int count = 0;

    for (struct ifaddrs* item = list; (item != NULL) && (count < max); item = item->ifa_next)
    {
        if ((item->ifa_addr == NULL) || (item->ifa_addr->sa_family != AF_INET) || !(item->ifa_flags & IFF_UP) ||
            (item->ifa_flags & IFF_LOOPBACK))
            continue;

        uint32_t ip = ntohl(((struct sockaddr_in*)item->ifa_addr)->sin_addr.s_addr);
        bool known = false;

        for (int i = 0; i < count; i++)
            known = known || (addresses[i] == ip);

        if (!known && ((ip >> 16) != 0xA9FE))
            addresses[count++] = ip;
    }

    freeifaddrs(list);
    return count;
}
