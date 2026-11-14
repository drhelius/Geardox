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

#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include "defines.h"

typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
typedef int64_t s64;

union u16_union
{
    u16 value;
    struct
    {
#ifdef GDOX_LITTLE_ENDIAN
        u8 low;
        u8 high;
#else
        u8 high;
        u8 low;
#endif
    };
};

union u32_union
{
    u32 value;
    struct
    {
#ifdef GDOX_LITTLE_ENDIAN
        u16 low;
        u16 high;
#else
        u16 high;
        u16 low;
#endif
    };
    struct
    {
#ifdef GDOX_LITTLE_ENDIAN
        u8 byte0;
        u8 byte1;
        u8 byte2;
        u8 byte3;
#else
        u8 byte3;
        u8 byte2;
        u8 byte1;
        u8 byte0;
#endif
    };
};

struct GDOX_Runtime_Info
{
    int screen_width;
    int screen_height;
    int display_width;
    int display_height;
    float frame_time;
    int sample_rate;
    bool paused;
};

enum GDOX_Run_Result
{
    GDOX_RUN_FRAME_READY = 0,
    GDOX_RUN_PAUSED,
    GDOX_RUN_NOT_READY
};

enum GDOX_Bus_Access_Origin
{
    GDOX_BUS_ORIGIN_CPU = 0,
    GDOX_BUS_ORIGIN_DMA
};

enum GDOX_Synchronize_Result
{
    GDOX_SYNC_NO_EVENTS = 0,
    GDOX_SYNC_EVENTS_AT_START,
    GDOX_SYNC_EVENTS_AT_ACCESS
};

struct GDOX_Bus_Access_Context;

typedef GDOX_Synchronize_Result (*GDOX_Synchronize_Hardware_Fn)(void* core, GDOX_Bus_Access_Context& context,
    u32 start_cycles, u32 access_cycles);
typedef void (*GDOX_Synchronize_Memory_Fn)(void* core, GDOX_Bus_Access_Context& context);
typedef u32 (*GDOX_Poll_Port_Fn)(void* core, u16 port, int width);
typedef void (*GDOX_Observe_Memory_Write_Fn)(void* observer, u32 bus_address, u8 previous, u8 value);

struct GDOX_Bus_Access_Context
{
    u64 clocks;
    u32 synchronized_cycles;
    u32 wait_cycles;
    GDOX_Bus_Access_Origin origin;
    GDOX_Synchronize_Hardware_Fn synchronize;
    GDOX_Synchronize_Memory_Fn synchronize_memory;
    GDOX_Poll_Port_Fn poll_port;
    void* synchronize_context;
    GDOX_Observe_Memory_Write_Fn observe_memory_write;
    void* memory_write_context;
    bool end_batch;
};

enum GDOX_Firmware_Type
{
    GDOX_FIRMWARE_SYSTEM_BIOS = 0,
    GDOX_FIRMWARE_VGA_BIOS,
    GDOX_FIRMWARE_COUNT
};

struct GDOX_Firmware_Info
{
    char name[128];
    int size;
    u32 crc;
    bool loaded;
};

enum GDOX_Keys
{
    GDOX_KEY_NONE             = 0x00,
    GDOX_KEY_ESCAPE           = 0x01,
    GDOX_KEY_1                = 0x02,
    GDOX_KEY_2                = 0x03,
    GDOX_KEY_3                = 0x04,
    GDOX_KEY_4                = 0x05,
    GDOX_KEY_5                = 0x06,
    GDOX_KEY_6                = 0x07,
    GDOX_KEY_7                = 0x08,
    GDOX_KEY_8                = 0x09,
    GDOX_KEY_9                = 0x0A,
    GDOX_KEY_0                = 0x0B,
    GDOX_KEY_MINUS            = 0x0C,
    GDOX_KEY_EQUALS           = 0x0D,
    GDOX_KEY_BACKSPACE        = 0x0E,
    GDOX_KEY_TAB              = 0x0F,
    GDOX_KEY_Q                = 0x10,
    GDOX_KEY_W                = 0x11,
    GDOX_KEY_E                = 0x12,
    GDOX_KEY_R                = 0x13,
    GDOX_KEY_T                = 0x14,
    GDOX_KEY_Y                = 0x15,
    GDOX_KEY_U                = 0x16,
    GDOX_KEY_I                = 0x17,
    GDOX_KEY_O                = 0x18,
    GDOX_KEY_P                = 0x19,
    GDOX_KEY_LEFT_BRACKET     = 0x1A,
    GDOX_KEY_RIGHT_BRACKET    = 0x1B,
    GDOX_KEY_RETURN           = 0x1C,
    GDOX_KEY_LEFT_CTRL        = 0x1D,
    GDOX_KEY_A                = 0x1E,
    GDOX_KEY_S                = 0x1F,
    GDOX_KEY_D                = 0x20,
    GDOX_KEY_F                = 0x21,
    GDOX_KEY_G                = 0x22,
    GDOX_KEY_H                = 0x23,
    GDOX_KEY_J                = 0x24,
    GDOX_KEY_K                = 0x25,
    GDOX_KEY_L                = 0x26,
    GDOX_KEY_SEMICOLON        = 0x27,
    GDOX_KEY_APOSTROPHE       = 0x28,
    GDOX_KEY_GRAVE            = 0x29,
    GDOX_KEY_LEFT_SHIFT       = 0x2A,
    GDOX_KEY_BACKSLASH        = 0x2B,
    GDOX_KEY_Z                = 0x2C,
    GDOX_KEY_X                = 0x2D,
    GDOX_KEY_C                = 0x2E,
    GDOX_KEY_V                = 0x2F,
    GDOX_KEY_B                = 0x30,
    GDOX_KEY_N                = 0x31,
    GDOX_KEY_M                = 0x32,
    GDOX_KEY_COMMA            = 0x33,
    GDOX_KEY_PERIOD           = 0x34,
    GDOX_KEY_SLASH            = 0x35,
    GDOX_KEY_RIGHT_SHIFT      = 0x36,
    GDOX_KEY_KP_MULTIPLY      = 0x37,
    GDOX_KEY_LEFT_ALT         = 0x38,
    GDOX_KEY_SPACE            = 0x39,
    GDOX_KEY_CAPS_LOCK        = 0x3A,
    GDOX_KEY_F1               = 0x3B,
    GDOX_KEY_F2               = 0x3C,
    GDOX_KEY_F3               = 0x3D,
    GDOX_KEY_F4               = 0x3E,
    GDOX_KEY_F5               = 0x3F,
    GDOX_KEY_F6               = 0x40,
    GDOX_KEY_F7               = 0x41,
    GDOX_KEY_F8               = 0x42,
    GDOX_KEY_F9               = 0x43,
    GDOX_KEY_F10              = 0x44,
    GDOX_KEY_NUM_LOCK         = 0x45,
    GDOX_KEY_SCROLL_LOCK      = 0x46,
    GDOX_KEY_KP_7             = 0x47,
    GDOX_KEY_KP_8             = 0x48,
    GDOX_KEY_KP_9             = 0x49,
    GDOX_KEY_KP_MINUS         = 0x4A,
    GDOX_KEY_KP_4             = 0x4B,
    GDOX_KEY_KP_5             = 0x4C,
    GDOX_KEY_KP_6             = 0x4D,
    GDOX_KEY_KP_PLUS          = 0x4E,
    GDOX_KEY_KP_1             = 0x4F,
    GDOX_KEY_KP_2             = 0x50,
    GDOX_KEY_KP_3             = 0x51,
    GDOX_KEY_KP_0             = 0x52,
    GDOX_KEY_KP_PERIOD        = 0x53,
    GDOX_KEY_SYSRQ            = 0x54,
    GDOX_KEY_NON_US_BACKSLASH = 0x56,
    GDOX_KEY_F11              = 0x57,
    GDOX_KEY_F12              = 0x58,
    GDOX_KEY_KP_ENTER         = 0x9C,
    GDOX_KEY_RIGHT_CTRL       = 0x9D,
    GDOX_KEY_KP_DIVIDE        = 0xB5,
    GDOX_KEY_PRINT_SCREEN     = 0xB7,
    GDOX_KEY_RIGHT_ALT        = 0xB8,
    GDOX_KEY_PAUSE            = 0xC5,
    GDOX_KEY_HOME             = 0xC7,
    GDOX_KEY_UP               = 0xC8,
    GDOX_KEY_PAGE_UP          = 0xC9,
    GDOX_KEY_LEFT             = 0xCB,
    GDOX_KEY_RIGHT            = 0xCD,
    GDOX_KEY_END              = 0xCF,
    GDOX_KEY_DOWN             = 0xD0,
    GDOX_KEY_PAGE_DOWN        = 0xD1,
    GDOX_KEY_INSERT           = 0xD2,
    GDOX_KEY_DELETE           = 0xD3,
    GDOX_KEY_LEFT_GUI         = 0xDB,
    GDOX_KEY_RIGHT_GUI        = 0xDC,
    GDOX_KEY_MENU             = 0xDD,
    GDOX_KEY_COUNT            = 0x100
};


enum GDOX_Mouse_Type
{
    GDOX_MOUSE_NONE = 0,
    GDOX_MOUSE_PS2,
    GDOX_MOUSE_SERIAL,
    GDOX_MOUSE_TYPE_COUNT
};

enum GDOX_Mouse_Buttons
{
    GDOX_MOUSE_LEFT   = 0x01,
    GDOX_MOUSE_RIGHT  = 0x02,
    GDOX_MOUSE_MIDDLE = 0x04
};

struct GDOX_Mouse_Info
{
    const char* name;
    const char* id;
};

enum GDOX_CdRom_Position
{
    GDOX_CDROM_NONE = 0,
    GDOX_CDROM_PRIMARY_MASTER,
    GDOX_CDROM_PRIMARY_SLAVE,
    GDOX_CDROM_SECONDARY_MASTER,
    GDOX_CDROM_SECONDARY_SLAVE,
    GDOX_CDROM_POSITION_COUNT
};

enum GDOX_CdRom_Speed
{
    GDOX_CDROM_SPEED_1X = 0,
    GDOX_CDROM_SPEED_2X,
    GDOX_CDROM_SPEED_4X,
    GDOX_CDROM_SPEED_8X,
    GDOX_CDROM_SPEED_COUNT
};

enum GDOX_Drive_Timing
{
    GDOX_DRIVE_TIMING_REAL = 0,
    GDOX_DRIVE_TIMING_FAST,
    GDOX_DRIVE_TIMING_COUNT
};

struct GDOX_CdRom_Info
{
    const char* name;
    const char* id;
};

struct GDOX_Drive_Timing_Info
{
    const char* name;
    const char* id;
};

enum GDOX_MIDI_Synth
{
    GDOX_MIDI_SYNTH_GM = 0,
    GDOX_MIDI_SYNTH_MT32,
    GDOX_MIDI_SYNTH_COUNT
};

struct GDOX_MIDI_Synth_Info
{
    const char* name;
    const char* id;
};

enum GDOX_Joystick_Type
{
    GDOX_JOYSTICK_NONE = 0,
    GDOX_JOYSTICK_STANDARD,
    GDOX_JOYSTICK_GAMEPAD,
    GDOX_JOYSTICK_FLIGHTSTICK,
    GDOX_JOYSTICK_TYPE_COUNT
};

enum GDOX_Joystick_Axis
{
    GDOX_JOYSTICK_AXIS_AX = 0,
    GDOX_JOYSTICK_AXIS_AY,
    GDOX_JOYSTICK_AXIS_BX,
    GDOX_JOYSTICK_AXIS_BY,
    GDOX_JOYSTICK_AXIS_COUNT
};

enum GDOX_Joystick_Buttons
{
    GDOX_JOYSTICK_BUTTON_1 = 0x01,
    GDOX_JOYSTICK_BUTTON_2 = 0x02,
    GDOX_JOYSTICK_BUTTON_3 = 0x04,
    GDOX_JOYSTICK_BUTTON_4 = 0x08
};

enum GDOX_Joystick_Hat
{
    GDOX_JOYSTICK_HAT_UP    = 0x01,
    GDOX_JOYSTICK_HAT_DOWN  = 0x02,
    GDOX_JOYSTICK_HAT_LEFT  = 0x04,
    GDOX_JOYSTICK_HAT_RIGHT = 0x08
};

struct GDOX_Joystick_State
{
    s16 axes[GDOX_JOYSTICK_AXIS_COUNT];
    u8 buttons;
    u8 hat;
};

struct GDOX_Joystick_Info
{
    const char* name;
    const char* id;
};

enum GDOX_Machine_Model
{
    GDOX_MACHINE_386SX_20 = 0,
    GDOX_MACHINE_386DX_33,
    GDOX_MACHINE_486DX2_66,
    GDOX_MACHINE_486SX_33,
    GDOX_MACHINE_486DX4_100,
    GDOX_MACHINE_COUNT
};

enum GDOX_Machine_CPU
{
    GDOX_MACHINE_CPU_80386DX = 0,
    GDOX_MACHINE_CPU_80386SX,
    GDOX_MACHINE_CPU_80486SX,
    GDOX_MACHINE_CPU_80486DX2,
    GDOX_MACHINE_CPU_80486DX4,
    GDOX_MACHINE_CPU_PENTIUM,
    GDOX_MACHINE_CPU_COUNT
};

enum GDOX_Machine_FPU
{
    GDOX_MACHINE_FPU_NONE = 0,
    GDOX_MACHINE_FPU_SOCKET,
    GDOX_MACHINE_FPU_INTEGRATED
};

enum GDOX_Machine_Map
{
    GDOX_MACHINE_MAP_32BIT = 0,
    GDOX_MACHINE_MAP_24BIT,
    GDOX_MACHINE_MAP_COUNT
};

enum GDOX_Boot_Device
{
    GDOX_BOOT_DEVICE_AUTO = 0,
    GDOX_BOOT_DEVICE_FLOPPY_0,
    GDOX_BOOT_DEVICE_FLOPPY_1,
    GDOX_BOOT_DEVICE_HARD_DISK,
    GDOX_BOOT_DEVICE_CDROM,
    GDOX_BOOT_DEVICE_COUNT
};

enum GDOX_Machine_Source
{
    GDOX_MACHINE_SOURCE_USER = 0,
    GDOX_MACHINE_SOURCE_GAME,
    GDOX_MACHINE_SOURCE_MODEL,
    GDOX_MACHINE_SOURCE_MEDIA
};

struct GDOX_Machine_CPU_Info
{
    const char* name;
    bool emulated;
    u8 family;
    bool fpu;
    u32 reset_edx;
    u32 cr0_fixed;
    int bus_width;
    u8 bus_ratio;
    u8 cache_kb;
    bool cpuid;
    bool fast_imul;
};

struct GDOX_Machine_Memory_Map
{
    const char* name;
    u32 address_mask;
    u32 conventional_size;
};

enum GDOX_Bus_Class
{
    GDOX_BUS_NONE = 0,
    GDOX_BUS_DRAM,
    GDOX_BUS_VRAM,
    GDOX_BUS_SPRITE,
    GDOX_BUS_ROM,
    GDOX_BUS_CMOS,
    GDOX_BUS_ISA8,
    GDOX_BUS_PCM,
    GDOX_BUS_MMIO,
    GDOX_BUS_CLASS_COUNT
};

#define GDOX_MACHINE_PORT_WAIT_BLOCKS 9

struct GDOX_Machine_Port_Wait
{
    u16 first;
    u16 last;
    s16 extra[2];
};

// In 1/256 clocks at the model's clock, reads first
// A follow adds to the wait of each later cycle of a read split in cycles, which overlaps the one before
// The 486's cache: the average wait of a read and of a write it serves, and the fill of a code line it misses
struct GDOX_Machine_Bus_Waits
{
    u16 dram_far[2];
    u16 dram_near[2];
    u16 dram_change[2];
    u16 dram_transfer;
    u16 dram_store_load;
    u16 dram_cycle[2];
    u16 vram_cycle[2];
    u16 sprite_cycle[2];
    u16 rom_cycle;
    u16 rom_code;
    u16 cmos_cycle[2];
    s16 vram_follow;
    s16 sprite_follow;
    s16 rom_follow;
    u16 cache_read;
    u16 cache_write;
    u16 cache_fill;
};

struct GDOX_Machine_Bus_Timing
{
    const GDOX_Machine_Bus_Waits* compatible;
    u16 refresh;
    u16 refill;
    u16 misalign;
    u16 string_row;
    u16 string_read;
    u16 isa8_byte[2];
    u16 isa8_word[2];
    u16 pcm[2];
    u16 mmio[2];
    u16 io[2];
    u16 io_free_write;
    u16 segment_load[2];
    const GDOX_Machine_Port_Wait* port_waits;
};

struct GDOX_Machine_Profile
{
    const char* name;
    const char* key;
    const char* models;
    const char* released;
    bool emulated;
    GDOX_Machine_CPU cpu;
    GDOX_Machine_FPU fpu;
    u32 cpu_clock_rate;
    GDOX_Machine_Map map;
    u32 features;
    const u8* ram_sizes_mb;
    int floppy_drives;
    const GDOX_Machine_Bus_Timing* bus_timing;
};

struct GDOX_Machine_Config
{
    GDOX_Machine_Model model;
    GDOX_Machine_CPU cpu;
    u32 ram_size;
    int floppy_drives;
    u8 floppy_type[GDOX_MAX_FLOPPY_DRIVES];
    u32 cpu_clock_rate;
    bool ide;
    bool fpu;
    int speed_mode;
    u8 sound_card;
    u8 sound_irq;
    u8 sound_dma;
    bool mpu;
    u8 mouse;
    u8 cdrom;
    u8 cdrom_speed;
    u8 video_card;
    u8 drive_timing;
};

enum GDOX_Sound_Card
{
    GDOX_SOUND_CARD_NONE = 0,
    GDOX_SOUND_CARD_ADLIB,
    GDOX_SOUND_CARD_SB15,
    GDOX_SOUND_CARD_SB20,
    GDOX_SOUND_CARD_SBPRO,
    GDOX_SOUND_CARD_SBPRO2,
    GDOX_SOUND_CARD_SB16,
    GDOX_SOUND_CARD_COUNT
};

enum GDOX_Video_Card
{
    GDOX_VIDEO_CARD_VGA = 0,
    GDOX_VIDEO_CARD_GD5428,
    GDOX_VIDEO_CARD_GD5428_VLB,
    GDOX_VIDEO_CARD_COUNT
};

// A VL-Bus card's memory cycles, read and write, in bus clocks past the 2 of a bus cycle, in 1/256 clocks
struct GDOX_Video_Card_Info
{
    const char* name;
    const char* id;
    u32 memory_size;
    bool cirrus;
    bool local_bus;
    u16 local_bus_cycle[2];
};

struct GDOX_Sound_Card_Info
{
    const char* name;
    const char* id;
    u8 dsp_major;
    u8 dsp_minor;
    u8 opl_chips;
    bool opl3;
    u8 mixer;
    bool mpu;
};

enum GDOX_Floppy_Type
{
    GDOX_FLOPPY_NONE = 0,
    GDOX_FLOPPY_360K,
    GDOX_FLOPPY_1200K,
    GDOX_FLOPPY_720K,
    GDOX_FLOPPY_1440K,
    GDOX_FLOPPY_2880K,
    GDOX_FLOPPY_TYPE_COUNT
};

struct GDOX_Machine_Settings
{
    int model;
    int cpu_mhz;
    int ram_mb;
    int fpu;
    int speed_mode;
    int floppy_type[GDOX_MAX_FLOPPY_DRIVES];
    int sound_card;
    int sound_irq;
    int sound_dma;
    int mpu;
    int mouse;
    int cdrom;
    int cdrom_speed;
    int video_card;
    int drive_timing;
};

struct GDOX_Machine_Report
{
    GDOX_Machine_Source model;
    GDOX_Machine_Source cpu_clock;
    GDOX_Machine_Source ram;
    GDOX_Machine_Source fpu;
    GDOX_Machine_Source speed_mode;
    const char* title;
    int warning_count;
    char warnings[GDOX_MACHINE_WARNING_COUNT][GDOX_MACHINE_WARNING_SIZE];
};

struct GDOX_SaveState_Header
{
    u32 magic;
    u32 version;
    u32 size;
    s64 timestamp;
    char rom_name[128];
    u32 rom_crc;
    u32 screenshot_size;
    u16 screenshot_width;
    u16 screenshot_height;
    char emu_build[32];
};

struct GDOX_SaveState_Header_Libretro
{
    u32 magic;
    u32 version;
};

struct GDOX_SaveState_Media
{
    bool valid;
    bool floppy_inserted[GDOX_MAX_FLOPPY_DRIVES];
    u32 floppy_crc[GDOX_MAX_FLOPPY_DRIVES];
    bool cdrom_inserted;
    u32 cdrom_crc;
    bool hard_disk_attached[GDOX_MAX_HARD_DISKS];
    u32 hard_disk_blocks[GDOX_MAX_HARD_DISKS];
    u64 hard_disk_session[GDOX_MAX_HARD_DISKS];
    u64 hard_disk_write[GDOX_MAX_HARD_DISKS];
};

struct GDOX_SaveState_Screenshot
{
    u32 width;
    u32 height;
    u32 size;
    u8* data;
};

#endif /* TYPES_H */
