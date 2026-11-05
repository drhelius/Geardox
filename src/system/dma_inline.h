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

#ifndef DMA_INLINE_H
#define DMA_INLINE_H

#include "dma.h"

// Board ports, so all 16 lines decode. 80h is the POST code in SystemControl
INLINE bool DMA::Decodes(u16 port) const
{
    return port <= 0x000F || (port >= 0x0081 && port <= 0x008F) || (port >= 0x00C0 && port <= 0x00DF);
}

// Chip 1 at 00h-0Fh, chip 2 on the even ports from C0h, so its odd ports mirror them
INLINE u8 DMA::Peek(u16 port) const
{
    if (port <= 0x000F)
        return m_chips[0].Peek(port);

    if (port <= 0x008F)
        return m_state.pages[port & 0x0F];

    return m_chips[1].Peek((port >> 1) & 0x0F);
}

// Only events change the status once its terminal count bits are clear, and only writes the page registers
INLINE u64 DMA::GetReadStableUntil(u16 port) const
{
    if (port >= 0x0081 && port <= 0x008F)
        return GDOX_NO_EVENT;

    if (port == 0x0008)
        return m_chips[0].GetState()->status_tc == 0 ? GDOX_NO_EVENT : 0;

    if (port == 0x00D0 || port == 0x00D1)
        return m_chips[1].GetState()->status_tc == 0 ? GDOX_NO_EVENT : 0;

    return 0;
}

INLINE I8237* DMA::GetController(int index)
{
    return &m_chips[index & 0x01];
}

INLINE I8237& DMA::GetChip(int channel)
{
    return m_chips[channel >> 2];
}

INLINE const I8237& DMA::GetChip(int channel) const
{
    return m_chips[channel >> 2];
}

// Channels 5-7 count words: the chip's address is A16-A1, and page bit 0 is not used
INLINE u32 DMA::GetPhysicalAddress(int channel) const
{
    u32 page = m_state.pages[k_dma_channel_pages[channel]];
    u32 address = GetChip(channel).GetState()->channels[channel & 0x03].current_address;

    return channel >= k_dma_cascade_channel ? ((page & 0xFE) << 16) | (address << 1) : (page << 16) | address;
}

INLINE DMA::DMA_State* DMA::GetState()
{
    return &m_state;
}

#endif /* DMA_INLINE_H */
