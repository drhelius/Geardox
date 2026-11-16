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

#ifndef GUI_DEBUG_PROGRAM_H
#define GUI_DEBUG_PROGRAM_H

#include "geardox.h"

#ifdef GUI_DEBUG_PROGRAM_IMPORT
    #define EXTERN
#else
    #define EXTERN extern
#endif

EXTERN void gui_debug_window_program(void);
EXTERN bool gui_debug_program_save_output(const char* file_path);

#undef GUI_DEBUG_PROGRAM_IMPORT
#undef EXTERN
#endif /* GUI_DEBUG_PROGRAM_H */
