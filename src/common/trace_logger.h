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

#ifndef TRACE_LOGGER_H
#define TRACE_LOGGER_H

#include "common.h"

#define GDOX_TRACE_HEADER_SIZE 16
#define GDOX_TRACE_ENTRY_SIZE 96
#define GDOX_TRACE_FILTER_RANGES 4
#define GDOX_TRACE_FILTER_FUNCTIONS 8
#define GDOX_TRACE_INTERRUPT_TEXT_SIZE 27

enum GDOX_Trace_Type : u8
{
    TRACE_CPU = 0,
    TRACE_CPU_INTERRUPT,
    TRACE_IO,
    TRACE_PIC,
    TRACE_TIMER,
    TRACE_DMA,
    TRACE_VIDEO,
    TRACE_FM,
    TRACE_PCM,
    TRACE_MIXER,
    TRACE_MIDI,
    TRACE_CDROM,
    TRACE_FDC,
    TRACE_IDE,
    TRACE_KEYBOARD,
    TRACE_INPUT,
    TRACE_SERIAL,
    TRACE_PARALLEL,
    TRACE_SYSTEM,
    TRACE_GUS,
    TRACE_VOODOO,
    TRACE_NET,
    TRACE_MARK,
    TRACE_TYPE_COUNT
};

static_assert(TRACE_TYPE_COUNT < 32, "Trace category flags exceed 32 bits");

#define TRACE_FLAG_CPU              (1U << TRACE_CPU)
#define TRACE_FLAG_CPU_INTERRUPT    (1U << TRACE_CPU_INTERRUPT)
#define TRACE_FLAG_IO               (1U << TRACE_IO)
#define TRACE_FLAG_PIC              (1U << TRACE_PIC)
#define TRACE_FLAG_TIMER            (1U << TRACE_TIMER)
#define TRACE_FLAG_DMA              (1U << TRACE_DMA)
#define TRACE_FLAG_VIDEO            (1U << TRACE_VIDEO)
#define TRACE_FLAG_FM               (1U << TRACE_FM)
#define TRACE_FLAG_PCM              (1U << TRACE_PCM)
#define TRACE_FLAG_MIXER            (1U << TRACE_MIXER)
#define TRACE_FLAG_MIDI             (1U << TRACE_MIDI)
#define TRACE_FLAG_CDROM            (1U << TRACE_CDROM)
#define TRACE_FLAG_FDC              (1U << TRACE_FDC)
#define TRACE_FLAG_IDE              (1U << TRACE_IDE)
#define TRACE_FLAG_KEYBOARD         (1U << TRACE_KEYBOARD)
#define TRACE_FLAG_INPUT            (1U << TRACE_INPUT)
#define TRACE_FLAG_SERIAL           (1U << TRACE_SERIAL)
#define TRACE_FLAG_PARALLEL         (1U << TRACE_PARALLEL)
#define TRACE_FLAG_SYSTEM           (1U << TRACE_SYSTEM)
#define TRACE_FLAG_GUS              (1U << TRACE_GUS)
#define TRACE_FLAG_VOODOO           (1U << TRACE_VOODOO)
#define TRACE_FLAG_NET              (1U << TRACE_NET)
#define TRACE_FLAG_MARK             (1U << TRACE_MARK)

#define TRACE_ENTRY_NEXT_PC         0x0001
#define TRACE_ENTRY_IGNORED         0x0002
#define TRACE_ENTRY_DEBUGGER        0x0004
#define TRACE_ENTRY_UNMAPPED        0x0008
#define TRACE_ENTRY_TYPE_SHIFT      8

enum GDOX_Trace_Mark_Event : u8
{
    TRACE_MARK_START = 0,
    TRACE_MARK_FRAME,
    TRACE_MARK_STOP,
    TRACE_MARK_CONTINUE,
    TRACE_MARK_STATE_LOAD,
    TRACE_MARK_REWIND,
    TRACE_MARK_RESET,
    TRACE_MARK_POWER,
    TRACE_MARK_MEDIA,
    TRACE_MARK_DEBUGGER_OFF,
    TRACE_MARK_LOST
};

enum GDOX_Trace_CPU_Interrupt_Event : u8
{
    TRACE_CPU_INTERRUPT_HARDWARE = 0,
    TRACE_CPU_INTERRUPT_EXCEPTION,
    TRACE_CPU_INTERRUPT_SOFTWARE,
    TRACE_CPU_INTERRUPT_RAISE,
    TRACE_CPU_INTERRUPT_SHUTDOWN,
    TRACE_CPU_INTERRUPT_RETURN,
    TRACE_CPU_INTERRUPT_MODE,
    TRACE_CPU_INTERRUPT_PAGING,
    TRACE_CPU_INTERRUPT_TASK,
    TRACE_CPU_INTERRUPT_TABLES,
    TRACE_CPU_INTERRUPT_MSR
};

enum GDOX_Trace_CPU_Cause : u8
{
    TRACE_CPU_CAUSE_MOV_CR0 = 0,
    TRACE_CPU_CAUSE_LMSW,
    TRACE_CPU_CAUSE_MOV_CR3,
    TRACE_CPU_CAUSE_MOV_CR4,
    TRACE_CPU_CAUSE_INVLPG,
    TRACE_CPU_CAUSE_LGDT,
    TRACE_CPU_CAUSE_LIDT,
    TRACE_CPU_CAUSE_CPUID,
    TRACE_CPU_CAUSE_RDTSC,
    TRACE_CPU_CAUSE_RDMSR,
    TRACE_CPU_CAUSE_WRMSR,
    TRACE_CPU_CAUSE_TASK_JUMP,
    TRACE_CPU_CAUSE_TASK_CALL,
    TRACE_CPU_CAUSE_TASK_IRET,
    TRACE_CPU_CAUSE_TASK_INTERRUPT
};

#define TRACE_CPU_INTERRUPT_EVENT_IRQS       (1U << TRACE_CPU_INTERRUPT_HARDWARE)
#define TRACE_CPU_INTERRUPT_EVENT_EXCEPTIONS ((1U << TRACE_CPU_INTERRUPT_EXCEPTION) | (1U << TRACE_CPU_INTERRUPT_RAISE) | \
    (1U << TRACE_CPU_INTERRUPT_SHUTDOWN))
#define TRACE_CPU_INTERRUPT_EVENT_SOFTWARE   (1U << TRACE_CPU_INTERRUPT_SOFTWARE)
#define TRACE_CPU_INTERRUPT_EVENT_RETURNS    (1U << TRACE_CPU_INTERRUPT_RETURN)
#define TRACE_CPU_INTERRUPT_EVENT_MODES      ((1U << TRACE_CPU_INTERRUPT_MODE) | (1U << TRACE_CPU_INTERRUPT_PAGING) | \
    (1U << TRACE_CPU_INTERRUPT_TASK) | (1U << TRACE_CPU_INTERRUPT_TABLES))
#define TRACE_CPU_INTERRUPT_EVENT_MSRS       (1U << TRACE_CPU_INTERRUPT_MSR)
#define TRACE_CPU_INTERRUPT_EVENT_DEFAULT    (TRACE_CPU_INTERRUPT_EVENT_IRQS | TRACE_CPU_INTERRUPT_EVENT_EXCEPTIONS | \
    TRACE_CPU_INTERRUPT_EVENT_MODES)
#define TRACE_CPU_INTERRUPT_EVENT_ALL        (TRACE_CPU_INTERRUPT_EVENT_DEFAULT | TRACE_CPU_INTERRUPT_EVENT_SOFTWARE | \
    TRACE_CPU_INTERRUPT_EVENT_RETURNS | TRACE_CPU_INTERRUPT_EVENT_MSRS)

#define TRACE_CPU_FLAG_FAILED                (1U << TRACE_ENTRY_TYPE_SHIFT)

enum GDOX_Trace_IO_Event : u8
{
    TRACE_IO_READ = 0,
    TRACE_IO_WRITE,
    TRACE_IO_POLL,
    TRACE_IO_DENIED
};

#define TRACE_IO_EVENT_READS  (1U << TRACE_IO_READ)
#define TRACE_IO_EVENT_WRITES (1U << TRACE_IO_WRITE)
#define TRACE_IO_EVENT_POLLS  (1U << TRACE_IO_POLL)
#define TRACE_IO_EVENT_DENIED (1U << TRACE_IO_DENIED)
#define TRACE_IO_EVENT_ALL    (TRACE_IO_EVENT_READS | TRACE_IO_EVENT_WRITES | TRACE_IO_EVENT_POLLS | \
    TRACE_IO_EVENT_DENIED)

#define TRACE_IO_FLAG_INS     (1U << TRACE_ENTRY_TYPE_SHIFT)

enum GDOX_Trace_PIC_Event : u8
{
    TRACE_PIC_REQUEST = 0,
    TRACE_PIC_MASK,
    TRACE_PIC_COMMAND,
    TRACE_PIC_INIT,
    TRACE_PIC_SPURIOUS,
    TRACE_PIC_LINE_LOW
};

#define TRACE_PIC_EVENT_REQUESTS (1U << TRACE_PIC_REQUEST)
#define TRACE_PIC_EVENT_MASK     (1U << TRACE_PIC_MASK)
#define TRACE_PIC_EVENT_COMMANDS (1U << TRACE_PIC_COMMAND)
#define TRACE_PIC_EVENT_INIT     (1U << TRACE_PIC_INIT)
#define TRACE_PIC_EVENT_SPURIOUS (1U << TRACE_PIC_SPURIOUS)
#define TRACE_PIC_EVENT_LINE_LOW (1U << TRACE_PIC_LINE_LOW)
#define TRACE_PIC_EVENT_DEFAULT  (TRACE_PIC_EVENT_REQUESTS | TRACE_PIC_EVENT_MASK | TRACE_PIC_EVENT_COMMANDS | \
    TRACE_PIC_EVENT_INIT | TRACE_PIC_EVENT_SPURIOUS)
#define TRACE_PIC_EVENT_ALL      (TRACE_PIC_EVENT_DEFAULT | TRACE_PIC_EVENT_LINE_LOW)

#define TRACE_PIC_FLAG_COALESCED  (1U << TRACE_ENTRY_TYPE_SHIFT)
#define TRACE_PIC_FLAG_IN_SERVICE (2U << TRACE_ENTRY_TYPE_SHIFT)

enum GDOX_Trace_Timer_Event : u8
{
    TRACE_TIMER_TIMEOUT = 0,
    TRACE_TIMER_CONTROL,
    TRACE_TIMER_COUNTER,
    TRACE_TIMER_SPEAKER,
    TRACE_TIMER_READ
};

#define TRACE_TIMER_EVENT_TIMEOUTS  (1U << TRACE_TIMER_TIMEOUT)
#define TRACE_TIMER_EVENT_COUNTERS  ((1U << TRACE_TIMER_CONTROL) | (1U << TRACE_TIMER_COUNTER))
#define TRACE_TIMER_EVENT_SPEAKER   (1U << TRACE_TIMER_SPEAKER)
#define TRACE_TIMER_EVENT_READS     (1U << TRACE_TIMER_READ)
#define TRACE_TIMER_EVENT_DEFAULT   (TRACE_TIMER_EVENT_COUNTERS | TRACE_TIMER_EVENT_SPEAKER)
#define TRACE_TIMER_EVENT_ALL       (TRACE_TIMER_EVENT_DEFAULT | TRACE_TIMER_EVENT_TIMEOUTS | \
    TRACE_TIMER_EVENT_READS)

#define TRACE_TIMER_FLAG_COALESCED  (1U << TRACE_ENTRY_TYPE_SHIFT)

enum GDOX_Trace_DMA_Event : u8
{
    TRACE_DMA_REGISTER = 0,
    TRACE_DMA_REQUEST,
    TRACE_DMA_END,
    TRACE_DMA_UNSUPPORTED
};

#define TRACE_DMA_EVENT_REGISTERS   (1U << TRACE_DMA_REGISTER)
#define TRACE_DMA_EVENT_REQUESTS    (1U << TRACE_DMA_REQUEST)
#define TRACE_DMA_EVENT_ENDS        (1U << TRACE_DMA_END)
#define TRACE_DMA_EVENT_UNSUPPORTED (1U << TRACE_DMA_UNSUPPORTED)
#define TRACE_DMA_EVENT_DEFAULT     (TRACE_DMA_EVENT_REGISTERS | TRACE_DMA_EVENT_ENDS | TRACE_DMA_EVENT_UNSUPPORTED)
#define TRACE_DMA_EVENT_ALL         (TRACE_DMA_EVENT_DEFAULT | TRACE_DMA_EVENT_REQUESTS)

enum GDOX_Trace_Video_Event : u8
{
    TRACE_VIDEO_CRTC = 0,
    TRACE_VIDEO_OUTPUT,
    TRACE_VIDEO_PALETTE,
    TRACE_VIDEO_VSYNC,
    TRACE_VIDEO_VSYNC_CLEAR,
    TRACE_VIDEO_MISSED_VBLANK,
    TRACE_VIDEO_BLT,
    TRACE_VIDEO_BLT_DONE,
    TRACE_VIDEO_MODE,
    TRACE_VIDEO_FRAME,
    TRACE_VIDEO_STATUS,
    TRACE_VIDEO_PALETTE_LOAD,
    TRACE_VIDEO_DAC,
    TRACE_VIDEO_DATAPATH,
    TRACE_VIDEO_BLT_REGISTER
};

enum GDOX_Trace_Video_Adapter : u8
{
    TRACE_VIDEO_ADAPTER_VGA = 0,
    TRACE_VIDEO_ADAPTER_EGA,
    TRACE_VIDEO_ADAPTER_CIRRUS,
    TRACE_VIDEO_ADAPTER_CGA
};

enum GDOX_Trace_Video_Bank : u8
{
    TRACE_VIDEO_BANK_SR = 0,
    TRACE_VIDEO_BANK_GR,
    TRACE_VIDEO_BANK_AR,
    TRACE_VIDEO_BANK_MISC,
    TRACE_VIDEO_BANK_CGA,
    TRACE_VIDEO_BANK_EGA_POSITION,
    TRACE_VIDEO_BANK_PEL_MASK,
    TRACE_VIDEO_BANK_DAC_READ,
    TRACE_VIDEO_BANK_DAC_WRITE,
    TRACE_VIDEO_BANK_HIDDEN_DAC
};

#define TRACE_VIDEO_EVENT_CRTC          (1U << TRACE_VIDEO_CRTC)
#define TRACE_VIDEO_EVENT_OUTPUT        (1U << TRACE_VIDEO_OUTPUT)
#define TRACE_VIDEO_EVENT_PALETTE       (1U << TRACE_VIDEO_PALETTE)
#define TRACE_VIDEO_EVENT_VSYNC         ((1U << TRACE_VIDEO_VSYNC) | (1U << TRACE_VIDEO_VSYNC_CLEAR))
#define TRACE_VIDEO_EVENT_MISSED_VBLANK (1U << TRACE_VIDEO_MISSED_VBLANK)
#define TRACE_VIDEO_EVENT_BLT           ((1U << TRACE_VIDEO_BLT) | (1U << TRACE_VIDEO_BLT_DONE))
#define TRACE_VIDEO_EVENT_MODE          (1U << TRACE_VIDEO_MODE)
#define TRACE_VIDEO_EVENT_FRAME         (1U << TRACE_VIDEO_FRAME)
#define TRACE_VIDEO_EVENT_STATUS        (1U << TRACE_VIDEO_STATUS)
#define TRACE_VIDEO_EVENT_PALETTE_LOAD  (1U << TRACE_VIDEO_PALETTE_LOAD)
#define TRACE_VIDEO_EVENT_DAC           (1U << TRACE_VIDEO_DAC)
#define TRACE_VIDEO_EVENT_DATAPATH      (1U << TRACE_VIDEO_DATAPATH)
#define TRACE_VIDEO_EVENT_BLT_REGISTERS (1U << TRACE_VIDEO_BLT_REGISTER)
#define TRACE_VIDEO_EVENT_DEFAULT       (TRACE_VIDEO_EVENT_CRTC | TRACE_VIDEO_EVENT_OUTPUT | TRACE_VIDEO_EVENT_VSYNC | \
    TRACE_VIDEO_EVENT_BLT | TRACE_VIDEO_EVENT_MODE | TRACE_VIDEO_EVENT_PALETTE_LOAD | TRACE_VIDEO_EVENT_DAC)
#define TRACE_VIDEO_EVENT_ALL           (TRACE_VIDEO_EVENT_DEFAULT | TRACE_VIDEO_EVENT_MISSED_VBLANK | \
    TRACE_VIDEO_EVENT_PALETTE | TRACE_VIDEO_EVENT_FRAME | TRACE_VIDEO_EVENT_STATUS | TRACE_VIDEO_EVENT_DATAPATH | \
    TRACE_VIDEO_EVENT_BLT_REGISTERS)

#define TRACE_VIDEO_FLAG_MMIO           (1U << TRACE_ENTRY_TYPE_SHIFT)
#define TRACE_VIDEO_FLAG_EXTENDED_DAC   (2U << TRACE_ENTRY_TYPE_SHIFT)
#define TRACE_VIDEO_FLAG_IRQ            (4U << TRACE_ENTRY_TYPE_SHIFT)
#define TRACE_VIDEO_FLAG_BANK           (8U << TRACE_ENTRY_TYPE_SHIFT)

enum GDOX_Trace_FM_Event : u8
{
    TRACE_FM_KEY = 0,
    TRACE_FM_FREQUENCY,
    TRACE_FM_OPERATOR,
    TRACE_FM_CHANNEL,
    TRACE_FM_GLOBAL,
    TRACE_FM_TIMER,
    TRACE_FM_STATUS
};

#define TRACE_FM_EVENT_KEY       (1U << TRACE_FM_KEY)
#define TRACE_FM_EVENT_FREQUENCY (1U << TRACE_FM_FREQUENCY)
#define TRACE_FM_EVENT_OPERATORS (1U << TRACE_FM_OPERATOR)
#define TRACE_FM_EVENT_CHANNELS  (1U << TRACE_FM_CHANNEL)
#define TRACE_FM_EVENT_GLOBAL    (1U << TRACE_FM_GLOBAL)
#define TRACE_FM_EVENT_TIMERS    ((1U << TRACE_FM_TIMER) | (1U << TRACE_FM_STATUS))
#define TRACE_FM_EVENT_DEFAULT   (TRACE_FM_EVENT_KEY | TRACE_FM_EVENT_FREQUENCY | TRACE_FM_EVENT_CHANNELS | \
    TRACE_FM_EVENT_GLOBAL | TRACE_FM_EVENT_TIMERS)

#define TRACE_FM_FLAG_INVALID    (1U << TRACE_ENTRY_TYPE_SHIFT)
#define TRACE_FM_FLAG_ALIAS      (2U << TRACE_ENTRY_TYPE_SHIFT)
#define TRACE_FM_FLAG_OPL3       (4U << TRACE_ENTRY_TYPE_SHIFT)
#define TRACE_FM_EVENT_ALL       (TRACE_FM_EVENT_DEFAULT | TRACE_FM_EVENT_OPERATORS)

enum GDOX_Trace_PCM_Event : u8
{
    TRACE_PCM_COMMAND = 0,
    TRACE_PCM_TRANSFER,
    TRACE_PCM_IRQ,
    TRACE_PCM_ACK,
    TRACE_PCM_RESET,
    TRACE_PCM_READ,
    TRACE_PCM_REJECT,
    TRACE_PCM_END,
    TRACE_PCM_CONFIG,
    TRACE_PCM_DIRECT
};

#define TRACE_PCM_EVENT_COMMANDS  ((1U << TRACE_PCM_COMMAND) | (1U << TRACE_PCM_REJECT) | (1U << TRACE_PCM_READ))
#define TRACE_PCM_EVENT_TRANSFERS ((1U << TRACE_PCM_TRANSFER) | (1U << TRACE_PCM_END))
#define TRACE_PCM_EVENT_IRQS      ((1U << TRACE_PCM_IRQ) | (1U << TRACE_PCM_ACK))
#define TRACE_PCM_EVENT_RESETS    ((1U << TRACE_PCM_RESET) | (1U << TRACE_PCM_CONFIG))
#define TRACE_PCM_EVENT_DIRECT    (1U << TRACE_PCM_DIRECT)
#define TRACE_PCM_EVENT_DEFAULT   (TRACE_PCM_EVENT_COMMANDS | TRACE_PCM_EVENT_TRANSFERS | TRACE_PCM_EVENT_IRQS | \
    TRACE_PCM_EVENT_RESETS)
#define TRACE_PCM_EVENT_ALL       (TRACE_PCM_EVENT_DEFAULT | TRACE_PCM_EVENT_DIRECT)

#define TRACE_PCM_FLAG_EMPTY      (1U << TRACE_ENTRY_TYPE_SHIFT)
#define TRACE_PCM_FLAG_NOT_ACKED  (2U << TRACE_ENTRY_TYPE_SHIFT)
#define TRACE_PCM_FLAG_HANDOFF    (4U << TRACE_ENTRY_TYPE_SHIFT)

enum GDOX_Trace_Mixer_Event : u8
{
    TRACE_MIXER_VOLUME = 0,
    TRACE_MIXER_CONTROL,
    TRACE_MIXER_READ
};

#define TRACE_MIXER_EVENT_VOLUME  (1U << TRACE_MIXER_VOLUME)
#define TRACE_MIXER_EVENT_CONTROL (1U << TRACE_MIXER_CONTROL)
#define TRACE_MIXER_EVENT_READS   (1U << TRACE_MIXER_READ)
#define TRACE_MIXER_EVENT_DEFAULT (TRACE_MIXER_EVENT_VOLUME | TRACE_MIXER_EVENT_CONTROL)
#define TRACE_MIXER_EVENT_ALL     (TRACE_MIXER_EVENT_DEFAULT | TRACE_MIXER_EVENT_READS)

enum GDOX_Trace_GUS_Event : u8
{
    TRACE_GUS_REGISTER = 0,
    TRACE_GUS_IRQ,
    TRACE_GUS_DMA,
    TRACE_GUS_LATCH
};

#define TRACE_GUS_EVENT_REGISTERS (1U << TRACE_GUS_REGISTER)
#define TRACE_GUS_EVENT_IRQS      (1U << TRACE_GUS_IRQ)
#define TRACE_GUS_EVENT_DMA       (1U << TRACE_GUS_DMA)
#define TRACE_GUS_EVENT_LATCHES   (1U << TRACE_GUS_LATCH)
#define TRACE_GUS_EVENT_ALL       (TRACE_GUS_EVENT_REGISTERS | TRACE_GUS_EVENT_IRQS | TRACE_GUS_EVENT_DMA | \
    TRACE_GUS_EVENT_LATCHES)

enum GDOX_Trace_Voodoo_Event : u8
{
    TRACE_VOODOO_COMMAND = 0,
    TRACE_VOODOO_CULLED,
    TRACE_VOODOO_OVERFLOW,
    TRACE_VOODOO_LFB_READ
};

enum GDOX_Trace_Voodoo_Command : u8
{
    TRACE_VOODOO_TRIANGLE = 0,
    TRACE_VOODOO_FAST_FILL,
    TRACE_VOODOO_SWAP,
    TRACE_VOODOO_NOP
};

#define TRACE_VOODOO_EVENT_COMMANDS  (1U << TRACE_VOODOO_COMMAND)
#define TRACE_VOODOO_EVENT_CULLED    (1U << TRACE_VOODOO_CULLED)
#define TRACE_VOODOO_EVENT_OVERFLOWS (1U << TRACE_VOODOO_OVERFLOW)
#define TRACE_VOODOO_EVENT_LFB_READS (1U << TRACE_VOODOO_LFB_READ)
#define TRACE_VOODOO_EVENT_ALL       (TRACE_VOODOO_EVENT_COMMANDS | TRACE_VOODOO_EVENT_CULLED | \
    TRACE_VOODOO_EVENT_OVERFLOWS | TRACE_VOODOO_EVENT_LFB_READS)

enum GDOX_Trace_Net_Event : u8
{
    TRACE_NET_WRITE = 0,
    TRACE_NET_TRANSMIT,
    TRACE_NET_RECEIVE,
    TRACE_NET_DROP,
    TRACE_NET_IRQ,
    TRACE_NET_DMA,
    TRACE_NET_RESET,
    TRACE_NET_READ
};

enum GDOX_Trace_Net_Drop : u8
{
    TRACE_NET_DROP_STOPPED = 0,
    TRACE_NET_DROP_ADDRESS,
    TRACE_NET_DROP_RUNT,
    TRACE_NET_DROP_MONITOR,
    TRACE_NET_DROP_OVERFLOW
};

#define TRACE_NET_EVENT_WRITES       (1U << TRACE_NET_WRITE)
#define TRACE_NET_EVENT_FRAMES       ((1U << TRACE_NET_TRANSMIT) | (1U << TRACE_NET_RECEIVE) | (1U << TRACE_NET_DROP))
#define TRACE_NET_EVENT_CONTROL      ((1U << TRACE_NET_IRQ) | (1U << TRACE_NET_DMA) | (1U << TRACE_NET_RESET))
#define TRACE_NET_EVENT_READS        (1U << TRACE_NET_READ)
#define TRACE_NET_EVENT_DEFAULT      (TRACE_NET_EVENT_FRAMES | TRACE_NET_EVENT_CONTROL)
#define TRACE_NET_EVENT_ALL          (TRACE_NET_EVENT_DEFAULT | TRACE_NET_EVENT_WRITES | TRACE_NET_EVENT_READS)

enum GDOX_Trace_MIDI_Event : u8
{
    TRACE_MIDI_DATA = 0,
    TRACE_MIDI_COMMAND,
    TRACE_MIDI_MESSAGE,
    TRACE_MIDI_OUT,
    TRACE_MIDI_READ,
    TRACE_MIDI_STATUS
};

enum GDOX_Trace_MIDI_Source : u8
{
    TRACE_MIDI_SOURCE_DSP = 0,
    TRACE_MIDI_SOURCE_MPU,
    TRACE_MIDI_SOURCE_GUS,
    TRACE_MIDI_SOURCE_MPU_PLAYBACK
};

enum GDOX_Trace_MIDI_Kind : u8
{
    TRACE_MIDI_KIND_DATA = 0,
    TRACE_MIDI_KIND_ACK,
    TRACE_MIDI_KIND_REQUEST,
    TRACE_MIDI_KIND_OVERFLOW,
    TRACE_MIDI_KIND_CONDUCTOR,
    TRACE_MIDI_KIND_RECORD,
    TRACE_MIDI_KIND_MEASURE,
    TRACE_MIDI_KIND_END,
    TRACE_MIDI_KIND_CLOCK
};

#define TRACE_MIDI_EVENT_DATA     ((1U << TRACE_MIDI_DATA) | (1U << TRACE_MIDI_READ))
#define TRACE_MIDI_EVENT_COMMANDS ((1U << TRACE_MIDI_COMMAND) | (1U << TRACE_MIDI_STATUS))
#define TRACE_MIDI_EVENT_MESSAGES (1U << TRACE_MIDI_MESSAGE)
#define TRACE_MIDI_EVENT_OUT      (1U << TRACE_MIDI_OUT)
#define TRACE_MIDI_EVENT_DEFAULT  (TRACE_MIDI_EVENT_COMMANDS | TRACE_MIDI_EVENT_OUT)
#define TRACE_MIDI_EVENT_ALL      (TRACE_MIDI_EVENT_DEFAULT | TRACE_MIDI_EVENT_DATA | TRACE_MIDI_EVENT_MESSAGES)

enum GDOX_Trace_CDROM_Event : u8
{
    TRACE_CDROM_PACKET = 0,
    TRACE_CDROM_COMMAND,
    TRACE_CDROM_COMPLETE,
    TRACE_CDROM_DATA,
    TRACE_CDROM_MEDIA,
    TRACE_CDROM_CDDA_PLAY,
    TRACE_CDROM_CDDA_PAUSE,
    TRACE_CDROM_CDDA_RESUME,
    TRACE_CDROM_CDDA_STOP,
    TRACE_CDROM_CDDA_END,
    TRACE_CDROM_CDDA_LOOP,
    TRACE_CDROM_CDDA_REJECT,
    TRACE_CDROM_POLL,
    TRACE_CDROM_RESET
};

enum GDOX_Trace_CDDA_Stop : u8
{
    TRACE_CDDA_STOP_COMMAND = 0,
    TRACE_CDDA_STOP_INSERT,
    TRACE_CDDA_STOP_EJECT
};

enum GDOX_Trace_CDROM_Media : u8
{
    TRACE_CDROM_MEDIA_INSERT = 0,
    TRACE_CDROM_MEDIA_EJECT,
    TRACE_CDROM_MEDIA_TRAY_OPEN,
    TRACE_CDROM_MEDIA_TRAY_CLOSE,
    TRACE_CDROM_MEDIA_READY,
    TRACE_CDROM_MEDIA_STOP
};

#define TRACE_CDROM_EVENT_PACKETS  ((1U << TRACE_CDROM_PACKET) | (1U << TRACE_CDROM_COMMAND))
#define TRACE_CDROM_EVENT_STATUS   (1U << TRACE_CDROM_COMPLETE)
#define TRACE_CDROM_EVENT_DATA     (1U << TRACE_CDROM_DATA)
#define TRACE_CDROM_EVENT_MEDIA    (1U << TRACE_CDROM_MEDIA)
#define TRACE_CDROM_EVENT_CDDA     ((1U << TRACE_CDROM_CDDA_PLAY) | (1U << TRACE_CDROM_CDDA_PAUSE) | \
    (1U << TRACE_CDROM_CDDA_RESUME) | (1U << TRACE_CDROM_CDDA_STOP) | (1U << TRACE_CDROM_CDDA_END) | \
    (1U << TRACE_CDROM_CDDA_LOOP) | (1U << TRACE_CDROM_CDDA_REJECT))
#define TRACE_CDROM_EVENT_POLLS    (1U << TRACE_CDROM_POLL)
#define TRACE_CDROM_EVENT_RESETS   (1U << TRACE_CDROM_RESET)
#define TRACE_CDROM_EVENT_DEFAULT  (TRACE_CDROM_EVENT_PACKETS | TRACE_CDROM_EVENT_STATUS | TRACE_CDROM_EVENT_DATA | \
    TRACE_CDROM_EVENT_MEDIA | TRACE_CDROM_EVENT_CDDA | TRACE_CDROM_EVENT_RESETS)
#define TRACE_CDROM_EVENT_ALL      (TRACE_CDROM_EVENT_DEFAULT | TRACE_CDROM_EVENT_POLLS)

enum GDOX_Trace_FDC_Event : u8
{
    TRACE_FDC_COMMAND = 0,
    TRACE_FDC_END,
    TRACE_FDC_DRIVE_CONTROL,
    TRACE_FDC_DRIVE_SELECT,
    TRACE_FDC_RESULT,
    TRACE_FDC_RATE,
    TRACE_FDC_SECTOR,
    TRACE_FDC_MEDIA,
    TRACE_FDC_TC,
    TRACE_FDC_SEEK_END
};

#define TRACE_FDC_EVENT_COMMANDS (1U << TRACE_FDC_COMMAND)
#define TRACE_FDC_EVENT_RESULTS  ((1U << TRACE_FDC_END) | (1U << TRACE_FDC_RESULT) | (1U << TRACE_FDC_SEEK_END) | \
    (1U << TRACE_FDC_TC))
#define TRACE_FDC_EVENT_DRIVES   ((1U << TRACE_FDC_DRIVE_CONTROL) | (1U << TRACE_FDC_DRIVE_SELECT) | \
    (1U << TRACE_FDC_RATE) | (1U << TRACE_FDC_MEDIA))
#define TRACE_FDC_EVENT_SECTORS  (1U << TRACE_FDC_SECTOR)
#define TRACE_FDC_EVENT_DEFAULT  (TRACE_FDC_EVENT_COMMANDS | TRACE_FDC_EVENT_RESULTS | TRACE_FDC_EVENT_DRIVES)
#define TRACE_FDC_EVENT_ALL      (TRACE_FDC_EVENT_DEFAULT | TRACE_FDC_EVENT_SECTORS)

enum GDOX_Trace_IDE_Event : u8
{
    TRACE_IDE_COMMAND = 0,
    TRACE_IDE_END,
    TRACE_IDE_RESET,
    TRACE_IDE_IGNORED,
    TRACE_IDE_RESET_RELEASE
};

#define TRACE_IDE_EVENT_COMMANDS ((1U << TRACE_IDE_COMMAND) | (1U << TRACE_IDE_IGNORED))
#define TRACE_IDE_EVENT_RESULTS  (1U << TRACE_IDE_END)
#define TRACE_IDE_EVENT_RESETS   ((1U << TRACE_IDE_RESET) | (1U << TRACE_IDE_RESET_RELEASE))
#define TRACE_IDE_EVENT_ALL      (TRACE_IDE_EVENT_COMMANDS | TRACE_IDE_EVENT_RESULTS | TRACE_IDE_EVENT_RESETS)

enum GDOX_Trace_Keyboard_Event : u8
{
    TRACE_KEYBOARD_KEY = 0,
    TRACE_KEYBOARD_READ,
    TRACE_KEYBOARD_COMMAND,
    TRACE_KEYBOARD_OUTPUT_PORT,
    TRACE_KEYBOARD_REJECT,
    TRACE_KEYBOARD_BYTE,
    TRACE_KEYBOARD_READ_EMPTY
};

enum GDOX_Trace_Keyboard_Target : u8
{
    TRACE_KEYBOARD_TARGET_8042 = 0,
    TRACE_KEYBOARD_TARGET_8042_PARAMETER,
    TRACE_KEYBOARD_TARGET_KEYBOARD
};

#define TRACE_KEYBOARD_EVENT_KEYS        ((1U << TRACE_KEYBOARD_KEY) | (1U << TRACE_KEYBOARD_REJECT))
#define TRACE_KEYBOARD_EVENT_READS       (1U << TRACE_KEYBOARD_READ)
#define TRACE_KEYBOARD_EVENT_COMMANDS    ((1U << TRACE_KEYBOARD_COMMAND) | (1U << TRACE_KEYBOARD_OUTPUT_PORT))
#define TRACE_KEYBOARD_EVENT_BYTES       (1U << TRACE_KEYBOARD_BYTE)
#define TRACE_KEYBOARD_EVENT_EMPTY_READS (1U << TRACE_KEYBOARD_READ_EMPTY)
#define TRACE_KEYBOARD_EVENT_DEFAULT     (TRACE_KEYBOARD_EVENT_KEYS | TRACE_KEYBOARD_EVENT_READS | \
    TRACE_KEYBOARD_EVENT_COMMANDS)
#define TRACE_KEYBOARD_EVENT_ALL         (TRACE_KEYBOARD_EVENT_DEFAULT | TRACE_KEYBOARD_EVENT_BYTES | \
    TRACE_KEYBOARD_EVENT_EMPTY_READS)

#define TRACE_KEYBOARD_FLAG_AUX          (1U << TRACE_ENTRY_TYPE_SHIFT)
#define TRACE_KEYBOARD_FLAG_LOST         (2U << TRACE_ENTRY_TYPE_SHIFT)
#define TRACE_KEYBOARD_FLAG_TIMEOUT      (4U << TRACE_ENTRY_TYPE_SHIFT)
#define TRACE_KEYBOARD_FLAG_AS_COMMAND   (8U << TRACE_ENTRY_TYPE_SHIFT)

enum GDOX_Trace_Input_Event : u8
{
    TRACE_INPUT_CHANGE = 0,
    TRACE_INPUT_MOUSE_COMMAND,
    TRACE_INPUT_MOUSE_PACKET,
    TRACE_INPUT_JOYSTICK_FIRE,
    TRACE_INPUT_MOUSE_RESET
};

enum GDOX_Trace_Input_Device : u8
{
    TRACE_INPUT_DEVICE_MOUSE = 0,
    TRACE_INPUT_DEVICE_JOYSTICK,
    TRACE_INPUT_DEVICE_PS2_MOUSE,
    TRACE_INPUT_DEVICE_SERIAL_MOUSE
};

#define TRACE_INPUT_EVENT_CHANGES  (1U << TRACE_INPUT_CHANGE)
#define TRACE_INPUT_EVENT_COMMANDS ((1U << TRACE_INPUT_MOUSE_COMMAND) | (1U << TRACE_INPUT_MOUSE_RESET))
#define TRACE_INPUT_EVENT_PACKETS  (1U << TRACE_INPUT_MOUSE_PACKET)
#define TRACE_INPUT_EVENT_FIRES    (1U << TRACE_INPUT_JOYSTICK_FIRE)
#define TRACE_INPUT_EVENT_DEFAULT  (TRACE_INPUT_EVENT_CHANGES | TRACE_INPUT_EVENT_COMMANDS)
#define TRACE_INPUT_EVENT_ALL      (TRACE_INPUT_EVENT_DEFAULT | TRACE_INPUT_EVENT_PACKETS | TRACE_INPUT_EVENT_FIRES)
#define TRACE_INPUT_FLAG_PARAMETER  (1U << TRACE_ENTRY_TYPE_SHIFT)
#define TRACE_INPUT_FLAG_REJECTED   (2U << TRACE_ENTRY_TYPE_SHIFT)
#define TRACE_INPUT_FLAG_WRAP       (4U << TRACE_ENTRY_TYPE_SHIFT)
#define TRACE_INPUT_FLAG_IDENT      (8U << TRACE_ENTRY_TYPE_SHIFT)

enum GDOX_Trace_Serial_Event : u8
{
    TRACE_SERIAL_WRITE = 0,
    TRACE_SERIAL_RECEIVE,
    TRACE_SERIAL_TRANSMIT,
    TRACE_SERIAL_MODEM,
    TRACE_SERIAL_IRQ,
    TRACE_SERIAL_CONFIG,
    TRACE_SERIAL_READ
};

#define TRACE_SERIAL_EVENT_WRITES    (1U << TRACE_SERIAL_WRITE)
#define TRACE_SERIAL_EVENT_DATA      ((1U << TRACE_SERIAL_RECEIVE) | (1U << TRACE_SERIAL_TRANSMIT))
#define TRACE_SERIAL_EVENT_CONTROL   ((1U << TRACE_SERIAL_MODEM) | (1U << TRACE_SERIAL_IRQ) | (1U << TRACE_SERIAL_CONFIG))
#define TRACE_SERIAL_EVENT_READS     (1U << TRACE_SERIAL_READ)
#define TRACE_SERIAL_EVENT_DEFAULT   (TRACE_SERIAL_EVENT_DATA | TRACE_SERIAL_EVENT_CONTROL)
#define TRACE_SERIAL_EVENT_ALL       (TRACE_SERIAL_EVENT_DEFAULT | TRACE_SERIAL_EVENT_WRITES | TRACE_SERIAL_EVENT_READS)

#define TRACE_SERIAL_FLAG_DLAB       (1U << TRACE_ENTRY_TYPE_SHIFT)

enum GDOX_Trace_Parallel_Event : u8
{
    TRACE_PARALLEL_DATA = 0,
    TRACE_PARALLEL_CONTROL,
    TRACE_PARALLEL_STROBE
};

#define TRACE_PARALLEL_EVENT_DATA    (1U << TRACE_PARALLEL_DATA)
#define TRACE_PARALLEL_EVENT_CONTROL (1U << TRACE_PARALLEL_CONTROL)
#define TRACE_PARALLEL_EVENT_STROBE  (1U << TRACE_PARALLEL_STROBE)
#define TRACE_PARALLEL_EVENT_DEFAULT (TRACE_PARALLEL_EVENT_DATA | TRACE_PARALLEL_EVENT_CONTROL | TRACE_PARALLEL_EVENT_STROBE)
#define TRACE_PARALLEL_EVENT_ALL     TRACE_PARALLEL_EVENT_DEFAULT

enum GDOX_Trace_System_Event : u8
{
    TRACE_SYSTEM_RESET = 0,
    TRACE_SYSTEM_MEMORY_MAP,
    TRACE_SYSTEM_RTC_READ,
    TRACE_SYSTEM_RTC_WRITE,
    TRACE_SYSTEM_RTC_IRQ,
    TRACE_SYSTEM_POST,
    TRACE_SYSTEM_CONTROL,
    TRACE_SYSTEM_PCI,
    TRACE_SYSTEM_CONSOLE,
    TRACE_SYSTEM_DEV
};

enum GDOX_Trace_System_Map : u8
{
    TRACE_SYSTEM_MAP_A20 = 0,
    TRACE_SYSTEM_MAP_SHADOW,
    TRACE_SYSTEM_MAP_PAM,
    TRACE_SYSTEM_MAP_DRAM
};

enum GDOX_Trace_A20_Source : u8
{
    TRACE_A20_PORT_92 = 0,
    TRACE_A20_KEYBOARD,
    TRACE_A20_RESET,
    TRACE_A20_POWER_ON,
    TRACE_A20_STATE_LOAD
};

#define TRACE_SYSTEM_EVENT_RESET       (1U << TRACE_SYSTEM_RESET)
#define TRACE_SYSTEM_EVENT_MEMORY      (1U << TRACE_SYSTEM_MEMORY_MAP)
#define TRACE_SYSTEM_EVENT_RTC_READS   (1U << TRACE_SYSTEM_RTC_READ)
#define TRACE_SYSTEM_EVENT_RTC_WRITES  ((1U << TRACE_SYSTEM_RTC_WRITE) | (1U << TRACE_SYSTEM_RTC_IRQ))
#define TRACE_SYSTEM_EVENT_RTC         (TRACE_SYSTEM_EVENT_RTC_READS | TRACE_SYSTEM_EVENT_RTC_WRITES)
#define TRACE_SYSTEM_EVENT_POST        (1U << TRACE_SYSTEM_POST)
#define TRACE_SYSTEM_EVENT_CONTROL     (1U << TRACE_SYSTEM_CONTROL)
#define TRACE_SYSTEM_EVENT_PCI         (1U << TRACE_SYSTEM_PCI)
#define TRACE_SYSTEM_EVENT_CONSOLE     ((1U << TRACE_SYSTEM_CONSOLE) | (1U << TRACE_SYSTEM_DEV))
#define TRACE_SYSTEM_EVENT_DEFAULT     (TRACE_SYSTEM_EVENT_RESET | TRACE_SYSTEM_EVENT_MEMORY | \
    TRACE_SYSTEM_EVENT_POST | TRACE_SYSTEM_EVENT_CONTROL | TRACE_SYSTEM_EVENT_RTC_WRITES | TRACE_SYSTEM_EVENT_CONSOLE)
#define TRACE_SYSTEM_EVENT_ALL         (TRACE_SYSTEM_EVENT_DEFAULT | TRACE_SYSTEM_EVENT_RTC_READS | \
    TRACE_SYSTEM_EVENT_PCI)

#define TRACE_SYSTEM_FLAG_PERFORMED    (1U << TRACE_ENTRY_TYPE_SHIFT)
#define TRACE_SYSTEM_FLAG_CHECKSUM     (2U << TRACE_ENTRY_TYPE_SHIFT)
#define TRACE_SYSTEM_FLAG_NO_DEVICE    (4U << TRACE_ENTRY_TYPE_SHIFT)
#define TRACE_SYSTEM_FLAG_READ         (8U << TRACE_ENTRY_TYPE_SHIFT)

static_assert(TRACE_VIDEO_BLT_REGISTER < 32 && TRACE_CDROM_RESET < 32, "Trace event filters exceed 32 bits");

struct GDOX_Trace_Entry
{
    u64 cycle;
    u32 pc;
    GDOX_Trace_Type type;
    u8 event;
    u16 flags;
    union
    {
        struct
        {
            u32 linear;
            u32 eip;
            u32 registers[8];
            u32 eflags;
            u16 cs;
            u16 ds;
            u16 es;
            u16 ss;
            u16 fs;
            u16 gs;
            u8 mode;
            u8 cpl;
            u8 default32;
            u8 size;
            u8 opcodes[GDOX_I386_MAX_INSTRUCTION_LENGTH];
        } cpu;

        struct
        {
            u32 from;
            u32 to;
            u32 error_code;
            u32 eflags;
            u32 from_eip;
            u32 to_eip;
            u16 ax;
            u16 bx;
            u16 cx;
            u16 dx;
            u16 si;
            u16 di;
            u16 ds;
            u16 es;
            u16 from_cs;
            u16 to_cs;
            u8 vector;
            u8 line;
            u8 has_error_code;
            u8 modes;
            u8 cpls;
            char text[GDOX_TRACE_INTERRUPT_TEXT_SIZE];
        } interrupt;

        struct
        {
            u32 value;
            u32 count;
            u32 cycles;
            u16 port;
            u8 size;
            u8 reason;
        } io;

        struct
        {
            u32 old_value;
            u32 new_value;
            u32 extra;
            u32 extra2;
            u32 eip;
            u32 to_eip;
            u16 cs;
            u16 to_cs;
            u16 selector;
            u8 cause;
            u8 vector;
            u8 vector2;
            u8 modes;
            u8 has_error_code;
        } cpu_event;

        struct
        {
            u64 value;
            u32 detail;
            u8 cause;
        } mark;

        struct
        {
            u8 chip;
            u8 line;
            u8 vector;
            u8 value;
            u8 previous;
            u8 irr;
            u8 isr;
            u8 imr;
            u8 step;
            u8 previous_isr;
        } pic;

        struct
        {
            u16 reload;
            u16 speaker_reload;
            u8 chip;
            u8 counter;
            u8 value;
            u8 access;
            u8 mode;
            u8 bcd;
            u8 complete;
            u8 latch;
            u8 enable;
            u8 sound;
            u8 speaker_mode;
            u8 edges;
        } timer;

        struct
        {
            u32 address;
            u16 count;
            u16 base_address;
            u16 base_count;
            u8 channel;
            u8 mode;
            u8 reg;
            u8 value;
            u8 terminal;
            u8 mask;
            u8 flip_flop;
            u8 reason;
            u8 page;
        } dma;

        struct
        {
            u32 param;
            u32 param2;
            u16 value;
            u16 line;
            u16 dot;
            u16 last_line;
            u16 count;
            u8 reg;
            u8 raw;
            u8 bank;
            u8 adapter;
            u8 index_bits;
            u8 previous;
        } video;

        struct
        {
            u32 destination;
            u32 source;
            u32 colors;
            u32 key;
            u16 width;
            u16 height;
            u16 dst_pitch;
            u16 src_pitch;
            u16 line;
            u16 dot;
            u8 mode;
            u8 rop;
            u8 extensions;
            u8 pixel_bytes;
            u8 kind;
        } blt;

        struct
        {
            u32 refresh;
            u32 offset;
            u32 start;
            u16 width;
            u16 height;
            u16 lines;
            u8 char_width;
            u8 char_height;
            u8 format;
            u8 extended;
            u8 bpp;
            u8 misc;
            u8 card;
            u8 adapter;
        } mode;

        struct
        {
            u32 start;
            u32 line_compare;
            u32 offset;
            u32 frame;
            u16 line;
            u8 pel;
            u8 byte_pan;
        } frame;

        struct
        {
            u32 reads;
            u32 retrace;
            u32 display_off;
            u32 frame;
            u16 first_line;
            u16 last_line;
        } status;

        struct
        {
            u16 address;
            u16 frequency;
            u8 value;
            u8 channel;
            u8 flags;
            u8 previous;
            s8 operator_index;
            u8 pairs;
        } fm;

        struct
        {
            u32 length;
            u32 rate;
            u32 remaining;
            u32 underruns;
            u8 command;
            u8 operands[3];
            u8 operand_count;
            u8 format;
            u8 flags;
            u8 dma;
            u8 dma16;
            u8 irq;
            u8 reason;
            u8 version;
            u8 minor;
            u8 queue;
            u8 rate_source;
        } pcm;

        struct
        {
            u8 index;
            u8 value;
            u8 raw;
            u8 mixer;
        } mixer;

        struct
        {
            u16 length;
            u8 value;
            u8 source;
            u8 mode;
            u8 kind;
            u8 count;
            u8 bytes[24];
        } midi;

        struct
        {
            u16 value;
            u8 index;
            u8 voice;
        } gus;

        struct
        {
            u32 value;
            u32 data;
            u32 pixels;
            u32 queued;
            u8 command;
            u8 buffer;
        } voodoo;

        struct
        {
            u16 length;
            u16 type;
            u16 address;
            u16 count;
            u8 reg;
            u8 value;
            u8 page;
            u8 cr;
            u8 isr;
            u8 imr;
            u8 bnry;
            u8 curr;
            u8 status;
            u8 reason;
            u8 destination[6];
            u8 source[6];
        } net;

        struct
        {
            u32 lba;
            u32 end_lba;
            u32 size;
            u16 limit;
            u8 command;
            u8 bytes[12];
            u8 value;
            u8 flags;
            u8 features;
            u8 sense[3];
        } cdrom;

        struct
        {
            u16 moved;
            u8 command;
            u8 track;
            u8 sector;
            u8 data;
            u8 status;
            u8 value;
            s8 drive;
            u8 cylinder;
            u8 count;
            u8 rate;
            u8 non_dma;
            u8 kind;
            u8 bytes[10];
        } fdc;

        struct
        {
            u8 id;
            u8 command;
            u8 status;
            u8 error;
            u8 count;
            u8 sector;
            u8 head;
            u16 cylinder;
            u8 features;
            u8 lba_mode;
        } ide;

        struct
        {
            u8 value;
            u8 flags;
            u8 key;
            u8 pending;
            u8 count;
            u8 scan_set;
            u8 reason;
            u8 previous;
            u8 bytes[8];
        } keyboard;

        struct
        {
            s16 positions[GDOX_JOYSTICK_AXIS_COUNT];
            u16 buttons;
            u16 previous;
            u8 device;
            u8 count;
            u8 command;
            u8 switches;
            u8 data[8];
        } input;


        struct
        {
            u16 divisor;
            u8 port;
            u8 reg;
            u8 value;
            u8 flags;
            u8 lcr;
            u8 fcr;
            u8 ier;
            u8 mcr;
            u8 lsr;
            u8 msr;
            u8 iir;
        } serial;

        struct
        {
            u16 port;
            u8 value;
            u8 data;
            u8 previous;
        } parallel;

        struct
        {
            u64 read_only_pages;
            u64 open_pages;
            u32 data;
            u16 port;
            u8 value;
            u8 address;
            u8 flags;
            u8 raw;
            u8 previous;
            u8 source;
            u8 kind;
            u8 bus;
            u8 device;
            u8 function;
        } system;

        struct
        {
            u16 port;
            u8 length;
            u8 kind;
            char text[64];
        } console;

        u8 raw[GDOX_TRACE_ENTRY_SIZE - GDOX_TRACE_HEADER_SIZE];
    };
};

static_assert(sizeof(GDOX_Trace_Entry) == GDOX_TRACE_ENTRY_SIZE, "GDOX_Trace_Entry must fill one slot");

struct GDOX_Trace_Range
{
    u32 first;
    u32 last;
    u32 events;
};

struct GDOX_Trace_Function
{
    u8 vector;
    u8 match;
    u16 value;
};

enum GDOX_Trace_Function_Match : u8
{
    TRACE_FUNCTION_ANY = 0,
    TRACE_FUNCTION_AH,
    TRACE_FUNCTION_AX
};

struct GDOX_Trace_Filters
{
    GDOX_Trace_Range code_include[GDOX_TRACE_FILTER_RANGES];
    GDOX_Trace_Range code_exclude[GDOX_TRACE_FILTER_RANGES];
    GDOX_Trace_Range port_include[GDOX_TRACE_FILTER_RANGES];
    GDOX_Trace_Range port_exclude[GDOX_TRACE_FILTER_RANGES];
    GDOX_Trace_Function functions[GDOX_TRACE_FILTER_FUNCTIONS];
    u32 vectors[8];
    u8 code_include_count;
    u8 code_exclude_count;
    u8 port_include_count;
    u8 port_exclude_count;
    u8 function_count;
};

class TraceLogger
{
public:
    TraceLogger();
    ~TraceLogger();
    static TraceLogger* GetDisabled();
    static TraceLogger* Resolve(TraceLogger* trace_logger);
    void Init(const u64* clocks);
    void SetPCSource(const u32* base, const u32* offset);
    void Reset();
    bool SetCapacity(u32 capacity);
    void SetActive(bool active);
    INLINE bool IsEnabled(GDOX_Trace_Type type) const;
    INLINE bool IsEventEnabled(GDOX_Trace_Type type, u8 event) const;
    INLINE bool IsRecording() const;
    INLINE bool IsCodeTraced(u32 linear) const;
    INLINE bool IsPortTraced(u16 port, u8 event) const;
    INLINE bool IsSoftwareInterruptTraced(u8 vector, u16 ax) const;
    void SetFilters(const GDOX_Trace_Filters& filters);
    const GDOX_Trace_Filters& GetFilters() const;
    static void ClearFilters(GDOX_Trace_Filters& filters);
    INLINE GDOX_Trace_Entry* Begin(GDOX_Trace_Type type, u8 event);
    INLINE GDOX_Trace_Entry* Begin(GDOX_Trace_Type type, u8 event, u64 clocks);
    INLINE GDOX_Trace_Entry* BeginRaw(GDOX_Trace_Type type, u8 event, u64 clocks);
    INLINE void TraceLog(const GDOX_Trace_Entry& entry);
    INLINE void SetDispatching(bool dispatching);
    INLINE u64 GetClocks() const;
    void Mark(u8 event, u64 value, u32 detail = 0, u8 cause = 0);
    void SetEnabledFlags(u32 flags);
    void SetEventFilter(GDOX_Trace_Type type, u32 filter);
    u32 GetEnabledFlags() const;
    u32 GetEventFilter(GDOX_Trace_Type type) const;
    u32 GetCount() const;
    u32 GetCapacity() const;
    u64 GetTotalLogged() const;
    u64 GetSequence() const;
    u64 GetOverwritten() const;
    const GDOX_Trace_Entry& GetEntry(u32 index) const;

private:
    void UpdateEnabled();
    bool MatchesCode(u32 linear) const;
    bool MatchesPort(u16 port, u8 event) const;
    bool MatchesFunction(u8 vector, u16 ax) const;

private:
    GDOX_Trace_Entry* m_buffer;
    const u64* m_clocks;
    const u32* m_pc_base;
    const u32* m_pc_offset;
    u32 m_position;
    u32 m_count;
    u32 m_capacity;
    u32 m_enabled_flags;
    u32 m_active_flags;
    bool m_active;
    bool m_dispatching;
    u32 m_event_filters[TRACE_TYPE_COUNT];
    u64 m_total_logged;
    u64 m_sequence;
    GDOX_Trace_Filters m_filters;
    bool m_code_filtered;
    bool m_port_filtered;
    bool m_vector_filtered;
};

// Events are only recorded while the debugger runs the machine
INLINE bool TraceLogger::IsEnabled(GDOX_Trace_Type type) const
{
#if !defined(GDOX_DISABLE_DISASSEMBLER)
    return IS_SET_BIT(m_active_flags, type);
#else
    UNUSED(type);
    return false;
#endif
}

INLINE bool TraceLogger::IsEventEnabled(GDOX_Trace_Type type, u8 event) const
{
#if !defined(GDOX_DISABLE_DISASSEMBLER)
    return IS_SET_BIT(m_active_flags, type) && IS_SET_BIT(m_event_filters[type], event);
#else
    UNUSED(type);
    UNUSED(event);
    return false;
#endif
}

INLINE bool TraceLogger::IsRecording() const
{
#if !defined(GDOX_DISABLE_DISASSEMBLER)
    return m_active_flags != 0;
#else
    return false;
#endif
}

INLINE bool TraceLogger::IsCodeTraced(u32 linear) const
{
    return !m_code_filtered || MatchesCode(linear);
}

INLINE bool TraceLogger::IsPortTraced(u16 port, u8 event) const
{
    return !m_port_filtered || MatchesPort(port, event);
}

INLINE bool TraceLogger::IsSoftwareInterruptTraced(u8 vector, u16 ax) const
{
    return !m_vector_filtered || MatchesFunction(vector, ax);
}

INLINE GDOX_Trace_Entry* TraceLogger::Begin(GDOX_Trace_Type type, u8 event)
{
    return Begin(type, event, *m_clocks);
}

INLINE GDOX_Trace_Entry* TraceLogger::Begin(GDOX_Trace_Type type, u8 event, u64 clocks)
{
    GDOX_Trace_Entry* entry = BeginRaw(type, event, clocks);
    memset((u8*)entry + GDOX_TRACE_HEADER_SIZE, 0, GDOX_TRACE_ENTRY_SIZE - GDOX_TRACE_HEADER_SIZE);
    return entry;
}

INLINE GDOX_Trace_Entry* TraceLogger::BeginRaw(GDOX_Trace_Type type, u8 event, u64 clocks)
{
#if !defined(GDOX_DISABLE_DISASSEMBLER)
    GDOX_Trace_Entry* entry = &m_buffer[m_position];
    m_position = m_position + 1 == m_capacity ? 0 : m_position + 1;

    if (m_count < m_capacity)
        m_count++;

    m_total_logged++;
    m_sequence++;

    entry->cycle = clocks;
    entry->pc = *m_pc_base + *m_pc_offset;
    entry->type = type;
    entry->event = event;
    entry->flags = m_dispatching ? TRACE_ENTRY_NEXT_PC : 0;
    return entry;
#else
    static GDOX_Trace_Entry k_scratch;
    UNUSED(type);
    UNUSED(event);
    UNUSED(clocks);
    return &k_scratch;
#endif
}

INLINE void TraceLogger::TraceLog(const GDOX_Trace_Entry& entry)
{
    GDOX_Trace_Entry* slot = BeginRaw(entry.type, entry.event, *m_clocks);
    slot->flags |= entry.flags;
    memcpy((u8*)slot + GDOX_TRACE_HEADER_SIZE, (const u8*)&entry + GDOX_TRACE_HEADER_SIZE,
        GDOX_TRACE_ENTRY_SIZE - GDOX_TRACE_HEADER_SIZE);
}

INLINE void TraceLogger::SetDispatching(bool dispatching)
{
    m_dispatching = dispatching;
}

INLINE u64 TraceLogger::GetClocks() const
{
    return *m_clocks;
}

#endif /* TRACE_LOGGER_H */
