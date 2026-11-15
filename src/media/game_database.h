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

#ifndef GAME_DATABASE_H
#define GAME_DATABASE_H

#include <string>
#include <vector>
#include "../common/common.h"

struct GameDatabase_File
{
    std::string path;
    u32 size;
    u32 crc;
    bool has_crc;
};

struct GameDatabase_Match
{
    int entry;
    int release;
    std::string root;
    bool preset;
};

struct GameDatabase_Rule
{
    std::string source;
    std::string target;
    bool file;
};

class GameDatabase
{
public:
    static bool Find(const char* path, std::vector<GameDatabase_Match>* matches);
    static bool List(const char* path, std::vector<GameDatabase_File>* files);
    static void Match(const std::vector<GameDatabase_File>& files, std::vector<GameDatabase_Match>* matches);
    static bool IsCandidate(u32 size);
    static int GetEntryCount();
    static int FindEntry(const char* id);
    static const char* GetId(int entry);
    static const char* GetName(int entry);
    static const char* GetDescriptor(int entry);
    static int GetEntry(int release);
    static const char* GetSettings(int release);
    static void GetLayout(int release, std::vector<GameDatabase_Rule>* rules);
};

#endif /* GAME_DATABASE_H */
