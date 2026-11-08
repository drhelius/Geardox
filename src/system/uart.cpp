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

#include "uart.h"
#include "../common/state_serializer.h"

static const u8 k_uart_trigger_levels[4] = { 1, 4, 8, 14 };

UART::UART()
{
    memset(&m_state, 0, sizeof(m_state));
    Reset();
}

// The divisor and scratch registers keep whatever they had, everything else goes to the sheet's reset values
void UART::Reset()
{
    u8 dll = m_state.dll;
    u8 dlm = m_state.dlm;
    u8 scr = m_state.scr;

    memset(&m_state, 0, sizeof(m_state));
    m_state.dll = dll;
    m_state.dlm = dlm;
    m_state.scr = scr;
    m_state.tx_clocks = GDOX_NO_EVENT;
    m_state.timeout_clocks = GDOX_NO_EVENT;
}

u8 UART::Read(u8 reg, u64 clocks)
{
    bool dlab = (m_state.lcr & 0x80) != 0;

    switch (reg)
    {
        case 0:
        {
            if (dlab)
                return m_state.dll;

            if (m_state.rx_count != 0)
            {
                m_state.rbr = m_state.rx_fifo[m_state.rx_read];
                m_state.rx_errors[m_state.rx_read] = 0;
                m_state.rx_read = (m_state.rx_read + 1) & (UART_FIFO_SIZE - 1);
                m_state.rx_count--;
            }

            m_state.timeout_pending = false;
            m_state.timeout_clocks = (m_state.rx_count != 0 && (m_state.fcr & 0x01) != 0) ?
                clocks + 4 * GetCharacterClocks() : GDOX_NO_EVENT;
            return m_state.rbr;
        }
        case 1:
            return dlab ? m_state.dlm : m_state.ier;
        case 2:
        {
            u8 value = GetInterruptIdentification();

            if ((value & 0x0F) == 0x02)
                m_state.thre_pending = false;

            return value;
        }
        case 5:
        {
            u8 value = GetLineStatus();

            m_state.overrun = false;

            if (m_state.rx_count != 0)
                m_state.rx_errors[m_state.rx_read] = 0;

            return value;
        }
        case 6:
        {
            u8 value = m_state.msr;
            m_state.msr &= 0xF0;
            return value;
        }
        default:
            return Peek(reg);
    }
}

u8 UART::Peek(u8 reg) const
{
    bool dlab = (m_state.lcr & 0x80) != 0;

    switch (reg)
    {
        case 0:
            return dlab ? m_state.dll : (m_state.rx_count != 0 ? m_state.rx_fifo[m_state.rx_read] : m_state.rbr);
        case 1:
            return dlab ? m_state.dlm : m_state.ier;
        case 2:
            return GetInterruptIdentification();
        case 3:
            return m_state.lcr;
        case 4:
            return m_state.mcr;
        case 5:
            return GetLineStatus();
        case 6:
            return m_state.msr;
        default:
            return m_state.scr;
    }
}

void UART::Write(u8 reg, u8 value, u64 clocks)
{
    bool dlab = (m_state.lcr & 0x80) != 0;

    switch (reg)
    {
        case 0:
        {
            if (dlab)
            {
                m_state.dll = value;
                break;
            }

            u8 capacity = (m_state.fcr & 0x01) != 0 ? UART_FIFO_SIZE : 1;

            m_state.thre_pending = false;

            if (m_state.tx_count < capacity)
            {
                m_state.tx_fifo[(m_state.tx_read + m_state.tx_count) & (UART_FIFO_SIZE - 1)] = value;
                m_state.tx_count++;
            }
            else if (capacity == 1)
                m_state.tx_fifo[m_state.tx_read] = value;

            StartTransmit(clocks);
            break;
        }
        case 1:
            if (dlab)
            {
                m_state.dlm = value;
                break;
            }

            m_state.ier = value & 0x0F;

            // Enabling the THR empty interrupt with the register already empty raises it
            if ((m_state.ier & 0x02) != 0 && m_state.tx_count == 0)
                m_state.thre_pending = true;
            break;
        case 2:
        {
            bool enabled = (m_state.fcr & 0x01) != 0;

            if ((value & 0x01) == 0)
            {
                if (enabled)
                {
                    ClearReceiver();
                    ClearTransmitter();
                }

                m_state.fcr = 0;
                break;
            }

            if (!enabled || (value & 0x02) != 0)
                ClearReceiver();

            if (!enabled || (value & 0x04) != 0)
                ClearTransmitter();

            m_state.fcr = value & 0xC9;
            break;
        }
        case 3:
            m_state.lcr = value;
            break;
        case 4:
            m_state.mcr = value & 0x1F;
            UpdateModemStatus();
            break;
        case 7:
            m_state.scr = value;
            break;
        default:
            break;
    }
}

// Reads that change nothing stay the same until the next event of their owner
u64 UART::GetReadStableUntil(u8 reg) const
{
    bool dlab = (m_state.lcr & 0x80) != 0;

    switch (reg)
    {
        case 0:
            return (!dlab && m_state.rx_count != 0) ? 0 : GDOX_NO_EVENT;
        case 2:
            return (GetInterruptIdentification() & 0x0F) == 0x02 ? 0 : GDOX_NO_EVENT;
        case 5:
            return (m_state.overrun || (m_state.rx_count != 0 && m_state.rx_errors[m_state.rx_read] != 0)) ? 0 :
                GDOX_NO_EVENT;
        case 6:
            return (m_state.msr & 0x0F) != 0 ? 0 : GDOX_NO_EVENT;
        default:
            return GDOX_NO_EVENT;
    }
}

// Returns true with the byte that left the shift register for the line, loopback keeps it
bool UART::HandleEvent(u64 clocks, u8& sent)
{
    if (m_state.tx_clocks <= clocks)
    {
        u8 value = m_state.tsr;

        m_state.tsr_full = false;
        m_state.tx_clocks = GDOX_NO_EVENT;
        StartTransmit(clocks);

        if (IsLoopback())
        {
            Store(value, 0, clocks);
            return false;
        }

        sent = value;
        return true;
    }

    if (m_state.timeout_clocks <= clocks)
    {
        m_state.timeout_clocks = GDOX_NO_EVENT;
        m_state.timeout_pending = m_state.rx_count != 0;
    }

    return false;
}

// What the receiver samples from a frame sent at its own rate: data bits first, then stop bits and an idle line
void UART::Receive(u8 data, u8 data_bits, u32 divisor, u64 clocks)
{
    if (IsLoopback())
        return;

    if (divisor != GetDivisor())
    {
        Store(0x00, 0x08, clocks);
        return;
    }

    u32 line = (u32)data | (0xFFFFFFFFU << data_bits);
    int width = 5 + (m_state.lcr & 0x03);
    int position = width;
    u8 value = (u8)(line & ((1U << width) - 1));
    u8 errors = 0;

    if ((m_state.lcr & 0x08) != 0)
    {
        u32 parity = (line >> position) & 1;
        u32 expected = 0;

        if ((m_state.lcr & 0x20) != 0)
            expected = (m_state.lcr & 0x10) != 0 ? 0 : 1;
        else
        {
            u32 ones = 0;

            for (int i = 0; i < width; i++)
                ones += (value >> i) & 1;

            expected = ((m_state.lcr & 0x10) != 0) ? (ones & 1) : ((ones & 1) ^ 1);
        }

        if (parity != expected)
            errors |= 0x04;

        position++;
    }

    if (((line >> position) & 1) == 0)
        errors |= 0x08;

    Store(value, errors, clocks);
}

u8 UART::GetLineStatus() const
{
    u8 value = 0;

    if (m_state.rx_count != 0)
        value |= (u8)(0x01 | m_state.rx_errors[m_state.rx_read]);

    if (m_state.overrun)
        value |= 0x02;

    if (m_state.tx_count == 0)
        value |= 0x20;

    if (m_state.tx_count == 0 && !m_state.tsr_full)
        value |= 0x40;

    if ((m_state.fcr & 0x01) != 0)
    {
        for (int i = 0; i < m_state.rx_count; i++)
        {
            if (m_state.rx_errors[(m_state.rx_read + i) & (UART_FIFO_SIZE - 1)] != 0)
            {
                value |= 0x80;
                break;
            }
        }
    }

    return value;
}

// By priority: line status, received data, character timeout, THR empty, modem status
u8 UART::GetInterruptIdentification() const
{
    u8 fifo = (m_state.fcr & 0x01) != 0 ? 0xC0 : 0x00;
    bool errors = m_state.overrun || (m_state.rx_count != 0 && m_state.rx_errors[m_state.rx_read] != 0);

    if ((m_state.ier & 0x04) != 0 && errors)
        return fifo | 0x06;

    if ((m_state.ier & 0x01) != 0 && m_state.rx_count != 0 && m_state.rx_count >= GetTriggerLevel())
        return fifo | 0x04;

    if ((m_state.ier & 0x01) != 0 && fifo != 0 && m_state.timeout_pending)
        return fifo | 0x0C;

    if ((m_state.ier & 0x02) != 0 && m_state.thre_pending)
        return fifo | 0x02;

    if ((m_state.ier & 0x08) != 0 && (m_state.msr & 0x0F) != 0)
        return fifo;

    return fifo | 0x01;
}

// Start, data, parity and stop bits, with a 5-bit word's second stop bit a half
u64 UART::GetCharacterClocks() const
{
    u32 data = 5 + (m_state.lcr & 0x03);
    u32 half_bits = 2 * (1 + data + ((m_state.lcr & 0x08) != 0 ? 1 : 0)) +
        ((m_state.lcr & 0x04) != 0 ? (data == 5 ? 3 : 4) : 2);

    return ((u64)half_bits * GetDivisor() * 8 * GDOX_MACHINE_CLOCK_RATE) / k_uart_clock;
}

// Without the FIFO a new byte destroys the unread one, with it the byte that finds the FIFO full is lost
void UART::Store(u8 value, u8 errors, u64 clocks)
{
    bool fifo = (m_state.fcr & 0x01) != 0;
    u8 capacity = fifo ? UART_FIFO_SIZE : 1;

    if (m_state.rx_count == capacity)
    {
        m_state.overrun = true;

        if (!fifo)
        {
            m_state.rx_fifo[m_state.rx_read] = value;
            m_state.rx_errors[m_state.rx_read] = errors;
        }
    }
    else
    {
        u8 write = (m_state.rx_read + m_state.rx_count) & (UART_FIFO_SIZE - 1);
        m_state.rx_fifo[write] = value;
        m_state.rx_errors[write] = errors;
        m_state.rx_count++;
    }

    m_state.timeout_pending = false;
    m_state.timeout_clocks = fifo ? clocks + 4 * GetCharacterClocks() : GDOX_NO_EVENT;
}

// The holding register moves to the idle shift register at once, which empties it
void UART::StartTransmit(u64 clocks)
{
    if (m_state.tsr_full || m_state.tx_count == 0)
        return;

    m_state.tsr = m_state.tx_fifo[m_state.tx_read];
    m_state.tx_read = (m_state.tx_read + 1) & (UART_FIFO_SIZE - 1);
    m_state.tx_count--;
    m_state.tsr_full = true;
    m_state.tx_clocks = clocks + GetCharacterClocks();

    if (m_state.tx_count == 0)
        m_state.thre_pending = true;
}

void UART::ClearReceiver()
{
    m_state.rx_read = 0;
    m_state.rx_count = 0;
    memset(m_state.rx_errors, 0, sizeof(m_state.rx_errors));
    m_state.timeout_pending = false;
    m_state.timeout_clocks = GDOX_NO_EVENT;
}

void UART::ClearTransmitter()
{
    m_state.tx_read = 0;
    m_state.tx_count = 0;
    m_state.thre_pending = (m_state.ier & 0x02) != 0;
}

// In loopback the outputs come back as inputs: RTS to CTS, DTR to DSR, OUT1 to RI and OUT2 to DCD
void UART::UpdateModemStatus()
{
    u8 inputs = GetModemInputs();
    u8 changed = (u8)((m_state.msr ^ inputs) & 0xF0);
    u8 deltas = m_state.msr & 0x0F;

    if ((changed & 0x10) != 0)
        deltas |= 0x01;

    if ((changed & 0x20) != 0)
        deltas |= 0x02;

    if ((changed & 0x40) != 0 && (inputs & 0x40) == 0)
        deltas |= 0x04;

    if ((changed & 0x80) != 0)
        deltas |= 0x08;

    m_state.msr = (u8)(inputs | deltas);
}

// Nothing a Microsoft mouse or an empty port drives reaches CTS, DSR, RI or DCD
u8 UART::GetModemInputs() const
{
    if (!IsLoopback())
        return 0;

    u8 mcr = m_state.mcr;

    return (u8)(((mcr & k_uart_rts) != 0 ? 0x10 : 0x00) | ((mcr & k_uart_dtr) != 0 ? 0x20 : 0x00) |
        ((mcr & k_uart_out1) != 0 ? 0x40 : 0x00) | ((mcr & k_uart_out2) != 0 ? 0x80 : 0x00));
}

u8 UART::GetTriggerLevel() const
{
    return (m_state.fcr & 0x01) != 0 ? k_uart_trigger_levels[m_state.fcr >> 6] : 1;
}

void UART::Serialize(StateSerializer& serializer)
{
    G_SERIALIZE_ARRAY(serializer, m_state.rx_fifo, UART_FIFO_SIZE);
    G_SERIALIZE_ARRAY(serializer, m_state.rx_errors, UART_FIFO_SIZE);
    G_SERIALIZE(serializer, m_state.rx_read);
    G_SERIALIZE(serializer, m_state.rx_count);
    G_SERIALIZE(serializer, m_state.rbr);
    G_SERIALIZE_ARRAY(serializer, m_state.tx_fifo, UART_FIFO_SIZE);
    G_SERIALIZE(serializer, m_state.tx_read);
    G_SERIALIZE(serializer, m_state.tx_count);
    G_SERIALIZE(serializer, m_state.tsr);
    G_SERIALIZE(serializer, m_state.tsr_full);
    G_SERIALIZE(serializer, m_state.ier);
    G_SERIALIZE(serializer, m_state.fcr);
    G_SERIALIZE(serializer, m_state.lcr);
    G_SERIALIZE(serializer, m_state.mcr);
    G_SERIALIZE(serializer, m_state.msr);
    G_SERIALIZE(serializer, m_state.scr);
    G_SERIALIZE(serializer, m_state.dll);
    G_SERIALIZE(serializer, m_state.dlm);
    G_SERIALIZE(serializer, m_state.overrun);
    G_SERIALIZE(serializer, m_state.thre_pending);
    G_SERIALIZE(serializer, m_state.timeout_pending);
    G_SERIALIZE(serializer, m_state.tx_clocks);
    G_SERIALIZE(serializer, m_state.timeout_clocks);
}

void UART::SanitizeState()
{
    m_state.rx_read &= UART_FIFO_SIZE - 1;
    m_state.tx_read &= UART_FIFO_SIZE - 1;
    m_state.rx_count = (u8)MIN((int)m_state.rx_count, UART_FIFO_SIZE);
    m_state.tx_count = (u8)MIN((int)m_state.tx_count, UART_FIFO_SIZE);
    m_state.ier &= 0x0F;
    m_state.fcr &= 0xC9;
    m_state.mcr &= 0x1F;

    for (int i = 0; i < UART_FIFO_SIZE; i++)
        m_state.rx_errors[i] &= 0x1C;

    if (!m_state.tsr_full)
        m_state.tx_clocks = GDOX_NO_EVENT;
}
