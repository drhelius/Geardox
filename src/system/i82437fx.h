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

#ifndef I82437FX_H
#define I82437FX_H

#include <iostream>
#include "../common/common.h"

class StateSerializer;
class SystemControl;

typedef void (*GDOX_Memory_Hole_Fn)(void* context, bool hole);

class I82437fx
{
public:
    struct I82437fx_State
    {
        u8 config[256];
    };

public:
    I82437fx();
    ~I82437fx();
    void Init(SystemControl* system_control);
    void SetHoleCallback(void* context, GDOX_Memory_Hole_Fn callback);
    void Reset(u32 ram_size, bool hole);
    static u8 ReadCallback(void* device, u8 function, u8 offset);
    static void WriteCallback(void* device, u8 function, u8 offset, u8 value);
    u8 Read(u8 function, u8 offset) const;
    void Write(u8 function, u8 offset, u8 value);
    bool HasMemoryHole() const;
    I82437fx_State* GetState();
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream, int version = GDOX_SAVESTATE_VERSION);

private:
    u8 ReadPAM(u8 offset) const;
    void WritePAM(u8 offset, u8 value);
    static void GetSegment(u8 offset, int nibble, u32& first, u32& count);
    void Serialize(StateSerializer& serializer);

private:
    I82437fx_State m_state;
    SystemControl* m_system_control;
    void* m_hole_context;
    GDOX_Memory_Hole_Fn m_hole_callback;
};

static const u8 k_i82437fx_pam0 = 0x59;
static const u8 k_i82437fx_pam6 = 0x5F;
static const u8 k_i82437fx_dramc = 0x57;
static const u8 k_i82437fx_hole_mask = 0xC0;
static const u8 k_i82437fx_hole_15m = 0x80;

#endif /* I82437FX_H */
