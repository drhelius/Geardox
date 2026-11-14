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

#ifndef KEYBOARD_KEYS_H
#define KEYBOARD_KEYS_H

#include "../common/common.h"

struct GDOX_Key_Info
{
    GDOX_Keys key;
    const char* name;
    const char* label;
};

struct GDOX_Default_Key
{
    u16 usage;
    GDOX_Keys key;
};

static const GDOX_Key_Info k_key_infos[] =
{
    { GDOX_KEY_ESCAPE, "escape", "Esc" },
    { GDOX_KEY_1, "1", "1" }, { GDOX_KEY_2, "2", "2" }, { GDOX_KEY_3, "3", "3" }, { GDOX_KEY_4, "4", "4" },
    { GDOX_KEY_5, "5", "5" }, { GDOX_KEY_6, "6", "6" }, { GDOX_KEY_7, "7", "7" }, { GDOX_KEY_8, "8", "8" },
    { GDOX_KEY_9, "9", "9" }, { GDOX_KEY_0, "0", "0" },
    { GDOX_KEY_MINUS, "minus", "-" },
    { GDOX_KEY_EQUALS, "equals", "=" },
    { GDOX_KEY_BACKSPACE, "backspace", "Backspace" },
    { GDOX_KEY_TAB, "tab", "Tab" },
    { GDOX_KEY_Q, "q", "Q" }, { GDOX_KEY_W, "w", "W" }, { GDOX_KEY_E, "e", "E" }, { GDOX_KEY_R, "r", "R" },
    { GDOX_KEY_T, "t", "T" }, { GDOX_KEY_Y, "y", "Y" }, { GDOX_KEY_U, "u", "U" }, { GDOX_KEY_I, "i", "I" },
    { GDOX_KEY_O, "o", "O" }, { GDOX_KEY_P, "p", "P" },
    { GDOX_KEY_LEFT_BRACKET, "left_bracket", "[" },
    { GDOX_KEY_RIGHT_BRACKET, "right_bracket", "]" },
    { GDOX_KEY_RETURN, "return", "Enter" },
    { GDOX_KEY_LEFT_CTRL, "left_ctrl", "Left Ctrl" },
    { GDOX_KEY_A, "a", "A" }, { GDOX_KEY_S, "s", "S" }, { GDOX_KEY_D, "d", "D" }, { GDOX_KEY_F, "f", "F" },
    { GDOX_KEY_G, "g", "G" }, { GDOX_KEY_H, "h", "H" }, { GDOX_KEY_J, "j", "J" }, { GDOX_KEY_K, "k", "K" },
    { GDOX_KEY_L, "l", "L" },
    { GDOX_KEY_SEMICOLON, "semicolon", ";" },
    { GDOX_KEY_APOSTROPHE, "apostrophe", "'" },
    { GDOX_KEY_GRAVE, "grave", "`" },
    { GDOX_KEY_LEFT_SHIFT, "left_shift", "Left Shift" },
    { GDOX_KEY_BACKSLASH, "backslash", "\\" },
    { GDOX_KEY_Z, "z", "Z" }, { GDOX_KEY_X, "x", "X" }, { GDOX_KEY_C, "c", "C" }, { GDOX_KEY_V, "v", "V" },
    { GDOX_KEY_B, "b", "B" }, { GDOX_KEY_N, "n", "N" }, { GDOX_KEY_M, "m", "M" },
    { GDOX_KEY_COMMA, "comma", "," },
    { GDOX_KEY_PERIOD, "period", "." },
    { GDOX_KEY_SLASH, "slash", "/" },
    { GDOX_KEY_RIGHT_SHIFT, "right_shift", "Right Shift" },
    { GDOX_KEY_KP_MULTIPLY, "kp_multiply", "Keypad *" },
    { GDOX_KEY_LEFT_ALT, "left_alt", "Left Alt" },
    { GDOX_KEY_SPACE, "space", "Space" },
    { GDOX_KEY_CAPS_LOCK, "caps_lock", "Caps Lock" },
    { GDOX_KEY_F1, "f1", "F1" }, { GDOX_KEY_F2, "f2", "F2" }, { GDOX_KEY_F3, "f3", "F3" },
    { GDOX_KEY_F4, "f4", "F4" }, { GDOX_KEY_F5, "f5", "F5" }, { GDOX_KEY_F6, "f6", "F6" },
    { GDOX_KEY_F7, "f7", "F7" }, { GDOX_KEY_F8, "f8", "F8" }, { GDOX_KEY_F9, "f9", "F9" },
    { GDOX_KEY_F10, "f10", "F10" }, { GDOX_KEY_F11, "f11", "F11" }, { GDOX_KEY_F12, "f12", "F12" },
    { GDOX_KEY_NUM_LOCK, "num_lock", "Num Lock" },
    { GDOX_KEY_SCROLL_LOCK, "scroll_lock", "Scroll Lock" },
    { GDOX_KEY_KP_7, "kp_7", "Keypad 7" }, { GDOX_KEY_KP_8, "kp_8", "Keypad 8" },
    { GDOX_KEY_KP_9, "kp_9", "Keypad 9" }, { GDOX_KEY_KP_4, "kp_4", "Keypad 4" },
    { GDOX_KEY_KP_5, "kp_5", "Keypad 5" }, { GDOX_KEY_KP_6, "kp_6", "Keypad 6" },
    { GDOX_KEY_KP_1, "kp_1", "Keypad 1" }, { GDOX_KEY_KP_2, "kp_2", "Keypad 2" },
    { GDOX_KEY_KP_3, "kp_3", "Keypad 3" }, { GDOX_KEY_KP_0, "kp_0", "Keypad 0" },
    { GDOX_KEY_KP_MINUS, "kp_minus", "Keypad -" },
    { GDOX_KEY_KP_PLUS, "kp_plus", "Keypad +" },
    { GDOX_KEY_KP_PERIOD, "kp_period", "Keypad ." },
    { GDOX_KEY_SYSRQ, "sysrq", "SysRq" },
    { GDOX_KEY_NON_US_BACKSLASH, "non_us_backslash", "\\ (ISO)" },
    { GDOX_KEY_KP_ENTER, "kp_enter", "Keypad Enter" },
    { GDOX_KEY_RIGHT_CTRL, "right_ctrl", "Right Ctrl" },
    { GDOX_KEY_KP_DIVIDE, "kp_divide", "Keypad /" },
    { GDOX_KEY_PRINT_SCREEN, "print_screen", "Print Screen" },
    { GDOX_KEY_RIGHT_ALT, "right_alt", "Right Alt" },
    { GDOX_KEY_PAUSE, "pause", "Pause" },
    { GDOX_KEY_HOME, "home", "Home" },
    { GDOX_KEY_UP, "up", "Up" },
    { GDOX_KEY_PAGE_UP, "page_up", "Page Up" },
    { GDOX_KEY_LEFT, "left", "Left" },
    { GDOX_KEY_RIGHT, "right", "Right" },
    { GDOX_KEY_END, "end", "End" },
    { GDOX_KEY_DOWN, "down", "Down" },
    { GDOX_KEY_PAGE_DOWN, "page_down", "Page Down" },
    { GDOX_KEY_INSERT, "insert", "Insert" },
    { GDOX_KEY_DELETE, "delete", "Delete" },
    { GDOX_KEY_LEFT_GUI, "left_gui", "Left Win" },
    { GDOX_KEY_RIGHT_GUI, "right_gui", "Right Win" },
    { GDOX_KEY_MENU, "menu", "Menu" }
};

static const int k_key_info_count = (int)(sizeof(k_key_infos) / sizeof(k_key_infos[0]));

static const GDOX_Default_Key k_default_keys[] =
{
    { 0x04, GDOX_KEY_A }, { 0x05, GDOX_KEY_B }, { 0x06, GDOX_KEY_C }, { 0x07, GDOX_KEY_D }, { 0x08, GDOX_KEY_E },
    { 0x09, GDOX_KEY_F }, { 0x0A, GDOX_KEY_G }, { 0x0B, GDOX_KEY_H }, { 0x0C, GDOX_KEY_I }, { 0x0D, GDOX_KEY_J },
    { 0x0E, GDOX_KEY_K }, { 0x0F, GDOX_KEY_L }, { 0x10, GDOX_KEY_M }, { 0x11, GDOX_KEY_N }, { 0x12, GDOX_KEY_O },
    { 0x13, GDOX_KEY_P }, { 0x14, GDOX_KEY_Q }, { 0x15, GDOX_KEY_R }, { 0x16, GDOX_KEY_S }, { 0x17, GDOX_KEY_T },
    { 0x18, GDOX_KEY_U }, { 0x19, GDOX_KEY_V }, { 0x1A, GDOX_KEY_W }, { 0x1B, GDOX_KEY_X }, { 0x1C, GDOX_KEY_Y },
    { 0x1D, GDOX_KEY_Z },
    { 0x1E, GDOX_KEY_1 }, { 0x1F, GDOX_KEY_2 }, { 0x20, GDOX_KEY_3 }, { 0x21, GDOX_KEY_4 }, { 0x22, GDOX_KEY_5 },
    { 0x23, GDOX_KEY_6 }, { 0x24, GDOX_KEY_7 }, { 0x25, GDOX_KEY_8 }, { 0x26, GDOX_KEY_9 }, { 0x27, GDOX_KEY_0 },
    { 0x28, GDOX_KEY_RETURN },
    { 0x29, GDOX_KEY_ESCAPE },
    { 0x2A, GDOX_KEY_BACKSPACE },
    { 0x2B, GDOX_KEY_TAB },
    { 0x2C, GDOX_KEY_SPACE },
    { 0x2D, GDOX_KEY_MINUS },
    { 0x2E, GDOX_KEY_EQUALS },
    { 0x2F, GDOX_KEY_LEFT_BRACKET },
    { 0x30, GDOX_KEY_RIGHT_BRACKET },
    { 0x31, GDOX_KEY_BACKSLASH },
    { 0x32, GDOX_KEY_BACKSLASH },
    { 0x33, GDOX_KEY_SEMICOLON },
    { 0x34, GDOX_KEY_APOSTROPHE },
    { 0x35, GDOX_KEY_GRAVE },
    { 0x36, GDOX_KEY_COMMA },
    { 0x37, GDOX_KEY_PERIOD },
    { 0x38, GDOX_KEY_SLASH },
    { 0x39, GDOX_KEY_CAPS_LOCK },
    { 0x3A, GDOX_KEY_F1 }, { 0x3B, GDOX_KEY_F2 }, { 0x3C, GDOX_KEY_F3 }, { 0x3D, GDOX_KEY_F4 },
    { 0x3E, GDOX_KEY_F5 }, { 0x3F, GDOX_KEY_F6 }, { 0x40, GDOX_KEY_F7 }, { 0x41, GDOX_KEY_F8 },
    { 0x42, GDOX_KEY_F9 }, { 0x43, GDOX_KEY_F10 }, { 0x44, GDOX_KEY_F11 }, { 0x45, GDOX_KEY_F12 },
    { 0x46, GDOX_KEY_PRINT_SCREEN },
    { 0x47, GDOX_KEY_SCROLL_LOCK },
    { 0x48, GDOX_KEY_PAUSE },
    { 0x49, GDOX_KEY_INSERT },
    { 0x4A, GDOX_KEY_HOME },
    { 0x4B, GDOX_KEY_PAGE_UP },
    { 0x4C, GDOX_KEY_DELETE },
    { 0x4D, GDOX_KEY_END },
    { 0x4E, GDOX_KEY_PAGE_DOWN },
    { 0x4F, GDOX_KEY_RIGHT },
    { 0x50, GDOX_KEY_LEFT },
    { 0x51, GDOX_KEY_DOWN },
    { 0x52, GDOX_KEY_UP },
    { 0x53, GDOX_KEY_NUM_LOCK },
    { 0x54, GDOX_KEY_KP_DIVIDE },
    { 0x55, GDOX_KEY_KP_MULTIPLY },
    { 0x56, GDOX_KEY_KP_MINUS },
    { 0x57, GDOX_KEY_KP_PLUS },
    { 0x58, GDOX_KEY_KP_ENTER },
    { 0x59, GDOX_KEY_KP_1 }, { 0x5A, GDOX_KEY_KP_2 }, { 0x5B, GDOX_KEY_KP_3 }, { 0x5C, GDOX_KEY_KP_4 },
    { 0x5D, GDOX_KEY_KP_5 }, { 0x5E, GDOX_KEY_KP_6 }, { 0x5F, GDOX_KEY_KP_7 }, { 0x60, GDOX_KEY_KP_8 },
    { 0x61, GDOX_KEY_KP_9 }, { 0x62, GDOX_KEY_KP_0 },
    { 0x63, GDOX_KEY_KP_PERIOD },
    { 0x64, GDOX_KEY_NON_US_BACKSLASH },
    { 0x65, GDOX_KEY_MENU },
    { 0x9A, GDOX_KEY_SYSRQ },
    { 0xE0, GDOX_KEY_LEFT_CTRL },
    { 0xE1, GDOX_KEY_LEFT_SHIFT },
    { 0xE2, GDOX_KEY_LEFT_ALT },
    { 0xE3, GDOX_KEY_LEFT_GUI },
    { 0xE4, GDOX_KEY_RIGHT_CTRL },
    { 0xE5, GDOX_KEY_RIGHT_SHIFT },
    { 0xE6, GDOX_KEY_RIGHT_ALT },
    { 0xE7, GDOX_KEY_RIGHT_GUI }
};

static const int k_default_key_count = (int)(sizeof(k_default_keys) / sizeof(k_default_keys[0]));

static const GDOX_Keys k_modifier_keys[] =
{
    GDOX_KEY_LEFT_CTRL, GDOX_KEY_RIGHT_CTRL, GDOX_KEY_LEFT_SHIFT, GDOX_KEY_RIGHT_SHIFT
};

static const int k_modifier_key_count = (int)(sizeof(k_modifier_keys) / sizeof(k_modifier_keys[0]));

#endif /* KEYBOARD_KEYS_H */
