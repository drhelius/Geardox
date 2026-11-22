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
#include <time.h>
#include <algorithm>
#include <set>
#include "dos_import.h"
#include "floppy_image.h"
#include "ms_compress.h"
#include "dos_fs.h"
#include "../media/media_file.h"
#include "../media/media_zip.h"
#include "import_common.h"
#include "../media/crc.h"

static const u32 k_dos_max_image_size = 16 * 1024 * 1024;
static const u32 k_dos_max_zip_size = 64 * 1024 * 1024;
static const size_t k_dos_max_images = 16;
static const u32 k_dos_kwaj_max_size = 8 * 1024 * 1024;

// MS-DOS 6's Setup disks, whatever the version or the language: PACKING.LST names every file Setup installs, the
// compressed ones in KWAJ, and DOSSETUP.INI's [dif-path] puts these in the root. MS-DOS 5's compress in SZDD, list
// each disk's names only as stored, and have the real ones in SETUP.INI and PACKING.LST's install layout
static const char k_dos_names_list[] = "PACKING.LST";
static const char* const k_dos_boot_files[] = { "IO.SYS", "MSDOS.SYS", NULL };
static const char* const k_dos_root_files[] = { "WINA20.386", NULL };
static const char* const k_dos_both_files[] = { "COMMAND.COM", NULL };
static const char* const k_dos_left_out_6[] = { "PACKING.LST", "SETUP.MSG", k_dos_config_sys, k_dos_autoexec_bat, NULL };
static const char* const k_dos_left_out_5[] =
{
    "SETUP.EXE", "SETUP.INI", "CV.COM", "README.1ST", k_dos_config_sys, k_dos_autoexec_bat, NULL
};
static const char* const k_dos_shell_copies[] = { "VGA.VID", "DOSSHELL.VID", "VGA.GRB", "DOSSHELL.GRB", "EGA.INI", "DOSSHELL.INI", NULL };
static const char* const k_dos_shell_adapters[] =
{
    "MONO.VID", "CGA.VID", "EGA.VID", "VGA.VID", "HERC.VID", "8514.VID", "MONO.GRB", "CGA.GRB", "EGA.GRB", "EGAMONO.GRB",
    "VGA.GRB", "VGAMONO.GRB", "HERC.GRB", "MONO.INI", "CGA.INI", "EGA.INI", NULL
};
static const char* const k_dos_renames[] = { "WNTOOLS.GRB", "WNTOOLS.GRP", NULL };
static const char* const k_dos_required[] = { "HIMEM.SYS", "EMM386.EXE", NULL };
static const char k_dos_cd_extension[] = "MSCDEX.EXE";
static const char k_dos_cache[] = "SMARTDRV.EXE";
static const char k_dos_family_6[] = "msdos6";
static const char k_dos_family_5[] = "msdos5";
static const char k_dos_setup_ini_5[] = "SETUP.INI";

enum Dos_Place
{
    DOS_PLACE_BOOT = 0,
    DOS_PLACE_ROOT,
    DOS_PLACE_BOTH,
    DOS_PLACE_DOS
};

struct Dos_Listed
{
    std::string stored;
    std::string name;
    int disk;
};

static bool in_list(const char* const* names, const std::string& name)
{
    for (int i = 0; IsValidPointer(names[i]); i++)
    {
        if (name == names[i])
            return true;
    }

    return false;
}

static Dos_Place get_place(const std::string& name)
{
    if (in_list(k_dos_boot_files, name))
        return DOS_PLACE_BOOT;

    if (in_list(k_dos_root_files, name))
        return DOS_PLACE_ROOT;

    return in_list(k_dos_both_files, name) ? DOS_PLACE_BOTH : DOS_PLACE_DOS;
}

// A disk's heading has its number after a # (6.x, "Setup Disk #1", "DISQUETE #1") or alone after a word (5.0,
// "DISK 1"), whatever the language. MS-DOS 6 lists a file as its stored and its expanded name, MS-DOS 5 as one name
static void parse_names(const std::string& text, std::vector<Dos_Listed>* pairs, std::vector<Dos_Listed>* singles)
{
    std::vector<std::string> lines;
    int disk = 0;
    split_string(text, '\n', &lines);

    for (size_t i = 0; i < lines.size(); i++)
    {
        const std::string& line = lines[i];
        size_t hash = line.find('#');
        char first[64] = "";
        char second[64] = "";
        char third[8];
        int count = sscanf(line.c_str(), "%63s %63s %7s", first, second, third);

        if ((hash != std::string::npos) && (hash + 1 < line.size()) && (line[hash + 1] >= '1') && (line[hash + 1] <= '9'))
            disk = line[hash + 1] - '0';
        else if ((count == 2) && (strlen(second) == 1) && (second[0] >= '1') && (second[0] <= '9') &&
            !dos_is_short_name(to_upper_ascii(first), true))
            disk = second[0] - '0';

        Dos_Listed item;
        item.stored = to_upper_ascii(first);
        item.name = count == 2 ? to_upper_ascii(second) : item.stored;
        item.disk = disk;

        if ((count == 2) && dos_is_short_name(item.stored, true) && dos_is_short_name(item.name, true))
            pairs->push_back(item);
        else if ((count == 1) && dos_is_short_name(item.stored, true))
            singles->push_back(item);
    }
}

// Every 8.3 name in a Setup file of MS-DOS 5, which isn't all text
static void add_names(const std::string& text, std::set<std::string>* names)
{
    std::string token;

    for (size_t i = 0; i <= text.size(); i++)
    {
        char c = i < text.size() ? text[i] : ' ';
        bool part = (c == '.') || dos_is_name_character(to_upper_ascii(c));

        if (part)
        {
            token += c;
            continue;
        }

        if (dos_is_short_name(to_upper_ascii(token), true))
            names->insert(to_upper_ascii(token));

        token.clear();
    }
}

// MS-DOS 5's stored names lose their last letter to an underscore, and the SZDD header doesn't keep it: the real name
// is the one that differs only there
static std::string get_real_name(const std::string& stored, const std::set<std::string>& names)
{
    if (stored[stored.size() - 1] != '_')
        return stored;

    std::string found;

    for (std::set<std::string>::const_iterator it = names.begin(); it != names.end(); ++it)
    {
        if ((it->size() != stored.size()) || ((*it)[it->size() - 1] == '_') ||
            (it->compare(0, it->size() - 1, stored, 0, stored.size() - 1) != 0))
            continue;

        if (!found.empty())
            return "";

        found = *it;
    }

    return found;
}

// COMMAND.COM's own banner, "MS-DOS(R) Version 6.22" or "Versión 6.22", names the version whatever the language
static std::string get_version_name(const std::string& command)
{
    static const char k_banner[] = "MS-DOS(R) ";
    size_t at = command.find(k_banner);

    if (at == std::string::npos)
        return "MS-DOS";

    size_t word = command.find(' ', at + sizeof(k_banner) - 1);
    size_t end = word == std::string::npos ? word : command.find_first_not_of("0123456789.", word + 1);

    if ((word == std::string::npos) || (end == word + 1))
        return "MS-DOS";

    std::string version = command.substr(word + 1, end - word - 1);
    return "MS-DOS " + (version.find('.') == std::string::npos ? version + ".0" : version);
}

// "MS-DOS 6.22" as 622, "MS-DOS 6.0" as 600, "MS-DOS 5.00" as 500: what the generated CONFIG.SYS can use
static int get_version_number(const std::string& name)
{
    size_t digit = name.find_first_of("0123456789");

    if (digit == std::string::npos)
        return 0;

    int major = atoi(name.c_str() + digit);
    size_t dot = name.find('.', digit);
    std::string minor = dot == std::string::npos ? std::string() : name.substr(dot + 1, 2);

    while (minor.size() < 2)
        minor += '0';

    return (major * 100) + atoi(minor.c_str());
}

DosImport::DosImport()
{
    m_result = DOS_IMPORT_ERROR;
}

DosImport::~DosImport()
{
}

// A folder, a ZIP, or one image of the set, which stands for the folder it's in
bool DosImport::AddSource(const char* path)
{
    if (!IsValidPointer(path) || (path[0] == 0))
        return Fail(DOS_IMPORT_ERROR, "No disk images were given");

    std::string source(path);
    std::vector<std::string> folders;
    std::vector<std::string> files;

    if (MediaFile::ListDirectory(source.c_str(), folders, files))
        return AddFolder(source);

    if (ends_with_no_case(source.c_str(), ".zip"))
        return AddZip(source);

    if (FloppyImage::IsImageName(source.c_str()))
        return AddFolder(path_directory(source));

    return Fail(DOS_IMPORT_ERROR, "Not a folder, a ZIP or a disk image: %s", path_filename(source).c_str());
}

bool DosImport::AddFolder(const std::string& path)
{
    std::vector<std::string> folders;
    std::vector<std::string> files;

    if (!MediaFile::ListDirectory(path.c_str(), folders, files))
        return Fail(DOS_IMPORT_ERROR, "Unable to read the folder %s", path.c_str());

    std::sort(files.begin(), files.end());
    size_t found = 0;

    for (size_t i = 0; i < files.size(); i++)
    {
        bool zip = ends_with_no_case(files[i].c_str(), ".zip");

        if ((files[i][0] == '.') || (!zip && !FloppyImage::IsImageName(files[i].c_str())))
            continue;

        if (++found > k_dos_max_images)
            return Fail(DOS_IMPORT_ERROR, "More than %d disk images in %s", (int)k_dos_max_images, path.c_str());

        if (zip ? !AddZip(path_join(path, files[i])) : !AddImageFile(path_join(path, files[i])))
            return false;
    }

    if (found == 0)
        return Fail(DOS_IMPORT_ERROR, "No disk images in %s", path.c_str());

    return true;
}

bool DosImport::AddZip(const std::string& path)
{
    std::vector<u8> archive;

    if (!MediaFile::ReadFile(path.c_str(), k_dos_max_zip_size, &archive))
        return Fail(DOS_IMPORT_ERROR, "Unable to read %s", path.c_str());

    int count = FloppyImage::GetZipImageCount(archive.data(), archive.size());

    if (count <= 0)
        return Fail(DOS_IMPORT_ERROR, "No disk images in %s", path_filename(path).c_str());

    if ((size_t)count > k_dos_max_images)
        return Fail(DOS_IMPORT_ERROR, "More than %d disk images in %s", (int)k_dos_max_images, path_filename(path).c_str());

    for (int i = 0; i < count; i++)
    {
        u8* data = NULL;
        u32 size = 0;
        char name[256];

        if (!FloppyImage::ExtractFromZip(archive.data(), archive.size(), i, &data, &size, name, sizeof(name)))
            return Fail(DOS_IMPORT_ERROR, "Unable to read the images in %s", path_filename(path).c_str());

        bool ok = AddImage(data, size, name);
        SafeDeleteArray(data);

        if (!ok)
            return false;
    }

    return true;
}

bool DosImport::AddImageFile(const std::string& path)
{
    std::vector<u8> data;

    if (!MediaFile::ReadFile(path.c_str(), k_dos_max_image_size, &data))
        return Fail(DOS_IMPORT_ERROR, "Unable to read %s", path.c_str());

    return AddImage(data.data(), (u32)data.size(), path_filename(path));
}

bool DosImport::AddImage(const u8* data, u32 size, const std::string& name)
{
    std::vector<u8> raw;
    DosFs_Raw_Result loaded = dos_fs_load_raw(data, size, &raw);

    if (loaded == DOS_FS_RAW_UNKNOWN)
        return Fail(DOS_IMPORT_ERROR, "Not a floppy image Geardox reads: %s", name.c_str());

    if (loaded == DOS_FS_RAW_NOT_512)
        return Fail(DOS_IMPORT_ERROR, "Not a disk of 512-byte sectors: %s", name.c_str());

    if (!ReadFloppy(raw.data(), (u32)raw.size(), (int)m_images.size()))
        return Fail(DOS_IMPORT_ERROR, "Not a FAT12 floppy: %s", name.c_str());

    m_images.push_back(std::make_pair(to_upper_ascii(name), CalculateCRC32(0, data, size)));
    return true;
}

// Setup floppies are FAT12 with every file in the root, so the root is all this walks
bool DosImport::ReadFloppy(const u8* raw, u32 size, int image)
{
    DosFs_Fat12 fs;

    if (!dos_fs_fat12_open(raw, size, &fs))
        return false;

    std::map<std::string, DosImport_File> found;
    std::string name;
    u32 index = 0;
    const u8* entry = NULL;

    while (IsValidPointer(entry = dos_fs_fat12_next(fs, &index, &name)))
    {
        if ((m_files.count(name) != 0) || (found.count(name) != 0))
            continue;

        DosImport_File file;
        file.date = read_u16_le(entry + 24);
        file.time = read_u16_le(entry + 22);
        file.image = image;

        if (!dos_fs_fat12_read(fs, entry, &file.data))
            return false;

        found[name] = file;
    }

    m_files.insert(found.begin(), found.end());
    m_boot.push_back(std::vector<u8>(raw, raw + k_dos_sector_size));
    return true;
}

// Any MS-DOS 6 or 5 Setup set, by the family's rules, named by its own COMMAND.COM
DosImport_Result DosImport::Import(const char* zip_path)
{
    m_name.clear();
    m_result = DOS_IMPORT_ERROR;

    if (m_files.empty())
    {
        Fail(DOS_IMPORT_ERROR, "No files on the disk images");
        return m_result;
    }

    std::map<std::string, DosImport_File>::const_iterator io = m_files.find(k_dos_boot_files[0]);
    std::map<std::string, DosImport_File>::const_iterator command = m_files.find(k_dos_both_files[0]);
    std::map<std::string, DosImport_File>::const_iterator list = m_files.find(k_dos_names_list);
    std::vector<Dos_Listed> listed;
    std::vector<Dos_Listed> singles;

    if ((io == m_files.end()) || (command == m_files.end()) || (m_files.count(k_dos_boot_files[1]) == 0))
    {
        Fail(DOS_IMPORT_UNKNOWN, "No IO.SYS, MSDOS.SYS and COMMAND.COM: these aren't MS-DOS 5 or 6 Setup disks, or Disk 1 is missing");
        return m_result;
    }

    if (list != m_files.end())
        parse_names(list->second.data, &listed, &singles);

    const char* family = k_dos_family_6;
    const char* const* left_out = k_dos_left_out_6;

    if (listed.empty() && !singles.empty())
    {
        std::set<std::string> names;
        std::map<std::string, DosImport_File>::const_iterator setup = m_files.find(k_dos_setup_ini_5);
        family = k_dos_family_5;
        left_out = k_dos_left_out_5;

        if (setup != m_files.end())
            add_names(setup->second.data, &names);

        for (size_t i = 0; i < singles.size(); i++)
            names.insert(singles[i].stored);

        for (size_t i = 0; i < singles.size(); i++)
        {
            Dos_Listed item = singles[i];
            bool compressed = item.stored[item.stored.size() - 1] == '_';

            if (!compressed && (m_files.count(item.stored) == 0))
                continue;

            item.name = get_real_name(item.stored, names);

            if (item.name.empty())
            {
                Fail(DOS_IMPORT_INCOMPLETE, "%s has no name of its own in %s or %s", item.stored.c_str(), k_dos_setup_ini_5,
                    k_dos_names_list);
                return m_result;
            }

            listed.push_back(item);
        }
    }

    if (listed.empty())
    {
        Fail(DOS_IMPORT_UNKNOWN, "No %s naming the files: these aren't MS-DOS 5 or 6 Setup disks", k_dos_names_list);
        return m_result;
    }

    std::map<std::string, DosImport_File> installed;

    for (size_t i = 0; i < listed.size(); i++)
    {
        std::map<std::string, DosImport_File>::const_iterator found = m_files.find(listed[i].stored);
        std::string name = listed[i].name;

        for (int r = 0; IsValidPointer(k_dos_renames[r]); r += 2)
        {
            if (name == k_dos_renames[r])
                name = k_dos_renames[r + 1];
        }

        if (in_list(left_out, name))
            continue;

        if (found == m_files.end())
        {
            Fail(DOS_IMPORT_INCOMPLETE, "%s is missing: it's on Disk %d", name.c_str(), listed[i].disk);
            return m_result;
        }

        DosImport_File file = found->second;
        const u8* data = (const u8*)found->second.data.data();
        size_t size = found->second.data.size();
        bool kwaj = ms_is_kwaj(data, size);
        bool szdd = ms_is_szdd(data, size);

        if ((kwaj && !ms_expand_kwaj(data, size, k_dos_kwaj_max_size, &file.data)) ||
            (szdd && !ms_expand_szdd(data, size, k_dos_kwaj_max_size, &file.data)))
        {
            Fail(DOS_IMPORT_INCOMPLETE, "%s doesn't expand: the image of Disk %d may be damaged", listed[i].stored.c_str(), file.image + 1);
            return m_result;
        }

        installed[name] = file;
    }

    // Setup gives the MS-DOS Shell the VGA's files under its own names, and installs no other adapter's: what 5.0's
    // Setup did on Geardox's VGA
    if ((installed.count(k_dos_shell_copies[0]) != 0) && (installed.count(k_dos_shell_copies[2]) != 0) &&
        (installed.count(k_dos_shell_copies[4]) != 0))
    {
        for (int i = 0; IsValidPointer(k_dos_shell_copies[i]); i += 2)
            installed[k_dos_shell_copies[i + 1]] = installed[k_dos_shell_copies[i]];

        for (int i = 0; IsValidPointer(k_dos_shell_adapters[i]); i++)
            installed.erase(k_dos_shell_adapters[i]);
    }

    for (int i = 0; IsValidPointer(k_dos_required[i]); i++)
    {
        if (installed.count(k_dos_required[i]) == 0)
        {
            Fail(DOS_IMPORT_INCOMPLETE, "%s isn't on these Setup disks, and games need it", k_dos_required[i]);
            return m_result;
        }
    }

    const std::vector<u8>& boot = m_boot[io->second.image];

    if ((boot[0] != 0xEB) && (boot[0] != 0xE9))
    {
        Fail(DOS_IMPORT_INCOMPLETE, "The disk with IO.SYS has no boot code of its own");
        return m_result;
    }

    m_name = get_version_name(command->second.data);
    MediaZip zip;

    if (!zip.BeginArchive())
    {
        Fail(DOS_IMPORT_ERROR, "Unable to make %s", k_dos_import_file);
        return m_result;
    }

    std::string version_text = std::string("id=") + family + "\r\nname=" + m_name + "\r\n";
    char source[32];
    snprintf(source, sizeof(source), "source=%08X\r\n", GetSourceFingerprint());
    version_text += source;
    bool ok = zip.AddEntry(k_import_version, version_text.data(), version_text.size(), MZ_DEFAULT_LEVEL) &&
        zip.AddEntry(k_dos_import_boot, boot.data(), boot.size(), 0);

    for (std::map<std::string, DosImport_File>::const_iterator it = installed.begin(); ok && (it != installed.end()); ++it)
    {
        const std::string& data = it->second.data;
        Dos_Place place = get_place(it->first);
        s64 modified = dos_time_to_host(it->second.date, it->second.time);
        const s64* stamp = modified != 0 ? &modified : NULL;
        std::string root = std::string(k_dos_import_root) + it->first;
        std::string dos = std::string(k_dos_import_root) + "DOS/" + it->first;

        if (place != DOS_PLACE_DOS)
            ok = zip.AddEntry(root.c_str(), data.data(), data.size(), MZ_DEFAULT_LEVEL, stamp);

        if (ok && ((place == DOS_PLACE_DOS) || (place == DOS_PLACE_BOTH)))
            ok = zip.AddEntry(dos.c_str(), data.data(), data.size(), MZ_DEFAULT_LEVEL, stamp);
    }

    if (!ok || !zip.FinishArchive())
    {
        Fail(DOS_IMPORT_ERROR, "Unable to make %s", k_dos_import_file);
        return m_result;
    }

    if (!zip.WriteArchive(zip_path))
    {
        Fail(DOS_IMPORT_ERROR, "Unable to write %s", zip_path);
        return m_result;
    }

    Log("%s imported from %d disk images into %s", m_name.c_str(), GetImageCount(), zip_path);
    m_result = DOS_IMPORT_DONE;
    m_error.clear();
    return m_result;
}

const char* DosImport::GetName() const
{
    return m_name.c_str();
}

// The images by name and contents, so a frontend knows when the user's disks changed
u32 DosImport::GetSourceFingerprint() const
{
    std::vector<std::pair<std::string, u32> > images(m_images);
    std::sort(images.begin(), images.end());
    u32 crc = 0;

    for (size_t i = 0; i < images.size(); i++)
    {
        u8 value[4];
        write_u32_le(value, images[i].second);
        crc = CalculateCRC32(crc, images[i].first.data(), images[i].first.size());
        crc = CalculateCRC32(crc, value, sizeof(value));
    }

    return crc;
}

int DosImport::GetImageCount() const
{
    return (int)m_images.size();
}

const char* DosImport::GetError() const
{
    return m_error.c_str();
}

static bool has_entry(MediaZip* zip, const std::string& name)
{
    return mz_zip_reader_locate_file(zip->GetArchive(), name.c_str(), NULL, 0) >= 0;
}

bool DosImport::Load(const char* zip_path, DosImport_System* system, std::string* error)
{
    std::vector<u8> data;
    system->name.clear();
    system->pinned.clear();
    system->root.clear();
    system->cd_extension = false;
    system->cache = false;
    system->zip.clear();
    system->crc = 0;

    MediaZip zip;
    std::string text;

    if (!import_open(zip_path, k_dos_max_zip_size, "MS-DOS", &data, &zip, &text, error))
        return false;

    std::string boot;
    std::string id = import_get_value(text, "id");
    bool ok = ((id == k_dos_family_6) || (id == k_dos_family_5)) && zip.Extract(k_dos_import_boot, &boot) &&
        (boot.size() == sizeof(system->boot)) && (read_u16_le((const u8*)boot.data() + 510) == 0xAA55);

    if (ok)
        memcpy(system->boot, boot.data(), sizeof(system->boot));

    for (int i = 0; ok && IsValidPointer(k_dos_boot_files[i]); i++)
        ok = has_entry(&zip, std::string(k_dos_import_root) + k_dos_boot_files[i]);

    for (int i = 0; ok && IsValidPointer(k_dos_required[i]); i++)
        ok = has_entry(&zip, std::string(k_dos_import_root) + "DOS/" + k_dos_required[i]);

    ok = ok && has_entry(&zip, std::string(k_dos_import_root) + k_dos_both_files[0]);

    for (mz_uint i = 0; ok && (i < mz_zip_reader_get_num_files(zip.GetArchive())); i++)
    {
        char name[256];
        mz_zip_reader_get_filename(zip.GetArchive(), i, name, sizeof(name));
        std::string path(name);

        if ((path.compare(0, 2, k_dos_import_root) == 0) && (path.find('/', 2) == std::string::npos))
            system->root.push_back(path.substr(2));
    }

    system->cd_extension = ok && has_entry(&zip, std::string(k_dos_import_root) + "DOS/" + k_dos_cd_extension);
    system->cache = ok && has_entry(&zip, std::string(k_dos_import_root) + "DOS/" + k_dos_cache);
    zip.Close();

    if (!ok)
    {
        *error = std::string("The imported MS-DOS is damaged or from a newer Geardox, import it again: ") + zip_path;
        system->root.clear();
        return false;
    }

    for (int i = 0; IsValidPointer(k_dos_boot_files[i]); i++)
        system->pinned.push_back(k_dos_boot_files[i]);

    system->name = import_get_value(text, "name");
    system->name = system->name.empty() ? std::string("MS-DOS") : system->name;
    system->version = get_version_number(system->name);
    system->zip.assign((const char*)data.data(), data.size());
    system->crc = CalculateCRC32(0, data.data(), data.size());
    error->clear();
    return true;
}

bool DosImport::ReadSourceFingerprint(const char* zip_path, u32* fingerprint)
{
    return import_read_fingerprint(zip_path, k_dos_max_zip_size, fingerprint);
}

bool DosImport::Fail(DosImport_Result result, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    m_error = vformat_string(format, args);
    va_end(args);
    m_result = result;
    Log("MS-DOS import: %s", m_error.c_str());
    return false;
}
