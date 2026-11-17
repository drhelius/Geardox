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
class IdeCdRom;
class IdeController;
class FloppyDisk;
class HardDiskImage;
class Firmware;
class I386;
class X87;
class Input;
class GamepadKeys;
class KeyMap;
class KeyMapper;
class Media;
class Memory;
class IO;
class DMA;
class PIC;
class PIT;
class Scheduler;
class SystemControl;
class PciBus;
class I82437fx;
class Piix;
class I8042;
class Keyboard;
class PS2Mouse;
class SerialPorts;
class ParallelPort;
class GamePort;
class MC146818;
class FirmwareConfig;
class DebugConsole;
class DevPort;
class Video;
class StateSerializer;
class TraceLogger;
class Profiler;
struct I386_Run_Result;

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
    void SetGameSettings(const GDOX_Machine_Settings& settings, const char* name);
    void ClearGameSettings();
    const GDOX_Machine_Settings& GetMachineSettings();
    const GDOX_Machine_Settings& GetGameSettings();
    const GDOX_Machine_Config& GetMachineConfig();
    const GDOX_Machine_Config& GetPendingMachineConfig();
    const GDOX_Machine_Report& GetMachineReport();
    const GDOX_Machine_Report& GetPendingMachineReport();
    bool IsMachineConfigPending();
    void SetTurbo(bool turbo);
    bool IsTurbo() const;
    u32 GetEffectiveCPUClockRate() const;
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
    void SetGameTarget(int target);
    bool IsPowerOffRequested() const;
    void AcknowledgePowerOff();
    void SetDebugConsole(bool enabled, bool log);
    void SetDevelopment(bool console, bool control, bool magic_breakpoint);
    void SetStopAtEntry(bool stop);
    void SetRunCommand(const char* command);
    void SetGameTiming(int mask);
    void DebugWritePort(u16 port, u32 value, int size);

    bool KeyPressed(GDOX_Keys key);
    void KeyReleased(GDOX_Keys key);
    void ReleaseAllKeys();
    void HostKeyPressed(u16 usage);
    void HostKeyReleased(u16 usage);
    void ReleaseAllHostKeys();
    void DiscardDeferredHostKeys();
    void EnableHostHotkeys(bool enable);
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
    KeyMap* GetKeyMap();
    KeyMapper* GetKeyMapper();
    GamepadKeys* GetGamepadKeys();
    Memory* GetMemory();
    I386* GetI386();
    X87* GetX87();
    IO* GetIO();
    PIC* GetPIC();
    PIT* GetPIT();
    DMA* GetDMA();
    SystemControl* GetSystemControl();
    PciBus* GetPciBus();
    I8042* GetI8042();
    Keyboard* GetKeyboard();
    PS2Mouse* GetPS2Mouse();
    SerialPorts* GetSerialPorts();
    ParallelPort* GetParallelPort();
    GamePort* GetGamePort();
    MC146818* GetRTC();
    FirmwareConfig* GetFirmwareConfig();
    DebugConsole* GetDebugConsole();
    DevPort* GetDevPort();
    Scheduler* GetScheduler();
    Video* GetVideo();
    CdRomMedia* GetCDROMMedia();
    CdRomAudio* GetCDROMAudio();
    IdeCdRom* GetCDROM();
    FDC* GetFDC();
    IdeController* GetIDE();
    FloppyDisk* GetFloppy(int drive);
    HardDiskImage* GetHardDisk(int id);
    bool HasHardDisks();
    TraceLogger* GetTraceLogger();
    Profiler* GetProfiler();

private:
    template<bool debugger>
    GDOX_Run_Result RunToFrameTemplate(u8* frame_buffer, s16* sample_buffer, int* sample_count, GDOX_Debug_Run* debug, bool render);
    template<bool debugger>
    bool RunFrame(u64 frame_start);
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
    static void MemoryHoleCallback(void* core, bool hole);
    static void KeyMapCallback(void* core, GDOX_Keys key, bool pressed);
    static void GamepadKeyCallback(void* core, GDOX_Keys key, bool pressed);
    static void DevPortEventCallback(void* core, u8 type);
    void UpdateHeldKey(GDOX_Keys key);
    void DispatchEvents();
    void Reset();
    void ResetCPU();
    u8 GetAutoFloppyType(int drive);
    void ResolveMachineConfig();
    void ApplyHardDisks();
    bool HasMemoryHole() const;
    u32 GetMemoryHoleStart() const;
    bool HasPCIBus() const;
    void LoadMachineFirmware();
    void ApplyMachineConfig();
    void ApplyCPUSpeed();
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
    KeyMap* m_key_map;
    KeyMapper* m_key_mapper;
    GamepadKeys* m_gamepad_keys;
    Media* m_media;
    Memory* m_memory;
    I386* m_i386;
    X87* m_x87;
    IO* m_io;
    PIC* m_pic;
    PIT* m_pit;
    DMA* m_dma;
    SystemControl* m_system_control;
    PciBus* m_pci_bus;
    I82437fx* m_i82437fx;
    Piix* m_piix;
    I8042* m_i8042;
    Keyboard* m_keyboard;
    PS2Mouse* m_ps2_mouse;
    SerialPorts* m_serial_ports;
    ParallelPort* m_parallel_port;
    GamePort* m_game_port;
    MC146818* m_rtc;
    FirmwareConfig* m_firmware_config;
    DebugConsole* m_debug_console;
    DevPort* m_dev_port;
    Scheduler* m_scheduler;
    Video* m_video;
    CdRomMedia* m_cdrom_media;
    CdRomAudio* m_cdrom_audio;
    FDC* m_fdc;
    IdeController* m_ide;
    TraceLogger* m_trace_logger;
    Profiler* m_profiler;

    GDOX_Machine_Settings m_machine_settings;
    GDOX_Machine_Settings m_game_settings;
    std::string m_game_name;
    bool m_game;
    GDOX_Machine_Bus_Waits m_card_bus_waits;
    GDOX_Machine_Bus_Timing m_card_bus_timing;
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
    int m_game_target;
    bool m_debug_console_enabled;
    bool m_debug_console_log;
    bool m_dev_console;
    bool m_dev_control;
    bool m_dev_magic_breakpoint;
    bool m_dev_stop_at_entry;
    bool m_debug_recheck;
    int m_game_timing;
    bool m_powered;
    bool m_paused;
    bool m_host_hotkeys;
    bool m_mapped_keys[GDOX_KEY_COUNT];
    bool m_gamepad_keys_down[GDOX_KEY_COUNT];
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
