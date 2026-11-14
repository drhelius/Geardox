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

#include <stdarg.h>
#include <algorithm>
#include "game_descriptor.h"
#include "media_file.h"
#include "../input/key_map.h"
#include "../system/machine_profiles.h"

enum Descriptor_Key_Type
{
    DESCRIPTOR_TEXT = 0,
    DESCRIPTOR_PATH,
    DESCRIPTOR_LIST,
    DESCRIPTOR_CHOICE,
    DESCRIPTOR_NUMBER,
    DESCRIPTOR_MODEL,
    DESCRIPTOR_CPU,
    DESCRIPTOR_RAM,
    DESCRIPTOR_SOUND,
    DESCRIPTOR_VIDEO,
    DESCRIPTOR_MOUSE,
    DESCRIPTOR_JOYSTICK,
    DESCRIPTOR_CD_SPEED,
    DESCRIPTOR_FRAME,
    DESCRIPTOR_DOS_VERSION,
    DESCRIPTOR_DOS_DIR,
    DESCRIPTOR_KEYBOARD
};

struct Descriptor_Key
{
    const char* section;
    const char* key;
    Descriptor_Key_Type type;
    const char* choices;
    int min;
    int max;
};

static const Descriptor_Key k_descriptor_keys[] =
{
    { "geardox", "version", DESCRIPTOR_NUMBER, NULL, 1, 1000 },
    { "game", "name", DESCRIPTOR_TEXT, NULL, 0, 0 },
    { "game", "source", DESCRIPTOR_PATH, NULL, 0, 0 },
    { "game", "folder", DESCRIPTOR_DOS_DIR, NULL, 0, 0 },
    { "game", "free_mb", DESCRIPTOR_NUMBER, NULL, 16, 1024 },
    { "machine", "model", DESCRIPTOR_MODEL, NULL, 0, 0 },
    { "machine", "cpu_mhz", DESCRIPTOR_CPU, NULL, 0, 0 },
    { "machine", "turbo", DESCRIPTOR_CHOICE, "on,off", 0, 0 },
    { "machine", "ram_mb", DESCRIPTOR_RAM, NULL, 0, 0 },
    { "machine", "fpu", DESCRIPTOR_CHOICE, "none,80387", 0, 0 },
    { "machine", "floppy_a_type", DESCRIPTOR_CHOICE, "360k,1200k,720k,1440k,2880k", 0, 0 },
    { "machine", "floppy_b_type", DESCRIPTOR_CHOICE, "360k,1200k,720k,1440k,2880k", 0, 0 },
    { "machine", "cdrom_speed", DESCRIPTOR_CD_SPEED, NULL, 0, 0 },
    { "machine", "drive_timing", DESCRIPTOR_CHOICE, "real,fast", 0, 0 },
    { "machine", "video", DESCRIPTOR_VIDEO, NULL, 0, 0 },
    { "sound", "card", DESCRIPTOR_SOUND, NULL, 0, 0 },
    { "sound", "irq", DESCRIPTOR_CHOICE, "5,7", 0, 0 },
    { "sound", "dma", DESCRIPTOR_CHOICE, "1,3", 0, 0 },
    { "sound", "mpu", DESCRIPTOR_CHOICE, "on,off", 0, 0 },
    { "sound", "midi", DESCRIPTOR_CHOICE, "gm,mt32", 0, 0 },
    { "sound", "midi_port", DESCRIPTOR_TEXT, NULL, 0, 0 },
    { "input", "mouse", DESCRIPTOR_MOUSE, NULL, 0, 0 },
    { "input", "joystick", DESCRIPTOR_JOYSTICK, NULL, 0, 0 },
    { "dos", "memory", DESCRIPTOR_CHOICE, "none,xms,umb,ems", 0, 0 },
    { "dos", "ems_kb", DESCRIPTOR_NUMBER, NULL, 16, 32768 },
    { "dos", "ems_frame", DESCRIPTOR_FRAME, NULL, 0, 0 },
    { "dos", "files", DESCRIPTOR_NUMBER, NULL, 8, 255 },
    { "dos", "buffers", DESCRIPTOR_NUMBER, NULL, 1, 99 },
    { "dos", "version", DESCRIPTOR_DOS_VERSION, NULL, 0, 0 },
    { "dos", "system", DESCRIPTOR_CHOICE, "freedos,msdos", 0, 0 },
    { "dos", "system_files", DESCRIPTOR_CHOICE, "visible,hidden", 0, 0 },
    { "dos", "cache", DESCRIPTOR_CHOICE, "off,on", 0, 0 },
    { "dos", "keyboard", DESCRIPTOR_KEYBOARD, NULL, 0, 0 },
    { "drives", "floppy_a", DESCRIPTOR_LIST, NULL, 0, 0 },
    { "drives", "floppy_b", DESCRIPTOR_LIST, NULL, 0, 0 },
    { "drives", "hdd_primary_slave", DESCRIPTOR_PATH, NULL, 0, 0 },
    { "drives", "hdd_secondary_slave", DESCRIPTOR_PATH, NULL, 0, 0 },
    { "drives", "cdrom", DESCRIPTOR_LIST, NULL, 0, 0 },
    { "run", "exe", DESCRIPTOR_PATH, NULL, 0, 0 },
    { "run", "args", DESCRIPTOR_TEXT, NULL, 0, 0 },
    { "run", "cwd", DESCRIPTOR_PATH, NULL, 0, 0 },
    { "run", "setup", DESCRIPTOR_TEXT, NULL, 0, 0 },
    { "run", "on_exit", DESCRIPTOR_CHOICE, "prompt,poweroff", 0, 0 },
    { "run", "boot", DESCRIPTOR_CHOICE, "c,a", 0, 0 }
};

static const char* const k_descriptor_hard_disks[GDOX_MAX_HARD_DISKS] = { NULL, "hdd_primary_slave", NULL, "hdd_secondary_slave" };

static const char* const k_descriptor_sections[] =
{
    "geardox", "game", "machine", "sound", "input", "keys", "dos", "env", "drives", "run", "config_sys", "autoexec"
};

static const int k_descriptor_key_count = (int)(sizeof(k_descriptor_keys) / sizeof(k_descriptor_keys[0]));
static const int k_descriptor_section_count = (int)(sizeof(k_descriptor_sections) / sizeof(k_descriptor_sections[0]));

static std::string lower(const std::string& text)
{
    std::string result(text);

    for (size_t i = 0; i < result.size(); i++)
        result[i] = ((result[i] >= 'A') && (result[i] <= 'Z')) ? (char)(result[i] + 32) : result[i];

    return result;
}

static std::string upper(const std::string& text)
{
    std::string result(text);

    for (size_t i = 0; i < result.size(); i++)
        result[i] = ((result[i] >= 'a') && (result[i] <= 'z')) ? (char)(result[i] - 32) : result[i];

    return result;
}

static std::string trim(const std::string& text)
{
    size_t start = text.find_first_not_of(" \t\r\n");

    if (start == std::string::npos)
        return "";

    size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(start, end - start + 1);
}

static bool parse_number(const std::string& text, int* value)
{
    if (text.empty() || (text.size() > 9))
        return false;

    int result = 0;

    for (size_t i = 0; i < text.size(); i++)
    {
        if ((text[i] < '0') || (text[i] > '9'))
            return false;

        result = (result * 10) + (text[i] - '0');
    }

    *value = result;
    return true;
}

static bool in_choices(const char* choices, const std::string& value)
{
    std::string list(choices);
    size_t start = 0;

    while (start <= list.size())
    {
        size_t end = list.find(',', start);
        end = end == std::string::npos ? list.size() : end;

        if (list.compare(start, end - start, value) == 0)
            return true;

        start = end + 1;
    }

    return false;
}

static int choice_index(const char* choices, const std::string& value)
{
    std::string list(choices);
    size_t start = 0;
    int index = 0;

    while (start <= list.size())
    {
        size_t end = list.find(',', start);
        end = end == std::string::npos ? list.size() : end;

        if (list.compare(start, end - start, value) == 0)
            return index;

        start = end + 1;
        index++;
    }

    return -1;
}

static void split_list(const std::string& text, std::vector<std::string>* items)
{
    size_t start = 0;
    items->clear();

    while (start <= text.size())
    {
        size_t end = text.find(',', start);
        end = end == std::string::npos ? text.size() : end;
        std::string item = trim(text.substr(start, end - start));

        if (!item.empty())
            items->push_back(item);

        start = end + 1;
    }
}

static bool is_dos_character(char c)
{
    return ((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')) || ((c != 0) && (strchr("$%'-_@~`!(){}^#&", c) != NULL));
}

// C:\ for the root, or DOS names from it with \ or /: C:\GAMES\ORION. FREEDOS and TEMP are the system's
static bool check_dos_dir(const std::string& value, std::string* normalized)
{
    std::string text = upper(trim(value));
    std::replace(text.begin(), text.end(), '/', '\\');

    if ((text.compare(0, 2, "C:") != 0) || ((text.size() > 2) && (text[2] != '\\')))
        return false;

    std::string result;
    size_t start = 2;

    while (start <= text.size())
    {
        size_t end = text.find('\\', start);
        end = end == std::string::npos ? text.size() : end;
        std::string part = text.substr(start, end - start);
        start = end + 1;

        if (part.empty())
            continue;

        size_t dot = part.find('.');
        std::string base = dot == std::string::npos ? part : part.substr(0, dot);
        std::string extension = dot == std::string::npos ? std::string() : part.substr(dot + 1);

        if (base.empty() || (base.size() > 8) || (extension.size() > 3) || (extension.find('.') != std::string::npos))
            return false;

        for (size_t i = 0; i < part.size(); i++)
        {
            if ((part[i] != '.') && !is_dos_character(part[i]))
                return false;
        }

        // The folders either DOS keeps in the root of a game's disk, so a descriptor works on both (docs/27 §6.1)
        if (result.empty() && ((part == "FREEDOS") || (part == "TEMP") || (part == "DOS") || (part == "GEARDOX")))
            return false;

        result += (result.empty() ? "" : "\\") + part;
    }

    if (result.size() > 48)
        return false;

    *normalized = "C:\\" + result;
    return true;
}

static const Descriptor_Key* find_key(const std::string& section, const std::string& key)
{
    for (int i = 0; i < k_descriptor_key_count; i++)
    {
        if ((section == k_descriptor_keys[i].section) && (key == k_descriptor_keys[i].key))
            return &k_descriptor_keys[i];
    }

    return NULL;
}

static bool is_section(const std::string& section)
{
    for (int i = 0; i < k_descriptor_section_count; i++)
    {
        if (section == k_descriptor_sections[i])
            return true;
    }

    return false;
}

static int find_model(const std::string& value)
{
    for (int i = 0; i < GDOX_MACHINE_COUNT; i++)
    {
        if (k_machine_profiles[i].emulated && (value == k_machine_profiles[i].key))
            return i;
    }

    return -1;
}

static bool check_value(const Descriptor_Key& key, const std::string& value, std::string* normalized)
{
    std::string text = lower(value);
    int number = 0;
    *normalized = text;

    switch (key.type)
    {
        case DESCRIPTOR_TEXT:
        case DESCRIPTOR_PATH:
        case DESCRIPTOR_LIST:
            *normalized = value;
            return (key.type == DESCRIPTOR_TEXT) || !value.empty();
        case DESCRIPTOR_CHOICE:
            return in_choices(key.choices, text);
        case DESCRIPTOR_NUMBER:
            if (!parse_number(text, &number) || (number < key.min) || (number > key.max))
                return false;

            *normalized = std::to_string(number);
            return true;
        case DESCRIPTOR_MODEL:
            return find_model(text) >= 0;
        case DESCRIPTOR_CPU:
            for (int i = 0; parse_number(text, &number) && (i < GDOX_MACHINE_CPU_SPEED_COUNT); i++)
            {
                if (k_machine_cpu_speeds_mhz[i] == number)
                    return true;
            }

            return false;
        case DESCRIPTOR_RAM:
            for (int i = 0; parse_number(text, &number) && (i < GDOX_MACHINE_RAM_SIZE_COUNT); i++)
            {
                if (k_machine_ram_sizes_mb[i] == number)
                    return true;
            }

            return false;
        case DESCRIPTOR_SOUND:
            for (int i = 0; i < GDOX_SOUND_CARD_COUNT; i++)
            {
                if (text == k_sound_cards[i].id)
                    return true;
            }

            return false;
        case DESCRIPTOR_VIDEO:
            for (int i = 0; i < GDOX_VIDEO_CARD_COUNT; i++)
            {
                if (text == k_video_cards[i].id)
                    return true;
            }

            return false;
        case DESCRIPTOR_MOUSE:
            for (int i = 0; i < GDOX_MOUSE_TYPE_COUNT; i++)
            {
                if (text == k_mice[i].id)
                    return true;
            }

            return false;
        case DESCRIPTOR_JOYSTICK:
            for (int i = 0; i < GDOX_JOYSTICK_TYPE_COUNT; i++)
            {
                if (text == k_joysticks[i].id)
                    return true;
            }

            return false;
        case DESCRIPTOR_KEYBOARD:
            for (int i = 0; i < k_keyboard_layout_count; i++)
            {
                if (text == k_keyboard_layouts[i].id)
                    return true;
            }

            return false;
        case DESCRIPTOR_CD_SPEED:
            for (int i = 0; i < GDOX_CDROM_SPEED_COUNT; i++)
            {
                if (text == k_cdrom_speeds[i].id)
                    return true;
            }

            return false;
        case DESCRIPTOR_FRAME:
        {
            if ((text == "auto") || (text == "none"))
                return true;

            char* end = NULL;
            long segment = strtol(text.c_str(), &end, 16);

            if ((text.size() != 4) || (end == NULL) || (*end != 0) || (segment < 0xC000) || (segment > 0xE000) ||
                ((segment & 0x3FF) != 0))
                return false;

            *normalized = upper(text);
            return true;
        }
        case DESCRIPTOR_DOS_DIR:
            return check_dos_dir(value, normalized);
        case DESCRIPTOR_DOS_VERSION:
            return (text.size() == 4) && (text[0] >= '1') && (text[0] <= '9') && (text[1] == '.') &&
                (text[2] >= '0') && (text[2] <= '9') && (text[3] >= '0') && (text[3] <= '9');
        default:
            return false;
    }
}

GameDescriptor::GameDescriptor()
{
    Clear();
}

GameDescriptor::~GameDescriptor()
{
}

void GameDescriptor::Clear()
{
    m_path.clear();
    m_values.clear();
    m_env_order.clear();
    m_keys_order.clear();
    m_config_sys.clear();
    m_autoexec.clear();
    m_has_config_sys = false;
    m_has_autoexec = false;
    m_error.clear();
    m_error_line = 0;
    m_warnings.clear();
    Build();
}

bool GameDescriptor::Parse(const char* path, const char* text, size_t size)
{
    Clear();
    m_path = IsValidPointer(path) ? path : "";

    if (size > GDOX_GAME_DESCRIPTOR_MAX_SIZE)
        return Fail(0, "The descriptor is larger than %d KB", GDOX_GAME_DESCRIPTOR_MAX_SIZE / 1024);

    std::string data(text, size);

    if ((data.size() >= 3) && ((u8)data[0] == 0xEF) && ((u8)data[1] == 0xBB) && ((u8)data[2] == 0xBF))
        data.erase(0, 3);

    std::string section;
    size_t start = 0;
    int number = 0;

    while (start < data.size())
    {
        size_t end = data.find('\n', start);
        end = end == std::string::npos ? data.size() : end;
        std::string line = data.substr(start, end - start);
        start = end + 1;
        number++;

        if (!line.empty() && (line[line.size() - 1] == '\r'))
            line.resize(line.size() - 1);

        std::string trimmed = trim(line);

        if (!trimmed.empty() && (trimmed[0] == '[') && (trimmed[trimmed.size() - 1] == ']'))
        {
            section = lower(trim(trimmed.substr(1, trimmed.size() - 2)));

            if (!is_section(section))
                Warn(number, "Unknown section [%s], ignored", section.c_str());
            else if (section == "config_sys")
                m_has_config_sys = true;
            else if (section == "autoexec")
                m_has_autoexec = true;

            continue;
        }

        if ((section == "config_sys") || (section == "autoexec"))
        {
            if (!trimmed.empty())
                (section == "config_sys" ? m_config_sys : m_autoexec).push_back(trimmed);

            continue;
        }

        if (trimmed.empty() || (trimmed[0] == ';') || (trimmed[0] == '#'))
            continue;

        if (!ParseLine(section, trimmed, number))
            return false;
    }

    if (!Has("geardox", "version"))
        return Fail(0, "No version in [geardox]: this isn't a Geardox game descriptor");

    int version = 0;
    parse_number(Get("geardox", "version"), &version);

    if (version > GDOX_GAME_DESCRIPTOR_VERSION)
        return Fail(0, "Made for a newer Geardox: descriptor version %d", version);

    Build();
    return true;
}

bool GameDescriptor::Load(const char* path)
{
    MediaFile* file = MediaFile::OpenFile(path);

    if (!IsValidPointer(file))
    {
        Clear();
        m_path = IsValidPointer(path) ? path : "";
        return Fail(0, "Unable to open %s", m_path.c_str());
    }

    s64 size = file->GetSize();

    if ((size < 0) || (size > GDOX_GAME_DESCRIPTOR_MAX_SIZE))
    {
        SafeDelete(file);
        Clear();
        m_path = path;
        return Fail(0, "The descriptor is larger than %d KB", GDOX_GAME_DESCRIPTOR_MAX_SIZE / 1024);
    }

    std::vector<char> data((size_t)size + 1, 0);
    bool read = (size == 0) || file->ReadAt(0, data.data(), (size_t)size);
    SafeDelete(file);

    if (!read)
    {
        Clear();
        m_path = path;
        return Fail(0, "Unable to read %s", path);
    }

    return Parse(path, data.data(), (size_t)size);
}

bool GameDescriptor::ParseLine(const std::string& section, const std::string& line, int number)
{
    size_t equals = line.find('=');

    if (equals == std::string::npos)
        return Fail(number, "Expected key = value");

    std::string key = lower(trim(line.substr(0, equals)));
    std::string value = trim(line.substr(equals + 1));

    if (section.empty())
        return Fail(number, "%s is outside of any section", key.c_str());

    if (!is_section(section))
        return true;

    if (section == "env")
    {
        std::string name = upper(key);

        if (name.empty() || (name.find_first_of(" \t=") != std::string::npos))
            return Fail(number, "Bad variable name in [env]");

        std::string id = "env." + name;

        if (m_values.count(id) != 0)
            return Fail(number, "%s is set twice in [env]", name.c_str());

        m_values[id] = value;
        m_env_order.push_back(name);
        return true;
    }

    if (section == "keys")
    {
        u16 usage = 0;
        GDOX_Key_Map_Entry entry;
        std::string error;

        if (!KeyMap::ParseEntry(key, value, &usage, &entry, &error))
        {
            Warn(number, "%s in [keys], ignored", error.c_str());
            return true;
        }

        std::string host = KeyMap::GetHostKeyName(usage);

        if (m_values.count("keys." + host) != 0)
            return Fail(number, "%s is set twice in [keys]", host.c_str());

        m_values["keys." + host] = KeyMap::FormatTargets(entry);
        m_keys_order.push_back(host);
        return true;
    }

    const Descriptor_Key* found = find_key(section, key);

    if (!IsValidPointer(found))
    {
        Warn(number, "Unknown key %s in [%s], ignored", key.c_str(), section.c_str());
        return true;
    }

    std::string id = section + "." + key;

    if (m_values.count(id) != 0)
        return Fail(number, "%s is set twice in [%s]", key.c_str(), section.c_str());

    std::string normalized;

    if (!check_value(*found, value, &normalized))
        return Fail(number, "Bad value for %s in [%s]: %s%s", key.c_str(), section.c_str(), value.c_str(),
            found->type == DESCRIPTOR_DOS_DIR ? ", expected a folder of C: such as C:\\GAME" : "");

    m_values[id] = normalized;
    return true;
}

void GameDescriptor::Merge(const GameDescriptor& overrides)
{
    for (std::map<std::string, std::string>::const_iterator it = overrides.m_values.begin();
        it != overrides.m_values.end(); ++it)
    {
        if (it->first == "geardox.version")
            continue;

        if ((it->first.compare(0, 4, "env.") == 0) && (m_values.count(it->first) == 0))
            m_env_order.push_back(it->first.substr(4));

        if ((it->first.compare(0, 5, "keys.") == 0) && (m_values.count(it->first) == 0))
            m_keys_order.push_back(it->first.substr(5));

        m_values[it->first] = it->second;
    }

    if (overrides.m_has_config_sys)
    {
        m_config_sys = overrides.m_config_sys;
        m_has_config_sys = true;
    }

    if (overrides.m_has_autoexec)
    {
        m_autoexec = overrides.m_autoexec;
        m_has_autoexec = true;
    }

    Build();
}

bool GameDescriptor::Set(const char* section, const char* key, const char* value)
{
    const Descriptor_Key* found = find_key(section, key);
    std::string normalized;

    if (!IsValidPointer(found) || !check_value(*found, value, &normalized))
        return false;

    m_values[std::string(section) + "." + key] = normalized;
    Build();
    return true;
}

void GameDescriptor::SetKeys(const std::string& text)
{
    for (size_t i = 0; i < m_keys_order.size(); i++)
        m_values.erase("keys." + m_keys_order[i]);

    m_keys_order.clear();
    size_t start = 0;

    while (start < text.size())
    {
        size_t end = text.find('\n', start);
        end = end == std::string::npos ? text.size() : end;
        std::string line = trim(text.substr(start, end - start));
        start = end + 1;
        size_t equals = line.find('=');
        u16 usage = 0;
        GDOX_Key_Map_Entry entry;
        std::string error;

        if ((equals == std::string::npos) ||
            !KeyMap::ParseEntry(line.substr(0, equals), line.substr(equals + 1), &usage, &entry, &error))
            continue;

        std::string host = KeyMap::GetHostKeyName(usage);

        if (m_values.count("keys." + host) == 0)
            m_keys_order.push_back(host);

        m_values["keys." + host] = KeyMap::FormatTargets(entry);
    }

    Build();
}

void GameDescriptor::Unset(const char* section, const char* key)
{
    m_values.erase(std::string(section) + "." + key);
    Build();
}

bool GameDescriptor::Has(const char* section, const char* key) const
{
    return m_values.count(std::string(section) + "." + key) != 0;
}

std::string GameDescriptor::Get(const char* section, const char* key) const
{
    std::map<std::string, std::string>::const_iterator it = m_values.find(std::string(section) + "." + key);
    return it == m_values.end() ? std::string() : it->second;
}

bool GameDescriptor::IsEmpty() const
{
    for (std::map<std::string, std::string>::const_iterator it = m_values.begin(); it != m_values.end(); ++it)
    {
        if (it->first != "geardox.version")
            return false;
    }

    return !m_has_config_sys && !m_has_autoexec;
}

std::string GameDescriptor::Write() const
{
    std::string out;
    std::string written;

    for (int s = 0; s < k_descriptor_section_count; s++)
    {
        std::string section = k_descriptor_sections[s];
        std::string body;

        if (section == "geardox")
            body += "version = " + std::to_string(GDOX_GAME_DESCRIPTOR_VERSION) + "\n";

        for (int k = 0; k < k_descriptor_key_count; k++)
        {
            const Descriptor_Key& key = k_descriptor_keys[k];

            if ((section != key.section) || ((section == "geardox") && (std::string(key.key) == "version")))
                continue;

            std::map<std::string, std::string>::const_iterator it = m_values.find(section + "." + key.key);

            if (it != m_values.end())
                body += std::string(key.key) + " = " + it->second + "\n";
        }

        if (section == "env")
        {
            for (size_t i = 0; i < m_env_order.size(); i++)
            {
                std::map<std::string, std::string>::const_iterator it = m_values.find("env." + m_env_order[i]);

                if (it != m_values.end())
                    body += m_env_order[i] + " = " + it->second + "\n";
            }
        }

        if (section == "keys")
        {
            for (size_t i = 0; i < m_keys_order.size(); i++)
            {
                std::map<std::string, std::string>::const_iterator it = m_values.find("keys." + m_keys_order[i]);

                if (it != m_values.end())
                    body += m_keys_order[i] + " = " + it->second + "\n";
            }
        }

        bool raw = (section == "config_sys") || (section == "autoexec");
        const std::vector<std::string>& lines = section == "config_sys" ? m_config_sys : m_autoexec;

        for (size_t i = 0; raw && (i < lines.size()); i++)
            body += lines[i] + "\n";

        if (body.empty() && !(raw && (section == "config_sys" ? m_has_config_sys : m_has_autoexec)))
            continue;

        out += (out.empty() ? "" : "\n") + std::string("[") + section + "]\n" + body;
    }

    return out;
}

bool GameDescriptor::WriteFile(const char* path) const
{
    std::string text = Write();
    std::string temporary = std::string(path) + ".tmp";
    MediaFile* file = MediaFile::OpenNewFile(temporary.c_str());

    if (!IsValidPointer(file))
        return false;

    bool ok = file->WriteAt(0, text.data(), text.size()) && file->Flush();
    SafeDelete(file);
    ok = ok && MediaFile::RenameFile(temporary.c_str(), path);

    if (!ok)
        MediaFile::RemoveFile(temporary.c_str());

    return ok;
}

const char* GameDescriptor::GetError() const
{
    return m_error.c_str();
}

int GameDescriptor::GetErrorLine() const
{
    return m_error_line;
}

int GameDescriptor::GetWarningCount() const
{
    return (int)m_warnings.size();
}

const char* GameDescriptor::GetWarning(int index) const
{
    return (index >= 0) && (index < (int)m_warnings.size()) ? m_warnings[index].c_str() : "";
}

void GameDescriptor::GetMachineSettings(GDOX_Machine_Settings* settings) const
{
    clear_machine_settings(settings);
    int number = 0;

    if (Has("machine", "model"))
        settings->model = find_model(Get("machine", "model"));

    if (parse_number(Get("machine", "cpu_mhz"), &number))
        settings->cpu_mhz = number;

    if (Has("machine", "turbo"))
        settings->speed_mode = Get("machine", "turbo") == "on" ? 1 : 0;

    if (parse_number(Get("machine", "ram_mb"), &number))
        settings->ram_mb = number;

    if (Has("machine", "fpu"))
        settings->fpu = Get("machine", "fpu") == "none" ? 0 : 1;

    static const char* const floppy_keys[2] = { "floppy_a_type", "floppy_b_type" };

    for (int i = 0; i < 2; i++)
    {
        if (Has("machine", floppy_keys[i]))
            settings->floppy_type[i] = choice_index("360k,1200k,720k,1440k,2880k", Get("machine", floppy_keys[i])) + 1;
    }

    for (int i = 0; Has("machine", "video") && (i < GDOX_VIDEO_CARD_COUNT); i++)
    {
        if (Get("machine", "video") == k_video_cards[i].id)
            settings->video_card = i;
    }

    for (int i = 0; Has("sound", "card") && (i < GDOX_SOUND_CARD_COUNT); i++)
    {
        if (Get("sound", "card") == k_sound_cards[i].id)
            settings->sound_card = i;
    }

    if (parse_number(Get("sound", "irq"), &number))
        settings->sound_irq = number;

    if (parse_number(Get("sound", "dma"), &number))
        settings->sound_dma = number;

    if (Has("sound", "mpu"))
        settings->mpu = Get("sound", "mpu") == "off" ? 0 : 1;

    for (int i = 0; Has("input", "mouse") && (i < GDOX_MOUSE_TYPE_COUNT); i++)
    {
        if (Get("input", "mouse") == k_mice[i].id)
            settings->mouse = i;
    }

    for (int i = 0; Has("machine", "cdrom_speed") && (i < GDOX_CDROM_SPEED_COUNT); i++)
    {
        if (Get("machine", "cdrom_speed") == k_cdrom_speeds[i].id)
            settings->cdrom_speed = i;
    }

    for (int i = 0; Has("machine", "drive_timing") && (i < GDOX_DRIVE_TIMING_COUNT); i++)
    {
        if (Get("machine", "drive_timing") == k_drive_timings[i].id)
            settings->drive_timing = i;
    }

    if (Has("drives", "cdrom"))
        settings->cdrom = GDOX_CDROM_SECONDARY_MASTER;
}

const GDOX_Game_Info& GameDescriptor::GetInfo() const
{
    return m_info;
}

bool GameDescriptor::IsDescriptor(const char* path)
{
    return IsValidPointer(path) && ends_with_no_case(path, ".gdox");
}

const char* GameDescriptor::GetHardDiskKey(int id)
{
    return (id >= 0) && (id < GDOX_MAX_HARD_DISKS) ? k_descriptor_hard_disks[id] : NULL;
}

// JEMM makes EMS from extended memory, less the HMA, its DMA buffer, the UMBs and itself (JEMM Readme.txt §3), and
// R14's hole takes 15-16 MB with a Cirrus card. MS-DOS's EMM386 leaves 1472K to 1488K less EMS free than RAM, measured
// with MEM (docs/27 §6.3). The smallest size of the model that leaves the game its EMS
int GameDescriptor::GetEMSRAM(int ems_kb, int model, bool cirrus, int system)
{
    int reserved_kb = system == GDOX_GAME_SYSTEM_MSDOS ? 1488 : 1024 + 512;
    const u8* sizes = k_machine_profiles[(model >= 0) && (model < GDOX_MACHINE_COUNT) ? model : k_machine_default_model].ram_sizes_mb;
    int size = 0;

    while (sizes[size + 1] != 0)
    {
        int hole = (cirrus && (sizes[size] >= 16)) ? 1024 : 0;

        if ((sizes[size] * 1024) - hole - reserved_kb >= ems_kb)
            break;

        size++;
    }

    return sizes[size];
}

bool GameDescriptor::Fail(int line, const char* format, ...)
{
    char text[512];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    m_error_line = line;
    m_error = line > 0 ? "Line " + std::to_string(line) + ": " + text : std::string(text);
    m_values.clear();
    Build();
    return false;
}

void GameDescriptor::Warn(int line, const char* format, ...)
{
    char text[512];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    m_warnings.push_back("Line " + std::to_string(line) + ": " + text);
}

void GameDescriptor::Build()
{
    GDOX_Game_Info& info = m_info;
    int number = 0;

    info.name = Get("game", "name");

    if (info.name.empty() && !m_path.empty())
    {
        size_t slash = m_path.find_last_of("/\\");
        info.name = slash == std::string::npos ? m_path : m_path.substr(slash + 1);

        if (ends_with_no_case(info.name.c_str(), ".gdox"))
            info.name.resize(info.name.size() - 5);
    }

    info.ems_kb = parse_number(Get("dos", "ems_kb"), &number) ? number : 0;
    info.memory = Has("dos", "memory") ? choice_index("none,xms,umb,ems", Get("dos", "memory")) :
        info.ems_kb > 0 ? (int)GDOX_GAME_MEMORY_EMS : (int)GDOX_GAME_MEMORY_XMS;

    std::string frame = Get("dos", "ems_frame");
    info.ems_frame = (frame.empty() || (frame == "auto")) ? -1 : frame == "none" ? 0 : (int)strtol(frame.c_str(), NULL, 16);
    info.files = parse_number(Get("dos", "files"), &number) ? number : 40;
    info.buffers = parse_number(Get("dos", "buffers"), &number) ? number : 20;
    info.dos_version = Get("dos", "version");
    info.system = Has("dos", "system") ? choice_index("freedos,msdos", Get("dos", "system")) : -1;
    info.system_hidden = Get("dos", "system_files") == "hidden";
    info.cache = Get("dos", "cache") == "on";
    info.keyboard = Get("dos", "keyboard");
    info.source = Get("game", "source");
    std::string folder = Get("game", "folder");
    info.folder = folder.size() > 3 ? folder.substr(3) : std::string();
    info.free_mb = parse_number(Get("game", "free_mb"), &number) ? number : 64;

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
        info.hard_disks[i] = IsValidPointer(k_descriptor_hard_disks[i]) ? Get("drives", k_descriptor_hard_disks[i]) : "";

    split_list(Get("drives", "floppy_a"), &info.floppies[0]);
    split_list(Get("drives", "floppy_b"), &info.floppies[1]);
    split_list(Get("drives", "cdrom"), &info.cds);
    info.exe = Get("run", "exe");
    info.args = Get("run", "args");
    info.cwd = Get("run", "cwd");
    info.setup = Get("run", "setup");
    info.exit = Get("run", "on_exit") == "poweroff" ? GDOX_GAME_EXIT_POWEROFF : GDOX_GAME_EXIT_PROMPT;
    info.boot_floppy = Get("run", "boot") == "a";
    info.joystick = -1;

    for (int i = 0; Has("input", "joystick") && (i < GDOX_JOYSTICK_TYPE_COUNT); i++)
    {
        if (Get("input", "joystick") == k_joysticks[i].id)
            info.joystick = i;
    }

    info.midi = -1;

    for (int i = 0; Has("sound", "midi") && (i < GDOX_MIDI_SYNTH_COUNT); i++)
    {
        if (Get("sound", "midi") == k_midi_synths[i].id)
            info.midi = i;
    }

    info.midi_port = Get("sound", "midi_port");

    info.env.clear();

    for (size_t i = 0; i < m_env_order.size(); i++)
    {
        GDOX_Game_Env env;
        env.name = m_env_order[i];
        env.value = Get("env", m_env_order[i].c_str());
        info.env.push_back(env);
    }

    info.keys.clear();

    for (size_t i = 0; i < m_keys_order.size(); i++)
        info.keys += m_keys_order[i] + " = " + Get("keys", m_keys_order[i].c_str()) + "\n";

    info.config_sys = m_config_sys;
    info.autoexec = m_autoexec;
}
