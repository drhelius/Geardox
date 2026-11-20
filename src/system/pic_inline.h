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

#ifndef PIC_INLINE_H
#define PIC_INLINE_H

#include "pic.h"
#include "../common/trace_logger.h"

// An ISA card on IRQ2 drives the slave's IR1, IRQ9, as the master's IR2 is the cascade
INLINE void PIC::SetIRQLine(int irq, bool high)
{
    if (irq == k_pic_cascade_line)
        irq = k_pic_redirected_irq;

    if (unlikely(m_trace_logger->IsEnabled(TRACE_PIC)))
    {
        SetIRQLineTraced(irq, high);
        return;
    }

    ApplyIRQLine(irq, high);
}

INLINE void PIC::ApplyIRQLine(int irq, bool high)
{
    if (irq >= 8)
    {
        m_slave.SetInputLine(irq - 8, high);
        UpdateCascade();
    }
    else
        m_master.SetInputLine(irq, high);
}

INLINE u8 PIC::Peek(u16 port) const
{
    int a0 = port & 0x01;
    return (port & 0x80) == 0 ? m_master.Peek(a0) : m_slave.Peek(a0);
}

INLINE bool PIC::IsInterruptPending() const
{
    return m_master.IsIRQAsserted();
}

INLINE I8259* PIC::GetMaster()
{
    return &m_master;
}

INLINE I8259* PIC::GetSlave()
{
    return &m_slave;
}

INLINE void PIC::UpdateCascade()
{
    m_master.SetInputLine(k_pic_cascade_line, m_slave.IsIRQAsserted());
}

#endif /* PIC_INLINE_H */
