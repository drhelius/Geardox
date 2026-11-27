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

#include "serial_link.h"
#include "modem.h"
#include "../system/uart.h"
#include "../common/state_serializer.h"

static const u8 k_link_dtr = 0x01;
static const u8 k_link_rts = 0x02;
static const u8 k_link_cts = 0x10;
static const u8 k_link_dsr = 0x20;
static const u8 k_link_dcd = 0x80;

SerialLink::SerialLink()
{
    InitPointer(m_uart);
    InitPointer(m_port);
    m_modem = new Modem();
    m_channel = 0;
    m_kind = SERIAL_LINK_NONE;
    memset(&m_state, 0, sizeof(m_state));
}

SerialLink::~SerialLink()
{
    SafeDelete(m_modem);
}

void SerialLink::Init(UART* uart, NetworkPort* port, int channel)
{
    m_uart = uart;
    m_port = port;
    m_channel = channel;
    m_modem->Init(this);
}

void SerialLink::SetKind(int kind)
{
    m_kind = (kind > SERIAL_LINK_NONE) && (kind < SERIAL_LINK_KIND_COUNT) ? kind : SERIAL_LINK_NONE;
    m_port->SetSerialAttached(m_channel, m_kind != SERIAL_LINK_NONE);
}

int SerialLink::GetKind() const
{
    return m_kind;
}

void SerialLink::Reset()
{
    memset(&m_state, 0, sizeof(m_state));
    m_modem->Reset();
    UpdateInputs();
}

void SerialLink::Transmit(u8 value, u64 clocks)
{
    if (m_kind == SERIAL_LINK_NULL_MODEM)
        Send(NETWORK_SERIAL_DATA, 0, &value, 1);
    else if (m_kind == SERIAL_LINK_MODEM)
        m_modem->Transmit(value, clocks);
}

void SerialLink::SetOutputs(u8 outputs, u64 clocks)
{
    outputs &= k_link_dtr | k_link_rts;

    if (outputs == m_state.outputs)
        return;

    bool rts_raised = (outputs & k_link_rts) && !(m_state.outputs & k_link_rts);
    m_state.outputs = outputs;

    if (rts_raised && (m_state.rx_next < clocks))
        m_state.rx_next = clocks;

    if (m_kind == SERIAL_LINK_NULL_MODEM)
        Send(NETWORK_SERIAL_LINES, 0, &outputs, 1);
    else if (m_kind == SERIAL_LINK_MODEM)
        m_modem->SetDTR((outputs & k_link_dtr) != 0, clocks);
}

void SerialLink::Receive(int kind, int peer, const u8* data, int size, u64 clocks)
{
    if (m_kind == SERIAL_LINK_MODEM)
    {
        m_modem->Receive(kind, peer, data, size, clocks);
        return;
    }

    if (m_kind != SERIAL_LINK_NULL_MODEM)
        return;

    if (kind == NETWORK_SERIAL_DATA)
        Queue(data, size, clocks);
    else if ((kind == NETWORK_SERIAL_LINES) && (size >= 1))
    {
        m_state.remote = data[0];
        UpdateInputs();
    }
}

u64 SerialLink::GetNextEventClocks() const
{
    if (m_kind == SERIAL_LINK_NONE)
        return GDOX_NO_EVENT;

    bool held = (m_state.outputs & k_link_dtr) && !(m_state.outputs & k_link_rts);
    u64 next = (m_state.rx_count > 0) && !held ? m_state.rx_next : GDOX_NO_EVENT;
    return m_kind == SERIAL_LINK_MODEM ? MIN(next, m_modem->GetNextEventClocks()) : next;
}

void SerialLink::HandleEvent(u64 clocks)
{
    if ((m_kind == SERIAL_LINK_MODEM) && (m_modem->GetNextEventClocks() <= clocks))
        m_modem->HandleEvent(clocks);

    bool held = (m_state.outputs & k_link_dtr) && !(m_state.outputs & k_link_rts);

    while ((m_state.rx_count > 0) && (m_state.rx_next <= clocks) && !held)
    {
        u8 value = m_state.rx[m_state.rx_head];
        m_state.rx_head = (u16)((m_state.rx_head + 1) % SERIAL_LINK_BUFFER);
        m_state.rx_count--;
        m_uart->ReceiveLinked(value, m_state.rx_next);
        m_state.rx_next += MAX(m_uart->GetCharacterClocks(), (u64)1);
    }
}

void SerialLink::Queue(const u8* data, int size, u64 clocks)
{
    if (m_state.rx_count == 0)
        m_state.rx_next = MAX(m_state.rx_next, clocks);

    for (int i = 0; i < size; i++)
    {
        if (m_state.rx_count >= SERIAL_LINK_BUFFER)
        {
            m_state.dropped += (u32)(size - i);
            return;
        }

        m_state.rx[(m_state.rx_head + m_state.rx_count) % SERIAL_LINK_BUFFER] = data[i];
        m_state.rx_count++;
    }
}

void SerialLink::Send(int kind, int peer, const u8* data, int size)
{
    m_port->SendSerial(m_channel, kind, peer, data, size);
}

void SerialLink::UpdateInputs()
{
    u8 inputs = 0;

    if (m_kind == SERIAL_LINK_NULL_MODEM)
        inputs = ((m_state.remote & k_link_dtr) ? (k_link_dsr | k_link_dcd) : 0) |
            ((m_state.remote & k_link_rts) ? k_link_cts : 0);
    else if (m_kind == SERIAL_LINK_MODEM)
        inputs = m_modem->GetInputs();

    m_uart->SetModemInputs(inputs);
}

UART* SerialLink::GetUART()
{
    return m_uart;
}

Modem* SerialLink::GetModem()
{
    return m_modem;
}

SerialLink::SerialLink_State* SerialLink::GetState()
{
    return &m_state;
}

void SerialLink::Serialize(StateSerializer& serializer)
{
    if (m_kind == SERIAL_LINK_NONE)
        return;

    G_SERIALIZE_ARRAY(serializer, m_state.rx, SERIAL_LINK_BUFFER);
    G_SERIALIZE(serializer, m_state.rx_head);
    G_SERIALIZE(serializer, m_state.rx_count);
    G_SERIALIZE(serializer, m_state.rx_next);
    G_SERIALIZE(serializer, m_state.outputs);
    G_SERIALIZE(serializer, m_state.remote);
    G_SERIALIZE(serializer, m_state.dropped);

    if (m_kind == SERIAL_LINK_MODEM)
        m_modem->Serialize(serializer);
}

void SerialLink::SanitizeState()
{
    m_state.rx_head %= SERIAL_LINK_BUFFER;
    m_state.rx_count = (u16)MIN((int)m_state.rx_count, SERIAL_LINK_BUFFER);
    m_modem->SanitizeState();
}
