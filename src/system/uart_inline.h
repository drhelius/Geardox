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

#ifndef UART_INLINE_H
#define UART_INLINE_H

#include "uart.h"

INLINE u64 UART::GetNextEventClocks() const
{
    return MIN(m_state.tx_clocks, m_state.timeout_clocks);
}

INLINE bool UART::IsInterruptPending() const
{
    return (GetInterruptIdentification() & 0x01) == 0 && (m_state.mcr & k_uart_out2) != 0 && !IsLoopback();
}

INLINE bool UART::IsLoopback() const
{
    return (m_state.mcr & k_uart_loopback) != 0;
}

// The outputs go inactive in loopback
INLINE u8 UART::GetModemOutputs() const
{
    return IsLoopback() ? 0 : (u8)(m_state.mcr & (k_uart_dtr | k_uart_rts | k_uart_out1 | k_uart_out2));
}

INLINE u32 UART::GetDivisor() const
{
    u32 divisor = (u32)m_state.dll | ((u32)m_state.dlm << 8);
    return divisor != 0 ? divisor : 0x10000;
}

INLINE UART::UART_State* UART::GetState()
{
    return &m_state;
}

#endif /* UART_INLINE_H */
