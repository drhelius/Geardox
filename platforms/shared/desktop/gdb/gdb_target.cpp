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

#include <SDL3/SDL.h>
#include <stdlib.h>
#include "gdb_target.h"
#include "gdb_server.h"
#include "../mcp/mcp_debug_adapter.h"
#include "../emu.h"
#include "../emu_dev.h"
#include "../debug/gui_debug_memory.h"

static const int k_gdb_register_count = 32;
static const int k_gdb_burst_ms = 4;
static const u64 k_gdb_hold_ms = 120000;
static const u32 k_gdb_max_read = 0x1000;
static const int k_gdb_segments[6] = { I386_SEGMENT_CS, I386_SEGMENT_SS, I386_SEGMENT_DS, I386_SEGMENT_ES,
    I386_SEGMENT_FS, I386_SEGMENT_GS };
static const char* const k_gdb_general_names[8] = { "EAX", "ECX", "EDX", "EBX", "ESP", "EBP", "ESI", "EDI" };
static const char* const k_gdb_segment_names[6] = { "CS", "SS", "DS", "ES", "FS", "GS" };

static std::string hex_u32(u32 value);
static std::string hex_text(const std::string& text);
static std::string hex_number(u32 value);
static bool parse_hex(const std::string& text, u32& value);
static u32 read_le(const std::vector<u8>& data, size_t offset, size_t size);

GdbTarget::GdbTarget()
{
    InitPointer(m_core);
    InitPointer(m_server);
    InitPointer(m_adapter);
    m_connection = 0;
    m_attached = false;
    m_running = false;
    m_wait_program = false;
    m_holding = false;
    m_hold_start = 0;
    m_output_mark = 0;
    m_mode = GDB_ADDRESS_AUTO;
    m_base_override = false;
    m_base = 0;
    m_armed_base = 0;
    m_exited = false;
    m_exit_code = 0;
    m_interrupted = false;
}

GdbTarget::~GdbTarget()
{
    SafeDelete(m_adapter);
}

void GdbTarget::Init(GeardoxCore* core, GdbServer* server)
{
    m_core = core;
    m_server = server;
    m_adapter = new DebugAdapter(core);
}

void GdbTarget::SetWaitProgram(bool wait)
{
    m_wait_program = wait;
}

void GdbTarget::Pump()
{
    if (!IsValidPointer(m_server) || !m_server->IsRunning())
        return;

    if (m_server->GetConnection() != m_connection)
    {
        m_connection = m_server->GetConnection();
        Connect();
    }

    if (!m_server->IsConnected())
    {
        if (m_attached)
            Detach();

        return;
    }

    if (IsHolding())
        return;

    CheckStop();
    SendOutput();

    std::string packet;
    int wait = 0;

    while (m_server->Pop(packet, wait))
    {
        Handle(packet);
        CheckStop();
        wait = m_running ? 0 : k_gdb_burst_ms;
    }
}

void GdbTarget::OnProgramEvent(u8 type)
{
    if (!m_attached)
        return;

    RearmBreakpoints();

    if ((type == DEV_PORT_EVENT_EXITED) && m_running)
    {
        Dev_Program_Info program;
        emu_dev_get_program(&program);
        m_exited = true;
        m_exit_code = program.exit_code;
    }
}

void GdbTarget::Connect()
{
    ClearBreakpoints();
    m_attached = true;
    m_running = false;
    m_exited = false;
    m_interrupted = false;
    m_target_xml.clear();
    m_output_mark = emu_dev_get_output_count();
    m_armed_base = GetBase();
    m_holding = m_wait_program;
    m_hold_start = SDL_GetTicks();
}

void GdbTarget::Detach()
{
    ClearBreakpoints();
    m_attached = false;
    m_running = false;

    if (emu_is_debug_idle())
        emu_debug_continue();
}

bool GdbTarget::IsHolding()
{
    if (!m_holding)
        return false;

    Dev_Program_Info program;
    emu_dev_get_program(&program);
    bool stopped = (program.state == DEV_PORT_PROGRAM_RUNNING) && emu_is_debug_idle();

    if (stopped || (program.state == DEV_PORT_PROGRAM_EXITED) || (SDL_GetTicks() - m_hold_start > k_gdb_hold_ms))
    {
        m_holding = false;
        m_armed_base = GetBase();
        m_output_mark = emu_dev_get_output_count();
    }

    return m_holding;
}

void GdbTarget::Handle(const std::string& packet)
{
    if (packet == GDB_SERVER_INTERRUPT)
    {
        if (!emu_is_debug_idle())
        {
            m_interrupted = true;
            emu_debug_break();
        }

        return;
    }

    if (packet.empty())
    {
        Reply("");
        return;
    }

    std::string args = packet.substr(1);

    switch (packet[0])
    {
        case '?':
            if (!emu_is_debug_idle())
                emu_debug_break();

            m_running = false;
            Reply("T05thread:1;");
            break;
        case 'g':
            Reply(ReadRegisters());
            break;
        case 'G':
        {
            std::vector<u8> data;

            if (!parse_hex_bytes(args, &data) || (data.size() < 64))
            {
                Reply("E01");
                break;
            }

            for (int i = 0; i < 10; i++)
                WriteRegister(i, args.substr(i * 8, 8));

            Reply("OK");
            break;
        }
        case 'p':
        {
            u32 index = 0;
            Reply(parse_hex(args, index) && (index < (u32)k_gdb_register_count) ? ReadRegister((int)index) : "E01");
            break;
        }
        case 'P':
        {
            size_t equals = args.find('=');
            u32 index = 0;
            bool valid = (equals != std::string::npos) && parse_hex(args.substr(0, equals), index) &&
                WriteRegister((int)index, args.substr(equals + 1));
            Reply(valid ? "OK" : "E01");
            break;
        }
        case 'm':
        {
            size_t comma = args.find(',');
            u32 address = 0;
            u32 length = 0;

            if ((comma == std::string::npos) || !parse_hex(args.substr(0, comma), address) ||
                !parse_hex(args.substr(comma + 1), length))
                Reply("E01");
            else
                Reply(ReadMemory(address, length));
            break;
        }
        case 'M':
        case 'X':
        {
            size_t comma = args.find(',');
            size_t colon = args.find(':');
            u32 address = 0;
            u32 length = 0;

            if ((comma == std::string::npos) || (colon == std::string::npos) || !parse_hex(args.substr(0, comma), address) ||
                !parse_hex(args.substr(comma + 1, colon - comma - 1), length))
            {
                Reply("E01");
                break;
            }

            std::vector<u8> data;

            if (packet[0] == 'X')
                data.assign(args.begin() + (long)colon + 1, args.end());
            else if (!parse_hex_bytes(args.substr(colon + 1), &data))
            {
                Reply("E01");
                break;
            }

            if (data.size() > length)
                data.resize(length);

            Reply(data.empty() || WriteMemory(address, data) ? "OK" : "E14");
            break;
        }
        case 'Z':
        case 'z':
            Reply(Breakpoint(packet[0] == 'Z', args));
            break;
        case 'c':
        case 'C':
            Resume(false);
            break;
        case 's':
        case 'S':
            Resume(true);
            break;
        case 'D':
            Reply("OK");
            Detach();
            break;
        case 'k':
            Detach();
            break;
        case 'H':
        case 'T':
            Reply("OK");
            break;
        case 'q':
        case 'Q':
            if (packet == "QStartNoAckMode")
            {
                Reply("OK");
                m_server->SetNoAck(true);
            }
            else
                Reply(Query(packet));
            break;
        case 'v':
            if (packet == "vCont?")
                Reply("vCont;c;C;s;S;t");
            else if (packet.compare(0, 6, "vCont;") == 0)
            {
                char action = packet.size() > 6 ? packet[6] : 'c';

                if ((action == 's') || (action == 'S'))
                    Resume(true);
                else if (action == 't')
                {
                    if (!emu_is_debug_idle())
                        emu_debug_break();

                    m_running = true;
                    m_interrupted = true;
                }
                else
                    Resume(false);
            }
            else if (packet.compare(0, 5, "vKill") == 0)
            {
                Reply("OK");
                Detach();
            }
            else
                Reply("");
            break;
        default:
            Reply("");
            break;
    }
}

void GdbTarget::CheckStop()
{
    if (!m_running)
        return;

    if (m_exited)
    {
        u8 code = (u8)m_exit_code;
        SendOutput();
        m_running = false;
        m_exited = false;
        Reply("W" + hex_string(&code, 1, false));
        return;
    }

    if (!emu_is_debug_idle())
        return;

    SendOutput();
    m_running = false;
    Reply(StopReply());
}

void GdbTarget::SendOutput()
{
    if (!m_running)
        return;

    while (true)
    {
        u64 next = m_output_mark;
        std::string text = emu_dev_get_output(m_output_mark, 512, &next);

        if (text.empty())
            break;

        m_output_mark = next;
        Reply("O" + hex_text(text));
    }
}

void GdbTarget::Reply(const std::string& payload)
{
    m_server->Send(payload);
}

std::string GdbTarget::StopReply()
{
    if (m_interrupted)
    {
        m_interrupted = false;
        return "T02thread:1;";
    }

    I386_Breakpoint_Hit hit;
    std::string reason;

    if (m_core->GetI386()->GetBreakpointHit(hit) && (hit.request == I386_HIT_REQUEST_NONE) && !hit.interrupt)
    {
        if (hit.type == I386_BREAKPOINT_EXECUTE)
        {
            reason = "swbreak:;";

            for (size_t i = 0; i < m_breakpoints.size(); i++)
            {
                if (m_breakpoints[i].armed && (m_breakpoints[i].type == 1) && (m_breakpoints[i].linear == hit.address))
                    reason = "hwbreak:;";
            }
        }
        else
        {
            const char* kind = hit.type == I386_BREAKPOINT_WRITE ? "watch" : hit.type == I386_BREAKPOINT_READ ? "rwatch" :
                "awatch";
            reason = std::string(kind) + ":" + hex_number(hit.address - GetBase()) + ";";
        }
    }

    return "T05thread:1;" + reason;
}

std::string GdbTarget::TargetXml()
{
    std::string xml = "<?xml version=\"1.0\"?>\n<!DOCTYPE target SYSTEM \"gdb-target.dtd\">\n<target version=\"1.0\">\n";
    xml += std::string("<architecture>") + (IsCode32() ? "i386" : "i8086") + "</architecture>\n";
    xml += "<feature name=\"org.gnu.gdb.i386.core\">\n";
    xml += "<flags id=\"i386_eflags\" size=\"4\">\n";
    xml += "<field name=\"CF\" start=\"0\" end=\"0\"/><field name=\"\" start=\"1\" end=\"1\"/>\n";
    xml += "<field name=\"PF\" start=\"2\" end=\"2\"/><field name=\"AF\" start=\"4\" end=\"4\"/>\n";
    xml += "<field name=\"ZF\" start=\"6\" end=\"6\"/><field name=\"SF\" start=\"7\" end=\"7\"/>\n";
    xml += "<field name=\"TF\" start=\"8\" end=\"8\"/><field name=\"IF\" start=\"9\" end=\"9\"/>\n";
    xml += "<field name=\"DF\" start=\"10\" end=\"10\"/><field name=\"OF\" start=\"11\" end=\"11\"/>\n";
    xml += "<field name=\"NT\" start=\"14\" end=\"14\"/><field name=\"RF\" start=\"16\" end=\"16\"/>\n";
    xml += "<field name=\"VM\" start=\"17\" end=\"17\"/><field name=\"AC\" start=\"18\" end=\"18\"/>\n";
    xml += "<field name=\"VIF\" start=\"19\" end=\"19\"/><field name=\"VIP\" start=\"20\" end=\"20\"/>\n";
    xml += "<field name=\"ID\" start=\"21\" end=\"21\"/>\n</flags>\n";

    static const char* const k_general[8] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
    static const char* const k_general_types[8] = { "int32", "int32", "int32", "int32", "data_ptr", "data_ptr", "int32",
        "int32" };

    for (int i = 0; i < 8; i++)
        xml += std::string("<reg name=\"") + k_general[i] + "\" bitsize=\"32\" type=\"" + k_general_types[i] + "\"/>\n";

    xml += "<reg name=\"eip\" bitsize=\"32\" type=\"code_ptr\"/>\n";
    xml += "<reg name=\"eflags\" bitsize=\"32\" type=\"i386_eflags\"/>\n";

    static const char* const k_segments[6] = { "cs", "ss", "ds", "es", "fs", "gs" };

    for (int i = 0; i < 6; i++)
        xml += std::string("<reg name=\"") + k_segments[i] + "\" bitsize=\"32\" type=\"int32\"/>\n";

    for (int i = 0; i < 8; i++)
        xml += "<reg name=\"st" + std::to_string(i) + "\" bitsize=\"80\" type=\"i387_ext\"/>\n";

    static const char* const k_fpu[8] = { "fctrl", "fstat", "ftag", "fiseg", "fioff", "foseg", "fooff", "fop" };

    for (int i = 0; i < 8; i++)
        xml += std::string("<reg name=\"") + k_fpu[i] + "\" bitsize=\"32\" type=\"int\" group=\"float\"/>\n";

    xml += "</feature>\n</target>\n";
    return xml;
}

std::string GdbTarget::ReadRegisters()
{
    std::string out;

    for (int i = 0; i < k_gdb_register_count; i++)
        out += ReadRegister(i);

    return out;
}

std::string GdbTarget::ReadRegister(int index)
{
    I386_State* cpu = m_core->GetI386()->GetState();
    X87::X87_State* fpu = m_core->GetX87()->GetState();

    if (index < 8)
        return hex_u32(cpu->registers[index].value);

    if (index == 8)
        return hex_u32(cpu->segments[I386_SEGMENT_CS].base + cpu->eip - GetBase());

    if (index == 9)
        return hex_u32(cpu->eflags);

    if (index < 16)
        return hex_u32(cpu->segments[k_gdb_segments[index - 10]].selector);

    if (index < 24)
    {
        int top = (m_core->GetX87()->GetStatusWord() >> 11) & 7;
        const X87::X87_Register& reg = fpu->registers[(top + index - 16) & 7];
        u8 bytes[10];

        for (int i = 0; i < 8; i++)
            bytes[i] = (u8)(reg.significand >> (i * 8));

        bytes[8] = (u8)reg.sign_exponent;
        bytes[9] = (u8)(reg.sign_exponent >> 8);
        return hex_string(bytes, 10, false);
    }

    switch (index)
    {
        case 24: return hex_u32(fpu->control);
        case 25: return hex_u32(m_core->GetX87()->GetStatusWord());
        case 26: return hex_u32(m_core->GetX87()->GetTagWord());
        case 27: return hex_u32(fpu->instruction_selector);
        case 28: return hex_u32(fpu->instruction_offset);
        case 29: return hex_u32(fpu->operand_selector);
        case 30: return hex_u32(fpu->operand_offset);
        default: return hex_u32(fpu->opcode);
    }
}

bool GdbTarget::WriteRegister(int index, const std::string& hex)
{
    std::vector<u8> data;

    if (!parse_hex_bytes(hex, &data) || (index < 0) || (index >= k_gdb_register_count))
        return false;

    if ((index >= 16) && (index < 24))
    {
        if (data.size() < 10)
            return false;

        X87::X87_State* fpu = m_core->GetX87()->GetState();
        int top = (m_core->GetX87()->GetStatusWord() >> 11) & 7;
        X87::X87_Register& reg = fpu->registers[(top + index - 16) & 7];
        reg.significand = read_u64_le(&data[0]);
        reg.sign_exponent = read_u16_le(&data[8]);
        return true;
    }

    if (data.size() < 4)
        return false;

    u32 value = read_le(data, 0, 4);

    if (index < 8)
        return !m_adapter->WriteI386Register(k_gdb_general_names[index], value).contains("error");

    if (index == 8)
    {
        I386_State* cpu = m_core->GetI386()->GetState();
        return !m_adapter->WriteI386Register("EIP", value + GetBase() - cpu->segments[I386_SEGMENT_CS].base).contains("error");
    }

    if (index == 9)
        return !m_adapter->WriteI386Register("EFLAGS", value).contains("error");

    if (index < 16)
    {
        I386_State* cpu = m_core->GetI386()->GetState();

        if (cpu->segments[k_gdb_segments[index - 10]].selector == (u16)value)
            return true;

        return !m_adapter->WriteI386Register(k_gdb_segment_names[index - 10], value & 0xFFFF).contains("error");
    }

    X87::X87_State* fpu = m_core->GetX87()->GetState();

    if (index == 24)
        fpu->control = (u16)value;
    else if (index == 25)
        fpu->status = (u16)value;

    return true;
}

std::string GdbTarget::ReadMemory(u32 address, u32 length)
{
    length = MIN(length, k_gdb_max_read);

    if (length == 0)
        return "";

    std::vector<u8> data(length);
    std::vector<GDOX_Debug_Memory_Status> status(length);
    GDOX_Debug_Memory_Address source;
    source.space = GDOX_DEBUG_MEMORY_LINEAR;
    source.address = GetBase() + address;
    source.segment = 0;
    source.segment_register = -1;
    source.region = 0;
    gui_debug_memory_read(source, &data[0], &status[0], length);

    u32 count = 0;

    while ((count < length) && ((status[count] == GDOX_DEBUG_MEMORY_VALID) || (status[count] == GDOX_DEBUG_MEMORY_READ_ONLY)))
        count++;

    return count == 0 ? "E14" : hex_string(&data[0], count, false);
}

bool GdbTarget::WriteMemory(u32 address, const std::vector<u8>& data)
{
    GDOX_Debug_Memory_Address target;
    target.space = GDOX_DEBUG_MEMORY_LINEAR;
    target.address = GetBase() + address;
    target.segment = 0;
    target.segment_register = -1;
    target.region = 0;
    return gui_debug_memory_write(target, &data[0], (u32)data.size());
}

std::string GdbTarget::Breakpoint(bool insert, const std::string& args)
{
    size_t first = args.find(',');
    size_t second = first == std::string::npos ? std::string::npos : args.find(',', first + 1);
    u32 type = 0;
    u32 address = 0;
    u32 kind = 0;

    if ((second == std::string::npos) || !parse_hex(args.substr(0, first), type) ||
        !parse_hex(args.substr(first + 1, second - first - 1), address) ||
        !parse_hex(args.substr(second + 1, args.find(';') == std::string::npos ? std::string::npos :
        args.find(';') - second - 1), kind) || (type > 4))
        return "E01";

    u32 length = type <= 1 ? 1 : MAX(kind, (u32)1);

    for (size_t i = 0; i < m_breakpoints.size(); i++)
    {
        Gdb_Breakpoint& existing = m_breakpoints[i];

        if ((existing.type != (int)type) || (existing.address != address) || (existing.length != length))
            continue;

        if (!insert)
        {
            DisarmBreakpoint(existing);
            m_breakpoints.erase(m_breakpoints.begin() + (long)i);
        }

        return "OK";
    }

    if (!insert)
        return "OK";

    Gdb_Breakpoint breakpoint;
    breakpoint.type = (int)type;
    breakpoint.address = address;
    breakpoint.length = length;
    breakpoint.linear = 0;
    breakpoint.armed = false;
    ArmBreakpoint(breakpoint);

    if (!breakpoint.armed)
        return "E01";

    m_breakpoints.push_back(breakpoint);
    return "OK";
}

void GdbTarget::ArmBreakpoint(Gdb_Breakpoint& breakpoint)
{
    static const u8 k_types[5] = { I386_BREAKPOINT_EXECUTE, I386_BREAKPOINT_EXECUTE, I386_BREAKPOINT_WRITE,
        I386_BREAKPOINT_READ, I386_BREAKPOINT_READ | I386_BREAKPOINT_WRITE };

    breakpoint.linear = GetBase() + breakpoint.address;
    breakpoint.armed = m_core->GetI386()->AddBreakpoint(breakpoint.linear, breakpoint.linear + breakpoint.length - 1,
        k_types[breakpoint.type], I386_BREAKPOINT_LINEAR);
}

void GdbTarget::DisarmBreakpoint(Gdb_Breakpoint& breakpoint)
{
    static const u8 k_types[5] = { I386_BREAKPOINT_EXECUTE, I386_BREAKPOINT_EXECUTE, I386_BREAKPOINT_WRITE,
        I386_BREAKPOINT_READ, I386_BREAKPOINT_READ | I386_BREAKPOINT_WRITE };

    if (!breakpoint.armed)
        return;

    bool shared = false;

    for (size_t i = 0; i < m_breakpoints.size(); i++)
    {
        const Gdb_Breakpoint& other = m_breakpoints[i];

        if ((&other != &breakpoint) && other.armed && (other.linear == breakpoint.linear) &&
            (other.length == breakpoint.length) && (k_types[other.type] == k_types[breakpoint.type]))
            shared = true;
    }

    if (!shared)
        m_core->GetI386()->RemoveBreakpoint(breakpoint.linear, breakpoint.linear + breakpoint.length - 1,
            k_types[breakpoint.type], I386_BREAKPOINT_LINEAR);

    breakpoint.armed = false;
}

void GdbTarget::RearmBreakpoints()
{
    u32 base = GetBase();

    if (base == m_armed_base)
        return;

    for (size_t i = 0; i < m_breakpoints.size(); i++)
        DisarmBreakpoint(m_breakpoints[i]);

    for (size_t i = 0; i < m_breakpoints.size(); i++)
        ArmBreakpoint(m_breakpoints[i]);

    m_armed_base = base;
}

void GdbTarget::ClearBreakpoints()
{
    for (size_t i = 0; i < m_breakpoints.size(); i++)
        DisarmBreakpoint(m_breakpoints[i]);

    m_breakpoints.clear();
}

std::string GdbTarget::Monitor(const std::string& command)
{
    std::string text;
    std::string name = command.substr(0, command.find(' '));
    std::string argument = command.find(' ') == std::string::npos ? std::string() : command.substr(command.find(' ') + 1);
    Dev_Program_Info program;
    emu_dev_get_program(&program);
    char line[256];

    if (name == "program")
    {
        static const char* const k_states[3] = { "none", "running", "exited" };
        snprintf(line, sizeof(line), "state %s\npath %s\nhost %s\npsp %04X\nentry %04X:%04X\nbase %08X%s\nmode %s\n",
            k_states[program.state % 3], program.dos_path.c_str(), program.host_path.c_str(), program.psp,
            program.entry_cs, program.entry_ip, GetBase(), program.based ? " (reported)" : "",
            m_mode == GDB_ADDRESS_LINEAR ? "linear" : m_mode == GDB_ADDRESS_PROGRAM ? "program" : "auto");
        text = line;

        if (program.state == DEV_PORT_PROGRAM_EXITED)
        {
            snprintf(line, sizeof(line), "exit code %d\n", program.exit_code);
            text += line;
        }

        if (!program.symbol_path.empty())
        {
            snprintf(line, sizeof(line), "symbols %d from %s\n", program.symbol_count, program.symbol_path.c_str());
            text += line;
        }
    }
    else if (name == "mode")
    {
        if (argument == "linear")
            m_mode = GDB_ADDRESS_LINEAR;
        else if (argument == "program")
            m_mode = GDB_ADDRESS_PROGRAM;
        else if (argument == "auto")
            m_mode = GDB_ADDRESS_AUTO;
        else
            return hex_text("mode linear, program or auto\n");

        RearmBreakpoints();
        snprintf(line, sizeof(line), "mode %s, base %08X\n", argument.c_str(), GetBase());
        text = line;
    }
    else if (name == "base")
    {
        u32 base = 0;

        if (argument == "auto")
            m_base_override = false;
        else if (parse_hex_with_prefix(argument, &base))
        {
            m_base_override = true;
            m_base = base;
        }
        else
            return hex_text("base ADDRESS (hex) or auto\n");

        RearmBreakpoints();
        snprintf(line, sizeof(line), "base %08X\n", GetBase());
        text = line;
    }
    else if (name == "relaunch")
        text = emu_dev_relaunch() ? "relaunching the game\n" : "no game is open\n";
    else if (name == "reset")
    {
        emu_reset();
        text = "reset\n";
    }
    else if (name == "screen")
    {
        json screen = m_adapter->GetTextScreen(false);

        if (screen.contains("error"))
            text = screen["error"].get<std::string>() + "\n";
        else
        {
            for (size_t i = 0; i < screen["lines"].size(); i++)
                text += screen["lines"][i].get<std::string>() + "\n";
        }
    }
    else if (name == "output")
    {
        u64 next = 0;
        text = emu_dev_get_output(0, DEBUG_CONSOLE_SIZE, &next);
    }
    else if (name == "symbols")
    {
        int count = emu_dev_load_program_symbols(argument.c_str());

        if (count < 0)
            snprintf(line, sizeof(line), "unable to read %s\n", argument.c_str());
        else
            snprintf(line, sizeof(line), "%d symbols loaded\n", count);

        text = line;
    }
    else
        text = "Geardox monitor commands:\n"
            "  program          the program GDXDBG started, its base and exit code\n"
            "  mode MODE        linear, program or auto (program once a program runs)\n"
            "  base ADDRESS     program base by hand, auto to follow the program\n"
            "  relaunch         rebuild the game's disk from its folder and run the program again\n"
            "  reset            reset the machine\n"
            "  screen           the text screen\n"
            "  output           the program console\n"
            "  symbols FILE     load program symbols into the Geardox debugger\n";

    return hex_text(text);
}

std::string GdbTarget::Query(const std::string& packet)
{
    if (packet.compare(0, 10, "qSupported") == 0)
        return "PacketSize=4000;qXfer:features:read+;QStartNoAckMode+;swbreak+;hwbreak+;vContSupported+";

    if (packet.compare(0, 30, "qXfer:features:read:target.xml") == 0)
    {
        size_t colon = packet.find(':', 30);
        size_t comma = packet.find(',', colon);
        u32 offset = 0;
        u32 length = 0;

        if ((colon == std::string::npos) || (comma == std::string::npos) ||
            !parse_hex(packet.substr(colon + 1, comma - colon - 1), offset) || !parse_hex(packet.substr(comma + 1), length))
            return "E01";

        if (m_target_xml.empty())
            m_target_xml = TargetXml();

        if (offset >= m_target_xml.size())
            return "l";

        std::string chunk = m_target_xml.substr(offset, length);
        return (offset + chunk.size() >= m_target_xml.size() ? "l" : "m") + chunk;
    }

    if (packet == "qAttached")
        return "1";

    if (packet == "qC")
        return "QC1";

    if (packet == "qfThreadInfo")
        return "m1";

    if (packet == "qsThreadInfo")
        return "l";

    if (packet == "qOffsets")
        return "Text=0;Data=0;Bss=0";

    if (packet.compare(0, 7, "qSymbol") == 0)
        return "OK";

    if (packet.compare(0, 17, "qThreadExtraInfo,") == 0)
        return hex_text("Geardox CPU");

    if (packet.compare(0, 6, "qRcmd,") == 0)
    {
        std::vector<u8> data;

        if (!parse_hex_bytes(packet.substr(6), &data))
            return "E01";

        return Monitor(std::string(data.begin(), data.end()));
    }

    return "";
}

void GdbTarget::Resume(bool step)
{
    m_running = true;
    m_interrupted = false;

    if (step)
        emu_debug_step_into();
    else
        emu_debug_continue();
}

u32 GdbTarget::GetBase()
{
    if (m_mode == GDB_ADDRESS_LINEAR)
        return 0;

    if (m_base_override)
        return m_base;

    Dev_Program_Info program;
    emu_dev_get_program(&program);
    return program.state == DEV_PORT_PROGRAM_RUNNING ? program.base : 0;
}

bool GdbTarget::IsCode32()
{
    Dev_Program_Info program;
    emu_dev_get_program(&program);

    if ((m_mode != GDB_ADDRESS_LINEAR) && (program.state == DEV_PORT_PROGRAM_RUNNING))
        return ((program.load_flags & DEV_PORT_LOAD_COFF) != 0) || (program.based && (program.code_size == 32));

    I386_State* cpu = m_core->GetI386()->GetState();
    return (cpu->execution_mode == I386_MODE_PROTECTED) &&
        ((cpu->segments[I386_SEGMENT_CS].attributes & I386_SEGMENT_DEFAULT_32) != 0);
}

static std::string hex_u32(u32 value)
{
    u8 bytes[4];
    write_u32_le(bytes, value);
    return hex_string(bytes, 4, false);
}

static std::string hex_text(const std::string& text)
{
    return hex_string((const u8*)text.data(), text.size(), false);
}

static std::string hex_number(u32 value)
{
    char text[16];
    snprintf(text, sizeof(text), "%x", value);
    return text;
}

static bool parse_hex(const std::string& text, u32& value)
{
    return parse_hex_string(text.c_str(), text.size(), &value);
}

static u32 read_le(const std::vector<u8>& data, size_t offset, size_t size)
{
    u32 value = 0;

    for (size_t i = 0; (i < size) && (offset + i < data.size()); i++)
        value |= (u32)data[offset + i] << (i * 8);

    return value;
}
