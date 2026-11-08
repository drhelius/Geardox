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
#include "drive/i82077.h"
#include "drive/ide_cdrom.h"
#include "system/i8259.h"

static const char* const k_trace_pit_names[3] = { "CH0", "CH1", "CH2" };
static const char* const k_trace_palette_components[3] = { "RED", "GREEN", "BLUE" };
static const char* const k_trace_pcm_formats[6] = { "PCM8", "PCM16", "ADPCM2", "ADPCM3", "ADPCM4", "SILENCE" };
static const char* const k_trace_uart_registers[8] = { "RBR/THR", "IER", "IIR/FCR", "LCR", "MCR", "LSR", "MSR", "SCR" };

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
static void format_bit_list(u8 bits, int base, char* buffer, size_t size);
static void format_msf(u32 lba, char* buffer, size_t size);
static void format_buttons(u8 device, u16 buttons, char* buffer, size_t size);
static void format_bytes(const u8* data, int count, char* buffer, size_t size);

void trace_log_format_cpu_address(const GDOX_Trace_Entry& entry, char* buffer, size_t buffer_size)
{
    if (entry.cpu.mode == I386_MODE_PROTECTED)
        snprintf(buffer, buffer_size, "%04X:%08X", entry.cpu.cs, entry.cpu.eip);
    else
        snprintf(buffer, buffer_size, "%04X:%04X    ", entry.cpu.cs, entry.cpu.eip & 0xFFFF);
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
    static const char k_hex[] = "0123456789ABCDEF";
    size_t pos = 0;
    u8 size = MIN(entry.cpu.size, (u8)GDOX_I386_MAX_INSTRUCTION_LENGTH);

    for (u8 i = 0; i < size && pos + 3 < buffer_size; i++)
    {
        u8 value = entry.cpu.opcodes[i];
        buffer[pos++] = k_hex[value >> 4];
        buffer[pos++] = k_hex[value & 0x0F];
        buffer[pos++] = ' ';
    }

    buffer[pos] = 0;
}

void trace_log_format_cycle_prefix(const GDOX_Trace_Entry& entry, bool previous_cycle_valid,
    u64 previous_cycle, char* buffer, size_t buffer_size)
{
    if (previous_cycle_valid && entry.cycle >= previous_cycle)
        snprintf(buffer, buffer_size, "@%012llu +%-12llu ", (unsigned long long)entry.cycle,
            (unsigned long long)(entry.cycle - previous_cycle));
    else if (previous_cycle_valid)
        snprintf(buffer, buffer_size, "@%012llu RESET         ", (unsigned long long)entry.cycle);
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
        trace_log_format_cycle_prefix(entry, options.previous_cycle_valid, options.previous_cycle, prefix,
            sizeof(prefix));
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

    trace_log_format_cpu_address(entry, address, sizeof(address));

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

    const char* name = entry.cpu.name[0] != 0 ? entry.cpu.name : "???";

    if (strlen(entry.cpu.name) == GDOX_TRACE_NAME_SIZE - 1)
    {
        emu_get_core()->GetI386()->FormatDisassemblerRecordText(entry.cpu.linear);

        const I386_Disassembler_Record* record = emu_get_core()->GetI386()->GetDisassemblerRecord(entry.cpu.linear);

        if (IsValidPointer(record) && record->size == entry.cpu.size &&
            memcmp(record->opcodes, entry.cpu.opcodes, entry.cpu.size) == 0)
            name = record->name;
    }

    snprintf(buffer, size, "%s%s  %s%s%s%-*s %s", linear, address, registers, segments, flags,
        GDOX_TRACE_INSTRUCTION_WIDTH, name, bytes);
}

static void format_interrupt(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    char name[16];
    char description[64];
    u8 vector = entry.interrupt.vector;
    u32 from = entry.interrupt.from;
    u32 to = entry.interrupt.to;

    gui_debug_i386_vector_name(vector, name, sizeof(name), description, sizeof(description), false);

    switch (entry.event)
    {
        case TRACE_CPU_INTERRUPT_HARDWARE:
        {
            u8 line = entry.interrupt.line;

            if (line < 16)
                format_event(buffer, size, "INT", "IRQ", "IRQ%u %s  Vector:$%02X  From:$%08X  To:$%08X", line,
                    k_debug_irq_sources[line], vector, from, to);
            else
                format_event(buffer, size, "INT", "IRQ", "%s  Vector:$%02X  From:$%08X  To:$%08X", description,
                    vector, from, to);
            break;
        }
        case TRACE_CPU_INTERRUPT_EXCEPTION:
        {
            char error[24] = "";

            if (entry.interrupt.has_error_code)
                snprintf(error, sizeof(error), "  Error:$%04X", entry.interrupt.error_code);

            format_event(buffer, size, "INT", "EXCEPTION", "%s %s  Vector:$%02X%s  From:$%08X  To:$%08X", name,
                description, vector, error, from, to);
            break;
        }
        default:
        {
            const char* function = gui_debug_i386_interrupt_function(vector, entry.interrupt.ax);
            bool named = IsValidPointer(function);

            format_event(buffer, size, "INT", "SOFTWARE", "INT $%02X %s%s%s  AX:$%04X  From:$%08X  To:$%08X", vector,
                description, named ? ": " : "", named ? function : "", entry.interrupt.ax, from, to);
            break;
        }
    }
}

static void format_io(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    const char* label = gui_debug_port_label(entry.io.port);
    int digits = MIN((int)entry.io.size, 4) * 2;
    u32 mask = digits >= 8 ? 0xFFFFFFFFU : (1U << (digits * 4)) - 1;
    bool write = entry.event == TRACE_IO_WRITE;

    format_event(buffer, size, "I/O", write ? "OUT" : "IN", "$%04X %-18s %s $%0*X  PC:$%08X", entry.io.port,
        IsValidPointer(label) ? label : "", write ? "<-" : "->", digits, entry.io.value & mask, entry.io.pc);
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
        {
            u8 line = entry.pic.line & 0x0F;
            bool masked = ((entry.pic.imr >> (line & 7)) & 0x01) != 0;

            format_event(buffer, size, "PIC", "REQUEST", "IRQ%u %s  Vector:$%02X%s  %s", line,
                k_debug_irq_sources[line], entry.pic.vector, masked ? "  MASKED" : "", state);
            break;
        }
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

            if (specific)
                format_event(buffer, size, "PIC", eoi ? "EOI" : "OCW2", "%s $%02X %s IRQ%d  %s", chip, value,
                    k_ocw2_names[operation], base + (value & 0x07), state);
            else
                format_event(buffer, size, "PIC", eoi ? "EOI" : "OCW2", "%s $%02X %s  %s", chip, value,
                    k_ocw2_names[operation], state);
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

static void format_timer(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    int channel = MIN((int)entry.timer.counter, 2);
    u8 value = entry.timer.value;

    switch (entry.event)
    {
        case TRACE_TIMER_TIMEOUT:
            format_event(buffer, size, "TIMER", "OUT0", "CH0 %s  Rising edge  IRQ0", k_debug_pit_uses[0]);
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
            if (!entry.timer.complete)
            {
                format_event(buffer, size, "TIMER", "COUNTER", "%s %s  Write:$%02X  LSB, MSB pending",
                    k_trace_pit_names[channel], k_debug_pit_uses[channel], value);
                break;
            }

            u32 reload = entry.timer.reload;
            u32 count = reload;

            if (entry.timer.bcd)
            {
                count = ((reload >> 12) & 0x0F) * 1000 + ((reload >> 8) & 0x0F) * 100 + ((reload >> 4) & 0x0F) * 10 +
                    (reload & 0x0F);
                count = count == 0 ? 10000 : count;
            }
            else if (count == 0)
                count = 65536;

            double clock = (double)GDOX_MASTER_CLOCK_RATE / 12.0;

            format_event(buffer, size, "TIMER", "COUNTER", "%s %s  Write:$%02X  Reload:$%04X  %.2f Hz",
                k_trace_pit_names[channel], k_debug_pit_uses[channel], value, reload, clock / count);
            break;
        }
        default:
            format_event(buffer, size, "TIMER", "SPEAKER", "Gate:%u  Data:%u", value & 0x01, (value >> 1) & 0x01);
            break;
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
                ((entry.dma.mask >> channel) & 0x01) != 0 ? "  MASKED" : "");
            break;
        case TRACE_DMA_END:
            format_event(buffer, size, "DMA", "END", "CH%u %s  TERMINAL COUNT  Address:$%06X  Count:$%04X", channel, device,
                entry.dma.address, entry.dma.count);
            break;
        default:
        {
            u8 value = entry.dma.value;

            if (entry.dma.reg >= 0x10)
            {
                format_event(buffer, size, "DMA", "PAGE", "Port:$%02X  CH%u %s  Write:$%02X  Address:$%06X",
                    0x80 | (entry.dma.reg & 0x0F), channel, device, value, entry.dma.address);
                break;
            }

            if (entry.dma.reg < 0x08)
            {
                format_event(buffer, size, "DMA", (entry.dma.reg & 0x01) != 0 ? "COUNT" : "ADDRESS",
                    "CH%u %s  Write:$%02X  Address:$%06X  Count:$%04X", channel, device, value, entry.dma.address,
                    entry.dma.count);
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

static void format_video(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    static const char* const k_files[4] = { "SR", "GR", "AR", "MISC" };

    char beam[32] = "CRTC stopped";
    u8 raw = entry.video.raw;

    if (entry.video.line != 0xFFFF)
        snprintf(beam, sizeof(beam), "Line:%u Dot:%u", entry.video.line, entry.video.dot);

    switch (entry.event)
    {
        case TRACE_VIDEO_CRTC:
            format_event(buffer, size, "VIDEO", "CRTC", "CR%02X %-12s <- $%02X  %s", entry.video.reg,
                k_debug_crtc_register_names[entry.video.reg % 25], raw, beam);
            break;
        case TRACE_VIDEO_OUTPUT:
            if (entry.video.bank == 3)
                format_event(buffer, size, "VIDEO", "OUTPUT", "MISC <- $%02X  %s", raw, beam);
            else
                format_event(buffer, size, "VIDEO", "OUTPUT", "%s%02X <- $%02X  %s", k_files[entry.video.bank & 3],
                    entry.video.reg, raw, beam);
            break;
        case TRACE_VIDEO_PALETTE:
            format_event(buffer, size, "VIDEO", "PALETTE", "DAC[$%02X] %s <- $%02X  RGB:$%06X  %s", entry.video.reg,
                k_trace_palette_components[MIN(entry.video.value, (u16)2)], raw, entry.video.param, beam);
            break;
        case TRACE_VIDEO_VSYNC:
            format_event(buffer, size, "VIDEO", "VSYNC", "Retrace  Frame:%u", entry.video.param);
            break;
        case TRACE_VIDEO_VSYNC_CLEAR:
            format_event(buffer, size, "VIDEO", "VSYNC ACK", "IRQ2 cleared  %s", beam);
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

static void format_fm(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    u16 address = entry.fm.address;
    u8 reg = (u8)address;
    u8 value = entry.fm.value;
    u8 channel = entry.fm.channel + 1;
    int chip = entry.fm.flags;
    u16 frequency = entry.fm.frequency;

    switch (entry.event)
    {
        case TRACE_FM_KEY:
            format_event(buffer, size, "FM", "KEY", "OPL%d CH%u %s  Block:%u FNum:$%03X", chip, channel,
                (value & 0x20) != 0 ? "ON " : "OFF", (frequency >> 10) & 0x07, frequency & 0x3FF);
            break;
        case TRACE_FM_FREQUENCY:
            format_event(buffer, size, "FM", "FREQ", "OPL%d CH%u  $%03X <- $%02X  Block:%u FNum:$%03X", chip, channel,
                address, value, (frequency >> 10) & 0x07, frequency & 0x3FF);
            break;
        case TRACE_FM_OPERATOR:
        {
            char fields[40];

            switch (reg & 0xE0)
            {
                case 0x20: snprintf(fields, sizeof(fields), "AM:%u VIB:%u EG:%u KSR:%u MUL:%u", value >> 7,
                    (value >> 6) & 0x01, (value >> 5) & 0x01, (value >> 4) & 0x01, value & 0x0F); break;
                case 0x40: snprintf(fields, sizeof(fields), "KSL:%u TL:$%02X", value >> 6, value & 0x3F); break;
                case 0x60: snprintf(fields, sizeof(fields), "AR:%u DR:%u", value >> 4, value & 0x0F); break;
                case 0x80: snprintf(fields, sizeof(fields), "SL:%u RR:%u", value >> 4, value & 0x0F); break;
                default: snprintf(fields, sizeof(fields), "WS:%u", value & 0x07); break;
            }

            format_event(buffer, size, "FM", "OPERATOR", "OPL%d $%03X <- $%02X  %s", chip, address, value, fields);
            break;
        }
        case TRACE_FM_CHANNEL:
            format_event(buffer, size, "FM", "CHANNEL", "OPL%d CH%u  $%03X <- $%02X  CNT:%u FB:%u A:%u B:%u", chip,
                channel, address, value, value & 0x01, (value >> 1) & 0x07, (value >> 4) & 0x01, (value >> 5) & 0x01);
            break;
        case TRACE_FM_TIMER:
            if (reg == 0x04)
                format_event(buffer, size, "FM", "TIMER",
                    "OPL%d CONTROL <- $%02X  Reset:%u Mask 1:%u 2:%u  Start 1:%u 2:%u", chip, value, value >> 7,
                    (value >> 6) & 0x01, (value >> 5) & 0x01, value & 0x01, (value >> 1) & 0x01);
            else
                format_event(buffer, size, "FM", "TIMER", "OPL%d Timer %u <- $%02X", chip, reg - 1, value);
            break;
        default:
            if (reg == 0xBD)
                format_event(buffer, size, "FM", "GLOBAL", "OPL%d $BD <- $%02X  AM:%u VIB:%u Rhythm:%u Drums:$%02X", chip,
                    value, value >> 7, (value >> 6) & 0x01, (value >> 5) & 0x01, value & 0x1F);
            else
                format_event(buffer, size, "FM", "GLOBAL", "OPL%d $%03X <- $%02X", chip, address, value);
            break;
    }
}

static void format_pcm(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    switch (entry.event)
    {
        case TRACE_PCM_COMMAND:
        {
            const u8* o = entry.pcm.operands;

            if (entry.pcm.operand_count == 0)
                format_event(buffer, size, "DSP", "COMMAND", "$%02X", entry.pcm.command);
            else if (entry.pcm.operand_count == 1)
                format_event(buffer, size, "DSP", "COMMAND", "$%02X $%02X", entry.pcm.command, o[0]);
            else if (entry.pcm.operand_count == 2)
                format_event(buffer, size, "DSP", "COMMAND", "$%02X $%02X $%02X", entry.pcm.command, o[0], o[1]);
            else
                format_event(buffer, size, "DSP", "COMMAND", "$%02X $%02X $%02X $%02X", entry.pcm.command, o[0], o[1],
                    o[2]);
            break;
        }
        case TRACE_PCM_TRANSFER:
        {
            u8 flags = entry.pcm.flags;
            format_event(buffer, size, "DSP", "TRANSFER", "%s %s  %s%s%s%s%u Hz  Length:%u",
                k_trace_pcm_formats[MIN(entry.pcm.format, (u8)5)], (flags & 0x02) != 0 ? "IN " : "OUT",
                (flags & 0x01) != 0 ? "AUTO " : "SINGLE ", (flags & 0x04) != 0 ? "HIGH SPEED " : "",
                (flags & 0x08) != 0 ? "STEREO " : "", (flags & 0x10) != 0 ? "SIGNED " : "", entry.pcm.rate,
                entry.pcm.length);
            break;
        }
        case TRACE_PCM_IRQ:
            format_event(buffer, size, "DSP", "IRQ", "%u-bit block end  Status:$%02X", entry.pcm.command, entry.pcm.flags);
            break;
        default:
            format_event(buffer, size, "DSP", "ACK", "%u-bit  Status:$%02X", entry.pcm.command, entry.pcm.flags);
            break;
    }
}

static void format_mixer(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    format_event(buffer, size, "MIXER", entry.event == TRACE_MIXER_VOLUME ? "VOLUME" : "CONTROL", "$%02X <- $%02X",
        entry.mixer.index, entry.mixer.value);
}

static void format_midi(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    const char* source = entry.midi.source == TRACE_MIDI_SOURCE_MPU ? "MPU" : "DSP";

    format_event(buffer, size, "MIDI", entry.event == TRACE_MIDI_DATA ? "DATA" : "COMMAND", "%s  $%02X", source,
        entry.midi.value);
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
            format_event(buffer, size, "CDROM", "PACKET",
                "%d  $%02X %-16s %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X", entry.cdrom.flags,
                entry.cdrom.command, GetCdRomPacketName(entry.cdrom.command), b[1], b[2], b[3], b[4], b[5], b[6], b[7],
                b[8], b[9], b[10], b[11]);
            break;
        case TRACE_CDROM_COMMAND:
            format_event(buffer, size, "CDROM", "COMMAND", "%d  $%02X %s", entry.cdrom.flags, entry.cdrom.command,
                GetCdRomAtaName(entry.cdrom.command));
            break;
        case TRACE_CDROM_COMPLETE:
            if (b[4] != 0)
                format_event(buffer, size, "CDROM", "COMPLETE", "%d  $%02X %-16s Status:%02X  Sense:%X/%02X/%02X",
                    entry.cdrom.flags, entry.cdrom.command, GetCdRomPacketName(entry.cdrom.command), entry.cdrom.value,
                    b[0], b[1], b[2]);
            else
                format_event(buffer, size, "CDROM", "COMPLETE", "%d  $%02X %-16s Status:%02X  Error:%02X",
                    entry.cdrom.flags, entry.cdrom.command, GetCdRomAtaName(entry.cdrom.command), entry.cdrom.value,
                    b[3]);
            break;
        case TRACE_CDROM_DATA:
            format_event(buffer, size, "CDROM", "DATA", "%d  LBA:%u-%u  MSF %s  Bytes:%u", entry.cdrom.flags, lba,
                entry.cdrom.end_lba, msf, entry.cdrom.size);
            break;
        case TRACE_CDROM_MEDIA:
        {
            static const char* const kinds[6] = { "DISC IN", "DISC OUT", "TRAY OPEN", "TRAY CLOSED", "READY",
                "MOTOR STOP" };
            format_event(buffer, size, "CDROM", "MEDIA", "%d  %s", entry.cdrom.flags,
                kinds[MIN(entry.cdrom.value, (u8)5)]);
            break;
        }
        case TRACE_CDROM_CDDA_PLAY:
            format_event(buffer, size, "CDROM", "CDDA PLAY", "LBA:%u-%u  MSF %s-%s  Seek:%u ms%s", lba,
                entry.cdrom.end_lba, msf, end_msf, entry.cdrom.size, (entry.cdrom.flags & 0x01) != 0 ? "  REPEAT" : "");
            break;
        case TRACE_CDROM_CDDA_PAUSE:
            format_event(buffer, size, "CDROM", "CDDA PAUSE", "LBA:%u  MSF %s", lba, msf);
            break;
        case TRACE_CDROM_CDDA_RESUME:
            format_event(buffer, size, "CDROM", "CDDA RESUME", "LBA:%u  MSF %s", lba, msf);
            break;
        case TRACE_CDROM_CDDA_STOP:
            format_event(buffer, size, "CDROM", "CDDA STOP", "LBA:%u  MSF %s", lba, msf);
            break;
        case TRACE_CDROM_CDDA_END:
            format_event(buffer, size, "CDROM", "CDDA END", "LBA:%u  MSF %s", lba, msf);
            break;
        default:
            format_event(buffer, size, "CDROM", "CDDA LOOP", "Back to LBA:%u  MSF %s  End:%u", lba, msf,
                entry.cdrom.end_lba);
            break;
    }
}

static void format_fdc(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    char drive[8] = "-";

    if (entry.fdc.drive >= 0)
        snprintf(drive, sizeof(drive), "%d", entry.fdc.drive);

    const char* command = k_debug_fdc_command_names[MIN(entry.fdc.command, (u8)20)];

    switch (entry.event)
    {
        case TRACE_FDC_COMMAND:
            if (entry.fdc.command >= I82077::I82077_COMMAND_READ && entry.fdc.command <= I82077::I82077_COMMAND_VERIFY)
                format_event(buffer, size, "FDC", "COMMAND", "$%02X %s  Drive:%s C:%u H:%u R:%u N:%u", entry.fdc.value, command,
                    drive, entry.fdc.cylinder, entry.fdc.track, entry.fdc.sector, entry.fdc.data);
            else if (entry.fdc.command == I82077::I82077_COMMAND_SEEK || entry.fdc.command == I82077::I82077_COMMAND_RELATIVE_SEEK)
                format_event(buffer, size, "FDC", "COMMAND", "$%02X %s  Drive:%s To:%u", entry.fdc.value, command, drive,
                    entry.fdc.cylinder);
            else
                format_event(buffer, size, "FDC", "COMMAND", "$%02X %s", entry.fdc.value, command);
            break;
        case TRACE_FDC_END:
            format_event(buffer, size, "FDC", "END", "%s  ST0:$%02X ST1:$%02X ST2:$%02X  Drive:%s C:%u H:%u R:%u", command,
                entry.fdc.status, entry.fdc.value, entry.fdc.data, drive, entry.fdc.cylinder, entry.fdc.track,
                entry.fdc.sector);
            break;
        case TRACE_FDC_DRIVE_CONTROL:
            format_event(buffer, size, "FDC", "DOR", "$%02X  Drive:%s", entry.fdc.value, drive);
            break;
        default:
            format_event(buffer, size, "FDC", "DRIVE SEL", "DOR:$%02X  Drive:%s  Cylinder:%u", entry.fdc.value, drive,
                entry.fdc.cylinder);
            break;
    }
}

static void format_ide(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    const char* command = gui_debug_ide_command_name(entry.ide.command);
    char address[48];

    if ((entry.ide.head & 0x40) != 0)
        snprintf(address, sizeof(address), "LBA:%u", ((u32)(entry.ide.head & 0x0F) << 24) |
            ((u32)entry.ide.cylinder << 8) | entry.ide.sector);
    else
        snprintf(address, sizeof(address), "C:%u H:%u S:%u", entry.ide.cylinder, entry.ide.head & 0x0F, entry.ide.sector);

    switch (entry.event)
    {
        case TRACE_IDE_COMMAND:
            format_event(buffer, size, "IDE", "COMMAND", "$%02X %s  ID %u  %s Count:%u", entry.ide.command, command,
                entry.ide.id, address, entry.ide.count);
            break;
        case TRACE_IDE_END:
            format_event(buffer, size, "IDE", "END", "%s  ID %u  Status:$%02X Error:$%02X %s Count:%u", command,
                entry.ide.id, entry.ide.status, entry.ide.error, address, entry.ide.count);
            break;
        default:
            format_event(buffer, size, "IDE", "RESET", "ID %u", entry.ide.id);
            break;
    }
}

static const char* keyboard_command_name(u8 value, u8 flags)
{
    if (flags == 0x02)
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
            case 0xFE: return "RESEND";
            case 0xFF: return "RESET";
            default: return "";
        }
    }

    if ((value & 0xE0) == 0x20)
        return "READ RAM";

    if ((value & 0xE0) == 0x60)
        return "WRITE RAM";

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
        default: return "";
    }
}

static void format_keyboard(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    u8 value = entry.keyboard.value;
    u8 flags = entry.keyboard.flags;

    switch (entry.event)
    {
        case TRACE_KEYBOARD_KEY:
        {
            const char* name = gui_debug_key_name(entry.keyboard.key);
            const char* kind = flags == 0x01 ? "MAKE  " : flags == 0x02 ? "REPEAT" : "BREAK ";

            format_event(buffer, size, "KEYBRD", "KEY", "%s %s  Code:$%02X  Bytes:%u  Queued:%u", kind,
                IsValidPointer(name) ? name : "?", entry.keyboard.key, value, entry.keyboard.pending);
            break;
        }
        case TRACE_KEYBOARD_READ:
            format_event(buffer, size, "KEYBRD", "READ", "$0060 -> $%02X  %s  Queued:%u", value,
                flags != 0 ? "AUX" : "KBD", entry.keyboard.pending);
            break;
        case TRACE_KEYBOARD_COMMAND:
        {
            const char* target = flags == 0x00 ? "8042 CMD" : flags == 0x01 ? "8042 PARAM" : "KEYBOARD";

            if (flags == 0x00)
                format_event(buffer, size, "KEYBRD", "COMMAND", "%s $%02X  %s", target, value,
                    keyboard_command_name(value, flags));
            else if (entry.keyboard.key != 0)
                format_event(buffer, size, "KEYBRD", "COMMAND", "%s $%02X  %s", target, value,
                    keyboard_command_name(entry.keyboard.key, flags));
            else
                format_event(buffer, size, "KEYBRD", "COMMAND", "%s $%02X  %s", target, value,
                    keyboard_command_name(value, flags));
            break;
        }
        default:
            format_event(buffer, size, "KEYBRD", "PORT", "Output $%02X -> $%02X  A20:%s%s", entry.keyboard.key, value,
                (value & 0x02) != 0 ? "ON " : "OFF", (value & 0x01) == 0 ? "  RESET" : "");
            break;
    }
}

static void format_input(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    char bytes[32];
    int count = MIN((int)entry.input.count, 4);

    switch (entry.event)
    {
        case TRACE_INPUT_MOUSE_COMMAND:
            format_bytes(entry.input.data + 1, count - 1, bytes, sizeof(bytes));
            format_event(buffer, size, "INPUT", "MOUSE CMD", "PS/2 <- $%02X  Reply:%s", entry.input.data[0], bytes);
            break;
        case TRACE_INPUT_MOUSE_PACKET:
            format_bytes(entry.input.data, count, bytes, sizeof(bytes));
            format_event(buffer, size, "INPUT", "MOUSE PKT", "%s -> %s",
                entry.input.device == TRACE_INPUT_DEVICE_SERIAL_MOUSE ? "SERIAL" : "PS/2", bytes);
            break;
        case TRACE_INPUT_JOYSTICK_FIRE:
        {
            char axes[GDOX_JOYSTICK_AXIS_COUNT][12];

            for (int i = 0; i < GDOX_JOYSTICK_AXIS_COUNT; i++)
            {
                if (entry.input.data[i] == 0)
                    snprintf(axes[i], sizeof(axes[i]), "OPEN");
                else
                    snprintf(axes[i], sizeof(axes[i]), "%uus", entry.input.data[i] * 5);
            }

            format_event(buffer, size, "INPUT", "JOY FIRE", "$0201  A-X:%s  A-Y:%s  B-X:%s  B-Y:%s", axes[0], axes[1],
                axes[2], axes[3]);
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

static void format_serial(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    int port = entry.serial.port + 1;
    u8 value = entry.serial.value;

    switch (entry.event)
    {
        case TRACE_SERIAL_WRITE:
            format_event(buffer, size, "SERIAL", "WRITE", "COM%d %s <- $%02X", port,
                k_trace_uart_registers[entry.serial.reg & 0x07], value);
            break;
        case TRACE_SERIAL_RECEIVE:
            format_event(buffer, size, "SERIAL", "RECEIVE", "COM%d <- $%02X  LSR:$%02X", port, value,
                entry.serial.flags);
            break;
        case TRACE_SERIAL_TRANSMIT:
            format_event(buffer, size, "SERIAL", "TRANSMIT", "COM%d -> $%02X", port, value);
            break;
        case TRACE_SERIAL_MODEM:
            format_event(buffer, size, "SERIAL", "MODEM", "COM%d DTR:%u RTS:%u OUT1:%u OUT2:%u", port, value & 0x01,
                (value >> 1) & 0x01, (value >> 2) & 0x01, (value >> 3) & 0x01);
            break;
        default:
            format_event(buffer, size, "SERIAL", "IRQ", "COM%d IRQ %d %s  IIR:$%02X", port,
                entry.serial.port == 0 ? 4 : 3, entry.serial.flags != 0 ? "ON " : "OFF", value);
            break;
    }
}

static void format_parallel(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    format_event(buffer, size, "LPT", entry.event == TRACE_PARALLEL_DATA ? "DATA" : "CONTROL", "$%03X <- $%02X",
        entry.parallel.port, entry.parallel.value);
}

static void format_system(const GDOX_Trace_Entry& entry, char* buffer, size_t size)
{
    u8 value = entry.system.value;
    u8 flags = entry.system.flags;

    switch (entry.event)
    {
        case TRACE_SYSTEM_RESET:
            format_event(buffer, size, "SYSTEM", "CPU RESET", "%s%s%s", (flags & 0x01) != 0 ? " PORT 92h" : "",
                (flags & 0x02) != 0 ? " SHUTDOWN" : "", (flags & 0x04) != 0 ? " KEYBOARD CONTROLLER" : "");
            break;
        case TRACE_SYSTEM_MEMORY_MAP:
            format_event(buffer, size, "SYSTEM", "A20", "$%04X <- $%02X  A20:%s", entry.system.port, value,
                (flags & 0x01) != 0 ? "ON" : "OFF");
            break;
        case TRACE_SYSTEM_POST:
            format_event(buffer, size, "SYSTEM", "POST", "$%02X", value);
            break;
        case TRACE_SYSTEM_CONTROL:
            if (entry.system.port == 0x0070)
                format_event(buffer, size, "SYSTEM", "NMI", "$0070 <- $%02X  NMI:%s", value,
                    (flags & 0x02) != 0 ? "MASKED" : "ENABLED");
            else
                format_event(buffer, size, "SYSTEM", "CONTROL", "$%04X <- $%02X", entry.system.port, value);
            break;
        case TRACE_SYSTEM_RTC_READ:
            format_event(buffer, size, "SYSTEM", "RTC READ", "$%02X -> $%02X", entry.system.address, value);
            break;
        case TRACE_SYSTEM_RTC_IRQ:
            format_event(buffer, size, "SYSTEM", "RTC IRQ", "IRQ8  C:$%02X %s%s%s", value,
                (value & 0x40) != 0 ? "PERIODIC " : "", (value & 0x20) != 0 ? "ALARM " : "",
                (value & 0x10) != 0 ? "UPDATE" : "");
            break;
        default:
            format_event(buffer, size, "SYSTEM", "RTC WRITE", "$%02X <- $%02X", entry.system.address, value);
            break;
    }
}

static void format_bit_list(u8 bits, int base, char* buffer, size_t size)
{
    size_t length = 0;
    buffer[0] = 0;

    for (int i = 0; i < 8 && length + 4 < size; i++)
    {
        if ((bits & (1 << i)) == 0)
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
        if ((buttons & (1 << i)) == 0)
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
    size_t length = 0;
    buffer[0] = 0;

    for (int i = 0; i < count && length + 4 < size; i++)
        length += (size_t)snprintf(buffer + length, size - length, " %02X", data[i]);

    if (length == 0)
        snprintf(buffer, size, " -");
}
