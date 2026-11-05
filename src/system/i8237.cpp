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

#include "i8237.h"
#include "../common/state_serializer.h"

I8237::I8237()
{
    memset(&m_state, 0, sizeof(m_state));
}

I8237::~I8237()
{
}

void I8237::Reset()
{
    memset(&m_state, 0, sizeof(m_state));
    MasterClear();
}

// Addresses, counts and modes stay, as the sheet only lists the rest
void I8237::MasterClear()
{
    m_state.command = 0;
    m_state.mask = 0x0F;
    m_state.software_requests = 0;
    m_state.status_tc = 0;
    m_state.flip_flop = false;
}

// Reading the status clears the terminal count bits, addresses and counts step the flip-flop
u8 I8237::Read(int reg)
{
    u8 value = Peek(reg);

    if (reg < k_i8237_reg_status)
        m_state.flip_flop = !m_state.flip_flop;
    else if (reg == k_i8237_reg_status)
        m_state.status_tc = 0;

    return value;
}

// The sheet calls 09h, 0Ah, 0Bh, 0Ch, 0Eh and 0Fh illegal to read, so nothing drives the bus. 0Dh is the temporary
// register, which only memory to memory transfers load
u8 I8237::Peek(int reg) const
{
    if (reg < k_i8237_reg_status)
    {
        const I8237_Channel& channel = m_state.channels[reg >> 1];
        u16 value = (reg & 0x01) != 0 ? channel.current_count : channel.current_address;
        return (u8)(m_state.flip_flop ? value >> 8 : value);
    }

    switch (reg)
    {
        case k_i8237_reg_status:
            return (u8)(((m_state.request_levels | m_state.software_requests) << 4) | m_state.status_tc);
        case k_i8237_reg_master_clear:
            return 0x00;
        default:
            return 0xFF;
    }
}

void I8237::Write(int reg, u8 value)
{
    u8 bit = (u8)(1 << (value & 0x03));

    switch (reg)
    {
        case k_i8237_reg_status:
            m_state.command = value;
            break;
        case k_i8237_reg_request:
            if ((value & 0x04) != 0)
                m_state.software_requests |= bit;
            else
                m_state.software_requests &= ~bit;
            break;
        case k_i8237_reg_single_mask:
            if ((value & 0x04) != 0)
                m_state.mask |= bit;
            else
                m_state.mask &= ~bit;
            break;
        case k_i8237_reg_mode:
            m_state.channels[value & 0x03].mode = value & 0xFC;
            break;
        case k_i8237_reg_clear_flip_flop:
            m_state.flip_flop = false;
            break;
        case k_i8237_reg_master_clear:
            MasterClear();
            break;
        case k_i8237_reg_clear_mask:
            m_state.mask = 0;
            break;
        case k_i8237_reg_all_mask:
            m_state.mask = value & 0x0F;
            break;
        default:
            WriteChannel(reg, value);
            break;
    }
}

// Writes go to the base and current registers together
void I8237::WriteChannel(int reg, u8 value)
{
    I8237_Channel& channel = m_state.channels[reg >> 1];
    u16* base = (reg & 0x01) != 0 ? &channel.base_count : &channel.base_address;
    u16* current = (reg & 0x01) != 0 ? &channel.current_count : &channel.current_address;

    *base = m_state.flip_flop ? (u16)((*base & 0x00FF) | (value << 8)) : (u16)((*base & 0xFF00) | value);
    *current = *base;
    m_state.flip_flop = !m_state.flip_flop;
}

// Fixed priority, channel 0 first. Cascade channels answer with no transfer of their own
int I8237::GetServiceChannel(u8 excluded) const
{
    if (!IsEnabled())
        return -1;

    for (int i = 0; i < I8237_CHANNELS; i++)
    {
        if ((excluded & (1 << i)) != 0 || !IsRequesting(i))
            continue;

        if (GetTransfer(i) != k_i8237_illegal || GetService(i) == k_i8237_cascade)
            return i;
    }

    return -1;
}

u8 I8237::GetCascadeChannels() const
{
    u8 channels = 0;

    for (int i = 0; i < I8237_CHANNELS; i++)
    {
        if (GetService(i) == k_i8237_cascade)
            channels |= (u8)(1 << i);
    }

    return channels;
}

// The count holds units minus one, so the unit that starts at zero is the last
bool I8237::Advance(int channel)
{
    I8237_Channel& state = m_state.channels[channel];
    bool terminal = state.current_count == 0;

    state.current_address += (state.mode & k_i8237_mode_decrement) != 0 ? 0xFFFF : 0x0001;
    state.current_count--;
    return terminal;
}

// Terminal count latches the status bit, then the channel reloads from base or masks itself
void I8237::Terminate(int channel)
{
    I8237_Channel& state = m_state.channels[channel];
    u8 bit = (u8)(1 << channel);

    m_state.status_tc |= bit;
    m_state.software_requests &= ~bit;

    if ((state.mode & k_i8237_mode_auto_init) != 0)
    {
        state.current_address = state.base_address;
        state.current_count = state.base_count;
    }
    else
        m_state.mask |= bit;
}

void I8237::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
}

void I8237::LoadState(std::istream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
    SanitizeState();
}

void I8237::Serialize(StateSerializer& serializer)
{
    for (int i = 0; i < I8237_CHANNELS; i++)
    {
        G_SERIALIZE(serializer, m_state.channels[i].current_address);
        G_SERIALIZE(serializer, m_state.channels[i].base_address);
        G_SERIALIZE(serializer, m_state.channels[i].current_count);
        G_SERIALIZE(serializer, m_state.channels[i].base_count);
        G_SERIALIZE(serializer, m_state.channels[i].mode);
    }

    G_SERIALIZE(serializer, m_state.command);
    G_SERIALIZE(serializer, m_state.mask);
    G_SERIALIZE(serializer, m_state.software_requests);
    G_SERIALIZE(serializer, m_state.request_levels);
    G_SERIALIZE(serializer, m_state.status_tc);
    G_SERIALIZE(serializer, m_state.flip_flop);
}

void I8237::SanitizeState()
{
    m_state.mask &= 0x0F;
    m_state.software_requests &= 0x0F;
    m_state.request_levels &= 0x0F;
    m_state.status_tc &= 0x0F;
}
