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

#ifndef SERIAL_LINK_H
#define SERIAL_LINK_H

#include "../common/common.h"
#include "network_port.h"

#define SERIAL_LINK_BUFFER 4096

enum SerialLink_Kind
{
    SERIAL_LINK_NONE = 0,
    SERIAL_LINK_NULL_MODEM,
    SERIAL_LINK_MODEM,
    SERIAL_LINK_KIND_COUNT
};

class UART;
class Modem;
class StateSerializer;

class SerialLink
{
public:
    struct SerialLink_State
    {
        u8 rx[SERIAL_LINK_BUFFER];
        u16 rx_head;
        u16 rx_count;
        u64 rx_next;
        u8 outputs;
        u8 remote;
        u32 dropped;
    };

public:
    SerialLink();
    ~SerialLink();
    void Init(UART* uart, NetworkPort* port, int channel);
    void SetKind(int kind);
    int GetKind() const;
    void Reset();
    void Transmit(u8 value, u64 clocks);
    void SetOutputs(u8 outputs, u64 clocks);
    void Receive(int kind, int peer, const u8* data, int size, u64 clocks);
    u64 GetNextEventClocks() const;
    void HandleEvent(u64 clocks);
    void Queue(const u8* data, int size, u64 clocks);
    void Send(int kind, int peer, const u8* data, int size);
    void UpdateInputs();
    UART* GetUART();
    Modem* GetModem();
    SerialLink_State* GetState();
    void Serialize(StateSerializer& serializer);
    void SanitizeState();

private:
    UART* m_uart;
    NetworkPort* m_port;
    Modem* m_modem;
    int m_channel;
    int m_kind;
    SerialLink_State m_state;
};

#endif /* SERIAL_LINK_H */
