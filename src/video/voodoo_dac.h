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

#ifndef VOODOO_DAC_H
#define VOODOO_DAC_H

#include <iostream>
#include "../common/common.h"

#define VOODOO_DAC_PLLS 16

class StateSerializer;

class VoodooDac
{
public:
    struct VoodooDac_State
    {
        u8 pll[VOODOO_DAC_PLLS][2];
        u8 pll_address;
        u8 pll_byte;
        u8 command;
        u8 mask;
        u8 address;
        u8 component;
        u8 color[3];
        u8 palette[256][3];
    };

public:
    VoodooDac();
    ~VoodooDac();
    void Init();
    void Reset();
    u8 Read(int reg);
    void Write(int reg, u8 value);
    u32 GetVideoClock() const;
    u32 GetGraphicsClock() const;
    VoodooDac_State* GetState();
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream);

private:
    u32 GetFrequency(int index) const;
    void StepPLLAddress();
    void Serialize(StateSerializer& serializer);
    void SanitizeState();

private:
    VoodooDac_State m_state;
};

static const u32 k_voodoo_dac_reference = 14318180;
static const int k_voodoo_dac_pll_control = 0x0E;
static const int k_voodoo_dac_pll_clk1_a = 0x0A;
static const int k_voodoo_dac_pll_clk1_b = 0x0B;

static const u8 k_voodoo_dac_pll_reset[VOODOO_DAC_PLLS][2] =
{
    { 0x7D, 0x50 }, { 0x55, 0x49 }, { 0x2A, 0x43 }, { 0x77, 0x4A }, { 0x79, 0x49 }, { 0x6F, 0x47 }, { 0x74, 0x2B },
    { 0x71, 0x29 }, { 0x00, 0x00 }, { 0x00, 0x00 }, { 0x4F, 0x2B }, { 0x79, 0x2E }, { 0x00, 0x00 }, { 0x00, 0x00 },
    { 0x00, 0x00 }, { 0x00, 0x00 }
};

#endif /* VOODOO_DAC_H */
