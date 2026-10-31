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

#ifndef GUI_DEBUG_MEMORY_PROVIDER_H
#define GUI_DEBUG_MEMORY_PROVIDER_H

#include <vector>
#include "common/debug_memory.h"

class DebugMemoryProvider
{
public:
    DebugMemoryProvider();
    ~DebugMemoryProvider();
    void Init();
    void Reset();
    void Update();

    int GetRegionCount() const;
    bool GetRegion(int index, GDOX_Debug_Memory_Region& region) const;
    bool GetRegionById(int id, GDOX_Debug_Memory_Region& region) const;
    u32 GetAddressLimit(const GDOX_Debug_Memory_Address& address) const;
    void ReadBlock(const GDOX_Debug_Memory_Address& address, u8* data, GDOX_Debug_Memory_Status* status, u32 size,
        GDOX_Debug_Memory_Block_Info* info) const;
    bool Translate(const GDOX_Debug_Memory_Address& address, GDOX_Debug_Memory_Translation& translation) const;

    bool QueueWrite(const GDOX_Debug_Memory_Address& address, const u8* data, u32 size, bool undo = true);
    bool WriteNow(const GDOX_Debug_Memory_Address& address, const u8* data, u32 size);
    void RequestUndo();
    void RequestRedo();
    bool CanUndo() const;
    bool CanRedo() const;
    bool ConsumeChanged();
    const char* GetLastMessage() const;

    bool GetRegisterValue(const char* name, u32& value) const;
    u64 GetViewKey(const GDOX_Debug_Memory_Address& source) const;
    static const char* GetSpaceName(GDOX_Debug_Memory_Space space);
    static const char* GetStatusName(GDOX_Debug_Memory_Status status);

private:
    struct WriteTransaction
    {
        GDOX_Debug_Memory_Address address;
        std::vector<u8> before;
        std::vector<u8> after;
        u32 map_generation;
        bool undo;
    };

    bool GetExternalRegion(int index, GDOX_Debug_Memory_Region& region) const;
    bool HasMediaImageRegion() const;
    bool ReadExternalRegion(int id, u32 offset, u8* data, GDOX_Debug_Memory_Status* status, u32 size) const;
    bool WriteBlock(const GDOX_Debug_Memory_Address& address, const u8* data, u32 size);
    bool ApplyTransaction(WriteTransaction& transaction, bool capture_before);
    bool ValidateWritable(const GDOX_Debug_Memory_Address& address, u32 size) const;
    u64 GetSnapshotId() const;
    u32 GetMapGeneration() const;
    void SetMessage(const char* message);

private:
    std::vector<WriteTransaction> m_pending_writes;
    std::vector<WriteTransaction> m_undo_history;
    std::vector<WriteTransaction> m_redo_history;

    char m_last_message[GDOX_DEBUG_MEMORY_REASON_SIZE];

    bool m_request_undo;
    bool m_request_redo;
    bool m_changed;
};

#endif /* GUI_DEBUG_MEMORY_PROVIDER_H */
