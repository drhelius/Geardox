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

#ifndef GUI_DEBUG_CONSTANTS_H
#define GUI_DEBUG_CONSTANTS_H

#include "geardox.h"

struct stDebugPortLabel
{
    u16 port;
    const char* label;
    const char* description;
};

static const int k_debug_port_label_count = 0;
static const stDebugPortLabel k_debug_port_labels[k_debug_port_label_count + 1] = { { 0, NULL, NULL } };

struct stDebugInterruptName
{
    u8 vector;
    const char* label;
    const char* description;
};

// Software interrupts of DOS
static const stDebugInterruptName k_debug_interrupt_names[] =
{
    { 0x20, "DOS_EXIT", "DOS terminate program" },
    { 0x21, "DOS", "DOS function call" },
    { 0x22, "DOS_EXIT_ADDR", "DOS terminate address" },
    { 0x23, "DOS_CTRL_C", "DOS Ctrl-C handler" },
    { 0x24, "DOS_CRIT_ERROR", "DOS critical error handler" },
    { 0x25, "DOS_DISK_READ", "DOS absolute disk read" },
    { 0x26, "DOS_DISK_WRITE", "DOS absolute disk write" },
    { 0x27, "DOS_TSR", "DOS terminate and stay resident" },
    { 0x28, "DOS_IDLE", "DOS idle" },
    { 0x29, "DOS_FAST_OUT", "DOS fast console output" },
    { 0x2F, "MULTIPLEX", "DOS multiplex, MSCDEX" }
};

static const int k_debug_interrupt_name_count = sizeof(k_debug_interrupt_names) / sizeof(k_debug_interrupt_names[0]);

struct stDebugInterruptFunction
{
    u8 vector;
    u16 mask;
    u16 value;
    const char* name;
};

// Functions picked by AX, the first match wins
static const stDebugInterruptFunction k_debug_interrupt_functions[] =
{
    { 0x21, 0xFF00, 0x0000, "Terminate program" },
    { 0x21, 0xFF00, 0x0100, "Read character with echo" },
    { 0x21, 0xFF00, 0x0200, "Write character" },
    { 0x21, 0xFF00, 0x0300, "Read auxiliary input" },
    { 0x21, 0xFF00, 0x0400, "Write auxiliary output" },
    { 0x21, 0xFF00, 0x0500, "Write to printer" },
    { 0x21, 0xFF00, 0x0600, "Direct console input or output" },
    { 0x21, 0xFF00, 0x0700, "Read character, no echo or filter" },
    { 0x21, 0xFF00, 0x0800, "Read character without echo" },
    { 0x21, 0xFF00, 0x0900, "Write string" },
    { 0x21, 0xFF00, 0x0A00, "Buffered keyboard input" },
    { 0x21, 0xFF00, 0x0B00, "Check input status" },
    { 0x21, 0xFF00, 0x0C00, "Flush input buffer and read" },
    { 0x21, 0xFF00, 0x0D00, "Disk reset" },
    { 0x21, 0xFF00, 0x0E00, "Select default drive" },
    { 0x21, 0xFF00, 0x0F00, "Open file (FCB)" },
    { 0x21, 0xFF00, 0x1000, "Close file (FCB)" },
    { 0x21, 0xFF00, 0x1100, "Find first file (FCB)" },
    { 0x21, 0xFF00, 0x1200, "Find next file (FCB)" },
    { 0x21, 0xFF00, 0x1300, "Delete file (FCB)" },
    { 0x21, 0xFF00, 0x1400, "Sequential read (FCB)" },
    { 0x21, 0xFF00, 0x1500, "Sequential write (FCB)" },
    { 0x21, 0xFF00, 0x1600, "Create file (FCB)" },
    { 0x21, 0xFF00, 0x1700, "Rename file (FCB)" },
    { 0x21, 0xFF00, 0x1900, "Get default drive" },
    { 0x21, 0xFF00, 0x1A00, "Set disk transfer address" },
    { 0x21, 0xFF00, 0x1B00, "Get default drive information" },
    { 0x21, 0xFF00, 0x1C00, "Get drive information" },
    { 0x21, 0xFF00, 0x2100, "Random read (FCB)" },
    { 0x21, 0xFF00, 0x2200, "Random write (FCB)" },
    { 0x21, 0xFF00, 0x2300, "Get file size (FCB)" },
    { 0x21, 0xFF00, 0x2400, "Set random record (FCB)" },
    { 0x21, 0xFF00, 0x2500, "Set interrupt vector, DOS extender call" },
    { 0x21, 0xFF00, 0x2600, "Create program segment prefix" },
    { 0x21, 0xFF00, 0x2700, "Random block read (FCB)" },
    { 0x21, 0xFF00, 0x2800, "Random block write (FCB)" },
    { 0x21, 0xFF00, 0x2900, "Parse file name" },
    { 0x21, 0xFF00, 0x2A00, "Get date" },
    { 0x21, 0xFF00, 0x2B00, "Set date" },
    { 0x21, 0xFF00, 0x2C00, "Get time" },
    { 0x21, 0xFF00, 0x2D00, "Set time" },
    { 0x21, 0xFF00, 0x2E00, "Set verify flag" },
    { 0x21, 0xFF00, 0x2F00, "Get disk transfer address" },
    { 0x21, 0xFF00, 0x3000, "Get DOS version" },
    { 0x21, 0xFF00, 0x3100, "Terminate and stay resident" },
    { 0x21, 0xFF00, 0x3300, "Get or set Ctrl-Break checking" },
    { 0x21, 0xFF00, 0x3400, "Get InDOS flag address" },
    { 0x21, 0xFF00, 0x3500, "Get interrupt vector" },
    { 0x21, 0xFF00, 0x3600, "Get free disk space" },
    { 0x21, 0xFF00, 0x3800, "Get or set country information" },
    { 0x21, 0xFF00, 0x3900, "Create directory" },
    { 0x21, 0xFF00, 0x3A00, "Remove directory" },
    { 0x21, 0xFF00, 0x3B00, "Change directory" },
    { 0x21, 0xFF00, 0x3C00, "Create file" },
    { 0x21, 0xFF00, 0x3D00, "Open file" },
    { 0x21, 0xFF00, 0x3E00, "Close file" },
    { 0x21, 0xFF00, 0x3F00, "Read from file or device" },
    { 0x21, 0xFF00, 0x4000, "Write to file or device" },
    { 0x21, 0xFF00, 0x4100, "Delete file" },
    { 0x21, 0xFF00, 0x4200, "Move file pointer" },
    { 0x21, 0xFF00, 0x4300, "Get or set file attributes" },
    { 0x21, 0xFF00, 0x4400, "Device I/O control" },
    { 0x21, 0xFF00, 0x4500, "Duplicate file handle" },
    { 0x21, 0xFF00, 0x4600, "Redirect file handle" },
    { 0x21, 0xFF00, 0x4700, "Get current directory" },
    { 0x21, 0xFF00, 0x4800, "Allocate memory" },
    { 0x21, 0xFF00, 0x4900, "Free memory" },
    { 0x21, 0xFF00, 0x4A00, "Resize memory block" },
    { 0x21, 0xFF00, 0x4B00, "Load or execute program" },
    { 0x21, 0xFF00, 0x4C00, "Terminate with return code" },
    { 0x21, 0xFF00, 0x4D00, "Get return code" },
    { 0x21, 0xFF00, 0x4E00, "Find first file" },
    { 0x21, 0xFF00, 0x4F00, "Find next file" },
    { 0x21, 0xFF00, 0x5000, "Set current process" },
    { 0x21, 0xFF00, 0x5100, "Get current process" },
    { 0x21, 0xFF00, 0x5200, "Get DOS internal tables" },
    { 0x21, 0xFF00, 0x5400, "Get verify flag" },
    { 0x21, 0xFF00, 0x5600, "Rename file" },
    { 0x21, 0xFF00, 0x5700, "Get or set file date and time" },
    { 0x21, 0xFF00, 0x5800, "Get or set memory strategy" },
    { 0x21, 0xFF00, 0x5900, "Get extended error" },
    { 0x21, 0xFF00, 0x5A00, "Create temporary file" },
    { 0x21, 0xFF00, 0x5B00, "Create new file" },
    { 0x21, 0xFF00, 0x5C00, "Lock or unlock file region" },
    { 0x21, 0xFF00, 0x5E00, "Network machine and printer" },
    { 0x21, 0xFF00, 0x5F00, "Network redirection" },
    { 0x21, 0xFF00, 0x6200, "Get current process" },
    { 0x21, 0xFF00, 0x6500, "Get extended country information" },
    { 0x21, 0xFF00, 0x6600, "Get or set code page" },
    { 0x21, 0xFF00, 0x6700, "Set file handle count" },
    { 0x21, 0xFF00, 0x6800, "Commit file" },
    { 0x21, 0xFF00, 0x6C00, "Extended open or create" },
    { 0x2F, 0xFFFF, 0x1500, "MSCDEX: get CD-ROM drive count" },
    { 0x2F, 0xFFFF, 0x1501, "MSCDEX: get drive device list" },
    { 0x2F, 0xFFFF, 0x1502, "MSCDEX: get copyright file name" },
    { 0x2F, 0xFFFF, 0x1503, "MSCDEX: get abstract file name" },
    { 0x2F, 0xFFFF, 0x1504, "MSCDEX: get bibliographic file name" },
    { 0x2F, 0xFFFF, 0x1505, "MSCDEX: read volume descriptor" },
    { 0x2F, 0xFFFF, 0x1506, "MSCDEX: debugging on" },
    { 0x2F, 0xFFFF, 0x1507, "MSCDEX: debugging off" },
    { 0x2F, 0xFFFF, 0x1508, "MSCDEX: absolute disk read" },
    { 0x2F, 0xFFFF, 0x1509, "MSCDEX: absolute disk write" },
    { 0x2F, 0xFFFF, 0x150B, "MSCDEX: check CD-ROM drive" },
    { 0x2F, 0xFFFF, 0x150C, "MSCDEX: get version" },
    { 0x2F, 0xFFFF, 0x150D, "MSCDEX: get drive letters" },
    { 0x2F, 0xFFFF, 0x150E, "MSCDEX: volume descriptor preference" },
    { 0x2F, 0xFFFF, 0x150F, "MSCDEX: get directory entry" },
    { 0x2F, 0xFFFF, 0x1510, "MSCDEX: send device request" }
};

static const int k_debug_interrupt_function_count =
    sizeof(k_debug_interrupt_functions) / sizeof(k_debug_interrupt_functions[0]);

static const char* const k_debug_irq_sources[16] =
{
    "TIMER", "KEYBOARD", "RS-232C", "EXT RS-232C", "I/O EXPANSION", "I/O EXPANSION", "FDC", "SLAVE PIC",
    "SCSI", "CD-ROM", "EXPANSION", "VSYNC", "PRINTER", "SOUND", "EXPANSION", "RESERVED"
};

static const char* const k_debug_dma_devices[4] =
{
    "FDC", "SCSI", "PRINTER", "CD-ROM"
};

static const char* const k_debug_pit_uses[6] =
{
    "INTERVAL TIMER", "I/O TIMEOUT", "BUZZER", "RESERVED", "RS-232C BAUD", "RESERVED"
};

static const char* const k_debug_exception_names[32] =
{
    "#DE", "#DB", "NMI", "#BP", "#OF", "#BR", "#UD", "#NM",
    "#DF", "CSO", "#TS", "#NP", "#SS", "#GP", "#PF", "--",
    "#MF", "--", "--", "--", "--", "--", "--", "--",
    "--", "--", "--", "--", "--", "--", "--", "--"
};

static const char* const k_debug_exception_descriptions[32] =
{
    "Divide error", "Debug", "Non-maskable interrupt", "Breakpoint", "Overflow", "Bound range exceeded",
    "Invalid opcode", "Coprocessor not available", "Double fault", "Coprocessor segment overrun", "Invalid TSS",
    "Segment not present", "Stack fault", "General protection", "Page fault", "Reserved", "Coprocessor error",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved"
};

static const char* const k_debug_pit_mode_names[6] =
{
    "INTERRUPT ON TC", "ONE-SHOT", "RATE GENERATOR", "SQUARE WAVE", "SOFTWARE STROBE", "HARDWARE STROBE"
};

static const char* const k_debug_pit_access_names[4] =
{
    "LATCH", "LSB", "MSB", "LSB+MSB"
};

static const char* const k_debug_pic_init_names[4] =
{
    "READY", "ICW2", "ICW3", "ICW4"
};

static const char* const k_debug_dma_direction_names[4] =
{
    "VERIFY", "I/O>MEM", "MEM>I/O", "INVALID"
};

static const char* const k_debug_dma_service_names[4] =
{
    "DEMAND", "SINGLE", "BLOCK", "CASCADE"
};

static const char* const k_debug_rtc_register_names[16] =
{
    "S1", "S10", "MI1", "MI10", "H1", "H10", "W", "D1", "D10", "MO1", "MO10", "Y1", "Y10", "RESET", "REF", "REF"
};

static const char* const k_debug_crtc_register_names[32] =
{
    "HSW1", "HSW2", "--", "--", "HST", "VST1", "VST2", "EET", "VST", "HDS0", "HDE0", "HDS1", "HDE1", "VDS0", "VDE0", "VDS1",
    "VDE1", "FA0", "HAJ0", "FO0", "LO0", "FA1", "HAJ1", "FO1", "LO1", "EHAJ", "EVAJ", "ZOOM", "CR0", "CR1", "FR", "CR2"
};

struct stDebugKeyName
{
    GDOX_Keys key;
    const char* name;
};

static const stDebugKeyName k_debug_key_names[] =
{
    { GDOX_KEY_ESCAPE, "ESCAPE" },
    { GDOX_KEY_1, "1" },
    { GDOX_KEY_2, "2" },
    { GDOX_KEY_3, "3" },
    { GDOX_KEY_4, "4" },
    { GDOX_KEY_5, "5" },
    { GDOX_KEY_6, "6" },
    { GDOX_KEY_7, "7" },
    { GDOX_KEY_8, "8" },
    { GDOX_KEY_9, "9" },
    { GDOX_KEY_0, "0" },
    { GDOX_KEY_MINUS, "MINUS" },
    { GDOX_KEY_EQUALS, "EQUALS" },
    { GDOX_KEY_BACKSPACE, "BACKSPACE" },
    { GDOX_KEY_TAB, "TAB" },
    { GDOX_KEY_Q, "Q" },
    { GDOX_KEY_W, "W" },
    { GDOX_KEY_E, "E" },
    { GDOX_KEY_R, "R" },
    { GDOX_KEY_T, "T" },
    { GDOX_KEY_Y, "Y" },
    { GDOX_KEY_U, "U" },
    { GDOX_KEY_I, "I" },
    { GDOX_KEY_O, "O" },
    { GDOX_KEY_P, "P" },
    { GDOX_KEY_LEFT_BRACKET, "LEFT BRACKET" },
    { GDOX_KEY_RIGHT_BRACKET, "RIGHT BRACKET" },
    { GDOX_KEY_RETURN, "RETURN" },
    { GDOX_KEY_LEFT_CTRL, "LEFT CTRL" },
    { GDOX_KEY_A, "A" },
    { GDOX_KEY_S, "S" },
    { GDOX_KEY_D, "D" },
    { GDOX_KEY_F, "F" },
    { GDOX_KEY_G, "G" },
    { GDOX_KEY_H, "H" },
    { GDOX_KEY_J, "J" },
    { GDOX_KEY_K, "K" },
    { GDOX_KEY_L, "L" },
    { GDOX_KEY_SEMICOLON, "SEMICOLON" },
    { GDOX_KEY_APOSTROPHE, "APOSTROPHE" },
    { GDOX_KEY_GRAVE, "GRAVE" },
    { GDOX_KEY_LEFT_SHIFT, "LEFT SHIFT" },
    { GDOX_KEY_BACKSLASH, "BACKSLASH" },
    { GDOX_KEY_Z, "Z" },
    { GDOX_KEY_X, "X" },
    { GDOX_KEY_C, "C" },
    { GDOX_KEY_V, "V" },
    { GDOX_KEY_B, "B" },
    { GDOX_KEY_N, "N" },
    { GDOX_KEY_M, "M" },
    { GDOX_KEY_COMMA, "COMMA" },
    { GDOX_KEY_PERIOD, "PERIOD" },
    { GDOX_KEY_SLASH, "SLASH" },
    { GDOX_KEY_RIGHT_SHIFT, "RIGHT SHIFT" },
    { GDOX_KEY_KP_MULTIPLY, "KP MULTIPLY" },
    { GDOX_KEY_LEFT_ALT, "LEFT ALT" },
    { GDOX_KEY_SPACE, "SPACE" },
    { GDOX_KEY_CAPS_LOCK, "CAPS LOCK" },
    { GDOX_KEY_F1, "F1" },
    { GDOX_KEY_F2, "F2" },
    { GDOX_KEY_F3, "F3" },
    { GDOX_KEY_F4, "F4" },
    { GDOX_KEY_F5, "F5" },
    { GDOX_KEY_F6, "F6" },
    { GDOX_KEY_F7, "F7" },
    { GDOX_KEY_F8, "F8" },
    { GDOX_KEY_F9, "F9" },
    { GDOX_KEY_F10, "F10" },
    { GDOX_KEY_NUM_LOCK, "NUM LOCK" },
    { GDOX_KEY_SCROLL_LOCK, "SCROLL LOCK" },
    { GDOX_KEY_KP_7, "KP 7" },
    { GDOX_KEY_KP_8, "KP 8" },
    { GDOX_KEY_KP_9, "KP 9" },
    { GDOX_KEY_KP_MINUS, "KP MINUS" },
    { GDOX_KEY_KP_4, "KP 4" },
    { GDOX_KEY_KP_5, "KP 5" },
    { GDOX_KEY_KP_6, "KP 6" },
    { GDOX_KEY_KP_PLUS, "KP PLUS" },
    { GDOX_KEY_KP_1, "KP 1" },
    { GDOX_KEY_KP_2, "KP 2" },
    { GDOX_KEY_KP_3, "KP 3" },
    { GDOX_KEY_KP_0, "KP 0" },
    { GDOX_KEY_KP_PERIOD, "KP PERIOD" },
    { GDOX_KEY_SYSRQ, "SYSRQ" },
    { GDOX_KEY_NON_US_BACKSLASH, "NON US BACKSLASH" },
    { GDOX_KEY_F11, "F11" },
    { GDOX_KEY_F12, "F12" },
    { GDOX_KEY_KP_ENTER, "KP ENTER" },
    { GDOX_KEY_RIGHT_CTRL, "RIGHT CTRL" },
    { GDOX_KEY_KP_DIVIDE, "KP DIVIDE" },
    { GDOX_KEY_PRINT_SCREEN, "PRINT SCREEN" },
    { GDOX_KEY_RIGHT_ALT, "RIGHT ALT" },
    { GDOX_KEY_PAUSE, "PAUSE" },
    { GDOX_KEY_HOME, "HOME" },
    { GDOX_KEY_UP, "UP" },
    { GDOX_KEY_PAGE_UP, "PAGE UP" },
    { GDOX_KEY_LEFT, "LEFT" },
    { GDOX_KEY_RIGHT, "RIGHT" },
    { GDOX_KEY_END, "END" },
    { GDOX_KEY_DOWN, "DOWN" },
    { GDOX_KEY_PAGE_DOWN, "PAGE DOWN" },
    { GDOX_KEY_INSERT, "INSERT" },
    { GDOX_KEY_DELETE, "DELETE" },
    { GDOX_KEY_LEFT_GUI, "LEFT GUI" },
    { GDOX_KEY_RIGHT_GUI, "RIGHT GUI" },
    { GDOX_KEY_MENU, "MENU" }
};

static const int k_debug_key_name_count = sizeof(k_debug_key_names) / sizeof(k_debug_key_names[0]);

static inline const stDebugPortLabel* gui_debug_find_port_label(u16 port)
{
    for (int i = 0; i < k_debug_port_label_count; i++)
    {
        if (k_debug_port_labels[i].port == port)
            return &k_debug_port_labels[i];
    }

    return NULL;
}

static inline const char* gui_debug_port_label(u16 port)
{
    const stDebugPortLabel* label = gui_debug_find_port_label(port);

    if (IsValidPointer(label))
        return label->label;

    return NULL;
}

static inline const char* gui_debug_key_name(int key)
{
    for (int i = 0; i < k_debug_key_name_count; i++)
    {
        if (k_debug_key_names[i].key == key)
            return k_debug_key_names[i].name;
    }

    return NULL;
}

static const char* const k_debug_mb8877_type1_status[8] =
{
    "BUSY", "INDEX", "TRACK 00", "CRC ERROR", "SEEK ERROR", "HEAD LOADED", "WRITE PROTECT", "NOT READY"
};

static const char* const k_debug_mb8877_type2_status[8] =
{
    "BUSY", "DRQ", "LOST DATA", "CRC ERROR", "RECORD NOT FOUND", "RECORD TYPE", "WRITE PROTECT", "NOT READY"
};

static const char* const k_debug_floppy_media_names[3] = { "2D", "2DD", "2HD" };

static inline void gui_debug_mb8877_command(u8 command, char* text, size_t size)
{
    static const char* const k_rates[4] = { "6MS", "12MS", "20MS", "30MS" };

    switch (command >> 4)
    {
        case 0x0:
        case 0x1:
            snprintf(text, size, "%s h=%d V=%d r=%s", (command >> 4) == 0 ? "RESTORE" : "SEEK", (command >> 3) & 1,
                (command >> 2) & 1, k_rates[command & 3]);
            break;
        case 0x2:
        case 0x3:
        case 0x4:
        case 0x5:
        case 0x6:
        case 0x7:
        {
            const char* name = (command >> 5) == 1 ? "STEP" : (command >> 5) == 2 ? "STEP IN" : "STEP OUT";
            snprintf(text, size, "%s u=%d h=%d V=%d r=%s", name, (command >> 4) & 1, (command >> 3) & 1,
                (command >> 2) & 1, k_rates[command & 3]);
            break;
        }
        case 0x8:
        case 0x9:
            snprintf(text, size, "READ SECTOR m=%d S=%d E=%d C=%d", (command >> 4) & 1, (command >> 3) & 1,
                (command >> 2) & 1, (command >> 1) & 1);
            break;
        case 0xA:
        case 0xB:
            snprintf(text, size, "WRITE SECTOR m=%d S=%d E=%d C=%d a0=%d", (command >> 4) & 1, (command >> 3) & 1,
                (command >> 2) & 1, (command >> 1) & 1, command & 1);
            break;
        case 0xC:
            snprintf(text, size, "READ ADDRESS E=%d", (command >> 2) & 1);
            break;
        case 0xD:
            snprintf(text, size, "FORCE INTERRUPT I=%X", command & 0x0F);
            break;
        case 0xE:
            snprintf(text, size, "READ TRACK E=%d", (command >> 2) & 1);
            break;
        default:
            snprintf(text, size, "WRITE TRACK E=%d", (command >> 2) & 1);
            break;
    }
}

static inline const char* gui_debug_floppy_status_name(u8 status)
{
    switch (status)
    {
        case 0x00: return "OK";
        case 0x10: return "OK";
        case 0xA0: return "ID CRC";
        case 0xB0: return "DATA CRC";
        case 0xE0: return "NO ID";
        case 0xF0: return "NO DATA";
        default: return "ERROR";
    }
}

static const char* const k_debug_ym3438_ch3_mode_names[4] = { "NORMAL ", "SPECIAL", "CSM    ", "SPECIAL" };

// Register order within a channel block is OP1, OP3, OP2, OP4
static const int k_debug_ym3438_register_slot[4] = { 1, 3, 2, 4 };

static inline bool gui_debug_ym3438_register_name(int part, u8 address, char* name, size_t size)
{
    static const char* const k_mode_names[16] =
    {
        NULL, "LSI TEST", "LFO", NULL, "TIMER A MSB", "TIMER A LSB", "TIMER B", "TIMER CONTROL / CH3 MODE",
        "KEY ON/OFF", NULL, "DAC DATA", "DAC ENABLE", "LSI TEST 2", NULL, NULL, NULL
    };
    static const char* const k_operator_names[7] = { "DT / MUL", "TL", "KS / AR", "AM / D1R", "D2R", "D1L / RR", "SSG-EG" };
    static const char* const k_channel_names[6] =
    {
        "F-NUMBER LSB", "BLOCK / F-NUMBER MSB", "CH3 SPECIAL F-NUMBER LSB", "CH3 SPECIAL BLOCK / F-NUMBER MSB",
        "FEEDBACK / ALGORITHM", "PAN / AMS / PMS"
    };

    if (address >= 0x20 && address < 0x30)
    {
        if (part != 0 || !IsValidPointer(k_mode_names[address & 0x0F]))
            return false;

        snprintf(name, size, "%s", k_mode_names[address & 0x0F]);
        return true;
    }

    int channel = address & 0x03;

    if (channel == 3)
        return false;

    channel += part * 3 + 1;

    if (address >= 0x30 && address < 0xA0)
    {
        snprintf(name, size, "%s  CH%d OP%d", k_operator_names[(address >> 4) - 3], channel,
            k_debug_ym3438_register_slot[(address >> 2) & 0x03]);
        return true;
    }

    if (address >= 0xA0 && address < 0xB8)
    {
        int index = (address - 0xA0) >> 2;

        if (index == 2 || index == 3)
        {
            static const int k_special_operator[3] = { 3, 1, 2 };

            if (part != 0)
                return false;

            snprintf(name, size, "%s  OP%d", k_channel_names[index], k_special_operator[address & 0x03]);
            return true;
        }

        snprintf(name, size, "%s  CH%d", k_channel_names[index], channel);
        return true;
    }

    return false;
}

#endif /* GUI_DEBUG_CONSTANTS_H */
