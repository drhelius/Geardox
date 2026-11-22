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

#include <stdint.h>
#include "memory.h"
#include "../common/state_serializer.h"

Memory::Memory()
{
    InitPointer(m_cpu_read_pages);
    InitPointer(m_cpu_write_pages);
    InitPointer(m_cpu_page_regions);
    InitPointer(m_cpu_page_bus);
    m_cpu_map_generation = 0;
    m_debug_region_count = 0;
    m_physical_address_mask = 0xFFFFFFFF;
    m_map_address_mask = 0xFFFFFFFF;
    m_a20_enabled = true;
    m_map_generation = 1;
    m_region_generation = 1;
    m_debug_snapshot_id = 1;
    InitPointer(m_working_ram);
    m_working_ram_size = 0;
    m_working_ram_base = 0;
    InitPointer(m_dirty_pages);
    m_dirty_page_count = 0;
    m_dirty_tracking = false;
    m_host_video_pages = true;
    m_dirty_valid = false;
    m_main_ram_size = GDOX_MAIN_RAM_SIZE;
    InitPointer(m_state.main_ram);
    InitPointer(m_video_ram);
    m_video_ram_size = 0;
    InitPointer(m_access_context);
    memset(m_debug_regions, 0, sizeof(m_debug_regions));
#if !defined(GDOX_DISABLE_DYNAREC)
    InitPointer(m_code_pages);
    m_code_page_count = 0;
    InitPointer(m_code_write_context);
    InitPointer(m_code_write_callback);
#endif
}

Memory::~Memory()
{
    SafeDeleteArray(m_cpu_read_pages);
    SafeDeleteArray(m_cpu_write_pages);
    SafeDeleteArray(m_cpu_page_regions);
    SafeDeleteArray(m_cpu_page_bus);
    SafeDeleteArray(m_state.main_ram);
    SafeDeleteArray(m_dirty_pages);
#if !defined(GDOX_DISABLE_DYNAREC)
    SafeDeleteArray(m_code_pages);
#endif
}

void Memory::Init()
{
    if (!IsValidPointer(m_state.main_ram))
        m_state.main_ram = new u8[m_main_ram_size];

    Reset();
}

void Memory::Reset()
{
    ClearDebugRegions();

    if (IsValidPointer(m_state.main_ram))
        memset(m_state.main_ram, 0, m_main_ram_size);
}

INLINE bool Memory::IsCanonicalPage(u32 page) const
{
    return (m_physical_address_mask & 0xFFF) == 0xFFF && ((page << 12) & ~m_physical_address_mask) == 0;
}

// With A20 off the high memory area keeps host pages that alias the first 64 KB, the only wrap real mode reaches
INLINE bool Memory::IsCPUPage(u32 page) const
{
    if (IsCanonicalPage(page))
        return true;

    return !m_a20_enabled && (page & ~0xFFU) == 0x100 && ((page << 12) & ~m_map_address_mask) == 0;
}

INLINE u64 Memory::GetSpanPages(const DebugRegion& region, u64 start, u64 end)
{
    u64 base = region.info.physical_base;
    u64 first = (MAX(start, base) - base) >> 12;
    u64 last = (MIN(end, base + region.info.size) - 1 - base) >> 12;
    u64 count = last - first + 1;

    return (count >= 64 ? ~0ULL : (1ULL << count) - 1) << first;
}

INLINE bool Memory::HoldsPage(const DebugRegion& region, u64 address)
{
    return (region.info.flags & GDOX_DEBUG_REGION_PAGED) == 0 ||
        ((region.pages >> ((address - region.info.physical_base) >> 12)) & 1) != 0;
}

INLINE const Memory::DebugRegion* Memory::FindMappedSpan(u32 physical, u32 size) const
{
    if (size == 0 || (u64)physical + size > 0x100000000ULL)
        return NULL;

    u32 masked_bits = ~m_physical_address_mask;

    if (masked_bits != 0)
    {
        u32 boundary = masked_bits & (0U - masked_bits);

        if ((u64)(physical & (boundary - 1)) + size > boundary)
            return NULL;
    }

    u32 bus_address = NormalizePhysicalAddress(physical);
    u64 span_end = (u64)bus_address + size;
    const DebugRegion* found = FindMappedRegion(bus_address);

    if (!IsValidPointer(found) || span_end > (u64)found->info.physical_base + found->info.size)
        return NULL;

    if ((found->info.flags & GDOX_DEBUG_REGION_PAGED) != 0)
    {
        u64 span = GetSpanPages(*found, bus_address, span_end);

        if ((found->pages & span) != span)
            return NULL;
    }

    bool found_overlay = (found->info.flags & GDOX_DEBUG_REGION_OVERLAY) != 0;

    // A region with priority over the one found must not start inside this span
    for (int i = 0; i < m_debug_region_count; i++)
    {
        const DebugRegion& region = m_debug_regions[i];

        if (&region == found)
            break;

        if ((region.info.flags & GDOX_DEBUG_REGION_MAPPED) == 0)
            continue;

        bool overlay = (region.info.flags & GDOX_DEBUG_REGION_OVERLAY) != 0;
        u64 start = region.info.physical_base;

        if (overlay == found_overlay && start < span_end && start + region.info.size > bus_address &&
            ((region.info.flags & GDOX_DEBUG_REGION_PAGED) == 0 ||
            (region.pages & GetSpanPages(region, bus_address, span_end)) != 0))
            return NULL;
    }

    if (!found_overlay)
    {
        for (int i = 0; i < m_debug_region_count; i++)
        {
            const DebugRegion& region = m_debug_regions[i];
            u64 start = region.info.physical_base;

            if ((region.info.flags & (GDOX_DEBUG_REGION_MAPPED | GDOX_DEBUG_REGION_OVERLAY)) ==
                (GDOX_DEBUG_REGION_MAPPED | GDOX_DEBUG_REGION_OVERLAY) &&
                start < span_end && start + region.info.size > bus_address &&
                ((region.info.flags & GDOX_DEBUG_REGION_PAGED) == 0 ||
                (region.pages & GetSpanPages(region, bus_address, span_end)) != 0))
                return NULL;
        }
    }

    return found;
}

INLINE const Memory::DebugRegion* Memory::FindPageSpan(u32 physical, u32 size) const
{
    if (m_cpu_map_generation == m_map_generation && size != 0 && (u64)(physical & 0xFFF) + size <= 0x1000)
    {
        u8 index = m_cpu_page_regions[physical >> 12];

        if (index != 0)
            return &m_debug_regions[index - 1];
    }

    return FindMappedSpan(physical, size);
}

INLINE bool Memory::IsHandlerSpan(const DebugRegion* region, u32 offset, u32 size) const
{
    return offset < region->handler_offset + region->handler_size && offset + size > region->handler_offset;
}

// Host data under a read, or NULL and the region holding all of it, if there is one
INLINE const u8* Memory::FindReadSpan(u32 physical, u32 size, const DebugRegion*& region) const
{
    if (size != 0 && (u64)(physical & 0xFFF) + size <= 0x1000 && m_cpu_map_generation == m_map_generation &&
        IsValidPointer(m_cpu_read_pages[physical >> 12]))
        return m_cpu_read_pages[physical >> 12] + (physical & 0xFFF);

    region = FindPageSpan(physical, size);

    if (!IsValidPointer(region) || !IsValidPointer(region->read_data) || (region->info.flags & GDOX_DEBUG_REGION_READABLE) == 0)
        return NULL;

    u32 offset = NormalizePhysicalAddress(physical) - region->info.physical_base;

    if (region->handler_reads && IsHandlerSpan(region, offset, size))
        return NULL;

    return region->read_data + offset;
}

// Unmapped and unreadable addresses float high like the I/O space
// Registers move machine time up to the start of their instruction and end the CPU batch
INLINE u8 Memory::ReadRegionBus(const DebugRegion& region, u32 offset, GDOX_Bus_Access_Context& context)
{
    if ((region.info.flags & GDOX_DEBUG_REGION_READABLE) == 0)
        return 0xFF;

    if ((region.info.flags & GDOX_DEBUG_REGION_MMIO) != 0)
        SynchronizeRegister(context);

    if (IsValidPointer(region.read8))
    {
        GDOX_Bus_Access_Context* outer = m_access_context;
        m_access_context = &context;
        u8 value = region.read8(region.device, offset);
        m_access_context = outer;
        return value;
    }

    if (IsValidPointer(region.read_data))
        return region.read_data[offset];

    return 0xFF;
}

INLINE void Memory::MarkDirty(const DebugRegion* region, u32 offset, u32 size)
{
    if (!m_dirty_tracking || !IsValidPointer(m_working_ram) || region->write_data < m_working_ram ||
        region->write_data >= m_working_ram + m_working_ram_size)
        return;

    offset += (u32)(region->write_data - m_working_ram);

    u32 first = offset >> 12;
    u32 last = (offset + size - 1) >> 12;
    bool patch = m_cpu_map_generation == m_map_generation && IsValidPointer(m_cpu_read_pages);

    for (u32 page = first; page <= last && page < m_dirty_page_count; page++)
    {
        if (m_dirty_pages[page] != 0)
            continue;

        m_dirty_pages[page] = 1;

        if (patch)
            UpdateRAMPage(page);
    }
}

#if !defined(GDOX_DISABLE_DYNAREC)
// Writes that reach RAM pages holding translated code outside the CPU fast path, from main RAM or a view of it
INLINE void Memory::NotifyCodeWrite(const DebugRegion* region, u32 offset, u32 size, bool changed)
{
    if (!IsValidPointer(m_code_pages) || !IsValidPointer(m_working_ram) || region->write_data < m_working_ram ||
        region->write_data >= m_working_ram + m_working_ram_size)
        return;

    offset += (u32)(region->write_data - m_working_ram);

    u32 first = offset >> 12;
    u32 last = (offset + size - 1) >> 12;

    for (u32 page = first; page <= last && page < m_code_page_count; page++)
    {
        if (m_code_pages[page] != 0)
        {
            m_code_write_callback(m_code_write_context, offset, size, changed);
            return;
        }
    }
}
#endif

// What a write needs once the address has found its region, from the CPU's view of the bus or the DMA's
INLINE void Memory::WriteRegionBus(const DebugRegion* region, u32 bus_address, u8 value, GDOX_Bus_Access_Context& context)
{
    if (!IsValidPointer(region) ||
        (!IsValidPointer(region->write_data) && !IsValidPointer(region->write8)) ||
        (region->info.flags & GDOX_DEBUG_REGION_WRITABLE) == 0)
        return;

    if ((region->info.flags & GDOX_DEBUG_REGION_MMIO) != 0)
        SynchronizeRegister(context);

    u32 offset = bus_address - region->info.physical_base;

    if (IsValidPointer(context.observe_memory_write))
    {
        u8 previous = 0;
        ReadRegion(*region, offset, previous);
        context.observe_memory_write(context.memory_write_context, bus_address, previous, value);
    }

    if (IsValidPointer(region->write8))
    {
        GDOX_Bus_Access_Context* outer = m_access_context;
        u32 generation = m_map_generation;
        m_access_context = &context;
        region->write8(region->device, offset, value);
        m_access_context = outer;

        if (m_map_generation != generation)
            context.end_batch = true;
    }
    else
    {
#if !defined(GDOX_DISABLE_DYNAREC)
        bool changed = region->write_data[offset] != value;
#endif
        region->write_data[offset] = value;
        MarkDirty(region, offset, 1);
#if !defined(GDOX_DISABLE_DYNAREC)
        NotifyCodeWrite(region, offset, 1, changed);
#endif
    }

    m_debug_snapshot_id++;
}

u8 Memory::Read8Physical(u32 physical, GDOX_Bus_Access_Context& context)
{
    if (m_cpu_map_generation == m_map_generation && IsValidPointer(m_cpu_read_pages[physical >> 12]))
        return m_cpu_read_pages[physical >> 12][physical & 0xFFF];

    return ReadBus(NormalizePhysicalAddress(physical), context);
}

// Both bytes in one region: its 16-bit handler, or its byte accesses without looking it up again
u16 Memory::Read16Physical(u32 physical, GDOX_Bus_Access_Context& context)
{
    const DebugRegion* region = NULL;
    const u8* data = FindReadSpan(physical, 2, region);

    if (IsValidPointer(data))
        return read_u16_le(data);

    if (IsValidPointer(region))
    {
        u32 offset = NormalizePhysicalAddress(physical) - region->info.physical_base;

        if (IsValidPointer(region->read16) && (region->info.flags & GDOX_DEBUG_REGION_READABLE) != 0)
        {
            GDOX_Bus_Access_Context* outer = m_access_context;
            m_access_context = &context;
            u16 value = region->read16(region->device, offset);
            m_access_context = outer;
            return value;
        }

        // A span across pages may have host data on one of them
        if ((physical & 0xFFF) != 0xFFF)
        {
            u16 value = ReadRegionBus(*region, offset, context);
            value |= (u16)ReadRegionBus(*region, offset + 1, context) << 8;
            return value;
        }
    }

    u16 value = Read8Physical(physical, context);
    value |= (u16)Read8Physical(physical + 1, context) << 8;
    return value;
}

u32 Memory::Read32Physical(u32 physical, GDOX_Bus_Access_Context& context)
{
    const DebugRegion* region = NULL;
    const u8* data = FindReadSpan(physical, 4, region);

    if (IsValidPointer(data))
        return read_u32_le(data);

    if (IsValidPointer(region) && IsValidPointer(region->read32) && (region->info.flags & GDOX_DEBUG_REGION_READABLE) != 0)
    {
        GDOX_Bus_Access_Context* outer = m_access_context;
        m_access_context = &context;
        u32 value = region->read32(region->device, NormalizePhysicalAddress(physical) - region->info.physical_base);
        m_access_context = outer;
        return value;
    }

    u32 value = Read16Physical(physical, context);
    value |= (u32)Read16Physical(physical + 2, context) << 16;
    return value;
}

void Memory::Write8Physical(u32 physical, u8 value, GDOX_Bus_Access_Context& context)
{
    WriteBus(NormalizePhysicalAddress(physical), value, context);
}

void Memory::Write16Physical(u32 physical, u16 value, GDOX_Bus_Access_Context& context)
{
    if (!IsValidPointer(context.observe_memory_write))
    {
        const DebugRegion* region = FindPageSpan(physical, 2);

        if (IsValidPointer(region) && (region->info.flags & GDOX_DEBUG_REGION_WRITABLE) != 0)
        {
            u32 offset = NormalizePhysicalAddress(physical) - region->info.physical_base;

            if (IsValidPointer(region->write_data) && !IsHandlerSpan(region, offset, 2))
            {
#if !defined(GDOX_DISABLE_DYNAREC)
                bool changed = read_u16_le(region->write_data + offset) != value;
#endif
                write_u16_le(region->write_data + offset, value);
                m_debug_snapshot_id += 2;
                MarkDirty(region, offset, 2);
#if !defined(GDOX_DISABLE_DYNAREC)
                NotifyCodeWrite(region, offset, 2, changed);
#endif
                return;
            }

            if (IsValidPointer(region->write16))
            {
                GDOX_Bus_Access_Context* outer = m_access_context;
                u32 generation = m_map_generation;
                m_access_context = &context;
                region->write16(region->device, offset, value);
                m_access_context = outer;
                m_debug_snapshot_id += 2;

                if (m_map_generation != generation)
                    context.end_batch = true;

                return;
            }
        }
    }

    Write8Physical(physical, (u8)value, context);
    Write8Physical(physical + 1, (u8)(value >> 8), context);
}

void Memory::Write32Physical(u32 physical, u32 value, GDOX_Bus_Access_Context& context)
{
    if (!IsValidPointer(context.observe_memory_write))
    {
        const DebugRegion* region = FindPageSpan(physical, 4);

        if (IsValidPointer(region) && (region->info.flags & GDOX_DEBUG_REGION_WRITABLE) != 0)
        {
            u32 offset = NormalizePhysicalAddress(physical) - region->info.physical_base;

            if (IsValidPointer(region->write_data) && !IsHandlerSpan(region, offset, 4))
            {
#if !defined(GDOX_DISABLE_DYNAREC)
                bool changed = read_u32_le(region->write_data + offset) != value;
#endif
                write_u32_le(region->write_data + offset, value);
                m_debug_snapshot_id += 4;
                MarkDirty(region, offset, 4);
#if !defined(GDOX_DISABLE_DYNAREC)
                NotifyCodeWrite(region, offset, 4, changed);
#endif
                return;
            }

            if (IsValidPointer(region->write32))
            {
                GDOX_Bus_Access_Context* outer = m_access_context;
                u32 generation = m_map_generation;
                m_access_context = &context;
                region->write32(region->device, offset, value);
                m_access_context = outer;
                m_debug_snapshot_id += 4;

                if (m_map_generation != generation)
                    context.end_batch = true;

                return;
            }
        }
    }

    Write16Physical(physical, (u16)value, context);
    Write16Physical(physical + 2, (u16)(value >> 16), context);
}

void Memory::ClearDebugRegions()
{
    memset(m_debug_regions, 0, sizeof(m_debug_regions));
    m_debug_region_count = 0;
    InitPointer(m_working_ram);
    m_working_ram_size = 0;
    m_working_ram_base = 0;
    SafeDeleteArray(m_dirty_pages);
    m_dirty_page_count = 0;
    m_dirty_valid = false;
    InitPointer(m_video_ram);
    m_video_ram_size = 0;
    m_map_generation++;
    m_region_generation++;
    m_debug_snapshot_id++;
#if !defined(GDOX_DISABLE_DYNAREC)
    SafeDeleteArray(m_code_pages);
    m_code_page_count = 0;
#endif
}

bool Memory::RegisterDebugRegion(int id, const char* name, const u8* read_data, u8* write_data, u32 size, u32 physical_base, u32 flags)
{
    if (id <= 0 ||
        !IsValidPointer(name) ||
        name[0] == 0 ||
        size == 0 ||
        m_debug_region_count >= GDOX_DEBUG_MEMORY_MAX_REGIONS ||
        IsValidPointer(FindRegion(id)))
        return false;

    if ((flags & GDOX_DEBUG_REGION_READABLE) != 0 && !IsValidPointer(read_data))
        return false;

    if ((flags & GDOX_DEBUG_REGION_WRITABLE) != 0 && !IsValidPointer(write_data))
        return false;

    if ((flags & (GDOX_DEBUG_REGION_MAPPED | GDOX_DEBUG_REGION_OVERLAY)) != 0 && (u64)physical_base + size > 0x100000000ULL)
        return false;

    DebugRegion& region = m_debug_regions[m_debug_region_count++];
    memset(&region, 0, sizeof(region));
    region.info.id = id;
    strncpy_fit(region.info.name, name, sizeof(region.info.name));
    region.info.size = size;
    region.info.physical_base = physical_base;
    region.info.flags = flags;
    region.read_data = read_data;
    region.write_data = write_data;

    UpdateRAMRegions();
    m_map_generation++;
    m_region_generation++;
    m_debug_snapshot_id++;
    return true;
}

// Handler regions only get host pages for the data given with SetRegionData, other CPU accesses reach the device
// A read handler with side effects needs a peek handler for passive reads
bool Memory::RegisterHandlerRegion(int id, const char* name, u32 size, u32 physical_base, u32 flags, void* device,
    GDOX_Memory_Read8_Fn read8, GDOX_Memory_Write8_Fn write8, GDOX_Memory_Read8_Fn peek8)
{
    if (id <= 0 ||
        !IsValidPointer(name) ||
        name[0] == 0 ||
        size == 0 ||
        m_debug_region_count >= GDOX_DEBUG_MEMORY_MAX_REGIONS ||
        IsValidPointer(FindRegion(id)))
        return false;

    if ((flags & GDOX_DEBUG_REGION_READABLE) != 0 && !IsValidPointer(read8))
        return false;

    if ((flags & GDOX_DEBUG_REGION_WRITABLE) != 0 && !IsValidPointer(write8))
        return false;

    if ((flags & (GDOX_DEBUG_REGION_MAPPED | GDOX_DEBUG_REGION_OVERLAY)) != 0 && (u64)physical_base + size > 0x100000000ULL)
        return false;

    DebugRegion& region = m_debug_regions[m_debug_region_count++];
    memset(&region, 0, sizeof(region));
    region.info.id = id;
    strncpy_fit(region.info.name, name, sizeof(region.info.name));
    region.info.size = size;
    region.info.physical_base = physical_base;
    region.info.flags = flags;
    region.device = device;
    region.read8 = read8;
    region.write8 = write8;
    region.peek8 = peek8;

    m_map_generation++;
    m_region_generation++;
    m_debug_snapshot_id++;
    return true;
}

bool Memory::SetRegionWideHandlers(int id, GDOX_Memory_Write16_Fn write16, GDOX_Memory_Write32_Fn write32)
{
    DebugRegion* region = FindRegion(id);

    if (!IsValidPointer(region) || !IsValidPointer(region->write8))
        return false;

    region->write16 = write16;
    region->write32 = write32;
    return true;
}

bool Memory::SetRegionWideReadHandlers(int id, GDOX_Memory_Read16_Fn read16, GDOX_Memory_Read32_Fn read32)
{
    DebugRegion* region = FindRegion(id);

    if (!IsValidPointer(region) || !IsValidPointer(region->read8))
        return false;

    region->read16 = read16;
    region->read32 = read32;
    return true;
}

// A banked view switches on or off, only the host pages it covers are rebuilt
bool Memory::SetRegionPages(int id, u64 pages)
{
    DebugRegion* region = FindRegion(id);

    if (!IsValidPointer(region) || region->info.size > 64 * 0x1000)
        return false;

    if ((region->info.flags & GDOX_DEBUG_REGION_PAGED) != 0 && region->pages == pages)
        return true;

    region->info.flags |= GDOX_DEBUG_REGION_PAGED;
    region->pages = pages;
    m_region_generation++;
    RemapRange(region->info.physical_base, region->info.size);
    return true;
}

bool Memory::SetRegionOverlay(int id, bool overlay)
{
    DebugRegion* region = FindRegion(id);

    if (!IsValidPointer(region))
        return false;

    if (((region->info.flags & GDOX_DEBUG_REGION_OVERLAY) != 0) == overlay)
        return true;

    if (overlay)
        region->info.flags |= GDOX_DEBUG_REGION_OVERLAY;
    else
        region->info.flags &= ~GDOX_DEBUG_REGION_OVERLAY;

    m_region_generation++;
    RemapRange(region->info.physical_base, region->info.size);
    return true;
}

bool Memory::SetRegionMapped(int id, bool mapped)
{
    DebugRegion* region = FindRegion(id);

    if (!IsValidPointer(region))
        return false;

    if (((region->info.flags & GDOX_DEBUG_REGION_MAPPED) != 0) == mapped)
        return true;

    if (mapped)
        region->info.flags |= GDOX_DEBUG_REGION_MAPPED;
    else
        region->info.flags &= ~GDOX_DEBUG_REGION_MAPPED;

    RemapRange(region->info.physical_base, region->info.size);
    return true;
}

// Board registers move what a region maps: the pages it leaves and the ones it takes are mapped again
bool Memory::SetRegionSpan(int id, u32 physical_base, u32 size, const u8* read_data, u8* write_data)
{
    DebugRegion* region = FindRegion(id);

    if (!IsValidPointer(region) || size == 0 || (u64)physical_base + size > 0x100000000ULL ||
        ((region->info.flags & GDOX_DEBUG_REGION_READABLE) != 0 && !IsValidPointer(read_data) &&
        !IsValidPointer(region->read8)) ||
        ((region->info.flags & GDOX_DEBUG_REGION_WRITABLE) != 0 && !IsValidPointer(write_data) &&
        !IsValidPointer(region->write8)))
        return false;

    u32 old_base = region->info.physical_base;
    u32 old_size = region->info.size;
    region->info.physical_base = physical_base;
    region->info.size = size;
    region->read_data = read_data;
    region->write_data = write_data;
    m_region_generation++;

    if ((region->info.flags & GDOX_DEBUG_REGION_MAPPED) == 0)
    {
        m_debug_snapshot_id++;
        return true;
    }

    RemapRange(old_base, old_size);
    RemapRange(physical_base, size);
    return true;
}

bool Memory::SetRegionData(int id, const u8* read_data, u8* write_data)
{
    DebugRegion* region = FindRegion(id);

    if (!IsValidPointer(region) ||
        ((region->info.flags & GDOX_DEBUG_REGION_READABLE) != 0 && !IsValidPointer(read_data) &&
        !IsValidPointer(region->read8)) ||
        ((region->info.flags & GDOX_DEBUG_REGION_WRITABLE) != 0 && !IsValidPointer(write_data) &&
        !IsValidPointer(region->write8)))
        return false;

    if (region->read_data == read_data && region->write_data == write_data)
        return true;

    region->read_data = read_data;
    region->write_data = write_data;

    if ((region->info.flags & GDOX_DEBUG_REGION_MAPPED) != 0)
        RemapRange(region->info.physical_base, region->info.size);
    else
        m_debug_snapshot_id++;

    return true;
}

// CPU writes to these pages, and reads too when asked, reach the handlers even though the region has host data
bool Memory::SetRegionHandlerRange(int id, u32 offset, u32 size, bool reads)
{
    DebugRegion* region = FindRegion(id);

    if (size == 0)
        offset = 0;

    if (!IsValidPointer(region) ||
        ((offset | size) & 0xFFF) != 0 ||
        (u64)offset + size > region->info.size ||
        ((region->info.flags & GDOX_DEBUG_REGION_READABLE) != 0 && !IsValidPointer(region->read8)) ||
        ((region->info.flags & GDOX_DEBUG_REGION_WRITABLE) != 0 && !IsValidPointer(region->write8)))
        return false;

    if (region->handler_offset == offset && region->handler_size == size && region->handler_reads == reads)
        return true;

    u32 old_offset = region->handler_offset;
    u32 old_size = region->handler_size;
    region->handler_offset = offset;
    region->handler_size = size;
    region->handler_reads = reads;

    if ((region->info.flags & GDOX_DEBUG_REGION_MAPPED) == 0)
    {
        m_debug_snapshot_id++;
        return true;
    }

    if (old_size != 0)
        RemapRange(region->info.physical_base + old_offset, old_size);

    if (size != 0 && (offset != old_offset || size != old_size))
        RemapRange(region->info.physical_base + offset, size);

    return true;
}

void Memory::SynchronizeRegister(GDOX_Bus_Access_Context& context)
{
    context.end_batch = true;

    if (IsValidPointer(context.synchronize_memory))
        context.synchronize_memory(context.synchronize_context, context);
}

int Memory::GetDebugRegionCount() const
{
    return m_debug_region_count;
}

bool Memory::GetDebugRegion(int index, GDOX_Debug_Memory_Region& region) const
{
    if (index < 0 || index >= m_debug_region_count)
        return false;

    region = m_debug_regions[index].info;
    return true;
}

void Memory::DebugReadRegionBlock(int id, u32 offset, u8* data, GDOX_Debug_Memory_Status* status, u32 size) const
{
    if (!IsValidPointer(data) || !IsValidPointer(status))
        return;

    for (u32 i = 0; i < size; i++)
    {
        u64 current = (u64)offset + i;

        if (current > 0xFFFFFFFFULL)
        {
            data[i] = 0;
            status[i] = GDOX_DEBUG_MEMORY_UNMAPPED;
        }
        else
            status[i] = DebugReadRegion(id, (u32)current, data[i]);
    }
}

bool Memory::DebugWriteRegionBlock(int id, u32 offset, const u8* data, u32 size)
{
    DebugRegion* region = FindRegion(id);

    if (!IsValidPointer(region) ||
        !IsValidPointer(data) ||
        size == 0 ||
        (region->info.flags & GDOX_DEBUG_REGION_WRITABLE) == 0 ||
        !IsValidPointer(region->write_data) ||
        (u64)offset + size > region->info.size)
        return false;

    memcpy(region->write_data + offset, data, size);
    m_debug_snapshot_id++;
    MarkDirty(region, offset, size);
#if !defined(GDOX_DISABLE_DYNAREC)
    NotifyCodeWrite(region, offset, size, true);
#endif
    return true;
}

bool Memory::TryPeekPhysical(u32 physical, u8& value) const
{
    if (m_cpu_map_generation == m_map_generation && IsValidPointer(m_cpu_read_pages[physical >> 12]))
    {
        value = m_cpu_read_pages[physical >> 12][physical & 0xFFF];
        return true;
    }

    GDOX_Debug_Memory_Status status = DebugReadBus(NormalizePhysicalAddress(physical), value);
    return status == GDOX_DEBUG_MEMORY_VALID || status == GDOX_DEBUG_MEMORY_READ_ONLY;
}

const u8* Memory::GetPhysicalReadSpan(u32 physical, u32 size) const
{
    const DebugRegion* region = NULL;
    return FindReadSpan(physical, size, region);
}

bool Memory::TryPeekPhysicalBlock(u32 physical, u8* data, u32 size) const
{
    if (!IsValidPointer(data))
        return false;

    const u8* source = GetPhysicalReadSpan(physical, size);

    if (IsValidPointer(source))
    {
        uintptr_t source_address = (uintptr_t)source;
        uintptr_t destination_address = (uintptr_t)data;
        uintptr_t distance = source_address > destination_address ?
            source_address - destination_address : destination_address - source_address;

        if (distance >= size)
        {
            memcpy(data, source, size);
            return true;
        }
    }

    for (u32 i = 0; i < size; i++)
    {
        if ((u64)physical + i > 0xFFFFFFFFULL || !TryPeekPhysical(physical + i, data[i]))
            return false;
    }

    return true;
}

void Memory::DebugReadPhysicalBlock(u32 physical, u8* data, GDOX_Debug_Memory_Status* status, u32 size) const
{
    if (!IsValidPointer(data) || !IsValidPointer(status))
        return;

    for (u32 i = 0; i < size; i++)
    {
        u64 current = (u64)physical + i;

        if (current > 0xFFFFFFFFULL)
        {
            data[i] = 0;
            status[i] = GDOX_DEBUG_MEMORY_UNMAPPED;
        }
        else
            status[i] = DebugReadBus(NormalizePhysicalAddress((u32)current), data[i]);
    }
}

void Memory::DebugReadBusBlock(u32 bus_address, u8* data, GDOX_Debug_Memory_Status* status, u32 size) const
{
    if (!IsValidPointer(data) || !IsValidPointer(status))
        return;

    for (u32 i = 0; i < size; i++)
    {
        u64 current = (u64)bus_address + i;

        if (current > 0xFFFFFFFFULL)
        {
            data[i] = 0;
            status[i] = GDOX_DEBUG_MEMORY_UNMAPPED;
        }
        else
            status[i] = DebugReadBus((u32)current, data[i]);
    }
}

bool Memory::DebugWritePhysicalBlock(u32 physical, const u8* data, u32 size)
{
    if (!IsValidPointer(data) || size == 0)
        return false;

    for (u32 i = 0; i < size; i++)
    {
        u64 current = (u64)physical + i;

        if (current > 0xFFFFFFFFULL)
            return false;

        u32 bus = NormalizePhysicalAddress((u32)current);
        DebugRegion* region = FindMappedRegion(bus);

        if (!IsValidPointer(region) ||
            (!IsValidPointer(region->write_data) && !IsValidPointer(region->write8)) ||
            (region->info.flags & GDOX_DEBUG_REGION_WRITABLE) == 0)
            return false;
    }

    for (u32 i = 0; i < size; i++)
    {
        u32 bus = NormalizePhysicalAddress(physical + i);
        DebugRegion* region = FindMappedRegion(bus);
        u32 offset = bus - region->info.physical_base;

        if (IsValidPointer(region->write8))
            region->write8(region->device, offset, data[i]);
        else
        {
            region->write_data[offset] = data[i];
            MarkDirty(region, offset, 1);
#if !defined(GDOX_DISABLE_DYNAREC)
            NotifyCodeWrite(region, offset, 1, true);
#endif
        }
    }

    m_debug_snapshot_id++;
    return true;
}

bool Memory::DebugWriteBusBlock(u32 bus_address, const u8* data, u32 size)
{
    if (!IsValidPointer(data) || size == 0)
        return false;

    for (u32 i = 0; i < size; i++)
    {
        u64 current = (u64)bus_address + i;

        if (current > 0xFFFFFFFFULL)
            return false;

        DebugRegion* region = FindMappedRegion((u32)current);

        if (!IsValidPointer(region) ||
            (!IsValidPointer(region->write_data) && !IsValidPointer(region->write8)) ||
            (region->info.flags & GDOX_DEBUG_REGION_WRITABLE) == 0)
            return false;
    }

    for (u32 i = 0; i < size; i++)
    {
        DebugRegion* region = FindMappedRegion(bus_address + i);
        u32 offset = bus_address + i - region->info.physical_base;

        if (IsValidPointer(region->write8))
            region->write8(region->device, offset, data[i]);
        else
        {
            region->write_data[offset] = data[i];
            MarkDirty(region, offset, 1);
#if !defined(GDOX_DISABLE_DYNAREC)
            NotifyCodeWrite(region, offset, 1, true);
#endif
        }
    }

    m_debug_snapshot_id++;
    return true;
}

bool Memory::DebugTranslatePhysical(u32 physical, GDOX_Debug_Memory_Translation& translation) const
{
    translation.physical_valid = true;
    translation.physical = physical;
    translation.bus = NormalizePhysicalAddress(physical);
    return DebugTranslateBus(translation.bus, translation);
}

bool Memory::DebugTranslateBus(u32 bus_address, GDOX_Debug_Memory_Translation& translation) const
{
    translation.bus_valid = true;
    translation.bus = bus_address;
    translation.map_generation = m_map_generation;

    const DebugRegion* region = FindMappedRegion(bus_address);

    if (!IsValidPointer(region))
    {
        strncpy_fit(translation.reason, "Bus address is unmapped", sizeof(translation.reason));
        return false;
    }

    translation.region_valid = true;
    translation.region = region->info.id;
    translation.region_offset = bus_address - region->info.physical_base;
    strncpy_fit(translation.region_name, region->info.name, sizeof(translation.region_name));
    translation.reason[0] = 0;
    return true;
}

// The map's address lines, 24 on a 386SX, A20 masks one of them on top
void Memory::SetPhysicalAddressMask(u32 mask)
{
    m_map_address_mask = mask;
    ApplyAddressMask();
}

void Memory::ApplyAddressMask()
{
    u32 mask = m_a20_enabled ? m_map_address_mask : m_map_address_mask & ~0x00100000U;

    if (m_physical_address_mask != mask)
    {
        m_physical_address_mask = mask;
        m_map_generation++;
        m_debug_snapshot_id++;
    }
}

// Only the pages with address bit 20 change, so a toggle never rebuilds the whole CPU map
void Memory::SetA20(bool enabled)
{
    if (m_a20_enabled == enabled)
        return;

    bool patch = m_cpu_map_generation == m_map_generation && IsValidPointer(m_cpu_read_pages);

    m_a20_enabled = enabled;
    ApplyAddressMask();

    if (!patch)
        return;

    RemapA20Pages();
    m_cpu_map_generation = m_map_generation;
}

bool Memory::IsA20Enabled() const
{
    return m_a20_enabled;
}

void Memory::RemapA20Pages()
{
    for (u32 page = 0x100; page < 0x200; page++)
        UpdateCPUPage(page);

    for (int i = 0; i < m_debug_region_count; i++)
    {
        const DebugRegion& region = m_debug_regions[i];

        if ((region.info.flags & GDOX_DEBUG_REGION_MAPPED) == 0)
            continue;

        u32 first = region.info.physical_base >> 12;
        u32 last = (u32)(((u64)region.info.physical_base + region.info.size - 1) >> 12);

        for (u32 page = MAX(first, 0x200U); page <= last; page++)
        {
            if ((page & 0x100) != 0)
                UpdateCPUPage(page);
        }
    }
}

u32 Memory::GetRegionGeneration() const
{
    return m_region_generation;
}

#if !defined(GDOX_DISABLE_DYNAREC)
void Memory::SetCodeWriteCallback(void* context, GDOX_Memory_Code_Write_Fn callback)
{
    m_code_write_context = context;
    m_code_write_callback = callback;
}

// A RAM page with translated code keeps no CPU write pointer, so every CPU write to it reaches NotifyCodeWrite
// An up to date host page is patched in place, the new generation makes the CPU drop its cached pointers
void Memory::SetCodePage(u32 page, bool code)
{
    if (!IsValidPointer(m_code_pages) || page >= m_code_page_count || (m_code_pages[page] != 0) == code)
        return;

    m_code_pages[page] = code ? 1 : 0;

    bool patch = m_cpu_map_generation == m_map_generation && IsValidPointer(m_cpu_read_pages) &&
        IsValidPointer(m_working_ram);

    m_map_generation++;

    if (!patch)
        return;

    UpdateRAMPage(page);
    m_cpu_map_generation = m_map_generation;
}

// Host memory of a ROM region, which no write ever reaches
bool Memory::IsReadOnlyHost(const u8* host) const
{
    for (int i = 0; i < m_debug_region_count; i++)
    {
        const DebugRegion& region = m_debug_regions[i];

        if ((region.info.flags & GDOX_DEBUG_REGION_ROM) != 0 && IsValidPointer(region.read_data) &&
            !IsValidPointer(region.write_data) && host >= region.read_data && host < region.read_data + region.info.size)
            return true;
    }

    return false;
}
#endif

u64 Memory::GetDebugSnapshotId() const
{
    return m_debug_snapshot_id;
}

void Memory::SetDirtyTracking(bool enabled)
{
    if (m_dirty_tracking == enabled)
        return;

    m_dirty_tracking = enabled;
    m_dirty_valid = false;

    if (IsValidPointer(m_dirty_pages))
        memset(m_dirty_pages, 0, m_dirty_page_count);

    m_map_generation++;
}

void Memory::SetHostVideoPages(bool host)
{
    if (m_host_video_pages == host)
        return;

    m_host_video_pages = host;
    m_map_generation++;
}

bool Memory::IsDirtyMapValid() const
{
    return m_dirty_tracking && m_dirty_valid && IsValidPointer(m_dirty_pages);
}

const u8* Memory::GetDirtyPages() const
{
    return m_dirty_pages;
}

u32 Memory::GetDirtyPageCount() const
{
    return m_dirty_page_count;
}

void Memory::ClearDirtyPages()
{
    if (!m_dirty_tracking || !IsValidPointer(m_dirty_pages))
        return;

    bool patch = m_cpu_map_generation == m_map_generation && IsValidPointer(m_cpu_read_pages);
    bool revoked = false;

    for (u32 page = 0; page < m_dirty_page_count; page++)
    {
        if (m_dirty_pages[page] == 0)
            continue;

        m_dirty_pages[page] = 0;
        revoked = true;

        if (patch)
            UpdateRAMPage(page);
    }

    m_dirty_valid = true;

    if (!revoked)
        return;

    m_map_generation++;

    if (patch)
        m_cpu_map_generation = m_map_generation;
}

u8* Memory::GetWorkingRAM()
{
    return m_working_ram;
}

size_t Memory::GetWorkingRAMSize() const
{
    return m_working_ram_size;
}

u8* Memory::GetMainRAM()
{
    return m_state.main_ram;
}

u32 Memory::GetMainRAMSize() const
{
    return m_main_ram_size;
}

// The reset that follows clears the new RAM and maps it
void Memory::SetMainRAMSize(u32 size)
{
    m_dirty_valid = false;

    if (IsValidPointer(m_state.main_ram) && (size == m_main_ram_size))
        return;

    SafeDeleteArray(m_state.main_ram);
    m_main_ram_size = size;
    m_state.main_ram = new u8[m_main_ram_size];
}

u8* Memory::GetVideoRAM()
{
    return m_video_ram;
}

size_t Memory::GetVideoRAMSize() const
{
    return m_video_ram_size;
}

const Memory::DebugRegion* Memory::FindRegion(int id) const
{
    for (int i = 0; i < m_debug_region_count; i++)
    {
        if (m_debug_regions[i].info.id == id)
            return &m_debug_regions[i];
    }

    return NULL;
}

Memory::DebugRegion* Memory::FindRegion(int id)
{
    for (int i = 0; i < m_debug_region_count; i++)
    {
        if (m_debug_regions[i].info.id == id)
            return &m_debug_regions[i];
    }

    return NULL;
}

// Overlays win over the regions below them, then the earliest registered region wins
const Memory::DebugRegion* Memory::FindMappedRegion(u32 bus_address) const
{
    const DebugRegion* found = NULL;

    for (int i = 0; i < m_debug_region_count; i++)
    {
        const DebugRegion& region = m_debug_regions[i];
        u64 start = region.info.physical_base;
        u64 end = start + region.info.size;

        if ((region.info.flags & GDOX_DEBUG_REGION_MAPPED) == 0 || (u64)bus_address < start || (u64)bus_address >= end)
            continue;

        if (!HoldsPage(region, bus_address))
            continue;

        if ((region.info.flags & GDOX_DEBUG_REGION_OVERLAY) != 0)
            return &region;

        if (!IsValidPointer(found))
            found = &region;
    }

    return found;
}

Memory::DebugRegion* Memory::FindMappedRegion(u32 bus_address)
{
    DebugRegion* found = NULL;

    for (int i = 0; i < m_debug_region_count; i++)
    {
        DebugRegion& region = m_debug_regions[i];
        u64 start = region.info.physical_base;
        u64 end = start + region.info.size;

        if ((region.info.flags & GDOX_DEBUG_REGION_MAPPED) == 0 || (u64)bus_address < start || (u64)bus_address >= end)
            continue;

        if (!HoldsPage(region, bus_address))
            continue;

        if ((region.info.flags & GDOX_DEBUG_REGION_OVERLAY) != 0)
            return &region;

        if (!IsValidPointer(found))
            found = &region;
    }

    return found;
}

GDOX_Debug_Memory_Status Memory::DebugReadRegion(int id, u32 offset, u8& value) const
{
    value = 0;
    const DebugRegion* region = FindRegion(id);

    if (!IsValidPointer(region) || offset >= region->info.size)
        return GDOX_DEBUG_MEMORY_UNMAPPED;

    return ReadRegion(*region, offset, value);
}

GDOX_Debug_Memory_Status Memory::DebugReadBus(u32 bus_address, u8& value) const
{
    value = 0;
    const DebugRegion* region = FindMappedRegion(bus_address);

    if (!IsValidPointer(region))
        return GDOX_DEBUG_MEMORY_UNMAPPED;

    return ReadRegion(*region, bus_address - region->info.physical_base, value);
}

// Debugger reads never call a handler's emulated read, only its peek
GDOX_Debug_Memory_Status Memory::ReadRegion(const DebugRegion& region, u32 offset, u8& value) const
{
    if ((region.info.flags & GDOX_DEBUG_REGION_READABLE) == 0)
        return GDOX_DEBUG_MEMORY_UNAVAILABLE;

    if (IsValidPointer(region.peek8))
        value = region.peek8(region.device, offset);
    else if (IsValidPointer(region.read_data))
        value = region.read_data[offset];
    else
        return GDOX_DEBUG_MEMORY_UNAVAILABLE;

    return (region.info.flags & GDOX_DEBUG_REGION_WRITABLE) != 0 ? GDOX_DEBUG_MEMORY_VALID : GDOX_DEBUG_MEMORY_READ_ONLY;
}

u8 Memory::ReadBus(u32 bus_address, GDOX_Bus_Access_Context& context)
{
    const DebugRegion* region = FindPageSpan(bus_address, 1);

    if (IsValidPointer(region))
        return ReadRegionBus(*region, bus_address - region->info.physical_base, context);

    return 0xFF;
}

void Memory::WriteBus(u32 bus_address, u8 value, GDOX_Bus_Access_Context& context)
{
    WriteRegionBus(FindPageSpan(bus_address, 1), bus_address, value, context);
}

// The A20 gate sits on the CPU's address lines, so the DMA reaches the bus without the CPU's view of it
u8 Memory::Read8DMA(u32 address, GDOX_Bus_Access_Context& context)
{
    const DebugRegion* region = FindMappedRegion(address);

    if (IsValidPointer(region))
        return ReadRegionBus(*region, address - region->info.physical_base, context);

    return 0xFF;
}

void Memory::Write8DMA(u32 address, u8 value, GDOX_Bus_Access_Context& context)
{
    const DebugRegion* region = FindMappedRegion(address);
    WriteRegionBus(region, address, value, context);
}

u32 Memory::NormalizePhysicalAddress(u32 physical) const
{
    return physical & m_physical_address_mask;
}

void Memory::UpdateRAMRegions()
{
    DebugRegion* ram = FindRegion(GDOX_DEBUG_REGION_MAIN_RAM);
    m_working_ram = IsValidPointer(ram) ? ram->write_data : NULL;
    m_working_ram_size = IsValidPointer(ram) ? ram->info.size : 0;
    m_working_ram_base = IsValidPointer(ram) ? ram->info.physical_base : 0;
    m_dirty_valid = false;

    u32 dirty_pages = (u32)((m_working_ram_size + 0xFFF) >> 12);

    if (dirty_pages != m_dirty_page_count)
    {
        SafeDeleteArray(m_dirty_pages);
        m_dirty_page_count = dirty_pages;

        if (dirty_pages != 0)
        {
            m_dirty_pages = new u8[dirty_pages];
            memset(m_dirty_pages, 0, dirty_pages);
        }
    }

#if !defined(GDOX_DISABLE_DYNAREC)
    u32 code_pages = (u32)((m_working_ram_size + 0xFFF) >> 12);

    if (code_pages != m_code_page_count)
    {
        SafeDeleteArray(m_code_pages);
        m_code_page_count = code_pages;

        if (code_pages != 0)
        {
            m_code_pages = new u8[code_pages];
            memset(m_code_pages, 0, code_pages);
        }
    }
#endif

    DebugRegion* vram = FindRegion(GDOX_DEBUG_REGION_VRAM);
    m_video_ram = IsValidPointer(vram) ? vram->write_data : NULL;
    m_video_ram_size = IsValidPointer(vram) ? vram->info.size : 0;
}

void Memory::PrepareCPUMap()
{
    if (m_cpu_map_generation == m_map_generation)
        return;

    const u32 page_count = 1U << 20;

    if (!IsValidPointer(m_cpu_read_pages))
    {
        m_cpu_read_pages = new const u8*[page_count];
        m_cpu_write_pages = new u8*[page_count];
        m_cpu_page_regions = new u8[page_count];
        m_cpu_page_bus = new u8[page_count];
    }

    memset(m_cpu_read_pages, 0, page_count * sizeof(m_cpu_read_pages[0]));
    memset(m_cpu_write_pages, 0, page_count * sizeof(m_cpu_write_pages[0]));
    memset(m_cpu_page_regions, 0, page_count);
    memset(m_cpu_page_bus, 0, page_count);

    for (u32 page = 0; page <= (m_map_address_mask >> 12); page++)
    {
        if (IsCPUPage(page))
            UpdateCPUPage(page);
    }

    m_cpu_map_generation = m_map_generation;
}

void Memory::UpdateCPUPage(u32 page)
{
    UpdateCPUPage(page, IsCPUPage(page) ? FindMappedSpan(page << 12, 0x1000) : NULL);
}

void Memory::UpdateCPUPage(u32 page, const DebugRegion* region)
{
    u32 physical = page << 12;

    m_cpu_page_regions[page] = IsValidPointer(region) ? (u8)(region - m_debug_regions + 1) : 0;
    m_cpu_page_bus[page] = IsValidPointer(region) ? GetBusClass(region->info) : (u8)GDOX_BUS_NONE;
    InitPointer(m_cpu_read_pages[page]);
    InitPointer(m_cpu_write_pages[page]);

    if (!IsValidPointer(region) || (region->info.flags & (GDOX_DEBUG_REGION_MMIO | GDOX_DEBUG_REGION_AUDIO)) != 0)
        return;

    if (!m_host_video_pages && m_cpu_page_bus[page] == GDOX_BUS_VRAM)
        return;

    u32 offset = NormalizePhysicalAddress(physical) - region->info.physical_base;
    bool handler = IsHandlerSpan(region, offset, 0x1000);

    if ((region->info.flags & GDOX_DEBUG_REGION_READABLE) != 0 && IsValidPointer(region->read_data) &&
        !(handler && region->handler_reads))
        m_cpu_read_pages[page] = region->read_data + offset;

    if ((region->info.flags & GDOX_DEBUG_REGION_WRITABLE) != 0 && IsValidPointer(region->write_data) && !handler)
    {
#if !defined(GDOX_DISABLE_DYNAREC)
        if (region->write_data == m_working_ram && IsValidPointer(m_code_pages) && (offset >> 12) < m_code_page_count &&
            m_code_pages[offset >> 12] != 0)
            return;
#endif
        if (AllowsWritePointer(page, region->write_data + offset))
            m_cpu_write_pages[page] = region->write_data + offset;
    }
}

void Memory::UpdateRAMPage(u32 page)
{
    u32 cpu_page = (m_working_ram_base >> 12) + page;

    if (IsCanonicalPage(cpu_page))
        UpdateCPUPage(cpu_page);

    if (!m_a20_enabled && cpu_page < 0x100)
        UpdateCPUPage(cpu_page | 0x100);
}

bool Memory::AllowsWritePointer(u32 page, const u8* host) const
{
    if (!m_dirty_tracking || !IsValidPointer(m_working_ram) || host < m_working_ram ||
        host >= m_working_ram + m_working_ram_size)
        return true;

    u32 ram_page = (u32)((host - m_working_ram) >> 12);
    u32 cpu_page = (m_working_ram_base >> 12) + ram_page;
    bool alias = !m_a20_enabled && cpu_page < 0x100 && page == (cpu_page | 0x100);

    if (page != cpu_page && !alias)
        return false;

    return ram_page < m_dirty_page_count && m_dirty_pages[ram_page] != 0;
}

u8 Memory::GetBusClass(const GDOX_Debug_Memory_Region& info)
{
    if ((info.flags & GDOX_DEBUG_REGION_ISA8) != 0)
        return GDOX_BUS_ISA8;

    switch (info.id)
    {
        case GDOX_DEBUG_REGION_MAIN_RAM:
        case GDOX_DEBUG_REGION_BIOS_COPY:
        case GDOX_DEBUG_REGION_SHADOW_ROM:
        case GDOX_DEBUG_REGION_SHADOW_BIOS:
            return GDOX_BUS_DRAM;
        case GDOX_DEBUG_REGION_VRAM:
        case GDOX_DEBUG_REGION_VIDEO_APERTURE:
        case GDOX_DEBUG_REGION_LINEAR_VRAM:
            return GDOX_BUS_VRAM;
        case GDOX_DEBUG_REGION_SYSTEM_ROM:
            return GDOX_BUS_ROM;
        case GDOX_DEBUG_REGION_CMOS:
            return GDOX_BUS_CMOS;
        default:
            return (info.flags & GDOX_DEBUG_REGION_MMIO) != 0 ? (u8)GDOX_BUS_MMIO : (u8)GDOX_BUS_NONE;
    }
}

// Up to date host pages are patched in place, the new generation makes the CPU drop its cached pointers
void Memory::RemapRange(u32 base, u32 size)
{
    bool patch = m_cpu_map_generation == m_map_generation && IsValidPointer(m_cpu_read_pages) && size != 0;

    m_map_generation++;
    m_debug_snapshot_id++;

    if (!patch)
        return;

    u32 last = (u32)(((u64)base + size - 1) >> 12);
    const DebugRegion* region = ((base | size) & 0xFFF) == 0 ? FindMappedSpan(base, size) : NULL;

    for (u32 page = base >> 12; page <= last; page++)
    {
        if (!IsCPUPage(page))
            continue;

        if (IsValidPointer(region))
            UpdateCPUPage(page, region);
        else
            UpdateCPUPage(page);

        // The high memory area mirrors what changed below 64 KB
        if (!m_a20_enabled && page < 0x100)
            UpdateCPUPage(page | 0x100);
    }

    m_cpu_map_generation = m_map_generation;
}

void Memory::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer, GDOX_SAVESTATE_VERSION);
}

void Memory::LoadState(std::istream& stream, int version)
{
    StateSerializer serializer(stream);
    Serialize(serializer, version);
    SanitizeState();
}

void Memory::Serialize(StateSerializer& serializer, int version)
{
    UNUSED(version);
    G_SERIALIZE_ARRAY(serializer, m_state.main_ram, m_main_ram_size);
}

void Memory::SanitizeState()
{
    m_debug_snapshot_id++;
    m_dirty_valid = false;
}
