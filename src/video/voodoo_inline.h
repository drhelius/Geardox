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

#ifndef VOODOO_INLINE_H
#define VOODOO_INLINE_H

#include "voodoo.h"

INLINE bool Voodoo::IsEnabled() const
{
    return m_enabled;
}

INLINE u64 Voodoo::GetEventClocks() const
{
    return m_enabled ? m_event_clocks : GDOX_NO_EVENT;
}

INLINE bool Voodoo::IsDisplayed() const
{
    return m_enabled && ((m_state.regs[k_voodoo_fbi_init0] & k_voodoo_init0_vga_pass) != 0) && m_video_timing.running;
}

INLINE const Voodoo::Voodoo_Video* Voodoo::GetVideo() const
{
    return &m_video_timing;
}

INLINE u16* Voodoo::GetFrameBuffer()
{
    return m_state.fb;
}

INLINE VoodooDac* Voodoo::GetDac()
{
    return m_dac;
}

INLINE VoodooTrex* Voodoo::GetTrex()
{
    return m_trex;
}

INLINE u32 Voodoo::GetRowPixels() const
{
    return ((m_state.regs[k_voodoo_fbi_init1] >> 4) & 0x0F) * 64;
}

INLINE u32 Voodoo::GetQueueCount() const
{
    return m_state.queue_count;
}

INLINE u64 Voodoo::GetBusyClocks() const
{
    return m_state.busy_clocks;
}

INLINE const u8* Voodoo::GetDirtyPages() const
{
    m_raster->Sync();
    return m_dirty_pages;
}

INLINE Voodoo::Voodoo_State* Voodoo::GetState()
{
    return &m_state;
}

#endif /* VOODOO_INLINE_H */
