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

#ifndef TRACE_LOGGER_FORMATTER_H
#define TRACE_LOGGER_FORMATTER_H

#include "geardox.h"

#define GDOX_TRACE_FORMAT_BUFFER_SIZE 512
#define GDOX_TRACE_INSTRUCTION_WIDTH 40

struct GDOX_Trace_Format_Options
{
    bool linear;
    bool registers;
    bool segments;
    bool flags;
    bool bytes;
    bool cycles;
    bool previous_cycle_valid;
    u64 previous_cycle;
};

void trace_log_format_cpu_address(const GDOX_Trace_Entry& entry, char* buffer, size_t buffer_size);
void trace_log_format_cpu_instruction(const GDOX_Trace_Entry& entry, char* buffer, size_t buffer_size);
void trace_log_reset_file_names(void);
bool trace_log_parse_filters(const char* code, const char* ports, const char* interrupts, GDOX_Trace_Filters& filters);
void trace_log_format_cpu_flags(const GDOX_Trace_Entry& entry, char* buffer, size_t buffer_size);
void trace_log_format_cpu_bytes(const GDOX_Trace_Entry& entry, char* buffer, size_t buffer_size);
void trace_log_format_cycle_prefix(const GDOX_Trace_Entry& entry, bool previous_cycle_valid,
    u64 previous_cycle, char* buffer, size_t buffer_size);
void trace_logger_format_entry(const GDOX_Trace_Entry& entry,
    const GDOX_Trace_Format_Options& options, char* buffer, size_t buffer_size);

#endif /* TRACE_LOGGER_FORMATTER_H */
