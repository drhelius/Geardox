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

INLINE bool Firmware::IsReady() const
{
    return m_ready;
}

INLINE int Firmware::GetFamily() const
{
    return m_family;
}

INLINE bool Firmware::IsPCI() const
{
    return m_pci;
}

INLINE const GDOX_Firmware_Info& Firmware::GetInfo(GDOX_Firmware_Type type) const
{
    return m_info[type];
}

INLINE const u8* Firmware::GetSystemRom() const
{
    return m_system_rom;
}

INLINE const u8* Firmware::GetVGARom() const
{
    return m_vga_rom;
}
