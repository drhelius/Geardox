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
#include "freedos.h"
#include "../media/media_file.h"
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
static const char k_game_disk_deleted[] = ".deleted";
static const char k_game_disk_conflicts[] = ".conflicts";
static const char k_game_disk_config_sys[] = "CONFIG.SYS";
static const char k_game_disk_autoexec[] = "AUTOEXEC.BAT";
static const u32 k_game_disk_startup_lines = 8;

// The names of C:'s root the system owns: a game's entry with one is left out, and DOS's writes there never go back
static const char* const k_game_disk_freedos_reserved[] =
{
    "KERNEL.SYS", "FDCONFIG.SYS", "FREEDOS", "TEMP", "CONFIG.SYS", "AUTOEXEC.BAT"
};

// MS-DOS's root files come from its version's table, these follow them
static const char* const k_game_disk_msdos_reserved[] = { "DOS", "GEARDOX", "TEMP", "CONFIG.SYS", "AUTOEXEC.BAT" };

static std::string upper(const std::string& text)
{
    std::string result(text);

    for (size_t i = 0; i < result.size(); i++)
        result[i] = ((result[i] >= 'a') && (result[i] <= 'z')) ? (char)(result[i] - 32) : result[i];

    return result;
}

static std::string slashes(const std::string& path)
{
    std::string result(path);
    std::replace(result.begin(), result.end(), '\\', '/');
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

static bool is_absolute(const std::string& path)
{
    return !path.empty() && ((path[0] == '/') || (path[0] == '\\') || ((path.size() > 1) && (path[1] == ':')));
}

static std::string directory_of(const std::string& path)
{
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash == 0 ? 1 : slash);
}

static std::string file_of(const std::string& path)
{
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
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

static std::string trim_trailing(const std::string& source)
{
    std::string path(source);

    while ((path.size() > 1) && ((path[path.size() - 1] == '/') || (path[path.size() - 1] == '\\')))
        path.resize(path.size() - 1);

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

static bool is_folder(const std::string& path)
{
    std::vector<std::string> folders;
    std::vector<std::string> files;
    return MediaFile::ListDirectory(path.c_str(), folders, files);
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
    if (is_folder(path))
        return true;

    std::string parent = directory_of(path);

    if ((parent != path) && (parent != ".") && !make_directories(parent))
        return false;

    return MediaFile::MakeDirectory(path.c_str());
}

static s64 dos_to_host_time(u16 date, u16 time_value)
{
    struct tm local;
    memset(&local, 0, sizeof(local));
    local.tm_year = ((date >> 9) & 0x7F) + 80;
    local.tm_mon = ((date >> 5) & 0x0F) - 1;
    local.tm_mday = date & 0x1F;
    local.tm_hour = (time_value >> 11) & 0x1F;
    local.tm_min = (time_value >> 5) & 0x3F;
    local.tm_sec = (time_value & 0x1F) * 2;
    local.tm_isdst = -1;
    return (s64)mktime(&local);
}

static bool is_scratch_path(const std::string& dos_path)
{
    return upper(dos_path).compare(0, 5, "TEMP\\") == 0;
}

static std::string read_text(const std::string& path)
{
    u64 size = 0;
    s64 modified = 0;

    if (!MediaFile::GetFileInfo(path.c_str(), &size, &modified))
        return "";

    MediaFile* file = MediaFile::OpenFile(path.c_str());

    if (!IsValidPointer(file))
        return "";

    std::string text((size_t)size, '\0');
    bool ok = (size == 0) || file->ReadAt(0, &text[0], size);
    SafeDelete(file);
    return ok ? text : std::string();
}

static bool replace_file(const std::string& temporary, const std::string& path)
{
    if (MediaFile::RenameFile(temporary.c_str(), path.c_str()))
        return true;

    MediaFile::RemoveFile(path.c_str());
    return MediaFile::RenameFile(temporary.c_str(), path.c_str());
}

static bool write_text(const std::string& path, const std::string& text)
{
    std::string temporary = path + ".tmp";
    MediaFile* file = MediaFile::OpenNewFile(temporary.c_str());

    if (!IsValidPointer(file))
        return false;

    bool ok = (text.empty() || file->WriteAt(0, text.data(), text.size())) && file->Flush();
    SafeDelete(file);
    ok = ok && replace_file(temporary, path);

    if (!ok)
        MediaFile::RemoveFile(temporary.c_str());

    return ok;
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

static size_t read_zip_file(void* opaque, mz_uint64 offset, void* buffer, size_t size)
{
    MediaFile* file = (MediaFile*)opaque;
    return file->ReadAt(offset, buffer, size) ? size : 0;
}

static bool open_zip(const char* path, mz_zip_archive* zip, MediaFile** file)
{
    memset(zip, 0, sizeof(*zip));
    *file = MediaFile::OpenFile(path);

    if (!IsValidPointer(*file))
        return false;

    zip->m_pRead = read_zip_file;
    zip->m_pIO_opaque = *file;

    if (!mz_zip_reader_init(zip, (mz_uint64)(*file)->GetSize(), 0))
    {
        SafeDelete(*file);
        return false;
    }

    return true;
}

static void close_zip(mz_zip_archive* zip, MediaFile** file)
{
    mz_zip_reader_end(zip);
    SafeDelete(*file);
}

static size_t write_zip_entry(void* opaque, mz_uint64 offset, const void* buffer, size_t size)
{
    MediaFile* file = (MediaFile*)opaque;
    return file->WriteAt(offset, buffer, size) ? size : 0;
}

static bool extract_zip_entry(mz_zip_archive* zip, mz_uint index, const std::string& path)
{
    std::string temporary = path + ".tmp";
    MediaFile* file = MediaFile::OpenNewFile(temporary.c_str());

    if (!IsValidPointer(file))
        return false;

    bool ok = mz_zip_reader_extract_to_callback(zip, index, write_zip_entry, file, 0) && file->Flush();
    SafeDelete(file);
    ok = ok && replace_file(temporary, path);

    if (!ok)
        MediaFile::RemoveFile(temporary.c_str());

    return ok;
}

static bool sort_found(const std::pair<std::string, GameDisk_Found>& a, const std::pair<std::string, GameDisk_Found>& b)
{
    return a.first < b.first;
}

GameDisk::GameDisk()
{
    m_open = false;
    m_source_zip = false;
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

    std::string source = trim_trailing(path);

    if (GameDescriptor::IsDescriptor(source.c_str()))
    {
        GameDisk_Found item;
        item.path = source;
        found->push_back(item);
        return true;
    }

    std::vector<std::pair<std::string, GameDisk_Found> > sorted;
    std::vector<std::string> folders;
    std::vector<std::string> files;

    if (ends_with_no_case(source.c_str(), ".zip"))
    {
        mz_zip_archive zip;
        MediaFile* file = NULL;

        if (!open_zip(source.c_str(), &zip, &file))
            return false;

        for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip); i++)
        {
            mz_zip_archive_file_stat stat;

            if (!mz_zip_reader_file_stat(&zip, i, &stat) || stat.m_is_directory)
                continue;

            std::string name = slashes(stat.m_filename);

            if ((name.find('/') == std::string::npos) && GameDescriptor::IsDescriptor(name.c_str()))
            {
                GameDisk_Found item;
                item.path = source;
                item.inner = name;
                sorted.push_back(std::make_pair(upper(name), item));
            }
        }

        close_zip(&zip, &file);
    }
    else if (MediaFile::ListDirectory(source.c_str(), folders, files))
    {
        for (size_t i = 0; i < files.size(); i++)
        {
            if ((files[i][0] == '.') || !GameDescriptor::IsDescriptor(files[i].c_str()))
                continue;

            GameDisk_Found item;
            item.path = join(source, files[i]);
            sorted.push_back(std::make_pair(upper(files[i]), item));
        }
    }

    std::sort(sorted.begin(), sorted.end(), sort_found);

    for (size_t i = 0; i < sorted.size(); i++)
        found->push_back(sorted[i].second);

    return !found->empty();
}

bool GameDisk::IsGameContent(const char* path)
{
    std::vector<GameDisk_Found> found;
    return Find(path, &found);
}

bool GameDisk::Open(const GameDisk_Found& found, const char* save_directory, const char* overrides_directory,
    bool write_to_folder, const char* msdos_path)
{
    Close();
    m_error.clear();
    m_msdos_path = (IsValidPointer(msdos_path) && exists(msdos_path)) ? std::string(msdos_path) : std::string();
    m_descriptor_path = found.path;
    m_save_directory = IsValidPointer(save_directory) ? trim_trailing(save_directory) : std::string();

    if (found.inner.empty())
    {
        if (!m_base.Load(found.path.c_str()))
            return Fail("%s: %s", file_of(found.path).c_str(), m_base.GetError());

        m_base_directory = directory_of(found.path);
    }
    else
    {
        mz_zip_archive zip;
        MediaFile* file = NULL;
        size_t size = 0;
        void* data = NULL;

        if (open_zip(found.path.c_str(), &zip, &file))
        {
            data = mz_zip_reader_extract_file_to_heap(&zip, found.inner.c_str(), &size, 0);
            close_zip(&zip, &file);
        }

        if (!IsValidPointer(data))
            return Fail("Unable to read %s in %s", found.inner.c_str(), file_of(found.path).c_str());

        std::string name = join(found.path, found.inner);
        bool parsed = m_base.Parse(name.c_str(), (const char*)data, size);
        mz_free(data);

        if (!parsed)
            return Fail("%s: %s", found.inner.c_str(), m_base.GetError());

        m_base_directory = directory_of(found.path);
    }

    const GDOX_Game_Info& base = m_base.GetInfo();

    if (!m_base.Has("game", "folder"))
        return Fail("%s: no folder in [game], the game's folder on C: such as C:\\GAME, or C:\\ for the root",
            file_of(found.inner.empty() ? found.path : found.inner).c_str());

    if (base.source.empty())
        m_source = found.inner.empty() ? m_base_directory : found.path;
    else
        m_source = trim_trailing(is_absolute(base.source) ? base.source : join(m_base_directory, base.source));

    if (!exists(m_source))
        return Fail("The game's folder or ZIP isn't there: %s", m_source.c_str());

    m_source_zip = ends_with_no_case(m_source.c_str(), ".zip");
    char key[16];
    std::string identity = found.path + "|" + found.inner;
    snprintf(key, sizeof(key), "%08X", (u32)mz_crc32(0, (const u8*)identity.data(), identity.size()));
    std::string label = safe_name(base.name);

    if (IsValidPointer(overrides_directory) && (overrides_directory[0] != 0))
    {
        m_overrides_path = join(trim_trailing(overrides_directory), label + "." + key + ".gdox");
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
    snprintf(key, sizeof(key), "%08X", (u32)mz_crc32(0, (const u8*)m_source.data(), m_source.size()));
    std::string source_name = file_of(m_source);

    if (ends_with_no_case(source_name.c_str(), ".zip"))
        source_name.resize(source_name.size() - 4);

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
    m_image.clear();
    m_manifest.clear();
    m_lock.clear();
    m_overlay.clear();
    m_msdos_path.clear();
    m_msdos.zip.clear();
    m_msdos.name.clear();
    m_msdos_loaded = false;

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

    if (!make_directories(directory_of(m_overrides_path)) || !overrides.WriteFile(m_overrides_path.c_str()))
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

const GameDescriptor& GameDisk::GetOverrides() const
{
    return m_overrides;
}

// The descriptor's machine, the CD drive moved off the game's hard disks, and RAM enough for its EMS when it names
// none and the user's is too small
void GameDisk::GetGameSettings(const GDOX_Machine_Settings& user, GDOX_Machine_Settings* game) const
{
    GetGameSettings(m_descriptor, user, game, GetSystem());
}

// §7.1 of docs/27: the game's own choice, then MS-DOS when the user imported it, then FreeDOS
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

// §5.4 steps 2 and 3: write back what a session left open, then build or reuse. Touches nothing of the emulator's
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

        m_cdroms.push_back(path);
    }

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        if (info.hard_disks[i].empty())
            continue;

        m_hard_disks[i] = is_absolute(info.hard_disks[i]) ? info.hard_disks[i] : join(m_base_directory, info.hard_disks[i]);

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

// §5.4 step 6, after the machine has flushed and let go of the disk
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

// FreeDOS's lines from its kernel's config.txt and JEMM's Readme.txt (R15 §4.3), MS-DOS's from its own HELP
// (docs/reference text/dos/msdos_6.22, docs/27 §6.3)
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

    // SWITCHES=/F came with MS-DOS 6.0, and HIMEM's memory test, with /TESTMEM to skip it, with 6.2 (docs/27 §19)
    if (msdos)
        out += m_msdos.version >= 600 ? "SWITCHES=/F\r\nLASTDRIVE=Z\r\n" : "LASTDRIVE=Z\r\n";
    else
        out += "SWITCHES=/F\r\nLASTDRIVE=Z\r\nIDLEHALT=1\r\n";

    snprintf(line, sizeof(line), "FILES=%d\r\nBUFFERS=%d\r\n", info.files, info.buffers);
    out += line;

    if (!info.dos_version.empty() && !msdos)
        out += "VERSION=" + info.dos_version + "\r\n";

    if (msdos)
    {
        if (info.memory != GDOX_GAME_MEMORY_NONE)
        {
            out += info.memory == GDOX_GAME_MEMORY_XMS ? "DOS=HIGH\r\n" : "DOS=HIGH,UMB\r\n";
            out += m_msdos.version >= 620 ? "DEVICE=C:\\DOS\\HIMEM.SYS /TESTMEM:OFF\r\n" : "DEVICE=C:\\DOS\\HIMEM.SYS\r\n";
        }

        if (info.memory == GDOX_GAME_MEMORY_UMB)
            out += "DEVICE=C:\\DOS\\EMM386.EXE NOEMS\r\n";
        else if ((info.memory == GDOX_GAME_MEMORY_EMS) && (m_msdos.version >= 600))
            out += "DEVICE=C:\\DOS\\EMM386.EXE RAM" + frame + "\r\n";
        else if (info.memory == GDOX_GAME_MEMORY_EMS)
        {
            // MS-DOS 5's EMM386 sets its EMS aside as it loads, 256K unless told: the game's, or all it finds (docs/27 §19)
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

    if (msdos)
        out += "SHELL=C:\\COMMAND.COM C:\\ /E:1024 /P\r\n";
    else
        out += "SHELL=C:\\FREEDOS\\BIN\\COMMAND.COM C:\\FREEDOS\\BIN /E:1024 /P\r\n";
    return out;
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

    *out += "CD \\" + under(base, where) + "\r\n";
    *out += std::string(batch ? "CALL " : "") + (upper(where) == upper(directory) ? program : "C:\\" + under(base, target));
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

    if (msdos)
        out += "@ECHO OFF\r\nSET PATH=C:\\DOS;C:\\GEARDOX\r\nSET TEMP=C:\\TEMP\r\n";
    else
        out += "@ECHO OFF\r\nSET DOSDIR=C:\\FREEDOS\r\nSET PATH=C:\\FREEDOS\\BIN\r\nSET TEMP=C:\\TEMP\r\n";

    out += "SET DIRCMD=/OGN\r\n";

    if ((card.dsp_major != 0) && IsValidPointer(k_types[machine.sound_card]))
    {
        snprintf(line, sizeof(line), "SET BLASTER=A220 I%d D%d%s T%s\r\n", machine.sound_irq, machine.sound_dma,
            card.mpu ? (machine.mpu ? " H5 P330" : " H5") : "", k_types[machine.sound_card]);
        out += line;
    }

    out += "PROMPT $P$G\r\n";

    // KEYB with the VGA's code page 437, from MS-DOS's HELP KEYB and FreeDOS's KEYB documents (docs/27 §18)
    if (strcmp(GetKeyboardLayout(), k_keyboard_layouts[0].id) != 0)
    {
        std::string layout = upper(GetKeyboardLayout());
        out += "LH KEYB " + layout + (msdos ? ",437,C:\\DOS\\KEYBOARD.SYS\r\n" : ",437,C:\\FREEDOS\\BIN\\KEYBOARD.SYS\r\n");
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

    out += "GDXRUN\r\nIF ERRORLEVEL 2 GOTO PROMPT\r\nIF ERRORLEVEL 1 GOTO SETUP\r\n";

    if (!info.exe.empty())
    {
        add_run(&out, info.folder, info.exe, info.cwd, info.args);

        if (info.exit == GDOX_GAME_EXIT_POWEROFF)
            out += "POWEROFF\r\n";
    }

    out += "GOTO PROMPT\r\n:SETUP\r\n";

    if (!info.setup.empty())
        add_run(&out, info.folder, info.setup, "", "");
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
    std::string path = upper(dos_path);

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
    std::string base = upper(m_descriptor.GetInfo().folder);
    std::string path = upper(dos_path);

    if (IsSystemPath(path))
        return false;

    return base.empty() || ((path.size() > base.size()) && (path.compare(0, base.size(), base) == 0) &&
        (path[base.size()] == '\\'));
}

bool GameDisk::Fail(const char* format, ...)
{
    char text[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    m_error = text;
    Log("%s", text);
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

    std::string top = upper(info.folder.substr(0, info.folder.find('\\')));

    if (!top.empty() && (std::find(system->reserved.begin(), system->reserved.end(), top) != system->reserved.end()))
        return Fail("The game's folder, C:\\%s, is the system's: give it another folder in [game]", info.folder.c_str());

    system->config_sys = GetConfigSys(machine);
    system->autoexec = GetBatch(machine);

    if (!image->AddGame(m_source.c_str(), info.folder.c_str()))
        return Fail("Unable to read the game's files: %s", m_source.c_str());

    if (is_folder(m_overlay) && !image->AddOverlay(m_overlay.c_str()))
        return Fail("Unable to read the game's saves: %s", m_overlay.c_str());

    if (msdos && !image->AddMSDOS((const u8*)m_msdos.zip.data(), m_msdos.zip.size(), info.system_hidden,
        system->reserved, pinned, !HasCDExtension()))
        return Fail("Unable to add %s to the game's disk", m_msdos.name.c_str());

    if (!msdos && !image->AddFreeDOS(info.system_hidden, system->reserved))
        return Fail("Unable to add FreeDOS to the game's disk");

    if (!image->AddMemoryFile(k_game_disk_config_sys, system->config_sys, attributes) ||
        !image->AddMemoryFile(k_game_disk_autoexec, system->autoexec, attributes))
        return Fail("Unable to add the game's configuration");

    std::string label = m_descriptor.GetInfo().name;
    image->FinishGame(label.c_str(), (u64)info.free_mb * 1024 * 1024, msdos ? m_msdos.boot : k_freedos_boot);
    return true;
}

// Relative to the descriptor's folder, then inside the game's ZIP, taken out to the media folder
bool GameDisk::ResolveMedia(const std::string& value, std::string* path)
{
    *path = is_absolute(value) ? value : join(m_base_directory, value);

    if (exists(*path))
        return true;

    if (m_source_zip && !is_absolute(value) && ExtractFromZip(slashes(value), path))
        return true;

    return Fail("%s isn't there: %s", m_descriptor.GetInfo().name.c_str(), value.c_str());
}

// A CUE takes its folder in the ZIP with it, for its tracks
bool GameDisk::ExtractFromZip(const std::string& inner, std::string* path)
{
    mz_zip_archive zip;
    MediaFile* file = NULL;

    if (!open_zip(m_source.c_str(), &zip, &file))
        return false;

    std::string wanted = upper(inner);
    std::string folder = upper(directory_of(inner));
    folder = folder == "." ? std::string() : folder + "/";
    bool cue = ends_with_no_case(inner.c_str(), ".cue");
    bool found = false;
    bool ok = make_directories(m_media_directory);

    for (mz_uint i = 0; ok && (i < mz_zip_reader_get_num_files(&zip)); i++)
    {
        mz_zip_archive_file_stat stat;

        if (!mz_zip_reader_file_stat(&zip, i, &stat) || stat.m_is_directory)
            continue;

        std::string name = slashes(stat.m_filename);
        std::string key = upper(name);
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
            Log("Taking %s out of %s", name.c_str(), file_of(m_source).c_str());
            ok = make_directories(directory_of(out)) && extract_zip_entry(&zip, i, out);
        }

        if (wanted_file)
        {
            *path = out;
            found = true;
        }
    }

    close_zip(&zip, &file);
    return ok && found;
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

    // A manifest from before R23 stops at its entries, and its disk is FreeDOS's
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

    if (!make_directories(directory_of(path)) || !image->CopyOut(file, temporary.c_str()))
    {
        MediaFile::RemoveFile(temporary.c_str());
        return false;
    }

    MediaFile::RemoveFile(path.c_str());
    return MediaFile::RenameFile(temporary.c_str(), path.c_str());
}

void GameDisk::LoadDeleted()
{
    m_deleted.clear();
    std::string text = read_text(join(m_overlay, k_game_disk_deleted));
    size_t start = 0;

    while (start < text.size())
    {
        size_t end = text.find('\n', start);
        end = end == std::string::npos ? text.size() : end;
        std::string line = text.substr(start, end - start);
        start = end + 1;

        if (!line.empty() && (line[line.size() - 1] == '\r'))
            line.resize(line.size() - 1);

        if (!line.empty())
            m_deleted.push_back(line);
    }
}

bool GameDisk::SaveDeleted()
{
    std::string path = join(m_overlay, k_game_disk_deleted);

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
        if (upper(m_deleted[i]) == upper(host_path))
            return true;
    }

    return false;
}

void GameDisk::SetDeleted(const std::string& host_path, bool deleted)
{
    for (size_t i = m_deleted.size(); i > 0; i--)
    {
        if (upper(m_deleted[i - 1]) == upper(host_path))
            m_deleted.erase(m_deleted.begin() + (i - 1));
    }

    if (deleted)
        m_deleted.push_back(host_path);
}

// §5.5. The image is read whole first and refused if it doesn't add up; then each change goes to the folder or the
// overlay, and nothing that changed on the host since the build is overwritten
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
        found[upper(files[i].path)] = i;

    for (size_t i = 0; i < entries.size(); i++)
    {
        known[upper(entries[i].dos_path)] = i;

        if (entries[i].directory)
            folders[upper(entries[i].dos_path)] = entries[i].host_path;
    }

    std::string base = m_descriptor.GetInfo().folder;

    if (!base.empty())
        folders[upper(base)] = "";

    std::vector<GameDisk_Manifest_Entry> updated;

    for (size_t i = 0; i < entries.size(); i++)
    {
        const GameDisk_Manifest_Entry& entry = entries[i];
        std::map<std::string, size_t>::const_iterator it = found.find(upper(entry.dos_path));
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

            if (!make_directories(directory_of(destination)) || !image.CopyOut(*file, temporary.c_str()) ||
                !replace_file(temporary, destination))
            {
                MediaFile::RemoveFile(temporary.c_str());
                result->kept++;
                result->notes.push_back("Unable to write " + destination);
                continue;
            }

            MediaFile::SetModified(destination.c_str(), dos_to_host_time(file->date, file->time));

            if (to_overlay && IsDeleted(entry.host_path))
            {
                SetDeleted(entry.host_path, false);
                deleted_changed = true;
            }

            written_hosts[upper(entry.dos_path)] = entry.host_path;
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

        if (!entry.directory || entry.system || (found.count(upper(entry.dos_path)) != 0))
            continue;

        std::string destination = entry.overlay ? join(m_overlay, entry.host_path) : GetDestination(entry.host_path);

        if (is_folder(destination) && !MediaFile::RemoveDirectory(destination.c_str()))
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
        std::string key = upper(file.path);

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
        std::string parent = slash == std::string::npos ? std::string() : upper(file.path.substr(0, slash));
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
        bool taken = false;
        MediaFile::ListDirectory(directory_of(destination).c_str(), host_folders, host_files);

        for (size_t f = 0; f < host_files.size(); f++)
            taken = taken || (upper(host_files[f]) == upper(name));

        for (size_t f = 0; f < host_folders.size(); f++)
            taken = taken || (upper(host_folders[f]) == upper(name));

        if (taken)
        {
            result->conflicts++;
            result->notes.push_back("A new file has the name of one on the host, the guest's copy is in the overlay's "
                ".conflicts: " + host_path);
            WriteConflict(&image, file, host_path);
            continue;
        }

        std::string temporary = destination + ".tmp";

        if (!make_directories(directory_of(destination)) || !image.CopyOut(file, temporary.c_str()) ||
            !MediaFile::RenameFile(temporary.c_str(), destination.c_str()))
        {
            MediaFile::RemoveFile(temporary.c_str());
            result->kept++;
            result->notes.push_back("Unable to write " + destination);
            continue;
        }

        MediaFile::SetModified(destination.c_str(), dos_to_host_time(file.date, file.time));

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
        result->notes.push_back("Unable to write " + join(m_overlay, k_game_disk_deleted));
    }

    for (size_t i = 0; i < files.size(); i++)
    {
        const FatImage_File& file = files[i];
        std::string key = upper(file.path);
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

static std::string trim_line(const std::string& line)
{
    size_t start = line.find_first_not_of(" \t\r\x1A");

    if (start == std::string::npos)
        return "";

    return line.substr(start, line.find_last_not_of(" \t\r\x1A") - start + 1);
}

// §6.5 of docs/27: the next launch writes CONFIG.SYS and AUTOEXEC.BAT again, so the lines DOS added are named
void GameDisk::NoteStartupFile(FatImage* image, const FatImage_File* file, const GameDisk_Manifest_Entry& entry, GameDisk_Result* result)
{
    std::string name = upper(entry.dos_path);
    bool config = name == k_game_disk_config_sys;

    if (!config && (name != k_game_disk_autoexec))
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
    size_t start = 0;

    while (start < generated.size())
    {
        size_t end = generated.find('\n', start);
        end = end == std::string::npos ? generated.size() : end;
        known.push_back(upper(trim_line(generated.substr(start, end - start))));
        start = end + 1;
    }

    start = 0;

    while (start < data.size())
    {
        size_t end = data.find('\n', start);
        end = end == std::string::npos ? data.size() : end;
        std::string line = trim_line(data.substr(start, end - start));
        start = end + 1;

        if (!line.empty() && (std::find(known.begin(), known.end(), upper(line)) == known.end()))
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
