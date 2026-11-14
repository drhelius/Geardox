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

#ifndef MC6845_H
#define MC6845_H

#include <iostream>
#include "../common/common.h"

#define MC6845_REGISTER_COUNT 18

class StateSerializer;

class MC6845
{
public:
    struct MC6845_State
    {
        u8 registers[MC6845_REGISTER_COUNT];
        u8 index;
    };

public:
    MC6845();
    ~MC6845();
    void Init();
    void Reset();
    u8 ReadData() const;
    void WriteIndex(u8 value);
    bool IsDataChange(u8 value) const;
    void WriteData(u8 value);
    bool IsTimingRegister() const;
    u8 GetIndex() const;
    u8 GetRegister(int index) const;
    u32 GetHorizontalTotal() const;
    u32 GetHorizontalDisplayed() const;
    u32 GetHorizontalSyncPosition() const;
    u32 GetRowLines() const;
    u32 GetTotalLines() const;
    u32 GetDisplayedLines() const;
    u32 GetVerticalSyncLine() const;
    u32 GetStartAddress() const;
    u32 GetCursorAddress() const;
    u32 GetCursorStart() const;
    u32 GetCursorEnd() const;
    bool IsCursorShown() const;
    MC6845_State* GetState();
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream, int version = GDOX_SAVESTATE_VERSION);

private:
    void Serialize(StateSerializer& serializer, int version);
    void SanitizeState();

private:
    MC6845_State m_state;
};

static const int k_mc6845_horizontal_total = 0;
static const int k_mc6845_horizontal_displayed = 1;
static const int k_mc6845_horizontal_sync = 2;
static const int k_mc6845_sync_width = 3;
static const int k_mc6845_vertical_total = 4;
static const int k_mc6845_vertical_adjust = 5;
static const int k_mc6845_vertical_displayed = 6;
static const int k_mc6845_vertical_sync = 7;
static const int k_mc6845_interlace = 8;
static const int k_mc6845_max_scan = 9;
static const int k_mc6845_cursor_start = 10;
static const int k_mc6845_cursor_end = 11;
static const int k_mc6845_start_high = 12;
static const int k_mc6845_start_low = 13;
static const int k_mc6845_cursor_high = 14;
static const int k_mc6845_cursor_low = 15;
static const int k_mc6845_pen_high = 16;
static const int k_mc6845_pen_low = 17;

static const u8 k_mc6845_masks[MC6845_REGISTER_COUNT] =
{
    0xFF, 0xFF, 0xFF, 0x0F, 0x7F, 0x1F, 0x7F, 0x7F, 0x03, 0x1F, 0x7F, 0x1F, 0x3F, 0xFF, 0x3F, 0xFF, 0x3F, 0xFF
};

#include "mc6845_inline.h"

#endif /* MC6845_H */
