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
#include "../media/media_file.h"
#include "../media/crc.h"

static const u32 k_dos_sector_size = 512;
static const u32 k_dos_max_image_size = 16 * 1024 * 1024;
static const u32 k_dos_max_zip_size = 64 * 1024 * 1024;
static const size_t k_dos_max_images = 16;
static const u32 k_dos_max_fat12_clusters = 4084;
static const u8 k_dos_kwaj_signature[8] = { 'K', 'W', 'A', 'J', 0x88, 0xF0, 0x27, 0xD1 };
static const u32 k_dos_kwaj_header_size = 14;
static const u32 k_dos_kwaj_window = 4096;
static const u32 k_dos_kwaj_max_size = 8 * 1024 * 1024;
static const u16 k_dos_kwaj_method_lzh = 3;

// MS-DOS 6's Setup disks, whatever the version or the language: PACKING.LST names every file Setup installs, the
// compressed ones in KWAJ, and DOSSETUP.INI's [dif-path] puts these in the root. MS-DOS 5's compress in SZDD, list
// each disk's names only as stored, and have the real ones in SETUP.INI and PACKING.LST's install layout
static const char k_dos_names_list[] = "PACKING.LST";
static const char* const k_dos_boot_files[] = { "IO.SYS", "MSDOS.SYS", NULL };
static const char* const k_dos_root_files[] = { "WINA20.386", NULL };
static const char* const k_dos_both_files[] = { "COMMAND.COM", NULL };
static const char* const k_dos_left_out_6[] = { "PACKING.LST", "SETUP.MSG", "CONFIG.SYS", "AUTOEXEC.BAT", NULL };
static const char* const k_dos_left_out_5[] = { "SETUP.EXE", "SETUP.INI", "CV.COM", "README.1ST", "CONFIG.SYS", "AUTOEXEC.BAT", NULL };
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
static const u8 k_dos_szdd_signature[8] = { 'S', 'Z', 'D', 'D', 0x88, 0xF0, 0x27, 0x33 };
static const u32 k_dos_szdd_header_size = 14;

enum Dos_Place
{
    DOS_PLACE_BOOT = 0,
    DOS_PLACE_ROOT,
    DOS_PLACE_BOTH,
    DOS_PLACE_DOS
};

struct Kwaj_Bits
{
    const u8* data;
    size_t size;
    size_t bit;
};

struct Kwaj_Huffman
{
    u16 count[16];
    u16 symbol[256];
};

static bool read_file(const std::string& path, u32 limit, std::vector<u8>* data)
{
    u64 size = 0;
    s64 modified = 0;
    data->clear();

    if (!MediaFile::GetFileInfo(path.c_str(), &size, &modified) || (size == 0) || (size > limit))
        return false;

    MediaFile* file = MediaFile::OpenFile(path.c_str());

    if (!IsValidPointer(file))
        return false;

    data->resize((size_t)size);
    bool ok = file->ReadAt(0, data->data(), size);
    SafeDelete(file);
    return ok;
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

// A DOS file name with an extension, as PACKING.LST's two columns have them
static bool is_listed_name(const std::string& name)
{
    size_t dot = name.find('.');

    if ((dot == std::string::npos) || (dot == 0) || (dot > 8) || (name.size() - dot - 1 == 0) || (name.size() - dot - 1 > 3))
        return false;

    for (size_t i = 0; i < name.size(); i++)
    {
        char c = name[i];
        bool valid = ((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')) || (strchr("$%'-_@~`!(){}^#&", c) != NULL);

        if (!valid && (i != dot))
            return false;
    }

    return true;
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
            !is_listed_name(to_upper_ascii(first)))
            disk = second[0] - '0';

        Dos_Listed item;
        item.stored = to_upper_ascii(first);
        item.name = count == 2 ? to_upper_ascii(second) : item.stored;
        item.disk = disk;

        if ((count == 2) && is_listed_name(item.stored) && is_listed_name(item.name))
            pairs->push_back(item);
        else if ((count == 1) && is_listed_name(item.stored))
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
        bool part = ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')) ||
            ((c != 0) && (strchr("$%'-_@~`!(){}^#&.", c) != NULL));

        if (part)
        {
            token += c;
            continue;
        }

        if (is_listed_name(to_upper_ascii(token)))
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

static u32 kwaj_bits(Kwaj_Bits* bits, int count)
{
    u32 value = 0;

    for (int i = 0; i < count; i++)
    {
        size_t byte = bits->bit >> 3;
        u32 bit = byte < bits->size ? (bits->data[byte] >> (7 - (bits->bit & 7))) & 1 : 0;
        value = (value << 1) | bit;
        bits->bit++;
    }

    return value;
}

// Canonical codes: shorter first, and in the order of the symbols within a length
static bool kwaj_build(Kwaj_Huffman* huffman, const u8* lengths, int symbols)
{
    u16 offsets[16];
    int left = 1;
    memset(huffman->count, 0, sizeof(huffman->count));

    for (int i = 0; i < symbols; i++)
        huffman->count[lengths[i]]++;

    for (int length = 1; length < 16; length++)
    {
        left = (left << 1) - huffman->count[length];

        if (left < 0)
            return false;
    }

    offsets[1] = 0;

    for (int length = 1; length < 15; length++)
        offsets[length + 1] = (u16)(offsets[length] + huffman->count[length]);

    for (int i = 0; i < symbols; i++)
    {
        if (lengths[i] != 0)
            huffman->symbol[offsets[lengths[i]]++] = (u16)i;
    }

    return true;
}

static int kwaj_decode(Kwaj_Bits* bits, const Kwaj_Huffman* huffman)
{
    int code = 0;
    int first = 0;
    int index = 0;

    for (int length = 1; length < 16; length++)
    {
        code |= (int)kwaj_bits(bits, 1);
        int count = huffman->count[length];

        if (code - count < first)
            return huffman->symbol[index + (code - first)];

        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }

    return -1;
}

// The four ways a tree's code lengths are stored, from libmspack's SZDD and KWAJ format page
static bool kwaj_lengths(Kwaj_Bits* bits, u32 type, int symbols, Kwaj_Huffman* huffman)
{
    u8 lengths[256];
    int value = 0;

    for (int i = 0; i < symbols; i++)
    {
        switch (type)
        {
            case 0:
                value = symbols == 16 ? 4 : symbols == 32 ? 5 : symbols == 64 ? 6 : 8;
                break;
            case 1:
                if (i == 0)
                    value = (int)kwaj_bits(bits, 4);
                else if (kwaj_bits(bits, 1) != 0)
                    value = kwaj_bits(bits, 1) == 0 ? value + 1 : (int)kwaj_bits(bits, 4);
                break;
            case 2:
                if (i == 0)
                    value = (int)kwaj_bits(bits, 4);
                else
                {
                    u32 selector = kwaj_bits(bits, 2);
                    value = selector == 3 ? (int)kwaj_bits(bits, 4) : value + (int)selector - 1;
                }
                break;
            case 3:
                value = (int)kwaj_bits(bits, 4);
                break;
            default:
                return false;
        }

        if ((value < 0) || (value > 15))
            return false;

        lengths[i] = (u8)value;
    }

    return kwaj_build(huffman, lengths, symbols);
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

    if (!read_file(path, k_dos_max_zip_size, &archive))
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

    if (!read_file(path, k_dos_max_image_size, &data))
        return Fail(DOS_IMPORT_ERROR, "Unable to read %s", path.c_str());

    return AddImage(data.data(), (u32)data.size(), path_filename(path));
}

bool DosImport::AddImage(const u8* data, u32 size, const std::string& name)
{
    u32 image_size = 0;
    u8* image = FloppyImage::Load(data, size, &image_size);

    if (!IsValidPointer(image))
        return Fail(DOS_IMPORT_ERROR, "Not a floppy image Geardox reads: %s", name.c_str());

    std::vector<u8> raw(FloppyImage::GetRawSize(image, image_size));
    bool exported = !raw.empty() && FloppyImage::ExportRaw(image, image_size, raw.data(), (u32)raw.size());
    SafeDeleteArray(image);

    if (!exported)
        return Fail(DOS_IMPORT_ERROR, "Not a disk of 512-byte sectors: %s", name.c_str());

    if (!ReadFloppy(raw.data(), (u32)raw.size(), (int)m_images.size()))
        return Fail(DOS_IMPORT_ERROR, "Not a FAT12 floppy: %s", name.c_str());

    m_images.push_back(std::make_pair(to_upper_ascii(name), CalculateCRC32(0, data, size)));
    return true;
}

// Setup floppies are FAT12 with every file in the root, so the root is all this walks
bool DosImport::ReadFloppy(const u8* raw, u32 size, int image)
{
    if ((size < k_dos_sector_size) || (read_u16_le(raw + 11) != k_dos_sector_size) || (read_u16_le(raw + 510) != 0xAA55))
        return false;

    u32 cluster_sectors = raw[13];
    u32 reserved = read_u16_le(raw + 14);
    u32 fats = raw[16];
    u32 root_entries = read_u16_le(raw + 17);
    u32 total = read_u16_le(raw + 19) != 0 ? read_u16_le(raw + 19) : read_u32_le(raw + 32);
    u32 fat_sectors = read_u16_le(raw + 22);

    if ((cluster_sectors == 0) || ((cluster_sectors & (cluster_sectors - 1)) != 0) || (reserved == 0) || (fats == 0) ||
        (fats > 2) || (root_entries == 0) || (fat_sectors == 0))
        return false;

    u32 root = (reserved + (fats * fat_sectors)) * k_dos_sector_size;
    u32 data = root + (((root_entries * 32) + k_dos_sector_size - 1) / k_dos_sector_size) * k_dos_sector_size;
    total = MIN(total, size / k_dos_sector_size);

    if ((data >= size) || (total * k_dos_sector_size <= data))
        return false;

    u32 cluster_size = cluster_sectors * k_dos_sector_size;
    u32 clusters = (total * k_dos_sector_size - data) / cluster_size;

    if ((clusters == 0) || (clusters > k_dos_max_fat12_clusters) || ((reserved + fat_sectors) * k_dos_sector_size > size))
        return false;

    const u8* fat = raw + (reserved * k_dos_sector_size);
    std::map<std::string, DosImport_File> found;

    for (u32 i = 0; i < root_entries; i++)
    {
        const u8* entry = raw + root + (i * 32);

        if (entry[0] == 0)
            break;

        if ((entry[0] == 0xE5) || ((entry[11] & 0x0F) == 0x0F) || ((entry[11] & 0x18) != 0))
            continue;

        std::string base((const char*)entry, 8);
        std::string extension((const char*)entry + 8, 3);
        base.erase(base.find_last_not_of(' ') + 1);
        extension.erase(extension.find_last_not_of(' ') + 1);

        if (!base.empty() && ((u8)base[0] == 0x05))
            base[0] = (char)0xE5;

        std::string name = to_upper_ascii(extension.empty() ? base : base + "." + extension);

        if ((m_files.count(name) != 0) || (found.count(name) != 0))
            continue;

        DosImport_File file;
        u32 file_size = read_u32_le(entry + 28);
        u32 cluster = read_u16_le(entry + 26);
        u32 steps = 0;
        file.date = read_u16_le(entry + 24);
        file.time = read_u16_le(entry + 22);
        file.image = image;

        while ((file.data.size() < file_size) && (cluster >= 2) && (cluster < clusters + 2) && (steps++ <= clusters))
        {
            u32 offset = data + ((cluster - 2) * cluster_size);
            u32 chunk = MIN(cluster_size, file_size - (u32)file.data.size());
            file.data.append((const char*)raw + offset, chunk);
            u32 at = cluster + (cluster / 2);
            u32 value = fat[at] | (fat[at + 1] << 8);
            cluster = (cluster & 1) != 0 ? value >> 4 : value & 0xFFF;
        }

        if (file.data.size() != file_size)
            return false;

        found[name] = file;
    }

    m_files.insert(found.begin(), found.end());
    m_boot.push_back(std::vector<u8>(raw, raw + k_dos_sector_size));
    return true;
}

// KWAJ method 3, written from libmspack's SZDD and KWAJ format page. It stops at the length in the header,
// as the format has no end marker
bool DosImport::ExpandKWAJ(const u8* data, size_t size, std::string* out)
{
    out->clear();

    if ((size < k_dos_kwaj_header_size + 4) || (memcmp(data, k_dos_kwaj_signature, sizeof(k_dos_kwaj_signature)) != 0))
        return false;

    u16 method = read_u16_le(data + 8);
    u16 offset = read_u16_le(data + 10);
    u16 flags = read_u16_le(data + 12);
    u32 length = read_u32_le(data + k_dos_kwaj_header_size);

    if ((method != k_dos_kwaj_method_lzh) || ((flags & 0x01) == 0) || (offset > size) || (length > k_dos_kwaj_max_size))
        return false;

    static const int k_symbols[5] = { 16, 16, 32, 64, 256 };
    Kwaj_Huffman trees[5];
    Kwaj_Bits bits = { data + offset, size - offset, 0 };
    u32 types[6];
    u8 window[k_dos_kwaj_window];
    u32 position = k_dos_kwaj_window - 17;
    int table = 0;
    memset(window, ' ', sizeof(window));

    for (int i = 0; i < 6; i++)
        types[i] = kwaj_bits(&bits, 4);

    for (int i = 0; i < 5; i++)
    {
        if (!kwaj_lengths(&bits, types[i], k_symbols[i], &trees[i]))
            return false;
    }

    out->reserve(length);

    while (out->size() < length)
    {
        if (bits.bit > (bits.size * 8) + 32)
            return false;

        int code = kwaj_decode(&bits, &trees[table]);

        if (code < 0)
            return false;

        if (code > 0)
        {
            int high = kwaj_decode(&bits, &trees[3]);

            if (high < 0)
                return false;

            u32 distance = ((u32)high << 6) | kwaj_bits(&bits, 6);

            for (int i = 0; (i < code + 2) && (out->size() < length); i++)
            {
                u8 value = window[(position - distance) & (k_dos_kwaj_window - 1)];
                window[position] = value;
                out->push_back((char)value);
                position = (position + 1) & (k_dos_kwaj_window - 1);
            }

            table = 0;
            continue;
        }

        int run = kwaj_decode(&bits, &trees[2]);

        if (run < 0)
            return false;

        table = run != 31 ? 1 : 0;

        for (int i = 0; (i <= run) && (out->size() < length); i++)
        {
            int value = kwaj_decode(&bits, &trees[4]);

            if (value < 0)
                return false;

            window[position] = (u8)value;
            out->push_back((char)value);
            position = (position + 1) & (k_dos_kwaj_window - 1);
        }
    }

    return bits.bit <= (bits.size * 8) + 32;
}

// SZDD, MS-DOS 5's COMPRESS, from the same format page: LZSS over a 4 KB window of spaces. A control byte's bits, low
// first, choose a literal or a 12-bit position and 4-bit length, plus 3
bool DosImport::ExpandSZDD(const u8* data, size_t size, std::string* out)
{
    out->clear();

    if ((size < k_dos_szdd_header_size) || (memcmp(data, k_dos_szdd_signature, sizeof(k_dos_szdd_signature)) != 0) ||
        (data[8] != 'A'))
        return false;

    u32 length = read_u32_le(data + 10);

    if (length > k_dos_kwaj_max_size)
        return false;

    u8 window[k_dos_kwaj_window];
    u32 position = k_dos_kwaj_window - 16;
    size_t at = k_dos_szdd_header_size;
    memset(window, ' ', sizeof(window));
    out->reserve(length);

    while ((out->size() < length) && (at < size))
    {
        u8 control = data[at++];

        for (int bit = 0; (bit < 8) && (out->size() < length); bit++)
        {
            if ((control & (1 << bit)) != 0)
            {
                if (at >= size)
                    return false;

                window[position] = data[at++];
                out->push_back((char)window[position]);
                position = (position + 1) & (k_dos_kwaj_window - 1);
                continue;
            }

            if (at + 1 >= size)
                return false;

            u32 from = data[at] | ((data[at + 1] & 0xF0) << 4);
            u32 count = (data[at + 1] & 0x0F) + 3;
            at += 2;

            for (u32 i = 0; (i < count) && (out->size() < length); i++)
            {
                window[position] = window[from];
                out->push_back((char)window[position]);
                position = (position + 1) & (k_dos_kwaj_window - 1);
                from = (from + 1) & (k_dos_kwaj_window - 1);
            }
        }
    }

    return out->size() == length;
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
        bool kwaj = (size >= 4) && (memcmp(data, k_dos_kwaj_signature, 4) == 0);
        bool szdd = (size >= 4) && (memcmp(data, k_dos_szdd_signature, 4) == 0);

        if ((kwaj && !ExpandKWAJ(data, size, &file.data)) || (szdd && !ExpandSZDD(data, size, &file.data)))
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
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_writer_init_heap(&zip, 0, 4 * 1024 * 1024))
    {
        Fail(DOS_IMPORT_ERROR, "Unable to make %s", k_dos_import_file);
        return m_result;
    }

    std::string version_text = std::string("id=") + family + "\r\nname=" + m_name + "\r\n";
    char source[32];
    snprintf(source, sizeof(source), "source=%08X\r\n", GetSourceFingerprint());
    version_text += source;
    bool ok = mz_zip_writer_add_mem(&zip, k_dos_import_version, version_text.data(), version_text.size(),
        MZ_DEFAULT_LEVEL) && mz_zip_writer_add_mem(&zip, k_dos_import_boot, boot.data(), boot.size(), 0);

    for (std::map<std::string, DosImport_File>::const_iterator it = installed.begin(); ok && (it != installed.end()); ++it)
    {
        const std::string& data = it->second.data;
        Dos_Place place = get_place(it->first);
        MZ_TIME_T modified = (MZ_TIME_T)dos_to_host_time(it->second.date, it->second.time);
        std::string root = std::string(k_dos_import_root) + it->first;
        std::string dos = std::string(k_dos_import_root) + "DOS/" + it->first;

        if (place != DOS_PLACE_DOS)
            ok = mz_zip_writer_add_mem_ex_v2(&zip, root.c_str(), data.data(), data.size(), NULL, 0, MZ_DEFAULT_LEVEL, 0,
                0, &modified, NULL, 0, NULL, 0) != 0;

        if (ok && ((place == DOS_PLACE_DOS) || (place == DOS_PLACE_BOTH)))
            ok = mz_zip_writer_add_mem_ex_v2(&zip, dos.c_str(), data.data(), data.size(), NULL, 0, MZ_DEFAULT_LEVEL, 0,
                0, &modified, NULL, 0, NULL, 0) != 0;
    }

    void* archive = NULL;
    size_t archive_size = 0;
    ok = ok && mz_zip_writer_finalize_heap_archive(&zip, &archive, &archive_size);

    if (!ok)
    {
        mz_zip_writer_end(&zip);
        Fail(DOS_IMPORT_ERROR, "Unable to make %s", k_dos_import_file);
        return m_result;
    }

    std::string temporary = std::string(zip_path) + ".tmp";
    MediaFile* file = MediaFile::OpenNewFile(temporary.c_str());
    ok = IsValidPointer(file) && file->WriteAt(0, archive, archive_size) && file->Flush();
    SafeDelete(file);
    mz_zip_writer_end(&zip);

    // Where a rename can't replace a file, the old import steps aside and comes back if the new one can't take its place
    if (ok && !MediaFile::RenameFile(temporary.c_str(), zip_path))
    {
        std::string old = std::string(zip_path) + ".old";
        MediaFile::RemoveFile(old.c_str());
        bool moved = MediaFile::RenameFile(zip_path, old.c_str());
        ok = MediaFile::RenameFile(temporary.c_str(), zip_path);

        if (ok)
            MediaFile::RemoveFile(old.c_str());
        else if (moved)
            MediaFile::RenameFile(old.c_str(), zip_path);
    }

    if (!ok)
    {
        MediaFile::RemoveFile(temporary.c_str());
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

static std::string get_value(const std::string& text, const char* key)
{
    std::string prefix = std::string(key) + "=";
    size_t at = text.find(prefix);

    if ((at == std::string::npos) || ((at != 0) && (text[at - 1] != '\n')))
        return "";

    size_t end = text.find_first_of("\r\n", at);
    return text.substr(at + prefix.size(), end == std::string::npos ? std::string::npos : end - at - prefix.size());
}

static bool read_version_text(mz_zip_archive* zip, std::string* text)
{
    size_t size = 0;
    void* data = mz_zip_reader_extract_file_to_heap(zip, k_dos_import_version, &size, 0);

    if (!IsValidPointer(data))
        return false;

    text->assign((const char*)data, size);
    mz_free(data);
    return !get_value(*text, "id").empty();
}

static bool has_entry(mz_zip_archive* zip, const std::string& name)
{
    return mz_zip_reader_locate_file(zip, name.c_str(), NULL, 0) >= 0;
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

    if (!read_file(zip_path, k_dos_max_zip_size, &data))
    {
        *error = std::string("Unable to read ") + zip_path;
        return false;
    }

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    std::string text;

    if (!mz_zip_reader_init_mem(&zip, data.data(), data.size(), 0) || !read_version_text(&zip, &text))
    {
        mz_zip_reader_end(&zip);
        *error = std::string("Not an MS-DOS import: ") + zip_path;
        return false;
    }

    size_t boot_size = 0;
    void* boot = mz_zip_reader_extract_file_to_heap(&zip, k_dos_import_boot, &boot_size, 0);
    std::string id = get_value(text, "id");
    bool ok = ((id == k_dos_family_6) || (id == k_dos_family_5)) && IsValidPointer(boot) && (boot_size == sizeof(system->boot)) &&
        (read_u16_le((const u8*)boot + 510) == 0xAA55);

    if (ok)
        memcpy(system->boot, boot, sizeof(system->boot));

    if (IsValidPointer(boot))
        mz_free(boot);

    for (int i = 0; ok && IsValidPointer(k_dos_boot_files[i]); i++)
        ok = has_entry(&zip, std::string(k_dos_import_root) + k_dos_boot_files[i]);

    for (int i = 0; ok && IsValidPointer(k_dos_required[i]); i++)
        ok = has_entry(&zip, std::string(k_dos_import_root) + "DOS/" + k_dos_required[i]);

    ok = ok && has_entry(&zip, std::string(k_dos_import_root) + k_dos_both_files[0]);

    for (mz_uint i = 0; ok && (i < mz_zip_reader_get_num_files(&zip)); i++)
    {
        char name[256];
        mz_zip_reader_get_filename(&zip, i, name, sizeof(name));
        std::string path(name);

        if ((path.compare(0, 2, k_dos_import_root) == 0) && (path.find('/', 2) == std::string::npos))
            system->root.push_back(path.substr(2));
    }

    system->cd_extension = ok && has_entry(&zip, std::string(k_dos_import_root) + "DOS/" + k_dos_cd_extension);
    system->cache = ok && has_entry(&zip, std::string(k_dos_import_root) + "DOS/" + k_dos_cache);
    mz_zip_reader_end(&zip);

    if (!ok)
    {
        *error = std::string("The imported MS-DOS is damaged or from a newer Geardox, import it again: ") + zip_path;
        system->root.clear();
        return false;
    }

    for (int i = 0; IsValidPointer(k_dos_boot_files[i]); i++)
        system->pinned.push_back(k_dos_boot_files[i]);

    system->name = get_value(text, "name");
    system->name = system->name.empty() ? std::string("MS-DOS") : system->name;
    system->version = get_version_number(system->name);
    system->zip.assign((const char*)data.data(), data.size());
    system->crc = CalculateCRC32(0, data.data(), data.size());
    error->clear();
    return true;
}

bool DosImport::ReadSourceFingerprint(const char* zip_path, u32* fingerprint)
{
    std::vector<u8> data;
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    std::string id;

    if (!read_file(zip_path, k_dos_max_zip_size, &data) || !mz_zip_reader_init_mem(&zip, data.data(), data.size(), 0))
        return false;

    std::string text;
    bool ok = read_version_text(&zip, &text);
    mz_zip_reader_end(&zip);
    std::string source = get_value(text, "source");

    if (!parse_hex_string(source.c_str(), source.size(), fingerprint))
        *fingerprint = 0;

    return ok;
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
