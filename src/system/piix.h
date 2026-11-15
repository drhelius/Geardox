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

#ifndef PIIX_H
#define PIIX_H

#include <iostream>
#include "../common/common.h"

class IdeController;
class StateSerializer;
class SystemControl;

#define PIIX_FUNCTIONS 2

class Piix
{
public:
    struct Piix_State
    {
        u8 config[PIIX_FUNCTIONS][256];
        u8 elcr[2];
        u8 reset_control;
    };

public:
    Piix();
    ~Piix();
    void Init(SystemControl* system_control, IdeController* ide);
    void SetEnabled(bool enabled);
    void Reset();
    static u8 ReadCallback(void* device, u8 function, u8 offset);
    static void WriteCallback(void* device, u8 function, u8 offset, u8 value);
    u8 Read(u8 function, u8 offset) const;
    void Write(u8 function, u8 offset, u8 value);
    bool Decodes(u16 port) const;
    u8 ReadPort(u16 port) const;
    void WritePort(u16 port, u8 value);
    Piix_State* GetState();
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream, int version = GDOX_SAVESTATE_VERSION);

private:
    void UpdateIDE();
    void Serialize(StateSerializer& serializer);

private:
    Piix_State m_state;
    SystemControl* m_system_control;
    IdeController* m_ide;
    bool m_enabled;
};

static const u16 k_piix_elcr1_port = 0x04D0;
static const u16 k_piix_elcr2_port = 0x04D1;
static const u16 k_piix_reset_port = 0x0CF9;

#endif /* PIIX_H */
