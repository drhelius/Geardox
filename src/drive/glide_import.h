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

#ifndef GLIDE_IMPORT_H
#define GLIDE_IMPORT_H

#include <string>
#include "../common/common.h"

enum GlideImport_Result
{
    GLIDE_IMPORT_ERROR = 0,
    GLIDE_IMPORT_NOT_FOUND,
    GLIDE_IMPORT_DONE
};

class GlideImport
{
public:
    GlideImport();
    ~GlideImport();
    GlideImport_Result Import(const char* source, const char* destination);
    GlideImport_Result Find(const char* source);
    GlideImport_Result Write(const char* destination);
    bool IsImported(const char* path) const;
    const char* GetError() const;
    const char* GetMessage() const;
    static bool IsOverlay(const std::string& data);
    static bool IsStub(const std::string& data);
    static bool ImportsOverlay(const std::string& data);
    static bool Load(const char* path, std::string* data, std::string* name);
    static const char* GetName(const std::string& data);

private:
    bool SearchFolder(const std::string& path, int depth);
    bool SearchZip(const u8* data, size_t size, int depth);
    void Consider(const std::string& name, const std::string& data);

private:
    std::string m_overlay;
    std::string m_message;
    std::string m_error;
    bool m_stub;
};

static const char k_glide_import_file[] = "GLIDE2X.OVL";

#endif /* GLIDE_IMPORT_H */
