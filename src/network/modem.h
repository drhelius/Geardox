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

#ifndef MODEM_H
#define MODEM_H

#include "../common/common.h"

#define MODEM_COMMAND_SIZE 64
#define MODEM_REGISTERS 32

enum Modem_Mode
{
    MODEM_IDLE = 0,
    MODEM_DIALING,
    MODEM_RINGING,
    MODEM_ONLINE,
    MODEM_ONLINE_COMMAND
};

class SerialLink;
class StateSerializer;

class Modem
{
public:
    struct Modem_State
    {
        u8 mode;
        u8 registers[MODEM_REGISTERS];
        bool echo;
        bool quiet;
        bool verbose;
        u8 dcd_mode;
        u8 dtr_mode;
        bool dtr;
        u8 peer;
        u8 rings;
        bool ring_on;
        u64 timer;
        u64 last_data;
        u8 plus_count;
        u64 plus_time;
        u8 command_size;
        char command[MODEM_COMMAND_SIZE];
        char last_command[MODEM_COMMAND_SIZE];
    };

public:
    Modem();
    void Init(SerialLink* link);
    void Reset();
    void Transmit(u8 value, u64 clocks);
    void SetDTR(bool dtr, u64 clocks);
    void Receive(int kind, int peer, const u8* data, int size, u64 clocks);
    u64 GetNextEventClocks() const;
    void HandleEvent(u64 clocks);
    u8 GetInputs() const;
    Modem_State* GetState();
    void Serialize(StateSerializer& serializer);
    void SanitizeState();

private:
    void Factory();
    void Execute(const char* line, u64 clocks);
    void Result(int code, u64 clocks);
    void Text(const char* text, u64 clocks);
    void Dial(const char* number, u64 clocks);
    void Answer(u64 clocks);
    void Connect(u64 clocks);
    void HangUp(bool notify, u64 clocks);
    void Ring(u64 clocks);
    bool IsPartner(int peer) const;
    u64 Seconds(u32 fiftieths) const;

private:
    SerialLink* m_link;
    Modem_State m_state;
};

#endif /* MODEM_H */
