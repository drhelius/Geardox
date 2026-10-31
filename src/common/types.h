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
    bool media_ready;
    bool bios_ready;
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
    GDOX_FIRMWARE_SYSTEM = 0,
    GDOX_FIRMWARE_OS,
    GDOX_FIRMWARE_FONT,
    GDOX_FIRMWARE_DICTIONARY,
    GDOX_FIRMWARE_FONT20,
    GDOX_FIRMWARE_SERIAL,
    GDOX_FIRMWARE_EXTENSION,
    GDOX_FIRMWARE_COUNT
};

struct GDOX_Firmware_Info
{
    char path[GDOX_MAX_PATH];
    char database_name[128];
    int size;
    u32 crc;
    bool loaded;
    bool recognized;
    bool synthetic;
};

enum GDOX_Firmware_Source_Type
{
    GDOX_FIRMWARE_SOURCE_NONE = 0,
    GDOX_FIRMWARE_SOURCE_MODEL_FOLDER,
    GDOX_FIRMWARE_SOURCE_OWN_SET,
    GDOX_FIRMWARE_SOURCE_COMPATIBLE_SET,
    GDOX_FIRMWARE_SOURCE_UNKNOWN,
    GDOX_FIRMWARE_SOURCE_OTHER_SET
};

struct GDOX_Firmware_Source
{
    GDOX_Firmware_Source_Type type;
    int set;
    bool zip;
    u32 crc;
    char path[GDOX_MAX_PATH];
};

struct GDOX_Firmware_Set
{
    const char* name;
    const char* description;
    u32 system_crc;
    u32 models;
    u32 compatible_models;
};

struct GDOX_Color
{
    u8 red;
    u8 green;
    u8 blue;
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
    GDOX_KEY_CARET            = 0x0D,
    GDOX_KEY_YEN              = 0x0E,
    GDOX_KEY_BACKSPACE        = 0x0F,
    GDOX_KEY_TAB              = 0x10,
    GDOX_KEY_Q                = 0x11,
    GDOX_KEY_W                = 0x12,
    GDOX_KEY_E                = 0x13,
    GDOX_KEY_R                = 0x14,
    GDOX_KEY_T                = 0x15,
    GDOX_KEY_Y                = 0x16,
    GDOX_KEY_U                = 0x17,
    GDOX_KEY_I                = 0x18,
    GDOX_KEY_O                = 0x19,
    GDOX_KEY_P                = 0x1A,
    GDOX_KEY_AT               = 0x1B,
    GDOX_KEY_LEFT_BRACKET     = 0x1C,
    GDOX_KEY_RETURN           = 0x1D,
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
    GDOX_KEY_COLON            = 0x28,
    GDOX_KEY_RIGHT_BRACKET    = 0x29,
    GDOX_KEY_Z                = 0x2A,
    GDOX_KEY_X                = 0x2B,
    GDOX_KEY_C                = 0x2C,
    GDOX_KEY_V                = 0x2D,
    GDOX_KEY_B                = 0x2E,
    GDOX_KEY_N                = 0x2F,
    GDOX_KEY_M                = 0x30,
    GDOX_KEY_COMMA            = 0x31,
    GDOX_KEY_PERIOD           = 0x32,
    GDOX_KEY_SLASH            = 0x33,
    GDOX_KEY_UNDERSCORE       = 0x34,
    GDOX_KEY_SPACE            = 0x35,
    GDOX_KEY_KP_MULTIPLY      = 0x36,
    GDOX_KEY_KP_DIVIDE        = 0x37,
    GDOX_KEY_KP_PLUS          = 0x38,
    GDOX_KEY_KP_MINUS         = 0x39,
    GDOX_KEY_KP_7             = 0x3A,
    GDOX_KEY_KP_8             = 0x3B,
    GDOX_KEY_KP_9             = 0x3C,
    GDOX_KEY_KP_EQUALS        = 0x3D,
    GDOX_KEY_KP_4             = 0x3E,
    GDOX_KEY_KP_5             = 0x3F,
    GDOX_KEY_KP_6             = 0x40,
    GDOX_KEY_KP_1             = 0x42,
    GDOX_KEY_KP_2             = 0x43,
    GDOX_KEY_KP_3             = 0x44,
    GDOX_KEY_KP_ENTER         = 0x45,
    GDOX_KEY_KP_0             = 0x46,
    GDOX_KEY_KP_PERIOD        = 0x47,
    GDOX_KEY_INSERT           = 0x48,
    GDOX_KEY_KP_000           = 0x4A,
    GDOX_KEY_DELETE           = 0x4B,
    GDOX_KEY_UP               = 0x4D,
    GDOX_KEY_HOME             = 0x4E,
    GDOX_KEY_LEFT             = 0x4F,
    GDOX_KEY_DOWN             = 0x50,
    GDOX_KEY_RIGHT            = 0x51,
    GDOX_KEY_CTRL             = 0x52,
    GDOX_KEY_SHIFT            = 0x53,
    GDOX_KEY_CAPS             = 0x55,
    GDOX_KEY_HIRAGANA         = 0x56,
    GDOX_KEY_NO_CONVERT       = 0x57,
    GDOX_KEY_CONVERT          = 0x58,
    GDOX_KEY_KANA_KANJI       = 0x59,
    GDOX_KEY_KATAKANA         = 0x5A,
    GDOX_KEY_PF12             = 0x5B,
    GDOX_KEY_ALT              = 0x5C,
    GDOX_KEY_PF1              = 0x5D,
    GDOX_KEY_PF2              = 0x5E,
    GDOX_KEY_PF3              = 0x5F,
    GDOX_KEY_PF4              = 0x60,
    GDOX_KEY_PF5              = 0x61,
    GDOX_KEY_PF6              = 0x62,
    GDOX_KEY_PF7              = 0x63,
    GDOX_KEY_PF8              = 0x64,
    GDOX_KEY_PF9              = 0x65,
    GDOX_KEY_PF10             = 0x66,
    GDOX_KEY_PF11             = 0x69,
    GDOX_KEY_KANJI_DICTIONARY = 0x6B,
    GDOX_KEY_DELETE_WORD      = 0x6C,
    GDOX_KEY_ADD_WORD         = 0x6D,
    GDOX_KEY_PREVIOUS         = 0x6E,
    GDOX_KEY_NEXT             = 0x70,
    GDOX_KEY_HALF_FULL        = 0x71,
    GDOX_KEY_CANCEL           = 0x72,
    GDOX_KEY_EXECUTE          = 0x73,
    GDOX_KEY_PF13             = 0x74,
    GDOX_KEY_PF14             = 0x75,
    GDOX_KEY_PF15             = 0x76,
    GDOX_KEY_PF16             = 0x77,
    GDOX_KEY_PF17             = 0x78,
    GDOX_KEY_PF18             = 0x79,
    GDOX_KEY_PF19             = 0x7A,
    GDOX_KEY_PF20             = 0x7B,
    GDOX_KEY_BREAK            = 0x7C,
    GDOX_KEY_COPY             = 0x7D,
    GDOX_KEY_COUNT            = 0x80
};

enum GDOX_Controller_Type
{
    GDOX_CONTROLLER_NONE = 0,
    GDOX_CONTROLLER_ORIGINAL_GAMEPAD,
    GDOX_CONTROLLER_MARTY_GAMEPAD,
    GDOX_CONTROLLER_6_BUTTON_GAMEPAD,
    GDOX_CONTROLLER_MOUSE
};

enum GDOX_Controllers
{
    GDOX_CONTROLLER_1 = 0,
    GDOX_CONTROLLER_2
};

enum GDOX_GamePad_Buttons
{
    GDOX_GAMEPAD_UP       = 0x0001,
    GDOX_GAMEPAD_DOWN     = 0x0002,
    GDOX_GAMEPAD_LEFT     = 0x0004,
    GDOX_GAMEPAD_RIGHT    = 0x0008,
    GDOX_GAMEPAD_SELECT   = 0x0010,
    GDOX_GAMEPAD_RUN      = 0x0020,
    GDOX_GAMEPAD_A        = 0x0040,
    GDOX_GAMEPAD_B        = 0x0080,
    GDOX_GAMEPAD_C        = 0x0100,
    GDOX_GAMEPAD_X        = 0x0200,
    GDOX_GAMEPAD_Y        = 0x0400,
    GDOX_GAMEPAD_Z        = 0x0800,
    GDOX_GAMEPAD_ZOOM     = 0x1000
};

struct GDOX_GamePad_State
{
    u16 buttons;
};

enum GDOX_Machine_Model
{
    GDOX_MACHINE_MODEL1_2 = 0,
    GDOX_MACHINE_1F_2F,
    GDOX_MACHINE_10F_20F,
    GDOX_MACHINE_CX,
    GDOX_MACHINE_UX,
    GDOX_MACHINE_UG,
    GDOX_MACHINE_HG,
    GDOX_MACHINE_HR,
    GDOX_MACHINE_UR,
    GDOX_MACHINE_ME,
    GDOX_MACHINE_MA,
    GDOX_MACHINE_MX,
    GDOX_MACHINE_MF_FRESH,
    GDOX_MACHINE_HC,
    GDOX_MACHINE_MARTY,
    GDOX_MACHINE_COUNT
};

enum GDOX_Machine_CPU
{
    GDOX_MACHINE_CPU_80386DX = 0,
    GDOX_MACHINE_CPU_80386SX,
    GDOX_MACHINE_CPU_80486SX,
    GDOX_MACHINE_CPU_80486DX2,
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
    GDOX_MACHINE_MAP_MARTY,
    GDOX_MACHINE_MAP_COUNT
};

enum GDOX_Machine_Feature
{
    GDOX_MACHINE_FEATURE_RAM_SIZE        = 0x0001,
    GDOX_MACHINE_FEATURE_FDD_STATUS      = 0x0002,
    GDOX_MACHINE_FEATURE_WAIT_1US        = 0x0004,
    GDOX_MACHINE_FEATURE_INTERVAL_TIMER2 = 0x0008,
    GDOX_MACHINE_FEATURE_MEMORY_WAIT     = 0x0010,
    GDOX_MACHINE_FEATURE_NMI_MASK        = 0x0020,
    GDOX_MACHINE_FEATURE_JEIDA4          = 0x0040,
    GDOX_MACHINE_FEATURE_LSPEED          = 0x0080,
    GDOX_MACHINE_FEATURE_POWER_OFF_RESET = 0x0100,
    GDOX_MACHINE_FEATURE_SPEED_CONTROL   = 0x0200,
    GDOX_MACHINE_FEATURE_UG_REGISTERS    = 0x0400,
    GDOX_MACHINE_FEATURE_FREE_RUN_TIMER  = 0x0800,
    GDOX_MACHINE_FEATURE_CRT_POWER_OFF   = 0x1000,
    GDOX_MACHINE_FEATURE_RAM_CARD        = 0x2000,
    GDOX_MACHINE_FEATURE_MARTY_PORTS     = 0x4000,
    GDOX_MACHINE_FEATURE_FDD_NO_TYPE     = 0x8000,
    GDOX_MACHINE_FEATURE_TIMER1_LATCHED  = 0x10000,
    GDOX_MACHINE_FEATURE_SCSI            = 0x20000,
    GDOX_MACHINE_FEATURE_SCSI_WORD_DMA   = 0x40000,
    GDOX_MACHINE_FEATURE_SCSI_WORD_PORT  = 0x80000,
    GDOX_MACHINE_FEATURE_CACHE_CONTROL   = 0x100000,
    GDOX_MACHINE_FEATURE_CD_CACHE        = 0x200000,
    GDOX_MACHINE_FEATURE_PCM_BYTE_LANE   = 0x400000,
    GDOX_MACHINE_FEATURE_DMA_CARRY       = 0x800000,
    GDOX_MACHINE_FEATURE_RAM_SIZE_127MB  = 0x1000000,
    GDOX_MACHINE_FEATURE_CD_DOUBLE_SPEED = 0x2000000,
    GDOX_MACHINE_FEATURE_SERIAL_FIFO     = 0x4000000,
    GDOX_MACHINE_FEATURE_PCM16           = 0x8000000
};

enum GDOX_Boot_Device
{
    GDOX_BOOT_DEVICE_AUTO = 0,
    GDOX_BOOT_DEVICE_CDROM,
    GDOX_BOOT_DEVICE_FLOPPY_0,
    GDOX_BOOT_DEVICE_FLOPPY_1,
    GDOX_BOOT_DEVICE_HARD_DISK_0,
    GDOX_BOOT_DEVICE_HARD_DISK_1,
    GDOX_BOOT_DEVICE_HARD_DISK_2,
    GDOX_BOOT_DEVICE_HARD_DISK_3,
    GDOX_BOOT_DEVICE_HARD_DISK_4,
    GDOX_BOOT_DEVICE_MEMORY_CARD,
    GDOX_BOOT_DEVICE_COUNT
};

typedef void (*GDOX_Read_Callback)(void* context);

enum GDOX_Machine_Source
{
    GDOX_MACHINE_SOURCE_USER = 0,
    GDOX_MACHINE_SOURCE_MODEL,
    GDOX_MACHINE_SOURCE_DEFAULT,
    GDOX_MACHINE_SOURCE_DATABASE,
    GDOX_MACHINE_SOURCE_FLOPPY,
    GDOX_MACHINE_SOURCE_HARD_DISK
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
};

struct GDOX_Machine_DMA_Window
{
    u16 slot;
    u16 count;
    u32 base;
};

struct GDOX_Machine_Memory_Map
{
    const char* name;
    u32 address_mask;
    u32 vram_two_page;
    u32 vram_single_page;
    u32 sprite_ram;
    u32 os_rom;
    u32 dictionary_rom;
    u32 font_rom;
    u32 cmos;
    u32 pcm_window;
    u32 system_rom;
    u32 extension_rom;
    u32 backup_ram;
    u32 memory_card;
    u32 memory_card_size;
    u32 memory_card_bank;
    const GDOX_Machine_DMA_Window* dma_windows;
};

struct GDOX_Machine_Video_Timing
{
    u16 sprite_clear;
    u16 sprite_entry;
    bool ntsc_field;
    u16 sprite_write_contention;
    u16 sprite_read_contention;
    u16 sprite_fetch_low;
    u16 sprite_fetch_high;
};

enum GDOX_Bus_Class
{
    GDOX_BUS_NONE = 0,
    GDOX_BUS_DRAM,
    GDOX_BUS_VRAM,
    GDOX_BUS_SPRITE,
    GDOX_BUS_ROM,
    GDOX_BUS_CMOS,
    GDOX_BUS_FMR,
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

enum GDOX_Machine_Bus_Mode
{
    GDOX_MACHINE_BUS_FIXED = 0,
    GDOX_MACHINE_BUS_WAIT_PORTS,
    GDOX_MACHINE_BUS_SPEED_PORT
};

// In 1/256 clocks at the model's clock, reads first
// A follow adds to the wait of each later cycle of a read split in cycles, which overlaps the one before
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
};

struct GDOX_Machine_Bus_Timing
{
    GDOX_Machine_Bus_Mode mode;
    const GDOX_Machine_Bus_Waits* compatible;
    const GDOX_Machine_Bus_Waits* fast;
    u16 refresh;
    u16 refill;
    u16 misalign;
    u16 string_row;
    u16 string_read;
    u16 fmr_byte[2];
    u16 fmr_word[2];
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
    u16 machine_id;
    GDOX_Machine_CPU cpu;
    GDOX_Machine_FPU fpu;
    u32 cpu_clock_rate;
    GDOX_Machine_Map map;
    u32 features;
    const u8* ram_sizes_mb;
    int ram_default_mb;
    int floppy_drives;
    bool three_mode_floppy;
    const char* firmware_set;
    const GDOX_Machine_Video_Timing* video_timing;
    const GDOX_Machine_Bus_Timing* bus_timing;
};

struct GDOX_Machine_Config
{
    GDOX_Machine_Model model;
    GDOX_Machine_CPU cpu;
    u32 ram_size;
    int floppy_drives;
    u32 cpu_clock_rate;
    bool scsi;
    bool fpu;
    int speed_mode;
};

struct GDOX_Machine_Settings
{
    int model;
    int cpu_mhz;
    int ram_mb;
    int fpu;
    int speed_mode;
};

struct GDOX_Machine_Report
{
    GDOX_Machine_Source model;
    GDOX_Machine_Source cpu_clock;
    GDOX_Machine_Source ram;
    GDOX_Machine_Source fpu;
    GDOX_Machine_Source speed_mode;
    bool fallback;
    const char* title;
    int warning_count;
    char warnings[GDOX_MACHINE_WARNING_COUNT][GDOX_MACHINE_WARNING_SIZE];
};

struct GDOX_Floppy_Notice
{
    bool warning;
    int line_count;
    char lines[2][GDOX_MACHINE_WARNING_SIZE];
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
    bool memory_card_inserted;
    u32 memory_card_size;
    u64 memory_card_session;
    u64 memory_card_write;
};

struct GDOX_SaveState_Screenshot
{
    u32 width;
    u32 height;
    u32 size;
    u8* data;
};

#endif /* TYPES_H */
