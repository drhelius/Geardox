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

#ifndef UART_H
#define UART_H

#include "../common/common.h"

#define UART_FIFO_SIZE 16

class StateSerializer;

// 16550A, with the 1.8432 MHz clock of the PC serial adapter. Its owner synchronizes it and wires its pins
class UART
{
public:
    struct UART_State
    {
        u8 rx_fifo[UART_FIFO_SIZE];
        u8 rx_errors[UART_FIFO_SIZE];
        u8 rx_read;
        u8 rx_count;
        u8 rbr;
        u8 tx_fifo[UART_FIFO_SIZE];
        u8 tx_read;
        u8 tx_count;
        u8 tsr;
        bool tsr_full;
        u8 ier;
        u8 fcr;
        u8 lcr;
        u8 mcr;
        u8 msr;
        u8 scr;
        u8 dll;
        u8 dlm;
        bool overrun;
        bool thre_pending;
        bool timeout_pending;
        u64 tx_clocks;
        u64 timeout_clocks;
        u8 inputs;
    };

public:
    UART();
    void Reset();
    u8 Read(u8 reg, u64 clocks);
    u8 Peek(u8 reg) const;
    void Write(u8 reg, u8 value, u64 clocks);
    u64 GetReadStableUntil(u8 reg) const;
    u64 GetNextEventClocks() const;
    bool HandleEvent(u64 clocks, u8& sent);
    void Receive(u8 data, u8 data_bits, u32 divisor, u64 clocks);
    void ReceiveLinked(u8 data, u64 clocks);
    void SetModemInputs(u8 inputs);
    bool CanReceive() const;
    bool IsInterruptPending() const;
    bool IsLoopback() const;
    u8 GetModemOutputs() const;
    u8 GetLineStatus() const;
    u8 GetInterruptIdentification() const;
    u32 GetDivisor() const;
    u64 GetCharacterClocks() const;
    UART_State* GetState();
    void Serialize(StateSerializer& serializer);
    void SanitizeState();

private:
    void Store(u8 value, u8 errors, u64 clocks);
    void StartTransmit(u64 clocks);
    void ClearReceiver();
    void ClearTransmitter();
    void UpdateModemStatus();
    u8 GetModemInputs() const;
    u8 GetTriggerLevel() const;

private:
    UART_State m_state;
};

static const u8 k_uart_dtr = 0x01;
static const u8 k_uart_rts = 0x02;
static const u8 k_uart_out1 = 0x04;
static const u8 k_uart_out2 = 0x08;
static const u8 k_uart_loopback = 0x10;
static const u32 k_uart_clock = 1843200;

#include "uart_inline.h"

#endif /* UART_H */
