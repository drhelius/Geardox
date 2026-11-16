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

#ifndef DEV_PORT_H
#define DEV_PORT_H

#include <iostream>
#include <string>
#include "../common/common.h"
#include "debug_console.h"

class I386;
class StateSerializer;

#define DEV_PORT_PATH_SIZE 128
#define DEV_PORT_EVENT_COUNT 16
#define DEV_PORT_OUTPUT_LIMIT 4096
#define DEV_PORT_LOAD_STOP 0x01
#define DEV_PORT_LOAD_STOP_BASED 0x02
#define DEV_PORT_LOAD_COFF 0x04

enum DevPort_Program_State
{
    DEV_PORT_PROGRAM_NONE = 0,
    DEV_PORT_PROGRAM_RUNNING,
    DEV_PORT_PROGRAM_EXITED
};

enum DevPort_Event_Type
{
    DEV_PORT_EVENT_LOADED = 1,
    DEV_PORT_EVENT_BASED,
    DEV_PORT_EVENT_EXITED,
    DEV_PORT_EVENT_BREAK
};

enum DevPort_Command
{
    DEV_PORT_COMMAND_LOADED = 0x01,
    DEV_PORT_COMMAND_BASED = 0x02,
    DEV_PORT_COMMAND_EXITED = 0x03,
    DEV_PORT_COMMAND_OUTPUT = 0x04,
    DEV_PORT_COMMAND_BREAK = 0x05,
    DEV_PORT_COMMAND_RUN = 0x06,
    DEV_PORT_COMMAND_DOS = 0x07
};

class DevPort
{
public:
    typedef void (*DevPort_Event_Fn)(void* context, u8 type);

    struct DevPort_Event
    {
        u32 sequence;
        u8 type;
    };

    struct DevPort_State
    {
        u8 program_state;
        u8 load_flags;
        char path[DEV_PORT_PATH_SIZE];
        u16 psp;
        u16 entry_cs;
        u16 entry_ip;
        u16 stack_ss;
        u16 stack_sp;
        bool based;
        u32 base;
        u16 code_selector;
        u16 data_selector;
        u8 code_size;
        u8 exit_code;
        u8 exit_type;
        u32 sequence;
        DevPort_Event events[DEV_PORT_EVENT_COUNT];
        u32 run_offset;
        bool dos_known;
        u32 dos_list;
        u32 dos_indos;
        u16 dos_version;
    };

public:
    DevPort();
    ~DevPort();
    void Init(I386* cpu);
    void Reset();
    void SetConsoleEnabled(bool enabled);
    void SetControlEnabled(bool enabled);
    bool IsConsoleEnabled() const;
    bool IsControlEnabled() const;
    void SetEventCallback(void* context, DevPort_Event_Fn callback);
    void SetRunCommand(const char* command);
    const std::string& GetRunCommand() const;
    bool Decodes(u16 port) const;
    u8 Read(u16 port);
    u8 Peek(u16 port) const;
    void Write(u16 port, u8 value);
    DebugConsole* GetConsole();
    DevPort_State* GetState();
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream, int version = GDOX_SAVESTATE_VERSION);

private:
    void Hypercall(u8 command);
    void AddEvent(u8 type);
    bool IsCode32() const;
    u32 GetOffset(int reg) const;
    u32 ReadString(int segment, u32 offset, char* buffer, u32 size) const;
    void Serialize(StateSerializer& serializer);

private:
    DevPort_State m_state;
    I386* m_cpu;
    DebugConsole m_console;
    bool m_console_enabled;
    bool m_control_enabled;
    void* m_event_context;
    DevPort_Event_Fn m_event_callback;
    std::string m_run_command;
};

static const u16 k_dev_port_console = 0x00E9;
static const u16 k_dev_port_control = 0x0520;
static const u16 k_dev_port_stream = 0x0521;
static const u8 k_dev_port_console_readback = 0xE9;
static const u8 k_dev_port_signature = 0x47;

#include "dev_port_inline.h"

#endif /* DEV_PORT_H */
