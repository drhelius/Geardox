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

#ifndef GUI_GAME_H
#define GUI_GAME_H

#ifdef GUI_GAME_IMPORT
    #define EXTERN
#else
    #define EXTERN extern
#endif

EXTERN void gui_game_menu(void);
EXTERN void gui_game_popups(void);
EXTERN bool gui_game_open(const char* path);
EXTERN void gui_game_set_next_target(int target);
EXTERN void gui_game_loaded(void);
EXTERN void gui_game_dialog_new_descriptor(const char* folder);
EXTERN void gui_game_dialog_save_descriptor(const char* path);
EXTERN void gui_game_dialog_import_msdos(const char* path);

#undef GUI_GAME_IMPORT
#undef EXTERN
#endif /* GUI_GAME_H */
