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

static const stDebugPortLabel k_debug_port_labels[] =
{
    { 0x0000, "DMA1_ADDR0", "DMA channel 0 current address" },
    { 0x0001, "DMA1_CNT0", "DMA channel 0 current count" },
    { 0x0002, "DMA1_ADDR1", "DMA channel 1 current address" },
    { 0x0003, "DMA1_CNT1", "DMA channel 1 current count" },
    { 0x0004, "DMA1_ADDR2", "DMA channel 2 current address" },
    { 0x0005, "DMA1_CNT2", "DMA channel 2 current count" },
    { 0x0006, "DMA1_ADDR3", "DMA channel 3 current address" },
    { 0x0007, "DMA1_CNT3", "DMA channel 3 current count" },
    { 0x0008, "DMA1_STATUS", "DMA chip 1 status, terminal count and requests, command on write" },
    { 0x0009, "DMA1_REQUEST", "DMA chip 1 software request" },
    { 0x000A, "DMA1_MASK", "DMA chip 1 single mask bit" },
    { 0x000B, "DMA1_MODE", "DMA chip 1 mode" },
    { 0x000C, "DMA1_FLIPFLOP", "DMA chip 1 clear byte flip-flop" },
    { 0x000D, "DMA1_CLEAR", "DMA chip 1 temporary, master clear on write" },
    { 0x000E, "DMA1_UNMASK", "DMA chip 1 clear all masks" },
    { 0x000F, "DMA1_ALLMASK", "DMA chip 1 all mask bits" },
    { 0x0020, "PIC_M_CMD", "PIC master ICW1/OCW2/OCW3, IRR/ISR read" },
    { 0x0021, "PIC_M_DATA", "PIC master ICW2-ICW4, IMR" },
    { 0x0040, "PIT_CNT0", "PIT counter 0, system timer on IRQ0" },
    { 0x0041, "PIT_CNT1", "PIT counter 1, DRAM refresh" },
    { 0x0042, "PIT_CNT2", "PIT counter 2, speaker" },
    { 0x0043, "PIT_CTRL", "PIT control word and read-back" },
    { 0x0060, "KBC_DATA", "8042 output buffer, parameters and bytes for the keyboard" },
    { 0x0061, "SYS_CTRL_B", "Speaker gate and data, refresh toggle, OUT2, NMI sources" },
    { 0x0064, "KBC_CMD", "8042 status and commands" },
    { 0x0070, "CMOS_INDEX", "CMOS index, bit 7 masks NMI" },
    { 0x0071, "CMOS_DATA", "CMOS data" },
    { 0x0080, "POST_CODE", "POST code latch" },
    { 0x0081, "DMA_PAGE2", "DMA page, channel 2" },
    { 0x0082, "DMA_PAGE3", "DMA page, channel 3" },
    { 0x0083, "DMA_PAGE1", "DMA page, channel 1" },
    { 0x0087, "DMA_PAGE0", "DMA page, channel 0" },
    { 0x0089, "DMA_PAGE6", "DMA page, channel 6" },
    { 0x008A, "DMA_PAGE7", "DMA page, channel 7" },
    { 0x008B, "DMA_PAGE5", "DMA page, channel 5" },
    { 0x008F, "DMA_REFRESH", "DMA page, refresh on the AT" },
    { 0x0092, "SYS_CTRL_A", "Fast A20 gate and fast reset" },
    { 0x00A0, "PIC_S_CMD", "PIC slave ICW1/OCW2/OCW3, IRR/ISR read" },
    { 0x00A1, "PIC_S_DATA", "PIC slave ICW2-ICW4, IMR" },
    { 0x00C0, "DMA2_ADDR4", "DMA channel 4 current address" },
    { 0x00C2, "DMA2_CNT4", "DMA channel 4 current count" },
    { 0x00C4, "DMA2_ADDR5", "DMA channel 5 current address" },
    { 0x00C6, "DMA2_CNT5", "DMA channel 5 current count" },
    { 0x00C8, "DMA2_ADDR6", "DMA channel 6 current address" },
    { 0x00CA, "DMA2_CNT6", "DMA channel 6 current count" },
    { 0x00CC, "DMA2_ADDR7", "DMA channel 7 current address" },
    { 0x00CE, "DMA2_CNT7", "DMA channel 7 current count" },
    { 0x00D0, "DMA2_STATUS", "DMA chip 2 status, terminal count and requests, command on write" },
    { 0x00D2, "DMA2_REQUEST", "DMA chip 2 software request" },
    { 0x00D4, "DMA2_MASK", "DMA chip 2 single mask bit" },
    { 0x00D6, "DMA2_MODE", "DMA chip 2 mode" },
    { 0x00D8, "DMA2_FLIPFLOP", "DMA chip 2 clear byte flip-flop" },
    { 0x00DA, "DMA2_CLEAR", "DMA chip 2 temporary, master clear on write" },
    { 0x00DC, "DMA2_UNMASK", "DMA chip 2 clear all masks" },
    { 0x00DE, "DMA2_ALLMASK", "DMA chip 2 all mask bits" },
    { 0x00F0, "FPU_CLEAR", "Clears the coprocessor error and IRQ13" },
    { 0x00F1, "FPU_RESET", "Resets the coprocessor" },
    { 0x03B4, "CRTC_M_IDX", "VGA CRTC index, monochrome block" },
    { 0x03B5, "CRTC_M_DATA", "VGA CRTC data, monochrome block" },
    { 0x03BA, "VGA_STAT_M", "VGA input status 1 and feature control, monochrome block" },
    { 0x03C0, "ATTR_INDEX", "VGA attribute index and data, by flip-flop" },
    { 0x03C1, "ATTR_DATA", "VGA attribute data read" },
    { 0x03C2, "VGA_MISC_W", "VGA input status 0 and miscellaneous output" },
    { 0x03C3, "VGA_ENABLE", "VGA enable" },
    { 0x03C4, "SEQ_INDEX", "VGA sequencer index" },
    { 0x03C5, "SEQ_DATA", "VGA sequencer data" },
    { 0x03C6, "DAC_MASK", "VGA pel mask" },
    { 0x03C7, "DAC_READ", "VGA DAC state and read index" },
    { 0x03C8, "DAC_WRITE", "VGA DAC write index" },
    { 0x03C9, "DAC_DATA", "VGA DAC data" },
    { 0x03CA, "VGA_FEATURE", "VGA feature control read" },
    { 0x03CC, "VGA_MISC_R", "VGA miscellaneous output read" },
    { 0x03CE, "GFX_INDEX", "VGA graphics controller index" },
    { 0x03CF, "GFX_DATA", "VGA graphics controller data" },
    { 0x03D4, "CRTC_C_IDX", "VGA CRTC index, color block" },
    { 0x03D5, "CRTC_C_DATA", "VGA CRTC data, color block" },
    { 0x03DA, "VGA_STAT_C", "VGA input status 1 and feature control, color block" },
    { 0x0402, "DEBUG_CON", "Debug console, SeaBIOS messages" },
    { 0x0510, "FW_CFG_SEL", "Firmware config selector" },
    { 0x0511, "FW_CFG_DATA", "Firmware config data" },
    { 0x0170, "IDE1_DATA", "Secondary IDE data, 16 bits" },
    { 0x0171, "IDE1_ERROR", "Secondary IDE error, features on write" },
    { 0x0172, "IDE1_COUNT", "Secondary IDE sector count" },
    { 0x0173, "IDE1_SECTOR", "Secondary IDE sector number, LBA 7-0" },
    { 0x0174, "IDE1_CYL_LO", "Secondary IDE cylinder low, LBA 15-8" },
    { 0x0175, "IDE1_CYL_HI", "Secondary IDE cylinder high, LBA 23-16" },
    { 0x0176, "IDE1_HEAD", "Secondary IDE device, head and LBA 27-24" },
    { 0x0177, "IDE1_STATUS", "Secondary IDE status, command on write" },
    { 0x01F0, "IDE0_DATA", "Primary IDE data, 16 bits" },
    { 0x01F1, "IDE0_ERROR", "Primary IDE error, features on write" },
    { 0x01F2, "IDE0_COUNT", "Primary IDE sector count" },
    { 0x01F3, "IDE0_SECTOR", "Primary IDE sector number, LBA 7-0" },
    { 0x01F4, "IDE0_CYL_LO", "Primary IDE cylinder low, LBA 15-8" },
    { 0x01F5, "IDE0_CYL_HI", "Primary IDE cylinder high, LBA 23-16" },
    { 0x01F6, "IDE0_HEAD", "Primary IDE device, head and LBA 27-24" },
    { 0x01F7, "IDE0_STATUS", "Primary IDE status, command on write" },
    { 0x0376, "IDE1_CONTROL", "Secondary IDE alternate status, device control on write" },
    { 0x03F6, "IDE0_CONTROL", "Primary IDE alternate status, device control on write" },
    { 0x03F2, "FDC_DOR", "Floppy digital output: motors, DMA gate, reset, drive" },
    { 0x03F4, "FDC_MSR", "Floppy main status, data rate select on write" },
    { 0x03F5, "FDC_DATA", "Floppy commands, results and data" },
    { 0x03F7, "FDC_DIR", "Floppy disk change, configuration control on write" },
};

static const int k_debug_port_label_count = sizeof(k_debug_port_labels) / sizeof(k_debug_port_labels[0]);

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

static const int k_debug_interrupt_function_count = sizeof(k_debug_interrupt_functions) / sizeof(k_debug_interrupt_functions[0]);

static const char* const k_debug_irq_sources[16] =
{
    "TIMER", "KEYBOARD", "CASCADE", "COM2", "COM1", "LPT2/SOUND", "FLOPPY", "LPT1",
    "RTC", "ISA IRQ2", "ISA", "ISA", "PS/2 MOUSE", "FPU", "PRIMARY IDE", "SECONDARY IDE"
};

static const char* const k_debug_dma_devices[8] =
{
    "SPARE", "SOUND", "FLOPPY", "SPARE", "CASCADE", "SOUND 16", "SPARE", "SPARE"
};

static const char* const k_debug_pit_uses[3] =
{
    "SYSTEM TIMER", "DRAM REFRESH", "SPEAKER"
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

static const char* const k_debug_dma_command_names[8] =
{
    "MEM-MEM", "HOLD", "DISABLE", "COMPRESS", "ROTATE", "EXTENDED", "DREQ LOW", "DACK HIGH"
};

static const char* const k_debug_rtc_register_names[14] =
{
    "SEC", "SEC ALARM", "MIN", "MIN ALARM", "HOUR", "HOUR ALARM", "WEEKDAY", "DAY", "MONTH", "YEAR", "STATUS A", "STATUS B",
    "STATUS C", "STATUS D"
};

static const char* const k_debug_weekday_names[7] =
{
    "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"
};

static const char* const k_debug_sequencer_register_names[5] =
{
    "RESET", "CLOCKING", "MAP MASK", "CHAR MAP", "MEM MODE"
};

static const char* const k_debug_graphics_register_names[9] =
{
    "SET/RESET", "ENABLE S/R", "COLOR CMP", "ROTATE/OP", "READ MAP", "MODE", "MISC", "DONT CARE", "BIT MASK"
};

static const char* const k_debug_crtc_register_names[25] =
{
    "H TOTAL", "H DISP END", "H BLANK ST", "H BLANK END", "H SYNC ST", "H SYNC END", "V TOTAL", "OVERFLOW", "PRESET ROW",
    "MAX SCAN", "CURSOR ST", "CURSOR END", "START HI", "START LO", "CURSOR HI", "CURSOR LO", "V SYNC ST", "V SYNC END",
    "V DISP END", "OFFSET", "UNDERLINE", "V BLANK ST", "V BLANK END", "MODE", "LINE CMP"
};

static const char* const k_debug_mc6845_register_names[18] =
{
    "H TOTAL", "H DISPLAYED", "H SYNC POS", "SYNC WIDTH", "V TOTAL", "V ADJUST", "V DISPLAYED", "V SYNC POS",
    "INTERLACE", "MAX SCAN", "CURSOR ST", "CURSOR END", "START HI", "START LO", "CURSOR HI", "CURSOR LO", "PEN HI", "PEN LO"
};

static const char* const k_debug_cga_register_names[2] =
{
    "MODE", "COLOR"
};

static const char* const k_debug_attribute_register_names[21] =
{
    "PAL 0", "PAL 1", "PAL 2", "PAL 3", "PAL 4", "PAL 5", "PAL 6", "PAL 7", "PAL 8", "PAL 9", "PAL A", "PAL B", "PAL C",
    "PAL D", "PAL E", "PAL F", "MODE", "OVERSCAN", "PLANE EN", "PAN", "COLOR SEL"
};

static const char* const k_debug_cirrus_sequencer_register_names[27] =
{
    "RESERVED", "UNLOCK", "EXT MODE", "EEPROM", "SCRATCH 9", "SCRATCH A", "VCLK0 NUM", "VCLK1 NUM", "VCLK2 NUM",
    "VCLK3 NUM", "DRAM", "CURSOR X", "CURSOR Y", "CURSOR ATTR", "CURSOR ADDR", "SCRATCH 14", "SCRATCH 15", "FIFO/RDY",
    "CONFIG", "SIGNATURE", "TEST 19", "RESERVED", "VCLK0 DEN", "VCLK1 DEN", "VCLK2 DEN", "VCLK3 DEN", "MCLK"
};

static const char* const k_debug_cirrus_graphics_register_names[9] =
{
    "OFFSET 0", "OFFSET 1", "EXT MODE", "COLOR KEY", "KEY MASK", "DCLK", "RESERVED", "BG HIGH", "FG HIGH"
};

static const char* const k_debug_cirrus_blt_register_names[26] =
{
    "BLT W LO", "BLT W HI", "BLT H LO", "BLT H HI", "DST PITCH L", "DST PITCH H", "SRC PITCH L", "SRC PITCH H",
    "DST ADDR L", "DST ADDR M", "DST ADDR H", "RESERVED", "SRC ADDR L", "SRC ADDR M", "SRC ADDR H", "DST MASK",
    "BLT MODE", "BLT STATUS", "BLT ROP", "RESERVED", "TRANS CLR L", "TRANS CLR H", "RESERVED", "RESERVED",
    "TRANS MSK L", "TRANS MSK H"
};

static const char* const k_debug_cirrus_crtc_register_names[5] =
{
    "INTERLACE", "MISC CTL", "EXT DISP", "RESERVED", "OVERLAY"
};

static const char* const k_debug_cirrus_id_register_names[3] =
{
    "PART STATUS", "AR INDEX", "CHIP ID"
};

static const char* const k_debug_extended_format_names[8] =
{
    "PALETTE", "GREY", "3:3:2", "5:5:5", "5:5:5 MIXED", "5:6:5", "8:8:8", "BLANK"
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

static const char* const k_debug_floppy_media_names[4] = { "2D", "2DD", "2HD", "ED" };

static const char* const k_debug_floppy_type_names[6] =
{
    "NONE", "360K 5.25", "1.2M 5.25", "720K 3.5", "1.44M 3.5", "2.88M 3.5"
};

static const char* const k_debug_fdc_command_names[21] =
{
    "NONE", "READ DATA", "READ DELETED", "WRITE DATA", "WRITE DELETED", "READ TRACK", "VERIFY", "FORMAT TRACK", "READ ID",
    "RECALIBRATE", "SEEK", "RELATIVE SEEK", "SENSE INTERRUPT", "SENSE DRIVE", "SPECIFY", "CONFIGURE", "VERSION", "DUMPREG",
    "PERPENDICULAR", "LOCK", "INVALID"
};

static const char* const k_debug_fdc_phase_names[4] = { "RESET", "COMMAND", "EXECUTION", "RESULT" };
static const char* const k_debug_ide_phase_names[11] =
{
    "IDLE", "READ", "DATA IN", "WRITE WAIT", "DATA OUT", "WRITE", "FORMAT", "COMPLETE", "DIAGNOSTIC", "RESET HELD",
    "RESET"
};

static const char* const k_debug_cdrom_phase_names[12] =
{
    "IDLE", "PACKET WAIT", "PACKET", "REPLY", "READ", "DATA IN", "DATA OUT", "IDENTIFY", "COMPLETE", "RESET HELD",
    "RESET", "DIAGNOSTIC"
};

static const char* const k_debug_ide_slot_names[4] = { "primary master", "primary slave", "secondary master", "secondary slave" };

static inline const char* gui_debug_ide_command_name(u8 command)
{
    switch (command & 0xF0)
    {
        case 0x10: return "RECALIBRATE";
        case 0x70: return "SEEK";
        default: break;
    }

    switch (command)
    {
        case 0x20: case 0x21: return "READ SECTORS";
        case 0x30: case 0x31: return "WRITE SECTORS";
        case 0x40: case 0x41: return "READ VERIFY";
        case 0x50: return "FORMAT TRACK";
        case 0x90: return "DIAGNOSTIC";
        case 0x91: return "INIT PARAMETERS";
        case 0xC4: return "READ MULTIPLE";
        case 0xC5: return "WRITE MULTIPLE";
        case 0xC6: return "SET MULTIPLE";
        case 0xE0: return "STANDBY IMMEDIATE";
        case 0xE1: return "IDLE IMMEDIATE";
        case 0xE2: return "STANDBY";
        case 0xE3: return "IDLE";
        case 0xE5: return "CHECK POWER";
        case 0xE6: return "SLEEP";
        case 0xEC: return "IDENTIFY";
        case 0xEF: return "SET FEATURES";
        default: return "UNKNOWN";
    }
}
static const char* const k_debug_fdc_rate_names[4] = { "500 KBPS", "300 KBPS", "250 KBPS", "1 MBPS" };

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

#endif /* GUI_DEBUG_CONSTANTS_H */
