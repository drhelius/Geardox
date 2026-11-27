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
#include "game_disk.h"
#include "content.h"
#include "dos_fs.h"
#include "freedos.h"
#include "glide_import.h"
#include "../media/game_database.h"
#include "../media/media_file.h"
#include "../media/media_zip.h"
#include "../media/crc.h"
#include "../audio/ultrasound.h"
#include "../system/machine_profiles.h"
#include "../common/process_utils.h"

static const char k_game_disk_database[] = "db:";
static const char k_game_disk_program[] = "exe:";
static const char k_game_disk_prompt[] = "prompt:";
static const char k_game_disk_saves[] = "Saves";
static const int k_game_disk_machines = 8;
static const char k_game_disk_c[] = "C";
static const char k_game_disk_freedos[] = "FreeDOS";
static const char k_game_disk_msdos[] = "MS-DOS";
static const char k_game_disk_settings[] = "Settings.ini";
static const char k_game_disk_keys[] = "Keys.ini";
static const char k_game_disk_base[] = ".base";
static const char k_game_disk_working[] = "C.hdd";
static const char k_game_disk_line_blanks[] = " \t\r\x1A";
static const u8 k_game_disk_layer_system = 0;
static const u8 k_game_disk_layer_game = 1;
static const u8 k_game_disk_layer_c = 2;
static bool game_disk_database = true;

static const char* const k_game_disk_freedos_reserved[] =
{
    "KERNEL.SYS", "FDCONFIG.SYS", k_dos_root_freedos, k_dos_root_temp, k_dos_config_sys, k_dos_autoexec_bat
};

static const char* const k_game_disk_msdos_reserved[] =
{
    k_dos_root_dos, k_dos_root_geardox, k_dos_root_temp, k_dos_config_sys, k_dos_autoexec_bat
};

static std::string collapse(const std::string& path)
{
    std::vector<std::string> parts;
    size_t start = 0;

    while (start <= path.size())
    {
        size_t end = path.find('/', start);

        if (end == std::string::npos)
            end = path.size();

        std::string part = path.substr(start, end - start);
        start = end + 1;

        if (part == "..")
        {
            if (parts.empty())
                return std::string();

            parts.pop_back();
        }
        else if (!part.empty() && (part != "."))
            parts.push_back(part);
    }

    std::string result;

    for (size_t i = 0; i < parts.size(); i++)
        result += (i > 0 ? "/" : "") + parts[i];

    return result;
}

static std::string backslashes(const std::string& path)
{
    std::string result = path_back_slashes(path);

    while (!result.empty() && (result[0] == '\\'))
        result.erase(0, 1);

    return result;
}

static std::string release_fragment(const char* settings, bool sound)
{
    std::string fragment = "[geardox]\nversion = 1\n";
    bool skip = false;

    while (*settings != 0)
    {
        const char* end = strchr(settings, '\n');
        size_t length = IsValidPointer(end) ? (size_t)(end - settings) + 1 : strlen(settings);

        if (settings[0] == '[')
            skip = !sound && strncmp(settings, "[sound]", 7) == 0;

        if (!skip)
            fragment.append(settings, length);

        settings += length;
    }

    return fragment;
}

static bool program_path(const std::string& folder, const std::string& command, std::string* path)
{
    std::string program = path_forward_slashes(command);
    size_t space = program.find(' ');

    if (space != std::string::npos)
        program = program.substr(0, space);

    if ((program.size() > 1) && (program[1] == ':') && (to_upper_ascii(program[0]) != 'C'))
        return false;

    *path = (program.size() > 1) && (program[1] == ':') ? collapse(program.substr(2)) :
        collapse(path_forward_slashes(folder) + "/" + program);
    return true;
}

static std::string program_name(const std::string& command)
{
    std::string program = path_forward_slashes(command.substr(0, command.find(' ')));
    size_t slash = program.find_last_of('/');
    return slash == std::string::npos ? program : program.substr(slash + 1);
}

static bool has_game_overlay(FatImage* image, const std::string& path)
{
    size_t slash = path.find_last_of('/');
    std::string folder = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
    std::string overlay;

    return (image->GetFile((folder + k_dos_glide_overlay).c_str(), &overlay) && GlideImport::IsOverlay(overlay)) ||
        (image->GetFile(k_dos_glide_overlay, &overlay) && GlideImport::IsOverlay(overlay));
}

static bool sort_found(const std::pair<std::string, GameDisk_Found>& a, const std::pair<std::string, GameDisk_Found>& b)
{
    return a.first < b.first;
}

static std::string find_host(const std::string& base, const std::string& relative)
{
    std::string rest = path_forward_slashes(relative);

    while (!rest.empty() && (rest[0] == '/'))
        rest.erase(0, 1);

    if (rest.empty())
        return base;

    std::string path = path_join(base, rest);
    std::string found;

    if (MediaFile::Exists(path.c_str()) || !MediaFile::FindPathIgnoringCase(base, rest, &found))
        return path;

    return found;
}

static void split_lines(const std::string& text, std::vector<std::string>* lines)
{
    std::vector<std::string> parts;
    split_string(text, '\n', &parts);
    lines->clear();

    for (size_t i = 0; i < parts.size(); i++)
        lines->push_back(trim_chars(parts[i], "\r\x1A"));

    while (!lines->empty() && trim_chars(lines->back(), k_game_disk_line_blanks).empty())
        lines->pop_back();
}

static std::string line_key(const std::string& line)
{
    return to_upper_ascii(trim_chars(line, k_game_disk_line_blanks));
}

static int find_line(const std::vector<std::string>& lines, const std::string& line)
{
    std::string wanted = line_key(line);

    for (size_t i = 0; i < lines.size(); i++)
    {
        if (line_key(lines[i]) == wanted)
            return (int)i;
    }

    return -1;
}

static void match_lines(const std::vector<std::string>& a, const std::vector<std::string>& b, std::vector<int>* matched)
{
    std::vector<std::string> keys_a(a.size());
    std::vector<std::string> keys_b(b.size());
    size_t width = b.size() + 1;
    std::vector<u32> common((a.size() + 1) * width, 0);

    for (size_t i = 0; i < a.size(); i++)
        keys_a[i] = line_key(a[i]);

    for (size_t j = 0; j < b.size(); j++)
        keys_b[j] = line_key(b[j]);

    for (size_t i = a.size(); i > 0; i--)
    {
        for (size_t j = b.size(); j > 0; j--)
        {
            common[(i - 1) * width + (j - 1)] = keys_a[i - 1] == keys_b[j - 1] ? common[i * width + j] + 1 :
                MAX(common[i * width + (j - 1)], common[(i - 1) * width + j]);
        }
    }

    matched->assign(a.size(), -1);
    size_t i = 0;
    size_t j = 0;

    while ((i < a.size()) && (j < b.size()))
    {
        if (keys_a[i] == keys_b[j])
            (*matched)[i++] = (int)j++;
        else if (common[(i + 1) * width + j] >= common[i * width + j + 1])
            i++;
        else
            j++;
    }
}

static size_t find_own_lines(const std::vector<std::string>& lines, const char* own)
{
    for (size_t i = 0; i < lines.size(); i++)
    {
        if (starts_with_no_case(trim_chars(lines[i], k_game_disk_line_blanks).c_str(), own))
            return i;
    }

    return lines.size();
}

static std::string merge_lines(const std::string& old_base, const std::string& user, const std::string& base,
    const char* own)
{
    std::vector<std::string> before;
    std::vector<std::string> mine;
    std::vector<std::string> next;
    std::vector<std::string> result;
    std::vector<int> matched;
    split_lines(old_base, &before);
    split_lines(user, &mine);
    split_lines(base, &next);
    match_lines(before, next, &matched);
    size_t old_line = 0;
    size_t new_line = 0;

    while ((old_line < before.size()) || (new_line < next.size()))
    {
        size_t old_end = old_line;

        while ((old_end < before.size()) && (matched[old_end] < 0))
            old_end++;

        size_t new_end = old_end < before.size() ? (size_t)matched[old_end] : next.size();
        bool yours = false;

        for (size_t i = old_line; i < old_end; i++)
            yours = yours || (!line_key(before[i]).empty() && (find_line(mine, before[i]) < 0));

        for (size_t i = old_line; yours && (i < old_end); i++)
        {
            if (find_line(mine, before[i]) >= 0)
                result.push_back(before[i]);
        }

        for (size_t i = new_line; !yours && (i < new_end); i++)
            result.push_back(next[i]);

        if (old_end < before.size())
            result.push_back(next[new_end]);

        old_line = old_end + 1;
        new_line = new_end + 1;
    }

    for (size_t i = 0; i < before.size(); i++)
    {
        int found = find_line(mine, before[i]) < 0 ? find_line(result, before[i]) : -1;

        if (found >= 0)
            result.erase(result.begin() + found);
    }

    for (size_t i = 0; i < mine.size(); i++)
    {
        if ((find_line(before, mine[i]) >= 0) || (!line_key(mine[i]).empty() && (find_line(result, mine[i]) >= 0)))
            continue;

        int anchor = i > 0 ? find_line(result, mine[i - 1]) : -1;
        size_t position = anchor >= 0 ? (size_t)anchor + 1 : find_own_lines(result, own);
        result.insert(result.begin() + position, mine[i]);
    }

    std::string text;

    for (size_t i = 0; i < result.size(); i++)
        text += result[i] + "\r\n";

    return text;
}

GameDisk::GameDisk()
{
    m_open = false;
    m_source_zip = false;
    m_source_image = false;
    m_database = false;
    m_release_settings = false;
    m_withheld_settings = false;
    m_write_saves = false;
    m_folder_read_only = false;
    m_machine_slot = 1;
    m_startup_edits = false;
    m_has_machine = false;
    m_msdos_loaded = false;
    m_msdos.version = 0;
    m_msdos.cd_extension = false;
    m_msdos.cache = false;
    m_msdos.crc = 0;
    m_ultrasnd_loaded = false;
    m_glide_loaded = false;
    m_voodoo_release = false;
    m_program = -1;
    m_built_program = GDOX_GAME_PROGRAM_GAME;
    m_voodoo_program = false;
    m_voodoo_driver = false;
    m_voodoo_exe = false;
    m_voodoo_note.kind = GAME_DISK_MESSAGE_VOODOO;
    m_ultrasnd.patches = 0;
    m_ultrasnd.crc = 0;
    m_left_out = 0;
    memset(&m_machine, 0, sizeof(m_machine));
    m_recovery = FolderDisk_Result();
    InitPointer(m_disk);

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
        InitPointer(m_hard_disk_sources[i]);
}

GameDisk::~GameDisk()
{
    Close();
}

bool GameDisk::Find(const char* path, std::vector<GameDisk_Found>* found)
{
    found->clear();

    if (!IsValidPointer(path) || (path[0] == 0))
        return false;

    std::string source = trim_trailing_separators(path);

    if (GameDescriptor::IsDescriptor(source.c_str()))
    {
        GameDisk_Found item = GameDisk_Found();
        item.path = source;
        found->push_back(item);
        return true;
    }

    if (IsDosProgram(source.c_str()) && MediaFile::Exists(source.c_str()) && !MediaFile::IsDirectory(source.c_str()))
    {
        std::string folder = path_directory(source);

        if (FindGames(folder, false, found))
            return true;

        GameDisk_Found item = GameDisk_Found();
        item.path = folder;
        item.inner = std::string(k_game_disk_program) + path_filename(source);
        found->push_back(item);
        return true;
    }

    return FindGames(source, true, found);
}

bool GameDisk::FindGames(const std::string& source, bool prompt, std::vector<GameDisk_Found>* found)
{
    std::vector<std::pair<std::string, GameDisk_Found> > sorted;
    std::vector<std::string> folders;
    std::vector<std::string> files;
    found->clear();

    if (ends_with_no_case(source.c_str(), ".zip"))
    {
        MediaZip zip;

        if (!zip.Open(source.c_str()))
            return false;

        for (mz_uint i = 0; i < mz_zip_reader_get_num_files(zip.GetArchive()); i++)
        {
            mz_zip_archive_file_stat stat;

            if (!mz_zip_reader_file_stat(zip.GetArchive(), i, &stat) || stat.m_is_directory)
                continue;

            std::string name = path_forward_slashes(stat.m_filename);

            if ((name.find('/') == std::string::npos) && GameDescriptor::IsDescriptor(name.c_str()))
            {
                GameDisk_Found item = GameDisk_Found();
                item.path = source;
                item.inner = name;
                sorted.push_back(std::make_pair(to_upper_ascii(name), item));
            }
        }
    }
    else if (MediaFile::ListDirectory(source.c_str(), folders, files))
    {
        for (size_t i = 0; i < files.size(); i++)
        {
            if ((files[i][0] == '.') || !GameDescriptor::IsDescriptor(files[i].c_str()))
                continue;

            GameDisk_Found item = GameDisk_Found();
            item.path = path_join(source, files[i]);
            sorted.push_back(std::make_pair(to_upper_ascii(files[i]), item));
        }
    }

    if (sorted.empty() && game_disk_database)
    {
        std::vector<GameDatabase_Match> matches;
        GameDatabase::Find(source.c_str(), &matches);

        for (size_t i = 0; i < matches.size(); i++)
        {
            GameDisk_Found item = GameDisk_Found();
            item.path = source;
            item.inner = std::string(k_game_disk_database) + GameDatabase::GetId(matches[i].entry);
            item.matched = true;
            item.match = matches[i];
            bool known = false;

            for (size_t j = 0; j < sorted.size(); j++)
                known = known || (sorted[j].second.inner == item.inner);

            if (!known)
                sorted.push_back(std::make_pair(to_upper_ascii(GameDatabase::GetName(matches[i].entry)), item));
        }
    }

    std::sort(sorted.begin(), sorted.end(), sort_found);

    for (size_t i = 0; i < sorted.size(); i++)
        found->push_back(sorted[i].second);

    if (found->empty() && prompt && (Content::Classify(source.c_str()) == GDOX_CONTENT_HARD_DISK_SOURCE))
    {
        GameDisk_Found item = GameDisk_Found();
        item.path = source;
        item.inner = k_game_disk_prompt;
        found->push_back(item);
    }

    return !found->empty();
}

bool GameDisk::IsDosProgram(const char* path)
{
    return ends_with_no_case(path, ".exe") || ends_with_no_case(path, ".com") || ends_with_no_case(path, ".bat");
}

void GameDisk::SetDatabaseEnabled(bool enabled)
{
    game_disk_database = enabled;
}

bool GameDisk::IsDatabaseEnabled()
{
    return game_disk_database;
}

std::string GameDisk::GetFoundName(const GameDisk_Found& found)
{
    if (found.inner.compare(0, strlen(k_game_disk_database), k_game_disk_database) == 0)
    {
        const char* name = GameDatabase::GetName(GameDatabase::FindEntry(found.inner.c_str() +
            strlen(k_game_disk_database)));

        if (IsValidPointer(name))
            return name;
    }

    if (found.inner.compare(0, strlen(k_game_disk_program), k_game_disk_program) == 0)
        return found.inner.substr(strlen(k_game_disk_program));

    if (found.inner == k_game_disk_prompt)
        return path_strip_extension(path_filename(found.path), ".zip");

    return found.inner.empty() ? path_filename(found.path) : found.inner;
}

std::string GameDisk::GetDosFolderName(const std::string& name)
{
    std::string folder;

    for (size_t i = 0; (i < name.size()) && (folder.size() < 8); i++)
    {
        char c = name[i];

        if (((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')))
            folder += to_upper_ascii(c);
    }

    if (folder.empty() || (folder == k_dos_root_freedos) || (folder == k_dos_root_temp) || (folder == k_dos_root_dos) ||
        (folder == k_dos_root_geardox))
        folder = "GAME";

    return folder;
}

bool GameDisk::OpenFromDatabase(const GameDisk_Found& found)
{
    std::string id = found.inner.substr(strlen(k_game_disk_database));
    std::vector<GameDatabase_Match> matches;

    if (found.matched)
        matches.push_back(found.match);
    else
        GameDatabase::Find(found.path.c_str(), &matches);

    int chosen = -1;

    for (size_t i = 0; (i < matches.size()) && (chosen < 0); i++)
    {
        if (id == GameDatabase::GetId(matches[i].entry))
            chosen = (int)i;
    }

    if (chosen < 0)
        return Fail("%s isn't recognised in %s any more", id.c_str(), path_filename(found.path).c_str());

    const GameDatabase_Match& match = matches[chosen];
    const char* text = GameDatabase::GetDescriptor(match.entry);
    std::string name = path_join(found.path, id + ".gdox");

    if (!m_base.Parse(name.c_str(), text, strlen(text)))
        return Fail("%s: %s", name.c_str(), m_base.GetError());

    const char* settings = GameDatabase::GetSettings(match.release);
    m_release_settings = IsValidPointer(settings) && match.preset;
    m_withheld_settings = IsValidPointer(settings) && !match.preset;

    if (IsValidPointer(settings))
    {
        GameDescriptor release;
        std::string fragment = release_fragment(settings, match.preset);

        if (!release.Parse(name.c_str(), fragment.data(), fragment.size()))
            return Fail("%s: %s", name.c_str(), release.GetError());

        m_base.Merge(release);
    }

    m_voodoo_release = match.voodoo;

    std::vector<GameDatabase_Rule> rules;
    GameDatabase::GetLayout(match.release, &rules);
    m_layout.clear();

    for (size_t i = 0; i < rules.size(); i++)
    {
        FatImage_Rule rule;
        rule.source = rules[i].source;
        rule.target = rules[i].target;
        rule.file = rules[i].file;
        m_layout.push_back(rule);
    }

    GameDatabase::GetPatches(match.release, &m_patches);

    std::string root = match.root;

    while (!root.empty() && (root[root.size() - 1] == '/'))
        root.resize(root.size() - 1);

    if (ends_with_no_case(found.path.c_str(), ".zip"))
    {
        m_source = found.path;
        m_zip_prefix = root.empty() ? std::string() : root + "/";
        m_base_directory = path_directory(found.path);
    }
    else
    {
        m_source = root.empty() ? trim_trailing_separators(found.path) : find_host(trim_trailing_separators(found.path), root);
        m_base_directory = m_source;
    }

    m_database = true;
    return true;
}

bool GameDisk::IsGameContent(const char* path)
{
    std::vector<GameDisk_Found> found;
    return Find(path, &found);
}

bool GameDisk::Open(const GameDisk_Found& found, const char* save_directory, const char* temp_directory, bool settings,
    bool write_to_folder, const char* msdos_path, const char* ultrasnd_path, const char* glide_path)
{
    Close();
    m_error.clear();
    m_msdos_path = (IsValidPointer(msdos_path) && MediaFile::Exists(msdos_path)) ? std::string(msdos_path) : std::string();
    m_ultrasnd_path = (IsValidPointer(ultrasnd_path) && MediaFile::Exists(ultrasnd_path)) ? std::string(ultrasnd_path) :
        std::string();
    m_glide_path = (IsValidPointer(glide_path) && MediaFile::Exists(glide_path)) ? std::string(glide_path) : std::string();
    m_descriptor_path = found.path;
    m_save_directory = IsValidPointer(save_directory) ? trim_trailing_separators(save_directory) : std::string();
    m_temp_directory = IsValidPointer(temp_directory) ? trim_trailing_separators(temp_directory) : std::string();

    if (found.inner.compare(0, strlen(k_game_disk_database), k_game_disk_database) == 0)
    {
        if (!OpenFromDatabase(found))
            return false;
    }
    else if ((found.inner.compare(0, strlen(k_game_disk_program), k_game_disk_program) == 0) ||
        (found.inner == k_game_disk_prompt))
    {
        bool prompt = found.inner == k_game_disk_prompt;
        std::string program = prompt ? std::string() : found.inner.substr(strlen(k_game_disk_program));
        std::string name = path_strip_extension(path_filename(found.path), ".zip");
        std::string text = "[geardox]\nversion = 1\n\n[game]\nname = " + name + "\nfolder = C:\\" +
            (prompt ? GetDosFolderName(name) : std::string()) + "\n" + (prompt ? "" : "\n[run]\nexe = " + program + "\n");

        if (!m_base.Parse(path_join(found.path, (prompt ? name : program) + ".gdox").c_str(), text.data(), text.size()))
            return Fail("%s: %s", name.c_str(), m_base.GetError());

        m_base_directory = ends_with_no_case(found.path.c_str(), ".zip") ? path_directory(found.path) :
            trim_trailing_separators(found.path);
    }
    else if (found.inner.empty())
    {
        if (!m_base.Load(found.path.c_str()))
            return Fail("%s: %s", path_filename(found.path).c_str(), m_base.GetError());

        m_base_directory = path_directory(found.path);
    }
    else
    {
        MediaZip zip;
        std::string data;

        if (!zip.Open(found.path.c_str()) || !zip.Extract(found.inner.c_str(), &data))
            return Fail("Unable to read %s in %s", found.inner.c_str(), path_filename(found.path).c_str());

        zip.Close();

        if (!m_base.Parse(path_join(found.path, found.inner).c_str(), data.data(), data.size()))
            return Fail("%s: %s", found.inner.c_str(), m_base.GetError());

        m_base_directory = path_directory(found.path);
    }

    const GDOX_Game_Info& base = m_base.GetInfo();

    if (!m_database && base.source.empty())
        m_source = found.inner.empty() ? m_base_directory : trim_trailing_separators(found.path);
    else if (!m_database)
    {
        std::string source = trim_trailing_separators(path_forward_slashes(base.source));
        size_t inside = to_upper_ascii(source).find(".ZIP/");

        if ((inside != std::string::npos) &&
            !MediaFile::Exists((is_absolute_path(source) ? source : path_join(m_base_directory, source)).c_str()))
        {
            m_zip_prefix = source.substr(inside + 5) + "/";
            source = source.substr(0, inside + 4);
        }

        m_source = is_absolute_path(source) ? source : find_host(m_base_directory, source);
    }

    m_source_zip = ends_with_no_case(m_source.c_str(), ".zip");
    m_source_image = HardDiskImage::IsImage(m_source.c_str()) && !MediaFile::IsDirectory(m_source.c_str());

    if (!m_source_image && !m_base.Has("game", "folder"))
        return Fail("%s: no folder in [game], the game's folder on C: such as C:\\GAME, or C:\\ for the root",
            path_filename(found.inner.empty() ? found.path : found.inner).c_str());

    if (!MediaFile::Exists(m_source.c_str()))
        return Fail("The game's %s isn't there: %s", m_source_image ? "hard disk image" : "folder or ZIP", m_source.c_str());

    std::string source = m_zip_prefix.empty() ? m_source : m_source + "|" + m_zip_prefix;
    m_folder = FolderDisk::GetDataFolder(m_save_directory.c_str(), base.name, crc_key(source + "|" + base.name));

    if (!MediaFile::MakeDirectories(m_folder.c_str()))
        return Fail("Unable to make the game's folder %s", m_folder.c_str());

    if (settings)
    {
        m_overrides_path = path_join(m_folder, k_game_disk_settings);

        if (MediaFile::Exists(m_overrides_path.c_str()) && !m_overrides.Load(m_overrides_path.c_str()))
        {
            Log("Ignoring the game settings %s: %s", m_overrides_path.c_str(), m_overrides.GetError());
            m_overrides.Clear();
        }
    }

    m_descriptor = m_base;
    m_descriptor.Merge(m_overrides);
    m_write_saves = m_source_zip || !write_to_folder;
    m_folder_read_only = !m_write_saves && !m_source_image && !FolderDisk::IsWritable(m_source);
    m_write_saves = m_write_saves || m_folder_read_only;

    if (!TakeMachineSlot())
        return Fail("%s already runs on %d machines of this computer", base.name.c_str(), k_game_disk_machines);

    m_open = true;
    return true;
}

static std::string get_temp_directory(void)
{
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH + 1];
    DWORD length = GetTempPathW(MAX_PATH + 1, buffer);
    return (length > 0) && (length <= MAX_PATH) ? wstring_to_utf8(buffer) : std::string();
#else
    const char* folder = getenv("TMPDIR");
    return IsValidPointer(folder) && (folder[0] != 0) ? std::string(folder) : std::string("/tmp");
#endif
}

static int take_process_lock(const std::string& path)
{
    std::string text;
    unsigned long self = get_process_id();

    if (MediaFile::ReadFile(path.c_str(), k_media_file_no_limit, &text, true))
    {
        unsigned long owner = strtoul(text.c_str(), NULL, 10);

        if ((owner != self) && is_process_running(owner))
            return 0;
    }

    std::string pid = format_string("%lu\n", self);
    return MediaFile::WriteFileAtomic(path.c_str(), pid.data(), pid.size()) ? 1 : -1;
}

bool GameDisk::TakeMachineSlot()
{
    bool source_lock = !m_write_saves;
    std::string temp = source_lock ? get_temp_directory() : std::string();
    std::string key = crc_key(m_source + "|" + m_zip_prefix);

    for (int slot = 1; slot <= k_game_disk_machines; slot++)
    {
        std::string data = slot == 1 ? m_folder : path_join(m_folder, format_string("Machine %d", slot));

        if (FolderDisk::IsLockedElsewhere(data))
            continue;

        if (source_lock && !temp.empty())
        {
            std::string lock = path_join(temp, format_string("geardox-%s-%d.lock", key.c_str(), slot));
            int taken = take_process_lock(lock);

            if (taken == 0)
                continue;

            if (taken > 0)
                m_source_lock = lock;
        }

        m_machine_slot = slot;
        m_disk_folder = data;

        if (slot > 1)
        {
            m_write_saves = true;
            Log("%s runs as machine %d on this computer, its changes stay in %s", m_base.GetInfo().name.c_str(), slot,
                data.c_str());
        }

        return MediaFile::MakeDirectories(m_disk_folder.c_str());
    }

    return false;
}

void GameDisk::ReleaseMachineSlot()
{
    if (!m_source_lock.empty())
        MediaFile::RemoveFile(m_source_lock.c_str());

    m_source_lock.clear();
    m_disk_folder.clear();
    m_machine_slot = 1;
}

int GameDisk::GetMachineSlot() const
{
    return m_machine_slot;
}

void GameDisk::Close()
{
    Discard();
    ReleaseMachineSlot();
    m_open = false;
    m_has_machine = false;
    m_base.Clear();
    m_overrides.Clear();
    m_descriptor.Clear();
    m_descriptor_path.clear();
    m_base_directory.clear();
    m_save_directory.clear();
    m_temp_directory.clear();
    m_folder.clear();
    m_overrides_path.clear();
    m_source.clear();
    m_source_zip = false;
    m_source_image = false;
    m_zip_prefix.clear();
    m_root.clear();
    m_layout.clear();
    m_patches.clear();
    m_database = false;
    m_release_settings = false;
    m_withheld_settings = false;
    m_msdos_path.clear();
    m_msdos.zip.clear();
    m_msdos.name.clear();
    m_msdos_loaded = false;
    m_ultrasnd_path.clear();
    m_ultrasnd.zip.clear();
    m_ultrasnd.name.clear();
    m_ultrasnd_loaded = false;
    m_glide_path.clear();
    m_glide.clear();
    m_glide_name.clear();
    m_glide_loaded = false;
    m_voodoo_release = false;
    m_program = -1;
    m_built_program = GDOX_GAME_PROGRAM_GAME;
    m_voodoo_program = false;
    m_voodoo_driver = false;
    m_voodoo_exe = false;
    m_voodoo_note.title.clear();
    m_voodoo_note.detail.clear();
    m_write_saves = false;
    m_folder_read_only = false;
    m_machine_slot = 1;
    m_config_base.clear();
    m_autoexec_base.clear();
    m_merged.clear();
    m_startup_edits = false;
    m_recovery = FolderDisk_Result();
    m_left_out = 0;

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
        m_hard_disks[i].clear();

    m_cdroms.clear();
    m_floppies[0].clear();
    m_floppies[1].clear();
}

void GameDisk::Discard()
{
    SafeDelete(m_disk);

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
        SafeDelete(m_hard_disk_sources[i]);

    if (!m_extracted.empty())
        MediaFile::RemoveTree(m_extracted.c_str());

    m_extracted.clear();
}

bool GameDisk::SaveOverrides(const GameDescriptor& overrides)
{
    if (!m_open || m_overrides_path.empty())
        return false;

    if (overrides.IsEmpty())
        return ClearOverrides();

    if (!MediaFile::MakeDirectories(m_folder.c_str()) || !overrides.WriteFile(m_overrides_path.c_str()))
        return Fail("Unable to write the game settings %s", m_overrides_path.c_str());

    m_overrides = overrides;
    m_descriptor = m_base;
    m_descriptor.Merge(m_overrides);
    return true;
}

bool GameDisk::ClearOverrides()
{
    if (!m_open)
        return false;

    if (!m_overrides_path.empty())
        MediaFile::RemoveFile(m_overrides_path.c_str());

    for (int i = 0; i < 2; i++)
    {
        std::string folder = path_join(m_disk_folder, i == 0 ? k_game_disk_freedos : k_game_disk_msdos);
        std::string base = path_join(folder, k_game_disk_base);
        MediaFile::RemoveFile(path_join(folder, k_dos_config_sys).c_str());
        MediaFile::RemoveFile(path_join(folder, k_dos_autoexec_bat).c_str());
        MediaFile::RemoveFile(path_join(base, k_dos_config_sys).c_str());
        MediaFile::RemoveFile(path_join(base, k_dos_autoexec_bat).c_str());
    }

    m_overrides.Clear();
    m_descriptor = m_base;
    m_startup_edits = false;
    return true;
}

bool GameDisk::IsOpen() const
{
    return m_open;
}

bool GameDisk::IsImage() const
{
    return m_source_image;
}

const GameDescriptor& GameDisk::GetDescriptor() const
{
    return m_descriptor;
}

const GameDescriptor& GameDisk::GetBase() const
{
    return m_base;
}

const GameDescriptor& GameDisk::GetOverrides() const
{
    return m_overrides;
}

// The descriptor's machine, the CD drive moved off the game's hard disks, RAM enough for its EMS when it names none
// and the user's is too small, and the 3dfx card for its 3dfx program
void GameDisk::GetGameSettings(const GDOX_Machine_Settings& user, GDOX_Machine_Settings* game) const
{
    GetGameSettings(m_descriptor, user, game, GetSystem());

    if (IsVoodooChosen())
        game->voodoo = 1;
}

// The game's own choice, then MS-DOS when the user imported it, then FreeDOS
int GameDisk::GetSystem() const
{
    int system = m_descriptor.GetInfo().system;
    return system >= 0 ? system : HasMSDOS() ? (int)GDOX_GAME_SYSTEM_MSDOS : (int)GDOX_GAME_SYSTEM_FREEDOS;
}

// A frontend's choice for this session over the descriptor's and the overrides', as libretro's core option is
void GameDisk::SetSystem(int system)
{
    m_descriptor = m_base;
    m_descriptor.Merge(m_overrides);

    if ((system >= 0) && (system < GDOX_GAME_SYSTEM_COUNT))
        m_descriptor.Set("dos", "system", system == GDOX_GAME_SYSTEM_MSDOS ? "msdos" : "freedos");
}

// The user's layout for every game, the frontend's setting; the descriptor's keyboard key wins over it
void GameDisk::SetKeyboardLayout(const char* layout)
{
    m_keyboard.clear();

    for (int i = 0; IsValidPointer(layout) && (i < k_keyboard_layout_count); i++)
    {
        if (strcmp(layout, k_keyboard_layouts[i].id) == 0)
            m_keyboard = layout;
    }
}

const char* GameDisk::GetKeyboardLayout() const
{
    const std::string& game = m_descriptor.GetInfo().keyboard;
    return !game.empty() ? game.c_str() : !m_keyboard.empty() ? m_keyboard.c_str() : k_keyboard_layouts[0].id;
}

bool GameDisk::HasMSDOS() const
{
    return !m_msdos_path.empty();
}

const char* GameDisk::GetSystemName() const
{
    if (m_source_image)
        return "DOS in its image";

    if (GetSystem() != GDOX_GAME_SYSTEM_MSDOS)
        return k_freedos_version;

    return m_msdos_loaded ? m_msdos.name.c_str() : "MS-DOS";
}

bool GameDisk::HasCDExtension() const
{
    return !m_msdos_loaded || m_msdos.cd_extension;
}

// The user's import is read once a session, and refused whole if it doesn't add up
bool GameDisk::UseVoodooExe(FatImage* image, const GDOX_Machine_Config& machine)
{
    const GDOX_Game_Info& info = m_descriptor.GetInfo();
    bool chosen = GetProgram() == GDOX_GAME_PROGRAM_VOODOO;
    bool offered = IsVoodooOffered();
    std::string path;
    std::string data;
    bool readable = offered && program_path(info.folder, info.voodoo_exe, &path);
    m_voodoo_program = offered && (!readable || image->GetFile(path.c_str(), &data));
    m_voodoo_driver = readable && m_voodoo_program && GlideImport::ImportsOverlay(data) && !has_game_overlay(image, path);
    m_voodoo_note.title.clear();
    m_voodoo_note.detail.clear();
    bool voodoo = chosen && m_voodoo_program && machine.voodoo && (!m_voodoo_driver || LoadGlide());

    if (chosen && !voodoo)
    {
        std::string program = program_name(info.voodoo_exe);
        m_voodoo_note.kind = m_voodoo_program && machine.voodoo ? GAME_DISK_MESSAGE_VOODOO_DRIVER : GAME_DISK_MESSAGE_VOODOO;
        m_voodoo_note.title = info.voodoo_exe.empty() ? "The game has no 3dfx program" :
            !offered ? "This copy of the game has no 3dfx program" :
            !m_voodoo_program ? program + " isn't in the game" :
            !machine.voodoo ? "The 3dfx Voodoo needs a Pentium machine" : program + " needs the 3dfx Voodoo driver";
        m_voodoo_note.detail = info.exe.empty() ? "The DOS prompt runs instead" : program_name(info.exe) + " runs instead";
    }
    else if (machine.voodoo && !voodoo && program_path(info.folder, info.exe, &path) &&
        image->GetFile(path.c_str(), &data) && GlideImport::ImportsOverlay(data) && !has_game_overlay(image, path) &&
        !LoadGlide())
    {
        m_voodoo_note.kind = GAME_DISK_MESSAGE_VOODOO_DRIVER;
        m_voodoo_note.title = program_name(info.exe) + " needs the 3dfx Voodoo driver";
    }

    return voodoo;
}

bool GameDisk::LoadGlide()
{
    if (m_glide_loaded)
        return !m_glide.empty();

    m_glide_loaded = true;

    if (!m_glide_path.empty() && !GlideImport::Load(m_glide_path.c_str(), &m_glide, &m_glide_name))
        Log("%s is not a GLIDE2X.OVL Geardox uses", m_glide_path.c_str());

    return !m_glide.empty();
}

bool GameDisk::LoadUltraSound()
{
    if (m_ultrasnd_loaded)
        return true;

    if (m_ultrasnd_path.empty())
        return false;

    std::string error;

    if (!GusImport::Load(m_ultrasnd_path.c_str(), &m_ultrasnd, &error))
    {
        Log("%s", error.c_str());
        return false;
    }

    m_ultrasnd_loaded = true;
    return true;
}

bool GameDisk::LoadSystem()
{
    if (GetSystem() != GDOX_GAME_SYSTEM_MSDOS)
        return true;

    if (!HasMSDOS())
        return Fail("This game needs MS-DOS, and none has been imported: import your own MS-DOS setup disks first");

    if (m_msdos_loaded)
        return true;

    std::string error;

    if (!DosImport::Load(m_msdos_path.c_str(), &m_msdos, &error))
        return Fail("%s", error.c_str());

    m_msdos_loaded = true;
    return true;
}

void GameDisk::GetGameSettings(const GameDescriptor& descriptor, const GDOX_Machine_Settings& user,
    GDOX_Machine_Settings* game, int system)
{
    descriptor.GetMachineSettings(game);
    const GDOX_Game_Info& info = descriptor.GetInfo();
    int cdrom = game->cdrom >= 0 ? game->cdrom : user.cdrom;

    if ((cdrom > GDOX_CDROM_NONE) && (cdrom < GDOX_CDROM_POSITION_COUNT) &&
        ((cdrom == GDOX_CDROM_PRIMARY_MASTER) || !info.hard_disks[cdrom - 1].empty()))
        game->cdrom = GDOX_CDROM_SECONDARY_MASTER;

    if ((info.ems_kb <= 0) || (info.memory != GDOX_GAME_MEMORY_EMS) || (game->ram_mb > 0))
        return;

    int model = game->model >= 0 ? game->model : user.model >= 0 ? user.model : (int)k_machine_default_model;
    int video = game->video_card >= 0 ? game->video_card : user.video_card;
    bool cirrus = (video < 0) || (video >= GDOX_VIDEO_CARD_COUNT) || (k_video_cards[video].family == GDOX_VIDEO_FAMILY_CIRRUS);
    int needed = GameDescriptor::GetEMSRAM(info.ems_kb, model, cirrus, system);
    int ram = user.ram_mb > 0 ? user.ram_mb : k_machine_default_ram_mb;

    if (needed > ram)
        game->ram_mb = needed;
}

bool GameDisk::Prepare(const GDOX_Machine_Config& machine)
{
    if (!m_open)
        return Fail("No game is open");

    Discard();
    m_error.clear();
    m_recovery = FolderDisk_Result();
    m_recoveries.clear();
    m_merged.clear();
    m_startup_edits = false;
    m_left_out = 0;
    const GDOX_Game_Info& info = m_descriptor.GetInfo();

    if (m_source_zip)
    {
        m_extracted = path_join(m_temp_directory, path_filename(m_folder));
        MediaFile::RemoveTree(m_extracted.c_str());
        MediaZip zip;
        Log("Unpacking %s to %s", m_source.c_str(), m_extracted.c_str());

        if (m_temp_directory.empty() || !zip.Open(m_source.c_str()) || !zip.ExtractAll(m_extracted.c_str()))
        {
            Discard();
            return Fail("Unable to unpack the game's ZIP: %s", m_source.c_str());
        }

        m_root = m_zip_prefix.empty() ? m_extracted : find_host(m_extracted, m_zip_prefix);
    }
    else
        m_root = m_source;

    m_floppies[0].clear();
    m_floppies[1].clear();
    m_cdroms.clear();

    for (int drive = 0; drive < 2; drive++)
    {
        for (size_t i = 0; i < info.floppies[drive].size(); i++)
        {
            std::string path;

            if (!ResolveMedia(info.floppies[drive][i], &path))
            {
                Discard();
                return false;
            }

            m_floppies[drive].push_back(path);
        }
    }

    for (size_t i = 0; i < info.cds.size(); i++)
    {
        std::string path;

        if (!ResolveMedia(info.cds[i], &path))
        {
            Discard();
            return false;
        }

        path = trim_trailing_separators(path);

        if (MediaFile::IsDirectory(path.c_str()) && ((path.size() <= 3) || (path == path_directory(path))))
        {
            Discard();
            return Fail("A drive's root can't be the game's CD: %s", path.c_str());
        }

        m_cdroms.push_back(path);
    }

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        m_hard_disks[i].clear();

        if (info.hard_disks[i].empty())
            continue;

        m_hard_disks[i] = is_absolute_path(info.hard_disks[i]) ? info.hard_disks[i] :
            find_host(m_base_directory, info.hard_disks[i]);

        if (!MediaFile::Exists(m_hard_disks[i].c_str()))
        {
            Discard();
            return Fail("The game's hard disk isn't there: %s", m_hard_disks[i].c_str());
        }

        if (HardDiskImage::IsImage(m_hard_disks[i].c_str()))
            continue;

        std::string error;
        m_hard_disk_sources[i] = FolderDisk::OpenSource(m_hard_disks[i].c_str(), m_save_directory.c_str(),
            m_temp_directory.c_str(), &error);

        if (!IsValidPointer(m_hard_disk_sources[i]))
        {
            Discard();
            return Fail("%s", error.c_str());
        }

        m_recoveries.push_back(m_hard_disk_sources[i]->GetRecovery());
    }

    m_machine = machine;
    m_has_machine = true;

    if (m_source_image)
    {
        m_hard_disks[0] = m_source;
        return true;
    }

    if (!LoadSystem())
    {
        Discard();
        return false;
    }

    if ((GetSystem() == GDOX_GAME_SYSTEM_MSDOS) && !info.dos_version.empty())
        Log("%s: [dos] version is FreeDOS's, MS-DOS sets versions per program with SETVER: ignored", GetName());

    std::vector<FolderDisk_Layer> layers;
    FolderDisk* disk = new FolderDisk();
    bool ok = true;

    if (FolderDisk::HasSession(m_disk_folder))
    {
        FatImage* tree = new FatImage();

        if (!BuildTree(machine, tree, &layers))
        {
            SafeDelete(tree);
            ok = false;
        }
        else if (!disk->Recover(tree, layers, m_disk_folder))
            ok = Fail("%s", disk->GetError());
    }

    if (ok)
    {
        FatImage* tree = new FatImage();

        if (!BuildTree(machine, tree, &layers))
        {
            SafeDelete(tree);
            ok = false;
        }
        else if (!disk->Open(tree, layers, m_disk_folder, (u64)info.free_mb * 1024 * 1024, m_source))
            ok = Fail("%s", disk->GetError());
    }

    if (!ok)
    {
        SafeDelete(disk);
        Discard();
        return false;
    }

    m_recovery = disk->GetRecovery();
    m_recoveries.insert(m_recoveries.begin(), m_recovery);

    if (!m_voodoo_note.title.empty())
        Log("%s: %s%s%s", GetName(), m_voodoo_note.title.c_str(), m_voodoo_note.detail.empty() ? "" : ", ",
            m_voodoo_note.detail.c_str());

    m_left_out = disk->GetLeftOut();
    disk->SetTemporary(m_extracted);
    m_extracted.clear();
    m_disk = disk;
    return true;
}

FolderDisk* GameDisk::TakeDisk()
{
    FolderDisk* disk = m_disk;
    InitPointer(m_disk);
    return disk;
}

FolderDisk* GameDisk::TakeHardDisk(int id)
{
    if ((id < 0) || (id >= GDOX_MAX_HARD_DISKS))
        return NULL;

    FolderDisk* disk = m_hard_disk_sources[id];
    InitPointer(m_hard_disk_sources[id]);
    return disk;
}

// FreeDOS's lines from its kernel's config.txt and JEMM's Readme.txt, MS-DOS's from MS-DOS 6.22's own HELP
std::string GameDisk::GetConfigSys(const GDOX_Machine_Config& machine) const
{
    UNUSED(machine);
    const GDOX_Game_Info& info = m_descriptor.GetInfo();
    bool msdos = GetSystem() == GDOX_GAME_SYSTEM_MSDOS;
    std::string frame;
    std::string out;
    char line[128];

    if (info.ems_frame == 0)
        frame = " FRAME=NONE";
    else if (info.ems_frame > 0)
    {
        snprintf(line, sizeof(line), " FRAME=%04X", info.ems_frame);
        frame = line;
    }

    out += dos_fs_config_header(msdos, m_msdos.version);

    if (!msdos)
        out += "IDLEHALT=1\r\n";

    snprintf(line, sizeof(line), "FILES=%d\r\nBUFFERS=%d\r\n", info.files, info.buffers);
    out += line;

    if (!info.dos_version.empty() && !msdos)
        out += "VERSION=" + info.dos_version + "\r\n";

    if (msdos)
    {
        if (info.memory != GDOX_GAME_MEMORY_NONE)
        {
            out += info.memory == GDOX_GAME_MEMORY_XMS ? "DOS=HIGH\r\n" : "DOS=HIGH,UMB\r\n";
            out += dos_fs_himem_line(m_msdos.version);
        }

        if (info.memory == GDOX_GAME_MEMORY_UMB)
            out += "DEVICE=C:\\DOS\\EMM386.EXE NOEMS\r\n";
        else if ((info.memory == GDOX_GAME_MEMORY_EMS) && (m_msdos.version >= 600))
            out += "DEVICE=C:\\DOS\\EMM386.EXE RAM" + frame + "\r\n";
        else if (info.memory == GDOX_GAME_MEMORY_EMS)
        {
            // MS-DOS 5's EMM386 sets its EMS aside as it loads, 256K unless told: the game's, or all it finds
            snprintf(line, sizeof(line), "DEVICE=C:\\DOS\\EMM386.EXE %d RAM", info.ems_kb > 0 ? ((info.ems_kb + 15) / 16) * 16 : 32768);
            out += line + frame + "\r\n";
        }

        out += "DEVICEHIGH=C:\\GEARDOX\\GDXIDLE.SYS\r\n";

        if (!info.cds.empty())
            out += "DEVICEHIGH=C:\\GEARDOX\\UDVD2.SYS /D:FDCD0001\r\n";
    }
    else
    {
        if (info.memory == GDOX_GAME_MEMORY_XMS)
            out += "DOS=HIGH\r\nDEVICE=C:\\FREEDOS\\BIN\\HIMEMX.EXE\r\n";
        else if (info.memory == GDOX_GAME_MEMORY_UMB)
            out += "DOS=HIGH,UMB\r\nDOSDATA=UMB\r\nDEVICE=C:\\FREEDOS\\BIN\\JEMMEX.EXE NOEMS\r\n";
        else if (info.memory == GDOX_GAME_MEMORY_EMS)
            out += "DOS=HIGH,UMB\r\nDOSDATA=UMB\r\nDEVICE=C:\\FREEDOS\\BIN\\JEMMEX.EXE" + frame + "\r\n";

        if (!info.cds.empty())
            out += "DEVICEHIGH=C:\\FREEDOS\\BIN\\UDVD2.SYS /D:FDCD0001\r\n";
    }

    for (size_t i = 0; i < info.config_sys.size(); i++)
        out += info.config_sys[i] + "\r\n";

    return out + dos_fs_shell_line(msdos);
}

// Paths in [run] are from the game's folder, which is base on C:
static void add_run(std::string* out, const std::string& base, const std::string& command, const std::string& cwd,
    const std::string& args)
{
    std::string line = backslashes(command);
    std::string target = line;
    std::string rest = args;
    size_t space = line.find(' ');

    if (rest.empty() && (space != std::string::npos))
    {
        target = line.substr(0, space);
        rest = line.substr(space + 1);
    }

    size_t slash = target.find_last_of('\\');
    std::string directory = slash == std::string::npos ? std::string() : target.substr(0, slash);
    std::string program = slash == std::string::npos ? target : target.substr(slash + 1);
    std::string where = cwd.empty() ? directory : backslashes(cwd);
    bool batch = ends_with_no_case(program.c_str(), ".bat");

    if ((target.size() > 1) && (target[1] == ':') && (to_upper_ascii(target[0]) != 'C'))
    {
        std::string inside = directory.size() > 2 ? directory.substr(2) : std::string();

        while (!inside.empty() && (inside[0] == '\\'))
            inside.erase(0, 1);

        *out += target.substr(0, 2) + "\r\n";
        *out += "CD \\" + inside + "\r\n";
        *out += std::string(batch ? "CALL " : "") + target;
        *out += rest.empty() ? "\r\n" : " " + rest + "\r\n";
        *out += "C:\r\n";
        return;
    }

    *out += "CD \\" + dos_fs_join(base, where) + "\r\n";
    *out += std::string(batch ? "CALL " : "") +
        (strings_equal_ignore_case(where, directory) ? program : "C:\\" + dos_fs_join(base, target));
    *out += rest.empty() ? "\r\n" : " " + rest + "\r\n";
}

std::string GameDisk::GetBatch(const GDOX_Machine_Config& machine) const
{
    static const char* const k_types[GDOX_SOUND_CARD_COUNT] = { NULL, NULL, "1", "3", "2", "4", "6" };
    const GDOX_Game_Info& info = m_descriptor.GetInfo();
    const GDOX_Sound_Card_Info& card = k_sound_cards[machine.sound_card < GDOX_SOUND_CARD_COUNT ? machine.sound_card : 0];
    bool msdos = GetSystem() == GDOX_GAME_SYSTEM_MSDOS;
    std::string out;
    char line[128];

    out += dos_fs_autoexec_header(msdos);

    if ((card.dsp_major != 0) && IsValidPointer(k_types[machine.sound_card]))
    {
        snprintf(line, sizeof(line), "SET BLASTER=A220 I%d D%d%s T%s\r\n", machine.sound_irq, machine.sound_dma,
            card.mpu ? (machine.mpu != GDOX_MPU_OFF ? " H5 P330" : " H5") : "", k_types[machine.sound_card]);
        out += line;
    }

    if (machine.gus)
    {
        int irq = get_gus_irq(machine);
        int dma = get_gus_dma(machine);
        snprintf(line, sizeof(line), "SET ULTRASND=%X,%d,%d,%d,%d\r\nSET ULTRADIR=C:\\ULTRASND\r\nGDXGUS\r\n",
            (unsigned)k_ultrasound_port, dma, dma, irq, irq);
        out += line;
    }

    if (machine.voodoo)
        out += "SET PATH=%PATH%;C:\\\r\n";

    out += "PROMPT $P$G\r\n";

    // KEYB with the VGA's code page 437, from MS-DOS's HELP KEYB and FreeDOS's KEYB documents
    if (strcmp(GetKeyboardLayout(), k_keyboard_layouts[0].id) != 0)
    {
        std::string layout = to_upper_ascii(GetKeyboardLayout());
        bool low = (info.memory == GDOX_GAME_MEMORY_NONE) || (info.memory == GDOX_GAME_MEMORY_XMS);

        if (!msdos && low)
            out += "MKEYB " + layout + "\r\n";
        else
            out += dos_fs_keyb_line(layout, msdos);
    }

    if (machine.mouse != GDOX_MOUSE_NONE)
        out += msdos ? "LH C:\\GEARDOX\\CTMOUSE.EXE\r\n" : "LH CTMOUSE\r\n";

    if (!info.cds.empty() && msdos && HasCDExtension())
        out += "LH C:\\DOS\\MSCDEX.EXE /D:FDCD0001\r\n";
    else if (!info.cds.empty())
        out += msdos ? "LH C:\\GEARDOX\\SHSUCDX.COM /QQ /D:FDCD0001\r\n" : "LH SHSUCDX /QQ /D:FDCD0001\r\n";

    // Read caching only, after the CD extension so SMARTDRV caches the disc too: a write is never held back
    if (info.cache && (info.memory != GDOX_GAME_MEMORY_NONE) && (!msdos || !m_msdos_loaded || m_msdos.cache))
        out += msdos ? "C:\\DOS\\SMARTDRV.EXE /X /Q\r\n" : "LH C:\\FREEDOS\\BIN\\LBACACHE.COM\r\n";

    for (size_t i = 0; i < info.env.size(); i++)
        out += "SET " + info.env[i].name + "=" + info.env[i].value + "\r\n";

    out += "C:\r\nCD \\" + info.folder + "\r\n";

    for (size_t i = 0; i < info.autoexec.size(); i++)
        out += info.autoexec[i] + "\r\n";

    out += "GDXRUN\r\nIF ERRORLEVEL 3 GOTO DEBUG\r\nIF ERRORLEVEL 2 GOTO PROMPT\r\nIF ERRORLEVEL 1 GOTO SETUP\r\n";

    bool voodoo = machine.voodoo && m_voodoo_exe;
    const std::string& exe = voodoo ? info.voodoo_exe : info.exe;

    if (!exe.empty())
    {
        add_run(&out, info.folder, exe, info.cwd, voodoo ? info.voodoo_args : info.args);

        if (info.exit == GDOX_GAME_EXIT_POWEROFF)
            out += "POWEROFF\r\n";
    }

    out += "GOTO PROMPT\r\n:DEBUG\r\nGDXDBG\r\n";

    if (info.exit == GDOX_GAME_EXIT_POWEROFF)
        out += "POWEROFF\r\n";

    out += "GOTO PROMPT\r\n:SETUP\r\n";

    if (!info.setup_exe.empty())
        add_run(&out, info.folder, info.setup_exe, "", info.setup_args);
    else
        out += "ECHO This game has no setup program.\r\n";

    out += ":PROMPT\r\n";

    if (msdos)
        out += dos_fs_doskey_line();

    return out;
}

const char* GameDisk::GetName() const
{
    return m_descriptor.GetInfo().name.c_str();
}

const char* GameDisk::GetDescriptorPath() const
{
    return m_descriptor_path.c_str();
}

const char* GameDisk::GetSourcePath() const
{
    return m_source.c_str();
}

const char* GameDisk::GetRootPath() const
{
    return m_root.c_str();
}

std::string GameDisk::GetSourceSetting() const
{
    return m_zip_prefix.empty() ? m_source : m_source + "/" + trim_trailing_separators(m_zip_prefix);
}

bool GameDisk::IsFromDatabase() const
{
    return m_database;
}

bool GameDisk::HasReleaseSettings() const
{
    return m_release_settings;
}

bool GameDisk::HasWithheldSettings() const
{
    return m_withheld_settings;
}

void GameDisk::SetProgram(int program)
{
    m_program = (program >= 0) && (program < GDOX_GAME_PROGRAM_COUNT) ? program : -1;
}

int GameDisk::GetProgram() const
{
    return m_program >= 0 ? m_program : m_descriptor.GetInfo().program;
}

bool GameDisk::IsVoodooOffered() const
{
    return !m_descriptor.GetInfo().voodoo_exe.empty() && (!m_database || m_voodoo_release);
}

bool GameDisk::IsVoodooChosen() const
{
    return IsVoodooOffered() && (GetProgram() == GDOX_GAME_PROGRAM_VOODOO);
}

bool GameDisk::HasVoodooProgram() const
{
    return m_voodoo_program;
}

bool GameDisk::VoodooProgramNeedsDriver() const
{
    return m_voodoo_driver;
}

bool GameDisk::UsesVoodooExe() const
{
    return m_voodoo_exe;
}

bool GameDisk::Fail(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    m_error = vformat_string(format, args);
    va_end(args);
    Log("%s", m_error.c_str());
    return false;
}

const char* GameDisk::GetFolder() const
{
    return m_folder.c_str();
}

std::string GameDisk::GetSavesPath() const
{
    if (m_source_image)
        return GetWorkingPath().empty() ? path_directory(m_source) : m_disk_folder;

    return m_write_saves ? path_join(m_disk_folder, k_game_disk_saves) : m_source;
}

std::string GameDisk::GetWorkingPath() const
{
    bool working = m_source_image && (m_write_saves || ends_with_no_case(m_source.c_str(), ".chd"));
    return working ? path_join(m_disk_folder, k_game_disk_working) : std::string();
}

const char* GameDisk::GetSettingsPath() const
{
    return m_overrides_path.c_str();
}

std::string GameDisk::GetKeysPath() const
{
    return path_join(m_folder, k_game_disk_keys);
}

bool GameDisk::IsWritingToSaves() const
{
    return m_write_saves;
}

bool GameDisk::IsFolderReadOnly() const
{
    return m_folder_read_only;
}

bool GameDisk::HasStartupEdits() const
{
    return m_startup_edits;
}

const FolderDisk_Result& GameDisk::GetRecovery() const
{
    return m_recovery;
}

u32 GameDisk::GetLeftOut() const
{
    return m_left_out;
}

const std::vector<std::string>& GameDisk::GetFloppies(int drive) const
{
    return m_floppies[drive == 1 ? 1 : 0];
}

const std::vector<std::string>& GameDisk::GetCdRoms() const
{
    return m_cdroms;
}

const char* GameDisk::GetCdRomLabel() const
{
    return m_descriptor.GetInfo().cdrom_label.c_str();
}

const char* GameDisk::GetHardDisk(int id) const
{
    return (id >= 0) && (id < GDOX_MAX_HARD_DISKS) ? m_hard_disks[id].c_str() : "";
}

const GDOX_Machine_Config& GameDisk::GetMachine() const
{
    return m_machine;
}

bool GameDisk::NeedsRestart(const GDOX_Machine_Config& machine) const
{
    return m_has_machine && !m_source_image && ((GetProgram() != m_built_program) || ((machine.gus != 0) != (m_machine.gus != 0)) ||
        ((machine.voodoo != 0) != (m_machine.voodoo != 0)) || (GetConfigSys(machine) != m_config_base) ||
        (GetBatch(machine) != m_autoexec_base));
}

void GameDisk::GetMessages(std::vector<GameDisk_Message>* messages) const
{
    GameDisk_Message message;
    messages->clear();

    for (size_t i = 0; i < m_recoveries.size(); i++)
    {
        const FolderDisk_Result& r = m_recoveries[i];
        const char* title = FolderDisk::DescribeRecovery(r);

        if (!IsValidPointer(title))
            continue;

        message.kind = r.done ? GAME_DISK_MESSAGE_RECOVERED : GAME_DISK_MESSAGE_REFUSED;
        message.title = title;

        if (r.done)
            message.detail = r.conflicts != 0 ? format_string("%u files in Conflicts", r.conflicts) :
                format_string("%u files written back", r.written);
        else
            message.detail = r.notes.empty() ? std::string() : r.notes[0];

        if (r.path != m_source)
            message.detail = path_filename(r.path) + ": " + message.detail;

        messages->push_back(message);
    }

    if (m_machine_slot > 1)
    {
        message.kind = GAME_DISK_MESSAGE_MACHINE;
        message.title = format_string("Machine %d: another Geardox runs this game, so its changes stay here", m_machine_slot);
        message.detail = m_disk_folder;
        messages->push_back(message);
    }

    if (IsFolderReadOnly())
    {
        message.kind = GAME_DISK_MESSAGE_READ_ONLY;
        message.title = "The game's folder is read-only: saves go to the save folder";
        message.detail = GetSavesPath();
        messages->push_back(message);
    }

    if (IsFromDatabase())
    {
        message.kind = GAME_DISK_MESSAGE_RECOGNISED;
        message.title = "Recognised from the game database";
        message.detail = GetName();

        if (HasReleaseSettings())
            message.detail += ", with this copy's sound settings";
        else if (HasWithheldSettings())
            message.detail += ", with the default sound settings";

        messages->push_back(message);
    }

    if (GetLeftOut() != 0)
    {
        message.kind = GAME_DISK_MESSAGE_LEFT_OUT;
        message.title = "Part of the game's folder didn't fit DOS";
        message.detail = format_string("%u items left out", GetLeftOut());
        messages->push_back(message);
    }

    for (size_t i = 0; i < m_merged.size(); i++)
    {
        message.kind = GAME_DISK_MESSAGE_STARTUP;
        message.title = "Your " + m_merged[i] + " changes now apply to its new version";
        message.detail = GetDosFolder();
        messages->push_back(message);
    }

    if (!m_voodoo_note.title.empty())
        messages->push_back(m_voodoo_note);
}

const char* GameDisk::GetError() const
{
    return m_error.c_str();
}

std::string GameDisk::GetDosFolder() const
{
    return path_join(m_disk_folder, GetSystem() == GDOX_GAME_SYSTEM_MSDOS ? k_game_disk_msdos : k_game_disk_freedos);
}

std::string GameDisk::GetStartupFile(const char* name, const std::string& base)
{
    std::string folder = GetDosFolder();
    std::string user_path = path_join(folder, name);
    std::string base_path = path_join(path_join(folder, k_game_disk_base), name);
    std::string user;
    std::string before;

    if (!MediaFile::ReadFile(user_path.c_str(), k_media_file_no_limit, &user, true))
        return base;

    m_startup_edits = true;

    if (!MediaFile::ReadFile(base_path.c_str(), k_media_file_no_limit, &before, true) || (before == base))
        return user;

    std::string merged = merge_lines(before, user, base, strcmp(name, k_dos_config_sys) == 0 ? "SHELL=" : "GDXRUN");

    if (!MediaFile::WriteFileAtomic(user_path.c_str(), merged.data(), merged.size()) ||
        !MediaFile::WriteFileAtomic(base_path.c_str(), base.data(), base.size()))
        Log("Unable to keep the merged %s in %s", name, folder.c_str());

    if (std::find(m_merged.begin(), m_merged.end(), std::string(name)) == m_merged.end())
        m_merged.push_back(name);

    return merged;
}

bool GameDisk::BuildTree(const GDOX_Machine_Config& machine, FatImage* image, std::vector<FolderDisk_Layer>* layers)
{
    const GDOX_Game_Info& info = m_descriptor.GetInfo();
    u8 attributes = info.system_hidden ? k_fat_attribute_hidden : 0x20;
    bool msdos = GetSystem() == GDOX_GAME_SYSTEM_MSDOS;
    std::vector<std::string> reserved;
    std::vector<std::string> pinned;

    if (msdos && !m_msdos_loaded)
        return Fail("MS-DOS isn't loaded");

    if (msdos)
    {
        pinned = m_msdos.pinned;
        reserved = m_msdos.root;
        reserved.insert(reserved.end(), k_game_disk_msdos_reserved, k_game_disk_msdos_reserved +
            sizeof(k_game_disk_msdos_reserved) / sizeof(k_game_disk_msdos_reserved[0]));
    }
    else
        reserved.assign(k_game_disk_freedos_reserved, k_game_disk_freedos_reserved +
            sizeof(k_game_disk_freedos_reserved) / sizeof(k_game_disk_freedos_reserved[0]));

    std::string top = to_upper_ascii(info.folder.substr(0, info.folder.find('\\')));

    if (!top.empty() && (std::find(reserved.begin(), reserved.end(), top) != reserved.end()))
        return Fail("The game's folder, C:\\%s, is the system's: give it another folder in [game]", info.folder.c_str());

    std::string saves = path_join(m_disk_folder, k_game_disk_saves);
    std::string c = path_join(m_disk_folder, k_game_disk_c);
    std::string dos = GetDosFolder();

    if (!image->AddGame(m_root.c_str(), info.folder.c_str(), k_game_disk_layer_game, k_game_disk_layer_c))
        return Fail("Unable to read the game's files: %s", m_root.c_str());

    if (m_write_saves && MediaFile::IsDirectory(saves.c_str()) &&
        !image->AddOverlay(saves.c_str(), true, k_game_disk_layer_game))
        return Fail("Unable to read the game's saves: %s", saves.c_str());

    image->ApplyLayout(m_layout);

    for (size_t i = 0; i < m_patches.size(); i++)
    {
        const GameDatabase_Patch& patch = m_patches[i];
        std::string path = info.folder.empty() ? patch.path : path_forward_slashes(info.folder) + "/" + patch.path;

        if (!image->PatchFile(path.c_str(), patch.offset, patch.before, patch.after))
            Log("%s: the release's patch to %s doesn't apply to this copy", info.name.c_str(), patch.path.c_str());
    }

    if (MediaFile::IsDirectory(c.c_str()) && !image->AddOverlay(c.c_str(), false, k_game_disk_layer_c))
        return Fail("Unable to read the game's changes to C: %s", c.c_str());

    image->SetLayer(k_game_disk_layer_system);

    if (msdos && !image->AddMSDOS((const u8*)m_msdos.zip.data(), m_msdos.zip.size(), info.system_hidden, reserved, pinned,
        !HasCDExtension()))
        return Fail("Unable to add %s to the game's disk", m_msdos.name.c_str());

    if (!msdos && !image->AddFreeDOS(info.system_hidden, reserved))
        return Fail("Unable to add FreeDOS to the game's disk");

    if (machine.gus && LoadUltraSound() && !image->AddUltraSound((const u8*)m_ultrasnd.zip.data(), m_ultrasnd.zip.size()))
        return Fail("Unable to add %s to the game's disk", m_ultrasnd.name.c_str());

    m_built_program = GetProgram();
    m_voodoo_exe = UseVoodooExe(image, machine);

    if (machine.voodoo)
    {
        image->LeaveOutMatching(k_dos_glide_overlay, GlideImport::IsStub);

        if (LoadGlide() && !image->HasFile(k_dos_glide_overlay) &&
            !image->AddMemoryFile(k_dos_glide_overlay, m_glide, attributes))
            return Fail("Unable to add %s to the game's disk", m_glide_name.c_str());
    }

    if (MediaFile::IsDirectory(dos.c_str()) && !image->AddOverlay(dos.c_str(), false, k_game_disk_layer_system))
        return Fail("Unable to read the changes to %s: %s", GetSystemName(), dos.c_str());

    m_config_base = GetConfigSys(machine);
    m_autoexec_base = GetBatch(machine);
    std::string config = GetStartupFile(k_dos_config_sys, m_config_base);
    std::string autoexec = GetStartupFile(k_dos_autoexec_bat, m_autoexec_base);

    if (!image->AddMemoryFile(k_dos_config_sys, config, attributes, &m_config_base) ||
        !image->AddMemoryFile(k_dos_autoexec_bat, autoexec, attributes, &m_autoexec_base))
        return Fail("Unable to add the game's configuration");

    image->FinishGame(info.name.c_str(), msdos ? m_msdos.boot : k_freedos_boot);
    layers->assign(3, FolderDisk_Layer());
    (*layers)[k_game_disk_layer_system].folder = dos;
    (*layers)[k_game_disk_layer_system].deletions = true;
    (*layers)[k_game_disk_layer_system].names = reserved;
    (*layers)[k_game_disk_layer_game].folder = m_write_saves ? saves : m_root;
    (*layers)[k_game_disk_layer_game].deletions = m_write_saves;
    (*layers)[k_game_disk_layer_c].folder = c;
    (*layers)[k_game_disk_layer_c].deletions = false;
    return true;
}

bool GameDisk::ResolveMedia(const std::string& value, std::string* path)
{
    *path = is_absolute_path(value) ? value : find_host(m_base_directory, value);

    if (MediaFile::Exists(path->c_str()))
        return true;

    if (m_source_zip && !is_absolute_path(value))
    {
        *path = find_host(m_root, value);

        if (MediaFile::Exists(path->c_str()))
            return true;
    }

    return Fail("%s isn't there: %s", m_descriptor.GetInfo().name.c_str(), value.c_str());
}
