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

#include <algorithm>
#include <map>
#include <sstream>
#include "game_database.h"
#include "game_db.h"
#include "media_file.h"
#include "crc.h"

static const int k_game_database_max_depth = 8;
static const size_t k_game_database_max_files = 50000;
static const size_t k_game_database_crc_block = 64 * 1024;

typedef std::map<std::string, const GameDatabase_File*> GameDatabase_View;

static std::string upper_path(const std::string& path)
{
    std::string out(path);

    for (size_t i = 0; i < out.size(); i++)
    {
        if (out[i] == '\\')
            out[i] = '/';
        else if (out[i] >= 'a' && out[i] <= 'z')
            out[i] = (char)(out[i] - 'a' + 'A');
    }

    return out;
}

static bool ends_with_path(const std::string& path, const std::string& tail)
{
    if (path == tail)
        return true;

    return (path.size() > tail.size()) && (path[path.size() - tail.size() - 1] == '/') &&
        (path.compare(path.size() - tail.size(), tail.size(), tail) == 0);
}

static const std::vector<u32>& candidate_sizes()
{
    static std::vector<u32> sizes;

    if (sizes.empty())
    {
        for (int i = 0; i < k_game_database_file_count; i++)
            sizes.push_back(k_game_database_files[i].size);

        std::sort(sizes.begin(), sizes.end());
        sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
    }

    return sizes;
}

static bool crc_of_file(const std::string& path, u32* crc)
{
    MediaFile* file = MediaFile::OpenFile(path.c_str());

    if (!IsValidPointer(file))
        return false;

    std::vector<u8> block(k_game_database_crc_block);
    u32 value = 0;
    bool ok = true;

    while (true)
    {
        s64 read = file->Read(&block[0], block.size());

        if (read < 0)
        {
            ok = false;
            break;
        }

        if (read == 0)
            break;

        value = CalculateCRC32(value, &block[0], (size_t)read);
    }

    SafeDelete(file);
    *crc = value;
    return ok;
}

static bool list_folder(const std::string& host, const std::string& relative, int depth,
    std::vector<GameDatabase_File>* files)
{
    std::vector<std::string> folders;
    std::vector<std::string> names;

    if (!MediaFile::ListDirectory(host.c_str(), folders, names))
        return false;

    for (size_t i = 0; i < names.size(); i++)
    {
        if (names[i].empty() || names[i][0] == '.')
            continue;

        std::string full = host + "/" + names[i];
        u64 size = 0;
        s64 modified = 0;

        if (!MediaFile::GetFileInfo(full.c_str(), &size, &modified) || size > 0xFFFFFFFFULL)
            continue;

        GameDatabase_File file;
        file.path = relative + names[i];
        file.size = (u32)size;
        file.crc = 0;
        file.has_crc = GameDatabase::IsCandidate(file.size) && crc_of_file(full, &file.crc);
        files->push_back(file);

        if (files->size() > k_game_database_max_files)
            return false;
    }

    if (depth >= k_game_database_max_depth)
        return true;

    for (size_t i = 0; i < folders.size(); i++)
    {
        if (folders[i].empty() || folders[i][0] == '.')
            continue;

        std::string full = host + "/" + folders[i];

        if (MediaFile::IsFolderLink(full.c_str()))
            continue;

        if (!list_folder(full, relative + folders[i] + "/", depth + 1, files))
            return false;
    }

    return true;
}

static size_t read_zip(void* opaque, mz_uint64 offset, void* buffer, size_t size)
{
    MediaFile* file = (MediaFile*)opaque;
    return file->ReadAt(offset, buffer, size) ? size : 0;
}

static bool list_zip(const char* path, std::vector<GameDatabase_File>* files)
{
    MediaFile* file = MediaFile::OpenFile(path);

    if (!IsValidPointer(file))
        return false;

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    zip.m_pRead = read_zip;
    zip.m_pIO_opaque = file;

    if (!mz_zip_reader_init(&zip, (mz_uint64)file->GetSize(), 0))
    {
        SafeDelete(file);
        return false;
    }

    mz_uint count = mz_zip_reader_get_num_files(&zip);

    for (mz_uint i = 0; (i < count) && (files->size() <= k_game_database_max_files); i++)
    {
        mz_zip_archive_file_stat stat;

        if (!mz_zip_reader_file_stat(&zip, i, &stat) || stat.m_is_directory || (stat.m_uncomp_size > 0xFFFFFFFFULL))
            continue;

        GameDatabase_File item;
        item.path = stat.m_filename;
        item.size = (u32)stat.m_uncomp_size;
        item.crc = (u32)stat.m_crc32;
        item.has_crc = true;
        files->push_back(item);
    }

    mz_zip_reader_end(&zip);
    SafeDelete(file);
    return files->size() <= k_game_database_max_files;
}

static void apply_layout(const GameDatabase_View& base, const std::vector<GameDatabase_Rule>& rules,
    GameDatabase_View* view)
{
    *view = base;

    for (size_t r = 0; r < rules.size(); r++)
    {
        const GameDatabase_Rule& rule = rules[r];

        for (GameDatabase_View::const_iterator it = base.begin(); it != base.end(); ++it)
        {
            const std::string& path = it->first;

            if (rule.file)
            {
                if (ends_with_path(path, rule.source))
                    (*view)[path.substr(0, path.size() - rule.source.size()) + rule.target] = it->second;

                continue;
            }

            std::string segment = rule.source + "/";
            size_t position = 0;

            while ((position = path.find(segment, position)) != std::string::npos)
            {
                if ((position == 0) || (path[position - 1] == '/'))
                {
                    std::string mapped = path.substr(0, position) + (rule.target.empty() ? std::string() :
                        rule.target + "/") + path.substr(position + segment.size());

                    if (view->find(mapped) == view->end())
                        (*view)[mapped] = it->second;
                }

                position++;
            }
        }
    }
}

static bool has_file(const GameDatabase_View& view, const std::string& path, const GDOX_DB_File& wanted)
{
    GameDatabase_View::const_iterator it = view.find(path);

    return (it != view.end()) && it->second->has_crc && (it->second->size == wanted.size) &&
        (it->second->crc == wanted.crc);
}

static void match_release(int release, const GameDatabase_View& view, std::vector<GameDatabase_Match>* matches)
{
    const GDOX_DB_Release& info = k_game_database_releases[release];
    const GDOX_DB_File& anchor = k_game_database_files[info.first_file];
    std::string anchor_path(anchor.path);

    for (GameDatabase_View::const_iterator it = view.begin(); it != view.end(); ++it)
    {
        const GameDatabase_File* file = it->second;

        if (!file->has_crc || (file->size != anchor.size) || (file->crc != anchor.crc) ||
            !ends_with_path(it->first, anchor_path))
            continue;

        std::string root = it->first.substr(0, it->first.size() - anchor_path.size());
        bool all = true;

        for (int i = 1; all && (i < info.file_count); i++)
        {
            const GDOX_DB_File& wanted = k_game_database_files[info.first_file + i];
            all = has_file(view, root + wanted.path, wanted);
        }

        if (!all)
            continue;

        bool preset = true;

        for (int i = 0; preset && (i < info.when_count); i++)
        {
            const GDOX_DB_File& wanted = k_game_database_files[info.first_file + info.file_count + i];
            preset = has_file(view, root + wanted.path, wanted);
        }

        bool voodoo = false;

        for (int i = 0; !voodoo && (i < info.voodoo_count); i++)
        {
            const GDOX_DB_File& wanted = k_game_database_files[info.first_file + info.file_count + info.when_count + i];
            voodoo = has_file(view, root + wanted.path, wanted);
        }

        GameDatabase_Match match;
        match.entry = info.entry;
        match.release = release;
        match.root = root;
        match.preset = preset;
        match.voodoo = voodoo;
        matches->push_back(match);
    }
}

static bool better_match(const GameDatabase_Match& a, const GameDatabase_Match& b)
{
    if (a.entry != b.entry)
        return a.entry < b.entry;

    if (a.root != b.root)
        return a.root < b.root;

    if (a.preset != b.preset)
        return a.preset;

    return a.release < b.release;
}

static bool same_game(const GameDatabase_Match& a, const GameDatabase_Match& b)
{
    return (a.entry == b.entry) && (a.root == b.root);
}

bool GameDatabase::Find(const char* path, std::vector<GameDatabase_Match>* matches)
{
    matches->clear();

    std::vector<GameDatabase_File> files;

    if (!List(path, &files))
        return false;

    Match(files, matches);
    return !matches->empty();
}

bool GameDatabase::List(const char* path, std::vector<GameDatabase_File>* files)
{
    files->clear();

    if (!IsValidPointer(path) || (path[0] == '\0'))
        return false;

    std::vector<std::string> folders;
    std::vector<std::string> names;

    if (MediaFile::ListDirectory(path, folders, names))
        return list_folder(path, std::string(), 0, files);

    std::string name = upper_path(path);

    if ((name.size() > 4) && (name.compare(name.size() - 4, 4, ".ZIP") == 0))
        return list_zip(path, files);

    return false;
}

void GameDatabase::Match(const std::vector<GameDatabase_File>& files, std::vector<GameDatabase_Match>* matches)
{
    matches->clear();

    GameDatabase_View base;

    for (size_t i = 0; i < files.size(); i++)
        base[upper_path(files[i].path)] = &files[i];

    std::vector<bool> nominated(k_game_database_release_count, false);

    for (size_t i = 0; i < files.size(); i++)
    {
        if (!files[i].has_crc)
            continue;

        int low = 0;
        int high = k_game_database_key_count;

        while (low < high)
        {
            int middle = (low + high) / 2;
            const GDOX_DB_Key& key = k_game_database_keys[middle];

            if ((key.size < files[i].size) || ((key.size == files[i].size) && (key.crc < files[i].crc)))
                low = middle + 1;
            else
                high = middle;
        }

        for (int k = low; (k < k_game_database_key_count) && (k_game_database_keys[k].size == files[i].size) &&
            (k_game_database_keys[k].crc == files[i].crc); k++)
            nominated[k_game_database_keys[k].release] = true;
    }

    for (int r = 0; r < k_game_database_release_count; r++)
    {
        if (!nominated[r])
            continue;

        std::vector<GameDatabase_Rule> rules;
        GetLayout(r, &rules);

        if (rules.empty())
        {
            match_release(r, base, matches);
            continue;
        }

        GameDatabase_View view;
        apply_layout(base, rules, &view);
        match_release(r, view, matches);
    }

    std::sort(matches->begin(), matches->end(), better_match);
    matches->erase(std::unique(matches->begin(), matches->end(), same_game), matches->end());

    for (size_t i = 0; i < matches->size(); i++)
    {
        GameDatabase_Match& match = (*matches)[i];

        for (size_t f = 0; f < files.size(); f++)
        {
            std::string path = upper_path(files[f].path);

            if ((path.size() >= match.root.size()) && (path.compare(0, match.root.size(), match.root) == 0))
            {
                match.root = files[f].path.substr(0, match.root.size());
                break;
            }
        }
    }
}

bool GameDatabase::IsCandidate(u32 size)
{
    const std::vector<u32>& sizes = candidate_sizes();
    return std::binary_search(sizes.begin(), sizes.end(), size);
}

int GameDatabase::GetEntryCount()
{
    return k_game_database_entry_count;
}

int GameDatabase::FindEntry(const char* id)
{
    for (int i = 0; IsValidPointer(id) && (i < k_game_database_entry_count); i++)
    {
        if (strcmp(k_game_database_entries[i].id, id) == 0)
            return i;
    }

    return -1;
}

const char* GameDatabase::GetId(int entry)
{
    return ((entry >= 0) && (entry < k_game_database_entry_count)) ? k_game_database_entries[entry].id : NULL;
}

const char* GameDatabase::GetName(int entry)
{
    return ((entry >= 0) && (entry < k_game_database_entry_count)) ? k_game_database_entries[entry].name : NULL;
}

const char* GameDatabase::GetDescriptor(int entry)
{
    return ((entry >= 0) && (entry < k_game_database_entry_count)) ? k_game_database_entries[entry].descriptor :
        NULL;
}

int GameDatabase::GetEntry(int release)
{
    return ((release >= 0) && (release < k_game_database_release_count)) ? k_game_database_releases[release].entry :
        -1;
}

const char* GameDatabase::GetSettings(int release)
{
    return ((release >= 0) && (release < k_game_database_release_count)) ?
        k_game_database_releases[release].settings : NULL;
}

void GameDatabase::GetLayout(int release, std::vector<GameDatabase_Rule>* rules)
{
    rules->clear();

    if ((release < 0) || (release >= k_game_database_release_count) ||
        !IsValidPointer(k_game_database_releases[release].layout))
        return;

    std::string text(k_game_database_releases[release].layout);
    size_t start = 0;

    while (start <= text.size())
    {
        size_t end = text.find(';', start);

        if (end == std::string::npos)
            end = text.size();

        std::string item = text.substr(start, end - start);
        size_t arrow = item.find('>');

        if (arrow != std::string::npos)
        {
            GameDatabase_Rule rule;
            rule.source = item.substr(0, arrow);
            rule.target = item.substr(arrow + 1);

            if (rule.target == ".")
                rule.target.clear();

            size_t slash = rule.source.find_last_of('/');
            std::string last = (slash == std::string::npos) ? rule.source : rule.source.substr(slash + 1);
            rule.file = (last.find('.') != std::string::npos) && !rule.target.empty();
            rules->push_back(rule);
        }

        start = end + 1;
    }
}

static bool hex_bytes(const std::string& text, std::string* bytes)
{
    bytes->clear();

    if ((text.size() % 2) != 0)
        return false;

    for (size_t i = 0; i < text.size(); i += 2)
    {
        char* end = NULL;
        std::string pair = text.substr(i, 2);
        long value = strtol(pair.c_str(), &end, 16);

        if (*end != '\0')
            return false;

        bytes->push_back((char)value);
    }

    return !bytes->empty();
}

void GameDatabase::GetPatches(int release, std::vector<GameDatabase_Patch>* patches)
{
    patches->clear();

    if ((release < 0) || (release >= k_game_database_release_count) ||
        !IsValidPointer(k_game_database_releases[release].patches))
        return;

    std::string text(k_game_database_releases[release].patches);
    size_t start = 0;

    while (start <= text.size())
    {
        size_t end = text.find(';', start);

        if (end == std::string::npos)
            end = text.size();

        std::istringstream item(text.substr(start, end - start));
        std::string offset;
        std::string before;
        std::string after;
        GameDatabase_Patch patch;

        if ((item >> patch.path >> offset >> before >> after) && hex_bytes(before, &patch.before) &&
            hex_bytes(after, &patch.after) && (patch.before.size() == patch.after.size()))
        {
            patch.offset = (u32)strtoul(offset.c_str(), NULL, 16);
            patches->push_back(patch);
        }

        start = end + 1;
    }
}
