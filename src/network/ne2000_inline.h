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

#ifndef NE2000_INLINE_H
#define NE2000_INLINE_H

#include "ne2000.h"

INLINE bool NE2000::Decodes(u16 port) const
{
    return (u16)(port - m_base) < NE2000_PORTS;
}

INLINE bool NE2000::IsDataPort(u16 port) const
{
    return (port & 0x03F8) == m_data_port;
}

#endif /* NE2000_INLINE_H */
