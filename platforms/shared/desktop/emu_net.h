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

#ifndef EMU_NET_H
#define EMU_NET_H

#include "geardox.h"

#ifdef EMU_NET_IMPORT
    #define EXTERN
#else
    #define EXTERN extern
#endif

class NetSession;

EXTERN void emu_net_init(void);
EXTERN void emu_net_destroy(void);
EXTERN void emu_net_update(bool loading);
EXTERN bool emu_net_host(void);
EXTERN bool emu_net_join(void);
EXTERN void emu_net_leave(void);
EXTERN bool emu_net_is_active(void);
EXTERN bool emu_net_is_connected(void);
EXTERN bool emu_net_has_device(void);
EXTERN NetSession* emu_net_get_session(void);
EXTERN void emu_net_state_loaded(void);
EXTERN void emu_net_set_command_line(int mode, const char* host, int port);
EXTERN void emu_net_start_command_line(void);
EXTERN int emu_net_get_local_addresses(u32* addresses, int max);
EXTERN bool emu_net_get_bar_text(char* text, int size);

#undef EMU_NET_IMPORT
#undef EXTERN
#endif /* EMU_NET_H */
