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

#include "voodoo_dac.h"
#include "../common/state_serializer.h"

VoodooDac::VoodooDac()
{
    memset(&m_state, 0, sizeof(m_state));
}

VoodooDac::~VoodooDac()
{
}

void VoodooDac::Init()
{
    Reset();
}

void VoodooDac::Reset()
{
    memset(&m_state, 0, sizeof(m_state));
    memcpy(m_state.pll, k_voodoo_dac_pll_reset, sizeof(m_state.pll));
    m_state.mask = 0xFF;
}

u8 VoodooDac::Read(int reg)
{
    switch (reg & 0x07)
    {
        case 0:
        case 3:
            return m_state.address;
        case 1:
        {
            u8 value = m_state.color[m_state.component];

            if (++m_state.component == 3)
            {
                m_state.component = 0;
                m_state.address++;
                memcpy(m_state.color, m_state.palette[m_state.address], 3);
            }

            return value;
        }
        case 2:
            return m_state.mask;
        case 5:
        {
            int index = m_state.pll_address & 0x0F;
            u8 value = index == k_voodoo_dac_pll_control ? m_state.pll[index][0] : m_state.pll[index][m_state.pll_byte];
            StepPLLAddress();
            return value;
        }
        case 6:
            return m_state.command;
        default:
            return m_state.pll_address;
    }
}

void VoodooDac::Write(int reg, u8 value)
{
    switch (reg & 0x07)
    {
        case 0:
            m_state.address = value;
            m_state.component = 0;
            break;
        case 3:
            m_state.address = value;
            m_state.component = 0;
            memcpy(m_state.color, m_state.palette[m_state.address], 3);
            m_state.address++;
            break;
        case 1:
            m_state.color[m_state.component] = value & 0x3F;

            if (++m_state.component == 3)
            {
                m_state.component = 0;
                memcpy(m_state.palette[m_state.address], m_state.color, 3);
                m_state.address++;
            }
            break;
        case 2:
            m_state.mask = value;
            break;
        case 5:
        {
            int index = m_state.pll_address & 0x0F;

            if (index == k_voodoo_dac_pll_control)
                m_state.pll[index][0] = value & 0x37;
            else
                m_state.pll[index][m_state.pll_byte] = value & 0x7F;

            StepPLLAddress();
            break;
        }
        case 6:
            m_state.command = value;
            break;
        default:
            m_state.pll_address = value;
            m_state.pll_byte = 0;
            break;
    }
}

void VoodooDac::StepPLLAddress()
{
    if (((m_state.pll_address & 0x0F) != k_voodoo_dac_pll_control) && (m_state.pll_byte == 0))
    {
        m_state.pll_byte = 1;
        return;
    }

    m_state.pll_byte = 0;
    m_state.pll_address++;
}

u32 VoodooDac::GetFrequency(int index) const
{
    u32 m = m_state.pll[index][0] & 0x7F;
    u32 n1 = m_state.pll[index][1] & 0x1F;
    u32 n2 = (m_state.pll[index][1] >> 5) & 0x03;

    return (u32)(((u64)k_voodoo_dac_reference * (m + 2)) / ((n1 + 2) << n2));
}

u32 VoodooDac::GetVideoClock() const
{
    u8 control = m_state.pll[k_voodoo_dac_pll_control][0];
    return GetFrequency((control & 0x20) != 0 ? control & 0x07 : 0);
}

u32 VoodooDac::GetGraphicsClock() const
{
    u8 control = m_state.pll[k_voodoo_dac_pll_control][0];
    return GetFrequency((control & 0x10) != 0 ? k_voodoo_dac_pll_clk1_b : k_voodoo_dac_pll_clk1_a);
}

VoodooDac::VoodooDac_State* VoodooDac::GetState()
{
    return &m_state;
}

void VoodooDac::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
}

void VoodooDac::LoadState(std::istream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
    SanitizeState();
}

void VoodooDac::Serialize(StateSerializer& serializer)
{
    G_SERIALIZE_ARRAY(serializer, &m_state.pll[0][0], VOODOO_DAC_PLLS * 2);
    G_SERIALIZE(serializer, m_state.pll_address);
    G_SERIALIZE(serializer, m_state.pll_byte);
    G_SERIALIZE(serializer, m_state.command);
    G_SERIALIZE(serializer, m_state.mask);
    G_SERIALIZE(serializer, m_state.address);
    G_SERIALIZE(serializer, m_state.component);
    G_SERIALIZE_ARRAY(serializer, m_state.color, 3);
    G_SERIALIZE_ARRAY(serializer, &m_state.palette[0][0], 256 * 3);
}

void VoodooDac::SanitizeState()
{
    m_state.pll_byte &= 0x01;
    m_state.component = m_state.component % 3;
}
