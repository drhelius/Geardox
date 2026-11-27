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

#ifndef GEARDOX_H
#define GEARDOX_H

#include "common/common.h"
#include "common/profiler.h"
#include "common/trace_logger.h"
#include "media/firmware.h"
#include "i386/i386.h"
#include "i386/x87.h"
#include "geardox_core.h"
#include "input/input.h"
#include "input/i8042.h"
#include "input/gamepad_keys.h"
#include "input/key_map.h"
#include "osd/key_mapper.h"
#include "audio/audio.h"
#include "cdrom/cdrom_audio.h"
#include "cdrom/cdrom_media.h"
#include "media/media.h"
#include "system/memory.h"
#include "system/mc146818.h"
#include "system/debug_console.h"
#include "system/dev_port.h"
#include "system/machine_profiles.h"
#include "network/ne2000.h"
#include "network/network_port.h"
#include "network/serial_link.h"

#endif /* GEARDOX_H */
