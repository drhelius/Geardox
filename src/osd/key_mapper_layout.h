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

#ifndef KEY_MAPPER_LAYOUT_H
#define KEY_MAPPER_LAYOUT_H

#include "../common/common.h"

struct GDOX_Key_Mapper_Key
{
    u8 x;
    u8 y;
    u8 width;
    u8 height;
    GDOX_Keys key;
    const char* label;
};

static const int k_key_mapper_width = 640;
static const int k_key_mapper_height = 400;
static const int k_key_mapper_display_width = 640;
static const int k_key_mapper_display_height = 480;
static const int k_key_mapper_grid = 4;
static const int k_key_mapper_unit_x = 26;
static const int k_key_mapper_unit_y = 22;
static const int k_key_mapper_origin_x = 27;
static const int k_key_mapper_origin_y = 30;

static const GDOX_Key_Mapper_Key k_key_mapper_layout[] =
{
    { 0, 0, 4, 4, GDOX_KEY_ESCAPE, "Esc" },
    { 8, 0, 4, 4, GDOX_KEY_F1, "F1" },
    { 12, 0, 4, 4, GDOX_KEY_F2, "F2" },
    { 16, 0, 4, 4, GDOX_KEY_F3, "F3" },
    { 20, 0, 4, 4, GDOX_KEY_F4, "F4" },
    { 26, 0, 4, 4, GDOX_KEY_F5, "F5" },
    { 30, 0, 4, 4, GDOX_KEY_F6, "F6" },
    { 34, 0, 4, 4, GDOX_KEY_F7, "F7" },
    { 38, 0, 4, 4, GDOX_KEY_F8, "F8" },
    { 44, 0, 4, 4, GDOX_KEY_F9, "F9" },
    { 48, 0, 4, 4, GDOX_KEY_F10, "F10" },
    { 52, 0, 4, 4, GDOX_KEY_F11, "F11" },
    { 56, 0, 4, 4, GDOX_KEY_F12, "F12" },
    { 61, 0, 4, 4, GDOX_KEY_PRINT_SCREEN, "Prt\nScr" },
    { 65, 0, 4, 4, GDOX_KEY_SCROLL_LOCK, "Scr\nLck" },
    { 69, 0, 4, 4, GDOX_KEY_PAUSE, "Pau" },

    { 0, 6, 4, 4, GDOX_KEY_GRAVE, "`" },
    { 4, 6, 4, 4, GDOX_KEY_1, "1" },
    { 8, 6, 4, 4, GDOX_KEY_2, "2" },
    { 12, 6, 4, 4, GDOX_KEY_3, "3" },
    { 16, 6, 4, 4, GDOX_KEY_4, "4" },
    { 20, 6, 4, 4, GDOX_KEY_5, "5" },
    { 24, 6, 4, 4, GDOX_KEY_6, "6" },
    { 28, 6, 4, 4, GDOX_KEY_7, "7" },
    { 32, 6, 4, 4, GDOX_KEY_8, "8" },
    { 36, 6, 4, 4, GDOX_KEY_9, "9" },
    { 40, 6, 4, 4, GDOX_KEY_0, "0" },
    { 44, 6, 4, 4, GDOX_KEY_MINUS, "-" },
    { 48, 6, 4, 4, GDOX_KEY_EQUALS, "=" },
    { 52, 6, 8, 4, GDOX_KEY_BACKSPACE, "Bksp" },
    { 61, 6, 4, 4, GDOX_KEY_INSERT, "Ins" },
    { 65, 6, 4, 4, GDOX_KEY_HOME, "Hom" },
    { 69, 6, 4, 4, GDOX_KEY_PAGE_UP, "Pg\nUp" },
    { 74, 6, 4, 4, GDOX_KEY_NUM_LOCK, "Num\nLck" },
    { 78, 6, 4, 4, GDOX_KEY_KP_DIVIDE, "/" },
    { 82, 6, 4, 4, GDOX_KEY_KP_MULTIPLY, "*" },
    { 86, 6, 4, 4, GDOX_KEY_KP_MINUS, "-" },

    { 0, 10, 6, 4, GDOX_KEY_TAB, "Tab" },
    { 6, 10, 4, 4, GDOX_KEY_Q, "Q" },
    { 10, 10, 4, 4, GDOX_KEY_W, "W" },
    { 14, 10, 4, 4, GDOX_KEY_E, "E" },
    { 18, 10, 4, 4, GDOX_KEY_R, "R" },
    { 22, 10, 4, 4, GDOX_KEY_T, "T" },
    { 26, 10, 4, 4, GDOX_KEY_Y, "Y" },
    { 30, 10, 4, 4, GDOX_KEY_U, "U" },
    { 34, 10, 4, 4, GDOX_KEY_I, "I" },
    { 38, 10, 4, 4, GDOX_KEY_O, "O" },
    { 42, 10, 4, 4, GDOX_KEY_P, "P" },
    { 46, 10, 4, 4, GDOX_KEY_LEFT_BRACKET, "[" },
    { 50, 10, 4, 4, GDOX_KEY_RIGHT_BRACKET, "]" },
    { 54, 10, 6, 4, GDOX_KEY_BACKSLASH, "\\" },
    { 61, 10, 4, 4, GDOX_KEY_DELETE, "Del" },
    { 65, 10, 4, 4, GDOX_KEY_END, "End" },
    { 69, 10, 4, 4, GDOX_KEY_PAGE_DOWN, "Pg\nDn" },
    { 74, 10, 4, 4, GDOX_KEY_KP_7, "7" },
    { 78, 10, 4, 4, GDOX_KEY_KP_8, "8" },
    { 82, 10, 4, 4, GDOX_KEY_KP_9, "9" },
    { 86, 10, 4, 8, GDOX_KEY_KP_PLUS, "+" },

    { 0, 14, 7, 4, GDOX_KEY_CAPS_LOCK, "Caps" },
    { 7, 14, 4, 4, GDOX_KEY_A, "A" },
    { 11, 14, 4, 4, GDOX_KEY_S, "S" },
    { 15, 14, 4, 4, GDOX_KEY_D, "D" },
    { 19, 14, 4, 4, GDOX_KEY_F, "F" },
    { 23, 14, 4, 4, GDOX_KEY_G, "G" },
    { 27, 14, 4, 4, GDOX_KEY_H, "H" },
    { 31, 14, 4, 4, GDOX_KEY_J, "J" },
    { 35, 14, 4, 4, GDOX_KEY_K, "K" },
    { 39, 14, 4, 4, GDOX_KEY_L, "L" },
    { 43, 14, 4, 4, GDOX_KEY_SEMICOLON, ";" },
    { 47, 14, 4, 4, GDOX_KEY_APOSTROPHE, "'" },
    { 51, 14, 9, 4, GDOX_KEY_RETURN, "Enter" },
    { 74, 14, 4, 4, GDOX_KEY_KP_4, "4" },
    { 78, 14, 4, 4, GDOX_KEY_KP_5, "5" },
    { 82, 14, 4, 4, GDOX_KEY_KP_6, "6" },

    { 0, 18, 5, 4, GDOX_KEY_LEFT_SHIFT, "Shf" },
    { 5, 18, 4, 4, GDOX_KEY_NON_US_BACKSLASH, "<>" },
    { 9, 18, 4, 4, GDOX_KEY_Z, "Z" },
    { 13, 18, 4, 4, GDOX_KEY_X, "X" },
    { 17, 18, 4, 4, GDOX_KEY_C, "C" },
    { 21, 18, 4, 4, GDOX_KEY_V, "V" },
    { 25, 18, 4, 4, GDOX_KEY_B, "B" },
    { 29, 18, 4, 4, GDOX_KEY_N, "N" },
    { 33, 18, 4, 4, GDOX_KEY_M, "M" },
    { 37, 18, 4, 4, GDOX_KEY_COMMA, "," },
    { 41, 18, 4, 4, GDOX_KEY_PERIOD, "." },
    { 45, 18, 4, 4, GDOX_KEY_SLASH, "/" },
    { 49, 18, 11, 4, GDOX_KEY_RIGHT_SHIFT, "Shift" },
    { 65, 18, 4, 4, GDOX_KEY_UP, "\x18" },
    { 74, 18, 4, 4, GDOX_KEY_KP_1, "1" },
    { 78, 18, 4, 4, GDOX_KEY_KP_2, "2" },
    { 82, 18, 4, 4, GDOX_KEY_KP_3, "3" },
    { 86, 18, 4, 8, GDOX_KEY_KP_ENTER, "Ent" },

    { 0, 22, 5, 4, GDOX_KEY_LEFT_CTRL, "Ctl" },
    { 5, 22, 5, 4, GDOX_KEY_LEFT_GUI, "Win" },
    { 10, 22, 5, 4, GDOX_KEY_LEFT_ALT, "Alt" },
    { 15, 22, 25, 4, GDOX_KEY_SPACE, "Space" },
    { 40, 22, 5, 4, GDOX_KEY_RIGHT_ALT, "Alt" },
    { 45, 22, 5, 4, GDOX_KEY_RIGHT_GUI, "Win" },
    { 50, 22, 5, 4, GDOX_KEY_MENU, "Mnu" },
    { 55, 22, 5, 4, GDOX_KEY_RIGHT_CTRL, "Ctl" },
    { 61, 22, 4, 4, GDOX_KEY_LEFT, "\x1B" },
    { 65, 22, 4, 4, GDOX_KEY_DOWN, "\x19" },
    { 69, 22, 4, 4, GDOX_KEY_RIGHT, "\x1A" },
    { 74, 22, 8, 4, GDOX_KEY_KP_0, "0" },
    { 82, 22, 4, 4, GDOX_KEY_KP_PERIOD, "." }
};

static const int k_key_mapper_key_count = (int)(sizeof(k_key_mapper_layout) / sizeof(k_key_mapper_layout[0]));

#endif /* KEY_MAPPER_LAYOUT_H */
