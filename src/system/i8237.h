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

#ifndef I8237_H
#define I8237_H

#include <iostream>
#include "../common/common.h"

#define I8237_CHANNELS 4

class StateSerializer;

class I8237
{
public:
    struct I8237_Channel
    {
        u16 current_address;
        u16 base_address;
        u16 current_count;
        u16 base_count;
        u8 mode;
    };

    struct I8237_State
    {
        I8237_Channel channels[I8237_CHANNELS];
        u8 command;
        u8 mask;
        u8 software_requests;
        u8 request_levels;
        u8 status_tc;
        bool flip_flop;
    };

public:
    I8237();
    ~I8237();
    void Reset();
    u8 Read(int reg);
    u8 Peek(int reg) const;
    void Write(int reg, u8 value);
    void SetRequestLine(int channel, bool active);
    bool IsEnabled() const;
    bool IsRequesting(int channel) const;
    int GetServiceChannel(u8 excluded) const;
    u8 GetCascadeChannels() const;
    u8 GetTransfer(int channel) const;
    u8 GetService(int channel) const;
    bool Advance(int channel);
    void Terminate(int channel);
    I8237_State* GetState();
    const I8237_State* GetState() const;
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream);

private:
    void WriteChannel(int reg, u8 value);
    void MasterClear();
    void Serialize(StateSerializer& serializer);
    void SanitizeState();

private:
    I8237_State m_state;
};

static const int k_i8237_reg_status = 0x08;
static const int k_i8237_reg_request = 0x09;
static const int k_i8237_reg_single_mask = 0x0A;
static const int k_i8237_reg_mode = 0x0B;
static const int k_i8237_reg_clear_flip_flop = 0x0C;
static const int k_i8237_reg_master_clear = 0x0D;
static const int k_i8237_reg_clear_mask = 0x0E;
static const int k_i8237_reg_all_mask = 0x0F;
static const u8 k_i8237_command_disable = 0x04;
static const u8 k_i8237_mode_auto_init = 0x10;
static const u8 k_i8237_mode_decrement = 0x20;
static const u8 k_i8237_verify = 0x00;
static const u8 k_i8237_io_to_memory = 0x01;
static const u8 k_i8237_memory_to_io = 0x02;
static const u8 k_i8237_illegal = 0x03;
static const u8 k_i8237_demand = 0x00;
static const u8 k_i8237_single = 0x01;
static const u8 k_i8237_block = 0x02;
static const u8 k_i8237_cascade = 0x03;

#include "i8237_inline.h"

#endif /* I8237_H */
