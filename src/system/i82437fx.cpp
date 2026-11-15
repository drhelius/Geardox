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

#include "i82437fx.h"
#include "system_control.h"
#include "../common/state_serializer.h"

static const u8 k_i82437fx_write_masks[256] =
{
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x00, 0x00,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0xEF, 0x00, 0xFB, 0x00, 0x00, 0x00, 0x00, 0xCF, 0x7F, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

I82437fx::I82437fx()
{
    memset(&m_state, 0, sizeof(m_state));
    InitPointer(m_system_control);
    InitPointer(m_hole_context);
    InitPointer(m_hole_callback);
}

I82437fx::~I82437fx()
{
}

void I82437fx::Init(SystemControl* system_control)
{
    m_system_control = system_control;
}

void I82437fx::SetHoleCallback(void* context, GDOX_Memory_Hole_Fn callback)
{
    m_hole_context = context;
    m_hole_callback = callback;
}

void I82437fx::Reset(u32 ram_size, bool hole)
{
    u8* c = m_state.config;
    u8 rows = (u8)MIN(ram_size >> 22, 0x20U);
    memset(c, 0, sizeof(m_state.config));

    c[0x00] = 0x86;
    c[0x01] = 0x80;
    c[0x02] = 0x2D;
    c[0x03] = 0x12;
    c[0x04] = 0x06;
    c[0x07] = 0x02;
    c[0x0B] = 0x06;
    c[0x52] = 0x42;
    c[k_i82437fx_dramc] = 0x01 | (hole ? k_i82437fx_hole_15m : 0);
    c[0x72] = 0x02;

    for (int i = 0x60; i <= 0x64; i++)
        c[i] = rows;
}

u8 I82437fx::ReadCallback(void* device, u8 function, u8 offset)
{
    return ((I82437fx*)device)->Read(function, offset);
}

void I82437fx::WriteCallback(void* device, u8 function, u8 offset, u8 value)
{
    ((I82437fx*)device)->Write(function, offset, value);
}

u8 I82437fx::Read(u8 function, u8 offset) const
{
    if (function != 0)
        return 0xFF;

    if (offset >= k_i82437fx_pam0 && offset <= k_i82437fx_pam6)
        return ReadPAM(offset);

    return m_state.config[offset];
}

void I82437fx::Write(u8 function, u8 offset, u8 value)
{
    if (function != 0)
        return;

    u8 mask = k_i82437fx_write_masks[offset];

    if (offset == 0x07)
    {
        m_state.config[offset] &= (u8)~(value & 0x30);
        return;
    }

    if (mask == 0)
        return;

    if (offset >= k_i82437fx_pam0 && offset <= k_i82437fx_pam6)
    {
        WritePAM(offset, value & mask);
        return;
    }

    bool hole = HasMemoryHole();
    m_state.config[offset] = (u8)((m_state.config[offset] & ~mask) | (value & mask));

    if (offset == k_i82437fx_dramc && hole != HasMemoryHole() && IsValidPointer(m_hole_callback))
        m_hole_callback(m_hole_context, HasMemoryHole());
}

bool I82437fx::HasMemoryHole() const
{
    return (m_state.config[k_i82437fx_dramc] & k_i82437fx_hole_mask) == k_i82437fx_hole_15m;
}

void I82437fx::GetSegment(u8 offset, int nibble, u32& first, u32& count)
{
    if (offset == k_i82437fx_pam0)
    {
        first = k_system_control_bios_page;
        count = k_system_control_upper_pages - k_system_control_bios_page;
        return;
    }

    first = ((u32)(offset - k_i82437fx_pam0 - 1) * 2 + (u32)nibble) * 4;
    count = 4;
}

u8 I82437fx::ReadPAM(u8 offset) const
{
    u8 value = 0;

    for (int nibble = (offset == k_i82437fx_pam0 ? 1 : 0); nibble < 2; nibble++)
    {
        u32 first = 0;
        u32 count = 0;
        GetSegment(offset, nibble, first, count);

        u8 access = m_system_control->GetUpperPage(first);
        u8 cache = (access & k_system_control_page_read) != 0 ? (u8)((m_state.config[offset] >> (nibble * 4)) & 0x04) : 0;
        value |= (u8)((access | cache) << (nibble * 4));
    }

    return value;
}

void I82437fx::WritePAM(u8 offset, u8 value)
{
    u8 current = ReadPAM(offset);
    m_state.config[offset] = value;

    for (int nibble = (offset == k_i82437fx_pam0 ? 1 : 0); nibble < 2; nibble++)
    {
        u8 field = (u8)((value >> (nibble * 4)) & 0x0F);

        if (field == ((current >> (nibble * 4)) & 0x0F))
            continue;

        u32 first = 0;
        u32 count = 0;
        GetSegment(offset, nibble, first, count);
        m_system_control->SetUpperPages(first, count, field & k_system_control_page_ram);
    }
}

I82437fx::I82437fx_State* I82437fx::GetState()
{
    return &m_state;
}

void I82437fx::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
}

void I82437fx::LoadState(std::istream& stream, int version)
{
    UNUSED(version);
    StateSerializer serializer(stream);
    Serialize(serializer);

    if (IsValidPointer(m_hole_callback))
        m_hole_callback(m_hole_context, HasMemoryHole());
}

void I82437fx::Serialize(StateSerializer& serializer)
{
    G_SERIALIZE_ARRAY(serializer, m_state.config, 256);
}
