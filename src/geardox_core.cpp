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

#include <fstream>
#include <sstream>
#include <time.h>
#include "geardox_core.h"
#include "audio/audio.h"
#include "cdrom/cdrom_audio.h"
#include "cdrom/cdrom_media.h"
#include "drive/fdc.h"
#include "drive/floppy_image.h"
#include "drive/hard_disk_image.h"
#include "drive/ide_controller.h"
#include "media/firmware.h"
#include "input/input.h"
#include "input/i8042.h"
#include "input/keyboard.h"
#include "input/ps2_mouse.h"
#include "input/game_port.h"
#include "common/memory_stream.h"
#include "common/profiler.h"
#include "common/trace_logger.h"
#include "common/state_serializer.h"
#include "media/media.h"
#include "system/memory.h"
#include "i386/i386.h"
#include "i386/x87.h"
#include "system/dma.h"
#include "system/io.h"
#include "system/pic.h"
#include "system/pit.h"
#include "system/scheduler.h"
#include "system/system_control.h"
#include "system/mc146818.h"
#include "system/firmware_config.h"
#include "system/debug_console.h"
#include "system/serial_ports.h"
#include "system/parallel_port.h"
#include "system/machine_profiles.h"
#include "video/video.h"

GeardoxCore::GeardoxCore()
{
    InitPointer(m_audio);
    InitPointer(m_firmware);
    InitPointer(m_input);
    InitPointer(m_media);
    InitPointer(m_memory);
    InitPointer(m_i386);
    InitPointer(m_x87);
    InitPointer(m_io);
    InitPointer(m_pic);
    InitPointer(m_pit);
    InitPointer(m_dma);
    InitPointer(m_system_control);
    InitPointer(m_i8042);
    InitPointer(m_keyboard);
    InitPointer(m_ps2_mouse);
    InitPointer(m_serial_ports);
    InitPointer(m_parallel_port);
    InitPointer(m_game_port);
    InitPointer(m_rtc);
    InitPointer(m_firmware_config);
    InitPointer(m_debug_console);
    InitPointer(m_scheduler);
    InitPointer(m_video);
    InitPointer(m_cdrom_media);
    InitPointer(m_cdrom_audio);
    InitPointer(m_fdc);
    InitPointer(m_ide);
    InitPointer(m_trace_logger);
    InitPointer(m_profiler);
    InitPointer(m_frame_buffer);

    clear_machine_settings(&m_machine_settings);
    clear_machine_settings(&m_game_settings);
    m_machine_settings.model = k_machine_default_model;
    m_game = false;
    memset(&m_card_bus_waits, 0, sizeof(m_card_bus_waits));

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        m_hard_disk_write_protected[i] = false;
        m_hard_disk_pending[i] = false;
    }

    m_hard_disk_state_changes = 0;
    m_boot_device = GDOX_BOOT_DEVICE_AUTO;
    m_ram_size_locked = false;
    m_boot_menu = false;
    m_game_target = -1;
    m_debug_console_enabled = true;
    m_debug_console_log = false;
    ResolveMachineConfig();
    m_machine_config = m_pending_machine_config;
    m_machine_report = m_pending_machine_report;
    m_powered = false;
    m_paused = false;
    m_debug_step = GDOX_DEBUG_STEP_NONE;
    m_debug_step_resume = false;
    m_debug_interrupt_deferred = false;
}

GeardoxCore::~GeardoxCore()
{
    SafeDelete(m_audio);
    SafeDelete(m_input);
    SafeDelete(m_media);
    SafeDelete(m_i386);
    SafeDelete(m_x87);
    SafeDelete(m_io);
    SafeDelete(m_pic);
    SafeDelete(m_pit);
    SafeDelete(m_dma);
    SafeDelete(m_system_control);
    SafeDelete(m_i8042);
    SafeDelete(m_keyboard);
    SafeDelete(m_ps2_mouse);
    SafeDelete(m_serial_ports);
    SafeDelete(m_parallel_port);
    SafeDelete(m_game_port);
    SafeDelete(m_rtc);
    SafeDelete(m_firmware_config);
    SafeDelete(m_debug_console);
    SafeDelete(m_scheduler);
    SafeDelete(m_video);
    SafeDelete(m_cdrom_audio);
    SafeDelete(m_fdc);
    SafeDelete(m_ide);


    SafeDelete(m_trace_logger);
    SafeDelete(m_profiler);
    SafeDelete(m_memory);
    SafeDelete(m_firmware);
    SafeDelete(m_cdrom_media);
}

void GeardoxCore::Init()
{
    if (!IsValidPointer(m_cdrom_media))
        m_cdrom_media = new CdRomMedia();

    if (!IsValidPointer(m_cdrom_audio))
        m_cdrom_audio = new CdRomAudio(m_cdrom_media);

    if (!IsValidPointer(m_audio))
        m_audio = new Audio();

    if (!IsValidPointer(m_firmware))
        m_firmware = new Firmware();

    if (!IsValidPointer(m_input))
        m_input = new Input();

    if (!IsValidPointer(m_media))
        m_media = new Media(m_cdrom_media);

    if (!IsValidPointer(m_memory))
        m_memory = new Memory();

    if (!IsValidPointer(m_i386))
        m_i386 = new I386();

    if (!IsValidPointer(m_x87))
        m_x87 = new X87();

    if (!IsValidPointer(m_io))
        m_io = new IO();

    if (!IsValidPointer(m_pic))
        m_pic = new PIC();

    if (!IsValidPointer(m_pit))
        m_pit = new PIT();

    if (!IsValidPointer(m_dma))
        m_dma = new DMA();

    if (!IsValidPointer(m_system_control))
        m_system_control = new SystemControl();

    if (!IsValidPointer(m_i8042))
        m_i8042 = new I8042();

    if (!IsValidPointer(m_keyboard))
        m_keyboard = new Keyboard();

    if (!IsValidPointer(m_ps2_mouse))
        m_ps2_mouse = new PS2Mouse();

    if (!IsValidPointer(m_serial_ports))
        m_serial_ports = new SerialPorts();

    if (!IsValidPointer(m_parallel_port))
        m_parallel_port = new ParallelPort();

    if (!IsValidPointer(m_game_port))
        m_game_port = new GamePort();

    if (!IsValidPointer(m_rtc))
        m_rtc = new MC146818();

    if (!IsValidPointer(m_firmware_config))
        m_firmware_config = new FirmwareConfig();

    if (!IsValidPointer(m_debug_console))
        m_debug_console = new DebugConsole();

    if (!IsValidPointer(m_scheduler))
        m_scheduler = new Scheduler();

    if (!IsValidPointer(m_video))
        m_video = new Video();

    if (!IsValidPointer(m_fdc))
        m_fdc = new FDC();

    if (!IsValidPointer(m_ide))
        m_ide = new IdeController();

    if (!IsValidPointer(m_trace_logger))
        m_trace_logger = new TraceLogger();

    if (!IsValidPointer(m_profiler))
        m_profiler = new Profiler();

    m_firmware->Init();
    m_scheduler->Init();
    m_memory->Init();
    m_cdrom_media->Init();
    m_cdrom_audio->Init();
    m_pic->Init();
    m_dma->Init(m_memory, m_scheduler);
    m_audio->Init(m_scheduler, m_cdrom_audio, m_pic, m_pit, m_memory, m_dma);
    m_pit->Init(m_pic, m_scheduler, m_audio);
    m_system_control->Init(m_memory, m_pic, m_pit);
    m_keyboard->Init();
    m_i8042->Init(m_pic, m_scheduler, m_system_control, m_keyboard);
    m_ps2_mouse->Init(m_input);
    m_serial_ports->Init(m_pic, m_scheduler, m_input);
    m_game_port->Init(m_input);
    m_rtc->Init(m_pic, m_scheduler);
    m_firmware_config->Init();
    m_debug_console->Init();
    m_video->Init(m_pic, m_pit, m_scheduler, m_memory);
    m_fdc->Init(m_pic, m_scheduler, m_dma);
    m_ide->Init(m_pic, m_scheduler, m_cdrom_media, m_cdrom_audio, m_audio);
    m_io->Init(m_scheduler, m_audio, m_pic, m_pit, m_video, m_memory, m_fdc, m_ide, m_system_control, m_rtc,
        m_firmware_config, m_debug_console, m_i8042, m_dma, m_serial_ports, m_parallel_port, m_game_port);
    m_i386->Init(m_memory, m_io);
    m_i386->SetFPUErrorCallback(this, &GeardoxCore::FPUErrorCallback);
    m_i386->SetDecodedPorts(m_io->GetDecodedPorts());
    m_input->Init();
    m_trace_logger->Init(&m_scheduler->GetState()->clocks);
    m_profiler->Init(&m_scheduler->GetState()->clocks);
    m_i386->SetTraceLogger(m_trace_logger);
    m_i386->SetProfiler(m_profiler);
    m_memory->SetTraceLogger(m_trace_logger);
    m_pic->SetTraceLogger(m_trace_logger);
    m_pit->SetTraceLogger(m_trace_logger);
    m_dma->SetTraceLogger(m_trace_logger);
    m_system_control->SetTraceLogger(m_trace_logger);
    m_keyboard->SetTraceLogger(m_trace_logger);
    m_i8042->SetTraceLogger(m_trace_logger);
    m_rtc->SetTraceLogger(m_trace_logger);
    m_video->SetTraceLogger(m_trace_logger);
    m_audio->SetTraceLogger(m_trace_logger);
    m_fdc->SetTraceLogger(m_trace_logger);
    m_ide->SetTraceLogger(m_trace_logger);
    m_input->SetTraceLogger(m_trace_logger);
    m_ps2_mouse->SetTraceLogger(m_trace_logger);
    m_serial_ports->SetTraceLogger(m_trace_logger);
    m_parallel_port->SetTraceLogger(m_trace_logger);
    m_game_port->SetTraceLogger(m_trace_logger);
    m_media->Init();
    Reset();
}

GDOX_Run_Result GeardoxCore::RunToFrame(u8* frame_buffer, s16* sample_buffer, int* sample_count, bool render)
{
    return RunToFrameTemplate<false>(frame_buffer, sample_buffer, sample_count, NULL, render);
}

GDOX_Run_Result GeardoxCore::RunToFrame(u8* frame_buffer, s16* sample_buffer, int* sample_count, GDOX_Debug_Run* debug,
    bool render)
{
#if defined(GDOX_DISABLE_DISASSEMBLER)
    return RunToFrameTemplate<false>(frame_buffer, sample_buffer, sample_count, debug, render);
#else
    return RunToFrameTemplate<true>(frame_buffer, sample_buffer, sample_count, debug, render);
#endif
}

template<bool debugger>
GDOX_Run_Result GeardoxCore::RunToFrameTemplate(u8* frame_buffer, s16* sample_buffer, int* sample_count,
    GDOX_Debug_Run* debug, bool render)
{
    m_frame_buffer = frame_buffer;

    if (sample_count != NULL)
        *sample_count = 0;

    if (m_paused)
        return GDOX_RUN_PAUSED;

    if (!m_powered || !IsValidPointer(m_firmware) || !m_firmware->IsReady())
        return GDOX_RUN_NOT_READY;

    u64 frame_start = m_scheduler->GetClocks();
    m_video->BeginFrame(frame_buffer, render);

    bool debugging = debugger && IsValidPointer(debug);
    m_trace_logger->SetActive(debugging);
    m_profiler->SetActive(debugging);
    m_i386->EnableDebuggerChecks(debugging);
    m_dma->SetDebugCallback(debugging ? &GeardoxCore::DMADebugCallback : NULL, m_i386);

    if (debugging)
    {
        RunDebuggerFrame(frame_start, debug);

        if (m_video->IsFrameReady())
        {
            m_profiler->CountFrame();
            m_i386->UpdateVBlankWatch();
        }
    }
    else
        RunFrame(frame_start);

    EndFrame(frame_start, sample_buffer, sample_count);

    return GDOX_RUN_FRAME_READY;
}

void GeardoxCore::RunFrame(u64 frame_start)
{
    if (unlikely(m_debug_interrupt_deferred))
    {
        m_debug_interrupt_deferred = false;
        GDOX_Bus_Access_Context context = BeginSlice();
        EnterPendingInterrupt(context);
    }

    while (!IsFrameDone(frame_start))
    {
        u32 slice = m_scheduler->GetSliceClocks(GetFrameLimit(frame_start));
        u32 cycles = m_scheduler->GetSliceCycles(slice);
        GDOX_Bus_Access_Context context = BeginSlice();

        I386_Run_Result result = m_i386->RunFor(cycles, context, false, m_pic->IsInterruptPending());
        CompleteSlice(result, context, slice, false);
    }
}

void GeardoxCore::RunDebuggerFrame(u64 frame_start, GDOX_Debug_Run* debug)
{
    if (BeginDebuggerFrame(debug))
        return;

    if (unlikely(m_debug_step != GDOX_DEBUG_STEP_NONE))
    {
        RunDebuggerSteps(frame_start, debug);
        return;
    }

    if (!m_i386->NeedsDebuggerSteps())
    {
        RunFrame(frame_start);
        return;
    }

    bool regular = debug->stop_on_breakpoint;
    bool run_to = debug->stop_on_run_to_breakpoint;

    while (!IsFrameDone(frame_start))
    {
        if (m_i386->CheckDebuggerBreakpoints(regular, run_to))
        {
            StopDebugger(debug, true);
            break;
        }

        u32 slice = m_scheduler->GetSliceClocks(GetFrameLimit(frame_start));
        GDOX_Bus_Access_Context context = BeginSlice();
        m_i386->RunInstruction(context);
        CompleteSlice(m_i386->GetStepInfo(), context, slice, regular);

        if (unlikely(m_i386->IsDebuggerHitPending()) && AcceptDebuggerHit(debug))
            break;
    }
}

NO_INLINE void GeardoxCore::RunDebuggerSteps(u64 frame_start, GDOX_Debug_Run* debug)
{
    bool regular = debug->stop_on_breakpoint;
    bool run_to = debug->stop_on_run_to_breakpoint;

    while (!IsFrameDone(frame_start))
    {
        if (m_i386->CheckDebuggerBreakpoints(regular, run_to) && BreakpointStopsDebugger(debug))
            break;

        if (m_debug_step == GDOX_DEBUG_STEP_INSTRUCTION)
        {
            if (RunDebuggerStep(frame_start, debug))
                break;

            continue;
        }

        u32 slice = m_scheduler->GetSliceClocks(GetFrameLimit(frame_start));
        GDOX_Bus_Access_Context context = BeginSlice();
        m_i386->RunInstruction(context);
        CompleteSlice(m_i386->GetStepInfo(), context, slice, regular);

        if (unlikely(m_i386->IsDebuggerHitPending()) && AcceptDebuggerHit(debug))
            break;
    }
}

NO_INLINE bool GeardoxCore::BeginDebuggerFrame(GDOX_Debug_Run* debug)
{
    debug->stopped = false;
    debug->breakpoint_hit = false;

    if (!debug->step_debugger && !debug->step_over)
        m_debug_step = GDOX_DEBUG_STEP_NONE;
    else if (debug->step_start || (m_debug_step == GDOX_DEBUG_STEP_NONE))
    {
        m_debug_step = GDOX_DEBUG_STEP_INSTRUCTION;
        m_debug_step_resume = false;
        m_i386->ClearRunToBreakpoint();
    }

    if (!m_debug_interrupt_deferred)
        return false;

    m_debug_interrupt_deferred = false;
    return RunDeferredInterrupt(debug);
}

// An IRQ handler that ran in the middle of the stepped instruction returned to it, so the step goes on
NO_INLINE bool GeardoxCore::BreakpointStopsDebugger(GDOX_Debug_Run* debug)
{
    if ((m_debug_step == GDOX_DEBUG_STEP_RETURN) && m_debug_step_resume && m_i386->RunToBreakpointHit())
    {
        m_i386->ClearBreakpointHit();
        m_debug_step = GDOX_DEBUG_STEP_INSTRUCTION;
        m_debug_step_resume = false;
        return false;
    }

    StopDebugger(debug, true);
    return true;
}

// One instruction of a step, returns true when the debugger stops
NO_INLINE bool GeardoxCore::RunDebuggerStep(u64 frame_start, GDOX_Debug_Run* debug)
{
    u16 stack_selector = 0;
    u32 stack_pointer = 0;

    if (debug->step_over)
        m_i386->GetStackPointer(stack_selector, stack_pointer);

    u32 slice = m_scheduler->GetSliceClocks(GetFrameLimit(frame_start));
    GDOX_Bus_Access_Context context = BeginSlice();
    m_i386->RunInstruction(context);

    u32 call_return_linear = 0;

    // Step Over runs a call or a software interrupt until it returns
    if (debug->step_over && m_i386->GetStepCall(call_return_linear))
    {
        m_i386->AddRunToBreakpoint(call_return_linear, stack_selector, stack_pointer);
        m_debug_step = GDOX_DEBUG_STEP_RETURN;
        m_debug_step_resume = false;
        CompleteSlice(m_i386->GetStepInfo(), context, slice, debug->stop_on_breakpoint);
        return unlikely(m_i386->IsDebuggerHitPending()) && AcceptDebuggerHit(debug);
    }

    u32 return_linear = m_i386->GetCurrentLinearPC();
    bool repeat = m_i386->IsRepeatActive();

    m_i386->GetStackPointer(stack_selector, stack_pointer);

    bool interrupted = CompleteSlice(m_i386->GetStepInfo(), context, slice, debug->stop_on_breakpoint);

    if (unlikely(m_i386->IsDebuggerHitPending()) && AcceptDebuggerHit(debug))
        return true;

    if (interrupted)
    {
        if (!StepInterrupt(debug, return_linear, stack_selector, stack_pointer, repeat))
            return false;
    }
    else if (m_i386->IsRepeatActive() || m_i386->Halted())
        return false;

    StopDebugger(debug, false);
    return true;
}

bool GeardoxCore::RunDeferredInterrupt(GDOX_Debug_Run* debug)
{
    u32 return_linear = m_i386->GetCurrentLinearPC();
    u16 stack_selector = 0;
    u32 stack_pointer = 0;

    m_i386->GetStackPointer(stack_selector, stack_pointer);

    GDOX_Bus_Access_Context context = BeginSlice();

    if (!EnterPendingInterrupt(context))
        return false;

    if (unlikely(m_i386->IsDebuggerHitPending()) && AcceptDebuggerHit(debug))
        return true;

    if ((m_debug_step == GDOX_DEBUG_STEP_INSTRUCTION) && StepInterrupt(debug, return_linear, stack_selector, stack_pointer, true))
    {
        StopDebugger(debug, false);
        return true;
    }

    return false;
}

bool GeardoxCore::StepInterrupt(GDOX_Debug_Run* debug, u32 return_linear, u16 stack_selector, u32 stack_pointer, bool resume)
{
    if (!debug->step_over && !debug->skip_interrupts_on_step)
        return true;

    m_i386->AddRunToBreakpoint(return_linear, stack_selector, stack_pointer);
    m_debug_step = GDOX_DEBUG_STEP_RETURN;
    m_debug_step_resume = resume;
    return false;
}

NO_INLINE bool GeardoxCore::AcceptDebuggerHit(GDOX_Debug_Run* debug)
{
    if (!debug->stop_on_breakpoint)
    {
        m_i386->DiscardDebuggerHit();
        return false;
    }

    if (!m_i386->AcceptDebuggerHit())
        return false;

    m_debug_interrupt_deferred = true;
    StopDebugger(debug, true);
    return true;
}

void GeardoxCore::StopDebugger(GDOX_Debug_Run* debug, bool breakpoint)
{
    debug->stopped = true;
    debug->breakpoint_hit = breakpoint;
    m_debug_step = GDOX_DEBUG_STEP_NONE;
    m_i386->ClearRunToBreakpoint();
}

void GeardoxCore::EndFrame(u64 frame_start, s16* sample_buffer, int* sample_count)
{
    m_video->EndFrame();

    // Debugger memory views refresh once per executed frame or debugger step
    if (m_scheduler->GetClocks() != frame_start)
        m_memory->InvalidateDebugSnapshot();

    m_audio->Synchronize(m_scheduler->GetClocks());
    m_audio->EndFrame(sample_buffer, sample_count);
}

INLINE bool GeardoxCore::IsFrameDone(u64 frame_start) const
{
    return m_video->IsFrameReady() || (m_scheduler->GetClocks() >= GetFrameLimit(frame_start)) || !m_powered;
}

INLINE u64 GeardoxCore::GetFrameLimit(u64 frame_start) const
{
    return frame_start + (m_video->IsRunning() ? GDOX_MACHINE_CLOCKS_PER_FRAME * 4 : GDOX_MACHINE_CLOCKS_PER_FRAME);
}

INLINE GDOX_Bus_Access_Context GeardoxCore::BeginSlice()
{
    GDOX_Bus_Access_Context context = {};
    context.origin = GDOX_BUS_ORIGIN_CPU;
    context.clocks = m_scheduler->GetClocks();
    context.synchronize = &GeardoxCore::SynchronizeIOCallback;
    context.synchronize_memory = &GeardoxCore::SynchronizeMemoryCallback;
    context.poll_port = &GeardoxCore::PollPortCallback;
    context.synchronize_context = this;
    return context;
}

void GeardoxCore::DMADebugCallback(void* cpu, u32 address, u32 size, bool write)
{
    ((I386*)cpu)->RecordDebuggerDMA(address, size, write);
}

void GeardoxCore::FPUErrorCallback(void* core)
{
    ((GeardoxCore*)core)->m_system_control->SetFPUError();
}

// A port access moves machine time up to the start of its instruction and then to the access, running the events
// due on the way. Events due by the start stop the instruction, so the CPU samples INTR before it
GDOX_Synchronize_Result GeardoxCore::SynchronizeIOCallback(void* core, GDOX_Bus_Access_Context& context, u32 start_cycles, u32 access_cycles)
{
    GeardoxCore* geardox = (GeardoxCore*)core;
    Scheduler* scheduler = geardox->m_scheduler;
    GDOX_Synchronize_Result result = GDOX_SYNC_NO_EVENTS;
    scheduler->AddCycles(start_cycles - context.synchronized_cycles);
    context.synchronized_cycles = start_cycles;

    if (scheduler->IsEventDue())
        result = GDOX_SYNC_EVENTS_AT_START;
    else
    {
        scheduler->AddCycles(access_cycles - start_cycles);
        context.synchronized_cycles = access_cycles;

        if (scheduler->IsEventDue())
            result = GDOX_SYNC_EVENTS_AT_ACCESS;
    }

    if (result != GDOX_SYNC_NO_EVENTS)
        geardox->DispatchEvents();

    context.clocks = scheduler->GetClocks();
    return result;
}

// A memory access that a device must see in time moves machine time up to the start of its instruction
// Events due by then run before the access, and their IRQs wait for the end of the instruction
void GeardoxCore::SynchronizeMemoryCallback(void* core, GDOX_Bus_Access_Context& context)
{
    GeardoxCore* geardox = (GeardoxCore*)core;
    Scheduler* scheduler = geardox->m_scheduler;
    u32 start_cycles = geardox->m_i386->GetStartCycles();

    if (start_cycles <= context.synchronized_cycles)
        return;

    scheduler->AddCycles(start_cycles - context.synchronized_cycles);
    context.synchronized_cycles = start_cycles;

    if (scheduler->IsEventDue())
    {
        context.end_batch = true;
        geardox->DispatchEvents();
    }

    context.clocks = scheduler->GetClocks();
}

// CPU cycles from the read just synchronized while the port reads the same, without side effects or due events
u32 GeardoxCore::PollPortCallback(void* core, u16 port, int width)
{
    GeardoxCore* geardox = (GeardoxCore*)core;
    Scheduler* scheduler = geardox->m_scheduler;
    u64 stable = geardox->m_io->GetReadStableUntil(port, width, scheduler->GetClocks());

    return scheduler->GetCyclesBefore(MIN(stable, scheduler->GetNextEventClocks()));
}

// A halted CPU idles through the slice
// Events due by the end of the slice run before the CPU samples INTR at its boundary
// The debugger holds the IRQ back when the slice hit a breakpoint, so it stops right after the instruction
// Returns whether an IRQ was entered
INLINE bool GeardoxCore::CompleteSlice(const I386_Run_Result& result, GDOX_Bus_Access_Context& context, u32 slice,
    bool hold_on_hit)
{
    bool idle = (result.steps == 0) && m_i386->Halted();

    if (idle)
        m_scheduler->AddClocks(slice);
    else
        m_scheduler->AddCycles((u32)(result.cycles + context.wait_cycles - context.synchronized_cycles));

    if (m_scheduler->IsEventDue())
        DispatchEvents();

    // The board answers a shutdown cycle by resetting the CPU
    if (unlikely(m_i386->Shutdown()))
        m_system_control->RequestCPUReset(k_system_control_reset_shutdown);

    if (unlikely(m_system_control->IsCPUResetPending()))
    {
        ResetCPU();
        return false;
    }

    if (hold_on_hit && m_i386->IsDebuggerHitPending())
        return false;

    return EnterPendingInterrupt(context);
}

INLINE bool GeardoxCore::EnterPendingInterrupt(GDOX_Bus_Access_Context& context)
{
    if (!m_pic->IsInterruptPending() || !m_i386->CanAcceptMaskableInterrupt())
        return false;

    int line = 0;
    u8 vector = m_pic->AcknowledgeInterrupt(line);
    u32 interrupt_cycles = m_i386->EnterExternalInterrupt(vector, context, line);
    m_scheduler->AddCycles(interrupt_cycles);

    if (m_scheduler->IsEventDue())
        DispatchEvents();

    return true;
}

INLINE void GeardoxCore::DispatchEvents()
{
    while (m_scheduler->IsEventDue())
    {
        u64 clocks = m_scheduler->GetClocks();

        switch (m_scheduler->PopEvent())
        {
            case SCHEDULER_EVENT_PIT:
                m_pit->HandleEvent(clocks);
                break;
            case SCHEDULER_EVENT_VIDEO:
                m_video->HandleEvent(clocks);
                break;
            case SCHEDULER_EVENT_RTC:
                m_rtc->HandleEvent(clocks);
                break;
            case SCHEDULER_EVENT_FDC:
                m_fdc->HandleEvent(clocks);
                break;
            case SCHEDULER_EVENT_IDE:
                m_ide->HandleEvent(clocks);
                break;
            case SCHEDULER_EVENT_KEYBOARD:
                m_i8042->HandleEvent(clocks);
                break;
            case SCHEDULER_EVENT_DMA:
                // The DMA owns the bus for each unit it moves, so the CPU loses that time
                m_scheduler->AddClocks(m_dma->HandleEvent(clocks));

                // A block can outlast several frames, and the frame loop only looks between slices
                if (m_video->IsFrameReady())
                    return;

                break;
            case SCHEDULER_EVENT_AUDIO:
                m_audio->HandleEvent(clocks);
                break;
            case SCHEDULER_EVENT_SERIAL:
                m_serial_ports->HandleEvent(clocks);
                break;
            default:
                break;
        }
    }
}

bool GeardoxCore::PowerOn()
{
    ResolveMachineConfig();
    Reset();
    m_powered = true;
    return true;
}

void GeardoxCore::PowerOff()
{
    m_powered = false;
    FlushHardDisks();

    if (IsValidPointer(m_i386))
        m_i386->ResetDisassembler();
}

void GeardoxCore::SetMachineSettings(const GDOX_Machine_Settings& settings)
{
    m_machine_settings = settings;
    ResolveMachineConfig();

    if (!IsMachineConfigPending())
        m_machine_report = m_pending_machine_report;
}

void GeardoxCore::SetGameSettings(const GDOX_Machine_Settings& settings, const char* name)
{
    m_game_settings = settings;
    m_game_name = IsValidPointer(name) ? name : "";
    m_game = true;
    ResolveMachineConfig();

    if (!IsMachineConfigPending())
        m_machine_report = m_pending_machine_report;
}

void GeardoxCore::ClearGameSettings()
{
    clear_machine_settings(&m_game_settings);
    m_game_name.clear();
    m_game = false;
    ResolveMachineConfig();

    if (!IsMachineConfigPending())
        m_machine_report = m_pending_machine_report;
}

bool GeardoxCore::IsMachineConfigPending()
{
    return (m_pending_machine_config.model != m_machine_config.model) ||
        (m_pending_machine_config.cpu != m_machine_config.cpu) ||
        (m_pending_machine_config.ram_size != m_machine_config.ram_size) ||
        (m_pending_machine_config.floppy_drives != m_machine_config.floppy_drives) ||
        (m_pending_machine_config.cpu_clock_rate != m_machine_config.cpu_clock_rate) ||
        (m_pending_machine_config.ide != m_machine_config.ide) ||
        (m_pending_machine_config.fpu != m_machine_config.fpu);
}

// The Turbo button: off holds the CPU two thirds of the time, as OPTi's 386 and 486 chipsets do (82C391 p11, 82C491
// p12), modelled as a third of its clock. It acts at once, no reset
void GeardoxCore::SetTurbo(bool turbo)
{
    m_machine_settings.speed_mode = turbo ? 1 : 0;

    if (m_game_settings.speed_mode >= 0)
        m_game_settings.speed_mode = turbo ? 1 : 0;

    m_machine_config.speed_mode = turbo ? 1 : 0;
    m_pending_machine_config.speed_mode = m_machine_config.speed_mode;
    ApplyCPUSpeed();
    ApplyBusTiming();
}

bool GeardoxCore::IsTurbo() const
{
    return m_machine_config.speed_mode != 0;
}

u32 GeardoxCore::GetEffectiveCPUClockRate() const
{
    u32 rate = m_machine_config.cpu_clock_rate;
    return m_machine_config.speed_mode == 0 ? rate / 3 : rate;
}

IdeCdRom* GeardoxCore::GetCDROM()
{
    return IsValidPointer(m_ide) ? m_ide->GetCdRom() : NULL;
}

// A disc going in or out is the drive's tray, never a reset
bool GeardoxCore::LoadMedia(const char* file_path)
{
    if (!IsValidPointer(m_media))
        return false;

    if (IsValidPointer(m_ide))
        m_ide->GetCdRom()->EjectDisc(m_scheduler->GetClocks());

    bool loaded = m_media->LoadMedia(file_path);

    if (loaded && IsValidPointer(m_ide))
        m_ide->GetCdRom()->InsertDisc(m_scheduler->GetClocks());

    return loaded;
}

#if defined(GDOX_ENABLE_PHYSICAL_CDROM)
bool GeardoxCore::LoadPhysicalCdRom(const char* device_id)
{
    if (!IsValidPointer(m_media))
        return false;

    if (IsValidPointer(m_ide))
        m_ide->GetCdRom()->EjectDisc(m_scheduler->GetClocks());

    bool loaded = m_media->LoadPhysicalCdRom(device_id);

    if (loaded && IsValidPointer(m_ide))
        m_ide->GetCdRom()->InsertDisc(m_scheduler->GetClocks());

    return loaded;
}
#endif

void GeardoxCore::EjectMedia()
{
    if (!IsValidPointer(m_media))
        return;

    if (IsValidPointer(m_ide))
        m_ide->GetCdRom()->EjectDisc(m_scheduler->GetClocks());

    m_media->Reset();
}

void GeardoxCore::ResetMedia()
{
    Reset();
}

bool GeardoxCore::InsertFloppy(int drive, const u8* data, u32 size, bool write_protected, u32 base_crc, u32 data_crc, const u8* source, u32 source_size)
{
    return IsValidPointer(m_fdc) &&
        m_fdc->InsertDisk(drive, data, size, write_protected, base_crc, data_crc, source, source_size);
}

void GeardoxCore::EjectFloppy(int drive)
{
    if (IsValidPointer(m_fdc))
        m_fdc->EjectDisk(drive);
}

void GeardoxCore::SwapFloppies()
{
    if (IsValidPointer(m_fdc))
        m_fdc->SwapDisks();
}

FloppyDisk* GeardoxCore::GetFloppy(int drive)
{
    return IsValidPointer(m_fdc) ? m_fdc->GetDisk(drive) : NULL;
}

bool GeardoxCore::AttachHardDisk(int id, const char* path, bool write_protected, const char* working_path)
{
    if ((id < 0) || (id >= GDOX_MAX_HARD_DISKS) || !IsValidPointer(path) || !IsValidPointer(m_ide))
        return false;

    if (id == (int)m_pending_machine_config.cdrom - 1)
    {
        Log("Hard disk %d is the CD-ROM drive's position", id);
        return false;
    }

    if (m_powered)
    {
        HardDiskImage probe;

        if (!probe.Open(path, true))
            return false;

        const HardDiskImage* image = m_ide->GetDisk(id)->GetImage();
        m_hard_disk_paths[id] = path;
        m_hard_disk_working_paths[id] = IsValidPointer(working_path) ? working_path : "";
        m_hard_disk_write_protected[id] = write_protected;
        m_hard_disk_pending[id] = !image->IsOpen() || (strcmp(image->GetPath(), path) != 0) ||
            (image->IsWriteProtected() != write_protected);
    }
    else
    {
        m_hard_disk_pending[id] = false;

        if (!m_ide->GetDisk(id)->Attach(path, write_protected, working_path))
            return false;
    }

    ResolveMachineConfig();

    if (!IsMachineConfigPending())
        m_machine_report = m_pending_machine_report;

    return true;
}

void GeardoxCore::DetachHardDisk(int id)
{
    if ((id < 0) || (id >= GDOX_MAX_HARD_DISKS) || !IsValidPointer(m_ide))
        return;

    if (m_powered)
    {
        m_hard_disk_paths[id].clear();
        m_hard_disk_pending[id] = m_ide->GetDisk(id)->IsAttached();
    }
    else
    {
        m_hard_disk_pending[id] = false;
        m_ide->GetDisk(id)->Detach();
    }

    ResolveMachineConfig();

    if (!IsMachineConfigPending())
        m_machine_report = m_pending_machine_report;
}

bool GeardoxCore::FlushHardDisks()
{
    bool ok = true;

    for (int i = 0; IsValidPointer(m_ide) && (i < GDOX_MAX_HARD_DISKS); i++)
        ok = m_ide->GetDisk(i)->GetImage()->Flush() && ok;

    return ok;
}

bool GeardoxCore::IsHardDiskPending(int id) const
{
    return (id >= 0) && (id < GDOX_MAX_HARD_DISKS) && m_hard_disk_pending[id];
}

const char* GeardoxCore::GetPendingHardDiskPath(int id) const
{
    return IsHardDiskPending(id) ? m_hard_disk_paths[id].c_str() : "";
}

bool GeardoxCore::IsPendingHardDiskWriteProtected(int id) const
{
    return IsHardDiskPending(id) && m_hard_disk_write_protected[id];
}

u32 GeardoxCore::GetHardDiskStateChanges() const
{
    return m_hard_disk_state_changes;
}

void GeardoxCore::SetBootDevice(GDOX_Boot_Device device)
{
    m_boot_device = ((device >= GDOX_BOOT_DEVICE_AUTO) && (device < GDOX_BOOT_DEVICE_COUNT)) ? device : GDOX_BOOT_DEVICE_AUTO;
    ResolveMachineConfig();

    if (!IsMachineConfigPending())
        m_machine_report = m_pending_machine_report;
}

GDOX_Boot_Device GeardoxCore::GetBootDevice() const
{
    return m_boot_device;
}

void GeardoxCore::LockRAMSize(bool locked)
{
    m_ram_size_locked = locked;
    ResolveMachineConfig();

    if (!IsMachineConfigPending())
        m_machine_report = m_pending_machine_report;
}

// SeaBIOS shows its boot menu prompt for 2.5 seconds when asked to
void GeardoxCore::SetBootMenu(bool show)
{
    m_boot_menu = show;

    if (IsValidPointer(m_firmware_config))
        m_firmware_config->SetBootMenu(show, show ? k_boot_menu_wait_ms : 0);
}

void GeardoxCore::SetGameTarget(int target)
{
    m_game_target = target;

    if (IsValidPointer(m_firmware_config))
        m_firmware_config->SetGameTarget(target);
}

bool GeardoxCore::IsPowerOffRequested() const
{
    return IsValidPointer(m_system_control) && m_system_control->IsPowerOffRequested();
}

void GeardoxCore::AcknowledgePowerOff()
{
    if (IsValidPointer(m_system_control))
        m_system_control->AcknowledgePowerOff();
}

void GeardoxCore::SetDebugConsole(bool enabled, bool log)
{
    m_debug_console_enabled = enabled;
    m_debug_console_log = log;
    ApplyFirmwareDevices();
}

void GeardoxCore::ApplyBootDevice()
{
    m_boot_device = GDOX_BOOT_DEVICE_AUTO;
}

HardDiskImage* GeardoxCore::GetHardDisk(int id)
{
    return (IsValidPointer(m_ide) && (id >= 0) && (id < GDOX_MAX_HARD_DISKS)) ? m_ide->GetDisk(id)->GetImage() : NULL;
}

bool GeardoxCore::HasHardDisks()
{
    for (int i = 0; IsValidPointer(m_ide) && (i < GDOX_MAX_HARD_DISKS); i++)
    {
        if (i == (int)m_pending_machine_config.cdrom - 1)
            continue;

        if (m_hard_disk_pending[i] ? !m_hard_disk_paths[i].empty() : m_ide->GetDisk(i)->IsAttached())
            return true;
    }

    return false;
}

void GeardoxCore::ApplyHardDisks()
{
    for (int i = 0; IsValidPointer(m_ide) && (i < GDOX_MAX_HARD_DISKS); i++)
    {
        if (!m_hard_disk_pending[i])
            continue;

        m_hard_disk_pending[i] = false;
        m_ide->GetDisk(i)->Detach();

        if (!m_hard_disk_paths[i].empty())
            m_ide->GetDisk(i)->Attach(m_hard_disk_paths[i].c_str(), m_hard_disk_write_protected[i],
                m_hard_disk_working_paths[i].c_str());
    }
}

void GeardoxCore::KeyPressed(GDOX_Keys key)
{
    m_i8042->KeyPressed(key);
}

void GeardoxCore::KeyReleased(GDOX_Keys key)
{
    m_i8042->KeyReleased(key);
}

void GeardoxCore::ReleaseAllKeys()
{
    m_i8042->ReleaseAllKeys();
}

bool GeardoxCore::SaveState(const char* path, int index, bool screenshot)
{
    using namespace std;

    if (!m_powered)
    {
        Error("Machine is not powered on when trying to save state");
        return false;
    }

    string full_path = GetSaveStatePath(path, index);
    string temp_path = full_path + ".tmp";
    Debug("Saving state to %s...", full_path.c_str());

    ofstream stream;
    open_ofstream_utf8(stream, temp_path.c_str(), ios::out | ios::binary);

    if (!stream.is_open())
    {
        Error("Failed to open save state file for writing: %s", temp_path.c_str());
        return false;
    }

    size_t size = 0;

    if (!SaveState(stream, size, screenshot, true))
    {
        stream.close();
        remove_file_utf8(temp_path.c_str());
        Error("Failed to save state to file: %s", full_path.c_str());
        return false;
    }

    stream.close();

    if (!stream.good() || !rename_file_utf8(temp_path.c_str(), full_path.c_str()))
    {
        remove_file_utf8(temp_path.c_str());
        Error("Failed to write save state file: %s", full_path.c_str());
        return false;
    }

    Log("Saved state to %s", full_path.c_str());
    return true;
}

bool GeardoxCore::SaveState(u8* buffer, size_t& size, bool screenshot, bool media)
{
    using namespace std;

    Debug("Saving state to buffer [%zu bytes]...", size);

    if (!m_powered)
    {
        Error("Machine is not powered on when trying to save state");
        return false;
    }

    if (!IsValidPointer(buffer))
    {
        stringstream stream;

        if (!SaveState(stream, size, screenshot, media))
        {
            Error("Failed to save state to stream to calculate size");
            return false;
        }

        return true;
    }

    memory_stream direct_stream(reinterpret_cast<char*>(buffer), size);

    if (!SaveState(direct_stream, size, screenshot, media))
    {
        Error("Failed to save state to buffer");
        return false;
    }

    if (!direct_stream.good())
    {
        Error("Failed to save state to buffer: output buffer is too small");
        return false;
    }

    size = direct_stream.size();
    return true;
}

bool GeardoxCore::GetMaxSaveStateSize(size_t& size)
{
    return SaveState(NULL, size, false);
}

bool GeardoxCore::SaveState(std::ostream& stream, size_t& size, bool screenshot, bool media)
{
    if (!m_powered)
    {
        Error("Machine is not powered on when trying to save state");
        return false;
    }

    Debug("Serializing save state...");

    StateSerializer serializer(stream);
    Serialize(serializer, m_machine_config, GDOX_SAVESTATE_VERSION);

    GDOX_SaveState_Media state_media = {};

    for (int i = 0; i < FDC_DRIVES; i++)
    {
        state_media.floppy_inserted[i] = m_fdc->GetDisk(i)->IsInserted();
        state_media.floppy_crc[i] = m_fdc->GetDisk(i)->GetBaseCRC();
    }

    state_media.cdrom_inserted = m_media->IsReady();
    state_media.cdrom_crc = m_media->GetCRC();

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        const HardDiskImage* image = m_ide->GetDisk(i)->GetImage();
        state_media.hard_disk_attached[i] = m_ide->GetDisk(i)->IsAttached();
        state_media.hard_disk_blocks[i] = image->GetBlocks();
        state_media.hard_disk_session[i] = image->GetSession();
        state_media.hard_disk_write[i] = image->GetLastWrite();
    }

    SerializeMedia(serializer, state_media, GDOX_SAVESTATE_VERSION);

    m_scheduler->SaveState(stream);
    m_memory->SaveState(stream);
    m_i386->SaveState(stream);
    m_audio->SaveState(stream);
    m_input->SaveState(stream);
    m_pic->SaveState(stream);
    m_pit->SaveState(stream);
    m_dma->SaveState(stream);
    m_system_control->SaveState(stream);
    m_rtc->SaveState(stream);
    m_firmware_config->SaveState(stream);
    m_keyboard->SaveState(stream);
    m_ps2_mouse->SaveState(stream);
    m_i8042->SaveState(stream);
    m_serial_ports->SaveState(stream);
    m_parallel_port->SaveState(stream);
    m_game_port->SaveState(stream);
    m_video->SaveState(stream);
    m_cdrom_audio->SaveState(stream);
    m_fdc->SaveState(stream, media);
    m_ide->SaveState(stream);
    m_x87->SaveState(stream);

    if (stream.fail())
    {
        Error("Failed to serialize save state");
        return false;
    }

#if defined(__LIBRETRO__)
    GDOX_SaveState_Header_Libretro header;
    header.magic = GDOX_SAVESTATE_MAGIC;
    header.version = GDOX_SAVESTATE_VERSION;
    Debug("Save state header magic: 0x%08X", header.magic);
    Debug("Save state header version: %u", header.version);
#else
    GDOX_SaveState_Header header;
    memset(&header, 0, sizeof(header));
    header.magic = GDOX_SAVESTATE_MAGIC;
    header.version = GDOX_SAVESTATE_VERSION;
    header.timestamp = (s64)time(NULL);
    strncpy_fit(header.rom_name, m_media->GetFileName(), sizeof(header.rom_name));
    header.rom_crc = m_media->GetCRC();
    strncpy_fit(header.emu_build, GDOX_VERSION, sizeof(header.emu_build));

    Debug("Save state header magic: 0x%08X", header.magic);
    Debug("Save state header version: %u", header.version);
    Debug("Save state header timestamp: %lld", (long long)header.timestamp);
    Debug("Save state header rom name: %s", header.rom_name);
    Debug("Save state header rom crc: 0x%08X", header.rom_crc);
    Debug("Save state header emu build: %s", header.emu_build);

    if (screenshot && IsValidPointer(m_frame_buffer))
    {
        header.screenshot_width = (u16)m_video->GetFrameWidth();
        header.screenshot_height = (u16)m_video->GetFrameHeight();
        header.screenshot_size = header.screenshot_width * header.screenshot_height * 4;
        stream.write(reinterpret_cast<const char*>(m_frame_buffer), header.screenshot_size);
    }
    else
    {
        header.screenshot_size = 0;
        header.screenshot_width = 0;
        header.screenshot_height = 0;
    }

    Debug("Save state header screenshot size: %u", header.screenshot_size);
    Debug("Save state header screenshot width: %u", header.screenshot_width);
    Debug("Save state header screenshot height: %u", header.screenshot_height);
#endif

    std::streampos position = stream.tellp();

    if (position == std::streampos(-1))
    {
        Error("Failed to calculate save state size");
        return false;
    }

    size = static_cast<size_t>(position) + sizeof(header);

#if !defined(__LIBRETRO__)
    header.size = static_cast<u32>(size);
    Debug("Save state header size: %u", header.size);
#endif

    stream.write(reinterpret_cast<const char*>(&header), sizeof(header));

    if (stream.fail())
    {
        Error("Failed to write save state header");
        return false;
    }

    return true;
}

bool GeardoxCore::LoadState(const char* path, int index)
{
    using namespace std;

    bool ret = false;
    string full_path = GetSaveStatePath(path, index);
    Debug("Loading state from %s...", full_path.c_str());

    ifstream stream;
    open_ifstream_utf8(stream, full_path.c_str(), ios::in | ios::binary);

    if (!stream.fail())
    {
        ret = LoadState(stream);

        if (ret)
            Log("Loaded state from %s", full_path.c_str());
        else
            Error("Failed to load state from %s", full_path.c_str());
    }
    else
        Error("Load state file doesn't exist: %s", full_path.c_str());

    stream.close();
    return ret;
}

bool GeardoxCore::LoadState(const u8* buffer, size_t size, GDOX_SaveState_Media* media)
{
    Debug("Loading state from buffer [%zu bytes]...", size);

    if (!m_powered)
    {
        Error("Machine is not powered on when trying to load state");
        return false;
    }

    if (!IsValidPointer(buffer) || size == 0)
    {
        Error("Invalid load state buffer");
        return false;
    }

    memory_input_stream direct_stream(reinterpret_cast<const char*>(buffer), size);
    return LoadState(direct_stream, media);
}

bool GeardoxCore::LoadState(std::istream& stream, GDOX_SaveState_Media* media)
{
    using namespace std;

    if (!m_powered)
    {
        Error("Machine is not powered on when trying to load state");
        return false;
    }

    GDOX_SaveState_Header_Libretro header = {};

    stream.seekg(0, ios::end);
    size_t size = static_cast<size_t>(stream.tellg());

#if !defined(__LIBRETRO__)
    bool is_desktop_savestate = false;
    GDOX_SaveState_Header desktop_header = {};

    if (size >= sizeof(desktop_header))
    {
        stream.seekg(size - sizeof(desktop_header), ios::beg);
        stream.read(reinterpret_cast<char*>(&desktop_header), sizeof(desktop_header));

        if (desktop_header.magic == GDOX_SAVESTATE_MAGIC)
        {
            header.magic = desktop_header.magic;
            header.version = desktop_header.version;
            is_desktop_savestate = true;
            Debug("Loading desktop save state");
        }
    }
#endif

    if (header.magic != GDOX_SAVESTATE_MAGIC && size >= sizeof(header))
    {
        stream.seekg(size - sizeof(header), ios::beg);
        stream.read(reinterpret_cast<char*>(&header), sizeof(header));
    }

    stream.clear();
    stream.seekg(0, ios::beg);

    Debug("Load state header magic: 0x%08X", header.magic);
    Debug("Load state header version: %u", header.version);

    if (header.magic != GDOX_SAVESTATE_MAGIC)
    {
        Log("Invalid save state: 0x%08X", header.magic);
        return false;
    }

    if ((header.version < GDOX_SAVESTATE_MIN_VERSION) || (header.version > GDOX_SAVESTATE_VERSION))
    {
        Error("Invalid save state version: %u", header.version);
        return false;
    }

#if !defined(__LIBRETRO__)
    if (is_desktop_savestate)
    {
        Debug("Load state header size: %u", desktop_header.size);
        Debug("Load state header timestamp: %lld", (long long)desktop_header.timestamp);
        Debug("Load state header rom name: %s", desktop_header.rom_name);
        Debug("Load state header rom crc: 0x%08X", desktop_header.rom_crc);
        Debug("Load state header screenshot size: %u", desktop_header.screenshot_size);
        Debug("Load state header screenshot width: %u", desktop_header.screenshot_width);
        Debug("Load state header screenshot height: %u", desktop_header.screenshot_height);
        Debug("Load state header emu build: %s", desktop_header.emu_build);

        if (desktop_header.size != size)
        {
            Error("Invalid save state size: %u", desktop_header.size);
            return false;
        }

        if (desktop_header.rom_crc != m_media->GetCRC())
        {
            Error("Invalid save state media crc: 0x%08X", desktop_header.rom_crc);
            return false;
        }
    }
#endif

    Debug("Unserializing save state...");

    GDOX_Machine_Config machine_config = m_machine_config;
    StateSerializer serializer(stream);
    Serialize(serializer, machine_config, header.version);
    SanitizeMachineConfig(machine_config);

    // The CPU speed may differ, the hardware may not
    // The running machine keeps its own configuration either way
    bool same_machine = (m_machine_config.model == machine_config.model) &&
        (m_machine_config.cpu == machine_config.cpu) && (m_machine_config.ram_size == machine_config.ram_size) &&
        (m_machine_config.floppy_drives == machine_config.floppy_drives) &&
        (m_machine_config.ide == machine_config.ide) && (m_machine_config.fpu == machine_config.fpu) &&
        (m_machine_config.sound_card == machine_config.sound_card) &&
        (m_machine_config.sound_irq == machine_config.sound_irq) &&
        (m_machine_config.sound_dma == machine_config.sound_dma) && (m_machine_config.mouse == machine_config.mouse) &&
        (m_machine_config.cdrom == machine_config.cdrom) && (m_machine_config.cdrom_speed == machine_config.cdrom_speed) &&
        (m_machine_config.video_card == machine_config.video_card);

    if (!same_machine)
    {
        Error("Save state is for another machine configuration");
        return false;
    }

    GDOX_SaveState_Media state_media = {};
    SerializeMedia(serializer, state_media, header.version);

    if (stream.fail())
    {
        Error("Failed to unserialize save state");
        return false;
    }

    state_media.valid = true;

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        HardDiskImage* image = m_ide->GetDisk(i)->GetImage();

        if ((state_media.hard_disk_attached[i] != image->IsOpen()) ||
            (image->IsOpen() && (state_media.hard_disk_blocks[i] != image->GetBlocks())))
        {
            Error("Save state is for another hard disk in slot %d", i);
            return false;
        }
    }

    bool mount = IsValidPointer(media);

    if (mount)
        *media = state_media;

    for (int i = 0; i < FDC_DRIVES; i++)
    {
        FloppyDisk* disk = m_fdc->GetDisk(i);
        bool other_disk = state_media.floppy_inserted[i] ?
            (disk->IsInserted() ? state_media.floppy_crc[i] != disk->GetBaseCRC() : mount) :
            (mount && disk->IsInserted());

        if (other_disk)
        {
            Error("Save state is for another disk in floppy drive %d", i + 1);
            return false;
        }
    }

    bool other_disc = state_media.cdrom_inserted ?
        (m_media->IsReady() ? state_media.cdrom_crc != m_media->GetCRC() : mount) : (mount && m_media->IsReady());

    if (other_disc)
    {
        Error("Save state is for another CD-ROM disc");
        return false;
    }

    // Turbo is the board's button: the state keeps it. The scheduler's remainder belongs to the state's clock rate
    m_machine_config.speed_mode = machine_config.speed_mode;
    m_pending_machine_config.speed_mode = machine_config.speed_mode;
    ApplyCPUSpeed();
    m_scheduler->LoadState(stream);
    m_memory->LoadState(stream, header.version);
    m_i386->LoadState(stream, header.version);
    m_audio->LoadState(stream, header.version);
    m_input->LoadState(stream);
    m_pic->LoadState(stream);
    m_pit->LoadState(stream, header.version);
    m_dma->LoadState(stream, header.version);
    m_system_control->LoadState(stream, header.version);
    m_rtc->LoadState(stream, header.version);
    m_firmware_config->LoadState(stream, header.version);
    m_keyboard->LoadState(stream, header.version);
    m_ps2_mouse->LoadState(stream, header.version);
    m_i8042->LoadState(stream, header.version);
    m_serial_ports->LoadState(stream, header.version);
    m_parallel_port->LoadState(stream, header.version);
    m_game_port->LoadState(stream, header.version);
    m_video->LoadState(stream, header.version);
    m_cdrom_audio->LoadState(stream);
    m_fdc->LoadState(stream, header.version);
    m_ide->LoadState(stream, header.version);
    m_x87->LoadState(stream);

    if (stream.fail())
    {
        Error("Failed to unserialize save state");
        return false;
    }

    m_hard_disk_state_changes = 0;

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        HardDiskImage* image = m_ide->GetDisk(i)->GetImage();

        if (state_media.hard_disk_attached[i] &&
            !image->Restore(state_media.hard_disk_session[i], state_media.hard_disk_write[i]))
            m_hard_disk_state_changes |= 1 << i;
    }

    ApplyBusTiming();
    return true;
}

bool GeardoxCore::GetSaveStateHeader(int index, const char* path, GDOX_SaveState_Header* header)
{
    using namespace std;

    if (!IsValidPointer(header))
        return false;

    string full_path = GetSaveStatePath(path, index);
    Debug("Loading state header from %s...", full_path.c_str());

    ifstream stream;
    open_ifstream_utf8(stream, full_path.c_str(), ios::in | ios::binary);

    if (stream.fail())
    {
        Debug("Save state file doesn't exist: %s", full_path.c_str());
        stream.close();
        return false;
    }

    stream.seekg(0, ios::end);
    size_t savestate_size = static_cast<size_t>(stream.tellg());

    if (savestate_size < sizeof(GDOX_SaveState_Header))
    {
        Error("Invalid save state file size: %zu", savestate_size);
        stream.close();
        return false;
    }

    stream.seekg(savestate_size - sizeof(GDOX_SaveState_Header), ios::beg);
    stream.read(reinterpret_cast<char*>(header), sizeof(GDOX_SaveState_Header));

    if (stream.fail())
    {
        Error("Failed to read save state header from %s", full_path.c_str());
        stream.close();
        return false;
    }

    stream.close();

    if (header->magic != GDOX_SAVESTATE_MAGIC)
    {
        Error("Invalid save state magic: 0x%08X", header->magic);
        return false;
    }

    if (header->size != savestate_size)
    {
        Error("Invalid save state size: %u", header->size);
        return false;
    }

    return true;
}

bool GeardoxCore::GetSaveStateScreenshot(int index, const char* path, GDOX_SaveState_Screenshot* screenshot)
{
    using namespace std;

    if (!IsValidPointer(screenshot) || !IsValidPointer(screenshot->data) || screenshot->size == 0)
    {
        Error("Invalid save state screenshot buffer");
        return false;
    }

    string full_path = GetSaveStatePath(path, index);
    Debug("Loading state screenshot from %s...", full_path.c_str());

    ifstream stream;
    open_ifstream_utf8(stream, full_path.c_str(), ios::in | ios::binary);

    if (stream.fail())
    {
        Error("Save state file doesn't exist: %s", full_path.c_str());
        stream.close();
        return false;
    }

    GDOX_SaveState_Header header;

    if (!GetSaveStateHeader(index, path, &header))
    {
        Error("Invalid save state header");
        stream.close();
        return false;
    }

    if (header.screenshot_size == 0)
    {
        Debug("No screenshot data");
        stream.close();
        return false;
    }

    if ((header.screenshot_width == 0) || (header.screenshot_height == 0) ||
        (header.screenshot_width > GDOX_MAX_FRAME_BUFFER_WIDTH) || (header.screenshot_height > GDOX_MAX_FRAME_BUFFER_HEIGHT) ||
        (header.screenshot_size != (u32)header.screenshot_width * header.screenshot_height * 4))
    {
        Error("Invalid screenshot dimensions %ux%u for %u bytes", header.screenshot_width, header.screenshot_height, header.screenshot_size);
        stream.close();
        return false;
    }

    if (screenshot->size < header.screenshot_size)
    {
        Error("Invalid screenshot buffer size %u < %u", screenshot->size, header.screenshot_size);
        stream.close();
        return false;
    }

    if (header.size < sizeof(header) + header.screenshot_size)
    {
        Error("Invalid screenshot offset");
        stream.close();
        return false;
    }

    screenshot->size = header.screenshot_size;
    screenshot->width = header.screenshot_width;
    screenshot->height = header.screenshot_height;

    stream.seekg(header.size - sizeof(header) - screenshot->size, ios::beg);
    stream.read(reinterpret_cast<char*>(screenshot->data), screenshot->size);
    bool success = !stream.fail();
    stream.close();
    return success;
}

void GeardoxCore::Serialize(StateSerializer& serializer, GDOX_Machine_Config& config, int version)
{
    UNUSED(version);
    G_SERIALIZE(serializer, config.model);
    G_SERIALIZE(serializer, config.cpu);
    G_SERIALIZE(serializer, config.ram_size);
    G_SERIALIZE(serializer, config.floppy_drives);
    G_SERIALIZE_ARRAY(serializer, config.floppy_type, GDOX_MAX_FLOPPY_DRIVES);
    G_SERIALIZE(serializer, config.cpu_clock_rate);
    G_SERIALIZE(serializer, config.speed_mode);
    G_SERIALIZE(serializer, config.ide);
    G_SERIALIZE(serializer, config.fpu);
    G_SERIALIZE(serializer, config.sound_card);
    G_SERIALIZE(serializer, config.sound_irq);
    G_SERIALIZE(serializer, config.sound_dma);
    G_SERIALIZE(serializer, config.mouse);
    G_SERIALIZE(serializer, config.cdrom);
    G_SERIALIZE(serializer, config.cdrom_speed);
    G_SERIALIZE(serializer, config.video_card);
}

void GeardoxCore::SerializeMedia(StateSerializer& serializer, GDOX_SaveState_Media& media, u32 version)
{
    UNUSED(version);

    for (int i = 0; i < FDC_DRIVES; i++)
    {
        G_SERIALIZE(serializer, media.floppy_inserted[i]);
        G_SERIALIZE(serializer, media.floppy_crc[i]);
    }

    G_SERIALIZE(serializer, media.cdrom_inserted);
    G_SERIALIZE(serializer, media.cdrom_crc);

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        G_SERIALIZE(serializer, media.hard_disk_attached[i]);
        G_SERIALIZE(serializer, media.hard_disk_blocks[i]);
        G_SERIALIZE(serializer, media.hard_disk_session[i]);
        G_SERIALIZE(serializer, media.hard_disk_write[i]);
    }
}

void GeardoxCore::SanitizeMachineConfig(GDOX_Machine_Config& config)
{
    bool known_model = ((int)config.model >= 0) && ((int)config.model < GDOX_MACHINE_COUNT);

    if (!known_model || !k_machine_profiles[config.model].emulated)
        config.model = k_machine_default_model;

    const GDOX_Machine_Profile& profile = k_machine_profiles[config.model];
    const u8* sizes = profile.ram_sizes_mb;
    u64 megabyte = 1024 * 1024;
    u64 ram_mb = config.ram_size == 0 ? (u64)k_machine_default_ram_mb : ((u64)config.ram_size + megabyte - 1) / megabyte;
    int size = 0;

    while ((sizes[size + 1] != 0) && (sizes[size] < ram_mb))
        size++;

    config.cpu = profile.cpu;
    config.ram_size = (u32)(sizes[size] * megabyte);
    config.floppy_drives = profile.floppy_drives;
    config.fpu = (profile.fpu == GDOX_MACHINE_FPU_INTEGRATED) || (config.fpu && (profile.fpu != GDOX_MACHINE_FPU_NONE));
    config.speed_mode = config.speed_mode == 0 ? 0 : 1;
    config.sound_card = config.sound_card < GDOX_SOUND_CARD_COUNT ? config.sound_card : (u8)GDOX_SOUND_CARD_SB16;
    config.sound_irq = config.sound_irq == k_sound_card_irqs[1] ? k_sound_card_irqs[1] : k_sound_card_irqs[0];
    config.sound_dma = config.sound_dma == k_sound_card_dmas[1] ? k_sound_card_dmas[1] : k_sound_card_dmas[0];
    config.mouse = config.mouse < GDOX_MOUSE_TYPE_COUNT ? config.mouse : (u8)GDOX_MOUSE_PS2;
    config.cdrom = config.cdrom < GDOX_CDROM_POSITION_COUNT ? config.cdrom : (u8)GDOX_CDROM_SECONDARY_MASTER;
    config.cdrom_speed = config.cdrom_speed < GDOX_CDROM_SPEED_COUNT ? config.cdrom_speed : (u8)GDOX_CDROM_SPEED_4X;
    config.video_card = config.video_card < GDOX_VIDEO_CARD_COUNT ? config.video_card : (u8)GDOX_VIDEO_CARD_GD5428;

    if (k_video_cards[config.video_card].local_bus && ((profile.features & GDOX_MACHINE_FEATURE_LOCAL_BUS) == 0))
        config.video_card = GDOX_VIDEO_CARD_GD5428;

    if (config.cpu_clock_rate == 0)
        config.cpu_clock_rate = profile.cpu_clock_rate;
}

std::string GeardoxCore::GetSaveStatePath(const char* path, int index)
{
    using namespace std;

    if (index < 0)
    {
        if (IsValidPointer(path))
            return path;

        string full_path = m_media->GetFilePath();
        string::size_type dot_index = full_path.rfind('.');

        if (dot_index != string::npos)
            full_path.replace(dot_index + 1, full_path.length() - dot_index - 1, "state");

        return full_path;
    }

    string full_path;

    if (IsValidPointer(path))
    {
        full_path = path;
        append_path_component(full_path, m_media->GetFileName());
    }
    else
        full_path = m_media->GetFilePath();

    string::size_type dot_index = full_path.rfind('.');

    if (dot_index != string::npos)
        full_path.replace(dot_index + 1, full_path.length() - dot_index - 1, "state");

    stringstream ss;
    ss << index;
    full_path += ss.str();
    return full_path;
}

void GeardoxCore::GetRuntimeInfo(GDOX_Runtime_Info& runtime_info)
{
    runtime_info.screen_width = IsValidPointer(m_video) ? m_video->GetFrameWidth() : GDOX_FRAME_BUFFER_WIDTH;
    runtime_info.screen_height = IsValidPointer(m_video) ? m_video->GetFrameHeight() : GDOX_FRAME_BUFFER_HEIGHT;
    runtime_info.display_width = IsValidPointer(m_video) ? m_video->GetDisplayWidth() : GDOX_FRAME_BUFFER_WIDTH;
    runtime_info.display_height = IsValidPointer(m_video) ? m_video->GetDisplayHeight() : GDOX_FRAME_BUFFER_HEIGHT;
    runtime_info.frame_time = (float)((GDOX_MACHINE_CLOCKS_PER_FRAME * 1000.0) / GDOX_MACHINE_CLOCK_RATE);

    // A running CRTC paces frames at its VSYNC rate
    if (IsValidPointer(m_video) && m_video->IsRunning())
        runtime_info.frame_time = m_video->GetFrameTime();

    runtime_info.sample_rate = GDOX_AUDIO_SAMPLE_RATE;
    runtime_info.media_ready = IsValidPointer(m_media) && m_media->IsReady();
    runtime_info.bios_ready = IsValidPointer(m_firmware) && m_firmware->IsReady();
    runtime_info.paused = m_paused;
}

void GeardoxCore::Reset()
{
    m_paused = false;
    m_debug_step = GDOX_DEBUG_STEP_NONE;
    m_debug_step_resume = false;
    m_debug_interrupt_deferred = false;
    ApplyHardDisks();
    LoadMachineFirmware();
    ApplyMachineConfig();

    if (IsValidPointer(m_scheduler))
        m_scheduler->Reset();

    if (IsValidPointer(m_profiler))
        m_profiler->Reset();

    if (IsValidPointer(m_memory))
        m_memory->Reset();

    if (IsValidPointer(m_io))
        m_io->Reset();

    if (IsValidPointer(m_pic))
        m_pic->Reset();

    if (IsValidPointer(m_pit))
        m_pit->Reset();

    if (IsValidPointer(m_dma))
        m_dma->Reset();

    if (IsValidPointer(m_system_control))
        m_system_control->Reset();

    if (IsValidPointer(m_keyboard))
        m_keyboard->Reset(m_scheduler->GetClocks());

    if (IsValidPointer(m_ps2_mouse))
        m_ps2_mouse->Reset(m_scheduler->GetClocks());

    if (IsValidPointer(m_i8042))
        m_i8042->Reset();

    if (IsValidPointer(m_serial_ports))
        m_serial_ports->Reset();

    if (IsValidPointer(m_parallel_port))
        m_parallel_port->Reset();

    if (IsValidPointer(m_game_port))
        m_game_port->Reset();

    if (IsValidPointer(m_rtc))
        m_rtc->Reset(m_scheduler->GetClocks());

    if (IsValidPointer(m_firmware_config))
        m_firmware_config->Reset();

    if (IsValidPointer(m_debug_console))
        m_debug_console->Reset();

    if (IsValidPointer(m_video))
        m_video->Reset();

    if (IsValidPointer(m_cdrom_audio))
        m_cdrom_audio->Reset();

    if (IsValidPointer(m_fdc))
        m_fdc->Reset();

    if (IsValidPointer(m_ide))
        m_ide->Reset();

    InitCMOS();
    ApplyBootDevice();
    InitMemoryMap();

    if (IsValidPointer(m_i386))
        m_i386->Reset();

    if (IsValidPointer(m_audio))
        m_audio->Reset();

    if (IsValidPointer(m_input))
        m_input->Reset();

    ApplyBusTiming();
}

// Only the CPU restarts, memory, A20 and devices keep their state
void GeardoxCore::ResetCPU()
{
    m_system_control->AcknowledgeCPUReset();
    m_i386->Reset();
}

// The smallest drive that holds the disk, and a 720K disk goes to a 1.44M drive as those read both
u8 GeardoxCore::GetAutoFloppyType(int drive)
{
    FloppyDisk* disk = GetFloppy(drive);

    if (!IsValidPointer(disk) || !disk->IsInserted())
        return GDOX_FLOPPY_1440K;

    FloppyDisk_Sector sectors[k_floppy_max_sectors];

    switch (disk->GetMedia())
    {
        case FLOPPY_MEDIA_2D:
            return GDOX_FLOPPY_360K;
        case FLOPPY_MEDIA_2HD:
            // SeaBIOS checks sector numbers against the drive, and only the 2.88M drive allows DMF's
            if (disk->GetSectors(0, sectors) > 18)
                return GDOX_FLOPPY_2880K;

            return disk->GetRPM() == 360 ? GDOX_FLOPPY_1200K : GDOX_FLOPPY_1440K;
        case FLOPPY_MEDIA_ED:
            return GDOX_FLOPPY_2880K;
        default:
            return GDOX_FLOPPY_1440K;
    }
}

static int pick_setting(int game, int user, int unset, GDOX_Machine_Source* source)
{
    *source = game > unset ? GDOX_MACHINE_SOURCE_GAME : GDOX_MACHINE_SOURCE_USER;
    return game > unset ? game : user;
}

// The game's settings over the user's, then the defaults: the 486DX2-66 with 8 MB and an FPU. Only the clock, the
// video card's bus and the floppy drives follow the model or the media
void GeardoxCore::ResolveMachineConfig()
{
    GDOX_Machine_Config& config = m_pending_machine_config;
    GDOX_Machine_Report& report = m_pending_machine_report;
    const GDOX_Machine_Settings& game = m_game_settings;
    const GDOX_Machine_Settings& user = m_machine_settings;
    GDOX_Machine_Source floppy_source;
    GDOX_Machine_Source source;
    u32 megabyte = 1024 * 1024;

    int model = pick_setting(game.model, user.model, -1, &report.model);
    int cpu_mhz = pick_setting(game.cpu_mhz, user.cpu_mhz, 0, &report.cpu_clock);
    int ram_mb = pick_setting(game.ram_mb, user.ram_mb, 0, &report.ram);
    int fpu = pick_setting(game.fpu, user.fpu, -1, &report.fpu);
    int speed_mode = pick_setting(game.speed_mode, user.speed_mode, -1, &report.speed_mode);
    bool known_model = (model >= 0) && (model < GDOX_MACHINE_COUNT);

    config.model = known_model ? (GDOX_Machine_Model)model : k_machine_default_model;
    config.cpu_clock_rate = cpu_mhz > 0 ? (u32)cpu_mhz * 1000000 : 0;
    config.ram_size = (u32)(ram_mb > 0 ? ram_mb : k_machine_default_ram_mb) * megabyte;
    config.ide = HasHardDisks();
    config.fpu = fpu != 0;
    config.speed_mode = speed_mode != 0 ? 1 : 0;

    if (cpu_mhz <= 0)
        report.cpu_clock = GDOX_MACHINE_SOURCE_MODEL;

    for (int i = 0; i < GDOX_MAX_FLOPPY_DRIVES; i++)
    {
        int type = pick_setting(game.floppy_type[i], user.floppy_type[i], 0, &floppy_source);
        config.floppy_type[i] = (type > 0 && type < GDOX_FLOPPY_TYPE_COUNT) ? (u8)type : GetAutoFloppyType(i);
    }

    int sound_card = pick_setting(game.sound_card, user.sound_card, -1, &source);
    int mouse = pick_setting(game.mouse, user.mouse, -1, &source);
    int cdrom = pick_setting(game.cdrom, user.cdrom, -1, &source);
    int cdrom_speed = pick_setting(game.cdrom_speed, user.cdrom_speed, -1, &source);
    int video_card = pick_setting(game.video_card, user.video_card, -1, &source);
    bool local_bus = (k_machine_profiles[config.model].features & GDOX_MACHINE_FEATURE_LOCAL_BUS) != 0;

    config.sound_card = (sound_card >= 0) && (sound_card < GDOX_SOUND_CARD_COUNT) ? (u8)sound_card : (u8)GDOX_SOUND_CARD_SB16;
    config.sound_irq = (u8)pick_setting(game.sound_irq, user.sound_irq, 0, &source);
    config.sound_dma = (u8)pick_setting(game.sound_dma, user.sound_dma, 0, &source);
    config.mouse = (mouse >= 0) && (mouse < GDOX_MOUSE_TYPE_COUNT) ? (u8)mouse : (u8)GDOX_MOUSE_PS2;
    config.cdrom = (cdrom >= 0) && (cdrom < GDOX_CDROM_POSITION_COUNT) ? (u8)cdrom : (u8)GDOX_CDROM_SECONDARY_MASTER;
    config.cdrom_speed = (cdrom_speed >= 0) && (cdrom_speed < GDOX_CDROM_SPEED_COUNT) ? (u8)cdrom_speed : (u8)GDOX_CDROM_SPEED_4X;

    if ((video_card >= 0) && (video_card < GDOX_VIDEO_CARD_COUNT))
        config.video_card = (u8)video_card;
    else
        config.video_card = local_bus ? (u8)GDOX_VIDEO_CARD_GD5428_VLB : (u8)GDOX_VIDEO_CARD_GD5428;

    report.title = m_game ? m_game_name.c_str() : NULL;
    report.warning_count = 0;

    SanitizeMachineConfig(config);

    if (k_machine_profiles[config.model].fpu == GDOX_MACHINE_FPU_INTEGRATED)
        report.fpu = GDOX_MACHINE_SOURCE_MODEL;

    if (!known_model || !k_machine_profiles[model].emulated)
        snprintf(report.warnings[report.warning_count++], GDOX_MACHINE_WARNING_SIZE, "Unknown model, the %s runs",
            k_machine_profiles[config.model].name);

    if ((fpu > 0) && !config.fpu)
        snprintf(report.warnings[report.warning_count++], GDOX_MACHINE_WARNING_SIZE, "This machine has no socket for an FPU");

    if ((video_card == GDOX_VIDEO_CARD_GD5428_VLB) && (config.video_card != GDOX_VIDEO_CARD_GD5428_VLB))
        snprintf(report.warnings[report.warning_count++], GDOX_MACHINE_WARNING_SIZE, "This machine has no VL-Bus slot");

    if ((report.ram == GDOX_MACHINE_SOURCE_GAME) && ((u32)ram_mb * megabyte != config.ram_size))
        snprintf(report.warnings[report.warning_count++], GDOX_MACHINE_WARNING_SIZE,
            "The %s takes no %d MB, it has %u MB", k_machine_profiles[config.model].name, ram_mb,
            config.ram_size / megabyte);

    if (m_ram_size_locked && m_powered && (config.ram_size != m_machine_config.ram_size))
    {
        snprintf(report.warnings[report.warning_count++], GDOX_MACHINE_WARNING_SIZE,
            "Restart the content to change the RAM from %u MB to %u MB", m_machine_config.ram_size / megabyte,
            config.ram_size / megabyte);
        config.ram_size = m_machine_config.ram_size;
        report.ram = m_machine_report.ram;
    }
}

// A Cirrus card's linear aperture sits at 15 MB, so with 16 MB or more the board leaves 15-16 MB to it, the "memory
// hole at 15M-16M" of 486 board setups: RAM there is lost and extended memory stops at 15 MB
bool GeardoxCore::HasMemoryHole() const
{
    return k_video_cards[m_machine_config.video_card].cirrus && (m_machine_config.ram_size >= 16 * 1024 * 1024);
}

void GeardoxCore::LoadMachineFirmware()
{
    if (!IsValidPointer(m_firmware))
        return;

    bool cirrus = k_video_cards[m_pending_machine_config.video_card].cirrus;

    if (m_firmware->IsReady() && (m_firmware->IsCirrus() == cirrus))
        return;

    m_firmware->Load(cirrus);

    if (IsValidPointer(m_i386))
        m_i386->ResetDisassembler();
}

void GeardoxCore::ApplyMachineConfig()
{
    m_machine_config = m_pending_machine_config;
    m_machine_report = m_pending_machine_report;
    const GDOX_Machine_Profile& profile = k_machine_profiles[m_machine_config.model];

    ApplyCPUSpeed();

    if (IsValidPointer(m_memory))
    {
        m_memory->SetMainRAMSize(m_machine_config.ram_size);
        m_memory->SetPhysicalAddressMask(k_machine_memory_maps[profile.map].address_mask);
    }

    if (IsValidPointer(m_i386))
    {
        const GDOX_Machine_CPU_Info& cpu = k_machine_cpus[m_machine_config.cpu];
        m_x87->Init(cpu.family >= 4 ? X87_FLAVOR_80486 : X87_FLAVOR_80387);
        m_i386->SetCPU(cpu);
        m_i386->SetFPU(m_machine_config.fpu ? m_x87 : NULL);
        m_system_control->SetFPU(m_machine_config.fpu ? m_x87 : NULL);
    }

    if (IsValidPointer(m_audio))
        m_audio->SetCard(m_machine_config.sound_card, m_machine_config.sound_irq, m_machine_config.sound_dma);

    if (IsValidPointer(m_video))
        m_video->SetCard(m_machine_config.video_card);

    if (IsValidPointer(m_i8042))
        m_i8042->SetMouse(m_machine_config.mouse == GDOX_MOUSE_PS2 ? m_ps2_mouse : NULL);

    if (IsValidPointer(m_serial_ports))
        m_serial_ports->SetMouse(m_machine_config.mouse == GDOX_MOUSE_SERIAL);

    if (IsValidPointer(m_ide))
        m_ide->SetCdRom((int)m_machine_config.cdrom - 1, k_cdrom_speed_factors[m_machine_config.cdrom_speed]);

    if (IsValidPointer(m_fdc))
    {
        m_fdc->SetInternalDrives(m_machine_config.floppy_drives);

        // Auto looks at the disks in the drives at every reset, they can change after the last resolve
        for (int i = 0; i < FDC_DRIVES; i++)
        {
            if ((m_game_settings.floppy_type[i] <= 0) && (m_machine_settings.floppy_type[i] <= 0))
                m_machine_config.floppy_type[i] = GetAutoFloppyType(i);

            m_fdc->SetDriveType(i, m_machine_config.floppy_type[i]);
        }
    }

    ApplyFirmwareDevices();
}

// SeaBIOS finds the VGA BIOS through the firmware config and prints to the debug console
void GeardoxCore::ApplyFirmwareDevices()
{
    if (!IsValidPointer(m_firmware) || !IsValidPointer(m_io))
        return;

    m_system_control->SetPowerOnA20(true);
    m_system_control->SetShadowControl(true);
    m_firmware_config->SetEnabled(true);
    m_firmware_config->SetVGABIOS(m_firmware->GetVGARom(), (u32)m_firmware->GetInfo(GDOX_FIRMWARE_VGA_BIOS).size);
    m_firmware_config->SetBootMenu(m_boot_menu, m_boot_menu ? k_boot_menu_wait_ms : 0);
    m_firmware_config->SetMemoryHole(m_machine_config.ram_size, HasMemoryHole());
    m_firmware_config->SetGameTarget(m_game_target);
    m_debug_console->SetEnabled(m_debug_console_enabled);
    m_debug_console->SetLogOutput(m_debug_console_log);
    m_io->UpdateDecodedPorts();
}

void GeardoxCore::ApplyCPUSpeed()
{
    if (IsValidPointer(m_scheduler))
        m_scheduler->SetCPUClockRate(GetEffectiveCPUClockRate());

    if (IsValidPointer(m_profiler))
        m_profiler->SetCPUClockRate(GetEffectiveCPUClockRate());
}

void GeardoxCore::ApplyBusTiming()
{
    if (!IsValidPointer(m_i386))
        return;

    const GDOX_Machine_Profile& profile = k_machine_profiles[m_machine_config.model];
    const GDOX_Machine_Bus_Timing* timing = profile.bus_timing;
    const GDOX_Machine_Bus_Waits* waits = IsValidPointer(timing) ? timing->compatible : NULL;
    const GDOX_Video_Card_Info& card = k_video_cards[m_machine_config.video_card];
    u32 cpu_clock_rate = GetEffectiveCPUClockRate();

    // A VL-Bus card's memory runs on the CPU's bus at its own waits, an ISA card keeps the model's
    if (IsValidPointer(waits) && card.local_bus)
    {
        m_card_bus_waits = *waits;
        m_card_bus_waits.vram_cycle[0] = card.local_bus_cycle[0];
        m_card_bus_waits.vram_cycle[1] = card.local_bus_cycle[1];
        waits = &m_card_bus_waits;
    }

    const GDOX_Machine_CPU_Info& cpu = k_machine_cpus[m_machine_config.cpu];

    // The board caches DRAM and its shadow copies; the VGA window, ROMs and the ISA bus stay on the bus
    m_i386->SetBoardCache(true, 1U << GDOX_BUS_DRAM);
    m_i386->SetBusTiming(timing, waits, cpu.bus_width, cpu_clock_rate, profile.cpu_clock_rate / cpu.bus_ratio);
}

// The board's DRAM covers the hole below 1 MB too: the BIOS runs from its copy at E0000h or F0000h, with the
// pristine image at the top of the address space for the reset vector
// SeaBIOS needs the upper memory and its copy writable
void GeardoxCore::InitMemoryMap()
{
    if (!IsValidPointer(m_memory) || !IsValidPointer(m_firmware) || !m_firmware->IsReady())
        return;

    u32 rom_flags = GDOX_DEBUG_REGION_READABLE | GDOX_DEBUG_REGION_EXECUTABLE | GDOX_DEBUG_REGION_ROM | GDOX_DEBUG_REGION_MAPPED;
    u32 ram_flags = GDOX_DEBUG_REGION_READABLE | GDOX_DEBUG_REGION_WRITABLE | GDOX_DEBUG_REGION_EXECUTABLE;
    const GDOX_Machine_Memory_Map& map = k_machine_memory_maps[k_machine_profiles[m_machine_config.model].map];
    const u8* system_rom = m_firmware->GetSystemRom();
    u32 rom_size = (u32)m_firmware->GetInfo(GDOX_FIRMWARE_SYSTEM_BIOS).size;
    u32 copy_size = MIN(rom_size, k_bios_copy_size);
    u32 copy_base = k_bios_copy_end - copy_size;
    u8* main_ram = m_memory->GetMainRAM();
    u8* bios_copy = main_ram + copy_base;

    memcpy(bios_copy, system_rom + rom_size - copy_size, copy_size);

    if (!m_memory->RegisterDebugRegion(GDOX_DEBUG_REGION_SYSTEM_ROM, "System BIOS", system_rom, NULL, rom_size,
        map.address_mask - rom_size + 1, rom_flags))
        Error("Unable to map the system BIOS");

    u32 video_flags = GDOX_DEBUG_REGION_READABLE | GDOX_DEBUG_REGION_WRITABLE | GDOX_DEBUG_REGION_VIDEO;

    if (!m_memory->RegisterHandlerRegion(GDOX_DEBUG_REGION_VIDEO_APERTURE, "VGA Window", k_video_max_window_size,
        map.conventional_size, video_flags | GDOX_DEBUG_REGION_EXECUTABLE | GDOX_DEBUG_REGION_MAPPED |
        GDOX_DEBUG_REGION_OVERLAY, m_video, Video::ReadWindowCallback, Video::WriteWindowCallback,
        Video::PeekWindowCallback) ||
        !m_memory->SetRegionWideHandlers(GDOX_DEBUG_REGION_VIDEO_APERTURE, Video::WriteWindow16Callback,
        Video::WriteWindow32Callback) ||
        !m_memory->SetRegionWideReadHandlers(GDOX_DEBUG_REGION_VIDEO_APERTURE, Video::ReadWindow16Callback,
        Video::ReadWindow32Callback))
        Error("Unable to map the VGA window");

    if (!m_memory->RegisterHandlerRegion(GDOX_DEBUG_REGION_VGA_PLANE_0, "VGA Plane 0", VIDEO_PLANE_SIZE, 0, video_flags,
        m_video, Video::PeekPlane0Callback, Video::PokePlane0Callback, Video::PeekPlane0Callback) ||
        !m_memory->RegisterHandlerRegion(GDOX_DEBUG_REGION_VGA_PLANE_1, "VGA Plane 1", VIDEO_PLANE_SIZE, 0, video_flags,
        m_video, Video::PeekPlane1Callback, Video::PokePlane1Callback, Video::PeekPlane1Callback) ||
        !m_memory->RegisterHandlerRegion(GDOX_DEBUG_REGION_VGA_PLANE_2, "VGA Plane 2", VIDEO_PLANE_SIZE, 0, video_flags,
        m_video, Video::PeekPlane2Callback, Video::PokePlane2Callback, Video::PeekPlane2Callback) ||
        !m_memory->RegisterHandlerRegion(GDOX_DEBUG_REGION_VGA_PLANE_3, "VGA Plane 3", VIDEO_PLANE_SIZE, 0, video_flags,
        m_video, Video::PeekPlane3Callback, Video::PokePlane3Callback, Video::PeekPlane3Callback))
        Error("Unable to register the VGA planes");

    if (!m_memory->RegisterDebugRegion(GDOX_DEBUG_REGION_MAIN_RAM, "Main RAM", main_ram, main_ram,
        m_memory->GetMainRAMSize(), 0, ram_flags | GDOX_DEBUG_REGION_MAPPED))
        Error("Unable to map main RAM");

    // Main RAM goes first: where it covers the linear aperture the board's DRAM answers, but for its hole at 15 MB
    if (m_video->IsCirrus() && (!m_memory->RegisterHandlerRegion(GDOX_DEBUG_REGION_LINEAR_VRAM, "Linear VRAM",
        m_video->GetVRAMSize(), k_video_aperture_base, video_flags | (HasMemoryHole() ? GDOX_DEBUG_REGION_OVERLAY : 0),
        m_video, Video::ReadLinearCallback,
        Video::WriteLinearCallback, Video::PeekLinearCallback) ||
        !m_memory->SetRegionWideHandlers(GDOX_DEBUG_REGION_LINEAR_VRAM, Video::WriteLinear16Callback,
        Video::WriteLinear32Callback) ||
        !m_memory->SetRegionWideReadHandlers(GDOX_DEBUG_REGION_LINEAR_VRAM, Video::ReadLinear16Callback,
        Video::ReadLinear32Callback)))
        Error("Unable to register the linear VRAM");

    if (!m_memory->RegisterDebugRegion(GDOX_DEBUG_REGION_BIOS_COPY, "BIOS Copy", bios_copy, bios_copy,
        copy_size, copy_base, ram_flags))
        Error("Unable to register the BIOS copy");

    // SeaBIOS' shadow register maps these at the end of POST: the ROMs and the F segment read-only, nothing between
    u32 shadow_flags = GDOX_DEBUG_REGION_READABLE | GDOX_DEBUG_REGION_EXECUTABLE | GDOX_DEBUG_REGION_ROM | GDOX_DEBUG_REGION_OVERLAY;

    if (!m_memory->RegisterDebugRegion(GDOX_DEBUG_REGION_SHADOW_ROM, "Shadow ROMs", main_ram + k_upper_memory_start,
        NULL, 0x1000, k_upper_memory_start, shadow_flags) ||
        !m_memory->RegisterDebugRegion(GDOX_DEBUG_REGION_UPPER_MEMORY, "Upper Memory", NULL, NULL, 0x1000, k_upper_memory_start,
        GDOX_DEBUG_REGION_OVERLAY) ||
        !m_memory->RegisterDebugRegion(GDOX_DEBUG_REGION_SHADOW_BIOS, "Shadow BIOS", main_ram + k_system_control_shadow_bios,
        NULL, 0x10000, k_system_control_shadow_bios, shadow_flags))
        Error("Unable to register the shadow regions");

    m_system_control->ApplyShadow();

    u8* cmos = m_rtc->GetCMOS();

    if (!m_memory->RegisterDebugRegion(GDOX_DEBUG_REGION_CMOS, "CMOS RAM", cmos, cmos, GDOX_CMOS_SIZE, 0,
        GDOX_DEBUG_REGION_READABLE | GDOX_DEBUG_REGION_WRITABLE))
        Error("Unable to register CMOS RAM");

    m_video->UpdateVRAMMapping();
}

// SeaBIOS has no setup screen, so the board writes the memory size, equipment and boot order at power on
// The rest of the CMOS stays as programs left it
void GeardoxCore::InitCMOS()
{
    if (!IsValidPointer(m_firmware) || !m_firmware->IsReady())
        return;

    u32 megabyte = 1024 * 1024;
    u32 ram_size = m_machine_config.ram_size;
    u32 extended_kb = MIN(ram_size > megabyte ? (ram_size - megabyte) / 1024 : 0, 63U * 1024);

    if (HasMemoryHole())
        extended_kb = 14 * 1024;
    u32 high_blocks = ram_size > 16 * megabyte ? (ram_size - 16 * megabyte) / 0x10000 : 0;
    u8 order[3] = { 1, 2, 3 };

    if (m_boot_device == GDOX_BOOT_DEVICE_HARD_DISK)
    {
        order[0] = 2;
        order[1] = 1;
    }
    else if (m_boot_device == GDOX_BOOT_DEVICE_CDROM)
    {
        order[0] = 3;
        order[1] = 1;
        order[2] = 2;
    }

    u8 drives = 0;

    for (int i = 0; i < m_machine_config.floppy_drives; i++)
        drives += m_machine_config.floppy_type[i] != GDOX_FLOPPY_NONE ? 1 : 0;

    u8 translation = 0;

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        if (i != m_ide->GetCdRomSlot())
            translation |= (u8)(m_ide->GetDisk(i)->GetTranslation() << (i * 2));
    }

    bool hard_disk_0 = m_ide->GetDisk(0)->IsAttached() && (m_ide->GetCdRomSlot() != 0);
    bool hard_disk_1 = m_ide->GetDisk(1)->IsAttached() && (m_ide->GetCdRomSlot() != 1);

    m_rtc->WriteRAM(0x10, (u8)((m_machine_config.floppy_type[0] << 4) | m_machine_config.floppy_type[1]));
    m_rtc->WriteRAM(0x12, (u8)((hard_disk_0 ? 0xF0 : 0x00) | (hard_disk_1 ? 0x0F : 0x00)));
    m_rtc->WriteRAM(0x14, (u8)((m_machine_config.fpu ? 0x02 : 0x00) | (drives > 0 ? 0x01 | ((drives - 1) << 6) : 0x00)));
    m_rtc->WriteRAM(0x15, 0x80);
    m_rtc->WriteRAM(0x16, 0x02);
    m_rtc->WriteRAM(0x17, (u8)extended_kb);
    m_rtc->WriteRAM(0x18, (u8)(extended_kb >> 8));
    m_rtc->WriteRAM(0x19, hard_disk_0 ? 47 : 0);
    m_rtc->WriteRAM(0x1A, hard_disk_1 ? 47 : 0);
    m_rtc->WriteRAM(0x30, (u8)extended_kb);
    m_rtc->WriteRAM(0x31, (u8)(extended_kb >> 8));
    m_rtc->WriteRAM(0x34, (u8)high_blocks);
    m_rtc->WriteRAM(0x35, (u8)(high_blocks >> 8));
    m_rtc->WriteRAM(0x38, (u8)(0x01 | (order[2] << 4)));
    m_rtc->WriteRAM(0x39, translation);
    m_rtc->WriteRAM(0x3D, (u8)(order[0] | (order[1] << 4)));

    u16 checksum = 0;

    for (int i = 0x10; i <= 0x2D; i++)
        checksum += m_rtc->ReadRAM((u8)i);

    m_rtc->WriteRAM(0x2E, (u8)(checksum >> 8));
    m_rtc->WriteRAM(0x2F, (u8)checksum);
}
