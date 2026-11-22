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

#include "import_common.h"
#include "../media/media_file.h"

std::string import_get_value(const std::string& text, const char* key)
{
    std::string prefix = std::string(key) + "=";
    size_t at = text.find(prefix);

    if ((at == std::string::npos) || ((at != 0) && (text[at - 1] != '\n')))
        return "";

    size_t end = text.find_first_of("\r\n", at);
    return text.substr(at + prefix.size(), end == std::string::npos ? std::string::npos : end - at - prefix.size());
}

bool import_read_version(MediaZip* zip, std::string* text)
{
    return zip->Extract(k_import_version, text) && !import_get_value(*text, "id").empty();
}

bool import_open(const char* zip_path, u64 max_size, const char* kind, std::vector<u8>* data, MediaZip* zip,
    std::string* text, std::string* error)
{
    if (!MediaFile::ReadFile(zip_path, max_size, data))
    {
        *error = std::string("Unable to read ") + zip_path;
        return false;
    }

    if (!zip->Open(data->data(), data->size()) || !import_read_version(zip, text))
    {
        *error = std::string("Not an ") + kind + " import: " + zip_path;
        return false;
    }

    return true;
}

bool import_read_fingerprint(const char* zip_path, u64 max_size, u32* fingerprint)
{
    std::vector<u8> data;
    MediaZip zip;

    if (!MediaFile::ReadFile(zip_path, max_size, &data) || !zip.Open(data.data(), data.size()))
        return false;

    std::string text;
    bool ok = import_read_version(&zip, &text);
    zip.Close();
    std::string source = import_get_value(text, "source");

    if (!parse_hex_string(source.c_str(), source.size(), fingerprint))
        *fingerprint = 0;

    return ok;
}
