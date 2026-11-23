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

#include <errno.h>
#include <stdarg.h>
#include <time.h>
#include <algorithm>
#include <map>
#include "game_disk.h"
#include "dos_fs.h"
#include "freedos.h"
#include "glide_import.h"
#include "iso_image.h"
#include "../cdrom/cdrom_media.h"
#include "../media/game_database.h"
#include "../media/media_file.h"
#include "../media/crc.h"
#include "../audio/ultrasound.h"
#include "../system/machine_profiles.h"
#if defined(_WIN32)
#include <process.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <signal.h>
#include <unistd.h>
#define GAME_DISK_POSIX_LOCK
#endif

static const u8 k_game_disk_mode_folder = 0;
static const u8 k_game_disk_mode_overlay = 1;
static const char k_game_disk_database[] = "db:";
static const char k_game_disk_program[] = "exe:";
static bool game_disk_database = true;
static const char k_game_disk_conflicts[] = ".conflicts";
static const char k_game_disk_line_blanks[] = " \t\r\x1A";
static const u32 k_game_disk_startup_lines = 8;

// The names of C:'s root the system owns: a game's entry with one is left out, and DOS's writes there never go back
static const char* const k_game_disk_freedos_reserved[] =
{
    "KERNEL.SYS", "FDCONFIG.SYS", k_dos_root_freedos, k_dos_root_temp, k_dos_config_sys, k_dos_autoexec_bat
};

// MS-DOS's root files come from its version's table, these follow them
static const char* const k_game_disk_msdos_reserved[] =
{
    k_dos_root_dos, k_dos_root_geardox, k_dos_root_temp, k_dos_config_sys, k_dos_autoexec_bat
};

static std::string slashes(const std::string& path)
{
    std::string result(path);
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
}

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
    std::string result(path);
    std::replace(result.begin(), result.end(), '/', '\\');

    while (!result.empty() && (result[0] == '\\'))
        result.erase(0, 1);

    return result;
}

static std::string join(const std::string& directory, const std::string& relative)
{
    std::string path(directory);
    std::string rest = slashes(relative);

    while (!rest.empty() && (rest[0] == '/'))
        rest.erase(0, 1);

    if (rest.empty())
        return path;

    append_path_component(path, rest.c_str());
    return path;
}

static bool exists(const std::string& path)
{
    u64 size = 0;
    s64 modified = 0;
    std::vector<std::string> folders;
    std::vector<std::string> files;
    return MediaFile::GetFileInfo(path.c_str(), &size, &modified) || MediaFile::ListDirectory(path.c_str(), folders, files);
}

static std::string safe_name(const std::string& name)
{
    std::string result;

    for (size_t i = 0; i < name.size(); i++)
    {
        char c = name[i];
        result += (strchr("/\\:*?\"<>|", c) != NULL) || ((u8)c < 32) ? '_' : c;
    }

    return result.empty() ? std::string("Game") : result;
}

static bool make_directories(const std::string& path)
{
    if (MediaFile::IsDirectory(path.c_str()))
        return true;

    std::string parent = path_directory(path);

    if ((parent != path) && (parent != ".") && !make_directories(parent))
        return false;

    return MediaFile::MakeDirectory(path.c_str());
}

static bool is_scratch_path(const std::string& dos_path)
{
    return to_upper_ascii(dos_path).compare(0, 5, "TEMP\\") == 0;
}

static std::string read_text(const std::string& path)
{
    std::string text;
    MediaFile::ReadFile(path.c_str(), k_media_file_no_limit, &text, true);
    return text;
}

static bool write_text(const std::string& path, const std::string& text)
{
    return MediaFile::WriteFileAtomic(path.c_str(), text.data(), text.size());
}

static void put_u8(std::string* out, u8 value)
{
    out->push_back((char)value);
}

static void put_u16(std::string* out, u16 value)
{
    put_u8(out, (u8)value);
    put_u8(out, (u8)(value >> 8));
}

static void put_u32(std::string* out, u32 value)
{
    put_u16(out, (u16)value);
    put_u16(out, (u16)(value >> 16));
}

static void put_u64(std::string* out, u64 value)
{
    put_u32(out, (u32)value);
    put_u32(out, (u32)(value >> 32));
}

static void put_string(std::string* out, const std::string& value)
{
    put_u32(out, (u32)value.size());
    *out += value;
}

struct Manifest_Reader
{
    const std::string* data;
    size_t offset;
    bool ok;

    u8 Byte()
    {
        if (offset >= data->size())
        {
            ok = false;
            return 0;
        }

        return (u8)(*data)[offset++];
    }

    u16 Word()
    {
        u16 low = Byte();
        return (u16)(low | (Byte() << 8));
    }

    u32 Long()
    {
        u32 low = Word();
        return low | ((u32)Word() << 16);
    }

    u64 Quad()
    {
        u64 low = Long();
        return low | ((u64)Long() << 32);
    }

    std::string Text()
    {
        u32 size = Long();

        if (!ok || (size > data->size() - offset))
        {
            ok = false;
            return "";
        }

        std::string value = data->substr(offset, size);
        offset += size;
        return value;
    }
};

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
    std::string program = slashes(command);
    size_t space = program.find(' ');

    if (space != std::string::npos)
        program = program.substr(0, space);

    if ((program.size() > 1) && (program[1] == ':') && (to_upper_ascii(program[0]) != 'C'))
        return false;

    *path = (program.size() > 1) && (program[1] == ':') ? collapse(program.substr(2)) :
        collapse(slashes(folder) + "/" + program);
    return true;
}

static std::string program_name(const std::string& command)
{
    std::string program = slashes(command.substr(0, command.find(' ')));
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

GameDisk::GameDisk()
{
    m_open = false;
    m_source_zip = false;
    m_database = false;
    m_release_settings = false;
    m_withheld_settings = false;
    m_write_overlay = false;
    m_folder_read_only = false;
    m_built = false;
    m_recovered = false;
    m_locked = false;
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
    m_last.done = false;
    m_last.refused = false;
    m_last.written = 0;
    m_last.deleted = 0;
    m_last.directories = 0;
    m_last.conflicts = 0;
    m_last.kept = 0;
    m_last.startup = 0;
    m_system.system = GDOX_GAME_SYSTEM_FREEDOS;
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
        GameDisk_Found item;
        item.path = source;
        found->push_back(item);
        return true;
    }

    if (IsDosProgram(source.c_str()) && exists(source) && !MediaFile::IsDirectory(source.c_str()))
    {
        std::string folder = path_directory(source);

        if (Find(folder.c_str(), found))
            return true;

        GameDisk_Found item;
        item.path = folder;
        item.inner = std::string(k_game_disk_program) + path_filename(source);
        found->push_back(item);
        return true;
    }

    std::vector<std::pair<std::string, GameDisk_Found> > sorted;
    std::vector<std::string> folders;
    std::vector<std::string> files;

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

            std::string name = slashes(stat.m_filename);

            if ((name.find('/') == std::string::npos) && GameDescriptor::IsDescriptor(name.c_str()))
            {
                GameDisk_Found item;
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

            GameDisk_Found item;
            item.path = join(source, files[i]);
            sorted.push_back(std::make_pair(to_upper_ascii(files[i]), item));
        }
    }

    if (sorted.empty() && game_disk_database)
    {
        std::vector<GameDatabase_Match> matches;
        GameDatabase::Find(source.c_str(), &matches);

        for (size_t i = 0; i < matches.size(); i++)
        {
            GameDisk_Found item;
            item.path = source;
            item.inner = std::string(k_game_disk_database) + GameDatabase::GetId(matches[i].entry);
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

    return found.inner.empty() ? path_filename(found.path) : found.inner;
}

bool GameDisk::OpenFromDatabase(const GameDisk_Found& found)
{
    std::string id = found.inner.substr(strlen(k_game_disk_database));
    std::vector<GameDatabase_Match> matches;
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
    std::string name = join(found.path, id + ".gdox");

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
        m_source = root.empty() ? trim_trailing_separators(found.path) : join(trim_trailing_separators(found.path), root);
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

bool GameDisk::Open(const GameDisk_Found& found, const char* save_directory, const char* overrides_directory,
    bool write_to_folder, const char* msdos_path, const char* ultrasnd_path, const char* glide_path)
{
    Close();
    m_error.clear();
    m_msdos_path = (IsValidPointer(msdos_path) && exists(msdos_path)) ? std::string(msdos_path) : std::string();
    m_ultrasnd_path = (IsValidPointer(ultrasnd_path) && exists(ultrasnd_path)) ? std::string(ultrasnd_path) : std::string();
    m_glide_path = (IsValidPointer(glide_path) && exists(glide_path)) ? std::string(glide_path) : std::string();
    m_descriptor_path = found.path;
    m_save_directory = IsValidPointer(save_directory) ? trim_trailing_separators(save_directory) : std::string();

    if (found.inner.compare(0, strlen(k_game_disk_database), k_game_disk_database) == 0)
    {
        if (!OpenFromDatabase(found))
            return false;
    }
    else if (found.inner.compare(0, strlen(k_game_disk_program), k_game_disk_program) == 0)
    {
        std::string program = found.inner.substr(strlen(k_game_disk_program));
        std::string text = "[geardox]\nversion = 1\n\n[game]\nname = " + path_filename(found.path) +
            "\nfolder = C:\\\n\n[run]\nexe = " + program + "\n";
        std::string name = join(found.path, program + ".gdox");

        if (!m_base.Parse(name.c_str(), text.data(), text.size()))
            return Fail("%s: %s", program.c_str(), m_base.GetError());

        m_base_directory = trim_trailing_separators(found.path);
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
        std::string name = join(found.path, found.inner);
        bool parsed = m_base.Parse(name.c_str(), data.data(), data.size());

        if (!parsed)
            return Fail("%s: %s", found.inner.c_str(), m_base.GetError());

        m_base_directory = path_directory(found.path);
    }

    const GDOX_Game_Info& base = m_base.GetInfo();

    if (!m_base.Has("game", "folder"))
        return Fail("%s: no folder in [game], the game's folder on C: such as C:\\GAME, or C:\\ for the root",
            path_filename(found.inner.empty() ? found.path : found.inner).c_str());

    if (!m_database && base.source.empty())
        m_source = found.inner.empty() ? m_base_directory : found.path;
    else if (!m_database)
    {
        m_source = trim_trailing_separators(is_absolute_path(base.source) ? base.source : join(m_base_directory, base.source));
        size_t inside = to_upper_ascii(slashes(m_source)).find(".ZIP/");

        if ((inside != std::string::npos) && !exists(m_source))
        {
            m_zip_prefix = slashes(m_source.substr(inside + 5)) + "/";
            m_source = m_source.substr(0, inside + 4);
        }
    }

    if (!exists(m_source))
        return Fail("The game's folder or ZIP isn't there: %s", m_source.c_str());

    m_source_zip = ends_with_no_case(m_source.c_str(), ".zip");
    std::string key = crc_key(found.path + "|" + found.inner);
    std::string label = safe_name(base.name);

    if (IsValidPointer(overrides_directory) && (overrides_directory[0] != 0))
    {
        m_overrides_path = join(trim_trailing_separators(overrides_directory), label + "." + key + ".gdox.ini");
        u64 size = 0;
        s64 modified = 0;

        if (MediaFile::GetFileInfo(m_overrides_path.c_str(), &size, &modified) && !m_overrides.Load(m_overrides_path.c_str()))
        {
            Log("Ignoring the game overrides %s: %s", m_overrides_path.c_str(), m_overrides.GetError());
            m_overrides.Clear();
        }
    }

    m_descriptor = m_base;
    m_descriptor.Merge(m_overrides);
    m_image = join(m_save_directory, label + "." + key + ".gdox.hdd");
    m_manifest = m_image + ".manifest";
    m_lock = m_image + ".lock";
    m_media_directory = join(m_save_directory, label + "." + key + ".gdox.media");
    std::string source_key = m_zip_prefix.empty() ? m_source : m_source + "|" + m_zip_prefix;
    key = crc_key(source_key);
    std::string source_name = path_strip_extension(path_filename(m_source), ".zip");

    m_overlay = join(m_save_directory, safe_name(source_name) + "." + key + ".gdox");
    m_write_overlay = m_source_zip || !write_to_folder;
    m_folder_read_only = false;

    if (!m_write_overlay)
    {
        std::string probe = join(m_source, ".geardox_write_test");
        MediaFile* file = MediaFile::OpenNewFile(probe.c_str());
        bool writable = IsValidPointer(file) && (file->WriteAt(0, "x", 1)) && file->Flush();
        SafeDelete(file);
        MediaFile::RemoveFile(probe.c_str());

        if (!writable)
        {
            m_write_overlay = true;
            m_folder_read_only = true;
        }
    }

    if (!make_directories(m_save_directory))
        return Fail("Unable to make the save folder %s", m_save_directory.c_str());

    if (!TakeLock())
        return Fail("This game is open in another Geardox: %s", m_lock.c_str());

    m_open = true;
    return true;
}

void GameDisk::Close()
{
    ReleaseLock();
    m_open = false;
    m_has_machine = false;
    m_base.Clear();
    m_overrides.Clear();
    m_descriptor.Clear();
    m_descriptor_path.clear();
    m_overrides_path.clear();
    m_source.clear();
    m_zip_prefix.clear();
    m_layout.clear();
    m_patches.clear();
    m_database = false;
    m_release_settings = false;
    m_withheld_settings = false;
    m_image.clear();
    m_manifest.clear();
    m_lock.clear();
    m_overlay.clear();
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

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
        m_hard_disks[i].clear();

    m_cdroms.clear();
    m_floppies[0].clear();
    m_floppies[1].clear();
    m_built = false;
    m_recovered = false;
    m_left_out = 0;
}

bool GameDisk::IsOpen() const
{
    return m_open;
}

bool GameDisk::SaveOverrides(const GameDescriptor& overrides)
{
    if (!m_open || m_overrides_path.empty())
        return false;

    if (overrides.IsEmpty())
        return ClearOverrides();

    if (!make_directories(path_directory(m_overrides_path)) || !overrides.WriteFile(m_overrides_path.c_str()))
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

    m_overrides.Clear();
    m_descriptor = m_base;
    return true;
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

// Writes back what a session left open, then builds the image or reuses it. Touches nothing of the emulator's, so the
// loading thread can run it
bool GameDisk::Prepare(const GDOX_Machine_Config& machine)
{
    if (!m_open)
        return Fail("No game is open");

    m_built = false;
    m_recovered = false;
    m_refused.clear();
    m_error.clear();
    std::vector<GameDisk_Manifest_Entry> entries;
    bool open = false;
    u32 fingerprint = 0;
    u8 mode = 0;
    bool manifest = LoadManifest(&entries, &open, &fingerprint, &mode);

    if (manifest && open && exists(m_image))
    {
        m_has_machine = false;
        Log("The last session of %s didn't end, writing it back", GetName());

        // A disk DOS left half written would be refused at every launch: it goes aside, kept, and the folder rebuilds it
        if (WriteBack(&m_last))
            m_recovered = true;
        else
        {
            std::string kept = m_image + ".refused";
            MediaFile::RemoveFile(kept.c_str());
            MediaFile::RemoveFile((kept + ".manifest").c_str());

            if (!MediaFile::RenameFile(m_image.c_str(), kept.c_str()))
                return Fail("The last session's changes can't be written back, the disk is kept: %s",
                    m_last.notes.empty() ? "" : m_last.notes[0].c_str());

            MediaFile::RenameFile(m_manifest.c_str(), (kept + ".manifest").c_str());
            m_refused = kept;
            Log("The last session's disk of %s doesn't add up, kept as %s", GetName(), kept.c_str());
        }

        manifest = LoadManifest(&entries, &open, &fingerprint, &mode);
    }

    const GDOX_Game_Info& info = m_descriptor.GetInfo();
    m_floppies[0].clear();
    m_floppies[1].clear();
    m_cdroms.clear();

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
        m_hard_disks[i].clear();

    for (int drive = 0; drive < 2; drive++)
    {
        for (size_t i = 0; i < info.floppies[drive].size(); i++)
        {
            std::string path;

            if (!ResolveMedia(info.floppies[drive][i], &path))
                return false;

            m_floppies[drive].push_back(path);
        }
    }

    for (size_t i = 0; i < info.cds.size(); i++)
    {
        std::string path;

        if (!ResolveMedia(info.cds[i], &path))
            return false;

        if (MediaFile::IsDirectory(path.c_str()) &&
            !BuildFolderCd(trim_trailing_separators(path), info.cdrom_label, &path))
            return false;

        m_cdroms.push_back(path);
    }

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        if (info.hard_disks[i].empty())
            continue;

        m_hard_disks[i] = is_absolute_path(info.hard_disks[i]) ? info.hard_disks[i] :
            join(m_base_directory, info.hard_disks[i]);

        if (!exists(m_hard_disks[i]))
            return Fail("The game's hard disk isn't there: %s", m_hard_disks[i].c_str());
    }

    m_machine = machine;
    m_has_machine = true;
    FatImage image;

    if (!LoadSystem())
        return false;

    if ((GetSystem() == GDOX_GAME_SYSTEM_MSDOS) && !m_descriptor.GetInfo().dos_version.empty())
        Log("%s: [dos] version is FreeDOS's, MS-DOS sets versions per program with SETVER: ignored", GetName());

    if (!Assemble(&image, machine, &m_system))
        return false;

    if (!m_voodoo_note.title.empty())
        Log("%s: %s%s%s", GetName(), m_voodoo_note.title.c_str(), m_voodoo_note.detail.empty() ? "" : ", ",
            m_voodoo_note.detail.c_str());

    m_left_out = image.GetLeftOut();
    u8 wanted = m_write_overlay ? k_game_disk_mode_overlay : k_game_disk_mode_folder;

    if (manifest && !open && (fingerprint != 0) && (fingerprint == image.GetFingerprint()) && (mode == wanted) &&
        exists(m_image))
        return SetManifestOpen(true);

    u64 size = image.GetRequiredSize((u64)info.free_mb * 1024 * 1024);

    if (size == 0)
        return Fail("The game doesn't fit in a 2 GB FAT16 disk");

    Log("Building the disk of %s: %s", GetName(), m_image.c_str());

    if (!image.Write(m_image.c_str(), size, true))
        return Fail("Unable to write the game's disk %s", m_image.c_str());

    ListBuilt(&image, &entries);

    if (!SaveManifest(entries, true, image.GetFingerprint()))
        return Fail("Unable to write the manifest %s", m_manifest.c_str());

    m_built = true;
    return true;
}

// The end of a session, after the machine has flushed and let go of the disk: an open session is written back
bool GameDisk::Finish()
{
    if (!m_open)
        return false;

    bool open = false;
    u32 fingerprint = 0;
    u8 mode = 0;
    std::vector<GameDisk_Manifest_Entry> entries;

    if (!LoadManifest(&entries, &open, &fingerprint, &mode) || !open)
        return true;

    return WriteBack(&m_last);
}

bool GameDisk::RetryFinish()
{
    return Finish();
}

bool GameDisk::IsSessionOpen() const
{
    std::vector<GameDisk_Manifest_Entry> entries;
    bool open = false;
    u32 fingerprint = 0;
    u8 mode = 0;
    return m_open && LoadManifest(&entries, &open, &fingerprint, &mode) && open;
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

static std::string under(const std::string& base, const std::string& path)
{
    return base.empty() ? path : path.empty() ? base : base + "\\" + path;
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

    *out += "CD \\" + under(base, where) + "\r\n";
    *out += std::string(batch ? "CALL " : "") +
        (strings_equal_ignore_case(where, directory) ? program : "C:\\" + under(base, target));
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

const char* GameDisk::GetImagePath() const
{
    return m_image.c_str();
}

const char* GameDisk::GetSourcePath() const
{
    return m_source.c_str();
}

const char* GameDisk::GetOverlayPath() const
{
    return m_overlay.c_str();
}

const char* GameDisk::GetOverridesPath() const
{
    return m_overrides_path.c_str();
}

bool GameDisk::IsWritingToOverlay() const
{
    return m_write_overlay;
}

bool GameDisk::WasBuilt() const
{
    return m_built;
}

bool GameDisk::WasRecovered() const
{
    return m_recovered;
}

const char* GameDisk::GetRefusedPath() const
{
    return m_refused.c_str();
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

bool GameDisk::NeedsRebuild(const GDOX_Machine_Config& machine) const
{
    return m_has_machine && ((GetProgram() != m_built_program) || ((machine.gus != 0) != (m_machine.gus != 0)) ||
        ((machine.voodoo != 0) != (m_machine.voodoo != 0)) || (GetConfigSys(machine) != m_system.config_sys) ||
        (GetBatch(machine) != m_system.autoexec));
}

bool GameDisk::IsFolderReadOnly() const
{
    return m_folder_read_only;
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

const char* GameDisk::GetHardDisk(int id) const
{
    return (id >= 0) && (id < GDOX_MAX_HARD_DISKS) ? m_hard_disks[id].c_str() : "";
}

bool GameDisk::GetHardDiskImage(int id, const char* save_folder, std::string* image, FatImage_Report* report) const
{
    const char* source = GetHardDisk(id);
    image->clear();

    if (source[0] == 0)
        return true;

    if (!FatImage::IsSource(source))
    {
        *image = source;
        return true;
    }

    if (FatImage::PrepareSource(source, save_folder, false, image, report))
        return true;

    image->clear();
    return false;
}

void GameDisk::GetMessages(std::vector<GameDisk_Message>* messages) const
{
    GameDisk_Message message;
    messages->clear();

    if (WasRecovered())
    {
        char detail[128];
        snprintf(detail, sizeof(detail), "%u files written back, %u conflicts", m_last.written, m_last.conflicts);
        message.kind = GAME_DISK_MESSAGE_RECOVERED;
        message.title = "The last session didn't end, its changes were saved";
        message.detail = detail;
        messages->push_back(message);
    }

    if (IsFolderReadOnly())
    {
        message.kind = GAME_DISK_MESSAGE_READ_ONLY;
        message.title = "The game's folder is read-only: saves go to the save folder";
        message.detail = GetOverlayPath();
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
        char detail[64];
        snprintf(detail, sizeof(detail), "%u items left out", GetLeftOut());
        message.kind = GAME_DISK_MESSAGE_LEFT_OUT;
        message.title = "Part of the game's folder didn't fit DOS";
        message.detail = detail;
        messages->push_back(message);
    }

    if (GetRefusedPath()[0] != 0)
    {
        message.kind = GAME_DISK_MESSAGE_REFUSED;
        message.title = "The last session's disk didn't add up, its changes weren't saved";
        message.detail = GetRefusedPath();
        messages->push_back(message);
    }

    if (!m_voodoo_note.title.empty())
        messages->push_back(m_voodoo_note);
}

const GameDisk_Result& GameDisk::GetLastWriteBack() const
{
    return m_last;
}

const char* GameDisk::GetError() const
{
    return m_error.c_str();
}

// Inside the game's folder on C:, and none of the system's
bool GameDisk::IsSystemPath(const std::string& dos_path) const
{
    std::string path = to_upper_ascii(dos_path);

    for (size_t i = 0; i < m_system.reserved.size(); i++)
    {
        const std::string& name = m_system.reserved[i];

        if ((path == name) || ((path.size() > name.size()) && (path.compare(0, name.size(), name) == 0) && (path[name.size()] == '\\')))
            return true;
    }

    return false;
}

bool GameDisk::IsGamePath(const std::string& dos_path) const
{
    std::string base = to_upper_ascii(m_descriptor.GetInfo().folder);
    std::string path = to_upper_ascii(dos_path);

    if (IsSystemPath(path))
        return false;

    return base.empty() || ((path.size() > base.size()) && (path.compare(0, base.size(), base) == 0) &&
        (path[base.size()] == '\\'));
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

bool GameDisk::Assemble(FatImage* image, const GDOX_Machine_Config& machine, GameDisk_Manifest_System* system)
{
    const GDOX_Game_Info& info = m_descriptor.GetInfo();
    u8 attributes = info.system_hidden ? k_fat_attribute_hidden : 0x20;
    bool msdos = GetSystem() == GDOX_GAME_SYSTEM_MSDOS;
    std::vector<std::string> pinned;

    if (msdos && !m_msdos_loaded)
        return Fail("MS-DOS isn't loaded");

    system->system = msdos ? (u8)GDOX_GAME_SYSTEM_MSDOS : (u8)GDOX_GAME_SYSTEM_FREEDOS;
    system->reserved.clear();

    if (msdos)
    {
        pinned = m_msdos.pinned;
        system->reserved = m_msdos.root;
        system->reserved.insert(system->reserved.end(), k_game_disk_msdos_reserved, k_game_disk_msdos_reserved +
            sizeof(k_game_disk_msdos_reserved) / sizeof(k_game_disk_msdos_reserved[0]));
    }
    else
        system->reserved.assign(k_game_disk_freedos_reserved, k_game_disk_freedos_reserved +
            sizeof(k_game_disk_freedos_reserved) / sizeof(k_game_disk_freedos_reserved[0]));

    std::string top = to_upper_ascii(info.folder.substr(0, info.folder.find('\\')));

    if (!top.empty() && (std::find(system->reserved.begin(), system->reserved.end(), top) != system->reserved.end()))
        return Fail("The game's folder, C:\\%s, is the system's: give it another folder in [game]", info.folder.c_str());

    system->config_sys = GetConfigSys(machine);

    if (!image->AddGame(m_source.c_str(), info.folder.c_str(), m_zip_prefix.c_str()))
        return Fail("Unable to read the game's files: %s", m_source.c_str());

    if (MediaFile::IsDirectory(m_overlay.c_str()) && !image->AddOverlay(m_overlay.c_str()))
        return Fail("Unable to read the game's saves: %s", m_overlay.c_str());

    image->ApplyLayout(m_layout);

    for (size_t i = 0; i < m_patches.size(); i++)
    {
        const GameDatabase_Patch& patch = m_patches[i];
        std::string path = info.folder.empty() ? patch.path : slashes(info.folder) + "/" + patch.path;

        if (!image->PatchFile(path.c_str(), patch.offset, patch.before, patch.after))
            Log("%s: the release's patch to %s doesn't apply to this copy", info.name.c_str(), patch.path.c_str());
    }

    if (msdos && !image->AddMSDOS((const u8*)m_msdos.zip.data(), m_msdos.zip.size(), info.system_hidden,
        system->reserved, pinned, !HasCDExtension()))
        return Fail("Unable to add %s to the game's disk", m_msdos.name.c_str());

    if (!msdos && !image->AddFreeDOS(info.system_hidden, system->reserved))
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

    system->autoexec = GetBatch(machine);

    if (!image->AddMemoryFile(k_dos_config_sys, system->config_sys, attributes) ||
        !image->AddMemoryFile(k_dos_autoexec_bat, system->autoexec, attributes))
        return Fail("Unable to add the game's configuration");

    std::string label = m_descriptor.GetInfo().name;
    image->FinishGame(label.c_str(), (u64)info.free_mb * 1024 * 1024, msdos ? m_msdos.boot : k_freedos_boot);
    return true;
}

// Relative to the descriptor's folder, then inside the game's ZIP, taken out to the media folder
bool GameDisk::ResolveMedia(const std::string& value, std::string* path)
{
    *path = is_absolute_path(value) ? value : join(m_base_directory, value);

    if (exists(*path))
        return true;

    std::string inner = m_source_zip && !is_absolute_path(value) ? collapse(m_zip_prefix + slashes(value)) : std::string();

    if (!inner.empty() && ExtractFromZip(inner, path))
        return true;

    return Fail("%s isn't there: %s", m_descriptor.GetInfo().name.c_str(), value.c_str());
}

// A CUE takes its folder in the ZIP with it, for its tracks
bool GameDisk::BuildFolderCd(const std::string& folder, const std::string& label, std::string* path)
{
    if ((folder.size() <= 3) || (folder == path_directory(folder)))
        return Fail("A drive's root can't be the game's CD: %s", folder.c_str());

    IsoImage image;
    image.SetLabel(label.empty() ? path_filename(folder).c_str() : label.c_str());

    if (!image.AddFolder(folder.c_str()))
        return Fail("Unable to read the folder for the game's CD: %s", folder.c_str());

    *path = join(m_media_directory, safe_name(path_filename(folder)) + ".iso");
    u32 fingerprint = 0;

    if (IsoImage::ReadFingerprint(path->c_str(), &fingerprint) && (fingerprint == image.GetFingerprint()))
        return true;

    Log("Building the CD of %s from %s", m_descriptor.GetInfo().name.c_str(), folder.c_str());

    if (!make_directories(m_media_directory) || !image.Write(path->c_str()))
        return Fail("Unable to build the game's CD from %s", folder.c_str());

    return true;
}

bool GameDisk::ExtractFromZip(const std::string& inner, std::string* path)
{
    MediaZip zip;

    if (!zip.Open(m_source.c_str()))
        return false;

    std::string wanted = to_upper_ascii(inner);
    std::string folder = to_upper_ascii(path_directory(inner));
    folder = folder == "." ? std::string() : folder + "/";
    bool cue = ends_with_no_case(inner.c_str(), ".cue");
    bool found = false;
    bool ok = make_directories(m_media_directory) && ExtractEntries(&zip, wanted, folder, cue, path, &found);

    if (ok && found && !cue && (CdRomMedia::ProbeFile(path->c_str()) == GDOX_CDROM_FILE_CUE))
        ok = ExtractEntries(&zip, wanted, folder, true, path, &found);

    return ok && found;
}

bool GameDisk::ExtractEntries(MediaZip* zip, const std::string& wanted, const std::string& folder, bool cue,
    std::string* path, bool* found)
{
    bool ok = true;

    for (mz_uint i = 0; ok && (i < mz_zip_reader_get_num_files(zip->GetArchive())); i++)
    {
        mz_zip_archive_file_stat stat;

        if (!mz_zip_reader_file_stat(zip->GetArchive(), i, &stat) || stat.m_is_directory)
            continue;

        std::string name = slashes(stat.m_filename);
        std::string key = to_upper_ascii(name);
        bool wanted_file = key == wanted;
        bool sibling = cue && (key.compare(0, folder.size(), folder) == 0) &&
            (key.find('/', folder.size()) == std::string::npos);

        if (!wanted_file && !sibling)
            continue;

        std::string out = join(m_media_directory, name);
        u64 size = 0;
        s64 modified = 0;
        bool fresh = MediaFile::GetFileInfo(out.c_str(), &size, &modified) && (size == stat.m_uncomp_size);

        if (!fresh)
        {
            Log("Taking %s out of %s", name.c_str(), path_filename(m_source).c_str());
            ok = make_directories(path_directory(out)) && zip->ExtractToFile(i, out.c_str());
        }

        if (wanted_file)
        {
            *path = out;
            *found = true;
        }
    }

    return ok;
}

bool GameDisk::LoadManifest(std::vector<GameDisk_Manifest_Entry>* entries, bool* open, u32* fingerprint, u8* mode,
    GameDisk_Manifest_System* system) const
{
    entries->clear();
    u64 size = 0;
    s64 modified = 0;

    if (m_manifest.empty() || !MediaFile::GetFileInfo(m_manifest.c_str(), &size, &modified))
        return false;

    std::string data = read_text(m_manifest);
    Manifest_Reader reader = { &data, 0, true };

    if ((reader.Long() != k_game_disk_manifest_magic) || (reader.Long() != k_game_disk_manifest_version))
        return false;

    *open = reader.Byte() != 0;
    *fingerprint = reader.Long();
    u32 builder = reader.Long();
    *mode = reader.Byte();
    u32 count = reader.Long();

    if (builder != k_game_disk_builder_version)
        *fingerprint = 0;

    for (u32 i = 0; reader.ok && (i < count); i++)
    {
        GameDisk_Manifest_Entry entry;
        entry.dos_path = reader.Text();
        entry.host_path = reader.Text();
        u8 flags = reader.Byte();
        entry.directory = (flags & 0x01) != 0;
        entry.system = (flags & 0x02) != 0;
        entry.overlay = (flags & 0x04) != 0;
        entry.host_exists = (flags & 0x08) != 0;
        entry.size = reader.Long();
        entry.crc = reader.Long();
        entry.date = reader.Word();
        entry.time = reader.Word();
        entry.first_cluster = reader.Long();
        entry.host_size = reader.Quad();
        entry.host_modified = (s64)reader.Quad();
        entries->push_back(entry);
    }

    if (!reader.ok || !IsValidPointer(system))
        return reader.ok;

    system->system = GDOX_GAME_SYSTEM_FREEDOS;
    system->reserved.assign(k_game_disk_freedos_reserved, k_game_disk_freedos_reserved +
        sizeof(k_game_disk_freedos_reserved) / sizeof(k_game_disk_freedos_reserved[0]));
    system->config_sys.clear();
    system->autoexec.clear();

    // An older manifest, without the DOS fields, stops at its entries, and its disk is FreeDOS's
    if (reader.offset == data.size())
        return true;

    system->system = reader.Byte();
    u32 reserved = reader.Long();
    system->reserved.clear();

    for (u32 i = 0; reader.ok && (i < reserved); i++)
        system->reserved.push_back(reader.Text());

    system->config_sys = reader.Text();
    system->autoexec = reader.Text();
    return reader.ok && (system->system < GDOX_GAME_SYSTEM_COUNT);
}

bool GameDisk::SaveManifest(const std::vector<GameDisk_Manifest_Entry>& entries, bool open, u32 fingerprint)
{
    std::string out;
    put_u32(&out, k_game_disk_manifest_magic);
    put_u32(&out, k_game_disk_manifest_version);
    put_u8(&out, open ? 1 : 0);
    put_u32(&out, fingerprint);
    put_u32(&out, k_game_disk_builder_version);
    put_u8(&out, m_write_overlay ? k_game_disk_mode_overlay : k_game_disk_mode_folder);
    put_u32(&out, (u32)entries.size());

    for (size_t i = 0; i < entries.size(); i++)
    {
        const GameDisk_Manifest_Entry& entry = entries[i];
        put_string(&out, entry.dos_path);
        put_string(&out, entry.host_path);
        put_u8(&out, (u8)((entry.directory ? 0x01 : 0) | (entry.system ? 0x02 : 0) | (entry.overlay ? 0x04 : 0) |
            (entry.host_exists ? 0x08 : 0)));
        put_u32(&out, entry.size);
        put_u32(&out, entry.crc);
        put_u16(&out, entry.date);
        put_u16(&out, entry.time);
        put_u32(&out, entry.first_cluster);
        put_u64(&out, entry.host_size);
        put_u64(&out, (u64)entry.host_modified);
    }

    put_u8(&out, m_system.system);
    put_u32(&out, (u32)m_system.reserved.size());

    for (size_t i = 0; i < m_system.reserved.size(); i++)
        put_string(&out, m_system.reserved[i]);

    put_string(&out, m_system.config_sys);
    put_string(&out, m_system.autoexec);
    return write_text(m_manifest, out);
}

bool GameDisk::SetManifestOpen(bool open)
{
    std::string data = read_text(m_manifest);

    if (data.size() < 9)
        return Fail("The manifest is damaged: %s", m_manifest.c_str());

    data[8] = open ? 1 : 0;

    if (!write_text(m_manifest, data))
        return Fail("Unable to write the manifest %s", m_manifest.c_str());

    return true;
}

std::string GameDisk::GetDestination(const std::string& host_path) const
{
    return join(m_write_overlay ? m_overlay : m_source, host_path);
}

void GameDisk::ListBuilt(FatImage* image, std::vector<GameDisk_Manifest_Entry>* entries)
{
    std::vector<FatImage_Listed> listed;
    image->ListEntries(&listed);
    entries->clear();

    for (size_t i = 0; i < listed.size(); i++)
    {
        const FatImage_Entry& built = image->GetEntry(listed[i].index);
        GameDisk_Manifest_Entry entry;
        entry.dos_path = listed[i].dos_path;
        entry.host_path = listed[i].host_path;
        entry.directory = built.directory;
        entry.system = built.origin == FAT_IMAGE_ORIGIN_SYSTEM;
        entry.overlay = built.origin == FAT_IMAGE_ORIGIN_OVERLAY;
        entry.size = (u32)built.size;
        entry.crc = built.crc;
        entry.date = built.date;
        entry.time = built.time;
        entry.first_cluster = built.first_cluster;
        entry.host_exists = false;
        entry.host_size = 0;
        entry.host_modified = 0;

        if (!entry.system && !entry.directory)
        {
            std::string destination = entry.overlay ? join(m_overlay, entry.host_path) : GetDestination(entry.host_path);
            entry.host_exists = MediaFile::GetFileInfo(destination.c_str(), &entry.host_size, &entry.host_modified);
        }

        entries->push_back(entry);
    }
}

bool GameDisk::WriteConflict(FatImage* image, const FatImage_File& file, const std::string& host_path)
{
    std::string path = join(join(m_overlay, k_game_disk_conflicts), host_path);
    std::string temporary = path + ".tmp";

    if (!make_directories(path_directory(path)) || !image->CopyOut(file, temporary.c_str()))
    {
        MediaFile::RemoveFile(temporary.c_str());
        return false;
    }

    if (MediaFile::MoveIntoPlace(temporary.c_str(), path.c_str()))
        return true;

    MediaFile::RemoveFile(temporary.c_str());
    return false;
}

void GameDisk::LoadDeleted()
{
    dos_fs_read_deleted(m_overlay, &m_deleted);
}

bool GameDisk::SaveDeleted()
{
    std::string path = join(m_overlay, k_dos_deleted_list);

    if (m_deleted.empty())
    {
        MediaFile::RemoveFile(path.c_str());
        return true;
    }

    std::string text;

    for (size_t i = 0; i < m_deleted.size(); i++)
        text += m_deleted[i] + "\n";

    return make_directories(m_overlay) && write_text(path, text);
}

bool GameDisk::IsDeleted(const std::string& host_path) const
{
    for (size_t i = 0; i < m_deleted.size(); i++)
    {
        if (strings_equal_ignore_case(m_deleted[i], host_path))
            return true;
    }

    return false;
}

void GameDisk::SetDeleted(const std::string& host_path, bool deleted)
{
    for (size_t i = m_deleted.size(); i > 0; i--)
    {
        if (strings_equal_ignore_case(m_deleted[i - 1], host_path))
            m_deleted.erase(m_deleted.begin() + (i - 1));
    }

    if (deleted)
        m_deleted.push_back(host_path);
}

// The image is read whole first and refused if it doesn't add up; then each change goes to the folder or the overlay,
// and nothing that changed on the host since the build is overwritten
bool GameDisk::WriteBack(GameDisk_Result* result)
{
    result->done = false;
    result->refused = false;
    result->written = 0;
    result->deleted = 0;
    result->directories = 0;
    result->conflicts = 0;
    result->kept = 0;
    result->startup = 0;
    result->notes.clear();

    std::vector<GameDisk_Manifest_Entry> entries;
    bool open = false;
    u32 fingerprint = 0;
    u8 mode = 0;

    if (!LoadManifest(&entries, &open, &fingerprint, &mode, &m_system))
    {
        result->refused = true;
        result->notes.push_back("The manifest is missing or damaged: " + m_manifest);
        return false;
    }

    FatImage image;

    if (!image.Read(m_image.c_str()))
    {
        result->refused = true;
        result->notes.push_back(std::string("The disk doesn't add up, nothing was written back: ") + image.GetReadError());
        Log("%s", result->notes.back().c_str());
        return false;
    }

    bool saved_overlay = m_write_overlay;
    m_write_overlay = mode == k_game_disk_mode_overlay;
    LoadDeleted();
    const std::vector<FatImage_File>& files = image.GetFiles();
    std::map<std::string, size_t> found;
    std::map<std::string, size_t> known;
    std::map<std::string, std::string> folders;
    std::map<std::string, std::string> written_hosts;
    bool system_changed = false;
    bool deleted_changed = false;

    for (size_t i = 0; i < files.size(); i++)
        found[to_upper_ascii(files[i].path)] = i;

    for (size_t i = 0; i < entries.size(); i++)
    {
        known[to_upper_ascii(entries[i].dos_path)] = i;

        if (entries[i].directory)
            folders[to_upper_ascii(entries[i].dos_path)] = entries[i].host_path;
    }

    std::string base = m_descriptor.GetInfo().folder;

    if (!base.empty())
        folders[to_upper_ascii(base)] = "";

    std::vector<GameDisk_Manifest_Entry> updated;

    for (size_t i = 0; i < entries.size(); i++)
    {
        const GameDisk_Manifest_Entry& entry = entries[i];
        std::map<std::string, size_t>::const_iterator it = found.find(to_upper_ascii(entry.dos_path));
        const FatImage_File* file = it == found.end() ? NULL : &files[it->second];

        if (entry.system)
        {
            if (is_scratch_path(entry.dos_path))
                continue;

            bool changed = !IsValidPointer(file) || (file->directory != entry.directory) || (!file->directory &&
                ((file->size != entry.size) || (file->date != entry.date) || (file->time != entry.time) ||
                (file->first_cluster != entry.first_cluster)));

            if (changed && !entry.directory)
                NoteStartupFile(&image, file, entry, result);

            system_changed = system_changed || changed;
            continue;
        }

        std::string destination = entry.overlay ? join(m_overlay, entry.host_path) : GetDestination(entry.host_path);
        bool to_overlay = entry.overlay || m_write_overlay;

        if (entry.directory)
            continue;

        u64 host_size = 0;
        s64 host_modified = 0;
        bool host_exists = MediaFile::GetFileInfo(destination.c_str(), &host_size, &host_modified);
        bool host_same = (host_exists == entry.host_exists) && (!host_exists ||
            ((host_size == entry.host_size) && (host_modified == entry.host_modified)));

        if (IsValidPointer(file) && !file->directory)
        {
            bool same = (file->size == entry.size) && (file->date == entry.date) && (file->time == entry.time) &&
                (file->first_cluster == entry.first_cluster);
            u32 crc = 0;

            if (!same)
                same = (file->size == entry.size) && image.GetFileCRC(*file, &crc) && (crc == entry.crc);

            if (same)
                continue;

            if (!host_same)
            {
                result->conflicts++;
                result->notes.push_back("Changed on the host too, the guest's copy is in the overlay's .conflicts: " +
                    entry.host_path);
                WriteConflict(&image, *file, entry.host_path);
                continue;
            }

            std::string temporary = destination + ".tmp";

            if (!make_directories(path_directory(destination)) || !image.CopyOut(*file, temporary.c_str()) ||
                !MediaFile::MoveIntoPlace(temporary.c_str(), destination.c_str()))
            {
                MediaFile::RemoveFile(temporary.c_str());
                result->kept++;
                result->notes.push_back("Unable to write " + destination);
                continue;
            }

            s64 modified = dos_time_to_host(file->date, file->time);

            if (modified != 0)
                MediaFile::SetModified(destination.c_str(), modified);

            if (to_overlay && IsDeleted(entry.host_path))
            {
                SetDeleted(entry.host_path, false);
                deleted_changed = true;
            }

            written_hosts[to_upper_ascii(entry.dos_path)] = entry.host_path;
            result->written++;
            continue;
        }

        if (!host_same)
        {
            result->kept++;
            result->notes.push_back("Deleted in the game but changed on the host, kept: " + entry.host_path);
            continue;
        }

        if (host_exists && !MediaFile::RemoveFile(destination.c_str()))
        {
            result->kept++;
            result->notes.push_back("Unable to delete " + destination);
            continue;
        }

        if (to_overlay)
        {
            SetDeleted(entry.host_path, true);
            deleted_changed = true;
        }

        result->deleted++;
    }

    for (size_t i = entries.size(); i > 0; i--)
    {
        const GameDisk_Manifest_Entry& entry = entries[i - 1];

        if (!entry.directory || entry.system || (found.count(to_upper_ascii(entry.dos_path)) != 0))
            continue;

        std::string destination = entry.overlay ? join(m_overlay, entry.host_path) : GetDestination(entry.host_path);

        if (MediaFile::IsDirectory(destination.c_str()) && !MediaFile::DeleteDirectory(destination.c_str()))
        {
            result->kept++;
            result->notes.push_back("A folder the game removed isn't empty on the host, kept: " + entry.host_path);
            continue;
        }

        if (m_write_overlay || entry.overlay)
        {
            SetDeleted(entry.host_path, true);
            deleted_changed = true;
        }

        result->directories++;
    }

    for (size_t i = 0; i < files.size(); i++)
    {
        const FatImage_File& file = files[i];
        std::string key = to_upper_ascii(file.path);

        if (known.count(key) != 0)
            continue;

        if (!IsGamePath(file.path))
        {
            if (!IsSystemPath(file.path))
            {
                result->kept++;
                result->notes.push_back("Outside the game's folder, not kept: C:\\" + file.path);
            }

            continue;
        }

        size_t slash = file.path.find_last_of('\\');
        std::string parent = slash == std::string::npos ? std::string() : to_upper_ascii(file.path.substr(0, slash));
        std::string name = slash == std::string::npos ? file.path : file.path.substr(slash + 1);
        std::map<std::string, std::string>::const_iterator folder = folders.find(parent);
        std::string host_parent = parent.empty() ? std::string() : folder == folders.end() ? backslashes(parent) : folder->second;
        std::replace(host_parent.begin(), host_parent.end(), '\\', '/');
        std::string host_path = host_parent.empty() ? name : host_parent + "/" + name;
        std::string destination = GetDestination(host_path);

        if (file.directory)
        {
            folders[key] = host_path;

            if (!make_directories(destination))
            {
                result->kept++;
                result->notes.push_back("Unable to make the folder " + destination);
                continue;
            }

            if (m_write_overlay && IsDeleted(host_path))
            {
                SetDeleted(host_path, false);
                deleted_changed = true;
            }

            written_hosts[key] = host_path;
            result->directories++;
            continue;
        }

        std::vector<std::string> host_folders;
        std::vector<std::string> host_files;
        MediaFile::ListDirectory(path_directory(destination).c_str(), host_folders, host_files);
        bool taken = (find_no_case(host_files, name) >= 0) || (find_no_case(host_folders, name) >= 0);

        if (taken)
        {
            result->conflicts++;
            result->notes.push_back("A new file has the name of one on the host, the guest's copy is in the overlay's "
                ".conflicts: " + host_path);
            WriteConflict(&image, file, host_path);
            continue;
        }

        std::string temporary = destination + ".tmp";

        if (!make_directories(path_directory(destination)) || !image.CopyOut(file, temporary.c_str()) ||
            !MediaFile::MoveIntoPlace(temporary.c_str(), destination.c_str()))
        {
            MediaFile::RemoveFile(temporary.c_str());
            result->kept++;
            result->notes.push_back("Unable to write " + destination);
            continue;
        }

        s64 modified = dos_time_to_host(file.date, file.time);

        if (modified != 0)
            MediaFile::SetModified(destination.c_str(), modified);

        if (m_write_overlay && IsDeleted(host_path))
        {
            SetDeleted(host_path, false);
            deleted_changed = true;
        }

        written_hosts[key] = host_path;
        result->written++;
    }

    if (deleted_changed && !SaveDeleted())
    {
        result->kept++;
        result->notes.push_back("Unable to write " + join(m_overlay, k_dos_deleted_list));
    }

    for (size_t i = 0; i < files.size(); i++)
    {
        const FatImage_File& file = files[i];
        std::string key = to_upper_ascii(file.path);
        std::map<std::string, size_t>::const_iterator old = known.find(key);
        GameDisk_Manifest_Entry entry;
        entry.dos_path = file.path;
        entry.directory = file.directory;
        entry.system = !IsGamePath(file.path);
        entry.overlay = (old != known.end()) ? entries[old->second].overlay : false;
        entry.size = file.size;
        entry.date = file.date;
        entry.time = file.time;
        entry.first_cluster = file.first_cluster;
        entry.crc = 0;
        entry.host_exists = false;
        entry.host_size = 0;
        entry.host_modified = 0;

        if (old != known.end())
            entry.host_path = entries[old->second].host_path;
        else if (written_hosts.count(key) != 0)
            entry.host_path = written_hosts[key];
        else
            entry.host_path = slashes(file.path);

        bool same = (old != known.end()) && (entries[old->second].size == file.size) &&
            (entries[old->second].date == file.date) && (entries[old->second].time == file.time) &&
            (entries[old->second].first_cluster == file.first_cluster);

        if (same)
            entry.crc = entries[old->second].crc;
        else if (!file.directory)
            image.GetFileCRC(file, &entry.crc);

        if (!entry.system && !entry.directory)
        {
            std::string destination = entry.overlay ? join(m_overlay, entry.host_path) : GetDestination(entry.host_path);
            entry.host_exists = MediaFile::GetFileInfo(destination.c_str(), &entry.host_size, &entry.host_modified);
        }

        updated.push_back(entry);
    }

    image.CloseRead();
    u32 next = 0;

    // The next launch reuses the image only if a fresh build would make this one: a DOS or startup files changed
    // during the session, by the game's overrides, need a new build
    if ((result->conflicts == 0) && (result->kept == 0) && !system_changed && m_has_machine)
    {
        FatImage fresh;
        GameDisk_Manifest_System system;

        if (Assemble(&fresh, m_machine, &system) && (system.system == m_system.system) &&
            (system.config_sys == m_system.config_sys) && (system.autoexec == m_system.autoexec))
            next = fresh.GetFingerprint();
    }

    bool saved = SaveManifest(updated, false, next);
    m_write_overlay = saved_overlay;

    if (!saved)
    {
        result->refused = true;
        result->notes.push_back("Unable to write the manifest " + m_manifest);
        return false;
    }

    result->done = true;
    Log("Write-back of %s: %u written, %u deleted, %u folders, %u conflicts, %u kept, %u startup", GetName(),
        result->written, result->deleted, result->directories, result->conflicts, result->kept, result->startup);
    return true;
}

// The next launch writes CONFIG.SYS and AUTOEXEC.BAT again, so the lines DOS added are named
void GameDisk::NoteStartupFile(FatImage* image, const FatImage_File* file, const GameDisk_Manifest_Entry& entry, GameDisk_Result* result)
{
    std::string name = to_upper_ascii(entry.dos_path);
    bool config = name == k_dos_config_sys;

    if (!config && (name != k_dos_autoexec_bat))
        return;

    std::string path = std::string("C:\\") + name;
    const char* section = config ? "[config_sys]" : "[autoexec]";

    if (!IsValidPointer(file) || file->directory)
    {
        result->startup++;
        result->notes.push_back("The game deleted " + path + ". Geardox writes it again at the next launch.");
        return;
    }

    std::string data;
    u32 crc = 0;

    if (!image->GetFileData(*file, &data, &crc) || (crc == entry.crc))
        return;

    const std::string& generated = config ? m_system.config_sys : m_system.autoexec;
    std::vector<std::string> known;
    std::vector<std::string> added;
    std::vector<std::string> lines;
    split_string(generated, '\n', &lines);

    for (size_t i = 0; i < lines.size(); i++)
        known.push_back(to_upper_ascii(trim_chars(lines[i], k_game_disk_line_blanks)));

    split_string(data, '\n', &lines);

    for (size_t i = 0; i < lines.size(); i++)
    {
        std::string line = trim_chars(lines[i], k_game_disk_line_blanks);

        if (!line.empty() && (std::find(known.begin(), known.end(), to_upper_ascii(line)) == known.end()))
            added.push_back(line);
    }

    std::string note = "The game changed " + path + ". Geardox writes it again at every launch, so ";

    if (added.empty())
        note += "the change wasn't kept.";
    else
    {
        note += added.size() == 1 ? "this line wasn't kept: " : "these lines weren't kept: ";

        for (size_t i = 0; (i < added.size()) && (i < k_game_disk_startup_lines); i++)
            note += (i > 0 ? "; " : "") + added[i];

        if (added.size() > k_game_disk_startup_lines)
            note += "; and " + int_to_string((int)(added.size() - k_game_disk_startup_lines)) + " more";

        note += std::string(". Lines the game needs go in ") + section + ".";
    }

    result->startup++;
    result->notes.push_back(note);
}

bool GameDisk::TakeLock()
{
#if defined(_WIN32)
    long self = (long)_getpid();
#elif defined(GAME_DISK_POSIX_LOCK)
    long self = (long)getpid();
#else
    long self = 1;
#endif
    std::string text = read_text(m_lock);

    if (!text.empty())
    {
        long owner = strtol(text.c_str(), NULL, 10);
        bool alive = false;

#if defined(GAME_DISK_POSIX_LOCK)
        alive = (owner > 0) && (owner != self) && ((kill((pid_t)owner, 0) == 0) || (errno == EPERM));
#endif

        if (alive)
            return false;

        if (owner != self)
            Log("Taking over the lock of a Geardox that is gone: %s", m_lock.c_str());
    }

    char pid[32];
    snprintf(pid, sizeof(pid), "%ld\n", self);
    m_locked = write_text(m_lock, pid);
    return m_locked;
}

void GameDisk::ReleaseLock()
{
    if (m_locked)
        MediaFile::RemoveFile(m_lock.c_str());

    m_locked = false;
}
