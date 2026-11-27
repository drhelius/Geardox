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

#include "modem.h"
#include "serial_link.h"
#include "network_port.h"
#include "../system/uart.h"
#include "../common/state_serializer.h"

enum Modem_Result
{
    MODEM_RESULT_OK = 0,
    MODEM_RESULT_CONNECT = 1,
    MODEM_RESULT_RING = 2,
    MODEM_RESULT_NO_CARRIER = 3,
    MODEM_RESULT_ERROR = 4,
    MODEM_RESULT_BUSY = 7,
    MODEM_RESULT_NO_ANSWER = 8,
    MODEM_RESULT_NONE = 0xFF
};

static const char* const k_modem_results[9] = { "OK", "CONNECT", "RING", "NO CARRIER", "ERROR", "", "", "BUSY",
    "NO ANSWER" };
static const u8 k_modem_defaults[MODEM_REGISTERS] = { 0, 0, '+', 13, 10, 8, 2, 50, 2, 6, 14, 95, 50 };

static int read_number(const char** text)
{
    int value = 0;

    while ((**text >= '0') && (**text <= '9'))
    {
        value = value * 10 + (**text - '0');
        (*text)++;
    }

    return value;
}

Modem::Modem()
{
    InitPointer(m_link);
    memset(&m_state, 0, sizeof(m_state));
    Factory();
}

void Modem::Init(SerialLink* link)
{
    m_link = link;
}

void Modem::Reset()
{
    memset(&m_state, 0, sizeof(m_state));
    Factory();
    m_state.mode = MODEM_IDLE;
    m_state.timer = GDOX_NO_EVENT;
}

void Modem::Factory()
{
    memcpy(m_state.registers, k_modem_defaults, sizeof(m_state.registers));
    m_state.echo = true;
    m_state.quiet = false;
    m_state.verbose = true;
    m_state.dcd_mode = 1;
    m_state.dtr_mode = 2;
}

void Modem::Transmit(u8 value, u64 clocks)
{
    if (m_state.mode == MODEM_ONLINE)
    {
        u64 guard = Seconds(m_state.registers[12]);

        if (value == m_state.registers[2])
        {
            if ((m_state.plus_count == 0) && (clocks - m_state.last_data >= guard))
                m_state.plus_count = 1;
            else if ((m_state.plus_count > 0) && (m_state.plus_count < 3) && (clocks - m_state.plus_time < guard))
                m_state.plus_count++;
            else
                m_state.plus_count = 0;

            m_state.plus_time = clocks;

            if (m_state.plus_count == 3)
                m_state.timer = clocks + guard;
        }
        else
        {
            m_state.plus_count = 0;
            m_state.timer = GDOX_NO_EVENT;
        }

        m_state.last_data = clocks;
        m_link->Send(NETWORK_SERIAL_DATA, m_state.peer, &value, 1);
        return;
    }

    if (m_state.mode == MODEM_DIALING)
    {
        HangUp(true, clocks);
        Result(MODEM_RESULT_NO_CARRIER, clocks);
        return;
    }

    if (m_state.echo)
        m_link->Queue(&value, 1, clocks);

    if (value == m_state.registers[3])
    {
        m_state.command[m_state.command_size] = 0;
        m_state.command_size = 0;
        Execute(m_state.command, clocks);
        return;
    }

    if (value == m_state.registers[5])
    {
        if (m_state.command_size > 0)
            m_state.command_size--;

        return;
    }

    if ((value < 32) || (m_state.command_size >= MODEM_COMMAND_SIZE - 1))
        return;

    m_state.command[m_state.command_size++] = (char)value;

    if ((m_state.command_size == 2) && (toupper(m_state.command[0]) == 'A') && (m_state.command[1] == '/'))
    {
        m_state.command_size = 0;
        Execute(m_state.last_command, clocks);
    }
}

void Modem::SetDTR(bool dtr, u64 clocks)
{
    bool dropped = m_state.dtr && !dtr;
    m_state.dtr = dtr;

    if (!dropped || ((m_state.mode != MODEM_ONLINE) && (m_state.mode != MODEM_ONLINE_COMMAND)))
        return;

    if (m_state.dtr_mode == 2)
    {
        HangUp(true, clocks);
        Result(MODEM_RESULT_NO_CARRIER, clocks);
    }
    else if ((m_state.dtr_mode == 1) && (m_state.mode == MODEM_ONLINE))
    {
        m_state.mode = MODEM_ONLINE_COMMAND;
        Result(MODEM_RESULT_OK, clocks);
    }
}

void Modem::Receive(int kind, int peer, const u8* data, int size, u64 clocks)
{
    switch (kind)
    {
        case NETWORK_SERIAL_CALL:
            if (m_state.mode != MODEM_IDLE)
            {
                m_link->Send(NETWORK_SERIAL_HANGUP, peer, NULL, 0);
                break;
            }

            m_state.mode = MODEM_RINGING;
            m_state.peer = (u8)peer;
            m_state.rings = 0;
            Ring(clocks);
            break;
        case NETWORK_SERIAL_ANSWER:
            if (m_state.mode == MODEM_DIALING)
            {
                m_state.peer = (u8)peer;
                Connect(clocks);
            }
            break;
        case NETWORK_SERIAL_HANGUP:
        {
            bool busy = m_state.mode == MODEM_DIALING;
            bool ringing = m_state.mode == MODEM_RINGING;

            if (m_state.mode == MODEM_IDLE)
                break;

            HangUp(false, clocks);

            if (!ringing)
                Result(busy ? MODEM_RESULT_BUSY : MODEM_RESULT_NO_CARRIER, clocks);
            break;
        }
        case NETWORK_SERIAL_DATA:
            if (m_state.mode == MODEM_ONLINE)
                m_link->Queue(data, size, clocks);
            break;
        default:
            break;
    }
}

u64 Modem::GetNextEventClocks() const
{
    return m_state.timer;
}

void Modem::HandleEvent(u64 clocks)
{
    if (m_state.timer > clocks)
        return;

    m_state.timer = GDOX_NO_EVENT;

    switch (m_state.mode)
    {
        case MODEM_DIALING:
            HangUp(true, clocks);
            Result(MODEM_RESULT_NO_ANSWER, clocks);
            break;
        case MODEM_RINGING:
            if (m_state.ring_on)
            {
                m_state.ring_on = false;
                m_link->UpdateInputs();
                m_state.timer = clocks + Seconds(200);
            }
            else
                Ring(clocks);
            break;
        case MODEM_ONLINE:
            if (m_state.plus_count == 3)
            {
                m_state.plus_count = 0;
                m_state.mode = MODEM_ONLINE_COMMAND;
                Result(MODEM_RESULT_OK, clocks);
            }
            break;
        default:
            break;
    }
}

u8 Modem::GetInputs() const
{
    bool carrier = (m_state.mode == MODEM_ONLINE) || (m_state.mode == MODEM_ONLINE_COMMAND);
    return 0x10 | 0x20 | (m_state.ring_on ? 0x40 : 0) | ((carrier || (m_state.dcd_mode == 0)) ? 0x80 : 0);
}

Modem::Modem_State* Modem::GetState()
{
    return &m_state;
}

void Modem::Execute(const char* line, u64 clocks)
{
    while (*line == ' ')
        line++;

    if ((toupper(line[0]) != 'A') || (toupper(line[1]) != 'T'))
    {
        if (line[0] != 0)
            Result(MODEM_RESULT_ERROR, clocks);

        return;
    }

    if (line != m_state.last_command)
        strncpy_fit(m_state.last_command, line, MODEM_COMMAND_SIZE);

    const char* p = line + 2;
    int result = MODEM_RESULT_OK;

    while (*p != 0)
    {
        char c = (char)toupper(*p++);

        switch (c)
        {
            case 'Z':
                read_number(&p);

                if (m_state.mode != MODEM_RINGING)
                    HangUp(true, clocks);

                Factory();
                break;
            case '&':
            {
                char option = (char)toupper(*p);

                if (option != 0)
                    p++;

                int value = read_number(&p);

                if (option == 'F')
                    Factory();
                else if (option == 'C')
                    m_state.dcd_mode = (u8)value;
                else if (option == 'D')
                    m_state.dtr_mode = (u8)value;
                break;
            }
            case 'E':
                m_state.echo = read_number(&p) != 0;
                break;
            case 'Q':
                m_state.quiet = read_number(&p) != 0;
                break;
            case 'V':
                m_state.verbose = read_number(&p) != 0;
                break;
            case 'H':
                if ((read_number(&p) == 0) && (m_state.mode != MODEM_RINGING))
                    HangUp(true, clocks);
                break;
            case 'S':
            {
                int reg = read_number(&p);

                if (*p == '=')
                {
                    p++;
                    int value = read_number(&p);

                    if (reg < MODEM_REGISTERS)
                        m_state.registers[reg] = (u8)value;
                }
                else if (*p == '?')
                {
                    p++;
                    char text[16];
                    snprintf(text, sizeof(text), "\r\n%03d\r\n", reg < MODEM_REGISTERS ? m_state.registers[reg] : 0);
                    Text(text, clocks);
                }
                break;
            }
            case 'D':
                Dial(p, clocks);
                return;
            case 'A':
                Answer(clocks);
                return;
            case 'O':
                read_number(&p);

                if (m_state.mode == MODEM_ONLINE_COMMAND)
                    Connect(clocks);
                else
                    Result(MODEM_RESULT_NO_CARRIER, clocks);
                return;
            case 'I':
                read_number(&p);
                Text("\r\nGeardox\r\n", clocks);
                break;
            case '+':
            case '%':
            case '\\':
            case '#':
            case '*':
            case '$':
                while (*p != 0)
                    p++;
                break;
            default:
                read_number(&p);
                break;
        }
    }

    Result(result, clocks);
}

void Modem::Result(int code, u64 clocks)
{
    if (m_state.quiet || (code == MODEM_RESULT_NONE))
        return;

    char text[48];

    if (!m_state.verbose)
        snprintf(text, sizeof(text), "%d\r", code);
    else if (code == MODEM_RESULT_CONNECT)
    {
        u32 divisor = m_link->GetUART()->GetDivisor();
        snprintf(text, sizeof(text), "\r\nCONNECT %u\r\n", divisor != 0 ? 115200 / divisor : 9600);
    }
    else
        snprintf(text, sizeof(text), "\r\n%s\r\n", k_modem_results[code]);

    Text(text, clocks);
}

void Modem::Text(const char* text, u64 clocks)
{
    m_link->Queue((const u8*)text, (int)strlen(text), clocks);
}

void Modem::Dial(const char* number, u64 clocks)
{
    int machine = 0;
    bool digits = false;

    for (const char* p = number; *p != 0; p++)
    {
        if ((*p >= '0') && (*p <= '9'))
        {
            machine = MIN(machine * 10 + (*p - '0'), 255);
            digits = true;
        }
        else if (*p == ';')
            break;
    }

    if (m_state.mode != MODEM_IDLE)
        HangUp(true, clocks);

    u8 target = digits ? (u8)machine : 0;
    m_state.mode = MODEM_DIALING;
    m_state.peer = target;
    m_state.timer = clocks + Seconds((u32)m_state.registers[7] * 50);
    m_link->Send(NETWORK_SERIAL_CALL, target, NULL, 0);
}

void Modem::Answer(u64 clocks)
{
    if (m_state.mode != MODEM_RINGING)
    {
        Result(MODEM_RESULT_NO_CARRIER, clocks);
        return;
    }

    m_link->Send(NETWORK_SERIAL_ANSWER, m_state.peer, NULL, 0);
    Connect(clocks);
}

void Modem::Connect(u64 clocks)
{
    m_state.mode = MODEM_ONLINE;
    m_state.ring_on = false;
    m_state.timer = GDOX_NO_EVENT;
    m_state.plus_count = 0;
    m_state.last_data = clocks;
    Result(MODEM_RESULT_CONNECT, clocks);
    m_link->UpdateInputs();
}

void Modem::HangUp(bool notify, u64 clocks)
{
    UNUSED(clocks);

    if (notify && (m_state.mode != MODEM_IDLE))
        m_link->Send(NETWORK_SERIAL_HANGUP, m_state.peer, NULL, 0);

    m_state.mode = MODEM_IDLE;
    m_state.ring_on = false;
    m_state.plus_count = 0;
    m_state.timer = GDOX_NO_EVENT;
    m_link->UpdateInputs();
}

void Modem::Ring(u64 clocks)
{
    m_state.rings++;
    m_state.registers[1] = m_state.rings;
    m_state.ring_on = true;
    Result(MODEM_RESULT_RING, clocks);
    m_link->UpdateInputs();

    if ((m_state.registers[0] > 0) && (m_state.rings >= m_state.registers[0]))
    {
        Answer(clocks);
        return;
    }

    m_state.timer = clocks + Seconds(100);
}

u64 Modem::Seconds(u32 fiftieths) const
{
    return (u64)fiftieths * GDOX_MACHINE_CLOCK_RATE / 50;
}

void Modem::Serialize(StateSerializer& serializer)
{
    G_SERIALIZE(serializer, m_state.mode);
    G_SERIALIZE_ARRAY(serializer, m_state.registers, MODEM_REGISTERS);
    G_SERIALIZE(serializer, m_state.echo);
    G_SERIALIZE(serializer, m_state.quiet);
    G_SERIALIZE(serializer, m_state.verbose);
    G_SERIALIZE(serializer, m_state.dcd_mode);
    G_SERIALIZE(serializer, m_state.dtr_mode);
    G_SERIALIZE(serializer, m_state.dtr);
    G_SERIALIZE(serializer, m_state.peer);
    G_SERIALIZE(serializer, m_state.rings);
    G_SERIALIZE(serializer, m_state.ring_on);
    G_SERIALIZE(serializer, m_state.timer);
    G_SERIALIZE(serializer, m_state.last_data);
    G_SERIALIZE(serializer, m_state.plus_count);
    G_SERIALIZE(serializer, m_state.plus_time);
    G_SERIALIZE(serializer, m_state.command_size);
    G_SERIALIZE_ARRAY(serializer, m_state.command, MODEM_COMMAND_SIZE);
    G_SERIALIZE_ARRAY(serializer, m_state.last_command, MODEM_COMMAND_SIZE);
}

void Modem::SanitizeState()
{
    if (m_state.mode > MODEM_ONLINE_COMMAND)
        m_state.mode = MODEM_IDLE;

    if (m_state.command_size >= MODEM_COMMAND_SIZE)
        m_state.command_size = 0;

    m_state.command[MODEM_COMMAND_SIZE - 1] = 0;
    m_state.last_command[MODEM_COMMAND_SIZE - 1] = 0;
}
