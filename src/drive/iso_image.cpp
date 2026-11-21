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

#include <time.h>
#include <algorithm>
#include <map>
#include <set>
#include "iso_image.h"
#include "../media/media_file.h"
#include "../media/crc.h"

static const u32 k_iso_sector_size = 2048;
static const u32 k_iso_descriptor_sector = 16;
static const u32 k_iso_path_table_sector = 18;
static const u32 k_iso_record_size = 33;
static const u32 k_iso_max_identifier = 14;
static const u32 k_iso_label_size = 32;
static const u32 k_iso_fingerprint_offset = 883;
static const int k_iso_max_level = 8;
static const size_t k_iso_max_entries = 131072;
static const size_t k_iso_max_directories = 65535;
static const u64 k_iso_max_file_size = 0xFFFFFFFFULL;
static const u32 k_iso_copy_size = 1024 * 1024;
static const char k_iso_standard[] = "CD001";
static const char k_iso_system[] = "GEARDOX";
static const char k_iso_label[] = "CDROM";
static const char k_iso_tag[] = "GDOX";

static const char* const k_iso_clutter[] =
{
    "thumbs.db", "desktop.ini", "__macosx", "$recycle.bin", "system volume information"
};

static const char* const k_iso_devices[] =
{
    "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "LPT1", "LPT2", "LPT3", "CLOCK$"
};

static bool is_name_character(char c)
{
    return ((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')) || (c == '_');
}

static bool is_clutter(const std::string& name)
{
    if (name.empty() || (name[0] == '.'))
        return true;

    std::string lower = to_lower_ascii(name);

    for (size_t i = 0; i < sizeof(k_iso_clutter) / sizeof(k_iso_clutter[0]); i++)
    {
        if (lower == k_iso_clutter[i])
            return true;
    }

    return false;
}

static void split_name(const std::string& name, std::string* base, std::string* extension)
{
    size_t dot = name.find_last_of('.');

    if ((dot == std::string::npos) || (dot == 0))
    {
        *base = name;
        extension->clear();
        return;
    }

    *base = name.substr(0, dot);
    *extension = name.substr(dot + 1);
}

static std::string clean_part(const std::string& text)
{
    std::string result;

    for (size_t i = 0; i < text.size(); i++)
    {
        u8 byte = (u8)text[i];

        if ((byte == ' ') || (byte == '.') || ((byte & 0xC0) == 0x80))
            continue;

        char c = to_upper_ascii(text[i]);
        result += is_name_character(c) ? c : '_';
    }

    return result;
}

static bool is_clean_part(const std::string& text, size_t max_size)
{
    if (text.size() > max_size)
        return false;

    for (size_t i = 0; i < text.size(); i++)
    {
        if (!is_name_character(to_upper_ascii(text[i])))
            return false;
    }

    return true;
}

static bool is_clean_name(const std::string& name, bool directory)
{
    std::string base;
    std::string extension;

    if (directory)
        base = name;
    else
        split_name(name, &base, &extension);

    if (base.empty() || !is_clean_part(base, 8) || !is_clean_part(extension, 3))
        return false;

    std::string device = to_upper_ascii(base);

    for (size_t i = 0; i < sizeof(k_iso_devices) / sizeof(k_iso_devices[0]); i++)
    {
        if (device == k_iso_devices[i])
            return false;
    }

    return true;
}

static std::string pad(const std::string& text, size_t size)
{
    std::string result = text.substr(0, size);
    result.resize(size, ' ');
    return result;
}

static void write_both_16(u8* p, u16 value)
{
    write_u16_le(p, value);
    write_u16_be(p + 2, value);
}

static void write_both_32(u8* p, u32 value)
{
    write_u32_le(p, value);
    write_u32_be(p + 4, value);
}

static bool get_time_fields(s64 modified, struct tm* local, int* offset)
{
    struct tm utc;

    if ((modified == 0) || !get_host_time((time_t)modified, false, local) || !get_host_time((time_t)modified, true, &utc))
        return false;

    int days = 0;

    if (local->tm_year != utc.tm_year)
        days = (local->tm_year > utc.tm_year) ? 1 : -1;
    else
        days = local->tm_yday - utc.tm_yday;

    int minutes = (days * 1440) + ((local->tm_hour - utc.tm_hour) * 60) + (local->tm_min - utc.tm_min);
    *offset = CLAMP(minutes / 15, -48, 52);
    return true;
}

static void write_record_time(u8* out, s64 modified)
{
    struct tm local;
    int offset = 0;
    memset(out, 0, 7);

    if (!get_time_fields(modified, &local, &offset))
        return;

    out[0] = (u8)CLAMP(local.tm_year, 0, 255);
    out[1] = (u8)(local.tm_mon + 1);
    out[2] = (u8)local.tm_mday;
    out[3] = (u8)local.tm_hour;
    out[4] = (u8)local.tm_min;
    out[5] = (u8)local.tm_sec;
    out[6] = (u8)(s8)offset;
}

static void write_volume_time(u8* out, s64 modified)
{
    struct tm local;
    int offset = 0;
    memset(out, '0', 16);
    out[16] = 0;

    if (!get_time_fields(modified, &local, &offset))
        return;

    char text[32];
    snprintf(text, sizeof(text), "%04d%02d%02d%02d%02d%02d00", CLAMP(local.tm_year + 1900, 1, 9999), local.tm_mon + 1,
        local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
    memcpy(out, text, 16);
    out[16] = (u8)(s8)offset;
}

static u32 get_identifier(const IsoImage_Entry& entry, u8* identifier)
{
    u32 size = 0;

    for (u32 i = 0; (i < 8) && (entry.short_name[i] != ' '); i++)
        identifier[size++] = (u8)entry.short_name[i];

    if (entry.directory)
        return size;

    identifier[size++] = '.';

    for (u32 i = 8; (i < 11) && (entry.short_name[i] != ' '); i++)
        identifier[size++] = (u8)entry.short_name[i];

    identifier[size++] = ';';
    identifier[size++] = '1';
    return size;
}

static void add_record(std::vector<u8>* content, const u8* identifier, u32 identifier_size, u32 extent, u32 length,
    bool directory, s64 modified)
{
    u32 size = (k_iso_record_size + identifier_size + 1) & ~1U;
    u32 used = (u32)(content->size() % k_iso_sector_size);

    if ((used + size) > k_iso_sector_size)
        content->resize(content->size() + (k_iso_sector_size - used), 0);

    size_t start = content->size();
    content->resize(start + size, 0);
    u8* record = content->data() + start;
    record[0] = (u8)size;
    write_both_32(record + 2, extent);
    write_both_32(record + 10, length);
    write_record_time(record + 18, modified);
    record[25] = directory ? 0x02 : 0x00;
    write_both_16(record + 28, 1);
    record[32] = (u8)identifier_size;
    memcpy(record + 33, identifier, identifier_size);
}

IsoImage::IsoImage()
{
    InitPointer(m_output);
    m_label[0] = 0;
    m_left_out = 0;
    m_path_size = 0;
    m_path_sectors = 0;
    m_total = 0;
    m_volume_time = 0;
    Clear();
    SetLabel(k_iso_label);
}

IsoImage::~IsoImage()
{
    SafeDelete(m_output);
    Clear();
}

bool IsoImage::AddFolder(const char* path)
{
    Clear();

    if (!IsValidPointer(path) || !WalkFolder(path, 0, 1))
        return false;

    u64 size = 0;
    s64 modified = 0;

    if (MediaFile::GetFileInfo(path, &size, &modified))
        m_entries[0].modified = modified;

    for (size_t i = 1; i < m_entries.size(); i++)
    {
        if (!m_entries[i].directory && (m_entries[i].modified > m_volume_time))
            m_volume_time = m_entries[i].modified;
    }

    AssignNames(0);
    return true;
}

void IsoImage::SetLabel(const char* label)
{
    std::string text(label);
    std::string result;

    for (size_t i = 0; (i < text.size()) && (result.size() < k_iso_label_size); i++)
    {
        u8 byte = (u8)text[i];

        if ((byte & 0xC0) == 0x80)
            continue;

        char c = to_upper_ascii(text[i]);
        result += is_name_character(c) ? c : '_';
    }

    memcpy(m_label, result.c_str(), result.size() + 1);
}

u32 IsoImage::GetFingerprint() const
{
    u32 crc = CalculateCRC32(0, m_label, strlen(m_label));
    HashEntries(0, &crc);
    return crc;
}

u32 IsoImage::GetLeftOut() const
{
    return m_left_out;
}

bool IsoImage::Write(const char* path)
{
    if (!Layout())
    {
        Error("The files don't fit in an ISO 9660 image: %s", path);
        return false;
    }

    std::string temporary = std::string(path) + ".tmp";
    m_output = MediaFile::OpenNewFile(temporary.c_str());

    if (!IsValidPointer(m_output))
    {
        Error("Unable to create the CD image %s", temporary.c_str());
        return false;
    }

    m_buffer.resize(k_iso_copy_size);

    u8 sector[k_iso_sector_size] = { };
    std::vector<u8> table;

    bool ok = m_output->WriteAt((u64)(m_total - 1) * k_iso_sector_size, sector, k_iso_sector_size);

    WriteDescriptor(sector);

    ok = ok && m_output->WriteAt((u64)k_iso_descriptor_sector * k_iso_sector_size, sector, k_iso_sector_size);

    memset(sector, 0, sizeof(sector));
    sector[0] = 255;
    memcpy(sector + 1, k_iso_standard, 5);
    sector[6] = 1;

    ok = ok && m_output->WriteAt((u64)(k_iso_descriptor_sector + 1) * k_iso_sector_size, sector, k_iso_sector_size);

    BuildPathTable(false, &table);

    ok = ok && m_output->WriteAt((u64)k_iso_path_table_sector * k_iso_sector_size, table.data(), table.size());

    BuildPathTable(true, &table);

    ok = ok && m_output->WriteAt((u64)(k_iso_path_table_sector + m_path_sectors) * k_iso_sector_size, table.data(),
        table.size());

    ok = ok && WriteDirectories() && CopyFiles() && m_output->Flush();

    SafeDelete(m_output);
    std::vector<u8>().swap(m_buffer);

    ok = ok && MediaFile::MoveIntoPlace(temporary.c_str(), path);

    if (!ok)
    {
        MediaFile::RemoveFile(temporary.c_str());
        Error("Unable to write the CD image %s", path);
    }

    return ok;
}

bool IsoImage::ReadFingerprint(const char* path, u32* fingerprint)
{
    MediaFile* file = MediaFile::OpenFile(path);
    u8 sector[k_iso_sector_size];

    if (!IsValidPointer(file))
        return false;

    bool ok = file->ReadAt((u64)k_iso_descriptor_sector * k_iso_sector_size, sector, k_iso_sector_size);
    SafeDelete(file);
    ok = ok && (sector[0] == 1) && (memcmp(sector + 1, k_iso_standard, 5) == 0) &&
        (memcmp(sector + k_iso_fingerprint_offset, k_iso_tag, 4) == 0);

    if (ok)
        *fingerprint = read_u32_le(sector + k_iso_fingerprint_offset + 4);

    return ok;
}

void IsoImage::Clear()
{
    m_entries.clear();
    m_entries.resize(1);
    m_entries[0].directory = true;
    m_entries[0].size = 0;
    m_entries[0].modified = 0;
    m_entries[0].parent = 0;
    m_entries[0].extent = 0;
    m_entries[0].length = 0;
    memset(m_entries[0].short_name, ' ', sizeof(m_entries[0].short_name));
    m_order.clear();
    m_left_out = 0;
    m_volume_time = 0;
}

bool IsoImage::WalkFolder(const std::string& path, u32 directory, int level)
{
    std::vector<std::string> folders;
    std::vector<std::string> files;

    if (!MediaFile::ListDirectory(path.c_str(), folders, files))
        return false;

    for (size_t i = 0; i < files.size(); i++)
    {
        std::string item(path);
        append_path_component(item, files[i].c_str());
        u64 size = 0;
        s64 modified = 0;

        if (is_clutter(files[i]))
            continue;

        if ((m_entries.size() >= k_iso_max_entries) || !MediaFile::GetFileInfo(item.c_str(), &size, &modified) ||
            (size > k_iso_max_file_size))
        {
            m_left_out++;
            continue;
        }

        u32 entry = AddEntry(directory, files[i], false);
        m_entries[entry].source = item;
        m_entries[entry].size = size;
        m_entries[entry].modified = modified;
    }

    for (size_t i = 0; i < folders.size(); i++)
    {
        std::string item(path);
        append_path_component(item, folders[i].c_str());
        u64 size = 0;
        s64 modified = 0;

        if (is_clutter(folders[i]) || MediaFile::IsFolderLink(item.c_str()))
            continue;

        if ((level >= k_iso_max_level) || (m_entries.size() >= k_iso_max_entries))
        {
            m_left_out++;
            continue;
        }

        u32 entry = AddEntry(directory, folders[i], true);

        if (MediaFile::GetFileInfo(item.c_str(), &size, &modified))
            m_entries[entry].modified = modified;

        if (!WalkFolder(item, entry, level + 1))
            m_left_out++;
    }

    return true;
}

u32 IsoImage::AddEntry(u32 directory, const std::string& name, bool is_directory)
{
    u32 index = (u32)m_entries.size();
    m_entries.resize(index + 1);
    IsoImage_Entry& entry = m_entries[index];
    entry.name = name;
    memset(entry.short_name, ' ', sizeof(entry.short_name));
    entry.directory = is_directory;
    entry.size = 0;
    entry.modified = 0;
    entry.parent = directory;
    entry.extent = 0;
    entry.length = 0;
    m_entries[directory].children.push_back(index);
    return index;
}

void IsoImage::AssignNames(u32 directory)
{
    std::vector<u32>& children = m_entries[directory].children;
    std::vector<std::pair<std::pair<bool, std::string>, std::pair<std::string, u32> > > order;

    for (size_t i = 0; i < children.size(); i++)
    {
        const IsoImage_Entry& entry = m_entries[children[i]];
        order.push_back(std::make_pair(std::make_pair(!is_clean_name(entry.name, entry.directory), to_upper_ascii(entry.name)),
            std::make_pair(entry.name, children[i])));
    }

    std::sort(order.begin(), order.end());
    std::set<std::string> taken;
    std::map<std::string, u32> tails;

    for (size_t i = 0; i < order.size(); i++)
    {
        IsoImage_Entry& entry = m_entries[order[i].second.second];
        std::string base;
        std::string extension;

        if (entry.directory)
            base = entry.name;
        else
            split_name(entry.name, &base, &extension);

        std::string short_base = clean_part(base);
        std::string short_extension = clean_part(extension).substr(0, 3);

        if (short_base.empty())
            short_base = "_";

        std::string candidate = pad(short_base, 8) + pad(short_extension, 3);

        if (!is_clean_name(entry.name, entry.directory) || (taken.count(candidate) != 0))
        {
            std::string key = short_base + "." + short_extension;
            u32& tail = tails[key];

            do
            {
                char number[16];
                snprintf(number, sizeof(number), "_%u", ++tail);
                size_t room = 8 - strlen(number);
                candidate = pad(short_base.substr(0, room) + number, 8) + pad(short_extension, 3);
            }
            while (taken.count(candidate) != 0);
        }

        taken.insert(candidate);
        memcpy(entry.short_name, candidate.data(), sizeof(entry.short_name));

        if (entry.directory)
            AssignNames(order[i].second.second);
    }

    std::vector<std::pair<std::string, u32> > sorted;

    for (size_t i = 0; i < children.size(); i++)
    {
        const IsoImage_Entry& entry = m_entries[children[i]];
        sorted.push_back(std::make_pair(std::string(entry.short_name, 11), children[i]));
    }

    std::sort(sorted.begin(), sorted.end());

    for (size_t i = 0; i < sorted.size(); i++)
        children[i] = sorted[i].second;
}

void IsoImage::HashEntries(u32 directory, u32* crc) const
{
    for (size_t i = 0; i < m_entries[directory].children.size(); i++)
    {
        const IsoImage_Entry& entry = m_entries[m_entries[directory].children[i]];
        u8 data[16] = { };

        if (!entry.directory)
        {
            write_u32_le(data, (u32)entry.size);
            write_u32_le(data + 4, (u32)(entry.size >> 32));
            write_u32_le(data + 8, (u32)entry.modified);
            write_u32_le(data + 12, (u32)(entry.modified >> 32));
        }

        *crc = CalculateCRC32(*crc, entry.name.data(), entry.name.size());
        *crc = CalculateCRC32(*crc, data, sizeof(data));

        if (entry.directory)
            HashEntries(m_entries[directory].children[i], crc);
    }
}

bool IsoImage::Layout()
{
    m_order.clear();
    m_order.push_back(0);

    for (size_t i = 0; i < m_order.size(); i++)
    {
        const std::vector<u32>& children = m_entries[m_order[i]].children;

        for (size_t j = 0; j < children.size(); j++)
        {
            if (m_entries[children[j]].directory)
                m_order.push_back(children[j]);
        }
    }

    if (m_order.size() > k_iso_max_directories)
        return false;

    std::vector<u8> content;
    BuildPathTable(false, &content);
    m_path_size = (u32)content.size();
    m_path_sectors = (m_path_size + k_iso_sector_size - 1) / k_iso_sector_size;
    u64 next = k_iso_path_table_sector + (2 * m_path_sectors);

    for (size_t i = 0; i < m_order.size(); i++)
    {
        IsoImage_Entry& entry = m_entries[m_order[i]];
        BuildDirectory(m_order[i], &content);
        entry.length = (u32)content.size();
        entry.extent = (u32)next;
        next += content.size() / k_iso_sector_size;
    }

    for (size_t i = 0; i < m_order.size(); i++)
    {
        const std::vector<u32>& children = m_entries[m_order[i]].children;

        for (size_t j = 0; j < children.size(); j++)
        {
            IsoImage_Entry& entry = m_entries[children[j]];

            if (entry.directory)
                continue;

            u64 sectors = (entry.size + k_iso_sector_size - 1) / k_iso_sector_size;
            entry.extent = (u32)next;
            next += MAX(sectors, (u64)1);
        }
    }

    if (next > 0xFFFFFFFFULL)
        return false;

    m_total = (u32)next;
    return true;
}

void IsoImage::BuildDirectory(u32 directory, std::vector<u8>* content) const
{
    const IsoImage_Entry& entry = m_entries[directory];
    const IsoImage_Entry& parent = m_entries[entry.parent];
    const u8 self_identifier[1] = { 0 };
    const u8 parent_identifier[1] = { 1 };
    content->clear();
    add_record(content, self_identifier, 1, entry.extent, entry.length, true, entry.modified);
    add_record(content, parent_identifier, 1, parent.extent, parent.length, true, parent.modified);

    for (size_t i = 0; i < entry.children.size(); i++)
    {
        const IsoImage_Entry& child = m_entries[entry.children[i]];
        u8 identifier[k_iso_max_identifier];
        u32 size = get_identifier(child, identifier);
        add_record(content, identifier, size, child.extent, child.directory ? child.length : (u32)child.size,
            child.directory, child.modified);
    }

    content->resize((content->size() + k_iso_sector_size - 1) / k_iso_sector_size * k_iso_sector_size, 0);
}

void IsoImage::BuildPathTable(bool big_endian, std::vector<u8>* content) const
{
    std::vector<u32> numbers(m_entries.size(), 1);
    content->clear();

    for (size_t i = 0; i < m_order.size(); i++)
    {
        const IsoImage_Entry& entry = m_entries[m_order[i]];
        u8 identifier[k_iso_max_identifier] = { };
        u32 size = (i == 0) ? 1 : get_identifier(entry, identifier);
        u8 record[8] = { };
        numbers[m_order[i]] = (u32)i + 1;
        record[0] = (u8)size;

        if (big_endian)
        {
            write_u32_be(record + 2, entry.extent);
            write_u16_be(record + 6, (u16)numbers[entry.parent]);
        }
        else
        {
            write_u32_le(record + 2, entry.extent);
            write_u16_le(record + 6, (u16)numbers[entry.parent]);
        }

        content->insert(content->end(), record, record + sizeof(record));
        content->insert(content->end(), identifier, identifier + size);

        if ((size & 1) != 0)
            content->push_back(0);
    }
}

void IsoImage::WriteDescriptor(u8* sector) const
{
    const IsoImage_Entry& root = m_entries[0];
    memset(sector, 0, k_iso_sector_size);
    memset(sector + 8, ' ', 32);
    memset(sector + 40, ' ', 32);
    memset(sector + 190, ' ', 128 * 4);
    memset(sector + 702, ' ', 37 * 3);
    sector[0] = 1;
    memcpy(sector + 1, k_iso_standard, 5);
    sector[6] = 1;
    memcpy(sector + 8, k_iso_system, strlen(k_iso_system));
    memcpy(sector + 40, m_label, strlen(m_label));
    write_both_32(sector + 80, m_total);
    write_both_16(sector + 120, 1);
    write_both_16(sector + 124, 1);
    write_both_16(sector + 128, (u16)k_iso_sector_size);
    write_both_32(sector + 132, m_path_size);
    write_u32_le(sector + 140, k_iso_path_table_sector);
    write_u32_be(sector + 148, k_iso_path_table_sector + m_path_sectors);
    std::vector<u8> record;
    const u8 self_identifier[1] = { 0 };
    add_record(&record, self_identifier, 1, root.extent, root.length, true, root.modified);
    memcpy(sector + 156, record.data(), 34);
    memcpy(sector + 574, k_iso_system, strlen(k_iso_system));
    write_volume_time(sector + 813, m_volume_time);
    write_volume_time(sector + 830, m_volume_time);
    write_volume_time(sector + 847, 0);
    write_volume_time(sector + 864, m_volume_time);
    sector[881] = 1;
    memcpy(sector + k_iso_fingerprint_offset, k_iso_tag, 4);
    write_u32_le(sector + k_iso_fingerprint_offset + 4, GetFingerprint());
}

bool IsoImage::WriteDirectories()
{
    std::vector<u8> content;

    for (size_t i = 0; i < m_order.size(); i++)
    {
        BuildDirectory(m_order[i], &content);

        if (!m_output->WriteAt((u64)m_entries[m_order[i]].extent * k_iso_sector_size, content.data(), content.size()))
            return false;
    }

    return true;
}

bool IsoImage::CopyFiles()
{
    for (size_t i = 0; i < m_order.size(); i++)
    {
        const std::vector<u32>& children = m_entries[m_order[i]].children;

        for (size_t j = 0; j < children.size(); j++)
        {
            const IsoImage_Entry& entry = m_entries[children[j]];

            if (entry.directory || (entry.size == 0))
                continue;

            MediaFile* file = MediaFile::OpenFile(entry.source.c_str());

            if (!IsValidPointer(file))
            {
                Error("Unable to read %s", entry.source.c_str());
                return false;
            }

            u64 offset = (u64)entry.extent * k_iso_sector_size;
            u64 left = entry.size;
            bool ok = true;

            while (ok && (left != 0))
            {
                u32 chunk = (u32)MIN(left, (u64)m_buffer.size());
                ok = file->ReadExact(m_buffer.data(), chunk) && m_output->WriteAt(offset, m_buffer.data(), chunk);
                offset += chunk;
                left -= chunk;
            }

            SafeDelete(file);

            if (!ok)
            {
                Error("Unable to copy %s", entry.source.c_str());
                return false;
            }
        }
    }

    return true;
}
