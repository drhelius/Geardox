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

#define GDOX_MACHINE_RELEASE_ORDER_COUNT 5
#define GDOX_MACHINE_CPU_SPEED_COUNT 8
#define GDOX_MACHINE_RAM_SIZE_COUNT 7
#define GDOX_MACHINE_FEATURE_LOCAL_BUS 0x01
#define GDOX_MACHINE_ISA_CLOCK_RATE 8333333

// ISA cycles in BCLKs at the model's bus clock, less the 2 clocks of a bus cycle, in 1/256 bus clocks
#define GDOX_MACHINE_ISA_WAIT(bclks, rate) ((u16)(((u64)((bclks) * 256.0 * (rate)) / GDOX_MACHINE_ISA_CLOCK_RATE) - 512))
#define GDOX_MACHINE_ISA16(rate) GDOX_MACHINE_ISA_WAIT(3, rate)
#define GDOX_MACHINE_ISA8(rate) GDOX_MACHINE_ISA_WAIT(6, rate)
#define GDOX_MACHINE_ISA8_WORD(rate) GDOX_MACHINE_ISA_WAIT(12, rate)

// DRAM read and write waits and a page miss in bus clocks, the VGA's cycle in BCLK, fitted to TOPBENCH's machines,
// 3D-Bench and Doom (R13 §4.5, R13B). A page miss: data outside the code's row, or a string element changing rows
// The 486's cache in bus clocks: the average waits of a read and a write it serves, and a code line's fill
#define GDOX_MACHINE_BUS_WAITS(rate, dram_read, dram_write, page_miss, vga, cache_read, cache_write, cache_fill) \
    { { (page_miss) * 256, (page_miss) * 256 }, { 0, 0 }, { 0, 0 }, 0, 0, { (dram_read) * 256, (dram_write) * 256 }, \
    { GDOX_MACHINE_ISA_WAIT(vga, rate), GDOX_MACHINE_ISA_WAIT(vga, rate) }, { 0, 0 }, GDOX_MACHINE_ISA16(rate), \
    GDOX_MACHINE_ISA16(rate), { 0, 0 }, 0, 0, 0, \
    (u16)((cache_read) * 256), (u16)((cache_write) * 256), (u16)((cache_fill) * 256) }

// The IDE data ports run a 16-bit cycle on a bus where every other port takes an 8-bit one
#define GDOX_MACHINE_IDE_DATA_WAIT(rate) ((s16)(GDOX_MACHINE_ISA16(rate) - GDOX_MACHINE_ISA8(rate)))
#define GDOX_MACHINE_PORT_WAITS(rate) { \
    { 0x0170, 0x0170, { GDOX_MACHINE_IDE_DATA_WAIT(rate), GDOX_MACHINE_IDE_DATA_WAIT(rate) } }, \
    { 0x01F0, 0x01F0, { GDOX_MACHINE_IDE_DATA_WAIT(rate), GDOX_MACHINE_IDE_DATA_WAIT(rate) } } }

#define GDOX_MACHINE_BUS_TIMING(waits, ports, rate, page_miss) { &waits, \
    0, 0, 0, (page_miss) * 256, 0, { GDOX_MACHINE_ISA8(rate), GDOX_MACHINE_ISA8(rate) }, \
    { GDOX_MACHINE_ISA8_WORD(rate), GDOX_MACHINE_ISA8_WORD(rate) }, { 0, 0 }, \
    { GDOX_MACHINE_ISA16(rate), GDOX_MACHINE_ISA16(rate) }, { GDOX_MACHINE_ISA8(rate), GDOX_MACHINE_ISA8(rate) }, \
    GDOX_MACHINE_ISA8(rate), { 0, 0 }, ports }

const u8 k_machine_ram_386sx_20[] = { 1, 2, 4, 8, 16, 0 };
const u8 k_machine_ram_386dx_33[] = { 1, 2, 4, 8, 16, 0 };
const u8 k_machine_ram_486[] = { 4, 8, 16, 32, 64, 0 };

// The 386SX at the 82C283's 1 and 1 with no page mode (p9, p11). The 386DX in page mode, no waits on a hit as the
// 82C302 (CS8230 p37, p39, p48). The 486s off their cache at the 82C495XLC's 3 and 2 (p28); a line the cache misses
// fills in r + 3b bus clocks, r the 2 clocks and 3 waits of a read and b 1 (DX2 data book p158, notes 6 and 12)
const GDOX_Machine_Bus_Waits k_machine_bus_waits_386sx_20 = GDOX_MACHINE_BUS_WAITS(20000000, 1, 1, 0, 6, 0, 0, 0);
const GDOX_Machine_Bus_Waits k_machine_bus_waits_386dx_33 = GDOX_MACHINE_BUS_WAITS(33000000, 0, 0, 3, 6, 0, 0, 0);
const GDOX_Machine_Bus_Waits k_machine_bus_waits_486 = GDOX_MACHINE_BUS_WAITS(33000000, 3, 2, 0, 3.5, 0.375, 0.75, 8);
const GDOX_Machine_Bus_Waits k_machine_bus_waits_486dx4 = GDOX_MACHINE_BUS_WAITS(33333333, 3, 2, 0, 3.5, 0.375, 0.75, 8);

const GDOX_Machine_Port_Wait k_machine_port_waits_386sx_20[GDOX_MACHINE_PORT_WAIT_BLOCKS] = GDOX_MACHINE_PORT_WAITS(20000000);
const GDOX_Machine_Port_Wait k_machine_port_waits_386dx_33[GDOX_MACHINE_PORT_WAIT_BLOCKS] = GDOX_MACHINE_PORT_WAITS(33000000);
const GDOX_Machine_Port_Wait k_machine_port_waits_486[GDOX_MACHINE_PORT_WAIT_BLOCKS] = GDOX_MACHINE_PORT_WAITS(33000000);
const GDOX_Machine_Port_Wait k_machine_port_waits_486dx4[GDOX_MACHINE_PORT_WAIT_BLOCKS] = GDOX_MACHINE_PORT_WAITS(33333333);

const GDOX_Machine_Bus_Timing k_machine_bus_386sx_20 =
    GDOX_MACHINE_BUS_TIMING(k_machine_bus_waits_386sx_20, k_machine_port_waits_386sx_20, 20000000, 0);
const GDOX_Machine_Bus_Timing k_machine_bus_386dx_33 =
    GDOX_MACHINE_BUS_TIMING(k_machine_bus_waits_386dx_33, k_machine_port_waits_386dx_33, 33000000, 3);
const GDOX_Machine_Bus_Timing k_machine_bus_486 =
    GDOX_MACHINE_BUS_TIMING(k_machine_bus_waits_486, k_machine_port_waits_486, 33000000, 0);
const GDOX_Machine_Bus_Timing k_machine_bus_486dx4 =
    GDOX_MACHINE_BUS_TIMING(k_machine_bus_waits_486dx4, k_machine_port_waits_486dx4, 33333333, 0);

const GDOX_Machine_Memory_Map k_machine_memory_maps[GDOX_MACHINE_MAP_COUNT] =
{
    { "32-bit", 0xFFFFFFFF, 0xA0000 },
    { "24-bit", 0x00FFFFFF, 0xA0000 }
};

const GDOX_Machine_Profile k_machine_profiles[GDOX_MACHINE_COUNT] =
{
    { "386SX-20", "386sx_20", "Generic 386SX AT", "1989", true, GDOX_MACHINE_CPU_80386SX, GDOX_MACHINE_FPU_SOCKET, 20000000, GDOX_MACHINE_MAP_24BIT, 0, k_machine_ram_386sx_20, 2, &k_machine_bus_386sx_20 },
    { "386DX-33", "386dx_33", "Generic 386DX AT", "1989", true, GDOX_MACHINE_CPU_80386DX, GDOX_MACHINE_FPU_SOCKET, 33000000, GDOX_MACHINE_MAP_32BIT, 0, k_machine_ram_386dx_33, 2, &k_machine_bus_386dx_33 },
    { "486DX2-66", "486dx2_66", "Generic 486DX2 AT", "1992", true, GDOX_MACHINE_CPU_80486DX2, GDOX_MACHINE_FPU_INTEGRATED, 66000000, GDOX_MACHINE_MAP_32BIT, GDOX_MACHINE_FEATURE_LOCAL_BUS, k_machine_ram_486, 2, &k_machine_bus_486 },
    { "486SX-33", "486sx_33", "Generic 486SX AT", "1992", true, GDOX_MACHINE_CPU_80486SX, GDOX_MACHINE_FPU_SOCKET, 33000000, GDOX_MACHINE_MAP_32BIT, GDOX_MACHINE_FEATURE_LOCAL_BUS, k_machine_ram_486, 2, &k_machine_bus_486 },
    { "486DX4-100", "486dx4_100", "Generic 486DX4 AT", "1994", true, GDOX_MACHINE_CPU_80486DX4, GDOX_MACHINE_FPU_INTEGRATED, 100000000, GDOX_MACHINE_MAP_32BIT, GDOX_MACHINE_FEATURE_LOCAL_BUS, k_machine_ram_486, 2, &k_machine_bus_486dx4 }
};

// The ISA cards keep the model's VGA waits. The VL-Bus card's: reads at 16 bus clocks fit TOPBENCH's video RAM column
// to its VL-Bus Cirrus DX2-66 rows, writes at 5 give the change one DX2-66 board saw from ISA to VL-Bus, Doom x1.27 and
// 3D-Bench x1.22 (R14 §8.2)
const GDOX_Video_Card_Info k_video_cards[GDOX_VIDEO_CARD_COUNT] =
{
    { "VGA", "vga", 0x40000, GDOX_VIDEO_FAMILY_VGA, false, { 0, 0 }, false, { 0, 0 } },
    { "SVGA Cirrus GD5428 ISA", "gd5428", 0x100000, GDOX_VIDEO_FAMILY_CIRRUS, false, { 0, 0 }, false, { 0, 0 } },
    { "SVGA Cirrus GD5428 VL-Bus", "gd5428_vlb", 0x100000, GDOX_VIDEO_FAMILY_CIRRUS, true, { 16 * 256, 5 * 256 }, false, { 0, 0 } },
    { "IBM CGA", "cga", 0x4000, GDOX_VIDEO_FAMILY_CGA, false, { 0, 0 }, true, { 740, 740 } },
    { "IBM EGA", "ega", 0x40000, GDOX_VIDEO_FAMILY_EGA, false, { 0, 0 }, true, { 1270, 1270 } }
};

const GDOX_Sound_Card_Info k_sound_cards[GDOX_SOUND_CARD_COUNT] =
{
    { "None", "none", 0, 0, 0, false, 0, false },
    { "AdLib", "adlib", 0, 0, 1, false, 0, false },
    { "Sound Blaster 1.5", "sb15", 1, 5, 1, false, 0, false },
    { "Sound Blaster 2.0", "sb20", 2, 1, 1, false, 0, false },
    { "Sound Blaster Pro", "sbpro", 3, 0, 2, false, 1, false },
    { "Sound Blaster Pro 2", "sbpro2", 3, 2, 1, true, 1, false },
    { "Sound Blaster 16", "sb16", 4, 5, 1, true, 2, true }
};

const u8 k_sound_card_irqs[2] = { 5, 7 };
const u8 k_sound_card_dmas[2] = { 1, 3 };

const GDOX_Mouse_Info k_mice[GDOX_MOUSE_TYPE_COUNT] =
{
    { "None", "none" },
    { "PS/2", "ps2" },
    { "Serial (COM1)", "serial" }
};

const GDOX_CdRom_Info k_cdrom_positions[GDOX_CDROM_POSITION_COUNT] =
{
    { "None", "none" },
    { "Primary Master", "pm" },
    { "Primary Slave", "ps" },
    { "Secondary Master", "sm" },
    { "Secondary Slave", "ss" }
};

const GDOX_CdRom_Info k_cdrom_speeds[GDOX_CDROM_SPEED_COUNT] =
{
    { "1x", "1x" },
    { "2x", "2x" },
    { "4x", "4x" },
    { "8x", "8x" }
};

const int k_cdrom_speed_factors[GDOX_CDROM_SPEED_COUNT] = { 1, 2, 4, 8 };

const GDOX_CGA_Output_Info k_cga_outputs[GDOX_CGA_OUTPUT_COUNT] =
{
    { "Auto", "auto" },
    { "RGB Monitor", "rgb" },
    { "Composite Monitor", "composite" }
};

const GDOX_Drive_Timing_Info k_drive_timings[GDOX_DRIVE_TIMING_COUNT] =
{
    { "Real", "real" },
    { "Fast", "fast" }
};

// Fast drives seek, wait for the sector, settle and spin up in a sixteenth of the time. Transfers keep their rate
const u32 k_drive_timing_dividers[GDOX_DRIVE_TIMING_COUNT] = { 1, 16 };

const GDOX_MIDI_Synth_Info k_midi_synths[GDOX_MIDI_SYNTH_COUNT] =
{
    { "General MIDI", "gm" },
    { "MT-32", "mt32" }
};

const GDOX_Joystick_Info k_joysticks[GDOX_JOYSTICK_TYPE_COUNT] =
{
    { "None", "none" },
    { "Joystick", "standard" },
    { "Gravis GamePad", "gamepad" },
    { "CH FlightStick Pro", "flightstick" }
};

const GDOX_Machine_Model k_machine_default_model = GDOX_MACHINE_486DX2_66;
const int k_machine_default_ram_mb = 8;

const GDOX_Machine_Model k_machine_release_order[GDOX_MACHINE_RELEASE_ORDER_COUNT] =
{
    GDOX_MACHINE_386SX_20, GDOX_MACHINE_386DX_33, GDOX_MACHINE_486SX_33, GDOX_MACHINE_486DX2_66, GDOX_MACHINE_486DX4_100
};

const int k_machine_cpu_speeds_mhz[GDOX_MACHINE_CPU_SPEED_COUNT] = { 16, 20, 25, 33, 40, 50, 66, 100 };
const int k_machine_ram_sizes_mb[GDOX_MACHINE_RAM_SIZE_COUNT] = { 1, 2, 4, 8, 16, 32, 64 };

const GDOX_Machine_CPU_Info k_machine_cpus[GDOX_MACHINE_CPU_COUNT] =
{
    { "80386DX", true, 3, false, 0x00000308, 0x7FFFFFE0, 32, 1, 0, false, false },
    { "80386SX", true, 3, false, 0x00002308, 0x7FFFFFF0, 16, 1, 0, false, false },
    { "80486SX", true, 4, false, 0x00000420, 0x00000010, 32, 1, 8, false, false },
    { "80486DX2", true, 4, true, 0x00000433, 0x00000010, 32, 2, 8, false, false },
    { "80486DX4", true, 4, true, 0x00000480, 0x00000010, 32, 3, 16, true, true },
    { "Pentium", false, 5, true, 0x00000520, 0x00000010, 64, 1, 16, true, false }
};

static inline void clear_machine_settings(GDOX_Machine_Settings* settings)
{
    settings->model = GDOX_MACHINE_AUTO;
    settings->cpu_mhz = GDOX_MACHINE_AUTO;
    settings->ram_mb = GDOX_MACHINE_AUTO;
    settings->fpu = GDOX_MACHINE_AUTO;
    settings->speed_mode = GDOX_MACHINE_AUTO;

    for (int i = 0; i < GDOX_MAX_FLOPPY_DRIVES; i++)
        settings->floppy_type[i] = GDOX_MACHINE_AUTO;

    settings->sound_card = GDOX_MACHINE_AUTO;
    settings->sound_irq = GDOX_MACHINE_AUTO;
    settings->sound_dma = GDOX_MACHINE_AUTO;
    settings->mpu = GDOX_MACHINE_AUTO;
    settings->mouse = GDOX_MACHINE_AUTO;
    settings->cdrom = GDOX_MACHINE_AUTO;
    settings->cdrom_speed = GDOX_MACHINE_AUTO;
    settings->drive_timing = GDOX_MACHINE_AUTO;
    settings->video_card = GDOX_MACHINE_AUTO;
}

static inline const char* get_machine_fpu_name(GDOX_Machine_CPU cpu)
{
    if (k_machine_cpus[cpu].fpu)
        return "Integrated";

    return k_machine_cpus[cpu].family >= 4 ? "487SX" : "80387";
}

#endif /* MACHINE_PROFILES_H */
