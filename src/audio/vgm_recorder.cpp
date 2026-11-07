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

#include "vgm_recorder.h"

static const int k_vgm_header_size = 0x100;
static const u32 k_vgm_version = 0x161;
static const u32 k_vgm_rate = 60;

VgmRecorder::VgmRecorder()
{
    m_recording = false;
    m_pending_wait = 0;
    m_total_samples = 0;
    m_clock_rate = 0;
    m_opl3 = false;
    m_dual = false;
}

VgmRecorder::~VgmRecorder()
{
    if (m_recording)
        Stop();
}

void VgmRecorder::Start(const char* file_path, int clock_rate, const GDOX_VGM_Metadata& metadata, bool opl3, bool dual)
{
    if (m_recording)
        return;

    open_ofstream_utf8(m_file, file_path, std::ios::out | std::ios::binary | std::ios::trunc);

    if (!m_file.is_open())
    {
        Error("Unable to open VGM file %s", file_path);
        return;
    }

    m_metadata = metadata;
    m_clock_rate = clock_rate;
    m_opl3 = opl3;
    m_dual = dual;
    m_recording = true;
    m_pending_wait = 0;
    m_total_samples = 0;
    m_command_buffer.clear();
}

void VgmRecorder::Stop()
{
    if (!m_recording)
        return;

    FlushPendingWait();
    WriteCommand(0x66);

    std::vector<u8> gd3_tag;
    BuildGD3Tag(gd3_tag, m_metadata);

    u8 header[k_vgm_header_size];
    memset(header, 0, sizeof(header));

    u32 gd3_offset = k_vgm_header_size + (u32)m_command_buffer.size();
    u32 eof_offset = gd3_offset + (u32)gd3_tag.size() - 4;

    header[0x00] = 'V';
    header[0x01] = 'g';
    header[0x02] = 'm';
    header[0x03] = ' ';
    write_u32_le(header + 0x04, eof_offset);
    write_u32_le(header + 0x08, k_vgm_version);
    write_u32_le(header + 0x14, gd3_offset - 0x14);
    write_u32_le(header + 0x18, (u32)m_total_samples);
    write_u32_le(header + 0x24, k_vgm_rate);
    write_u32_le(header + 0x34, k_vgm_header_size - 0x34);

    // Bit 30 is a second chip and bit 31 pans the pair hard, as the first Pro wires them
    write_u32_le(header + (m_opl3 ? 0x5C : 0x50), (u32)m_clock_rate | (m_dual ? 0xC0000000 : 0));

    m_file.write((const char*)header, sizeof(header));
    m_file.write((const char*)&m_command_buffer[0], m_command_buffer.size());
    m_file.write((const char*)&gd3_tag[0], gd3_tag.size());
    m_file.close();

    m_recording = false;
    m_command_buffer.clear();
}

// 5Ah is a YM3812 and AAh the second one, 5Eh and 5Fh a YMF262's two arrays
void VgmRecorder::WriteOPL(int chip, u16 address, u8 data)
{
    if (!m_recording)
        return;

    FlushPendingWait();

    if (m_opl3)
        WriteCommand((u8)(0x5E + ((address >> 8) & 0x01)), (u8)address, data);
    else
        WriteCommand(chip != 0 ? 0xAA : 0x5A, (u8)address, data);
}

void VgmRecorder::WriteCommand(u8 command)
{
    m_command_buffer.push_back(command);
}

void VgmRecorder::WriteCommand(u8 command, u8 data1, u8 data2)
{
    WriteCommand(command);
    m_command_buffer.push_back(data1);
    m_command_buffer.push_back(data2);
}

void VgmRecorder::WriteWait(int samples)
{
    while (samples > 0)
    {
        if (samples == 735)
        {
            WriteCommand(0x62);
            samples -= 735;
        }
        else if (samples == 882)
        {
            WriteCommand(0x63);
            samples -= 882;
        }
        else if (samples <= 16)
        {
            WriteCommand((u8)(0x70 + (samples - 1)));
            samples = 0;
        }
        else if (samples <= 65535)
        {
            WriteCommand(0x61);
            m_command_buffer.push_back((u8)samples);
            m_command_buffer.push_back((u8)(samples >> 8));
            samples = 0;
        }
        else
        {
            WriteCommand(0x61);
            m_command_buffer.push_back(0xFF);
            m_command_buffer.push_back(0xFF);
            samples -= 65535;
        }
    }
}

void VgmRecorder::FlushPendingWait()
{
    if (m_pending_wait > 0)
    {
        WriteWait(m_pending_wait);
        m_pending_wait = 0;
    }
}

void VgmRecorder::AppendUint32(std::vector<u8>& buffer, u32 value)
{
    u8 data[4];
    write_u32_le(data, value);
    buffer.insert(buffer.end(), data, data + 4);
}

void VgmRecorder::AppendGD3Codepoint(std::vector<u8>& buffer, u32 codepoint)
{
    u8 data[2];

    if (codepoint <= 0xFFFF)
    {
        write_u16_le(data, (u16)codepoint);
        buffer.insert(buffer.end(), data, data + 2);
    }
    else
    {
        codepoint -= 0x10000;
        write_u16_le(data, (u16)(0xD800 + (codepoint >> 10)));
        buffer.insert(buffer.end(), data, data + 2);
        write_u16_le(data, (u16)(0xDC00 + (codepoint & 0x3FF)));
        buffer.insert(buffer.end(), data, data + 2);
    }
}

void VgmRecorder::AppendGD3String(std::vector<u8>& buffer, const char* value)
{
    size_t length = strlen(value);
    size_t index = 0;

    while (index < length)
        AppendGD3Codepoint(buffer, utf8_decode_next(value, length, index));

    AppendGD3Codepoint(buffer, 0);
}

void VgmRecorder::BuildGD3Tag(std::vector<u8>& tag, const GDOX_VGM_Metadata& metadata)
{
    std::vector<u8> data;

    for (int i = 0; i < 11; i++)
    {
        const char* value = "";

        if (i == 2)
            value = metadata.game_name.c_str();
        else if (i == 4)
            value = metadata.system_name.c_str();
        else if (i == 10)
            value = metadata.comment.c_str();

        AppendGD3String(data, value);
    }

    tag.push_back('G');
    tag.push_back('d');
    tag.push_back('3');
    tag.push_back(' ');
    AppendUint32(tag, 0x00000100);
    AppendUint32(tag, (u32)data.size());
    tag.insert(tag.end(), data.begin(), data.end());
}
