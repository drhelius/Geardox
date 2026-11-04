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
    ((Video*)device)->WriteVRAM(offset, value);
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
    video->WriteVRAM(offset, (u8)value);
    video->WriteVRAM(offset + 1, (u8)(value >> 8));
}

void Video::WriteWindow32Callback(void* device, u32 offset, u32 value)
{
    Video* video = (Video*)device;
    video->WriteVRAM(offset, (u8)value);
    video->WriteVRAM(offset + 1, (u8)(value >> 8));
    video->WriteVRAM(offset + 2, (u8)(value >> 16));
    video->WriteVRAM(offset + 3, (u8)(value >> 24));
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

// The window takes the place of the RAM under it and the four planes show in their real addressing for the debugger
bool Video::RegisterMemoryRegions(u32 window_base)
{
    static const char* const k_names[VIDEO_PLANE_COUNT] = { "VGA Plane 0", "VGA Plane 1", "VGA Plane 2", "VGA Plane 3" };
    static const GDOX_Memory_Read8_Fn k_peek[VIDEO_PLANE_COUNT] = { PeekPlane0Callback, PeekPlane1Callback,
        PeekPlane2Callback, PeekPlane3Callback };
    static const GDOX_Memory_Write8_Fn k_poke[VIDEO_PLANE_COUNT] = { PokePlane0Callback, PokePlane1Callback,
        PokePlane2Callback, PokePlane3Callback };

    u32 access = GDOX_DEBUG_REGION_READABLE | GDOX_DEBUG_REGION_WRITABLE | GDOX_DEBUG_REGION_VIDEO;

    if (!m_memory->RegisterHandlerRegion(GDOX_DEBUG_REGION_VIDEO_APERTURE, "VGA Window", k_video_max_window_size,
        window_base, access | GDOX_DEBUG_REGION_EXECUTABLE | GDOX_DEBUG_REGION_MAPPED | GDOX_DEBUG_REGION_OVERLAY, this,
        ReadWindowCallback, WriteWindowCallback, PeekWindowCallback))
        return false;

    m_memory->SetRegionWideHandlers(GDOX_DEBUG_REGION_VIDEO_APERTURE, WriteWindow16Callback, WriteWindow32Callback);
    m_memory->SetRegionWideReadHandlers(GDOX_DEBUG_REGION_VIDEO_APERTURE, ReadWindow16Callback, ReadWindow32Callback);

    for (int plane = 0; plane < VIDEO_PLANE_COUNT; plane++)
    {
        if (!m_memory->RegisterHandlerRegion(GDOX_DEBUG_REGION_VGA_PLANE_0 + plane, k_names[plane], VIDEO_PLANE_SIZE, 0,
            access, this, k_peek[plane], k_poke[plane], k_peek[plane]))
            return false;
    }

    m_mapped_size = 0;
    UpdateVRAMMapping();
    return true;
}

u8 Video::CompareLatches() const
{
    u32 difference = (m_state.latch ^ m_plane_masks[m_state.graphics[k_video_gr_color_compare] & 0x0F]) &
        m_plane_masks[m_state.graphics[k_video_gr_color_dont_care] & 0x0F];
    u8 planes[4];

    memcpy(planes, &difference, sizeof(planes));

    return (u8)~(planes[0] | planes[1] | planes[2] | planes[3]);
}

// Odd/even sends address bit 0 to the plane pair, and address bit 16 takes its place in the offset,
// which is what the CRTC's word mode rotation undoes
u8 Video::ReadVRAM(u32 offset)
{
    u32 address = offset - m_window_start;

    if (address >= m_window_size)
        return 0xFF;

    u32 base;
    u32 plane = m_state.graphics[k_video_gr_read_map] & 0x03;

    if (m_chain4)
    {
        base = address & 0x3FFFC;
        plane = address & 0x03;
    }
    else if (m_odd_even_read)
    {
        base = GetPlaneBase((address & 0xFFFE) | ((address >> 16) & 0x01));
        plane = (plane & 0x02) | (address & 0x01);
    }
    else
        base = GetPlaneBase(address);

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

    u32 plane = m_state.graphics[k_video_gr_read_map] & 0x03;

    if (m_chain4)
        return m_state.vram[address];

    if (m_odd_even_read)
    {
        plane = (plane & 0x02) | (address & 0x01);
        return m_state.vram[GetPlaneBase((address & 0xFFFE) | ((address >> 16) & 0x01)) + plane];
    }

    return m_state.vram[GetPlaneBase(address) + plane];
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

    u32 base;
    u32 planes = m_write_planes;

    if (m_chain4)
    {
        base = address & 0x3FFFC;
        planes &= m_plane_masks[1 << (address & 0x03)];
    }
    else if (m_odd_even_write)
    {
        base = GetPlaneBase((address & 0xFFFE) | ((address >> 16) & 0x01));
        planes &= m_plane_masks[0x05 << (address & 0x01)];
    }
    else
        base = GetPlaneBase(address);

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
    m_write_planes = m_plane_masks[m_state.sequencer[k_video_sr_map_mask] & 0x0F];
    m_set_reset = m_plane_masks[graphics[k_video_gr_set_reset] & 0x0F];
    m_set_reset_enable = m_plane_masks[graphics[k_video_gr_enable_set_reset] & 0x0F];
    m_bit_mask = graphics[k_video_gr_bit_mask] * 0x01010101U;
    m_plain_write = (graphics[k_video_gr_mode] & 0x03) == 0 && (graphics[k_video_gr_rotate] & 0x1F) == 0 &&
        graphics[k_video_gr_enable_set_reset] == 0 && graphics[k_video_gr_bit_mask] == 0xFF;

    UpdateVRAMMapping();
}

// Plain mode 13h writes go straight to host memory, as chain-4 is linear there
// Reads stay on the handlers for the latches
void Video::UpdateVRAMMapping()
{
    u32 size = 0;

    if (m_chain4 && m_plain_write && m_window_start == 0 && m_window_size != 0 &&
        (m_state.sequencer[k_video_sr_map_mask] & 0x0F) == 0x0F)
        size = m_window_size;

    if (size == m_mapped_size || !IsValidPointer(m_memory))
        return;

    m_mapped_size = size;

    int id = GDOX_DEBUG_REGION_VIDEO_APERTURE;

    m_memory->SetRegionHandlerRange(id, size, k_video_max_window_size - size, false);
    m_memory->SetRegionData(id, NULL, size != 0 ? m_state.vram : NULL);
}
