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

#ifndef MACHINE_PROFILES_H
#define MACHINE_PROFILES_H

#include "../common/common.h"

#define GDOX_MACHINE_RELEASE_ORDER_COUNT 3
#define GDOX_MACHINE_CPU_SPEED_COUNT 8
#define GDOX_MACHINE_RAM_SIZE_COUNT 7
#define GDOX_MACHINE_ISA_CLOCK_RATE 8333333

// ISA cycles in BCLKs at the model's clock, less the 2 clocks of a bus cycle, in 1/256 clocks
#define GDOX_MACHINE_ISA_WAIT(bclks, rate) ((u16)((((u64)(bclks) * (rate) * 256) / GDOX_MACHINE_ISA_CLOCK_RATE) - 512))
#define GDOX_MACHINE_ISA16(rate) GDOX_MACHINE_ISA_WAIT(3, rate)
#define GDOX_MACHINE_ISA8(rate) GDOX_MACHINE_ISA_WAIT(6, rate)
#define GDOX_MACHINE_ISA8_WORD(rate) GDOX_MACHINE_ISA_WAIT(12, rate)

#define GDOX_MACHINE_BUS_WAITS(rate) { { 0, 0 }, { 0, 0 }, { 0, 0 }, 0, 0, { 0, 0 }, \
    { GDOX_MACHINE_ISA16(rate), GDOX_MACHINE_ISA16(rate) }, { 0, 0 }, GDOX_MACHINE_ISA16(rate), \
    GDOX_MACHINE_ISA16(rate), { 0, 0 }, 0, 0, 0 }

// The IDE data ports run a 16-bit cycle on a bus where every other port takes an 8-bit one
#define GDOX_MACHINE_IDE_DATA_WAIT(rate) ((s16)(GDOX_MACHINE_ISA16(rate) - GDOX_MACHINE_ISA8(rate)))
#define GDOX_MACHINE_PORT_WAITS(rate) { \
    { 0x0170, 0x0170, { GDOX_MACHINE_IDE_DATA_WAIT(rate), GDOX_MACHINE_IDE_DATA_WAIT(rate) } }, \
    { 0x01F0, 0x01F0, { GDOX_MACHINE_IDE_DATA_WAIT(rate), GDOX_MACHINE_IDE_DATA_WAIT(rate) } } }

#define GDOX_MACHINE_BUS_TIMING(waits, ports, rate) { GDOX_MACHINE_BUS_FIXED, &waits, NULL, 0, 0, 0, 0, 0, \
    { GDOX_MACHINE_ISA8(rate), GDOX_MACHINE_ISA8(rate) }, \
    { GDOX_MACHINE_ISA8_WORD(rate), GDOX_MACHINE_ISA8_WORD(rate) }, { 0, 0 }, \
    { GDOX_MACHINE_ISA16(rate), GDOX_MACHINE_ISA16(rate) }, { GDOX_MACHINE_ISA8(rate), GDOX_MACHINE_ISA8(rate) }, \
    GDOX_MACHINE_ISA8(rate), { 0, 0 }, ports }

const u8 k_machine_ram_386sx_20[] = { 1, 2, 4, 8, 16, 0 };
const u8 k_machine_ram_386dx_33[] = { 1, 2, 4, 8, 16, 0 };
const u8 k_machine_ram_486dx2_66[] = { 4, 8, 16, 32, 64, 0 };

const GDOX_Machine_Bus_Waits k_machine_bus_waits_386sx_20 = GDOX_MACHINE_BUS_WAITS(20000000);
const GDOX_Machine_Bus_Waits k_machine_bus_waits_386dx_33 = GDOX_MACHINE_BUS_WAITS(33000000);
const GDOX_Machine_Bus_Waits k_machine_bus_waits_486dx2_66 = GDOX_MACHINE_BUS_WAITS(66000000);

const GDOX_Machine_Port_Wait k_machine_port_waits_386sx_20[GDOX_MACHINE_PORT_WAIT_BLOCKS] = GDOX_MACHINE_PORT_WAITS(20000000);
const GDOX_Machine_Port_Wait k_machine_port_waits_386dx_33[GDOX_MACHINE_PORT_WAIT_BLOCKS] = GDOX_MACHINE_PORT_WAITS(33000000);
const GDOX_Machine_Port_Wait k_machine_port_waits_486dx2_66[GDOX_MACHINE_PORT_WAIT_BLOCKS] = GDOX_MACHINE_PORT_WAITS(66000000);

const GDOX_Machine_Bus_Timing k_machine_bus_386sx_20 =
    GDOX_MACHINE_BUS_TIMING(k_machine_bus_waits_386sx_20, k_machine_port_waits_386sx_20, 20000000);
const GDOX_Machine_Bus_Timing k_machine_bus_386dx_33 =
    GDOX_MACHINE_BUS_TIMING(k_machine_bus_waits_386dx_33, k_machine_port_waits_386dx_33, 33000000);
const GDOX_Machine_Bus_Timing k_machine_bus_486dx2_66 =
    GDOX_MACHINE_BUS_TIMING(k_machine_bus_waits_486dx2_66, k_machine_port_waits_486dx2_66, 66000000);

const GDOX_Machine_Memory_Map k_machine_memory_maps[GDOX_MACHINE_MAP_COUNT] =
{
    { "32-bit", 0xFFFFFFFF, 0xA0000 },
    { "24-bit", 0x00FFFFFF, 0xA0000 }
};

const GDOX_Machine_Profile k_machine_profiles[GDOX_MACHINE_COUNT] =
{
    { "386SX-20", "386sx_20", "Generic 386SX AT", "1989", true, GDOX_MACHINE_CPU_80386SX, GDOX_MACHINE_FPU_SOCKET, 20000000, GDOX_MACHINE_MAP_24BIT, 0, k_machine_ram_386sx_20, 2, 2, NULL, &k_machine_bus_386sx_20 },
    { "386DX-33", "386dx_33", "Generic 386DX AT", "1989", true, GDOX_MACHINE_CPU_80386DX, GDOX_MACHINE_FPU_SOCKET, 33000000, GDOX_MACHINE_MAP_32BIT, 0, k_machine_ram_386dx_33, 4, 2, NULL, &k_machine_bus_386dx_33 },
    { "486DX2-66", "486dx2_66", "Generic 486DX2 AT", "1992", true, GDOX_MACHINE_CPU_80486DX2, GDOX_MACHINE_FPU_INTEGRATED, 66000000, GDOX_MACHINE_MAP_32BIT, 0, k_machine_ram_486dx2_66, 8, 2, NULL, &k_machine_bus_486dx2_66 }
};

const GDOX_Machine_Model k_machine_release_order[GDOX_MACHINE_RELEASE_ORDER_COUNT] =
{
    GDOX_MACHINE_386SX_20, GDOX_MACHINE_386DX_33, GDOX_MACHINE_486DX2_66
};

const int k_machine_cpu_speeds_mhz[GDOX_MACHINE_CPU_SPEED_COUNT] = { 16, 20, 25, 33, 40, 50, 66, 100 };
const int k_machine_ram_sizes_mb[GDOX_MACHINE_RAM_SIZE_COUNT] = { 1, 2, 4, 8, 16, 32, 64 };

const GDOX_Machine_CPU_Info k_machine_cpus[GDOX_MACHINE_CPU_COUNT] =
{
    { "80386DX", true, 3, false, 0x00000300, 0x7FFFFFE0, 32 },
    { "80386SX", true, 3, false, 0x00002308, 0x7FFFFFF0, 16 },
    { "80486SX", true, 4, false, 0x00000420, 0x00000010, 32 },
    { "80486DX2", true, 4, true, 0x00000430, 0x00000010, 32 },
    { "Pentium", false, 5, true, 0x00000520, 0x00000010, 64 }
};

static inline const char* get_machine_fpu_name(GDOX_Machine_CPU cpu)
{
    if (k_machine_cpus[cpu].fpu)
        return "integrated";

    return k_machine_cpus[cpu].family >= 4 ? "487SX" : "80387";
}

#endif /* MACHINE_PROFILES_H */
