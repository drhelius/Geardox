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

#ifndef GDB_TARGET_H
#define GDB_TARGET_H

#include <string>
#include <vector>
#include "geardox.h"

class GdbServer;
class DebugAdapter;

enum Gdb_Address_Mode
{
    GDB_ADDRESS_AUTO = 0,
    GDB_ADDRESS_LINEAR,
    GDB_ADDRESS_PROGRAM
};

class GdbTarget
{
public:
    struct Gdb_Breakpoint
    {
        int type;
        u32 address;
        u32 length;
        u32 linear;
        bool armed;
    };

public:
    GdbTarget();
    ~GdbTarget();
    void Init(GeardoxCore* core, GdbServer* server);
    void SetWaitProgram(bool wait);
    void Pump();
    void OnProgramEvent(u8 type);

private:
    void Connect();
    void Detach();
    bool IsHolding();
    void Handle(const std::string& packet);
    void CheckStop();
    void SendOutput();
    void Reply(const std::string& payload);
    std::string StopReply();
    std::string TargetXml();
    std::string ReadRegisters();
    std::string ReadRegister(int index);
    bool WriteRegister(int index, const std::string& hex);
    std::string ReadMemory(u32 address, u32 length);
    bool WriteMemory(u32 address, const std::vector<u8>& data);
    std::string Breakpoint(bool insert, const std::string& args);
    void ArmBreakpoint(Gdb_Breakpoint& breakpoint);
    void DisarmBreakpoint(Gdb_Breakpoint& breakpoint);
    void RearmBreakpoints();
    void ClearBreakpoints();
    std::string Monitor(const std::string& command);
    std::string Query(const std::string& packet);
    void Resume(bool step);
    u32 GetBase();
    bool IsCode32();

private:
    GeardoxCore* m_core;
    GdbServer* m_server;
    DebugAdapter* m_adapter;
    unsigned int m_connection;
    bool m_attached;
    bool m_running;
    bool m_wait_program;
    bool m_holding;
    u64 m_hold_start;
    u64 m_output_mark;
    Gdb_Address_Mode m_mode;
    bool m_base_override;
    u32 m_base;
    u32 m_armed_base;
    bool m_exited;
    int m_exit_code;
    bool m_interrupted;
    std::vector<Gdb_Breakpoint> m_breakpoints;
    std::string m_target_xml;
};

#endif /* GDB_TARGET_H */
