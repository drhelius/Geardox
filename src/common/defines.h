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

#ifndef DEFINES_H
#define DEFINES_H

#if !defined(EMULATOR_BUILD)
    #define EMULATOR_BUILD "undefined"
#endif

#define GDOX_VERSION EMULATOR_BUILD

#define GDOX_TITLE "Geardox"
#define GDOX_TITLE_ASCII "" \
"  ▄████  ▄▄▄▄▄  ▄▄▄  ▄▄▄▄  ▄▄▄▄   ▄▄▄  ▄▄ ▄▄\n" \
" ██  ▄▄▄ ██▄▄  ██▀██ ██▄█▄ ██▀██ ██▀██ ▀█▄█▀\n" \
"  ▀███▀  ██▄▄▄ ██▀██ ██ ██ ████▀ ▀███▀ ██ ██\n"


#if defined(DEBUG)
    #define GDOX_DEBUG 1
#endif

#define GDOX_MAX_GAMEPADS 2
#define GDOX_MAX_FLOPPY_DRIVES 2
#define GDOX_MAX_HARD_DISKS 4

#define GDOX_MAX_PATH 4096

#define GDOX_FIRMWARE_SYSTEM_BIOS_SIZE (64 * 1024)
#define GDOX_FIRMWARE_VGA_BIOS_SIZE (32 * 1024)
#define GDOX_MAIN_RAM_SIZE (2 * 1024 * 1024)
#define GDOX_CMOS_SIZE 128
#define GDOX_CMOS_FILE_MAGIC 0x4347
#define GDOX_CMOS_FILE_VERSION 1
#define GDOX_CMOS_FILE_HEADER_SIZE 4
#define GDOX_CMOS_FILE_SIZE (GDOX_CMOS_FILE_HEADER_SIZE + GDOX_CMOS_SIZE)

#define GDOX_FRAME_BUFFER_WIDTH 640
#define GDOX_FRAME_BUFFER_HEIGHT 400
#define GDOX_MAX_FRAME_BUFFER_WIDTH 1024
#define GDOX_MAX_FRAME_BUFFER_HEIGHT 768

#define GDOX_AUDIO_SAMPLE_RATE 44100
#define GDOX_AUDIO_BUFFER_SIZE 8192
#define GDOX_AUDIO_QUEUE_SIZE 1600

#define GDOX_MASTER_CLOCK_RATE 32000000
#define GDOX_MACHINE_CLOCK_RATE (GDOX_MASTER_CLOCK_RATE / 2)
#define GDOX_SOUND_CLOCK_RATE (GDOX_MASTER_CLOCK_RATE / 4)
#define GDOX_VIDEO_FRAME_RATE 60
#define GDOX_MACHINE_CLOCKS_PER_FRAME ((GDOX_MACHINE_CLOCK_RATE + GDOX_VIDEO_FRAME_RATE - 1) / GDOX_VIDEO_FRAME_RATE)
#define GDOX_NO_EVENT 0xFFFFFFFFFFFFFFFFULL
#define GDOX_MACHINE_AUTO -1
#define GDOX_MACHINE_WARNING_COUNT 9
#define GDOX_MACHINE_WARNING_SIZE 192

#define GDOX_I386_TRACE_SIZE 32
#define GDOX_I386_MAX_INSTRUCTION_LENGTH 15

#define GDOX_SAVESTATE_VERSION 1
#define GDOX_SAVESTATE_MIN_VERSION 1
#define GDOX_SAVESTATE_MAGIC 0x47445354

#if !defined(NULL)
    #define NULL 0
#endif

#define SafeDelete(pointer) if(pointer != NULL) {delete pointer; pointer = NULL;}
#define SafeDeleteArray(pointer) if(pointer != NULL) {delete [] pointer; pointer = NULL;}

#define InitPointer(pointer) ((pointer) = NULL)
#define IsValidPointer(pointer) ((pointer) != NULL)

#define UNUSED(expr) (void)(expr)

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(value, min, max) MIN(MAX(value, min), max)

#if defined(MSB_FIRST) || defined(__BIG_ENDIAN__) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    #define GDOX_BIG_ENDIAN
#else
    #define GDOX_LITTLE_ENDIAN
#endif

#if defined(__GNUC__) || defined(__clang__)
    #define INLINE inline __attribute__((always_inline))
    #define NO_INLINE __attribute__((noinline))
#elif defined(_MSC_VER)
    #define INLINE __forceinline
    #define NO_INLINE __declspec(noinline)
#else
    #define INLINE inline
    #define NO_INLINE
#endif

#if !defined(GDOX_DEBUG)
    #if defined(__GNUC__) || defined(__clang__)
        #if !defined(__OPTIMIZE__) && !defined(__OPTIMIZE_SIZE__)
            #warning "Compiling without optimizations."
            #define GDOX_NO_OPTIMIZATIONS
        #endif
    #elif defined(_MSC_VER)
        #if !defined(NDEBUG)
            #pragma message("Compiling without optimizations.")
            #define GDOX_NO_OPTIMIZATIONS
        #endif
    #endif
#endif

#if defined(__GNUC__) || defined(__clang__)
    #define likely(x)   __builtin_expect(!!(x), 1)
    #define unlikely(x) __builtin_expect(!!(x), 0)
#else
    #define likely(x)   (x)
    #define unlikely(x) (x)
#endif

#if defined(__has_cpp_attribute)
  #if __has_cpp_attribute(fallthrough)
    #define FALLTHROUGH [[fallthrough]]
  #elif __has_cpp_attribute(clang::fallthrough)
    #define FALLTHROUGH [[clang::fallthrough]]
  #elif __has_cpp_attribute(gnu::fallthrough)
    #define FALLTHROUGH [[gnu::fallthrough]]
  #endif
#endif

// MSVC older toolsets
#ifndef FALLTHROUGH
  #if defined(_MSC_VER) && !defined(__clang__)
    #include <sal.h>
    #ifdef __fallthrough
      #define FALLTHROUGH __fallthrough
    #else
      #define FALLTHROUGH ((void)0) /* fall through */
    #endif
  #else
    // Safe no-op for compilers without a recognized attribute
    #define FALLTHROUGH ((void)0) /* fall through */
  #endif
#endif

#endif /* DEFINES_H */
