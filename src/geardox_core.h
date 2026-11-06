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

#ifndef GEARDOX_CORE_H
#define GEARDOX_CORE_H

#include <iostream>
#include "common/common.h"

class Audio;
class CdRomAudio;
class CdRomMedia;
class FDC;
class FloppyDisk;
class HardDiskImage;
class Firmware;
class I386;
class X87;
class Input;
class Media;
class Memory;
class IO;
class DMA;
class PIC;
class PIT;
class Scheduler;
class SystemControl;
class I8042;
class Keyboard;
class MC146818;
class FirmwareConfig;
class DebugConsole;
class Video;
class StateSerializer;
class TraceLogger;
class Profiler;
struct I386_Run_Result;
struct GDOX_DB_Entry;

enum GDOX_Debug_Step
{
    GDOX_DEBUG_STEP_NONE = 0,
    GDOX_DEBUG_STEP_INSTRUCTION,
    GDOX_DEBUG_STEP_RETURN
};

class GeardoxCore
{
public:
    struct GDOX_Debug_Run
    {
        bool step_debugger;
        bool step_over;
        bool step_start;
        bool stop_on_breakpoint;
        bool stop_on_run_to_breakpoint;
        bool skip_interrupts_on_step;
        bool stopped;
        bool breakpoint_hit;
    };

public:
    GeardoxCore();
    ~GeardoxCore();
    void Init();
    GDOX_Run_Result RunToFrame(u8* frame_buffer, s16* sample_buffer, int* sample_count, bool render = true);
    GDOX_Run_Result RunToFrame(u8* frame_buffer, s16* sample_buffer, int* sample_count, GDOX_Debug_Run* debug,
        bool render = true);

    bool PowerOn();
    void PowerOff();
    bool IsPoweredOn();
    void SetMachineSettings(const GDOX_Machine_Settings& settings);
    void LatchAutoMedia();
    const GDOX_Machine_Settings& GetMachineSettings();
    const GDOX_Machine_Config& GetMachineConfig();
    const GDOX_Machine_Config& GetPendingMachineConfig();
    const GDOX_Machine_Report& GetMachineReport();
    const GDOX_Machine_Report& GetPendingMachineReport();
    bool IsMachineConfigPending();
    bool GetFloppyNotice(GDOX_Floppy_Notice& notice);
    bool GetDatabaseController(int port, GDOX_Controller_Type& type) const;
    bool LoadBios(const char* directory_path);
    void UnloadBios();
    void SetModelFirmwareFolder(GDOX_Machine_Model model, const char* directory_path);
    void SearchFirmware();
    bool ReloadFirmware();
    const GDOX_Firmware_Source& GetFirmwareSource(GDOX_Machine_Model model);
    bool HasFirmware(GDOX_Machine_Model model);
    bool LoadMedia(const char* file_path);
#if defined(GDOX_ENABLE_PHYSICAL_CDROM)
    bool LoadPhysicalCdRom(const char* device_id);
#endif
    void EjectMedia();
    void ResetMedia();
    bool InsertFloppy(int drive, const u8* data, u32 size, bool write_protected, u32 base_crc = 0, u32 data_crc = 0, const u8* source = NULL, u32 source_size = 0);
    void EjectFloppy(int drive);
    void SwapFloppies();
    bool AttachHardDisk(int id, const char* path, bool write_protected = false, const char* working_path = NULL);
    void DetachHardDisk(int id);
    bool FlushHardDisks();
    bool IsHardDiskPending(int id) const;
    const char* GetPendingHardDiskPath(int id) const;
    bool IsPendingHardDiskWriteProtected(int id) const;
    u32 GetHardDiskStateChanges() const;
    void SetBootDevice(GDOX_Boot_Device device);
    GDOX_Boot_Device GetBootDevice() const;
    void LockRAMSize(bool locked);
    void SetBootMenu(bool show);
    void SetDebugConsole(bool enabled, bool log);

    void KeyPressed(GDOX_Keys key);
    void KeyReleased(GDOX_Keys key);
    void ReleaseAllKeys();
    void Pause(bool paused);
    bool IsPaused();

    bool SaveState(const char* path = NULL, int index = -1, bool screenshot = false);
    bool SaveState(u8* buffer, size_t& size, bool screenshot = false, bool media = false);
    bool SaveState(std::ostream& stream, size_t& size, bool screenshot, bool media);
    bool GetMaxSaveStateSize(size_t& size);
    bool LoadState(const char* path = NULL, int index = -1);
    bool LoadState(const u8* buffer, size_t size, GDOX_SaveState_Media* media = NULL);
    bool GetSaveStateHeader(int index, const char* path, GDOX_SaveState_Header* header);
    bool GetSaveStateScreenshot(int index, const char* path, GDOX_SaveState_Screenshot* screenshot);

    void GetRuntimeInfo(GDOX_Runtime_Info& runtime_info);

    Firmware* GetFirmware();
    Media* GetMedia();
    Audio* GetAudio();
    Input* GetInput();
    Memory* GetMemory();
    I386* GetI386();
    X87* GetX87();
    IO* GetIO();
    PIC* GetPIC();
    PIT* GetPIT();
    DMA* GetDMA();
    SystemControl* GetSystemControl();
    I8042* GetI8042();
    Keyboard* GetKeyboard();
    MC146818* GetRTC();
    FirmwareConfig* GetFirmwareConfig();
    DebugConsole* GetDebugConsole();
    Scheduler* GetScheduler();
    Video* GetVideo();
    CdRomMedia* GetCDROMMedia();
    CdRomAudio* GetCDROMAudio();
    FDC* GetFDC();
    FloppyDisk* GetFloppy(int drive);
    HardDiskImage* GetHardDisk(int id);
    TraceLogger* GetTraceLogger();
    Profiler* GetProfiler();

private:
    template<bool debugger>
    GDOX_Run_Result RunToFrameTemplate(u8* frame_buffer, s16* sample_buffer, int* sample_count, GDOX_Debug_Run* debug, bool render);
    void RunFrame(u64 frame_start);
    void RunDebuggerFrame(u64 frame_start, GDOX_Debug_Run* debug);
    void RunDebuggerSteps(u64 frame_start, GDOX_Debug_Run* debug);
    bool BeginDebuggerFrame(GDOX_Debug_Run* debug);
    bool BreakpointStopsDebugger(GDOX_Debug_Run* debug);
    bool RunDebuggerStep(u64 frame_start, GDOX_Debug_Run* debug);
    bool RunDeferredInterrupt(GDOX_Debug_Run* debug);
    bool StepInterrupt(GDOX_Debug_Run* debug, u32 return_linear, u16 stack_selector, u32 stack_pointer, bool resume);
    bool AcceptDebuggerHit(GDOX_Debug_Run* debug);
    void StopDebugger(GDOX_Debug_Run* debug, bool breakpoint);
    void EndFrame(u64 frame_start, s16* sample_buffer, int* sample_count);
    bool IsFrameDone(u64 frame_start) const;
    u64 GetFrameLimit(u64 frame_start) const;
    GDOX_Bus_Access_Context BeginSlice();
    bool CompleteSlice(const I386_Run_Result& result, GDOX_Bus_Access_Context& context, u32 slice, bool hold_on_hit);
    bool EnterPendingInterrupt(GDOX_Bus_Access_Context& context);
    static GDOX_Synchronize_Result SynchronizeIOCallback(void* core, GDOX_Bus_Access_Context& context, u32 start_cycles, u32 access_cycles);
    static void SynchronizeMemoryCallback(void* core, GDOX_Bus_Access_Context& context);
    static u32 PollPortCallback(void* core, u16 port, int width);
    static void DMADebugCallback(void* cpu, u32 address, u32 size, bool write);
    static void FPUErrorCallback(void* core);
    void DispatchEvents();
    void Reset();
    void ResetCPU();
    u8 GetAutoFloppyType(int drive);
    void ResolveMachineConfig();
    bool HasHardDisks();
    void ApplyHardDisks();
    bool CanAutoSelect(GDOX_Machine_Model model);
    bool LoadMachineFirmware(bool force, bool report);
    void ApplyMachineConfig();
    void ApplyBusTiming();
    void ApplyBootDevice();
    void InitMemoryMap();
    void InitCMOS();
    void ApplyFirmwareDevices();

    bool LoadState(std::istream& stream, GDOX_SaveState_Media* media = NULL);
    void Serialize(StateSerializer& serializer, GDOX_Machine_Config& config, int version);
    void SerializeMedia(StateSerializer& serializer, GDOX_SaveState_Media& media, u32 version);
    void SanitizeMachineConfig(GDOX_Machine_Config& config);
    std::string GetSaveStatePath(const char* path, int index);

private:
    Audio* m_audio;
    Firmware* m_firmware;
    Input* m_input;
    Media* m_media;
    Memory* m_memory;
    I386* m_i386;
    X87* m_x87;
    IO* m_io;
    PIC* m_pic;
    PIT* m_pit;
    DMA* m_dma;
    SystemControl* m_system_control;
    I8042* m_i8042;
    Keyboard* m_keyboard;
    MC146818* m_rtc;
    FirmwareConfig* m_firmware_config;
    DebugConsole* m_debug_console;
    Scheduler* m_scheduler;
    Video* m_video;
    CdRomMedia* m_cdrom_media;
    CdRomAudio* m_cdrom_audio;
    FDC* m_fdc;
    HardDiskImage* m_hard_disks[GDOX_MAX_HARD_DISKS];
    TraceLogger* m_trace_logger;
    Profiler* m_profiler;

    GDOX_Machine_Settings m_machine_settings;
    const GDOX_DB_Entry* m_auto_entry;
    std::string m_firmware_folder;
    std::string m_model_firmware_folders[GDOX_MACHINE_COUNT];
    GDOX_Firmware_Source m_firmware_sources[GDOX_MACHINE_COUNT];
    GDOX_Machine_Config m_machine_config;
    GDOX_Machine_Config m_pending_machine_config;
    GDOX_Machine_Report m_machine_report;
    GDOX_Machine_Report m_pending_machine_report;
    std::string m_hard_disk_paths[GDOX_MAX_HARD_DISKS];
    std::string m_hard_disk_working_paths[GDOX_MAX_HARD_DISKS];
    bool m_hard_disk_write_protected[GDOX_MAX_HARD_DISKS];
    bool m_hard_disk_pending[GDOX_MAX_HARD_DISKS];
    u32 m_hard_disk_state_changes;
    GDOX_Boot_Device m_boot_device;
    bool m_ram_size_locked;
    bool m_boot_menu;
    bool m_debug_console_enabled;
    bool m_debug_console_log;
    bool m_powered;
    bool m_paused;
    GDOX_Debug_Step m_debug_step;
    bool m_debug_step_resume;
    bool m_debug_interrupt_deferred;

    u8* m_frame_buffer;
};

static const u32 k_bios_copy_size = 0x20000;
static const u32 k_bios_copy_end = 0x100000;
static const u32 k_upper_memory_start = 0xC0000;
static const u16 k_boot_menu_wait_ms = 2500;

#include "geardox_core_inline.h"

#endif /* GEARDOX_CORE_H */
