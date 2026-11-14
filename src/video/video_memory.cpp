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

#include "video.h"
#include "video_inline.h"
#include "../system/memory.h"

u8 Video::ReadWindowCallback(void* device, u32 offset)
{
    return ((Video*)device)->ReadVRAM(offset);
}

u8 Video::PeekWindowCallback(void* device, u32 offset)
{
    return ((Video*)device)->PeekVRAM(offset);
}

void Video::WriteWindowCallback(void* device, u32 offset, u8 value)
{
    Video* video = (Video*)device;
    video->SynchronizeVRAM();
    video->WriteVRAM(offset, value);
}

// Every byte of a wide access is its own ISA transfer, with its own latch load and pipeline pass
u16 Video::ReadWindow16Callback(void* device, u32 offset)
{
    Video* video = (Video*)device;
    u16 value = video->ReadVRAM(offset);
    value |= (u16)video->ReadVRAM(offset + 1) << 8;
    return value;
}

u32 Video::ReadWindow32Callback(void* device, u32 offset)
{
    Video* video = (Video*)device;
    u32 value = video->ReadVRAM(offset);
    value |= (u32)video->ReadVRAM(offset + 1) << 8;
    value |= (u32)video->ReadVRAM(offset + 2) << 16;
    value |= (u32)video->ReadVRAM(offset + 3) << 24;
    return value;
}

void Video::WriteWindow16Callback(void* device, u32 offset, u16 value)
{
    Video* video = (Video*)device;
    video->SynchronizeVRAM();
    video->WriteVRAM(offset, (u8)value);
    video->WriteVRAM(offset + 1, (u8)(value >> 8));
}

void Video::WriteWindow32Callback(void* device, u32 offset, u32 value)
{
    Video* video = (Video*)device;
    video->SynchronizeVRAM();
    video->WriteVRAM(offset, (u8)value);
    video->WriteVRAM(offset + 1, (u8)(value >> 8));
    video->WriteVRAM(offset + 2, (u8)(value >> 16));
    video->WriteVRAM(offset + 3, (u8)(value >> 24));
}

// The linear aperture reaches video memory through the same pipeline as the window
u8 Video::ReadLinearCallback(void* device, u32 offset)
{
    Video* video = (Video*)device;
    return video->ReadMemory(offset & video->m_vram_mask);
}

u8 Video::PeekLinearCallback(void* device, u32 offset)
{
    Video* video = (Video*)device;
    return video->PeekMemory(offset & video->m_vram_mask);
}

void Video::WriteLinearCallback(void* device, u32 offset, u8 value)
{
    Video* video = (Video*)device;
    video->SynchronizeVRAM();
    video->WriteMemory(offset & video->m_vram_mask, value);
}

u16 Video::ReadLinear16Callback(void* device, u32 offset)
{
    Video* video = (Video*)device;
    u32 mask = video->m_vram_mask;
    u16 value = video->ReadMemory(offset & mask);
    value |= (u16)video->ReadMemory((offset + 1) & mask) << 8;
    return value;
}

u32 Video::ReadLinear32Callback(void* device, u32 offset)
{
    Video* video = (Video*)device;
    u32 mask = video->m_vram_mask;
    u32 value = video->ReadMemory(offset & mask);
    value |= (u32)video->ReadMemory((offset + 1) & mask) << 8;
    value |= (u32)video->ReadMemory((offset + 2) & mask) << 16;
    value |= (u32)video->ReadMemory((offset + 3) & mask) << 24;
    return value;
}

void Video::WriteLinear16Callback(void* device, u32 offset, u16 value)
{
    Video* video = (Video*)device;
    u32 mask = video->m_vram_mask;
    video->SynchronizeVRAM();
    video->WriteMemory(offset & mask, (u8)value);
    video->WriteMemory((offset + 1) & mask, (u8)(value >> 8));
}

void Video::WriteLinear32Callback(void* device, u32 offset, u32 value)
{
    Video* video = (Video*)device;
    u32 mask = video->m_vram_mask;
    video->SynchronizeVRAM();
    video->WriteMemory(offset & mask, (u8)value);
    video->WriteMemory((offset + 1) & mask, (u8)(value >> 8));
    video->WriteMemory((offset + 2) & mask, (u8)(value >> 16));
    video->WriteMemory((offset + 3) & mask, (u8)(value >> 24));
}

u8 Video::PeekPlane0Callback(void* device, u32 offset)
{
    return ((Video*)device)->PeekPlane(0, offset);
}

u8 Video::PeekPlane1Callback(void* device, u32 offset)
{
    return ((Video*)device)->PeekPlane(1, offset);
}

u8 Video::PeekPlane2Callback(void* device, u32 offset)
{
    return ((Video*)device)->PeekPlane(2, offset);
}

u8 Video::PeekPlane3Callback(void* device, u32 offset)
{
    return ((Video*)device)->PeekPlane(3, offset);
}

void Video::PokePlane0Callback(void* device, u32 offset, u8 value)
{
    ((Video*)device)->PokePlane(0, offset, value);
}

void Video::PokePlane1Callback(void* device, u32 offset, u8 value)
{
    ((Video*)device)->PokePlane(1, offset, value);
}

void Video::PokePlane2Callback(void* device, u32 offset, u8 value)
{
    ((Video*)device)->PokePlane(2, offset, value);
}

void Video::PokePlane3Callback(void* device, u32 offset, u8 value)
{
    ((Video*)device)->PokePlane(3, offset, value);
}

u8 Video::CompareLatches() const
{
    u32 difference = (m_state.latch ^ m_plane_masks[m_state.graphics[k_video_gr_color_compare] & 0x0F]) &
        m_plane_masks[m_state.graphics[k_video_gr_color_dont_care] & 0x0F];
    u8 planes[4];

    memcpy(planes, &difference, sizeof(planes));

    return (u8)~(planes[0] | planes[1] | planes[2] | planes[3]);
}

u8 Video::ReadVRAM(u32 offset)
{
    u32 address = offset - m_window_start;

    if (address >= m_window_size)
        return 0xFF;

    address = GetWindowAddress(address);

    if (address >= m_window_limit)
        return 0xFF;

    return ReadMemory(address & m_vram_mask);
}

// Odd/even sends address bit 0 to the plane pair
u8 Video::ReadMemory(u32 address)
{
    u32 base;
    u32 plane = m_state.graphics[k_video_gr_read_map] & 0x03;

    if (m_chain4)
    {
        base = address & (m_vram_mask & ~0x03U);
        plane = address & 0x03;
    }
    else
    {
        base = GetPlaneBase(GetHostOffset(address));

        if (m_odd_even_read)
            plane = (plane & 0x02) | (address & 0x01);
    }

    memcpy(&m_state.latch, m_state.vram + base, sizeof(m_state.latch));

    if ((m_state.graphics[k_video_gr_mode] & 0x08) != 0)
        return CompareLatches();

    return m_state.vram[base + plane];
}

// What the window shows without loading the latches, as read mode 0 would see it
u8 Video::PeekVRAM(u32 offset) const
{
    u32 address = offset - m_window_start;

    if (address >= m_window_size)
        return 0xFF;

    address = GetWindowAddress(address);

    if (address >= m_window_limit)
        return 0xFF;

    return PeekMemory(address & m_vram_mask);
}

u8 Video::PeekMemory(u32 address) const
{
    u32 plane = m_state.graphics[k_video_gr_read_map] & 0x03;

    if (m_chain4)
        return m_state.vram[address];

    if (m_odd_even_read)
        plane = (plane & 0x02) | (address & 0x01);

    return m_state.vram[GetPlaneBase(GetHostOffset(address)) + plane];
}

u32 Video::GetWriteData(u8 value) const
{
    u8 control = m_state.graphics[k_video_gr_rotate];
    u8 rotate = control & 0x07;
    u32 latch = m_state.latch;
    u32 mask = m_bit_mask;
    u32 rotated = (u8)((value >> rotate) | (value << ((8 - rotate) & 0x07))) * 0x01010101U;
    u32 data;

    switch (m_state.graphics[k_video_gr_mode] & 0x03)
    {
        case 0:
            data = (rotated & ~m_set_reset_enable) | (m_set_reset & m_set_reset_enable);
            break;
        case 1:
            return latch;
        case 2:
            data = m_plane_masks[value & 0x0F];
            break;
        default:
            mask &= rotated;
            data = m_set_reset;
            break;
    }

    switch ((control >> 3) & 0x03)
    {
        case 1:
            data &= latch;
            break;
        case 2:
            data |= latch;
            break;
        case 3:
            data ^= latch;
            break;
        default:
            break;
    }

    return (data & mask) | (latch & ~mask);
}

void Video::WriteVRAM(u32 offset, u8 value)
{
    u32 address = offset - m_window_start;

    if (address >= m_window_size)
        return;

    address = GetWindowAddress(address);

    if (address < m_window_limit)
        WriteMemory(address & m_vram_mask, value);
}

void Video::WriteMemory(u32 address, u8 value)
{
    u32 base;
    u32 planes = m_write_planes;

    if (m_chain4)
    {
        base = address & (m_vram_mask & ~0x03U);
        planes &= m_plane_masks[1 << (address & 0x03)];
    }
    else
    {
        base = GetPlaneBase(GetHostOffset(address));

        if (m_odd_even_write)
            planes &= m_plane_masks[0x05 << (address & 0x01)];
    }

    u32 data = m_plain_write ? value * 0x01010101U : GetWriteData(value);
    u8* memory = m_state.vram + base;
    u32 current;

    memcpy(&current, memory, sizeof(current));
    current = (current & ~planes) | (data & planes);
    memcpy(memory, &current, sizeof(current));
}

// Everything the CPU's path through video memory depends on, worked out once per register write
void Video::UpdateMemoryAccess()
{
    static const u32 k_starts[4] = { 0x00000, 0x00000, 0x10000, 0x18000 };
    static const u32 k_sizes[4] = { 0x20000, 0x10000, 0x08000, 0x08000 };

    const u8* graphics = m_state.graphics;
    u8 memory_mode = m_state.sequencer[k_video_sr_memory_mode];
    u8 map = (graphics[k_video_gr_misc] >> 2) & 0x03;

    m_window_start = k_starts[map];
    m_window_size = (m_state.misc & 0x02) != 0 ? k_sizes[map] : 0;
    m_chain4 = (memory_mode & 0x08) != 0;
    m_odd_even_write = (memory_mode & 0x04) == 0;
    m_odd_even_read = (graphics[k_video_gr_mode] & 0x10) != 0;
    m_odd_even_address = (graphics[k_video_gr_misc] & 0x02) != 0;
    m_write_planes = m_plane_masks[m_state.sequencer[k_video_sr_map_mask] & 0x0F];
    m_set_reset = m_plane_masks[graphics[k_video_gr_set_reset] & 0x0F];
    m_set_reset_enable = m_plane_masks[graphics[k_video_gr_enable_set_reset] & 0x0F];
    m_bit_mask = graphics[k_video_gr_bit_mask] * 0x01010101U;
    m_plain_write = (graphics[k_video_gr_mode] & 0x03) == 0 && (graphics[k_video_gr_rotate] & 0x1F) == 0 &&
        graphics[k_video_gr_enable_set_reset] == 0 && graphics[k_video_gr_bit_mask] == 0xFF;

    // A bank past the installed memory reaches no DRAM on the Cirrus, a VGA wraps its window inside its 256 KB
    if (m_cirrus)
    {
        m_window_limit = m_chain4 ? m_vram_size : m_vram_size >> 2;
        u8 extensions = graphics[k_video_gr_extensions];
        u32 granularity = (extensions & 0x20) != 0 ? 0x4000 : 0x1000;

        m_extended = (m_state.sequencer[k_video_sr_extended] & 0x01) != 0;
        m_dual_page = (extensions & 0x01) != 0;
        m_bank_low = graphics[k_video_gr_offset_0] * granularity;
        m_bank_high = graphics[k_video_gr_offset_1] * granularity - 0x8000;
    }

    bool plain = m_chain4 && m_plain_write && (m_state.sequencer[k_video_sr_map_mask] & 0x0F) == 0x0F;

    if (GetDirectWriteSize() != m_mapped_size || m_bank_low != m_mapped_bank ||
        (m_cirrus && (plain ? m_vram_size : 0) != m_aperture_mapped))
        UpdateVRAMMapping();
}

// Plain chain-4 writes go straight to host memory, as chain-4 is linear there, with one bank for the whole window
// Reads stay on the handlers for the latches
u32 Video::GetDirectWriteSize() const
{
    bool plain = m_chain4 && m_plain_write && (m_state.sequencer[k_video_sr_map_mask] & 0x0F) == 0x0F;

    if (!plain || m_window_start != 0 || m_dual_page || m_bank_low >= m_vram_size)
        return 0;

    return MIN(m_window_size, m_vram_size - m_bank_low);
}

u32 Video::GetApertureSize() const
{
    return m_vram_size;
}

void Video::UpdateVRAMMapping()
{
    if (m_cga)
        return;

    m_mapped_size = GetDirectWriteSize();
    m_mapped_bank = m_bank_low;
    m_memory->SetRegionHandlerRange(GDOX_DEBUG_REGION_VIDEO_APERTURE, m_mapped_size,
        k_video_max_window_size - m_mapped_size, false);
    m_memory->SetRegionData(GDOX_DEBUG_REGION_VIDEO_APERTURE, NULL, m_mapped_size != 0 ? m_state.vram + m_bank_low : NULL);

    if (!m_cirrus)
        return;

    bool plain = m_chain4 && m_plain_write && (m_state.sequencer[k_video_sr_map_mask] & 0x0F) == 0x0F;

    m_aperture_mapped = plain ? m_vram_size : 0;
    m_memory->SetRegionHandlerRange(GDOX_DEBUG_REGION_LINEAR_VRAM, m_aperture_mapped,
        GetApertureSize() - m_aperture_mapped, false);
    m_memory->SetRegionData(GDOX_DEBUG_REGION_LINEAR_VRAM, NULL, m_aperture_mapped != 0 ? m_state.vram : NULL);
}

// SR07 bits 7-4 place video memory at that MB, 0 for none; with 16 KB banks a 2 MB block sits on an even MB
void Video::UpdateAperture()
{
    if (!m_cirrus)
        return;

    u32 field = m_state.sequencer[k_video_sr_extended] >> 4;

    if ((m_state.graphics[k_video_gr_extensions] & 0x20) != 0)
        field &= 0x0E;

    m_aperture_base = field << 20;

    if (m_aperture_base != 0)
        m_memory->SetRegionSpan(GDOX_DEBUG_REGION_LINEAR_VRAM, m_aperture_base, GetApertureSize(), NULL,
            m_aperture_mapped != 0 ? m_state.vram : NULL);

    m_memory->SetRegionMapped(GDOX_DEBUG_REGION_LINEAR_VRAM, m_aperture_base != 0);
}
