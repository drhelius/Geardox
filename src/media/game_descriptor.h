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

#ifndef GAME_DESCRIPTOR_H
#define GAME_DESCRIPTOR_H

#include <map>
#include <string>
#include <vector>
#include "../common/common.h"

#define GDOX_GAME_DESCRIPTOR_VERSION 1
#define GDOX_GAME_DESCRIPTOR_MAX_SIZE (64 * 1024)

enum GDOX_Game_Memory
{
    GDOX_GAME_MEMORY_NONE = 0,
    GDOX_GAME_MEMORY_XMS,
    GDOX_GAME_MEMORY_UMB,
    GDOX_GAME_MEMORY_EMS
};

enum GDOX_Game_Exit
{
    GDOX_GAME_EXIT_PROMPT = 0,
    GDOX_GAME_EXIT_POWEROFF
};

enum GDOX_Game_System
{
    GDOX_GAME_SYSTEM_FREEDOS = 0,
    GDOX_GAME_SYSTEM_MSDOS,
    GDOX_GAME_SYSTEM_COUNT
};

enum GDOX_Game_Target
{
    GDOX_GAME_TARGET_GAME = 0,
    GDOX_GAME_TARGET_SETUP,
    GDOX_GAME_TARGET_PROMPT,
    GDOX_GAME_TARGET_COUNT
};

struct GDOX_Game_Env
{
    std::string name;
    std::string value;
};

// A frame of -1 lets JEMM pick it, 0 asks for none
struct GDOX_Game_Info
{
    std::string name;
    int memory;
    int ems_kb;
    int ems_frame;
    int files;
    int buffers;
    std::string dos_version;
    int system;
    bool system_hidden;
    bool cache;
    std::string source;
    std::string folder;
    int free_mb;
    std::string hard_disks[GDOX_MAX_HARD_DISKS];
    std::vector<std::string> floppies[2];
    std::vector<std::string> cds;
    std::string exe;
    std::string args;
    std::string cwd;
    std::string setup;
    int exit;
    bool boot_floppy;
    int joystick;
    std::vector<GDOX_Game_Env> env;
    std::vector<std::string> config_sys;
    std::vector<std::string> autoexec;
};

class GameDescriptor
{
public:
    GameDescriptor();
    ~GameDescriptor();
    void Clear();
    bool Parse(const char* path, const char* text, size_t size);
    bool Load(const char* path);
    void Merge(const GameDescriptor& overrides);
    bool Set(const char* section, const char* key, const char* value);
    void Unset(const char* section, const char* key);
    bool Has(const char* section, const char* key) const;
    std::string Get(const char* section, const char* key) const;
    bool IsEmpty() const;
    std::string Write() const;
    bool WriteFile(const char* path) const;
    const char* GetError() const;
    int GetErrorLine() const;
    int GetWarningCount() const;
    const char* GetWarning(int index) const;
    void GetMachineSettings(GDOX_Machine_Settings* settings) const;
    const GDOX_Game_Info& GetInfo() const;
    static bool IsDescriptor(const char* path);
    static const char* GetHardDiskKey(int id);
    static int GetEMSRAM(int ems_kb, int model, bool cirrus, int system);

private:
    bool ParseLine(const std::string& section, const std::string& line, int number);
    bool Fail(int line, const char* format, ...);
    void Warn(int line, const char* format, ...);
    void Build();

private:
    std::string m_path;
    std::map<std::string, std::string> m_values;
    std::vector<std::string> m_env_order;
    std::vector<std::string> m_config_sys;
    std::vector<std::string> m_autoexec;
    bool m_has_config_sys;
    bool m_has_autoexec;
    std::string m_error;
    int m_error_line;
    std::vector<std::string> m_warnings;
    GDOX_Game_Info m_info;
};

#endif /* GAME_DESCRIPTOR_H */
