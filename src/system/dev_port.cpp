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

#include "dev_port.h"
#include "../i386/i386.h"
#include "../common/state_serializer.h"

DevPort::DevPort()
{
    InitPointer(m_cpu);
    InitPointer(m_event_context);
    InitPointer(m_event_callback);
    m_console_enabled = false;
    m_control_enabled = false;
    memset(&m_state, 0, sizeof(m_state));
}

DevPort::~DevPort()
{
}

void DevPort::Init(I386* cpu)
{
    m_cpu = cpu;
    m_console.Init();
    m_console.SetEnabled(true);
    Reset();
}

void DevPort::Reset()
{
    memset(&m_state, 0, sizeof(m_state));
    m_console.Reset();
}

void DevPort::SetConsoleEnabled(bool enabled)
{
    m_console_enabled = enabled;
}

void DevPort::SetControlEnabled(bool enabled)
{
    m_control_enabled = enabled;
}

void DevPort::SetEventCallback(void* context, DevPort_Event_Fn callback)
{
    m_event_context = context;
    m_event_callback = callback;
}

void DevPort::SetRunCommand(const char* command)
{
    m_run_command = IsValidPointer(command) ? command : "";
}

const std::string& DevPort::GetRunCommand() const
{
    return m_run_command;
}

u8 DevPort::Read(u16 port)
{
    u8 value = Peek(port);

    if (port == k_dev_port_stream && m_state.run_offset < m_run_command.size())
        m_state.run_offset++;

    return value;
}

u8 DevPort::Peek(u16 port) const
{
    if (port == k_dev_port_console)
        return k_dev_port_console_readback;

    if (port == k_dev_port_control)
        return k_dev_port_signature;

    if (m_state.run_offset < m_run_command.size())
        return (u8)m_run_command[m_state.run_offset];

    return 0x00;
}

void DevPort::Write(u16 port, u8 value)
{
    if (port == k_dev_port_console)
        m_console.Write(value);
    else if (port == k_dev_port_control)
        Hypercall(value);
}

void DevPort::Hypercall(u8 command)
{
    if (!IsValidPointer(m_cpu))
        return;

    I386_State* cpu = m_cpu->GetState();

    switch (command)
    {
        case DEV_PORT_COMMAND_LOADED:
            m_state.program_state = DEV_PORT_PROGRAM_RUNNING;
            m_state.load_flags = (u8)(cpu->registers[I386_REG_EAX].low >> 8);
            m_state.psp = cpu->registers[I386_REG_EBX].low;
            m_state.entry_cs = cpu->registers[I386_REG_ECX].high;
            m_state.entry_ip = cpu->registers[I386_REG_ECX].low;
            m_state.stack_ss = cpu->registers[I386_REG_EDI].high;
            m_state.stack_sp = cpu->registers[I386_REG_EDI].low;
            m_state.based = false;
            m_state.base = (u32)m_state.entry_cs << 4;
            m_state.code_selector = m_state.entry_cs;
            m_state.data_selector = m_state.psp;
            m_state.code_size = 16;
            m_state.exit_code = 0;
            m_state.exit_type = 0;
            ReadString(I386_SEGMENT_DS, GetOffset(I386_REG_ESI), m_state.path, DEV_PORT_PATH_SIZE);
            AddEvent(DEV_PORT_EVENT_LOADED);
            break;
        case DEV_PORT_COMMAND_BASED:
            m_state.based = true;
            m_state.base = cpu->registers[I386_REG_EBX].value;
            m_state.code_selector = cpu->registers[I386_REG_ECX].low;
            m_state.data_selector = cpu->registers[I386_REG_ESI].low;
            m_state.code_size = cpu->registers[I386_REG_EDI].low == 32 ? 32 : 16;
            AddEvent(DEV_PORT_EVENT_BASED);
            break;
        case DEV_PORT_COMMAND_EXITED:
            m_state.program_state = DEV_PORT_PROGRAM_EXITED;
            m_state.exit_code = (u8)cpu->registers[I386_REG_EBX].low;
            m_state.exit_type = (u8)(cpu->registers[I386_REG_EBX].low >> 8);
            AddEvent(DEV_PORT_EVENT_EXITED);
            break;
        case DEV_PORT_COMMAND_OUTPUT:
        {
            u32 offset = GetOffset(I386_REG_ESI);
            u32 count = MIN(GetOffset(I386_REG_ECX), (u32)DEV_PORT_OUTPUT_LIMIT);

            for (u32 i = 0; i < count; i++)
            {
                u8 value = 0;

                if (!m_cpu->TryPeekLogical(I386_SEGMENT_DS, offset + i, value))
                    break;

                m_console.Write(value);
            }

            break;
        }
        case DEV_PORT_COMMAND_BREAK:
            AddEvent(DEV_PORT_EVENT_BREAK);
            break;
        case DEV_PORT_COMMAND_RUN:
            m_state.run_offset = 0;
            break;
        case DEV_PORT_COMMAND_DOS:
            m_state.dos_known = true;
            m_state.dos_list = cpu->registers[I386_REG_EBX].value;
            m_state.dos_indos = cpu->registers[I386_REG_ECX].value;
            m_state.dos_version = cpu->registers[I386_REG_ESI].low;
            break;
        default:
            break;
    }
}

void DevPort::AddEvent(u8 type)
{
    m_state.sequence++;
    DevPort_Event& event = m_state.events[m_state.sequence % DEV_PORT_EVENT_COUNT];
    event.sequence = m_state.sequence;
    event.type = type;

    if (IsValidPointer(m_event_callback))
        m_event_callback(m_event_context, type);
}

bool DevPort::IsCode32() const
{
    const I386_State* cpu = m_cpu->GetState();

    if (cpu->execution_mode != I386_MODE_PROTECTED)
        return false;

    return (cpu->segments[I386_SEGMENT_CS].attributes & I386_SEGMENT_DEFAULT_32) != 0;
}

u32 DevPort::GetOffset(int reg) const
{
    const I386_State* cpu = m_cpu->GetState();
    return IsCode32() ? cpu->registers[reg].value : cpu->registers[reg].low;
}

u32 DevPort::ReadString(int segment, u32 offset, char* buffer, u32 size) const
{
    u32 count = 0;

    while (count + 1 < size)
    {
        u8 value = 0;

        if (!m_cpu->TryPeekLogical((I386_Segment_Register)segment, offset + count, value) || value == 0)
            break;

        buffer[count++] = (char)value;
    }

    buffer[count] = 0;
    return count;
}

void DevPort::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
}

void DevPort::LoadState(std::istream& stream, int version)
{
    UNUSED(version);
    StateSerializer serializer(stream);
    Serialize(serializer);
    m_state.path[DEV_PORT_PATH_SIZE - 1] = 0;

    if (m_state.program_state > DEV_PORT_PROGRAM_EXITED)
        m_state.program_state = DEV_PORT_PROGRAM_NONE;

    if (m_state.code_size != 32)
        m_state.code_size = 16;
}

void DevPort::Serialize(StateSerializer& serializer)
{
    G_SERIALIZE(serializer, m_state.program_state);
    G_SERIALIZE(serializer, m_state.load_flags);
    G_SERIALIZE_ARRAY(serializer, m_state.path, DEV_PORT_PATH_SIZE);
    G_SERIALIZE(serializer, m_state.psp);
    G_SERIALIZE(serializer, m_state.entry_cs);
    G_SERIALIZE(serializer, m_state.entry_ip);
    G_SERIALIZE(serializer, m_state.stack_ss);
    G_SERIALIZE(serializer, m_state.stack_sp);
    G_SERIALIZE(serializer, m_state.based);
    G_SERIALIZE(serializer, m_state.base);
    G_SERIALIZE(serializer, m_state.code_selector);
    G_SERIALIZE(serializer, m_state.data_selector);
    G_SERIALIZE(serializer, m_state.code_size);
    G_SERIALIZE(serializer, m_state.exit_code);
    G_SERIALIZE(serializer, m_state.exit_type);
    G_SERIALIZE(serializer, m_state.sequence);

    for (int i = 0; i < DEV_PORT_EVENT_COUNT; i++)
    {
        G_SERIALIZE(serializer, m_state.events[i].sequence);
        G_SERIALIZE(serializer, m_state.events[i].type);
    }

    G_SERIALIZE(serializer, m_state.run_offset);
    G_SERIALIZE(serializer, m_state.dos_known);
    G_SERIALIZE(serializer, m_state.dos_list);
    G_SERIALIZE(serializer, m_state.dos_indos);
    G_SERIALIZE(serializer, m_state.dos_version);

    u64 written = m_console.GetWrittenCount();
    G_SERIALIZE(serializer, written);

    if (serializer.IsLoading())
        m_console.Rewind(written);
}
