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
#include "gus_import.h"
#include "floppy_image.h"
#include "dos_fs.h"
#include "../media/media_file.h"
#include "../media/media_zip.h"
#include "import_common.h"
#include "../media/crc.h"

static const u32 k_gus_max_image_size = 16 * 1024 * 1024;
static const u32 k_gus_max_zip_size = 64 * 1024 * 1024;
static const u32 k_gus_max_volume_size = 8 * 1024 * 1024;
static const u32 k_gus_max_installed_size = 32 * 1024 * 1024;
static const u32 k_gus_max_installed_total = 128 * 1024 * 1024;
static const int k_gus_max_depth = 3;
static const u32 k_gus_table_size = 18272;
static const u32 k_gus_table_crc = 0xD65F520D;
static const u32 k_gus_record_size = 59;
static const u32 k_gus_record_mark = 0x1C;
static const size_t k_gus_max_records = 1024;
static const u32 k_gus_max_volumes = 32;
static const size_t k_gus_list_window = 65536;
static const u32 k_gus_group_stride = 88;
static const char k_gus_volume_base[] = "ULTRASND";
static const char k_gus_family_411[] = "gus411";
static const char k_gus_family_installed[] = "gusfolder";
static const char k_gus_name_411[] = "Gravis UltraSound 4.11";
static const char k_gus_name_installed[] = "Gravis UltraSound";
static const char k_gus_ini[] = "ULTRASND.INI";
static const char k_gus_midi[] = "MIDI";
static const char k_gus_refuse[] = "isn't supported: import the 4.11 disks";

struct Gus_Group
{
    const char* name;
    bool left_out;
};

static const Gus_Group k_gus_groups[] =
{
    { "MIDI", false }, { "PMAKER", true }, { "WINDOWS", true }, { "SBOS", true }, { "MAXSBOS", true }
};

static const u8 k_gus_lit_length[] =
{
    11, 124, 8, 7, 28, 7, 188, 13, 76, 4, 10, 8, 12, 10, 12, 10, 8, 23, 8, 9, 7, 6, 7, 8, 7, 6, 55, 8, 23, 24, 12, 11,
    7, 9, 11, 12, 6, 7, 22, 5, 7, 24, 6, 11, 9, 6, 7, 22, 7, 11, 38, 7, 9, 8, 25, 11, 8, 11, 9, 12, 8, 12, 5, 38, 5,
    38, 5, 11, 7, 5, 6, 21, 6, 10, 53, 8, 7, 24, 10, 27, 44, 253, 253, 253, 252, 252, 252, 13, 12, 45, 12, 45, 12, 61,
    12, 45, 44, 173
};
static const u8 k_gus_len_length[] = { 2, 35, 36, 53, 38, 23 };
static const u8 k_gus_dist_length[] = { 2, 20, 53, 230, 247, 151, 248 };
static const u16 k_gus_len_base[16] = { 3, 2, 4, 5, 6, 7, 8, 9, 10, 12, 16, 24, 40, 72, 136, 264 };
static const u8 k_gus_len_extra[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8 };

struct Gus_Huffman
{
    u16 count[16];
    u16 symbol[256];
};

struct Gus_Bits
{
    const u8* data;
    size_t size;
    size_t position;
    u32 buffer;
    int count;
    bool failed;
};

struct Gus_Output
{
    std::string path;
    std::string data;
    s64 modified;
};

struct Gus_Record
{
    std::string name;
    u8 flag;
    u8 component;
    u8 first_volume;
    u8 last_volume;
    u32 offset;
    u32 packed_size;
    u32 size;
    u32 crc;
    u16 date;
    u16 time;
};

static bool is_volume_name(const std::string& name)
{
    size_t base = strlen(k_gus_volume_base);
    return (name.size() == base + 4) && (name.compare(0, base, k_gus_volume_base) == 0) &&
        (name.compare(base, 2, ".0") == 0) && isdigit((unsigned char)name[base + 2]) &&
        isdigit((unsigned char)name[base + 3]);
}

static bool is_volume_of(const std::string& name, const char* base)
{
    size_t length = strlen(base);
    return (name.size() == length + 4) && (name.compare(0, length, base) == 0) && (name.compare(length, 2, ".0") == 0) &&
        isdigit((unsigned char)name[length + 2]) && isdigit((unsigned char)name[length + 3]);
}

static bool is_wanted(const std::string& name)
{
    return is_volume_name(name) || (name == "DISK.ID");
}

static bool is_dos_name(const std::string& name)
{
    size_t dot = name.find('.');
    size_t base = dot == std::string::npos ? name.size() : dot;

    size_t extension = dot == std::string::npos ? 0 : name.size() - dot - 1;

    if ((base == 0) || (base > 8) || ((dot != std::string::npos) && ((extension > 3) || (extension == 0))))
        return false;

    for (size_t i = 0; i < name.size(); i++)
    {
        char c = name[i];

        if ((i != dot) && !isalnum((unsigned char)c) && (strchr("_-~$!#&", c) == NULL))
            return false;
    }

    return true;
}

static u32 gus_bits(Gus_Bits* bits, int need)
{
    u32 value = bits->buffer;

    while (bits->count < need)
    {
        if (bits->position >= bits->size)
        {
            bits->failed = true;
            return 0;
        }

        value |= (u32)bits->data[bits->position++] << bits->count;
        bits->count += 8;
    }

    bits->buffer = value >> need;
    bits->count -= need;
    return value & ((1u << need) - 1);
}

static void gus_build(Gus_Huffman* huffman, const u8* repeats, int entries)
{
    u8 lengths[256];
    u16 offsets[16];
    int total = 0;
    memset(huffman->count, 0, sizeof(huffman->count));
    memset(huffman->symbol, 0, sizeof(huffman->symbol));

    for (int i = 0; i < entries; i++)
    {
        int repeat = (repeats[i] >> 4) + 1;

        for (int j = 0; j < repeat; j++)
            lengths[total++] = repeats[i] & 15;
    }

    for (int i = 0; i < total; i++)
        huffman->count[lengths[i]]++;

    offsets[0] = 0;
    offsets[1] = 0;

    for (int i = 1; i < 15; i++)
        offsets[i + 1] = (u16)(offsets[i] + huffman->count[i]);

    for (int i = 0; i < total; i++)
    {
        if (lengths[i] != 0)
            huffman->symbol[offsets[lengths[i]]++] = (u16)i;
    }
}

static int gus_decode(Gus_Bits* bits, const Gus_Huffman* huffman)
{
    int code = 0;
    int first = 0;
    int index = 0;

    for (int length = 1; length < 14; length++)
    {
        code |= (int)gus_bits(bits, 1) ^ 1;

        if (bits->failed)
            return -1;

        int count = huffman->count[length];

        if (code < first + count)
            return huffman->symbol[index + (code - first)];

        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }

    return -1;
}

GusImport::GusImport()
{
    m_patches = 0;
    m_result = GUS_IMPORT_ERROR;
}

GusImport::~GusImport()
{
}

bool GusImport::AddSource(const char* path)
{
    if (!IsValidPointer(path) || (path[0] == 0))
        return Fail(GUS_IMPORT_ERROR, "No UltraSound install disks were given");

    std::string source(path);
    std::vector<std::string> folders;
    std::vector<std::string> files;

    if (MediaFile::ListDirectory(source.c_str(), folders, files))
    {
        if (IsInstalled(source))
            return AddInstalled(source, "", 0);

        return AddFolder(source, 0);
    }

    if (ends_with_no_case(source.c_str(), ".zip"))
    {
        std::vector<u8> archive;

        if (!MediaFile::ReadFile(source.c_str(), k_gus_max_zip_size, &archive))
            return Fail(GUS_IMPORT_ERROR, "Unable to read %s", source.c_str());

        return AddZipData(archive.data(), archive.size(), 0);
    }

    std::string directory = path_directory(source);

    if (IsInstalled(directory))
        return AddInstalled(directory, "", 0);

    return AddFolder(directory, 0);
}

bool GusImport::IsInstalled(const std::string& path) const
{
    std::vector<std::string> folders;
    std::vector<std::string> files;

    if (!MediaFile::ListDirectory(path.c_str(), folders, files) || (find_no_case(files, k_gus_ini) < 0))
        return false;

    int found = find_no_case(folders, k_gus_midi);
    std::string midi = found >= 0 ? folders[found] : "";
    folders.clear();
    files.clear();

    if (midi.empty() || !MediaFile::ListDirectory(path_join(path, midi).c_str(), folders, files))
        return false;

    for (size_t i = 0; i < files.size(); i++)
    {
        if (ends_with_no_case(files[i].c_str(), ".pat"))
            return true;
    }

    return false;
}

bool GusImport::AddInstalled(const std::string& path, const std::string& prefix, int depth)
{
    std::vector<std::string> folders;
    std::vector<std::string> files;

    if ((depth > k_gus_max_depth) || !MediaFile::ListDirectory(path.c_str(), folders, files))
        return Fail(GUS_IMPORT_ERROR, "Unable to read the folder %s", path.c_str());

    std::sort(files.begin(), files.end());
    std::sort(folders.begin(), folders.end());

    for (size_t i = 0; i < files.size(); i++)
    {
        std::vector<u8> data;

        if ((files[i][0] == '.') ||
            !MediaFile::ReadFile(path_join(path, files[i]).c_str(), k_gus_max_installed_size, &data))
            continue;

        m_installed.push_back(std::make_pair(prefix + to_upper_ascii(files[i]),
            std::string((const char*)data.data(), data.size())));
    }

    for (size_t i = 0; i < folders.size(); i++)
    {
        if ((folders[i][0] != '.') &&
            !AddInstalled(path_join(path, folders[i]), prefix + to_upper_ascii(folders[i]) + "/", depth + 1))
            return false;
    }

    return true;
}

bool GusImport::AddFolder(const std::string& path, int depth)
{
    std::vector<std::string> folders;
    std::vector<std::string> files;

    if (!MediaFile::ListDirectory(path.c_str(), folders, files))
        return Fail(GUS_IMPORT_ERROR, "Unable to read the folder %s", path.c_str());

    std::sort(files.begin(), files.end());
    std::sort(folders.begin(), folders.end());

    for (size_t i = 0; i < files.size(); i++)
    {
        std::string name = to_upper_ascii(files[i]);
        std::string full = path_join(path, files[i]);
        std::vector<u8> data;

        if ((files[i][0] == '.') || ends_with_no_case(name, ".zip"))
        {
            if ((files[i][0] != '.') && MediaFile::ReadFile(full.c_str(), k_gus_max_zip_size, &data) &&
                !AddZipData(data.data(), data.size(), depth))
                return false;

            continue;
        }

        if (FloppyImage::IsImageName(name.c_str()))
        {
            if (MediaFile::ReadFile(full.c_str(), k_gus_max_image_size, &data) &&
                !AddImage(data.data(), (u32)data.size(), name))
                return false;

            continue;
        }

        if (!is_wanted(name))
        {
            AddFile(name, NULL, 0);
            continue;
        }

        if (!MediaFile::ReadFile(full.c_str(), k_gus_max_volume_size, &data))
            return Fail(GUS_IMPORT_ERROR, "Unable to read %s", full.c_str());

        AddFile(name, data.data(), data.size());
    }

    for (size_t i = 0; (depth < k_gus_max_depth) && (i < folders.size()); i++)
    {
        if ((folders[i][0] != '.') && !AddFolder(path_join(path, folders[i]), depth + 1))
            return false;
    }

    return true;
}

bool GusImport::AddZipData(const u8* data, size_t size, int depth)
{
    MediaZip zip;

    if (!zip.Open(data, size))
        return Fail(GUS_IMPORT_ERROR, "Not a ZIP file Geardox reads");

    bool ok = true;

    for (mz_uint i = 0; ok && (i < mz_zip_reader_get_num_files(zip.GetArchive())); i++)
    {
        mz_zip_archive_file_stat stat;

        if (!mz_zip_reader_file_stat(zip.GetArchive(), i, &stat) || stat.m_is_directory)
            continue;

        std::string name = to_upper_ascii(path_filename(stat.m_filename));
        bool nested = ends_with_no_case(name, ".zip");
        bool image = FloppyImage::IsImageName(name.c_str());

        if ((name[0] == '.') || (stat.m_uncomp_size > k_gus_max_zip_size) || (!nested && !image && !is_wanted(name)))
        {
            if ((name[0] != '.') && !nested && !image)
                AddFile(name, NULL, 0);

            continue;
        }

        std::string bytes;

        if (!zip.Extract(i, &bytes))
        {
            ok = Fail(GUS_IMPORT_ERROR, "Unable to read %s in the ZIP", name.c_str());
            continue;
        }

        const u8* extracted = (const u8*)bytes.data();

        if (nested)
            ok = (depth >= k_gus_max_depth) || AddZipData(extracted, bytes.size(), depth + 1);
        else if (image)
            ok = AddImage(extracted, (u32)bytes.size(), name);
        else
            AddFile(name, extracted, bytes.size());
    }

    return ok;
}

bool GusImport::AddImage(const u8* data, u32 size, const std::string& name)
{
    std::vector<u8> raw;
    DosFs_Raw_Result loaded = dos_fs_load_raw(data, size, &raw);

    if (loaded == DOS_FS_RAW_UNKNOWN)
        return Fail(GUS_IMPORT_ERROR, "Not a floppy image Geardox reads: %s", name.c_str());

    if (loaded == DOS_FS_RAW_NOT_512)
        return Fail(GUS_IMPORT_ERROR, "Not a disk of 512-byte sectors: %s", name.c_str());

    if (!ReadFloppy(raw.data(), (u32)raw.size()))
        return Fail(GUS_IMPORT_ERROR, "Not a FAT12 floppy: %s", name.c_str());

    return true;
}

bool GusImport::ReadFloppy(const u8* raw, u32 size)
{
    DosFs_Fat12 fs;

    if (!dos_fs_fat12_open(raw, size, &fs))
        return false;

    std::string name;
    u32 index = 0;
    const u8* entry = NULL;

    while (IsValidPointer(entry = dos_fs_fat12_next(fs, &index, &name)))
    {
        if (!is_wanted(name))
        {
            AddFile(name, NULL, 0);
            continue;
        }

        std::string content;

        if (!dos_fs_fat12_read(fs, entry, &content))
            return false;

        AddFile(name, (const u8*)content.data(), content.size());
    }

    return true;
}

void GusImport::AddFile(const std::string& name, const u8* data, size_t size)
{
    m_names.insert(name);

    if (IsValidPointer(data) && (m_files.count(name) == 0))
        m_files[name] = std::string((const char*)data, size);
}

bool GusImport::ExpandImplode(const u8* data, size_t size, size_t expected, std::string* out)
{
    Gus_Huffman literals;
    Gus_Huffman lengths;
    Gus_Huffman distances;
    Gus_Bits bits = { data, size, 0, 0, 0, false };
    out->clear();
    gus_build(&literals, k_gus_lit_length, sizeof(k_gus_lit_length));
    gus_build(&lengths, k_gus_len_length, sizeof(k_gus_len_length));
    gus_build(&distances, k_gus_dist_length, sizeof(k_gus_dist_length));
    u32 coded = gus_bits(&bits, 8);
    u32 window = gus_bits(&bits, 8);

    if (bits.failed || (coded > 1) || (window < 4) || (window > 6))
        return false;

    out->reserve(expected);

    while (true)
    {
        if (gus_bits(&bits, 1) != 0)
        {
            int symbol = gus_decode(&bits, &lengths);

            if ((symbol < 0) || (symbol > 15))
                return false;

            u32 length = k_gus_len_base[symbol] + gus_bits(&bits, k_gus_len_extra[symbol]);

            if (bits.failed)
                return false;

            if (length == 519)
                break;

            int low = length == 2 ? 2 : (int)window;
            int high = gus_decode(&bits, &distances);

            if (high < 0)
                return false;

            size_t distance = ((size_t)high << low) + gus_bits(&bits, low) + 1;

            if (bits.failed || (distance > out->size()) || (out->size() + length > expected))
                return false;

            size_t from = out->size() - distance;

            for (u32 i = 0; i < length; i++)
                out->push_back((*out)[from + i]);
        }
        else
        {
            int literal = coded != 0 ? gus_decode(&bits, &literals) : (int)gus_bits(&bits, 8);

            if (bits.failed || (literal < 0) || (out->size() >= expected))
                return false;

            out->push_back((char)literal);
        }

        if (bits.failed)
            return false;
    }

    return out->size() == expected;
}

GusImport_Result GusImport::Refuse()
{
    std::string version;
    std::map<std::string, std::string>::const_iterator id = m_files.find("DISK.ID");

    if (id != m_files.end())
    {
        size_t first = id->second.find('\n');
        size_t second = first == std::string::npos ? first : id->second.find_first_of("\r\n", first + 1);

        if (second != std::string::npos)
        {
            version = id->second.substr(first + 1, second - first - 1);
            version.erase(0, version.find_first_not_of("\r "));
        }
    }

    bool lzx = false;
    bool gus = false;
    bool ace = false;

    for (std::set<std::string>::const_iterator it = m_names.begin(); it != m_names.end(); ++it)
    {
        lzx = lzx || ((it->size() > 4) && (it->compare(it->size() - 4, 4, ".LZX") == 0)) || (*it == "ULT0001");
        gus = gus || is_volume_of(*it, "GUS");
        ace = ace || is_volume_of(*it, "ACE");
    }

    if (!version.empty() && (version.size() < 8) && isdigit((unsigned char)version[0]))
        Fail(GUS_IMPORT_UNSUPPORTED, "UltraSound software %s %s", version.c_str(), k_gus_refuse);
    else if (gus)
        Fail(GUS_IMPORT_UNSUPPORTED, "UltraSound software 3.x %s", k_gus_refuse);
    else if (ace)
        Fail(GUS_IMPORT_UNSUPPORTED, "UltraSound ACE software %s", k_gus_refuse);
    else if (lzx)
        Fail(GUS_IMPORT_UNSUPPORTED, "UltraSound software 1.x %s", k_gus_refuse);
    else
        Fail(GUS_IMPORT_UNKNOWN, "No UltraSound 4.11 disks here: ULTRASND.001 isn't there. Import the 4.11 disks, or a "
            "folder with the software installed");

    return m_result;
}

bool GusImport::ReadRecords(const std::string& volume, u32 start, std::vector<Gus_Record>* records, size_t* end)
{
    const u8* table = (const u8*)volume.data();
    size_t size = volume.size();
    size_t at = start;
    records->clear();

    if (at >= size)
        return false;

    if (table[at] != k_gus_record_mark)
        at += 1 + table[at];

    while ((at + k_gus_record_size <= size) && (table[at] == k_gus_record_mark) && (records->size() < k_gus_max_records))
    {
        const u8* record = table + at;
        Gus_Record item;
        size_t length = 0;

        while ((length < 13) && (record[6 + length] != 0))
            length++;

        item.name = std::string((const char*)record + 6, length);
        item.flag = record[5];
        item.component = record[51];
        item.first_volume = record[32];
        item.last_volume = record[33];
        item.offset = read_u32_le(record + 1);
        item.packed_size = read_u32_le(record + 20);
        item.date = read_u16_le(record + 24);
        item.time = read_u16_le(record + 26);
        item.crc = read_u32_le(record + 28);
        item.size = read_u32_le(record + 35);

        if (!is_dos_name(item.name) || (item.first_volume == 0) || (item.last_volume < item.first_volume) ||
            (item.last_volume > k_gus_max_volumes))
            return false;

        records->push_back(item);
        at += k_gus_record_size;
    }

    *end = at;
    return !records->empty();
}

GusImport_Result GusImport::ImportDisks(const char* zip_path)
{
    std::map<std::string, std::string>::const_iterator first = m_files.find("ULTRASND.001");

    if ((first == m_files.end()) || (m_files.count("DISK.ID") != 0))
        return Refuse();

    const std::string& volume = first->second;
    std::vector<u32> starts(k_gus_max_volumes + 1, 0xFFFFFFFF);
    size_t base_length = strlen(k_gus_volume_base);
    size_t from = volume.size() > k_gus_list_window ? volume.size() - k_gus_list_window : 0;
    const u8* bytes = (const u8*)volume.data();
    starts[0] = 0;

    for (size_t i = from; i + base_length + 9 <= volume.size(); i++)
    {
        if ((bytes[i] != base_length + 4) || (memcmp(bytes + i + 1, k_gus_volume_base, base_length) != 0))
            continue;

        std::string name((const char*)bytes + i + 1, base_length + 4);
        u32 number = is_volume_name(name) ? (u32)atoi(name.c_str() + base_length + 1) : 0;

        if ((number >= 2) && (number <= k_gus_max_volumes) && (starts[number - 1] == 0xFFFFFFFF))
            starts[number - 1] = read_u32_le(bytes + i + base_length + 5);
    }

    u32 table_start = starts[1];

    if ((table_start == 0xFFFFFFFF) || (table_start >= volume.size()) ||
        (volume.size() - table_start != k_gus_table_size) ||
        (CalculateCRC32(0, bytes + table_start, k_gus_table_size) != k_gus_table_crc))
    {
        Fail(GUS_IMPORT_UNSUPPORTED, "This UltraSound software %s", k_gus_refuse);
        return m_result;
    }

    std::vector<Gus_Record> records;
    size_t table_end = 0;

    if (!ReadRecords(volume, table_start, &records, &table_end))
    {
        Fail(GUS_IMPORT_UNKNOWN, "The table of files on ULTRASND.001 doesn't read: import the 4.11 disks again");
        return m_result;
    }

    u32 volumes = 1;

    for (size_t i = 0; i < records.size(); i++)
        volumes = MAX(volumes, (u32)records[i].last_volume);

    std::string stream = volume.substr(0, table_start);

    for (u32 number = 2; number <= volumes; number++)
    {
        char name[32];
        snprintf(name, sizeof(name), "%s.%03u", k_gus_volume_base, number);
        std::map<std::string, std::string>::const_iterator next = m_files.find(name);

        if (next == m_files.end())
        {
            Fail(GUS_IMPORT_INCOMPLETE, "%s is missing: it's the disk number %u", name, number);
            return m_result;
        }

        if ((starts[number - 1] != stream.size()) || ((number < volumes) && (starts[number] == 0xFFFFFFFF)))
        {
            Fail(GUS_IMPORT_INCOMPLETE, "%s doesn't fit the disks before it: it may be damaged", name);
            return m_result;
        }

        stream += next->second;
    }

    int midi = -1;
    bool grouped = false;
    std::vector<int> left_out;

    for (size_t g = 0; g < sizeof(k_gus_groups) / sizeof(k_gus_groups[0]); g++)
    {
        size_t length = strlen(k_gus_groups[g].name);

        for (size_t i = table_end; i + k_gus_group_stride <= volume.size(); i++)
        {
            const u8* group = bytes + i;

            if ((group[0] != length) ||
                (memcmp(group + 1, k_gus_groups[g].name, length) != 0) || (group[80] != 0x70) || (group[82] != 0) ||
                (group[83] != 2))
                continue;

            if (g == 0)
            {
                midi = group[81];
                grouped = true;
            }
            else if (k_gus_groups[g].left_out)
                left_out.push_back((int)group[81]);

            break;
        }
    }

    std::vector<Gus_Output> output;
    std::set<std::string> kept;
    int patches = 0;

    for (size_t i = 0; i < records.size(); i++)
    {
        const Gus_Record& record = records[i];
        u64 offset = (u64)record.offset + starts[record.first_volume - 1];

        if ((offset + record.packed_size > stream.size()) || (record.size > k_gus_max_installed_size))
        {
            Fail(GUS_IMPORT_INCOMPLETE, "%s isn't on these disks: they may be damaged", record.name.c_str());
            return m_result;
        }

        const u8* packed = (const u8*)stream.data() + offset;
        std::string data;

        if (record.packed_size == record.size)
            data.assign((const char*)packed, record.size);
        else if (!ExpandImplode(packed, record.packed_size, record.size, &data))
            data.clear();

        if ((data.size() != record.size) || (CalculateCRC32(0, data.data(), data.size()) != record.crc))
        {
            Fail(GUS_IMPORT_INCOMPLETE, "%s doesn't unpack to what the table says: the disk with it may be damaged",
                record.name.c_str());
            return m_result;
        }

        bool skipped = std::find(left_out.begin(), left_out.end(), (int)record.component) != left_out.end();
        bool in_midi = grouped ? (record.component == midi) : (record.flag == 0xC8);
        std::string path = (in_midi ? std::string(k_gus_import_midi) : std::string()) + record.name;

        if ((record.flag != 0xF0) && (record.flag != 0xC8))
            continue;

        if (skipped || !kept.insert(path).second)
            continue;

        Gus_Output item;
        item.path = path;
        item.data = data;
        item.modified = dos_time_to_host(record.date, record.time);
        output.push_back(item);

        if (in_midi && ends_with_no_case(record.name.c_str(), ".pat"))
            patches++;
    }

    if (patches == 0)
    {
        Fail(GUS_IMPORT_INCOMPLETE, "No patches on these disks");
        return m_result;
    }

    return Finish(zip_path, k_gus_family_411, k_gus_name_411, "Gravis UltraSound software 4.11", patches, output);
}

GusImport_Result GusImport::ImportInstalled(const char* zip_path)
{
    std::vector<Gus_Output> output;
    u64 total = 0;
    int patches = 0;

    for (size_t i = 0; i < m_installed.size(); i++)
    {
        const std::string& path = m_installed[i].first;
        total += m_installed[i].second.size();

        if (total > k_gus_max_installed_total)
        {
            Fail(GUS_IMPORT_ERROR, "The folder holds too much for the UltraSound software");
            return m_result;
        }

        Gus_Output item;
        item.path = path;
        item.data = m_installed[i].second;
        item.modified = 0;
        output.push_back(item);

        if ((path.compare(0, strlen(k_gus_import_midi), k_gus_import_midi) == 0) && ends_with_no_case(path.c_str(), ".pat"))
            patches++;
    }

    return Finish(zip_path, k_gus_family_installed, k_gus_name_installed, "Gravis UltraSound software", patches, output);
}

GusImport_Result GusImport::Finish(const char* zip_path, const char* family, const char* name, const char* title, int patches,
    const std::vector<Gus_Output>& output)
{
    char text[128];
    MediaZip zip;
    m_name = name;
    m_patches = patches;
    snprintf(text, sizeof(text), "%s imported, %d patches", title, patches);
    m_message = text;
    snprintf(text, sizeof(text), "id=%s\r\nname=%s\r\npatches=%d\r\nsource=%08X\r\n", family, name, patches,
        GetSourceFingerprint());
    std::string version_text = text;

    if (!zip.BeginArchive())
    {
        Fail(GUS_IMPORT_ERROR, "Unable to make %s", k_gus_import_file);
        return m_result;
    }

    bool ok = zip.AddEntry(k_import_version, version_text.data(), version_text.size(), MZ_DEFAULT_LEVEL);

    for (size_t i = 0; ok && (i < output.size()); i++)
    {
        s64 modified = output[i].modified;
        ok = zip.AddEntry(output[i].path.c_str(), output[i].data.data(), output[i].data.size(), MZ_DEFAULT_LEVEL,
            output[i].modified != 0 ? &modified : NULL);
    }

    if (!ok || !zip.FinishArchive())
    {
        Fail(GUS_IMPORT_ERROR, "Unable to make %s", k_gus_import_file);
        return m_result;
    }

    if (!zip.WriteArchive(zip_path))
    {
        Fail(GUS_IMPORT_ERROR, "Unable to write %s", zip_path);
        return m_result;
    }

    Log("%s: %d patches imported into %s", name, patches, zip_path);
    m_result = GUS_IMPORT_DONE;
    m_error.clear();
    return m_result;
}

GusImport_Result GusImport::Import(const char* zip_path)
{
    m_name.clear();
    m_message.clear();
    m_patches = 0;
    m_result = GUS_IMPORT_ERROR;

    if (!m_installed.empty())
        return ImportInstalled(zip_path);

    if (m_names.empty())
    {
        Fail(GUS_IMPORT_ERROR, "No files in the UltraSound install disks");
        return m_result;
    }

    return ImportDisks(zip_path);
}

const char* GusImport::GetName() const
{
    return m_name.c_str();
}

const char* GusImport::GetMessage() const
{
    return m_message.c_str();
}

int GusImport::GetPatchCount() const
{
    return m_patches;
}

u32 GusImport::GetSourceFingerprint() const
{
    std::vector<std::pair<std::string, u32> > files;
    u32 crc = 0;

    for (std::map<std::string, std::string>::const_iterator it = m_files.begin(); it != m_files.end(); ++it)
        files.push_back(std::make_pair(it->first, CalculateCRC32(0, it->second.data(), it->second.size())));

    for (size_t i = 0; i < m_installed.size(); i++)
        files.push_back(std::make_pair(m_installed[i].first, CalculateCRC32(0, m_installed[i].second.data(),
            m_installed[i].second.size())));

    std::sort(files.begin(), files.end());

    for (size_t i = 0; i < files.size(); i++)
    {
        u8 value[4];
        write_u32_le(value, files[i].second);
        crc = CalculateCRC32(crc, files[i].first.data(), files[i].first.size());
        crc = CalculateCRC32(crc, value, sizeof(value));
    }

    return crc;
}

const char* GusImport::GetError() const
{
    return m_error.c_str();
}

bool GusImport::Load(const char* zip_path, GusImport_Software* software, std::string* error)
{
    std::vector<u8> data;
    software->name.clear();
    software->zip.clear();
    software->patches = 0;
    software->crc = 0;

    MediaZip zip;
    std::string text;

    if (!import_open(zip_path, k_gus_max_zip_size, "UltraSound", &data, &zip, &text, error))
        return false;

    std::string id = import_get_value(text, "id");
    bool ok = ((id == k_gus_family_411) || (id == k_gus_family_installed)) &&
        (mz_zip_reader_locate_file(zip.GetArchive(), k_gus_ini, NULL, 0) >= 0);
    zip.Close();

    if (!ok)
    {
        *error = std::string("The imported UltraSound software is damaged or from a newer Geardox, import it again: ") + zip_path;
        return false;
    }

    software->name = (id == k_gus_family_411) ? k_gus_name_411 : k_gus_name_installed;
    software->patches = atoi(import_get_value(text, "patches").c_str());
    software->zip.assign((const char*)data.data(), data.size());
    software->crc = CalculateCRC32(0, data.data(), data.size());
    error->clear();
    return true;
}

bool GusImport::ReadSourceFingerprint(const char* zip_path, u32* fingerprint)
{
    return import_read_fingerprint(zip_path, k_gus_max_zip_size, fingerprint);
}

bool GusImport::Fail(GusImport_Result result, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    m_error = vformat_string(format, args);
    va_end(args);
    m_result = result;
    Log("UltraSound import: %s", m_error.c_str());
    return false;
}
