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

#include "serial_ports.h"
#include "uart.h"
#include "pic.h"
#include "scheduler.h"
#include "../input/serial_mouse.h"
#include "../common/trace_logger.h"
#include "../common/state_serializer.h"

SerialPorts::SerialPorts()
{
    InitPointer(m_pic);
    InitPointer(m_scheduler);
    InitPointer(m_trace_logger);
    m_mouse_attached = false;

    for (int i = 0; i < SERIAL_PORTS; i++)
    {
        m_uart[i] = new UART();
        m_irq[i] = false;
        m_modem_outputs[i] = 0;
    }

    m_mouse = new SerialMouse();
}

SerialPorts::~SerialPorts()
{
    for (int i = 0; i < SERIAL_PORTS; i++)
        SafeDelete(m_uart[i]);

    SafeDelete(m_mouse);
}

void SerialPorts::Init(PIC* pic, Scheduler* scheduler, Input* input)
{
    m_pic = pic;
    m_scheduler = scheduler;
    m_mouse->Init(input);
}

void SerialPorts::SetTraceLogger(TraceLogger* trace_logger)
{
    m_trace_logger = trace_logger;
    m_mouse->SetTraceLogger(trace_logger);
}

void SerialPorts::SetMouse(bool attached)
{
    m_mouse_attached = attached;
}

void SerialPorts::Reset()
{
    for (int i = 0; i < SERIAL_PORTS; i++)
    {
        m_uart[i]->Reset();
        m_modem_outputs[i] = 0;
    }

    m_mouse->Reset();
    UpdateIRQ();
    UpdateEvent();
}

bool SerialPorts::Decodes(u16 port) const
{
    return GetIndex(port) >= 0;
}

u8 SerialPorts::Read(u16 port, u64 clocks)
{
    int index = GetIndex(port);

    Synchronize(clocks);
    u8 value = m_uart[index]->Read(port & 0x07, clocks);
    UpdateIRQ();
    UpdateEvent();
    return value;
}

u8 SerialPorts::Peek(u16 port) const
{
    return m_uart[GetIndex(port)]->Peek(port & 0x07);
}

void SerialPorts::Write(u16 port, u8 value, u64 clocks)
{
    int index = GetIndex(port);
    u8 reg = port & 0x07;

    Synchronize(clocks);
    TraceEvent(TRACE_SERIAL_WRITE, index, reg, value, 0);
    m_uart[index]->Write(reg, value, clocks);

    if (reg == 4)
        UpdateModemLines(index, clocks);

    UpdateIRQ();
    UpdateEvent();
}

u64 SerialPorts::GetReadStableUntil(u16 port) const
{
    return m_uart[GetIndex(port)]->GetReadStableUntil(port & 0x07);
}

void SerialPorts::HandleEvent(u64 clocks)
{
    Synchronize(clocks);
}

// Every event due by the clock settles in order, the mouse's bytes reaching COM1 as they end
void SerialPorts::Synchronize(u64 clocks)
{
    u64 next = GetNextClocks();

    if (next > clocks)
        return;

    while (next <= clocks)
    {
        u8 value = 0;

        if (next == m_uart[0]->GetNextEventClocks())
        {
            if (m_uart[0]->HandleEvent(next, value))
                TraceEvent(TRACE_SERIAL_TRANSMIT, 0, 0, value, 0);
        }
        else if (next == m_uart[1]->GetNextEventClocks())
        {
            if (m_uart[1]->HandleEvent(next, value))
                TraceEvent(TRACE_SERIAL_TRANSMIT, 1, 0, value, 0);
        }
        else if (m_mouse->HandleEvent(next, value))
        {
            m_uart[0]->Receive(value, k_serial_mouse_data_bits, k_serial_mouse_divisor, next);
            TraceEvent(TRACE_SERIAL_RECEIVE, 0, 0, value, m_uart[0]->GetLineStatus());
        }

        UpdateIRQ();
        next = GetNextClocks();
    }

    UpdateEvent();
}

u64 SerialPorts::GetNextClocks() const
{
    u64 next = MIN(m_uart[0]->GetNextEventClocks(), m_uart[1]->GetNextEventClocks());
    return m_mouse_attached ? MIN(next, m_mouse->GetNextEventClocks()) : next;
}

int SerialPorts::GetIndex(u16 port) const
{
    for (int i = 0; i < SERIAL_PORTS; i++)
    {
        if ((port & 0xFFF8) == k_serial_ports_base[i])
            return i;
    }

    return -1;
}

void SerialPorts::UpdateModemLines(int index, u64 clocks)
{
    u8 outputs = m_uart[index]->GetModemOutputs();

    if (outputs == m_modem_outputs[index])
        return;

    m_modem_outputs[index] = outputs;
    TraceEvent(TRACE_SERIAL_MODEM, index, 4, outputs, 0);

    if (index == 0 && m_mouse_attached)
        m_mouse->SetRTS((outputs & k_uart_rts) != 0, clocks);
}

void SerialPorts::UpdateIRQ()
{
    for (int i = 0; i < SERIAL_PORTS; i++)
    {
        bool irq = m_uart[i]->IsInterruptPending();

        if (irq == m_irq[i])
            continue;

        m_irq[i] = irq;
        m_pic->SetIRQLine(k_serial_ports_irq[i], irq);
        TraceEvent(TRACE_SERIAL_IRQ, i, 2, m_uart[i]->GetInterruptIdentification(), irq ? 1 : 0);
    }
}

void SerialPorts::UpdateEvent()
{
    m_scheduler->Schedule(SCHEDULER_EVENT_SERIAL, GetNextClocks());
}

UART* SerialPorts::GetUART(int index)
{
    return m_uart[index];
}

SerialMouse* SerialPorts::GetMouse()
{
    return m_mouse;
}

bool SerialPorts::IsMouseAttached() const
{
    return m_mouse_attached;
}

bool SerialPorts::GetIRQ(int index) const
{
    return m_irq[index];
}

void SerialPorts::TraceEvent(u8 event, int index, u8 reg, u8 value, u8 flags)
{
    if (!IsValidPointer(m_trace_logger) || !m_trace_logger->IsEventEnabled(TRACE_SERIAL, event))
        return;

    GDOX_Trace_Entry entry = {};
    entry.type = TRACE_SERIAL;
    entry.event = event;
    entry.serial.port = (u8)index;
    entry.serial.reg = reg;
    entry.serial.value = value;
    entry.serial.flags = flags;
    m_trace_logger->TraceLog(entry);
}

void SerialPorts::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
}

void SerialPorts::LoadState(std::istream& stream, int version)
{
    UNUSED(version);
    StateSerializer serializer(stream);
    Serialize(serializer);

    for (int i = 0; i < SERIAL_PORTS; i++)
        m_uart[i]->SanitizeState();

    m_mouse->SanitizeState();

    for (int i = 0; i < SERIAL_PORTS; i++)
        m_pic->SetIRQLine(k_serial_ports_irq[i], m_irq[i]);

    UpdateEvent();
}

void SerialPorts::Serialize(StateSerializer& serializer)
{
    for (int i = 0; i < SERIAL_PORTS; i++)
    {
        m_uart[i]->Serialize(serializer);
        G_SERIALIZE(serializer, m_irq[i]);
        G_SERIALIZE(serializer, m_modem_outputs[i]);
    }

    m_mouse->Serialize(serializer);
}
