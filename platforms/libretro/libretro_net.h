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

#ifndef LIBRETRO_NET_H
#define LIBRETRO_NET_H

#include "libretro.h"

class GeardoxCore;

typedef void (*libretro_net_event_t)(int event, int machine);

enum Libretro_Net_Event
{
    LIBRETRO_NET_STARTED = 0,
    LIBRETRO_NET_STOPPED,
    LIBRETRO_NET_JOINED,
    LIBRETRO_NET_LEFT,
    LIBRETRO_NET_REFUSED
};

void libretro_net_init(retro_environment_t environ_cb, GeardoxCore* core, libretro_net_event_t event_cb);
void libretro_net_deinit(void);
void libretro_net_begin_run(int av_enable);
void libretro_net_end_run(void);
bool libretro_net_is_active(void);
bool libretro_net_is_host(void);
int libretro_net_get_machine(void);
int libretro_net_get_machine_count(void);

#endif /* LIBRETRO_NET_H */
