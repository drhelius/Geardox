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

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "trace_logger_formatter.h"
#include "gui_debug_constants.h"
#include "gui_debug_i386_tables.h"
#include "../emu.h"
#include <algorithm>
#include <vector>
#include "drive/fat_image.h"
#include "media/media_file.h"
#include "drive/hard_disk_image.h"
#include "drive/i82077.h"
#include "drive/ide_cdrom.h"
#include "drive/ide_controller.h"
#include "drive/ide_disk.h"
#include "system/i8259.h"

static const char* const k_trace_pit_names[3] = { "CH0", "CH1", "CH2" };
static const char* const k_trace_pcm_formats[6] = { "PCM8", "PCM16", "ADPCM2", "ADPCM3", "ADPCM4", "SILENCE" };
static const char* const k_trace_uart_registers[8] = { "RBR/THR", "IER", "IIR/FCR", "LCR", "MCR", "LSR", "MSR", "SCR" };
static const char* const k_trace_cpu_modes[3] = { "REAL", "PM", "V86" };

static void format_event(char* buffer, size_t size, const char* tag, const char* name, const char* format, ...);
static void format_cpu(const GDOX_Trace_Entry& entry, const GDOX_Trace_Format_Options& options, char* buffer, size_t size);
static void format_interrupt(const GDOX_Trace_Entry& entry, char* buffer, size_t size);
static void format_io(const GDOX_Trace_Entry& entry, char* buffer, size_t size);
static void format_pic(const GDOX_Trace_Entry& entry, char* buffer, size_t size);
static void format_timer(const GDOX_Trace_Entry& entry, char* buffer, size_t size);
static void format_dma(const GDOX_Trace_Entry& entry, char* buffer, size_t size);
static void format_video(const GDOX_Trace_Entry& entry, char* buffer, size_t size);
static void format_fm(const GDOX_Trace_Entry& entry, char* buffer, size_t size);
static void format_pcm(const GDOX_Trace_Entry& entry, char* buffer, size_t size);
static void format_mixer(const GDOX_Trace_Entry& entry, char* buffer, size_t size);
static void format_midi(const GDOX_Trace_Entry& entry, char* buffer, size_t size);
static void format_cdrom(const GDOX_Trace_Entry& entry, char* buffer, size_t size);
static void format_fdc(const GDOX_Trace_Entry& entry, char* buffer, size_t size);
static void format_ide(const GDOX_Trace_Entry& entry, char* buffer, size_t size);
static void format_keyboard(const GDOX_Trace_Entry& entry, char* buffer, size_t size);
static void format_input(const GDOX_Trace_Entry& entry, char* buffer, size_t size);
static void format_serial(const GDOX_Trace_Entry& entry, char* buffer, size_t size);
static void format_parallel(const GDOX_Trace_Entry& entry, char* buffer, size_t size);
static void format_system(const GDOX_Trace_Entry& entry, char* buffer, size_t size);
static void format_gus(const GDOX_Trace_Entry& entry, char* buffer, size_t size);
static void format_voodoo(const GDOX_Trace_Entry& entry, char* buffer, size_t size);
static void format_net(const GDOX_Trace_Entry& entry, char* buffer, size_t size);
static void format_mark(const GDOX_Trace_Entry& entry, char* buffer, size_t size);
static void format_bit_list(u8 bits, int base, char* buffer, size_t size);
static void format_msf(u32 lba, char* buffer, size_t size);
static void format_buttons(u8 device, u16 buttons, char* buffer, size_t size);
static void format_bytes(const u8* data, int count, char* buffer, size_t size);

void trace_log_format_far_address(u16 cs, u32 eip, u8 mode, bool padded, char* buffer, size_t buffer_size)
{
    if (mode == I386_MODE_PROTECTED)
        snprintf(buffer, buffer_size, "%04X:%08X", cs, eip);
    else
        snprintf(buffer, buffer_size, padded ? "%04X:%04X    " : "%04X:%04X", cs, eip & 0xFFFF);
}

// VM RF NT, IOPL and then the status flags, upper case when set
void trace_log_format_cpu_flags(const GDOX_Trace_Entry& entry, char* buffer, size_t buffer_size)
{
    u32 f = entry.cpu.eflags;

    snprintf(buffer, buffer_size, "%c%c%c%u%c%c%c%c%c%c%c%c%c",
        (f & I386_FLAG_VM) ? 'V' : 'v', (f & I386_FLAG_RF) ? 'R' : 'r', (f & I386_FLAG_NT) ? 'N' : 'n',
        (unsigned)((f & I386_FLAG_IOPL) >> 12), (f & I386_FLAG_OF) ? 'O' : 'o', (f & I386_FLAG_DF) ? 'D' : 'd',
        (f & I386_FLAG_IF) ? 'I' : 'i', (f & I386_FLAG_TF) ? 'T' : 't', (f & I386_FLAG_SF) ? 'S' : 's',
        (f & I386_FLAG_ZF) ? 'Z' : 'z', (f & I386_FLAG_AF) ? 'A' : 'a', (f & I386_FLAG_PF) ? 'P' : 'p',
        (f & I386_FLAG_CF) ? 'C' : 'c');
}

void trace_log_format_cpu_bytes(const GDOX_Trace_Entry& entry, char* buffer, size_t buffer_size)
{
    u8 size = MIN(entry.cpu.size, (u8)GDOX_I386_MAX_INSTRUCTION_LENGTH);
    size_t length = hex_encode(entry.cpu.opcodes, size, buffer, buffer_size, true, ' ');

    if ((length > 0) && (length + 1 < buffer_size))
    {
        buffer[length] = ' ';
        buffer[length + 1] = 0;
    }
}

void trace_log_format_cycle_prefix(const GDOX_Trace_Entry& entry, bool previous_cycle_valid,
    u64 previous_cycle, char* buffer, size_t buffer_size)
{
    if (previous_cycle_valid && entry.cycle >= previous_cycle)
        snprintf(buffer, buffer_size, "@%012llu +%-12llu ", (unsigned long long)entry.cycle,
            (unsigned long long)(entry.cycle - previous_cycle));
    else if (previous_cycle_valid)
        snprintf(buffer, buffer_size, "@%012llu -%-12llu ", (unsigned long long)entry.cycle,
            (unsigned long long)(previous_cycle - entry.cycle));
    else
        snprintf(buffer, buffer_size, "@%012llu               ", (unsigned long long)entry.cycle);
}

void trace_logger_format_entry(const GDOX_Trace_Entry& entry,
    const GDOX_Trace_Format_Options& options, char* buffer, size_t buffer_size)
{
    if (options.cycles)
    {
        GDOX_Trace_Format_Options body_options = options;
        body_options.cycles = false;
        char body[GDOX_TRACE_FORMAT_BUFFER_SIZE];
        char prefix[64];
        trace_logger_format_entry(entry, body_options, body, sizeof(body));
        trace_log_format_cycle_prefix(entry, options.previous_cycle_valid, options.previous_cycle, prefix, sizeof(prefix));
        snprintf(buffer, buffer_size, "%s%s", prefix, body);
        return;
    }

    switch (entry.type)
    {
        case TRACE_CPU:
            format_cpu(entry, options, buffer, buffer_size);
            break;
        case TRACE_CPU_INTERRUPT:
            format_interrupt(entry, buffer, buffer_size);
            break;
        case TRACE_IO:
            format_io(entry, buffer, buffer_size);
            break;
        case TRACE_PIC:
            format_pic(entry, buffer, buffer_size);
            break;
        case TRACE_TIMER:
            format_timer(entry, buffer, buffer_size);
            break;
        case TRACE_DMA:
            format_dma(entry, buffer, buffer_size);
            break;
        case TRACE_VIDEO:
            format_video(entry, buffer, buffer_size);
            break;
        case TRACE_FM:
            format_fm(entry, buffer, buffer_size);
            break;
        case TRACE_PCM:
            format_pcm(entry, buffer, buffer_size);
            break;
        case TRACE_MIXER:
            format_mixer(entry, buffer, buffer_size);
            break;
        case TRACE_MIDI:
            format_midi(entry, buffer, buffer_size);
            break;
        case TRACE_CDROM:
            format_cdrom(entry, buffer, buffer_size);
            break;
        case TRACE_IDE:
            format_ide(entry, buffer, buffer_size);
            break;
        case TRACE_FDC:
            format_fdc(entry, buffer, buffer_size);
            break;
        case TRACE_KEYBOARD:
            format_keyboard(entry, buffer, buffer_size);
            break;
        case TRACE_INPUT:
            format_input(entry, buffer, buffer_size);
            break;
        case TRACE_SERIAL:
            format_serial(entry, buffer, buffer_size);
            break;
        case TRACE_PARALLEL:
            format_parallel(entry, buffer, buffer_size);
            break;
        case TRACE_SYSTEM:
            format_system(entry, buffer, buffer_size);
            break;
        case TRACE_GUS:
            format_gus(entry, buffer, buffer_size);
            break;
        case TRACE_VOODOO:
            format_voodoo(entry, buffer, buffer_size);
            break;
        case TRACE_NET:
            format_net(entry, buffer, buffer_size);
            break;
        case TRACE_MARK:
            format_mark(entry, buffer, buffer_size);
            break;
        default:
            snprintf(buffer, buffer_size, "  [TRACE]  UNKNOWN TYPE:$%02X", entry.type);
            break;
    }
}

// Event lines are indented under the instructions, with the tag and the event name in fixed columns
static void format_event(char* buffer, size_t size, const char* tag, const char* name, const char* format, ...)
{
    char tag_text[16];
    snprintf(tag_text, sizeof(tag_text), "[%s]", tag);
    int length = snprintf(buffer, size, "  %-9s%-11s", tag_text, name);

    if (length < 0 || (size_t)length >= size)
        return;

    va_list args;
    va_start(args, format);
    vsnprintf(buffer + length, size - length, format, args);
    va_end(args);
}

static void format_cpu(const GDOX_Trace_Entry& entry, const GDOX_Trace_Format_Options& options, char* buffer, size_t size)
{
    char linear[16] = "";
    char address[24];
    char registers[112] = "";
    char segments[48] = "";
    char flags[24] = "";
    char bytes[48] = "";

    if (options.linear)
        snprintf(linear, sizeof(linear), "%08X ", entry.cpu.linear);

    trace_log_format_far_address(entry.cpu.cs, entry.cpu.eip, entry.cpu.mode, true, address, sizeof(address));

    if (options.registers)
    {
        const u32* r = entry.cpu.registers;
        snprintf(registers, sizeof(registers),
            "EAX:%08X EBX:%08X ECX:%08X EDX:%08X ESI:%08X EDI:%08X EBP:%08X ESP:%08X  ", r[I386_REG_EAX],
            r[I386_REG_EBX], r[I386_REG_ECX], r[I386_REG_EDX], r[I386_REG_ESI], r[I386_REG_EDI], r[I386_REG_EBP],
            r[I386_REG_ESP]);
    }

    if (options.segments)
        snprintf(segments, sizeof(segments), "DS:%04X ES:%04X SS:%04X FS:%04X GS:%04X  ", entry.cpu.ds, entry.cpu.es,
            entry.cpu.ss, entry.cpu.fs, entry.cpu.gs);

    if (options.flags)
    {
        trace_log_format_cpu_flags(entry, flags, sizeof(flags) - 2);
        strcat(flags, "  ");
    }

    if (options.bytes)
        trace_log_format_cpu_bytes(entry, bytes, sizeof(bytes));

    char name[128];
    trace_log_format_cpu_instruction(entry, name, sizeof(name));

    snprintf(buffer, size, "%s%s  %s%s%s%-*s %s", linear, address, registers, segments, flags,
        GDOX_TRACE_INSTRUCTION_WIDTH, name, bytes);
}

void trace_log_format_cpu_instruction(const GDOX_Trace_Entry& entry, char* buffer, size_t buffer_size)
{
    u32 count = MIN((u32)entry.cpu.size, (u32)GDOX_I386_MAX_INSTRUCTION_LENGTH);

    if (!emu_get_core()->GetI386()->FormatInstruction(entry.cpu.opcodes, count, entry.cpu.cs, entry.cpu.eip,
        entry.cpu.linear, entry.cpu.mode, entry.cpu.default32 != 0, buffer, buffer_size))
        snprintf(buffer, buffer_size, "???");
}

static void format_cpu_event(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    static const char* const k_causes[15] = { "MOV CR0", "LMSW", "MOV CR3", "MOV CR4", "INVLPG", "LGDT", "LIDT", "CPUID",
        "RDTSC", "RDMSR", "WRMSR", "JMP", "CALL", "IRET", "INTERRUPT" };
    static const char* const k_classes[3] = { "FAULT", "TRAP", "ABORT" };
    char at[24];
    char to[24];
    u8 mode = MIN(entry.cpu_event.modes & 0x0F, 2);
    u8 to_mode = MIN(entry.cpu_event.vector2 & 0x0F, 2);
    const char* cause = k_causes[MIN(entry.cpu_event.cause, (u8)14)];
    u32 old_value = entry.cpu_event.old_value;
    u32 new_value = entry.cpu_event.new_value;

    trace_log_format_far_address(entry.cpu_event.cs, entry.cpu_event.eip, mode, false, at, sizeof(at));
    trace_log_format_far_address(entry.cpu_event.to_cs, entry.cpu_event.to_eip, to_mode, false, to, sizeof(to));

    switch (entry.event)
    {
        case TRACE_CPU_INTERRUPT_RAISE:
        case TRACE_CPU_INTERRUPT_SHUTDOWN:
        {
            char name[16];
            char description[64];
            char error[24] = "";
            char cr2[24] = "";
            char next[40] = "";
            u8 vector = entry.cpu_event.vector;

            gui_debug_i386_vector_name(vector, name, sizeof(name), description, sizeof(description), false,
                I386_INTERRUPT_EXCEPTION);

            if (entry.cpu_event.has_error_code)
                snprintf(error, sizeof(error), "  Error:$%04X", old_value);

            if (vector == 14)
                snprintf(cr2, sizeof(cr2), "  CR2:$%08X", new_value);

            if (entry.cpu_event.vector2 != 0xFF)
                snprintf(next, sizeof(next), "  Then #%02X while delivering", entry.cpu_event.vector2);

            if (entry.event == TRACE_CPU_INTERRUPT_SHUTDOWN)
                format_event(buffer, size, "CPU", "SHUTDOWN", "Triple fault  %s%s%s%s  At:%s  ESP:$%08X",
                    vector == 0xFF ? "No vector" : name, error, cr2, next, at, entry.cpu_event.extra);
            else
                format_event(buffer, size, "CPU", "EXCEPTION", "%s %s %s%s%s  At:%s%s%s", name, description,
                    k_classes[MIN(entry.cpu_event.cause, (u8)2)], error, cr2, at,
                    (entry.flags & TRACE_CPU_FLAG_FAILED) != 0 ? "  NOT DELIVERED" : "", next);
            break;
        }
        case TRACE_CPU_INTERRUPT_RETURN:
            format_event(buffer, size, "CPU", "IRET", "From:%s  To:%s  %s:%u->%s:%u  EFLAGS:$%08X  Calls:%u", at, to,
                k_trace_cpu_modes[mode], entry.cpu_event.modes >> 4, k_trace_cpu_modes[to_mode],
                entry.cpu_event.vector2 >> 4, new_value, entry.cpu_event.extra);
            break;
        case TRACE_CPU_INTERRUPT_MODE:
            format_event(buffer, size, "CPU", "MODE", "%s  CR0:$%08X->$%08X  PE:%u->%u PG:%u->%u  Now %s  At:%s", cause,
                old_value, new_value, old_value & 1, new_value & 1, old_value >> 31, new_value >> 31,
                k_trace_cpu_modes[mode], at);
            break;
        case TRACE_CPU_INTERRUPT_PAGING:
            if (entry.cpu_event.cause == TRACE_CPU_CAUSE_INVLPG)
                format_event(buffer, size, "CPU", "PAGING", "INVLPG $%08X  CR3:$%08X  At:%s", new_value,
                    entry.cpu_event.extra, at);
            else if (entry.cpu_event.cause == TRACE_CPU_CAUSE_MOV_CR0)
                format_event(buffer, size, "CPU", "PAGING", "Paging %s  CR0:$%08X  CR3:$%08X  At:%s",
                    (new_value & 0x80000000U) != 0 ? "ON" : "OFF", new_value, entry.cpu_event.extra, at);
            else
                format_event(buffer, size, "CPU", "PAGING", "%s $%08X -> $%08X  At:%s", cause, old_value, new_value, at);
            break;
        case TRACE_CPU_INTERRUPT_TASK:
            format_event(buffer, size, "CPU", "TASK", "%s  TR:$%04X -> $%04X%s  From:%s  To:%s  CR3:$%08X", cause,
                old_value, new_value, entry.cpu_event.selector != 0 ? " via gate" : "", at, to, entry.cpu_event.extra);
            break;
        case TRACE_CPU_INTERRUPT_TABLES:
            format_event(buffer, size, "CPU", "TABLES", "%s  Base:$%08X -> $%08X  Limit:$%04X -> $%04X  At:%s", cause,
                old_value, new_value, entry.cpu_event.extra2, entry.cpu_event.extra, at);
            break;
        default:
            if (entry.cpu_event.cause == TRACE_CPU_CAUSE_CPUID)
                format_event(buffer, size, "CPU", "CPUID", "EAX:$%08X -> EAX:$%08X EBX:$%08X EDX:$%08X  At:%s", old_value,
                    new_value, entry.cpu_event.extra, entry.cpu_event.extra2, at);
            else if (entry.cpu_event.cause == TRACE_CPU_CAUSE_RDTSC)
                format_event(buffer, size, "CPU", "RDTSC", "$%08X%08X  At:%s", entry.cpu_event.extra, new_value, at);
            else
                format_event(buffer, size, "CPU", cause, "MSR $%08X %s $%08X%08X%s  At:%s", old_value,
                    entry.cpu_event.cause == TRACE_CPU_CAUSE_WRMSR ? "<-" : "->", entry.cpu_event.extra, new_value,
                    (entry.flags & TRACE_CPU_FLAG_FAILED) != 0 ? "  #GP" : "", at);
            break;
    }
}

static void format_interrupt(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    if (entry.event >= TRACE_CPU_INTERRUPT_RAISE)
    {
        format_cpu_event(entry, buffer, size);
        return;
    }

    char name[16];
    char description[64];
    char from[24];
    char to[24];
    char change[32] = "";
    u8 vector = entry.interrupt.vector;
    u8 from_mode = MIN(entry.interrupt.modes & 0x0F, 2);
    u8 to_mode = MIN(entry.interrupt.modes >> 4, 2);
    u8 from_cpl = entry.interrupt.cpls & 0x0F;
    u8 to_cpl = entry.interrupt.cpls >> 4;

    gui_debug_i386_vector_name(vector, name, sizeof(name), description, sizeof(description), false,
        entry.event == TRACE_CPU_INTERRUPT_HARDWARE ? I386_INTERRUPT_HARDWARE :
        entry.event == TRACE_CPU_INTERRUPT_EXCEPTION ? I386_INTERRUPT_EXCEPTION : I386_INTERRUPT_SOFTWARE);
    trace_log_format_far_address(entry.interrupt.from_cs, entry.interrupt.from_eip, from_mode, false, from, sizeof(from));
    trace_log_format_far_address(entry.interrupt.to_cs, entry.interrupt.to_eip, to_mode, false, to, sizeof(to));

    if (from_mode != to_mode || from_cpl != to_cpl)
        snprintf(change, sizeof(change), "  %s:%u->%s:%u", k_trace_cpu_modes[from_mode], from_cpl,
            k_trace_cpu_modes[to_mode], to_cpl);

    switch (entry.event)
    {
        case TRACE_CPU_INTERRUPT_HARDWARE:
        {
            u8 line = entry.interrupt.line;

            if (line < 16)
                format_event(buffer, size, "INT", "IRQ", "IRQ%u %s  Vector:$%02X  From:%s  To:%s%s", line,
                    k_debug_irq_sources[line], vector, from, to, change);
            else
                format_event(buffer, size, "INT", "IRQ", "%s  Vector:$%02X  From:%s  To:%s%s", description, vector,
                    from, to, change);
            break;
        }
        case TRACE_CPU_INTERRUPT_EXCEPTION:
        {
            char error[24] = "";

            if (entry.interrupt.has_error_code)
                snprintf(error, sizeof(error), "  Error:$%04X", entry.interrupt.error_code);

            format_event(buffer, size, "INT", "EXCEPTION", "%s %s  Vector:$%02X%s  At:%s  To:%s%s", name, description,
                vector, error, from, to, change);
            break;
        }
        default:
        {
            const char* function = gui_debug_i386_interrupt_function(vector, entry.interrupt.ax);
            bool named = IsValidPointer(function);
            char text[GDOX_TRACE_INTERRUPT_TEXT_SIZE + 8] = "";

            if (entry.interrupt.text[0] != 0)
                snprintf(text, sizeof(text), "  \"%.*s\"", GDOX_TRACE_INTERRUPT_TEXT_SIZE, entry.interrupt.text);

            format_event(buffer, size, "INT", "SOFTWARE",
                "INT $%02X %s%s%s  AX:%04X BX:%04X CX:%04X DX:%04X SI:%04X DI:%04X DS:%04X ES:%04X%s  From:%s  To:%s%s",
                vector, description, named ? ": " : "", named ? function : "", entry.interrupt.ax, entry.interrupt.bx,
                entry.interrupt.cx, entry.interrupt.dx, entry.interrupt.si, entry.interrupt.di, entry.interrupt.ds,
                entry.interrupt.es, text, from, to, change);
            break;
        }
    }
}

static void format_io(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    const char* label = gui_debug_port_label(entry.io.port);
    const char* name = IsValidPointer(label) ? label : "";
    int digits = MAX(MIN((int)entry.io.size, 4), 1) * 2;
    u32 mask = digits >= 8 ? 0xFFFFFFFFU : BIT_MASK(digits * 4);
    bool write = entry.event == TRACE_IO_WRITE;
    const char* tags = (entry.flags & TRACE_ENTRY_DEBUGGER) != 0 ? "  DEBUGGER" :
        (entry.flags & TRACE_ENTRY_UNMAPPED) != 0 ? "  UNMAPPED" : "";

    switch (entry.event)
    {
        case TRACE_IO_POLL:
            format_event(buffer, size, "I/O", "POLL", "$%04X %-18s -> $%0*X  x%u  %u cycles", entry.io.port, name,
                digits, entry.io.value & mask, entry.io.count, entry.io.cycles);
            break;
        case TRACE_IO_DENIED:
            format_event(buffer, size, "I/O", "DENIED", "$%04X %-18s %s %u-bit  %s", entry.io.port, name,
                (entry.io.reason & 0x01) != 0 ? "OUT" : "IN", entry.io.size * 8,
                (entry.io.reason & 0x02) != 0 ? "I/O bitmap" : "IOPL");
            break;
        default:
            format_event(buffer, size, "I/O", write ? "OUT" : "IN", "$%04X %-18s %s $%0*X%s", entry.io.port, name,
                write ? "<-" : "->", digits, entry.io.value & mask, tags);
            break;
    }
}

static void format_mark(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    switch (entry.event)
    {
        case TRACE_MARK_START:
            format_event(buffer, size, "MARK", "START", "Recording started");
            break;
        case TRACE_MARK_FRAME:
            format_event(buffer, size, "MARK", "FRAME", "%llu", (unsigned long long)entry.mark.value);
            break;
        case TRACE_MARK_STOP:
            format_event(buffer, size, "MARK", "STOP", "Debugger stopped");
            break;
        case TRACE_MARK_CONTINUE:
            format_event(buffer, size, "MARK", "CONTINUE", "Debugger continued");
            break;
        case TRACE_MARK_STATE_LOAD:
            format_event(buffer, size, "MARK", "STATE LOAD", "Machine time jumps");
            break;
        case TRACE_MARK_REWIND:
            format_event(buffer, size, "MARK", "REWIND", "Machine time jumps back");
            break;
        case TRACE_MARK_RESET:
            format_event(buffer, size, "MARK", "RESET", "%s", entry.mark.cause != 0 ? "CPU reset" : "Machine reset");
            break;
        case TRACE_MARK_POWER:
            format_event(buffer, size, "MARK", "POWER", "%s", entry.mark.cause != 0 ? "Power on" : "Power off");
            break;
        case TRACE_MARK_MEDIA:
            format_event(buffer, size, "MARK", "MEDIA", "Media changed");
            break;
        case TRACE_MARK_DEBUGGER_OFF:
            format_event(buffer, size, "MARK", "DEBUG OFF", "The debugger stopped running the machine");
            break;
        default:
            format_event(buffer, size, "MARK", "LOST", "%llu entries", (unsigned long long)entry.mark.value);
            break;
    }
}

static void format_pic(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    const char* chip = entry.pic.chip != 0 ? "SLAVE " : "MASTER";
    int base = entry.pic.chip != 0 ? 8 : 0;
    u8 value = entry.pic.value;
    char state[40];

    snprintf(state, sizeof(state), "IRR:$%02X ISR:$%02X IMR:$%02X", entry.pic.irr, entry.pic.isr, entry.pic.imr);

    switch (entry.event)
    {
        case TRACE_PIC_REQUEST:
        case TRACE_PIC_LINE_LOW:
        {
            u8 line = entry.pic.line & 0x0F;
            bool masked = ((entry.pic.imr >> (line & 7)) & 0x01) != 0;
            bool low = entry.event == TRACE_PIC_LINE_LOW;

            format_event(buffer, size, "PIC", low ? "LINE LOW" : "REQUEST", "IRQ%u %s  Vector:$%02X%s%s%s  %s", line,
                k_debug_irq_sources[line], entry.pic.vector, masked ? "  MASKED" : "",
                (entry.flags & TRACE_PIC_FLAG_COALESCED) != 0 ? "  ALREADY PENDING" : "",
                (entry.flags & TRACE_PIC_FLAG_IN_SERVICE) != 0 ? "  IN SERVICE" : "", state);
            break;
        }
        case TRACE_PIC_SPURIOUS:
            format_event(buffer, size, "PIC", "SPURIOUS", "%s IR7  Vector:$%02X  No request at acknowledge  %s", chip,
                entry.pic.vector, state);
            break;
        case TRACE_PIC_MASK:
        {
            char unmasked[48];
            format_bit_list((u8)~entry.pic.imr, base, unmasked, sizeof(unmasked));
            format_event(buffer, size, "PIC", "MASK", "%s  IMR:$%02X->$%02X  Unmasked IRQs:%s  %s", chip,
                entry.pic.previous, entry.pic.imr, unmasked, state);
            break;
        }
        case TRACE_PIC_COMMAND:
        {
            if ((value & k_i8259_ocw3_select) != 0)
            {
                const char* read = (value & k_i8259_ocw3_rr) == 0 ? "" : (value & k_i8259_ocw3_ris) != 0 ?
                    " READ ISR" : " READ IRR";
                const char* special = (value & k_i8259_ocw3_esmm) == 0 ? "" : (value & k_i8259_ocw3_smm) != 0 ?
                    " SPECIAL MASK ON" : " SPECIAL MASK OFF";

                format_event(buffer, size, "PIC", "OCW3", "%s $%02X %s%s%s  %s", chip, value, read,
                    (value & k_i8259_ocw3_poll) != 0 ? " POLL" : "", special, state);
                break;
            }

            static const char* const k_ocw2_names[8] =
            {
                "CLEAR ROTATE ON AEOI", "NON-SPECIFIC EOI", "NOP", "SPECIFIC EOI", "SET ROTATE ON AEOI",
                "ROTATE ON NON-SPECIFIC EOI", "SET PRIORITY", "ROTATE ON SPECIFIC EOI"
            };
            int operation = value >> 5;
            bool eoi = (operation & 0x01) != 0;
            bool specific = operation == 3 || operation >= 6;

            u8 cleared = (u8)(entry.pic.previous_isr & ~entry.pic.isr);

            if (specific)
                format_event(buffer, size, "PIC", eoi ? "EOI" : "OCW2", "%s $%02X %s IRQ%d%s  %s", chip, value,
                    k_ocw2_names[operation], base + (value & 0x07), eoi && cleared == 0 ? "  NOT IN SERVICE" : "", state);
            else if (eoi && cleared != 0)
            {
                int bit = 0;

                while (bit < 7 && IS_NOT_SET_BIT(cleared, bit))
                    bit++;

                format_event(buffer, size, "PIC", "EOI", "%s $%02X %s  Cleared IRQ%d  %s", chip, value,
                    k_ocw2_names[operation], base + bit, state);
            }
            else
                format_event(buffer, size, "PIC", eoi ? "EOI" : "OCW2", "%s $%02X %s%s  %s", chip, value,
                    k_ocw2_names[operation], eoi ? "  NOTHING IN SERVICE" : "", state);
            break;
        }
        default:
        {
            switch (entry.pic.step)
            {
                case I8259::I8259_INIT_READY:
                    format_event(buffer, size, "PIC", "ICW1", "%s $%02X  %s %s ICW4:%s", chip, value,
                        (value & k_i8259_icw1_ltim) != 0 ? "LEVEL" : "EDGE",
                        (value & k_i8259_icw1_sngl) != 0 ? "SINGLE" : "CASCADE",
                        (value & k_i8259_icw1_ic4) != 0 ? "YES" : "NO");
                    break;
                case I8259::I8259_INIT_ICW2:
                    format_event(buffer, size, "PIC", "ICW2", "%s $%02X  Vectors:$%02X-$%02X", chip, value,
                        value & 0xF8, (value & 0xF8) + 7);
                    break;
                case I8259::I8259_INIT_ICW3:
                    if (entry.pic.chip == 0)
                        format_event(buffer, size, "PIC", "ICW3", "%s $%02X  Slave on IR:$%02X", chip, value, value);
                    else
                        format_event(buffer, size, "PIC", "ICW3", "%s $%02X  Slave ID:%u", chip, value, value & 0x07);
                    break;
                default:
                    format_event(buffer, size, "PIC", "ICW4", "%s $%02X  8086:%u AEOI:%u SFNM:%u", chip, value,
                        (value & k_i8259_icw4_upm) != 0 ? 1 : 0, (value & k_i8259_icw4_aeoi) != 0 ? 1 : 0,
                        (value & k_i8259_icw4_sfnm) != 0 ? 1 : 0);
                    break;
            }
            break;
        }
    }
}

static u32 timer_count(u16 reload, bool bcd)
{
    u32 count = reload;

    if (bcd)
    {
        count = ((reload >> 12) & 0x0F) * 1000 + ((reload >> 8) & 0x0F) * 100 + ((reload >> 4) & 0x0F) * 10 +
            (reload & 0x0F);
        return count == 0 ? 10000 : count;
    }

    return count == 0 ? 65536 : count;
}

static void format_timer_rate(u16 reload, u8 mode, bool bcd, char* buffer, size_t size)
{
    if (mode != 2 && mode != 3 && mode != 6 && mode != 7)
    {
        snprintf(buffer, size, "one-shot %.1f us", timer_count(reload, bcd) * 12.0 * 1000000.0 /
            (double)GDOX_MASTER_CLOCK_RATE);
        return;
    }

    snprintf(buffer, size, "%.2f Hz", (double)GDOX_MASTER_CLOCK_RATE / 12.0 / timer_count(reload, bcd));
}

static void format_timer(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    int channel = MIN((int)entry.timer.counter, 2);
    u8 value = entry.timer.value;
    char rate[32];

    format_timer_rate(entry.timer.reload, entry.timer.mode, entry.timer.bcd != 0, rate, sizeof(rate));

    switch (entry.event)
    {
        case TRACE_TIMER_TIMEOUT:
            format_event(buffer, size, "TIMER", "OUT0", "CH0 %s  Rising edge  IRQ0  Mode:%u Reload:$%04X %s%s",
                k_debug_pit_uses[0], entry.timer.mode, entry.timer.reload, rate,
                (entry.flags & TRACE_TIMER_FLAG_COALESCED) != 0 ? "  SAME IRQ" : "");
            break;
        case TRACE_TIMER_CONTROL:
        {
            if (entry.timer.counter > 2)
            {
                format_event(buffer, size, "TIMER", "READ-BACK", "$%02X %s%s CH0:%u CH1:%u CH2:%u", value,
                    (value & 0x20) == 0 ? " COUNT" : "", (value & 0x10) == 0 ? " STATUS" : "", (value >> 1) & 0x01,
                    (value >> 2) & 0x01, (value >> 3) & 0x01);
                break;
            }

            if (((value >> 4) & 0x03) == 0)
            {
                format_event(buffer, size, "TIMER", "CONTROL", "%s %s  $%02X  LATCH COUNT", k_trace_pit_names[channel],
                    k_debug_pit_uses[channel], value);
                break;
            }

            format_event(buffer, size, "TIMER", "CONTROL", "%s %s  $%02X  Mode:%u %s  Access:%s%s",
                k_trace_pit_names[channel], k_debug_pit_uses[channel], value, entry.timer.mode,
                k_debug_pit_mode_names[MIN(entry.timer.mode, (u8)5)], k_debug_pit_access_names[entry.timer.access & 3],
                entry.timer.bcd ? "  BCD" : "");
            break;
        }
        case TRACE_TIMER_COUNTER:
        {
            if ((entry.flags & TRACE_ENTRY_IGNORED) != 0)
            {
                format_event(buffer, size, "TIMER", "COUNTER", "%s %s  Write:$%02X  IGNORED, no control word yet",
                    k_trace_pit_names[channel], k_debug_pit_uses[channel], value);
                break;
            }

            if (!entry.timer.complete)
            {
                format_event(buffer, size, "TIMER", "COUNTER", "%s %s  Write:$%02X  LSB, MSB pending",
                    k_trace_pit_names[channel], k_debug_pit_uses[channel], value);
                break;
            }

            format_event(buffer, size, "TIMER", "COUNTER", "%s %s  Write:$%02X  Reload:$%04X  Mode:%u  %s",
                k_trace_pit_names[channel], k_debug_pit_uses[channel], value, entry.timer.reload, entry.timer.mode,
                rate);
            break;
        }
        case TRACE_TIMER_READ:
            format_event(buffer, size, "TIMER", "READ", "%s %s  -> $%02X%s", k_trace_pit_names[channel],
                k_debug_pit_uses[channel], value, entry.timer.latch == 1 ? "  LSB" : entry.timer.latch == 2 ? "  MSB" : "");
            break;
        default:
        {
            char tone[32];
            format_timer_rate(entry.timer.speaker_reload, entry.timer.speaker_mode, false, tone, sizeof(tone));
            format_event(buffer, size, "TIMER", "SPEAKER", "Gate:%u  Data:%u  CH2 Mode:%u Reload:$%04X %s",
                value & 0x01, (value >> 1) & 0x01, entry.timer.speaker_mode, entry.timer.speaker_reload, tone);
            break;
        }
    }
}

static void format_dma(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    u8 channel = entry.dma.channel & 0x07;
    const char* device = k_debug_dma_devices[channel];
    u32 chip = (channel >> 2) + 1;

    switch (entry.event)
    {
        case TRACE_DMA_REQUEST:
            format_event(buffer, size, "DMA", "REQUEST", "CH%u %s  Address:$%06X  Count:$%04X  Mode:$%02X%s", channel,
                device, entry.dma.address, entry.dma.count, entry.dma.mode,
                IS_SET_BIT(entry.dma.mask, channel) ? "  MASKED" : "");
            break;
        case TRACE_DMA_END:
        {
            bool word = channel >= 4;
            u32 base = word ? ((u32)(entry.dma.page & 0xFE) << 16) | ((u32)entry.dma.base_address << 1) :
                ((u32)entry.dma.page << 16) | entry.dma.base_address;
            bool auto_init = (entry.dma.mode & 0x10) != 0;

            format_event(buffer, size, "DMA", "END", "CH%u %s  TERMINAL COUNT  %s  Base:$%06X Length:%u %s  %s%s", channel,
                device, k_debug_dma_direction_names[(entry.dma.mode >> 2) & 0x03], base, entry.dma.base_count + 1,
                word ? "words" : "bytes", auto_init ? "AUTO-INIT reloaded" : "SINGLE",
                IS_SET_BIT(entry.dma.mask, channel) ? "  MASKED" : "");
            break;
        }
        case TRACE_DMA_UNSUPPORTED:
        {
            static const char* const k_reasons[3] = { "Command not run: memory to memory, rotating or compressed",
                "Illegal transfer type in the mode", "Channel 4 holds back a request on chip 1" };
            format_event(buffer, size, "DMA", "UNSUPPORTED", "CH%u  Write:$%02X  %s", channel, entry.dma.value,
                k_reasons[MIN(entry.dma.reason, (u8)2)]);
            break;
        }
        default:
        {
            u8 value = entry.dma.value;

            if (entry.dma.reg >= 0x10)
            {
                if ((entry.flags & TRACE_ENTRY_UNMAPPED) != 0)
                    format_event(buffer, size, "DMA", "PAGE", "Port:$%02X  Write:$%02X  No channel uses this page register",
                        0x80 | (entry.dma.reg & 0x0F), value);
                else
                    format_event(buffer, size, "DMA", "PAGE", "Port:$%02X  CH%u %s  Write:$%02X  Address:$%06X",
                        0x80 | (entry.dma.reg & 0x0F), channel, device, value, entry.dma.address);
                break;
            }

            if (entry.dma.reg < 0x08)
            {
                format_event(buffer, size, "DMA", (entry.dma.reg & 0x01) != 0 ? "COUNT" : "ADDRESS",
                    "CH%u %s  Write:$%02X %s  Address:$%06X  Count:$%04X", channel, device, value,
                    entry.dma.flip_flop != 0 ? "LOW " : "HIGH", entry.dma.address, entry.dma.count);
                break;
            }

            switch (entry.dma.reg)
            {
                case 0x08:
                    format_event(buffer, size, "DMA", "COMMAND", "Chip %u  Write:$%02X%s", chip, value,
                        (value & 0x04) != 0 ? "  DISABLED" : "");
                    break;
                case 0x09:
                    format_event(buffer, size, "DMA", "SOFTWARE", "CH%u %s  %s", channel, device,
                        (value & 0x04) != 0 ? "SET" : "CLEAR");
                    break;
                case 0x0A:
                    format_event(buffer, size, "DMA", "MASK", "CH%u %s  %s", channel, device,
                        (value & 0x04) != 0 ? "MASKED" : "UNMASKED");
                    break;
                case 0x0B:
                    format_event(buffer, size, "DMA", "MODE", "CH%u %s  $%02X  %s %s%s%s", channel, device, value,
                        k_debug_dma_direction_names[(value >> 2) & 0x03], k_debug_dma_service_names[(value >> 6) & 0x03],
                        (value & 0x10) != 0 ? " AUTO-INIT" : "", (value & 0x20) != 0 ? " DECREMENT" : "");
                    break;
                case 0x0C:
                    format_event(buffer, size, "DMA", "FLIP-FLOP", "Chip %u  Cleared", chip);
                    break;
                case 0x0D:
                    format_event(buffer, size, "DMA", "CLEAR", "Chip %u  Master clear", chip);
                    break;
                default:
                {
                    char masked[24];
                    u8 mask = (entry.dma.mask >> ((chip - 1) * 4)) & 0x0F;
                    format_bit_list(mask, 0, masked, sizeof(masked));
                    format_event(buffer, size, "DMA", "MASK", "Chip %u  $%X  Masked channels:%s", chip, mask, masked);
                    break;
                }
            }
            break;
        }
    }
}

static const char* video_register_name(u8 event, u8 bank, u8 reg, u8 adapter)
{
    bool cirrus = adapter == TRACE_VIDEO_ADAPTER_CIRRUS;

    if (event == TRACE_VIDEO_CRTC)
    {
        if (adapter == TRACE_VIDEO_ADAPTER_CGA)
            return reg < 18 ? k_debug_mc6845_register_names[reg] : "INVALID";

        if (reg < 25)
            return k_debug_crtc_register_names[reg];

        if (cirrus && reg >= 0x19 && reg <= 0x1D)
            return k_debug_cirrus_crtc_register_names[reg - 0x19];

        if (cirrus && reg >= 0x25 && reg <= 0x27)
            return k_debug_cirrus_id_register_names[reg - 0x25];

        return cirrus ? "EXTENSION" : "INVALID";
    }

    switch (bank)
    {
        case TRACE_VIDEO_BANK_SR:
            if (reg < 5)
                return k_debug_sequencer_register_names[reg];

            return cirrus && reg >= 0x05 && reg <= 0x1F ? k_debug_cirrus_sequencer_register_names[reg - 0x05] : "INVALID";
        case TRACE_VIDEO_BANK_GR:
            if (reg < 9)
                return k_debug_graphics_register_names[reg];

            if (cirrus && reg >= 0x09 && reg <= 0x11)
                return k_debug_cirrus_graphics_register_names[reg - 0x09];

            if (cirrus && reg >= 0x20 && reg <= 0x39)
                return k_debug_cirrus_blt_register_names[reg - 0x20];

            return cirrus ? "EXTENSION" : "INVALID";
        case TRACE_VIDEO_BANK_AR:
            return reg < 21 ? k_debug_attribute_register_names[reg] : "INVALID";
        case TRACE_VIDEO_BANK_CGA:
            return k_debug_cga_register_names[reg & 1];
        case TRACE_VIDEO_BANK_EGA_POSITION:
            return reg != 0 ? "POSITION 2" : "POSITION 1";
        default:
            return "MISC";
    }
}

static const char* blt_rop_name(u8 rop)
{
    switch (rop)
    {
        case 0x00: return "0";
        case 0x05: return "S AND D";
        case 0x06: return "D";
        case 0x09: return "S AND NOT D";
        case 0x0B: return "NOT D";
        case 0x0D: return "S";
        case 0x0E: return "1";
        case 0x50: return "NOT S AND D";
        case 0x59: return "S XOR D";
        case 0x6D: return "S OR D";
        case 0x90: return "NOT (S OR D)";
        case 0x95: return "NOT (S XOR D)";
        case 0xAD: return "S OR NOT D";
        case 0xD0: return "NOT S";
        case 0xD6: return "NOT S OR D";
        case 0xDA: return "NOT (S AND D)";
        default: return "UNLISTED";
    }
}

static void format_video(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    static const char* const k_bank_tags[4] = { "SR", "GR", "AR", "" };
    static const char* const k_adapters[4] = { "VGA", "EGA", "CIRRUS", "CGA" };
    char beam[32] = "CRTC stopped";
    char kept[24] = "";
    u8 raw = entry.video.raw;
    u8 adapter = MIN(entry.video.adapter, (u8)3);

    if (entry.video.line != 0xFFFF)
        snprintf(beam, sizeof(beam), "Line:%u Dot:%u", entry.video.line, entry.video.dot);

    if ((entry.flags & TRACE_ENTRY_IGNORED) != 0)
        snprintf(kept, sizeof(kept), "  Kept:$%02X", (u8)entry.video.value);

    switch (entry.event)
    {
        case TRACE_VIDEO_CRTC:
            format_event(buffer, size, "VIDEO", "CRTC", "%s%02X %-12s <- $%02X%s  %s", adapter == 3 ? "R" : "CR",
                entry.video.reg, video_register_name(entry.event, 0, entry.video.reg, adapter), raw, kept, beam);
            break;
        case TRACE_VIDEO_OUTPUT:
        case TRACE_VIDEO_DATAPATH:
        case TRACE_VIDEO_BLT_REGISTER:
        {
            const char* tag = entry.event == TRACE_VIDEO_DATAPATH ? "DATAPATH" :
                entry.event == TRACE_VIDEO_BLT_REGISTER ? "BLT REG" : "OUTPUT";
            const char* name = video_register_name(entry.event, entry.video.bank, entry.video.reg, adapter);
            char extra[40] = "";

            if (entry.video.index_bits != 0)
                snprintf(extra, sizeof(extra), "  Index bits:%u", entry.video.index_bits);
            else if ((entry.flags & TRACE_VIDEO_FLAG_BANK) != 0)
                snprintf(extra, sizeof(extra), "  Bank:$%06X", entry.video.param);

            if (entry.video.bank == TRACE_VIDEO_BANK_MISC)
                format_event(buffer, size, "VIDEO", tag, "MISC <- $%02X%s  %s", raw, kept, beam);
            else if (entry.video.bank == TRACE_VIDEO_BANK_CGA || entry.video.bank == TRACE_VIDEO_BANK_EGA_POSITION)
                format_event(buffer, size, "VIDEO", tag, "%s <- $%02X%s  %s", name, raw, kept, beam);
            else
                format_event(buffer, size, "VIDEO", tag, "%s%02X %-12s <- $%02X%s%s%s  %s",
                    k_bank_tags[MIN(entry.video.bank, (u8)3)], entry.video.reg, name, raw, kept, extra,
                    (entry.flags & TRACE_VIDEO_FLAG_MMIO) != 0 ? "  MMIO" : "", beam);
            break;
        }
        case TRACE_VIDEO_PALETTE:
        {
            u32 rgb = entry.video.param;
            u8 r = (u8)(rgb >> 16);
            u8 g = (u8)(rgb >> 8);
            u8 b = (u8)rgb;
            format_event(buffer, size, "VIDEO", "PALETTE", "%s[$%02X] RGB6:%02u,%02u,%02u RGB8:$%02X%02X%02X  %s",
                (entry.flags & TRACE_VIDEO_FLAG_EXTENDED_DAC) != 0 ? "EXT" : "DAC", entry.video.reg, r, g, b,
                (r << 2) | (r >> 4), (g << 2) | (g >> 4), (b << 2) | (b >> 4), beam);
            break;
        }
        case TRACE_VIDEO_PALETTE_LOAD:
        {
            u32 first = entry.video.param;
            u32 last = entry.video.param2;
            char lines[32] = "CRTC stopped";

            if (entry.video.line != 0xFFFF)
                snprintf(lines, sizeof(lines), "Lines:%u-%u", entry.video.line, entry.video.last_line);

            format_event(buffer, size, "VIDEO", "PAL LOAD", "%s[$%02X-$%02X] %u colors  First:%02u,%02u,%02u  Last:%02u,%02u,%02u  %s",
                (entry.flags & TRACE_VIDEO_FLAG_EXTENDED_DAC) != 0 ? "EXT" : "DAC", entry.video.reg,
                (entry.video.reg + entry.video.count - 1) & 0xFF, entry.video.count, (first >> 16) & 0x3F,
                (first >> 8) & 0x3F, first & 0x3F, (last >> 16) & 0x3F, (last >> 8) & 0x3F, last & 0x3F, lines);
            break;
        }
        case TRACE_VIDEO_DAC:
            switch (entry.video.bank)
            {
                case TRACE_VIDEO_BANK_HIDDEN_DAC:
                {
                    u8 value = (u8)entry.video.value;
                    format_event(buffer, size, "VIDEO", "HIDDEN DAC", "$%02X  %s  %u bytes per pixel  %s", value,
                        (value & 0x80) == 0 ? "PALETTE" : (value & 0x0F) == 0x00 ? "5:5:5" : (value & 0x0F) == 0x01 ?
                        "5:6:5" : (value & 0x0F) == 0x05 ? "8:8:8" : "EXTENDED", entry.video.param, beam);
                    break;
                }
                case TRACE_VIDEO_BANK_PEL_MASK:
                    format_event(buffer, size, "VIDEO", "PEL MASK", "$03C6 <- $%02X  %s", raw, beam);
                    break;
                default:
                    format_event(buffer, size, "VIDEO", "DAC ADDR", "%s index $%02X  %s",
                        entry.video.bank == TRACE_VIDEO_BANK_DAC_READ ? "READ " : "WRITE", entry.video.reg, beam);
                    break;
            }
            break;
        case TRACE_VIDEO_VSYNC:
        {
            u32 hz = entry.video.param2;
            format_event(buffer, size, "VIDEO", "VSYNC", "Retrace  Frame:%u  %ux%u dots  %u.%02u Hz%s", entry.video.param,
                entry.video.count, entry.video.value, hz / 100, hz % 100,
                (entry.flags & TRACE_VIDEO_FLAG_IRQ) != 0 ? "  IRQ2 raised" : "");
            break;
        }
        case TRACE_VIDEO_VSYNC_CLEAR:
            format_event(buffer, size, "VIDEO", "VSYNC ACK", "IRQ2 cleared  %s", beam);
            break;
        case TRACE_VIDEO_BLT:
        case TRACE_VIDEO_BLT_DONE:
        {
            static const char* const k_kinds[4] = { "SCREEN", "PATTERN", "EXPAND", "SYSTEM" };
            const char* name = entry.event == TRACE_VIDEO_BLT_DONE ? "BLT DONE" : "BITBLT";
            u8 mode = entry.blt.mode;
            u8 bytes = MAX(entry.blt.pixel_bytes, (u8)1);

            format_event(buffer, size, "VIDEO", name,
                "%s %ux%u bytes (%u px)  $%06X -> $%06X  Pitch:%u/%u  ROP:$%02X %s  %u bpp%s%s%s  FG:$%04X BG:$%04X%s",
                k_kinds[MIN(entry.blt.kind, (u8)3)], entry.blt.width, entry.blt.height, entry.blt.width / bytes,
                entry.blt.source, entry.blt.destination, entry.blt.src_pitch, entry.blt.dst_pitch, entry.blt.rop,
                blt_rop_name(entry.blt.rop), bytes * 8, (mode & 0x01) != 0 ? "  BACKWARDS" : "",
                (mode & 0x08) != 0 ? "  TRANSPARENT" : "", (mode & 0x80) != 0 ? "  COLOR EXPAND" : "",
                entry.blt.colors & 0xFFFF, entry.blt.colors >> 16, (entry.flags & TRACE_VIDEO_FLAG_MMIO) != 0 ? "  MMIO" : "");
            break;
        }
        case TRACE_VIDEO_MODE:
        {
            static const char* const k_formats[6] = { "TEXT", "PLANAR 16", "CGA 4", "256", "CGA", "EXTENDED" };
            u32 hz = entry.mode.refresh;
            char text[24] = "";

            if (entry.mode.format == 0 && entry.mode.char_height != 0)
                snprintf(text, sizeof(text), "  Char:%ux%u", entry.mode.char_width, entry.mode.char_height);

            format_event(buffer, size, "VIDEO", "MODE", "%s %ux%u %s%s%s  %u.%02u Hz  Offset:%u  %s:$%02X%s",
                k_adapters[MIN(entry.mode.adapter, (u8)3)], entry.mode.width, entry.mode.height,
                k_formats[MIN(entry.mode.format, (u8)5)], entry.mode.format == 5 ? " " : "",
                entry.mode.format == 5 ? k_debug_extended_format_names[MIN(entry.mode.extended, (u8)7)] : "", hz / 100,
                hz % 100, entry.mode.offset, entry.mode.adapter == TRACE_VIDEO_ADAPTER_CGA ? "MODE" : "MISC",
                entry.mode.misc, text);
            break;
        }
        case TRACE_VIDEO_FRAME:
        {
            char line[24] = "";

            if (entry.frame.line != 0xFFFF)
                snprintf(line, sizeof(line), "  Written at line %u", entry.frame.line);

            format_event(buffer, size, "VIDEO", "FRAME", "Start:$%05X  Line compare:%u  Pan:%u Byte pan:%u  Offset:%u  Frame:%u%s",
                entry.frame.start, entry.frame.line_compare, entry.frame.pel, entry.frame.byte_pan, entry.frame.offset,
                entry.frame.frame, line);
            break;
        }
        case TRACE_VIDEO_STATUS:
            format_event(buffer, size, "VIDEO", "STATUS", "Frame:%u  %u reads of $03DA  Retrace:%u  Display off:%u  Lines:%u-%u",
                entry.status.frame, entry.status.reads, entry.status.retrace, entry.status.display_off,
                entry.status.first_line, entry.status.last_line);
            break;
        default:
        {
            const char* access = raw == 0x03 ? "R/W" : raw == 0x02 ? "W" : "R";
            format_event(buffer, size, "VIDEO", "VBLANK", "MISSED  Watch:$%08X %s  Consecutive:%u", entry.video.param,
                access, entry.video.value);
            break;
        }
    }
}

static void format_fm_pair(u8 pairs, u8 channel, char* buffer, size_t size)
{
    static const u8 k_pair_first[6] = { 0, 1, 2, 9, 10, 11 };
    buffer[0] = 0;

    for (int i = 0; i < 6; i++)
    {
        if (IS_NOT_SET_BIT(pairs, i))
            continue;

        if (channel == k_pair_first[i] || channel == k_pair_first[i] + 3)
        {
            snprintf(buffer, size, "  4-OP CH%u+CH%u", k_pair_first[i] + 1, k_pair_first[i] + 4);
            return;
        }
    }
}

static void format_fm(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    u16 address = entry.fm.address;
    u8 reg = (u8)address;
    u8 value = entry.fm.value;
    u8 channel = entry.fm.channel + 1;
    int chip = entry.fm.flags;
    u16 frequency = entry.fm.frequency;
    char flags[32] = "";
    char pair[24] = "";

    if ((entry.flags & TRACE_FM_FLAG_INVALID) != 0)
        snprintf(flags, sizeof(flags), "  NO REGISTER, IGNORED");
    else if ((entry.flags & TRACE_FM_FLAG_ALIAS) != 0)
        snprintf(flags, sizeof(flags), "  ALIAS OF $%03X", (address & 0x1F0) | (u16)(channel - 1 - (address >> 8) * 9));

    if ((entry.flags & TRACE_FM_FLAG_OPL3) != 0 && entry.fm.channel != 0xFF)
        format_fm_pair(entry.fm.pairs, entry.fm.channel, pair, sizeof(pair));

    switch (entry.event)
    {
        case TRACE_FM_KEY:
        {
            bool on = (value & 0x20) != 0;
            bool was = (entry.fm.previous & 0x20) != 0;
            const char* edge = on && !was ? "ON " : !on && was ? "OFF" : on ? "ON, no edge" : "OFF, no edge";

            if ((entry.flags & TRACE_FM_FLAG_INVALID) != 0)
                format_event(buffer, size, "FM", "KEY", "OPL%d $%03X <- $%02X%s", chip, address, value, flags);
            else
                format_event(buffer, size, "FM", "KEY", "OPL%d CH%u %s  Block:%u FNum:$%03X%s%s", chip, channel, edge,
                    (frequency >> 10) & 0x07, frequency & 0x3FF, pair, flags);
            break;
        }
        case TRACE_FM_FREQUENCY:
            format_event(buffer, size, "FM", "FREQ", "OPL%d CH%u  $%03X <- $%02X  Block:%u FNum:$%03X%s", chip, channel,
                address, value, (frequency >> 10) & 0x07, frequency & 0x3FF, flags);
            break;
        case TRACE_FM_OPERATOR:
        {
            char fields[40];
            char slot[24] = "";

            switch (reg & 0xE0)
            {
                case 0x20: snprintf(fields, sizeof(fields), "AM:%u VIB:%u EG:%u KSR:%u MUL:%u", value >> 7,
                    (value >> 6) & 0x01, (value >> 5) & 0x01, (value >> 4) & 0x01, value & 0x0F); break;
                case 0x40: snprintf(fields, sizeof(fields), "KSL:%u TL:$%02X", value >> 6, value & 0x3F); break;
                case 0x60: snprintf(fields, sizeof(fields), "AR:%u DR:%u", value >> 4, value & 0x0F); break;
                case 0x80: snprintf(fields, sizeof(fields), "SL:%u RR:%u", value >> 4, value & 0x0F); break;
                default: snprintf(fields, sizeof(fields), "WS:%u", value & 0x07); break;
            }

            if (entry.fm.operator_index >= 0)
            {
                int index = entry.fm.operator_index % 18;
                int bank = entry.fm.operator_index / 18;
                snprintf(slot, sizeof(slot), "OP%d CH%d %s  ", entry.fm.operator_index, (index % 3) + 3 * (index / 6) +
                    bank * 9 + 1, ((index / 3) & 0x01) != 0 ? "CAR" : "MOD");
            }

            format_event(buffer, size, "FM", "OPERATOR", "OPL%d $%03X <- $%02X  %s%s%s", chip, address, value, slot,
                fields, flags);
            break;
        }
        case TRACE_FM_CHANNEL:
            format_event(buffer, size, "FM", "CHANNEL", "OPL%d CH%u  $%03X <- $%02X  CNT:%u FB:%u A:%u B:%u%s%s", chip,
                channel, address, value, value & 0x01, (value >> 1) & 0x07, (value >> 4) & 0x01, (value >> 5) & 0x01,
                pair, flags);
            break;
        case TRACE_FM_TIMER:
            if (reg == 0x04)
                format_event(buffer, size, "FM", "TIMER",
                    "OPL%d CONTROL <- $%02X  Reset:%u Mask 1:%u 2:%u  Start 1:%u 2:%u", chip, value, value >> 7,
                    (value >> 6) & 0x01, (value >> 5) & 0x01, value & 0x01, (value >> 1) & 0x01);
            else
                format_event(buffer, size, "FM", "TIMER", "OPL%d Timer %u <- $%02X  Period:%u us", chip, reg - 1, value,
                    (256 - value) * (reg == 0x02 ? 80 : 320));
            break;
        case TRACE_FM_STATUS:
            format_event(buffer, size, "FM", "STATUS", "OPL%d $%02X -> $%02X  IRQ:%u T1:%u T2:%u", chip, entry.fm.previous,
                value, value >> 7, (value >> 6) & 0x01, (value >> 5) & 0x01);
            break;
        default:
            if (address == 0x0BD)
                format_event(buffer, size, "FM", "GLOBAL", "OPL%d $BD <- $%02X  AM:%u VIB:%u Rhythm:%u Drums:$%02X", chip,
                    value, value >> 7, (value >> 6) & 0x01, (value >> 5) & 0x01, value & 0x1F);
            else if (address == 0x001)
                format_event(buffer, size, "FM", "GLOBAL", "OPL%d $001 <- $%02X  Waveform select:%s", chip, value,
                    (value & 0x20) != 0 ? "ON" : "OFF");
            else if (address == 0x008)
                format_event(buffer, size, "FM", "GLOBAL", "OPL%d $008 <- $%02X  CSM:%u NOTE-SEL:%u", chip, value,
                    value >> 7, (value >> 6) & 0x01);
            else if (address == 0x104)
                format_event(buffer, size, "FM", "GLOBAL", "OPL%d $104 <- $%02X  4-OP pairs:%s%s%s%s%s%s", chip, value,
                    (value & 0x01) != 0 ? " 1+4" : "", (value & 0x02) != 0 ? " 2+5" : "", (value & 0x04) != 0 ? " 3+6" : "",
                    (value & 0x08) != 0 ? " 10+13" : "", (value & 0x10) != 0 ? " 11+14" : "",
                    (value & 0x20) != 0 ? " 12+15" : "");
            else if (address == 0x105)
                format_event(buffer, size, "FM", "GLOBAL", "OPL%d $105 <- $%02X  OPL3 mode:%s", chip, value,
                    (value & 0x01) != 0 ? "ON" : "OFF");
            else
                format_event(buffer, size, "FM", "GLOBAL", "OPL%d $%03X <- $%02X%s", chip, address, value, flags);
            break;
    }
}

static const char* dsp_command_name(u8 command)
{
    switch (command)
    {
        case 0x10: return "DIRECT DAC";
        case 0x14: return "DMA DAC 8-BIT";
        case 0x16: return "DMA DAC 2-BIT ADPCM";
        case 0x17: return "DMA DAC 2-BIT ADPCM REF";
        case 0x1C: return "AUTO DMA DAC 8-BIT";
        case 0x1F: return "AUTO DMA DAC 2-BIT ADPCM REF";
        case 0x20: return "DIRECT ADC";
        case 0x24: return "DMA ADC 8-BIT";
        case 0x2C: return "AUTO DMA ADC 8-BIT";
        case 0x30: case 0x31: case 0x34: case 0x35: case 0x36: case 0x37: return "MIDI READ";
        case 0x38: return "MIDI WRITE";
        case 0x40: return "TIME CONSTANT";
        case 0x41: return "OUTPUT RATE";
        case 0x42: return "INPUT RATE";
        case 0x48: return "BLOCK SIZE";
        case 0x74: return "DMA DAC 4-BIT ADPCM";
        case 0x75: return "DMA DAC 4-BIT ADPCM REF";
        case 0x76: return "DMA DAC 3-BIT ADPCM";
        case 0x77: return "DMA DAC 3-BIT ADPCM REF";
        case 0x7D: return "AUTO DMA DAC 4-BIT ADPCM REF";
        case 0x7F: return "AUTO DMA DAC 3-BIT ADPCM REF";
        case 0x80: return "SILENCE";
        case 0x90: return "AUTO HIGH-SPEED DAC";
        case 0x91: return "HIGH-SPEED DAC";
        case 0x98: return "AUTO HIGH-SPEED ADC";
        case 0x99: return "HIGH-SPEED ADC";
        case 0xA0: return "MONO INPUT";
        case 0xA8: return "STEREO INPUT";
        case 0xD0: return "PAUSE 8-BIT";
        case 0xD1: return "SPEAKER ON";
        case 0xD3: return "SPEAKER OFF";
        case 0xD4: return "CONTINUE 8-BIT";
        case 0xD5: return "PAUSE 16-BIT";
        case 0xD6: return "CONTINUE 16-BIT";
        case 0xD8: return "SPEAKER STATUS";
        case 0xD9: return "EXIT AUTO 16-BIT";
        case 0xDA: return "EXIT AUTO 8-BIT";
        case 0xE0: return "IDENTIFY";
        case 0xE1: return "VERSION";
        case 0xE2: return "DMA IDENTIFY";
        case 0xE3: return "COPYRIGHT";
        case 0xE4: return "WRITE TEST";
        case 0xE8: return "READ TEST";
        case 0xF2: return "IRQ 8-BIT";
        case 0xF3: return "IRQ 16-BIT";
        case 0xFB: return "STATUS";
        case 0xFC: return "AUX STATUS";
        case 0xFD: return "LAST COMMAND";
        default:
            if (command >= 0xB0 && command <= 0xBF)
                return "16-BIT DMA";

            if (command >= 0xC0 && command <= 0xCF)
                return "8-BIT DMA";

            return "UNKNOWN";
    }
}

static void format_pcm(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    u8 command = entry.pcm.command;

    switch (entry.event)
    {
        case TRACE_PCM_COMMAND:
        case TRACE_PCM_DIRECT:
        {
            const u8* o = entry.pcm.operands;
            char operands[16] = "";

            for (int i = 0; i < MIN((int)entry.pcm.operand_count, 3); i++)
                snprintf(operands + strlen(operands), sizeof(operands) - strlen(operands), " $%02X", o[i]);

            format_event(buffer, size, "DSP", entry.event == TRACE_PCM_DIRECT ? "DIRECT" : "COMMAND", "$%02X%s  %s",
                command, operands, dsp_command_name(command));
            break;
        }
        case TRACE_PCM_TRANSFER:
        {
            u8 flags = entry.pcm.flags;
            format_event(buffer, size, "DSP", "TRANSFER", "%s %s  %s%s%s%s%u Hz ($%02X)  Length:%u  DMA %u%s  $%02X %s",
                k_trace_pcm_formats[MIN(entry.pcm.format, (u8)5)], (flags & 0x02) != 0 ? "IN " : "OUT",
                (flags & 0x01) != 0 ? "AUTO " : "SINGLE ", (flags & 0x04) != 0 ? "HIGH SPEED " : "",
                (flags & 0x08) != 0 ? "STEREO " : "", (flags & 0x10) != 0 ? "SIGNED " : "", entry.pcm.rate,
                entry.pcm.rate_source, entry.pcm.length, entry.pcm.dma,
                (entry.flags & TRACE_PCM_FLAG_HANDOFF) != 0 ? "  NEXT BLOCK, after the current one" : "", command,
                dsp_command_name(command));
            break;
        }
        case TRACE_PCM_END:
            format_event(buffer, size, "DSP", "END", "%s  %s  Length:%u Left:%u  Underruns:%u",
                k_trace_pcm_formats[MIN(entry.pcm.format, (u8)5)], entry.pcm.reason == 0 ? "Block done" :
                "Stopped by a command or reset", entry.pcm.length, entry.pcm.remaining, entry.pcm.underruns);
            break;
        case TRACE_PCM_IRQ:
            format_event(buffer, size, "DSP", "IRQ", "%u-bit block end  IRQ %u  Status:$%02X%s  Underruns:%u", command,
                entry.pcm.irq, entry.pcm.flags, (entry.flags & TRACE_PCM_FLAG_NOT_ACKED) != 0 ? "  PREVIOUS NOT ACKED" : "",
                entry.pcm.underruns);
            break;
        case TRACE_PCM_ACK:
            format_event(buffer, size, "DSP", "ACK", "%u-bit  Status:$%02X", command, entry.pcm.flags);
            break;
        case TRACE_PCM_RESET:
            format_event(buffer, size, "DSP", "RESET", "%s", command == 0 ? "Released, 0AAh follows" : command == 2 ?
                "Held, leaves high-speed or MIDI UART mode" : "Held");
            break;
        case TRACE_PCM_READ:
            format_event(buffer, size, "DSP", "READ", "$xxA -> $%02X%s  Queued:%u", command,
                (entry.flags & TRACE_PCM_FLAG_EMPTY) != 0 ? "  EMPTY, last byte again" : "", entry.pcm.queue);
            break;
        case TRACE_PCM_REJECT:
        {
            static const char* const k_reasons[3] = { "In reset", "In high-speed mode", "Not on this DSP version" };
            format_event(buffer, size, "DSP", "REJECT", "$%02X %s  %s", command, dsp_command_name(command),
                k_reasons[MIN(entry.pcm.reason, (u8)2)]);
            break;
        }
        default:
            format_event(buffer, size, "DSP", "CONFIG", "DSP %u.%02u  IRQ %u  DMA %u/%u%s", entry.pcm.version, entry.pcm.minor,
                entry.pcm.irq, entry.pcm.dma, entry.pcm.dma16, command >= 0x80 ? "  from the mixer" : "");
            break;
    }
}

static const char* mixer_register_name(u8 mixer, u8 index)
{
    if (index == 0x00)
        return "RESET";

    if (mixer == 1)
    {
        switch (index)
        {
            case 0x04: return "VOICE";
            case 0x0A: return "MIC";
            case 0x0C: return "INPUT";
            case 0x0E: return "OUTPUT";
            case 0x22: return "MASTER";
            case 0x26: return "FM";
            case 0x28: return "CD";
            case 0x2E: return "LINE";
            default: return "?";
        }
    }

    switch (index)
    {
        case 0x04: return "VOICE";
        case 0x0A: return "MIC";
        case 0x22: return "MASTER";
        case 0x26: return "FM";
        case 0x28: return "CD";
        case 0x2E: return "LINE";
        case 0x30: return "MASTER L";
        case 0x31: return "MASTER R";
        case 0x32: return "VOICE L";
        case 0x33: return "VOICE R";
        case 0x34: return "FM L";
        case 0x35: return "FM R";
        case 0x36: return "CD L";
        case 0x37: return "CD R";
        case 0x38: return "LINE L";
        case 0x39: return "LINE R";
        case 0x3A: return "MIC";
        case 0x3B: return "PC SPEAKER";
        case 0x3C: return "OUTPUT MIX";
        case 0x3D: return "INPUT MIX L";
        case 0x3E: return "INPUT MIX R";
        case 0x3F: return "INPUT GAIN L";
        case 0x40: return "INPUT GAIN R";
        case 0x41: return "OUTPUT GAIN L";
        case 0x42: return "OUTPUT GAIN R";
        case 0x43: return "AGC";
        case 0x44: return "TREBLE L";
        case 0x45: return "TREBLE R";
        case 0x46: return "BASS L";
        case 0x47: return "BASS R";
        case 0x80: return "IRQ SELECT";
        case 0x81: return "DMA SELECT";
        case 0x82: return "IRQ STATUS";
        default: return "?";
    }
}

static void format_mixer(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    const char* name = mixer_register_name(entry.mixer.mixer, entry.mixer.index);
    const char* chip = entry.mixer.mixer == 1 ? "CT1345" : entry.mixer.mixer == 2 ? "CT1745" : "";

    if (entry.event == TRACE_MIXER_READ)
    {
        format_event(buffer, size, "MIXER", "READ", "%s $%02X %-12s -> $%02X", chip, entry.mixer.index, name,
            entry.mixer.value);
        return;
    }

    char kept[24] = "";

    if ((entry.flags & TRACE_ENTRY_IGNORED) != 0)
        snprintf(kept, sizeof(kept), "  Kept:$%02X", entry.mixer.value);

    format_event(buffer, size, "MIXER", entry.event == TRACE_MIXER_VOLUME ? "VOLUME" : "CONTROL", "%s $%02X %-12s <- $%02X%s",
        chip, entry.mixer.index, name, entry.mixer.raw, kept);
}

static const char* mpu_command_name(u8 value)
{
    if (value == 0xFF) return "RESET";
    if (value == 0x3F) return "UART MODE";
    if (value <= 0x2F) return "START/STOP";
    if (value <= 0x3E) return "SWITCH";
    if (value <= 0x7F) return "CHANNEL TABLE";
    if (value <= 0x82) return "SYNC SOURCE";
    if (value <= 0x85) return "METRONOME";
    if (value <= 0x97) return "SWITCH";
    if (value <= 0x9F) return "TABLE ON/OFF";
    if (value <= 0xA7) return "REQUEST PLAY COUNTER";
    if (value == 0xAB) return "REQUEST RECORD COUNTER";
    if (value == 0xAC) return "REQUEST VERSION";
    if (value == 0xAD) return "REQUEST REVISION";
    if (value == 0xAF) return "REQUEST TEMPO";
    if (value == 0xB1) return "RESET RELATIVE TEMPO";
    if (value == 0xB8) return "CLEAR PLAY COUNTERS";
    if (value == 0xB9) return "CLEAR PLAY MAP";
    if (value == 0xBA) return "CLEAR RECORD COUNTER";
    if (value >= 0xC2 && value <= 0xC8) return "TIMEBASE";
    if (value >= 0xD0 && value <= 0xD7) return "WANT TO SEND TRACK";
    if (value == 0xDF) return "WANT TO SEND SYSTEM";
    if ((value & 0xF0) == 0xE0) return "SET PARAMETER";
    return "UNKNOWN";
}

static const char* midi_message_name(const u8* data, int count)
{
    u8 status = data[0];

    if (status == 0xF0)
    {
        if (count >= 2 && data[1] == 0x41) return "SYSEX ROLAND";
        if (count >= 2 && data[1] == 0x43) return "SYSEX YAMAHA";
        if (count >= 2 && (data[1] == 0x7E || data[1] == 0x7F)) return "SYSEX UNIVERSAL";
        return "SYSEX";
    }

    switch (status & 0xF0)
    {
        case 0x80: return "NOTE OFF";
        case 0x90: return count >= 3 && data[2] == 0 ? "NOTE OFF" : "NOTE ON";
        case 0xA0: return "AFTERTOUCH";
        case 0xB0: return "CONTROL";
        case 0xC0: return "PROGRAM";
        case 0xD0: return "PRESSURE";
        case 0xE0: return "PITCH BEND";
        default: return status == 0xF8 ? "CLOCK" : status == 0xFA ? "START" : status == 0xFC ? "STOP" :
            status == 0xFE ? "ACTIVE SENSE" : status == 0xFF ? "RESET" : "SYSTEM";
    }
}

static void format_midi(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    static const char* const k_sources[4] = { "DSP", "MPU", "GUS", "MPU PLAY" };
    static const char* const k_modes[3] = { "", " UART", " INTELLIGENT" };
    static const char* const k_kinds[9] = { "DATA", "ACK", "TRACK REQUEST", "OVERFLOW", "CONDUCTOR REQUEST", "RECORD",
        "MEASURE END", "END", "CLOCK" };
    const char* source = k_sources[MIN(entry.midi.source, (u8)3)];
    const char* mode = k_modes[MIN(entry.midi.mode, (u8)2)];
    u8 value = entry.midi.value;

    switch (entry.event)
    {
        case TRACE_MIDI_OUT:
        {
            char bytes[96];
            int count = MIN((int)entry.midi.count, 24);
            format_bytes(entry.midi.bytes, MIN(count, 24), bytes, sizeof(bytes));

            if (entry.midi.bytes[0] == 0xF0)
                format_event(buffer, size, "MIDI", "OUT", "%s  %s  %u bytes:%s%s", source,
                    midi_message_name(entry.midi.bytes, count), entry.midi.length, bytes, entry.midi.length > 24 ? " ..." : "");
            else if (entry.midi.bytes[0] < 0xF0)
                format_event(buffer, size, "MIDI", "OUT", "%s  CH%u %-10s%s", source, (entry.midi.bytes[0] & 0x0F) + 1,
                    midi_message_name(entry.midi.bytes, count), bytes);
            else
                format_event(buffer, size, "MIDI", "OUT", "%s  %s%s", source, midi_message_name(entry.midi.bytes, count),
                    bytes);
            break;
        }
        case TRACE_MIDI_MESSAGE:
            format_event(buffer, size, "MIDI", "TO HOST", "%s%s  %s  $%02X  Queued:%u", source, mode,
                k_kinds[MIN(entry.midi.kind, (u8)8)], value, entry.midi.count);
            break;
        case TRACE_MIDI_COMMAND:
            format_event(buffer, size, "MIDI", "COMMAND", "%s%s  $%02X  %s", source, mode, value, mpu_command_name(value));
            break;
        case TRACE_MIDI_READ:
            format_event(buffer, size, "MIDI", "READ", "%s%s  -> $%02X%s", source, mode, value,
                entry.midi.kind == 0xFF ? "  EMPTY, last byte again" : "");
            break;
        case TRACE_MIDI_STATUS:
            format_event(buffer, size, "MIDI", "STATUS", "%s%s  $%02X  %s %s", source, mode, value,
                (value & 0x80) == 0 ? "DATA READY" : "NO DATA", (value & 0x40) == 0 ? "READY" : "BUSY");
            break;
        default:
            format_event(buffer, size, "MIDI", "DATA", "%s%s  $%02X", source, mode, value);
            break;
    }
}

static void format_packet_detail(const u8* b, char* buffer, size_t size)
{
    buffer[0] = 0;

    switch (b[0])
    {
        case 0x28:
            snprintf(buffer, size, "  LBA:%u Count:%u", read_u32_be(b + 2), read_u16_be(b + 7));
            break;
        case 0xA8:
            snprintf(buffer, size, "  LBA:%u Count:%u", read_u32_be(b + 2), read_u32_be(b + 6));
            break;
        case 0xBE:
            snprintf(buffer, size, "  LBA:%u Count:%u  Type:%u  Fields:$%02X", read_u32_be(b + 2),
                ((u32)b[6] << 16) | ((u32)b[7] << 8) | b[8], (b[1] >> 2) & 0x07, b[9]);
            break;
        case 0xB9:
            snprintf(buffer, size, "  %02u:%02u:%02u-%02u:%02u:%02u  Fields:$%02X", b[3], b[4], b[5], b[6], b[7], b[8], b[9]);
            break;
        case 0x43:
            snprintf(buffer, size, "  Format:%u %s Track:%u Length:%u", (b[2] & 0x0F) != 0 ? b[2] & 0x0F : b[9] >> 6,
                (b[1] & 0x02) != 0 ? "MSF" : "LBA", b[6], read_u16_be(b + 7));
            break;
        case 0x1A:
        case 0x5A:
            snprintf(buffer, size, "  Page:$%02X Control:%u Length:%u", b[2] & 0x3F, b[2] >> 6,
                b[0] == 0x1A ? b[4] : read_u16_be(b + 7));
            break;
        case 0x15:
        case 0x55:
            snprintf(buffer, size, "  PF:%u Length:%u", (b[1] >> 4) & 0x01, b[0] == 0x15 ? b[4] : read_u16_be(b + 7));
            break;
        case 0x42:
            snprintf(buffer, size, "  %s SubQ:%u Format:%u Track:%u", (b[1] & 0x02) != 0 ? "MSF" : "LBA", (b[2] >> 6) & 0x01,
                b[3], b[6]);
            break;
        case 0x45:
            snprintf(buffer, size, "  LBA:%u Count:%u", read_u32_be(b + 2), read_u16_be(b + 7));
            break;
        case 0xA5:
            snprintf(buffer, size, "  LBA:%u Count:%u", read_u32_be(b + 2), read_u32_be(b + 6));
            break;
        case 0x47:
            snprintf(buffer, size, "  %02u:%02u:%02u-%02u:%02u:%02u", b[3], b[4], b[5], b[6], b[7], b[8]);
            break;
        case 0x4B:
            snprintf(buffer, size, "  %s", (b[8] & 0x01) != 0 ? "RESUME" : "PAUSE");
            break;
        case 0x2B:
            snprintf(buffer, size, "  LBA:%u", read_u32_be(b + 2));
            break;
        case 0x1B:
            snprintf(buffer, size, "  %s%s", (b[4] & 0x02) != 0 ? ((b[4] & 0x01) != 0 ? "LOAD" : "EJECT") : "",
                (b[4] & 0x02) == 0 ? ((b[4] & 0x01) != 0 ? "START" : "STOP") : "");
            break;
        case 0x1E:
            snprintf(buffer, size, "  %s", (b[4] & 0x01) != 0 ? "PREVENT" : "ALLOW");
            break;
        case 0xBB:
            snprintf(buffer, size, "  Read:%u KB/s", read_u16_be(b + 2));
            break;
        case 0x03:
        case 0x12:
            snprintf(buffer, size, "  Length:%u", b[4]);
            break;
        default:
            break;
    }
}

static void format_cdrom(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    char msf[16];
    char end_msf[16];
    const u8* b = entry.cdrom.bytes;
    u32 lba = entry.cdrom.lba;

    format_msf(lba, msf, sizeof(msf));
    format_msf(entry.cdrom.end_lba, end_msf, sizeof(end_msf));

    switch (entry.event)
    {
        case TRACE_CDROM_PACKET:
        case TRACE_CDROM_POLL:
        {
            char detail[96];

            if (entry.event == TRACE_CDROM_POLL && b[5] != 0)
            {
                format_event(buffer, size, "CDROM", "POLL", "%d  $%02X %-16s Status:%02X  Sense:%X/%02X/%02X",
                    entry.cdrom.flags, entry.cdrom.command, GetCdRomPacketName(entry.cdrom.command), entry.cdrom.value,
                    entry.cdrom.sense[0], entry.cdrom.sense[1], entry.cdrom.sense[2]);
                break;
            }

            format_packet_detail(b, detail, sizeof(detail));
            format_event(buffer, size, "CDROM", entry.event == TRACE_CDROM_POLL ? "POLL" : "PACKET",
                "%d  $%02X %-16s %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X%s", entry.cdrom.flags,
                entry.cdrom.command, GetCdRomPacketName(entry.cdrom.command), b[1], b[2], b[3], b[4], b[5], b[6], b[7],
                b[8], b[9], b[10], b[11], detail);
            break;
        }
        case TRACE_CDROM_COMMAND:
            if (entry.cdrom.command == 0xA0)
                format_event(buffer, size, "CDROM", "COMMAND", "%d  $A0 %s  Byte limit:%u%s%s", entry.cdrom.flags,
                    GetCdRomAtaName(entry.cdrom.command), entry.cdrom.limit, (entry.cdrom.features & 0x01) != 0 ?
                    "  DMA, ABORTED" : "", (entry.cdrom.features & 0x02) != 0 ? "  OVERLAP, ABORTED" : "");
            else
                format_event(buffer, size, "CDROM", "COMMAND", "%d  $%02X %s", entry.cdrom.flags, entry.cdrom.command,
                    GetCdRomAtaName(entry.cdrom.command));
            break;
        case TRACE_CDROM_COMPLETE:
            if (b[4] != 0)
                format_event(buffer, size, "CDROM", "COMPLETE", "%d  $%02X %-16s Status:%02X  %s:%X/%02X/%02X",
                    entry.cdrom.flags, entry.cdrom.command, GetCdRomPacketName(entry.cdrom.command), entry.cdrom.value,
                    b[6] != 0 ? "Sent sense" : "Sense", entry.cdrom.sense[0], entry.cdrom.sense[1], entry.cdrom.sense[2]);
            else
                format_event(buffer, size, "CDROM", "COMPLETE", "%d  $%02X %-16s Status:%02X  Error:%02X",
                    entry.cdrom.flags, entry.cdrom.command, GetCdRomAtaName(entry.cdrom.command), entry.cdrom.value,
                    b[3]);
            break;
        case TRACE_CDROM_DATA:
            format_event(buffer, size, "CDROM", "DATA", "%d  LBA:%u Sectors:%u  MSF %s  Bytes:%u", entry.cdrom.flags, lba,
                entry.cdrom.end_lba - lba, msf, entry.cdrom.size);
            break;
        case TRACE_CDROM_MEDIA:
        {
            static const char* const kinds[6] = { "DISC IN", "DISC OUT", "TRAY OPEN", "TRAY CLOSED", "READY", "MOTOR STOP" };
            format_event(buffer, size, "CDROM", "MEDIA", "%d  %s", entry.cdrom.flags, kinds[MIN(entry.cdrom.value, (u8)5)]);
            break;
        }
        case TRACE_CDROM_RESET:
            format_event(buffer, size, "CDROM", "RESET", "%d  Device reset", entry.cdrom.flags);
            break;
        case TRACE_CDROM_CDDA_PLAY:
            format_event(buffer, size, "CDROM", "CDDA PLAY", "Track %u  LBA:%u-%u  MSF %s-%s  Seek:%u ms%s", b[0], lba,
                entry.cdrom.end_lba, msf, end_msf, entry.cdrom.size, (entry.cdrom.flags & 0x01) != 0 ? "  REPEAT" : "");
            break;
        case TRACE_CDROM_CDDA_REJECT:
            format_event(buffer, size, "CDROM", "CDDA PLAY", "REJECTED  LBA:%u-%u  Empty range", lba, entry.cdrom.end_lba);
            break;
        case TRACE_CDROM_CDDA_PAUSE:
            format_event(buffer, size, "CDROM", "CDDA PAUSE", "Track %u  LBA:%u  MSF %s", b[0], lba, msf);
            break;
        case TRACE_CDROM_CDDA_RESUME:
            format_event(buffer, size, "CDROM", "CDDA RESUME", "Track %u  LBA:%u  MSF %s", b[0], lba, msf);
            break;
        case TRACE_CDROM_CDDA_STOP:
        {
            static const char* const k_causes[3] = { "Command", "Disc inserted", "Disc ejected" };
            format_event(buffer, size, "CDROM", "CDDA STOP", "Track %u  LBA:%u  MSF %s  %s", b[0], lba, msf,
                k_causes[MIN(b[1], (u8)2)]);
            break;
        }
        case TRACE_CDROM_CDDA_END:
            format_event(buffer, size, "CDROM", "CDDA END", "Track %u  LBA:%u  MSF %s", b[0], lba, msf);
            break;
        default:
            format_event(buffer, size, "CDROM", "CDDA LOOP", "Back to LBA:%u  MSF %s  End:%u", lba, msf, entry.cdrom.end_lba);
            break;
    }
}

static void format_st_bits(u8 st0, u8 st1, u8 st2, char* buffer, size_t size)
{
    static const char* const k_codes[4] = { "NORMAL", "ABNORMAL", "INVALID", "READY CHANGED" };

    snprintf(buffer, size, "%s%s%s%s%s%s%s%s%s%s%s%s%s", k_codes[st0 >> 6], (st0 & 0x20) != 0 ? " SE" : "",
        (st0 & 0x10) != 0 ? " EC" : "", (st1 & 0x80) != 0 ? " EN" : "", (st1 & 0x20) != 0 ? " DE" : "",
        (st1 & 0x10) != 0 ? " OR" : "", (st1 & 0x04) != 0 ? " ND" : "", (st1 & 0x02) != 0 ? " NW" : "",
        (st1 & 0x01) != 0 ? " MA" : "", (st2 & 0x40) != 0 ? " CM" : "", (st2 & 0x20) != 0 ? " DD" : "",
        (st2 & 0x10) != 0 ? " WC" : "", (st2 & 0x01) != 0 ? " MD" : "");
}

static void format_fdc(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    char drive[8] = "-";
    const u8* b = entry.fdc.bytes;

    if (entry.fdc.drive >= 0)
        snprintf(drive, sizeof(drive), "%d", entry.fdc.drive);

    const char* command = k_debug_fdc_command_names[MIN(entry.fdc.command, (u8)20)];
    u8 c = entry.fdc.command;

    switch (entry.event)
    {
        case TRACE_FDC_COMMAND:
        {
            u8 op = entry.fdc.value;
            const char* rate = k_debug_fdc_rate_names[entry.fdc.rate & 3];

            if (c >= I82077::I82077_COMMAND_READ && c <= I82077::I82077_COMMAND_VERIFY)
                format_event(buffer, size, "FDC", "COMMAND",
                    "$%02X %s%s%s%s  Drive:%s HD:%u C:%u H:%u R:%u N:%u EOT:%u GPL:%u DTL:%u  %s %s", op, command,
                    (op & 0x80) != 0 ? " MT" : "", (op & 0x40) != 0 ? " MFM" : " FM", (op & 0x20) != 0 ? " SK" : "", drive,
                    (b[1] >> 2) & 0x01, b[2], b[3], b[4], b[5], b[6], b[7], b[8], rate, entry.fdc.non_dma ? "PIO" : "DMA");
            else if (c == I82077::I82077_COMMAND_FORMAT)
                format_event(buffer, size, "FDC", "COMMAND", "$%02X %s%s  Drive:%s HD:%u N:%u SC:%u GPL:%u Fill:$%02X  %s %s",
                    op, command, (op & 0x40) != 0 ? " MFM" : " FM", drive, (b[1] >> 2) & 0x01, b[2], b[3], b[4], b[5], rate,
                    entry.fdc.non_dma ? "PIO" : "DMA");
            else if (c == I82077::I82077_COMMAND_SEEK)
                format_event(buffer, size, "FDC", "COMMAND", "$%02X %s  Drive:%s HD:%u To:%u", op, command, drive,
                    (b[1] >> 2) & 0x01, b[2]);
            else if (c == I82077::I82077_COMMAND_RELATIVE_SEEK)
                format_event(buffer, size, "FDC", "COMMAND", "$%02X %s  Drive:%s %s %u steps", op, command, drive,
                    (op & 0x40) != 0 ? "IN" : "OUT", b[2]);
            else if (c == I82077::I82077_COMMAND_SPECIFY)
                format_event(buffer, size, "FDC", "COMMAND", "$%02X %s  SRT:%u HUT:%u HLT:%u %s", op, command, b[1] >> 4,
                    b[1] & 0x0F, b[2] >> 1, (b[2] & 0x01) != 0 ? "PIO" : "DMA");
            else if (c == I82077::I82077_COMMAND_RECALIBRATE || c == I82077::I82077_COMMAND_SENSE_DRIVE ||
                c == I82077::I82077_COMMAND_READ_ID)
                format_event(buffer, size, "FDC", "COMMAND", "$%02X %s  Drive:%s", op, command, drive);
            else
                format_event(buffer, size, "FDC", "COMMAND", "$%02X %s", op, command);
            break;
        }
        case TRACE_FDC_END:
        {
            char bits[96];
            format_st_bits(b[0], b[1], b[2], bits, sizeof(bits));
            format_event(buffer, size, "FDC", "END", "%s  ST0:$%02X ST1:$%02X ST2:$%02X %s  Drive:%s C:%u H:%u R:%u N:%u",
                command, b[0], b[1], b[2], bits, drive, b[3], b[4], b[5], b[6]);
            break;
        }
        case TRACE_FDC_RESULT:
        {
            char bytes[48];
            format_bytes(b, MIN(entry.fdc.count, (u8)10), bytes, sizeof(bytes));

            if (c == I82077::I82077_COMMAND_SENSE_INTERRUPT && entry.fdc.count >= 2)
                format_event(buffer, size, "FDC", "RESULT", "%s  ST0:$%02X %s Drive:%u PCN:%u", command, b[0],
                    (b[0] >> 6) == 0 ? "NORMAL" : (b[0] >> 6) == 3 ? "READY CHANGED" : (b[0] >> 6) == 2 ? "INVALID" :
                    "ABNORMAL", b[0] & 0x03, b[1]);
            else if (c == I82077::I82077_COMMAND_SENSE_DRIVE)
                format_event(buffer, size, "FDC", "RESULT", "%s  ST3:$%02X%s%s%s%s", command, b[0],
                    (b[0] & 0x40) != 0 ? " WP" : "", (b[0] & 0x10) != 0 ? " TRACK0" : "", (b[0] & 0x04) != 0 ? " HEAD1" : "",
                    (b[0] & 0x20) != 0 ? " READY" : "");
            else
                format_event(buffer, size, "FDC", "RESULT", "%s ->%s", command, bytes);
            break;
        }
        case TRACE_FDC_SEEK_END:
            format_event(buffer, size, "FDC", "SEEK END", "Drive:%s ST0:$%02X%s%s PCN:%u", drive, entry.fdc.status,
                (entry.fdc.status & 0x20) != 0 ? " SE" : "", (entry.fdc.status & 0x10) != 0 ? " EC" : "", entry.fdc.cylinder);
            break;
        case TRACE_FDC_RATE:
            format_event(buffer, size, "FDC", "RATE", "%s <- $%02X  %s", entry.fdc.kind == 7 ? "CCR" : "DSR", entry.fdc.value,
                k_debug_fdc_rate_names[entry.fdc.rate & 3]);
            break;
        case TRACE_FDC_SECTOR:
            format_event(buffer, size, "FDC", "SECTOR", "%s  Drive:%s  Found C:%u H:%u R:%u N:%u  %u bytes%s%s%s", command,
                drive, entry.fdc.cylinder, entry.fdc.track, entry.fdc.sector, entry.fdc.data, entry.fdc.moved,
                (entry.fdc.kind & 0x01) != 0 ? "  DELETED" : "", (entry.fdc.kind & 0x02) != 0 ? "  FM" : "",
                entry.fdc.status != 0 ? "  CRC ERROR" : "");
            break;
        case TRACE_FDC_MEDIA:
            format_event(buffer, size, "FDC", "MEDIA", "Drive:%s  %s", drive, entry.fdc.kind == 0 ?
                (entry.fdc.value != 0 ? "DISK IN, change line set" : "DISK OUT, change line set") : "Change line cleared");
            break;
        case TRACE_FDC_TC:
            format_event(buffer, size, "FDC", "TC", "%s  Drive:%s  Terminal count at R:%u", command, drive, entry.fdc.sector);
            break;
        case TRACE_FDC_DRIVE_CONTROL:
            format_event(buffer, size, "FDC", "DOR", "$%02X  Select:%u%s%s  Motors:%X", entry.fdc.value, entry.fdc.value & 0x03,
                (entry.fdc.value & 0x04) == 0 ? "  RESET" : "", (entry.fdc.value & 0x08) != 0 ? "  DMA/IRQ" : "",
                entry.fdc.value >> 4);
            break;
        default:
            format_event(buffer, size, "FDC", "DRIVE SEL", "DOR:$%02X  Select:%u Drive:%s  Motors:%X  Cylinder:%u",
                entry.fdc.value, entry.fdc.value & 0x03, drive, entry.fdc.value >> 4, entry.fdc.cylinder);
            break;
    }
}

struct Trace_File_Map
{
    std::string path;
    u64 blocks;
    bool built;
    std::vector<FatImage_Extent> extents;
    std::vector<std::string> names;
};

static Trace_File_Map s_trace_file_maps[GDOX_MAX_HARD_DISKS];

static bool compare_extent(const FatImage_Extent& a, const FatImage_Extent& b)
{
    return a.first < b.first;
}

void trace_log_reset_file_names(void)
{
    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        s_trace_file_maps[i].built = false;
        s_trace_file_maps[i].extents.clear();
        s_trace_file_maps[i].names.clear();
    }
}

static Trace_File_Map* get_file_map(int id)
{
    if (id < 0 || id >= GDOX_MAX_HARD_DISKS)
        return NULL;

    HardDiskImage* image = emu_get_core()->GetIDE()->GetDisk(id)->GetImage();

    if (!image->IsOpen())
        return NULL;

    Trace_File_Map& map = s_trace_file_maps[id];

    if (map.built && map.path == image->GetPath() && map.blocks == image->GetBlocks())
        return map.extents.empty() ? NULL : &map;

    map.built = true;
    map.path = image->GetPath();
    map.blocks = image->GetBlocks();
    map.extents.clear();
    map.names.clear();

    FatImage reader;
    MediaFile* file = MediaFile::OpenFile(map.path.c_str());

    if (IsValidPointer(file) && reader.Read(file))
    {
        reader.GetSectorMap(&map.extents);
        const std::vector<FatImage_File>& files = reader.GetFiles();

        for (size_t i = 0; i < files.size(); i++)
            map.names.push_back(files[i].path);

        std::sort(map.extents.begin(), map.extents.end(), compare_extent);
    }

    reader.CloseRead();
    SafeDelete(file);
    return map.extents.empty() ? NULL : &map;
}

static void format_file_name(int id, u32 lba, char* buffer, size_t size)
{
    static const char* const k_areas[3] = { "MBR", "BOOT AND FAT", "ROOT DIRECTORY" };
    buffer[0] = 0;
    Trace_File_Map* map = get_file_map(id);

    if (!IsValidPointer(map))
        return;

    size_t low = 0;
    size_t high = map->extents.size();

    while (low < high)
    {
        size_t middle = (low + high) / 2;

        if (map->extents[middle].last < lba)
            low = middle + 1;
        else
            high = middle;
    }

    if (low >= map->extents.size() || map->extents[low].first > lba)
    {
        snprintf(buffer, size, "  FREE OR NEW");
        return;
    }

    const FatImage_Extent& extent = map->extents[low];

    if (extent.file < 0)
    {
        snprintf(buffer, size, "  %s", k_areas[MIN(-extent.file - 1, 2)]);
        return;
    }

    u64 offset = 0;

    for (size_t i = 0; i < low; i++)
    {
        if (map->extents[i].file == extent.file)
            offset += (u64)(map->extents[i].last - map->extents[i].first + 1) * 512;
    }

    offset += (u64)(lba - extent.first) * 512;
    std::string name = path_back_slashes(map->names[(size_t)extent.file]);
    snprintf(buffer, size, "  %c:\\%s +%llXh", id == 0 ? 'C' : 'D', name.c_str(), (unsigned long long)offset);
}

static void format_ide_bits(u8 status, u8 error, char* buffer, size_t size)
{
    snprintf(buffer, size, "%s%s%s%s%s%s%s%s%s%s%s%s%s%s", (status & 0x80) != 0 ? " BSY" : "", (status & 0x40) != 0 ? " DRDY" : "",
        (status & 0x20) != 0 ? " DF" : "", (status & 0x10) != 0 ? " DSC" : "", (status & 0x08) != 0 ? " DRQ" : "",
        (status & 0x04) != 0 ? " CORR" : "", (status & 0x01) != 0 ? " ERR" : "", (error & 0x80) != 0 ? " BBK" : "",
        (error & 0x40) != 0 ? " UNC" : "", (error & 0x10) != 0 ? " IDNF" : "", (error & 0x04) != 0 ? " ABRT" : "",
        (error & 0x02) != 0 ? " TK0NF" : "", (error & 0x01) != 0 ? " AMNF" : "", (error & 0x20) != 0 ? " MC" : "");
}

static void format_ide(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    const char* command = gui_debug_ide_command_name(entry.ide.command);
    u8 c = entry.ide.command;
    char address[48];
    char detail[64] = "";
    u32 count = entry.ide.count == 0 ? 256 : entry.ide.count;

    u32 lba = ((u32)(entry.ide.head & 0x0F) << 24) | ((u32)entry.ide.cylinder << 8) | entry.ide.sector;
    char file[160] = "";

    if ((entry.ide.head & 0x40) != 0)
        snprintf(address, sizeof(address), "LBA:%u", lba);
    else
    {
        snprintf(address, sizeof(address), "C:%u H:%u S:%u", entry.ide.cylinder, entry.ide.head & 0x0F, entry.ide.sector);
        const IdeDisk::IdeDisk_State* disk = emu_get_core()->GetIDE()->GetDisk(MIN(entry.ide.id, (u8)3))->GetState();
        lba = disk->current_heads != 0 && disk->current_sectors != 0 && entry.ide.sector != 0 ?
            ((u32)entry.ide.cylinder * disk->current_heads + (entry.ide.head & 0x0F)) * disk->current_sectors +
            entry.ide.sector - 1 : 0xFFFFFFFFU;
    }

    bool data = c == 0x20 || c == 0x21 || c == 0x30 || c == 0x31 || c == 0xC4 || c == 0xC5 || c == 0xC8 || c == 0xCA ||
        c == 0x40 || c == 0x41;

    if (data && lba != 0xFFFFFFFFU && entry.event == TRACE_IDE_COMMAND)
        format_file_name(entry.ide.id, lba, file, sizeof(file));

    if (c == 0x91)
        snprintf(detail, sizeof(detail), "Heads:%u Sectors per track:%u", (entry.ide.head & 0x0F) + 1, entry.ide.count);
    else if (c == 0xEF)
        snprintf(detail, sizeof(detail), "Feature:$%02X%s Count:$%02X", entry.ide.features,
            entry.ide.features == 0x03 ? " TRANSFER MODE" : entry.ide.features == 0x02 ? " WRITE CACHE ON" :
            entry.ide.features == 0x82 ? " WRITE CACHE OFF" : "", entry.ide.count);
    else if (c == 0xC6)
        snprintf(detail, sizeof(detail), "Block:%u sectors", entry.ide.count);
    else if (c == 0xEC || c == 0x90 || c == 0xE5 || c == 0xE7 || (c >= 0xE0 && c <= 0xE3) || (c & 0xF0) == 0x10)
        detail[0] = 0;
    else if ((c & 0xF0) == 0x70)
        snprintf(detail, sizeof(detail), "%s", address);
    else
        snprintf(detail, sizeof(detail), "%s Count:%u", address, count);

    switch (entry.event)
    {
        case TRACE_IDE_COMMAND:
            format_event(buffer, size, "IDE", "COMMAND", "$%02X %s  ID %u  %s%s", c, command, entry.ide.id, detail, file);
            break;
        case TRACE_IDE_IGNORED:
            format_event(buffer, size, "IDE", "IGNORED", "$%02X %s  ID %u  No device", c, command, entry.ide.id);
            break;
        case TRACE_IDE_END:
        {
            char bits[96];
            format_ide_bits(entry.ide.status, entry.ide.error, bits, sizeof(bits));
            format_event(buffer, size, "IDE", "END", "%s  ID %u  Status:$%02X Error:$%02X%s  %s Left:%u", command,
                entry.ide.id, entry.ide.status, entry.ide.error, bits, address, entry.ide.count);
            break;
        }
        case TRACE_IDE_RESET_RELEASE:
            format_event(buffer, size, "IDE", "RESET", "ID %u  Released", entry.ide.id);
            break;
        default:
            format_event(buffer, size, "IDE", "RESET", "ID %u  Held", entry.ide.id);
            break;
    }
}

static const char* keyboard_command_name(u8 value, u8 target)
{
    if (target == TRACE_KEYBOARD_TARGET_KEYBOARD)
    {
        switch (value)
        {
            case 0xED: return "SET LEDS";
            case 0xEE: return "ECHO";
            case 0xF0: return "SCAN CODE SET";
            case 0xF2: return "IDENTIFY";
            case 0xF3: return "TYPEMATIC";
            case 0xF4: return "ENABLE";
            case 0xF5: return "DISABLE";
            case 0xF6: return "DEFAULTS";
            case 0xF7: case 0xF8: case 0xF9: case 0xFA: return "SET ALL KEYS";
            case 0xFB: case 0xFC: case 0xFD: return "SET KEY TYPE";
            case 0xFE: return "RESEND";
            case 0xFF: return "RESET";
            default: return "UNKNOWN";
        }
    }

    if ((value & 0xE0) == 0x20)
        return value == 0x20 ? "READ COMMAND BYTE" : "READ RAM";

    if ((value & 0xE0) == 0x60)
        return value == 0x60 ? "WRITE COMMAND BYTE" : "WRITE RAM";

    if ((value & 0xF0) == 0xF0)
        return (value & 0x01) == 0 ? "PULSE RESET" : "PULSE";

    switch (value)
    {
        case 0xA7: return "AUX DISABLE";
        case 0xA8: return "AUX ENABLE";
        case 0xA9: return "AUX TEST";
        case 0xAA: return "SELF TEST";
        case 0xAB: return "KEYBOARD TEST";
        case 0xAD: return "KEYBOARD DISABLE";
        case 0xAE: return "KEYBOARD ENABLE";
        case 0xC0: return "READ INPUT PORT";
        case 0xD0: return "READ OUTPUT PORT";
        case 0xD1: return "WRITE OUTPUT PORT";
        case 0xD2: return "WRITE KEYBOARD BUFFER";
        case 0xD3: return "WRITE AUX BUFFER";
        case 0xD4: return "WRITE AUX";
        case 0xE0: return "READ TEST INPUTS";
        default: return "UNHANDLED";
    }
}

static void format_command_byte(u8 value, char* buffer, size_t size)
{
    snprintf(buffer, size, "KBD IRQ:%u AUX IRQ:%u SYS:%u KBD:%s AUX:%s XLAT:%u", value & 0x01, (value >> 1) & 0x01,
        (value >> 2) & 0x01, (value & 0x10) != 0 ? "OFF" : "ON", (value & 0x20) != 0 ? "OFF" : "ON", (value >> 6) & 0x01);
}

static void format_keyboard_parameter(u8 command, u8 value, char* buffer, size_t size)
{
    buffer[0] = 0;

    switch (command)
    {
        case 0xED:
            snprintf(buffer, size, "  Scroll:%u Num:%u Caps:%u", value & 0x01, (value >> 1) & 0x01, (value >> 2) & 0x01);
            break;
        case 0xF3:
        {
            double period = (8 + (value & 0x07)) * (double)(1 << ((value >> 3) & 0x03)) * 4.17;
            snprintf(buffer, size, "  Delay:%u ms Rate:%.1f/s", (((value >> 5) & 0x03) + 1) * 250, 1000.0 / period);
            break;
        }
        case 0xF0:
            snprintf(buffer, size, value == 0 ? "  Query" : "  Set %u", value);
            break;
        default:
            break;
    }
}

static const char* keyboard_reply_name(u8 value)
{
    switch (value)
    {
        case 0xFA: return "  ACK";
        case 0xFE: return "  RESEND";
        case 0xAA: return "  SELF TEST OK";
        case 0xEE: return "  ECHO";
        case 0xFC: return "  SELF TEST FAILED";
        case 0x00: case 0xFF: return "  OVERRUN";
        default: return "";
    }
}

static void format_keyboard(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    u8 value = entry.keyboard.value;
    u8 flags = entry.keyboard.flags;
    const char* source = (entry.flags & TRACE_KEYBOARD_FLAG_AUX) != 0 ? "AUX" : "KBD";

    switch (entry.event)
    {
        case TRACE_KEYBOARD_KEY:
        {
            const char* name = gui_debug_key_name(entry.keyboard.key);
            const char* kind = flags == 0x01 ? "MAKE  " : flags == 0x02 ? "REPEAT" : "BREAK ";
            char bytes[32];
            format_bytes(entry.keyboard.bytes, MIN(entry.keyboard.count, (u8)8), bytes, sizeof(bytes));

            format_event(buffer, size, "KEYBRD", "KEY", "%s %s  Code:$%02X  Set %u:%s  Queued:%u", kind,
                IsValidPointer(name) ? name : "?", entry.keyboard.key, entry.keyboard.scan_set, bytes,
                entry.keyboard.pending);
            break;
        }
        case TRACE_KEYBOARD_REJECT:
        {
            const char* name = gui_debug_key_name(entry.keyboard.key);
            format_event(buffer, size, "KEYBRD", "REJECT", "%s  Code:$%02X  %s  Queued:%u", IsValidPointer(name) ? name : "?",
                entry.keyboard.key, entry.keyboard.reason == 0 ? "Keyboard not scanning" : "Buffer overrun",
                entry.keyboard.pending);
            break;
        }
        case TRACE_KEYBOARD_READ:
        case TRACE_KEYBOARD_READ_EMPTY:
            format_event(buffer, size, "KEYBRD", "READ", "$0060 -> $%02X  %s%s  Queued:%u", value, source,
                entry.event == TRACE_KEYBOARD_READ_EMPTY ? "  EMPTY, last byte again" : "", entry.keyboard.pending);
            break;
        case TRACE_KEYBOARD_BYTE:
            format_event(buffer, size, "KEYBRD", "BYTE", "%s <- $%02X%s%s%s", source, value,
                (entry.flags & TRACE_KEYBOARD_FLAG_AUX) != 0 ? "" : keyboard_reply_name(value),
                (entry.flags & TRACE_KEYBOARD_FLAG_LOST) != 0 ? "  LOST, replaced before the CPU read it" : "",
                (entry.flags & TRACE_KEYBOARD_FLAG_TIMEOUT) != 0 ? "  TIMEOUT" : "");
            break;
        case TRACE_KEYBOARD_COMMAND:
        {
            char detail[96] = "";

            if (flags == TRACE_KEYBOARD_TARGET_8042)
            {
                format_event(buffer, size, "KEYBRD", "COMMAND", "8042 CMD $%02X  %s", value,
                    keyboard_command_name(value, flags));
                break;
            }

            if (flags == TRACE_KEYBOARD_TARGET_8042_PARAMETER)
            {
                if (entry.keyboard.key == 0x60)
                {
                    format_command_byte(value, detail, sizeof(detail));
                    format_event(buffer, size, "KEYBRD", "COMMAND", "8042 COMMAND BYTE <- $%02X  %s", value, detail);
                }
                else
                    format_event(buffer, size, "KEYBRD", "COMMAND", "8042 PARAM $%02X  %s", value,
                        keyboard_command_name(entry.keyboard.key, TRACE_KEYBOARD_TARGET_8042));
                break;
            }

            if (entry.keyboard.key != 0 && (entry.flags & TRACE_KEYBOARD_FLAG_AS_COMMAND) == 0)
            {
                format_keyboard_parameter(entry.keyboard.key, value, detail, sizeof(detail));
                format_event(buffer, size, "KEYBRD", "COMMAND", "KEYBOARD $%02X  %s parameter%s", value,
                    keyboard_command_name(entry.keyboard.key, flags), detail);
            }
            else if (entry.keyboard.key != 0)
                format_event(buffer, size, "KEYBRD", "COMMAND", "KEYBOARD $%02X  %s  Ends %s, taken as a command",
                    value, keyboard_command_name(value, flags), keyboard_command_name(entry.keyboard.key, flags));
            else
                format_event(buffer, size, "KEYBRD", "COMMAND", "KEYBOARD $%02X  %s", value,
                    keyboard_command_name(value, flags));
            break;
        }
        default:
            format_event(buffer, size, "KEYBRD", "PORT", "Output $%02X -> $%02X  A20:%s%s", entry.keyboard.key, value,
                (value & 0x02) != 0 ? "ON " : "OFF", (value & 0x01) == 0 ? "  RESET" : "");
            break;
    }
}

static const char* ps2_mouse_command_name(u8 value)
{
    switch (value)
    {
        case 0xE6: return "SCALING 1:1";
        case 0xE7: return "SCALING 2:1";
        case 0xE8: return "SET RESOLUTION";
        case 0xE9: return "STATUS REQUEST";
        case 0xEA: return "SET STREAM MODE";
        case 0xEB: return "READ DATA";
        case 0xEC: return "RESET WRAP MODE";
        case 0xEE: return "SET WRAP MODE";
        case 0xF0: return "SET REMOTE MODE";
        case 0xF2: return "GET DEVICE ID";
        case 0xF3: return "SET SAMPLE RATE";
        case 0xF4: return "ENABLE REPORTING";
        case 0xF5: return "DISABLE REPORTING";
        case 0xF6: return "SET DEFAULTS";
        case 0xFE: return "RESEND";
        case 0xFF: return "RESET";
        default: return "UNKNOWN";
    }
}

static void format_ps2_packet(const u8* data, char* buffer, size_t size)
{
    u8 status = data[0];
    int dx = (status & 0x10) != 0 ? (int)data[1] - 256 : (int)data[1];
    int dy = (status & 0x20) != 0 ? (int)data[2] - 256 : (int)data[2];

    snprintf(buffer, size, "  %s%s%s dx:%d dy:%d%s%s", (status & 0x01) != 0 ? "L" : "-", (status & 0x04) != 0 ? "M" : "-",
        (status & 0x02) != 0 ? "R" : "-", dx, dy, (status & 0x40) != 0 ? "  X OVERFLOW" : "",
        (status & 0x80) != 0 ? "  Y OVERFLOW" : "");
}

static void format_serial_packet(const u8* data, char* buffer, size_t size)
{
    int dx = (s8)(((data[0] & 0x03) << 6) | (data[1] & 0x3F));
    int dy = (s8)(((data[0] & 0x0C) << 4) | (data[2] & 0x3F));

    snprintf(buffer, size, "  %s%s dx:%d dy:%d", (data[0] & 0x20) != 0 ? "L" : "-", (data[0] & 0x10) != 0 ? "R" : "-",
        dx, dy);
}

static void format_input(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    char bytes[40];
    int count = MIN((int)entry.input.count, 8);

    switch (entry.event)
    {
        case TRACE_INPUT_MOUSE_COMMAND:
        {
            format_bytes(entry.input.data + 1, count - 1, bytes, sizeof(bytes));

            if ((entry.flags & TRACE_INPUT_FLAG_WRAP) != 0)
                format_event(buffer, size, "INPUT", "MOUSE CMD", "PS/2 <- $%02X  WRAP echo:%s", entry.input.data[0], bytes);
            else if ((entry.flags & TRACE_INPUT_FLAG_PARAMETER) != 0)
                format_event(buffer, size, "INPUT", "MOUSE CMD", "PS/2 <- $%02X  %s parameter%s  Reply:%s",
                    entry.input.data[0], ps2_mouse_command_name(entry.input.command),
                    (entry.flags & TRACE_INPUT_FLAG_REJECTED) != 0 ? "  REJECTED" : "", bytes);
            else
                format_event(buffer, size, "INPUT", "MOUSE CMD", "PS/2 <- $%02X  %s%s  Reply:%s", entry.input.data[0],
                    ps2_mouse_command_name(entry.input.data[0]),
                    (entry.flags & TRACE_INPUT_FLAG_REJECTED) != 0 ? "  REJECTED" : "", bytes);
            break;
        }
        case TRACE_INPUT_MOUSE_PACKET:
        {
            char decoded[64] = "";
            bool serial = entry.input.device == TRACE_INPUT_DEVICE_SERIAL_MOUSE;
            format_bytes(entry.input.data, count, bytes, sizeof(bytes));

            if ((entry.flags & TRACE_INPUT_FLAG_IDENT) != 0)
                snprintf(decoded, sizeof(decoded), "  IDENT '%c'", entry.input.data[0]);
            else if (count >= 3 && serial)
                format_serial_packet(entry.input.data, decoded, sizeof(decoded));
            else if (count >= 3)
                format_ps2_packet(entry.input.data, decoded, sizeof(decoded));

            format_event(buffer, size, "INPUT", "MOUSE PKT", "%s ->%s%s", serial ? "SERIAL" : "PS/2", bytes, decoded);
            break;
        }
        case TRACE_INPUT_MOUSE_RESET:
            format_event(buffer, size, "INPUT", "MOUSE RTS", "SERIAL RTS %s", entry.input.data[0] != 0 ?
                "UP, ident follows" : "DOWN, mouse silent");
            break;
        case TRACE_INPUT_JOYSTICK_FIRE:
        {
            char axes[GDOX_JOYSTICK_AXIS_COUNT][20];

            for (int i = 0; i < GDOX_JOYSTICK_AXIS_COUNT; i++)
            {
                if (entry.input.data[i] == 0)
                    snprintf(axes[i], sizeof(axes[i]), "OPEN");
                else
                    snprintf(axes[i], sizeof(axes[i]), "%uus(%d)", entry.input.data[i] * 5, entry.input.positions[i]);
            }

            format_event(buffer, size, "INPUT", "JOY FIRE", "$0201  A-X:%s  A-Y:%s  B-X:%s  B-Y:%s  Switches:%X", axes[0],
                axes[1], axes[2], axes[3], (entry.input.switches >> 4) & 0x0F);
            break;
        }
        default:
        {
            char pressed[96];
            char released[96];
            u16 buttons = entry.input.buttons;
            u16 previous = entry.input.previous;

            format_buttons(entry.input.device, buttons & ~previous, pressed, sizeof(pressed));
            format_buttons(entry.input.device, previous & ~buttons, released, sizeof(released));
            format_event(buffer, size, "INPUT", "CHANGE", "%-8s Pressed:%s  Released:%s",
                entry.input.device == TRACE_INPUT_DEVICE_MOUSE ? "MOUSE" : "JOYSTICK", pressed, released);
            break;
        }
    }
}

static void format_serial_config(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    static const char k_parity[8] = { 'N', 'O', 'N', 'E', 'N', 'M', 'N', 'S' };
    static const int k_triggers[4] = { 1, 4, 8, 14 };
    u8 lcr = entry.serial.lcr;
    u32 divisor = entry.serial.divisor != 0 ? entry.serial.divisor : 65536;
    u8 parity = (lcr & 0x08) != 0 ? (u8)((lcr >> 3) & 0x07) : 0;
    char fifo[24] = "FIFO off";

    if ((entry.serial.fcr & 0x01) != 0)
        snprintf(fifo, sizeof(fifo), "FIFO trigger %d", k_triggers[(entry.serial.fcr >> 6) & 0x03]);

    snprintf(buffer, size, "%u baud %u%c%s  %s  IER:$%02X  MCR:$%02X%s", 115200 / divisor, (lcr & 0x03) + 5,
        k_parity[parity], (lcr & 0x04) != 0 ? ((lcr & 0x03) == 0 ? "1.5" : "2") : "1", fifo, entry.serial.ier,
        entry.serial.mcr, (entry.serial.mcr & 0x10) != 0 ? " LOOPBACK" : "");
}

static void format_serial(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    static const char* const k_dlab_registers[2] = { "DLL", "DLM" };
    int port = entry.serial.port + 1;
    u8 value = entry.serial.value;
    char lsr[48];

    snprintf(lsr, sizeof(lsr), "LSR:$%02X%s%s%s%s", entry.serial.lsr, (entry.serial.lsr & 0x02) != 0 ? " OVERRUN" : "",
        (entry.serial.lsr & 0x04) != 0 ? " PARITY" : "", (entry.serial.lsr & 0x08) != 0 ? " FRAMING" : "",
        (entry.serial.lsr & 0x10) != 0 ? " BREAK" : "");

    switch (entry.event)
    {
        case TRACE_SERIAL_WRITE:
        {
            u8 reg = entry.serial.reg & 0x07;
            const char* name = (entry.flags & TRACE_SERIAL_FLAG_DLAB) != 0 && reg < 2 ? k_dlab_registers[reg] :
                k_trace_uart_registers[reg];
            format_event(buffer, size, "SERIAL", "WRITE", "COM%d %s <- $%02X", port, name, value);
            break;
        }
        case TRACE_SERIAL_CONFIG:
        {
            char config[96];
            format_serial_config(entry, config, sizeof(config));
            format_event(buffer, size, "SERIAL", "CONFIG", "COM%d %s", port, config);
            break;
        }
        case TRACE_SERIAL_RECEIVE:
            format_event(buffer, size, "SERIAL", "RECEIVE", "COM%d <- $%02X  %s", port, value, lsr);
            break;
        case TRACE_SERIAL_READ:
            format_event(buffer, size, "SERIAL", "READ", "COM%d RBR -> $%02X '%c'  %s", port, value,
                printable_char(value), lsr);
            break;
        case TRACE_SERIAL_TRANSMIT:
            format_event(buffer, size, "SERIAL", "TRANSMIT", "COM%d -> $%02X '%c'", port, value,
                printable_char(value));
            break;
        case TRACE_SERIAL_MODEM:
            format_event(buffer, size, "SERIAL", "MODEM", "COM%d DTR:%u RTS:%u OUT1:%u OUT2:%u  MSR:$%02X", port,
                value & 0x01, (value >> 1) & 0x01, (value >> 2) & 0x01, (value >> 3) & 0x01, entry.serial.msr);
            break;
        default:
        {
            static const char* const k_sources[8] = { "MODEM STATUS", "NONE", "THR EMPTY", "NONE", "DATA", "NONE",
                "LINE STATUS", "NONE" };
            u8 iir = entry.serial.iir;
            format_event(buffer, size, "SERIAL", "IRQ", "COM%d IRQ %d %s  IIR:$%02X %s%s", port,
                entry.serial.port == 0 ? 4 : 3, entry.serial.flags != 0 ? "ON " : "OFF", iir,
                (iir & 0x08) != 0 ? "TIMEOUT" : k_sources[iir & 0x07], (iir & 0xC0) != 0 ? " FIFO" : "");
            break;
        }
    }
}

static void format_parallel(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    u8 value = entry.parallel.value;

    switch (entry.event)
    {
        case TRACE_PARALLEL_DATA:
            format_event(buffer, size, "LPT", "DATA", "$%03X <- $%02X '%c'", entry.parallel.port, value,
                printable_char(value));
            break;
        case TRACE_PARALLEL_STROBE:
            format_event(buffer, size, "LPT", "STROBE", "$%03X  Byte:$%02X '%c'", entry.parallel.port, value,
                printable_char(value));
            break;
        default:
            format_event(buffer, size, "LPT", "CONTROL", "$%03X <- $%02X  STROBE:%u AUTOFEED:%u INIT:%u SELECT:%u IRQ:%u",
                entry.parallel.port, value, value & 0x01, (value >> 1) & 0x01, (value >> 2) & 0x01 ? 0 : 1,
                (value >> 3) & 0x01, (value >> 4) & 0x01);
            break;
    }
}

static const char* rtc_register_name(u8 index)
{
    static const char* const k_names[14] = { "SECONDS", "SEC ALARM", "MINUTES", "MIN ALARM", "HOURS", "HOUR ALARM",
        "WEEKDAY", "DAY", "MONTH", "YEAR", "STATUS A", "STATUS B", "STATUS C", "STATUS D" };

    if (index < 14)
        return k_names[index];

    switch (index)
    {
        case 0x0E: return "DIAGNOSTIC";
        case 0x0F: return "SHUTDOWN";
        case 0x10: return "FLOPPY TYPES";
        case 0x12: return "DISK TYPES";
        case 0x14: return "EQUIPMENT";
        case 0x15: return "BASE MEM LO";
        case 0x16: return "BASE MEM HI";
        case 0x17: return "EXT MEM LO";
        case 0x18: return "EXT MEM HI";
        case 0x2E: return "CHECKSUM HI";
        case 0x2F: return "CHECKSUM LO";
        case 0x30: return "EXT MEM LO";
        case 0x31: return "EXT MEM HI";
        case 0x32: return "CENTURY";
        default: return "CMOS";
    }
}

static void format_rtc_detail(u8 index, u8 value, char* buffer, size_t size)
{
    buffer[0] = 0;

    if (index == 0x0A)
    {
        u8 rate = value & 0x0F;

        if (rate == 0)
            snprintf(buffer, size, "  Divider:%u Rate:off", (value >> 4) & 0x07);
        else
            snprintf(buffer, size, "  Divider:%u Rate:%u Hz", (value >> 4) & 0x07, 32768U >> (rate - 1));
    }
    else if (index == 0x0B)
        snprintf(buffer, size, "  %s%s%s%s%s%s%s", (value & 0x80) != 0 ? "SET " : "", (value & 0x40) != 0 ? "PIE " : "",
            (value & 0x20) != 0 ? "AIE " : "", (value & 0x10) != 0 ? "UIE " : "", (value & 0x08) != 0 ? "SQWE " : "",
            (value & 0x04) != 0 ? "BINARY " : "BCD ", (value & 0x02) != 0 ? "24H" : "12H");
    else if (index == 0x0C)
        snprintf(buffer, size, "  %s%s%s%s", (value & 0x80) != 0 ? "IRQF " : "", (value & 0x40) != 0 ? "PERIODIC " : "",
            (value & 0x20) != 0 ? "ALARM " : "", (value & 0x10) != 0 ? "UPDATE" : "");
}

static void format_pages(u64 pages, char* buffer, size_t size)
{
    size_t length = 0;
    buffer[0] = 0;

    for (int page = 0; page < 64 && length + 16 < size; page++)
    {
        if (((pages >> page) & 1) == 0)
            continue;

        int last = page;

        while (last + 1 < 64 && ((pages >> (last + 1)) & 1) != 0)
            last++;

        int written = snprintf(buffer + length, size - length, "%s%05X-%05X", length == 0 ? "" : ",",
            0xC0000 + page * 0x1000, 0xC0000 + last * 0x1000 + 0xFFF);

        if (written > 0)
            length += (size_t)written;

        page = last;
    }

    if (length == 0)
        snprintf(buffer, size, "NONE");
}

static void format_system(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    u8 value = entry.system.value;
    u8 flags = entry.system.flags;

    switch (entry.event)
    {
        case TRACE_SYSTEM_RESET:
        {
            char port[16] = "";

            if (entry.system.port != 0)
                snprintf(port, sizeof(port), " PORT %Xh", entry.system.port);

            format_event(buffer, size, "SYSTEM", (entry.flags & TRACE_SYSTEM_FLAG_PERFORMED) != 0 ? "RESET DONE" :
                "CPU RESET", "%s%s%s%s  Count:%u", port, (flags & 0x01) != 0 && entry.system.port == 0 ? " FAST" : "",
                (flags & 0x02) != 0 ? " SHUTDOWN" : "", (flags & 0x04) != 0 ? " KEYBOARD CONTROLLER" : "", value);
            break;
        }
        case TRACE_SYSTEM_MEMORY_MAP:
        {
            if (entry.system.kind == TRACE_SYSTEM_MAP_A20)
            {
                static const char* const k_sources[5] = { "PORT 92h", "8042 OUTPUT PORT", "CPU RESET", "POWER ON",
                    "STATE LOAD" };
                format_event(buffer, size, "SYSTEM", "A20", "%s  By:%s  Gates: 92h:%u 8042:%u reset:%u",
                    value != 0 ? "ON" : "OFF", k_sources[MIN(entry.system.source, (u8)4)], flags & 0x01, (flags >> 1) & 0x01,
                    (flags >> 2) & 0x01);
                break;
            }

            char read_only[96];
            char open[96];
            format_pages(entry.system.read_only_pages, read_only, sizeof(read_only));
            format_pages(entry.system.open_pages, open, sizeof(open));

            if (entry.system.kind == TRACE_SYSTEM_MAP_SHADOW)
                format_event(buffer, size, "SYSTEM", "SHADOW", "Read only:%s  Upper RAM:%s%s", read_only, open,
                    value != 0 ? "  LOCKED" : "");
            else
                format_event(buffer, size, "SYSTEM", entry.system.kind == TRACE_SYSTEM_MAP_PAM ? "PAM" : "DRAM",
                    "Reg:$%02X $%02X -> $%02X", entry.system.address, entry.system.previous, value);
            break;
        }
        case TRACE_SYSTEM_POST:
            format_event(buffer, size, "SYSTEM", "POST", "$%02X", value);
            break;
        case TRACE_SYSTEM_CONTROL:
            if (entry.system.port == 0x0070)
                format_event(buffer, size, "SYSTEM", "NMI", "$0070 <- $%02X  NMI:%s", value,
                    (value & 0x80) != 0 ? "MASKED" : "ENABLED");
            else if (entry.system.port == 0x0061)
                format_event(buffer, size, "SYSTEM", "PORT 61h", "$%02X -> $%02X  Gate2:%u Speaker:%u%s%s",
                    entry.system.previous, value, value & 0x01, (value >> 1) & 0x01,
                    (value & 0x04) != 0 ? "  PARITY CHECK OFF" : "", (value & 0x08) != 0 ? "  CHANNEL CHECK OFF" : "");
            else if (entry.system.port == 0x00F0 && entry.system.kind == 1)
                format_event(buffer, size, "SYSTEM", "FPU ERROR", "IRQ13 raised");
            else if (entry.system.port == 0x00F0)
                format_event(buffer, size, "SYSTEM", "FPU ERROR", "$00F0 <- $%02X  IRQ13 cleared", value);
            else
                format_event(buffer, size, "SYSTEM", "CONTROL", "$%04X <- $%02X", entry.system.port, value);
            break;
        case TRACE_SYSTEM_RTC_READ:
        case TRACE_SYSTEM_RTC_WRITE:
        {
            char detail[64];
            char kept[24] = "";
            bool write = entry.event == TRACE_SYSTEM_RTC_WRITE;
            format_rtc_detail(entry.system.address, value, detail, sizeof(detail));

            if (write && (entry.flags & TRACE_ENTRY_IGNORED) != 0)
                snprintf(kept, sizeof(kept), "  Kept:$%02X", value);

            format_event(buffer, size, "SYSTEM", write ? "RTC WRITE" : "RTC READ", "$%02X %-12s %s $%02X%s%s%s",
                entry.system.address, rtc_register_name(entry.system.address), write ? "<-" : "->",
                write ? entry.system.raw : value, kept, detail,
                (entry.flags & TRACE_SYSTEM_FLAG_CHECKSUM) != 0 ? "  CHECKSUM AREA" : "");
            break;
        }
        case TRACE_SYSTEM_RTC_IRQ:
            format_event(buffer, size, "SYSTEM", "RTC IRQ", "IRQ8  C:$%02X %s%s%s", value,
                (value & 0x40) != 0 ? "PERIODIC " : "", (value & 0x20) != 0 ? "ALARM " : "",
                (value & 0x10) != 0 ? "UPDATE" : "");
            break;
        case TRACE_SYSTEM_PCI:
        {
            const char* direction = (entry.flags & TRACE_SYSTEM_FLAG_READ) != 0 ? "->" : "<-";
            format_event(buffer, size, "SYSTEM", "PCI", "%02X:%02X.%u Reg:$%02X %s $%0*X%s", entry.system.bus,
                entry.system.device, entry.system.function, entry.system.address, direction, entry.system.raw * 2,
                entry.system.data, (entry.flags & TRACE_SYSTEM_FLAG_NO_DEVICE) != 0 ? "  NO DEVICE" : "");
            break;
        }
        case TRACE_SYSTEM_CONSOLE:
            format_event(buffer, size, "SYSTEM", "CONSOLE", "$%04X \"%.*s\"", entry.console.port,
                (int)MIN(entry.console.length, (u8)64), entry.console.text);
            break;
        default:
            format_event(buffer, size, "SYSTEM", "PROGRAM", "%.*s", (int)MIN(entry.console.length, (u8)64),
                entry.console.text);
            break;
    }
}

static void format_bit_list(u8 bits, int base, char* buffer, size_t size)
{
    size_t length = 0;
    buffer[0] = 0;

    for (int i = 0; i < 8 && length + 4 < size; i++)
    {
        if (IS_NOT_SET_BIT(bits, i))
            continue;

        int written = snprintf(buffer + length, size - length, length == 0 ? "%d" : ",%d", base + i);

        if (written > 0)
            length += (size_t)written;
    }

    if (length == 0)
        snprintf(buffer, size, "NONE");
}

static void format_msf(u32 lba, char* buffer, size_t size)
{
    u32 frames = lba + 150;
    snprintf(buffer, size, "%02u:%02u:%02u", frames / (75 * 60), (frames / 75) % 60, frames % 75);
}

static void format_buttons(u8 device, u16 buttons, char* buffer, size_t size)
{
    static const char* const k_mouse_names[3] = { "LEFT", "RIGHT", "MIDDLE" };
    static const char* const k_joystick_names[8] = { "B1", "B2", "B3", "B4", "UP", "DOWN", "LEFT", "RIGHT" };
    bool mouse = device == TRACE_INPUT_DEVICE_MOUSE;
    const char* const* names = mouse ? k_mouse_names : k_joystick_names;
    int count = mouse ? 3 : 8;
    size_t length = 0;
    buffer[0] = 0;

    for (int i = 0; i < count; i++)
    {
        if (IS_NOT_SET_BIT(buttons, i))
            continue;

        int written = snprintf(buffer + length, size - length, length == 0 ? "%s" : "+%s", names[i]);

        if (written < 0 || (size_t)written >= size - length)
            break;

        length += (size_t)written;
    }

    if (length == 0)
        snprintf(buffer, size, "-");
}

static void format_bytes(const u8* data, int count, char* buffer, size_t size)
{
    buffer[0] = ' ';

    if ((count <= 0) || (hex_encode(data, (size_t)count, buffer + 1, size - 1, true, ' ') == 0))
        snprintf(buffer, size, " -");
}

static void format_gus(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    switch (entry.event)
    {
        case TRACE_GUS_REGISTER:
            format_event(buffer, size, "GUS", "REGISTER", "v%02u reg %02X = %04X", entry.gus.voice, entry.gus.index,
                entry.gus.value);
            break;
        case TRACE_GUS_IRQ:
            format_event(buffer, size, "GUS", "IRQ", "v%02u latch %02X", entry.gus.voice, entry.gus.value);
            break;
        case TRACE_GUS_DMA:
            if (entry.gus.voice == 0)
                format_event(buffer, size, "GUS", "DMA", "start  control %02X", entry.gus.value);
            else
                format_event(buffer, size, "GUS", "DMA", "TC  control %02X", entry.gus.value);
            break;
        default:
            format_event(buffer, size, "GUS", "LATCH", "%s latch = %02X", entry.gus.voice != 0 ? "IRQ" : "DMA",
                entry.gus.value);
            break;
    }
}

static void format_voodoo(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    switch (entry.event)
    {
        case TRACE_VOODOO_COMMAND:
            switch (entry.voodoo.command)
            {
                case TRACE_VOODOO_TRIANGLE:
                    format_event(buffer, size, "VOODOO", "TRIANGLE", "%u px  buffer %u  fifo %u", entry.voodoo.pixels,
                        entry.voodoo.buffer, entry.voodoo.queued);
                    break;
                case TRACE_VOODOO_FAST_FILL:
                    format_event(buffer, size, "VOODOO", "FASTFILL", "%u px  buffer %u  fifo %u", entry.voodoo.pixels,
                        entry.voodoo.buffer, entry.voodoo.queued);
                    break;
                case TRACE_VOODOO_SWAP:
                    format_event(buffer, size, "VOODOO", "SWAP", "%s interval %u  front %u  fifo %u",
                        (entry.voodoo.value & 0x01) != 0 ? "vsync" : "now", (entry.voodoo.value >> 1) & 0xFF,
                        entry.voodoo.buffer, entry.voodoo.queued);
                    break;
                default:
                    format_event(buffer, size, "VOODOO", "NOP", "%s  fifo %u",
                        (entry.voodoo.value & 0x01) != 0 ? "clear counters" : "-", entry.voodoo.queued);
                    break;
            }
            break;
        case TRACE_VOODOO_CULLED:
            format_event(buffer, size, "VOODOO", "CULLED", "%s  triangleCMD %08X",
                entry.voodoo.data != 0 ? "wrong sign" : "zero area", entry.voodoo.value);
            break;
        case TRACE_VOODOO_OVERFLOW:
            format_event(buffer, size, "VOODOO", "OVERFLOW", "%06X = %08X run early  fifo %u", entry.voodoo.value,
                entry.voodoo.data, entry.voodoo.queued);
            break;
        default:
            format_event(buffer, size, "VOODOO", "LFB READ", "%06X = %08X  fifo drained %u", entry.voodoo.value,
                entry.voodoo.data, entry.voodoo.queued);
            break;
    }
}

static void format_net(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    static const char* const k_drops[] = { "stopped", "address", "runt", "monitor", "ring full" };
    const u8* d = entry.net.destination;
    const u8* s = entry.net.source;

    switch (entry.event)
    {
        case TRACE_NET_WRITE:
            format_event(buffer, size, "NET", "WRITE", "p%u reg %02X = %02X  cr %02X isr %02X", entry.net.page,
                entry.net.reg, entry.net.value, entry.net.cr, entry.net.isr);
            break;
        case TRACE_NET_READ:
            format_event(buffer, size, "NET", "READ", "p%u reg %02X = %02X", entry.net.page, entry.net.reg,
                entry.net.value);
            break;
        case TRACE_NET_TRANSMIT:
            format_event(buffer, size, "NET", entry.net.value != 0 ? "LOOPBACK" : "TRANSMIT",
                "%u bytes  type %04X  %02X:%02X:%02X:%02X:%02X:%02X > %02X:%02X:%02X:%02X:%02X:%02X", entry.net.length,
                entry.net.type, s[0], s[1], s[2], s[3], s[4], s[5], d[0], d[1], d[2], d[3], d[4], d[5]);
            break;
        case TRACE_NET_RECEIVE:
            format_event(buffer, size, "NET", "RECEIVE",
                "%u bytes  type %04X  %02X:%02X:%02X:%02X:%02X:%02X > %02X:%02X:%02X:%02X:%02X:%02X  curr %02X bnry %02X",
                entry.net.length, entry.net.type, s[0], s[1], s[2], s[3], s[4], s[5], d[0], d[1], d[2], d[3], d[4], d[5],
                entry.net.curr, entry.net.bnry);
            break;
        case TRACE_NET_DROP:
            format_event(buffer, size, "NET", "DROP", "%s  %u bytes  type %04X  > %02X:%02X:%02X:%02X:%02X:%02X",
                entry.net.reason < 5 ? k_drops[entry.net.reason] : "?", entry.net.length, entry.net.type, d[0], d[1], d[2],
                d[3], d[4], d[5]);
            break;
        case TRACE_NET_IRQ:
            format_event(buffer, size, "NET", "IRQ", "%s  isr %02X imr %02X", entry.net.value != 0 ? "raise" : "lower",
                entry.net.isr, entry.net.imr);
            break;
        case TRACE_NET_DMA:
            if (entry.net.reg != 0)
                format_event(buffer, size, "NET", "DMA", "complete  address %04X", entry.net.address);
            else
                format_event(buffer, size, "NET", "DMA", "%s  address %04X count %04X",
                    entry.net.value == 0x08 ? "read" : entry.net.value == 0x10 ? "write" : "send packet",
                    entry.net.address, entry.net.count);
            break;
        default:
            format_event(buffer, size, "NET", "RESET", "cr %02X isr %02X", entry.net.cr, entry.net.isr);
            break;
    }
}

static bool parse_range_token(const char* text, size_t length, bool ports, GDOX_Trace_Range& range, bool& exclude)
{
    exclude = length > 0 && text[0] == '!';

    if (exclude)
    {
        text++;
        length--;
    }

    range.events = ports ? (TRACE_IO_EVENT_ALL) : 0;

    if (ports && length > 2 && text[length - 2] == ':')
    {
        char kind = text[length - 1];
        range.events = kind == 'r' || kind == 'R' ? TRACE_IO_EVENT_READS | TRACE_IO_EVENT_POLLS :
            kind == 'w' || kind == 'W' ? TRACE_IO_EVENT_WRITES : 0;
        length -= 2;

        if (range.events == 0)
            return false;
    }

    const char* dash = (const char*)memchr(text, '-', length);

    if (dash == NULL)
    {
        if (!parse_hex_string(text, length, &range.first))
            return false;

        range.last = range.first;
        return true;
    }

    size_t first_length = (size_t)(dash - text);
    return parse_hex_string(text, first_length, &range.first) &&
        parse_hex_string(dash + 1, length - first_length - 1, &range.last) && range.first <= range.last;
}

static bool parse_ranges(const char* text, bool ports, GDOX_Trace_Range* include, u8* include_count,
    GDOX_Trace_Range* exclude, u8* exclude_count)
{
    *include_count = 0;
    *exclude_count = 0;

    while (*text != 0)
    {
        while (*text == ' ' || *text == ',')
            text++;

        size_t length = 0;

        while (text[length] != 0 && text[length] != ' ' && text[length] != ',')
            length++;

        if (length == 0)
            break;

        GDOX_Trace_Range range;
        bool excluded = false;

        if (!parse_range_token(text, length, ports, range, excluded))
            return false;

        if (excluded && *exclude_count < GDOX_TRACE_FILTER_RANGES)
            exclude[(*exclude_count)++] = range;
        else if (!excluded && *include_count < GDOX_TRACE_FILTER_RANGES)
            include[(*include_count)++] = range;
        else
            return false;

        text += length;
    }

    return true;
}

bool trace_log_parse_filters(const char* code, const char* ports, const char* interrupts, GDOX_Trace_Filters& filters)
{
    TraceLogger::ClearFilters(filters);

    if (!parse_ranges(code, false, filters.code_include, &filters.code_include_count, filters.code_exclude,
        &filters.code_exclude_count))
        return false;

    if (!parse_ranges(ports, true, filters.port_include, &filters.port_include_count, filters.port_exclude,
        &filters.port_exclude_count))
        return false;

    bool any = false;

    while (*interrupts != 0)
    {
        while (*interrupts == ' ' || *interrupts == ',')
            interrupts++;

        size_t length = 0;

        while (interrupts[length] != 0 && interrupts[length] != ' ' && interrupts[length] != ',')
            length++;

        if (length == 0)
            break;

        const char* colon = (const char*)memchr(interrupts, ':', length);
        size_t vector_length = colon != NULL ? (size_t)(colon - interrupts) : length;
        u32 vector = 0;

        if (!parse_hex_string(interrupts, vector_length, &vector) || vector > 0xFF ||
            filters.function_count >= GDOX_TRACE_FILTER_FUNCTIONS)
            return false;

        if (!any)
        {
            memset(filters.vectors, 0, sizeof(filters.vectors));
            any = true;
        }

        filters.vectors[vector >> 5] |= 1U << (vector & 31);

        if (colon != NULL)
        {
            u32 value = 0;
            size_t value_length = length - vector_length - 1;

            if (!parse_hex_string(colon + 1, value_length, &value) || value > 0xFFFF)
                return false;

            GDOX_Trace_Function& function = filters.functions[filters.function_count++];
            function.vector = (u8)vector;
            function.match = value_length <= 2 ? TRACE_FUNCTION_AH : TRACE_FUNCTION_AX;
            function.value = (u16)value;
        }

        interrupts += length;
    }

    return true;
}
