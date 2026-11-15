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

#include "piix.h"
#include "system_control.h"
#include "../drive/ide_controller.h"
#include "../common/state_serializer.h"

static const u8 k_piix_isa_masks[256] =
{
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0x8F, 0x8F, 0x8F, 0x8F, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFB, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xCF, 0xCF, 0x00, 0x00, 0x00, 0x00, 0x8F, 0x8F, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0x1F, 0x00, 0xFF, 0x00, 0xFB, 0xFF, 0x00, 0xE0, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00
};

static const u8 k_piix_ide_masks[256] =
{
    0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x00, 0x00,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0xFF, 0xBF, 0xFF, 0xBF
};

Piix::Piix()
{
    memset(&m_state, 0, sizeof(m_state));
    InitPointer(m_system_control);
    InitPointer(m_ide);
    m_enabled = false;
}

Piix::~Piix()
{
}

void Piix::Init(SystemControl* system_control, IdeController* ide)
{
    m_system_control = system_control;
    m_ide = ide;
}

void Piix::SetEnabled(bool enabled)
{
    m_enabled = enabled;
    UpdateIDE();
}

void Piix::Reset()
{
    memset(&m_state, 0, sizeof(m_state));
    u8* isa = m_state.config[0];
    u8* ide = m_state.config[1];

    isa[0x00] = 0x86;
    isa[0x01] = 0x80;
    isa[0x02] = 0x2E;
    isa[0x03] = 0x12;
    isa[0x04] = 0x07;
    isa[0x07] = 0x02;
    isa[0x0A] = 0x01;
    isa[0x0B] = 0x06;
    isa[0x0E] = 0x80;
    isa[0x4C] = 0x4D;
    isa[0x4E] = 0x03;
    isa[0x60] = 0x80;
    isa[0x61] = 0x80;
    isa[0x62] = 0x80;
    isa[0x63] = 0x80;
    isa[0x69] = 0x02;
    isa[0x6A] = 0x04;
    isa[0x70] = 0x80;
    isa[0x71] = 0x80;
    isa[0x76] = 0x0C;
    isa[0x77] = 0x0C;
    isa[0x78] = 0x02;
    isa[0xA0] = 0x08;
    isa[0xA8] = 0x0F;

    ide[0x00] = 0x86;
    ide[0x01] = 0x80;
    ide[0x02] = 0x30;
    ide[0x03] = 0x12;
    ide[0x06] = 0x80;
    ide[0x07] = 0x02;
    ide[0x09] = 0x80;
    ide[0x0A] = 0x01;
    ide[0x0B] = 0x01;

    UpdateIDE();
}

u8 Piix::ReadCallback(void* device, u8 function, u8 offset)
{
    return ((Piix*)device)->Read(function, offset);
}

void Piix::WriteCallback(void* device, u8 function, u8 offset, u8 value)
{
    ((Piix*)device)->Write(function, offset, value);
}

u8 Piix::Read(u8 function, u8 offset) const
{
    return function < PIIX_FUNCTIONS ? m_state.config[function][offset] : 0xFF;
}

void Piix::Write(u8 function, u8 offset, u8 value)
{
    if (function >= PIIX_FUNCTIONS)
        return;

    u8* config = m_state.config[function];

    if (offset == 0x07)
    {
        config[offset] &= (u8)~(value & 0x38);
        return;
    }

    if (function == 0 && offset == 0xAA)
    {
        config[offset] &= value;
        return;
    }

    u8 mask = function == 0 ? k_piix_isa_masks[offset] : k_piix_ide_masks[offset];

    if (mask == 0)
        return;

    config[offset] = (u8)((config[offset] & ~mask) | (value & mask));

    if (function == 1)
        UpdateIDE();
}

void Piix::UpdateIDE()
{
    if (!IsValidPointer(m_ide))
        return;

    if (!m_enabled)
    {
        m_ide->SetDecode(0x03);
        return;
    }

    const u8* ide = m_state.config[1];
    bool io = (ide[0x04] & 0x01) != 0;
    u8 decode = (u8)((io && (ide[0x41] & 0x80) != 0 ? 0x01 : 0) | (io && (ide[0x43] & 0x80) != 0 ? 0x02 : 0));

    m_ide->SetDecode(decode);
}

bool Piix::Decodes(u16 port) const
{
    return m_enabled && (port == k_piix_elcr1_port || port == k_piix_elcr2_port || port == k_piix_reset_port);
}

u8 Piix::ReadPort(u16 port) const
{
    switch (port)
    {
        case k_piix_elcr1_port:
            return m_state.elcr[0];
        case k_piix_elcr2_port:
            return m_state.elcr[1];
        default:
            return m_state.reset_control & 0x02;
    }
}

void Piix::WritePort(u16 port, u8 value)
{
    switch (port)
    {
        case k_piix_elcr1_port:
            m_state.elcr[0] = value & 0xF8;
            break;
        case k_piix_elcr2_port:
            m_state.elcr[1] = value & 0xDE;
            break;
        default:
        {
            bool start = (value & 0x04) != 0 && (m_state.reset_control & 0x04) == 0;
            m_state.reset_control = value & 0x06;

            if (start && IsValidPointer(m_system_control))
                m_system_control->RequestCPUReset(k_system_control_reset_fast);
            break;
        }
    }
}

Piix::Piix_State* Piix::GetState()
{
    return &m_state;
}

void Piix::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
}

void Piix::LoadState(std::istream& stream, int version)
{
    UNUSED(version);
    StateSerializer serializer(stream);
    Serialize(serializer);
    UpdateIDE();
}

void Piix::Serialize(StateSerializer& serializer)
{
    G_SERIALIZE_ARRAY(serializer, m_state.config[0], 256);
    G_SERIALIZE_ARRAY(serializer, m_state.config[1], 256);
    G_SERIALIZE_ARRAY(serializer, m_state.elcr, 2);
    G_SERIALIZE(serializer, m_state.reset_control);
}
