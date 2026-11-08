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

#ifndef MCP_DEBUG_ADAPTER_H
#define MCP_DEBUG_ADAPTER_H

#include <string>
#include <vector>
#include "json.hpp"
#include "geardox.h"
#include "../debug/gui_debug_memory.h"

using json = nlohmann::json;

static const int k_mcp_mouse_motion_step = 4;

struct McpTraceFilter
{
    const char* name;
    GDOX_Trace_Type type;
    u32 mask;
};

// Filter names for set_trace_log, cpu.instructions has no event mask
static const McpTraceFilter k_mcp_trace_filters[] =
{
    { "cpu.instructions", TRACE_CPU, 0 },
    { "cpu.irqs", TRACE_CPU_INTERRUPT, TRACE_CPU_INTERRUPT_EVENT_IRQS },
    { "cpu.exceptions", TRACE_CPU_INTERRUPT, TRACE_CPU_INTERRUPT_EVENT_EXCEPTIONS },
    { "cpu.software_ints", TRACE_CPU_INTERRUPT, TRACE_CPU_INTERRUPT_EVENT_SOFTWARE },
    { "io.reads", TRACE_IO, TRACE_IO_EVENT_READS },
    { "io.writes", TRACE_IO, TRACE_IO_EVENT_WRITES },
    { "pic.requests", TRACE_PIC, TRACE_PIC_EVENT_REQUESTS },
    { "pic.mask", TRACE_PIC, TRACE_PIC_EVENT_MASK },
    { "pic.commands", TRACE_PIC, TRACE_PIC_EVENT_COMMANDS },
    { "pic.init", TRACE_PIC, TRACE_PIC_EVENT_INIT },
    { "timer.timeouts", TRACE_TIMER, TRACE_TIMER_EVENT_TIMEOUTS },
    { "timer.counters", TRACE_TIMER, TRACE_TIMER_EVENT_COUNTERS },
    { "timer.speaker", TRACE_TIMER, TRACE_TIMER_EVENT_SPEAKER },
    { "dma.registers", TRACE_DMA, TRACE_DMA_EVENT_REGISTERS },
    { "dma.requests", TRACE_DMA, TRACE_DMA_EVENT_REQUESTS },
    { "dma.ends", TRACE_DMA, TRACE_DMA_EVENT_ENDS },
    { "video.crtc", TRACE_VIDEO, TRACE_VIDEO_EVENT_CRTC },
    { "video.output", TRACE_VIDEO, TRACE_VIDEO_EVENT_OUTPUT },
    { "video.palette", TRACE_VIDEO, TRACE_VIDEO_EVENT_PALETTE },
    { "video.vsync", TRACE_VIDEO, TRACE_VIDEO_EVENT_VSYNC },
    { "video.missed_vblank", TRACE_VIDEO, TRACE_VIDEO_EVENT_MISSED_VBLANK },
    { "fm.key", TRACE_FM, TRACE_FM_EVENT_KEY },
    { "fm.frequency", TRACE_FM, TRACE_FM_EVENT_FREQUENCY },
    { "fm.operators", TRACE_FM, TRACE_FM_EVENT_OPERATORS },
    { "fm.channels", TRACE_FM, TRACE_FM_EVENT_CHANNELS },
    { "fm.global", TRACE_FM, TRACE_FM_EVENT_GLOBAL },
    { "fm.timers", TRACE_FM, TRACE_FM_EVENT_TIMERS },
    { "pcm.commands", TRACE_PCM, TRACE_PCM_EVENT_COMMANDS },
    { "pcm.transfers", TRACE_PCM, TRACE_PCM_EVENT_TRANSFERS },
    { "pcm.irqs", TRACE_PCM, TRACE_PCM_EVENT_IRQS },
    { "mixer.volume", TRACE_MIXER, TRACE_MIXER_EVENT_VOLUME },
    { "mixer.control", TRACE_MIXER, TRACE_MIXER_EVENT_CONTROL },
    { "midi.data", TRACE_MIDI, TRACE_MIDI_EVENT_DATA },
    { "midi.commands", TRACE_MIDI, TRACE_MIDI_EVENT_COMMANDS },
    { "cdrom.packets", TRACE_CDROM, TRACE_CDROM_EVENT_PACKETS },
    { "cdrom.status", TRACE_CDROM, TRACE_CDROM_EVENT_STATUS },
    { "cdrom.data", TRACE_CDROM, TRACE_CDROM_EVENT_DATA },
    { "cdrom.media", TRACE_CDROM, TRACE_CDROM_EVENT_MEDIA },
    { "cdrom.cdda", TRACE_CDROM, TRACE_CDROM_EVENT_CDDA },
    { "fdc.commands", TRACE_FDC, TRACE_FDC_EVENT_COMMANDS },
    { "fdc.results", TRACE_FDC, TRACE_FDC_EVENT_RESULTS },
    { "fdc.drives", TRACE_FDC, TRACE_FDC_EVENT_DRIVES },
    { "ide.commands", TRACE_IDE, TRACE_IDE_EVENT_COMMANDS },
    { "ide.results", TRACE_IDE, TRACE_IDE_EVENT_RESULTS },
    { "ide.resets", TRACE_IDE, TRACE_IDE_EVENT_RESETS },
    { "keyboard.keys", TRACE_KEYBOARD, TRACE_KEYBOARD_EVENT_KEYS },
    { "keyboard.reads", TRACE_KEYBOARD, TRACE_KEYBOARD_EVENT_READS },
    { "keyboard.commands", TRACE_KEYBOARD, TRACE_KEYBOARD_EVENT_COMMANDS },
    { "input.changes", TRACE_INPUT, TRACE_INPUT_EVENT_CHANGES },
    { "input.commands", TRACE_INPUT, TRACE_INPUT_EVENT_COMMANDS },
    { "input.packets", TRACE_INPUT, TRACE_INPUT_EVENT_PACKETS },
    { "input.fires", TRACE_INPUT, TRACE_INPUT_EVENT_FIRES },
    { "serial.writes", TRACE_SERIAL, TRACE_SERIAL_EVENT_WRITES },
    { "serial.data", TRACE_SERIAL, TRACE_SERIAL_EVENT_DATA },
    { "serial.control", TRACE_SERIAL, TRACE_SERIAL_EVENT_CONTROL },
    { "parallel.data", TRACE_PARALLEL, TRACE_PARALLEL_EVENT_DATA },
    { "parallel.control", TRACE_PARALLEL, TRACE_PARALLEL_EVENT_CONTROL },
    { "system.reset", TRACE_SYSTEM, TRACE_SYSTEM_EVENT_RESET },
    { "system.a20", TRACE_SYSTEM, TRACE_SYSTEM_EVENT_MEMORY },
    { "system.control", TRACE_SYSTEM, TRACE_SYSTEM_EVENT_CONTROL },
    { "system.post", TRACE_SYSTEM, TRACE_SYSTEM_EVENT_POST },
    { "system.rtc", TRACE_SYSTEM, TRACE_SYSTEM_EVENT_RTC }
};

static const int k_mcp_trace_filter_count = sizeof(k_mcp_trace_filters) / sizeof(k_mcp_trace_filters[0]);

struct McpAddress
{
    u32 linear;
    bool logical;
    u16 segment;
    u32 offset;
    int segment_register;
};

enum McpMouseMiss
{
    MCP_MOUSE_MISS_NONE = 0,
    MCP_MOUSE_MISS_UNSEEN,
    MCP_MOUSE_MISS_OVERLAP,
    MCP_MOUSE_MISS_REJECTED
};

struct McpMousePointer
{
    bool valid;
    int x;
    int y;
    int width;
    int height;
    int screen_width;
    int screen_height;
    double pixels_per_count_x;
    double pixels_per_count_y;
    u64 frame;
    u32 generation;
    bool estimated;
    std::vector<u32> pixels;
    std::vector<u8> mask;
};

class DebugAdapter
{
public:
    DebugAdapter(GeardoxCore* core);

    // Execution control
    void Pause();
    void Resume();
    void StepInto();
    void StepOver();
    bool StepOut();
    void StepFrames(int frames);
    json Reset(const std::string& boot = "");
    json GetDebugStatus();
    json RunToAddress(const McpAddress& address);
    json SetFastForwardSpeed(int speed);
    json ToggleFastForward(bool enabled);

    // Addresses
    bool ParseAddress(const std::string& text, McpAddress& address, std::string& error);

    // Breakpoints
    json SetBreakpoint(u32 address, u32 end_address, bool range, u8 type, u8 space);
    json RemoveBreakpoint(u32 address, u32 end_address, bool range, u8 type, u8 space);
    json ListBreakpoints();
    json EnableBreakpoint(u32 address, u32 end_address, bool range, u8 type, u8 space, bool enabled);
    json EnableInterruptBreakpoint(int vector, const std::string& source, bool enabled);
    json EnableIRQBreakpoint(int irq, bool enabled);
    json SetBreakpointsActive(bool active);
    json SetInterruptBreakpoint(int vector, const std::string& source);
    json ClearInterruptBreakpoint(int vector, const std::string& source);
    json ListInterruptBreakpoints();
    json SetIRQBreakpoint(int irq);
    json ClearIRQBreakpoint(int irq);
    json ListIRQBreakpoints();
    json GetBreakpointHit();

    // Memory areas
    json ListMemoryAreas();
    bool GetMemoryArea(int area, GuiDebugMemoryArea& info, std::string& error);
    json ReadMemory(int area, const std::string& offset, size_t size);
    json WriteMemory(int area, const std::string& offset, const std::vector<u8>& data);
    json TranslateAddress(const std::string& address);
    json SelectMemoryRange(int area, u32 start_address, u32 end_address);
    json GetMemorySelection(int area);
    json AddMemoryBookmark(int area, u32 address, const std::string& name);
    json RemoveMemoryBookmark(int area, u32 address);
    json ListMemoryBookmarks(int area);
    json AddMemoryWatch(int area, u32 address, const std::string& notes, int size);
    json RemoveMemoryWatch(int area, u32 address);
    json ListMemoryWatches(int area);
    json MemorySearchCapture(int area, const json& arguments);
    json MemorySearch(int area, const std::string& op, const std::string& compare_type, u64 compare_value,
        const std::string& data_type);
    json MemoryFind(int area, const std::string& value, bool text, bool case_sensitive, const json& arguments);

    // CPU
    json GetI386Status();
    json GetX87Status();
    json GetI386Descriptors(const std::string& table, int start, int count);
    json GetPageDirectory(int index);
    json WriteI386Register(const std::string& name, u32 value);

    // Disassembly and symbols
    json GetDisassembly(u32 start_address, u32 end_address, int count, int code_size, bool resolve_symbols,
        bool detailed);
    json ListCallStack();
    json AddDisassemblerBookmark(u32 address, const std::string& name);
    json RemoveDisassemblerBookmark(u32 address);
    json ListDisassemblerBookmarks();
    json ListSymbols(const std::string& filter, int start, int count);
    json AddSymbol(u32 address, const std::string& name);
    json RemoveSymbol(u32 address);
    json LoadSymbols(const std::string& file_path);
    json LookupSymbolByName(const std::string& name, bool partial);
    json LookupSymbolAtAddress(u32 address);

    // System hardware
    json GetPICStatus();
    json GetPITStatus();
    json GetDMAStatus();
    json GetFDCStatus();
    json GetIDEStatus();
    json GetSystemStatus();
    json GetRTCStatus();
    json GetKeyboardStatus();
    json GetSerialStatus();
    json GetParallelStatus();
    json GetCRTCStatus();
    json GetCRTCRegisters();
    json WriteCRTCRegister(int reg, u8 value);
    json GetVideoOutputStatus();
    json GetVGARegisters();
    json GetPalettes(const std::string& palette);
    json GetFrameBuffer(const std::string& buffer, int pitch);
    json GetTextScreen(bool attributes);
    json GetBiosLog(int max_bytes);

    // Audio hardware
    json GetSoundStatus();
    json SetAudioMute(const std::string& source, bool mute);

    // Storage
    json GetCDROMStatus();
    json ListCDROMTracks();
    json GetCDROMAudioStatus();
    json ReadCDROMSector(u32 lba, const std::string& mode);
    json ListFloppyDrives();
    json ListFloppySectors(int drive, int cylinder, int head);
    json ReadFloppySector(int drive, int cylinder, int head, int sector);
    json InsertFloppy(int drive, const std::string& file_path, const json& write_protected);
    json CreateBlankFloppy(int drive, const std::string& file_path, const std::string& type, bool formatted);
    json EjectFloppy(int drive);
    json SwapFloppies();
    json ListHardDisks();
    json AttachHardDisk(int id, const std::string& file_path, const json& write_protected, const json& create_size_mb,
        bool formatted);
    json DetachHardDisk(int id);
    json FlushHardDisks();
    json ReadHardDiskSector(int id, s64 lba);
    json SetFloppyWriteProtect(int drive, bool write_protected);

    // Trace and profiler
    json GetTraceLog(s64 start, int count);
    json SetTraceLog(bool enabled, u32 flags, const std::string& output, const std::string& memory_size,
        const std::string& disk_size, const std::string& output_path, const u32* event_filters,
        const std::string& vblank_watch_address, const std::string& vblank_watch_operation);
    json SetProfiler(const std::string& action);
    json GetProfilerData(const std::string& sort, int count, const std::string& filter);

    // Capture
    json GetScreenshot();
    json StartVideoRecording(const std::string& file_path, int scale, const std::string& aspect_ratio,
        const std::string& quality);
    json StopVideoRecording();

    // Media and state management
    json GetMediaInfo();
    json ListRecentMedia();
    json EjectCdrom();
    json CloseCdromTray();
    json ListCdromPlaylist();
    json StartInsertPlaylistDisc(int index);
    json LoadBios(const std::string& directory_path);
    json StartLoadMedia(const std::string& file_path);
    bool IsMediaLoading() const;
    json FinishLoadMedia(const std::string& file_path);
    json ListSaveStateSlots();
    json SelectSaveStateSlot(int slot);
    json SaveState();
    json LoadState();
    json SaveStateFile(const std::string& file_path);
    json LoadStateFile(const std::string& file_path);

    // Input
    json JoystickSet(const json& arguments);
    void ReleaseJoystick(u8 buttons, u8 hat);
    json JoystickPressed(u8 buttons, u8 hat) const;
    bool HasMouse() const;
    bool ApplyMouseMotion(int delta_x, int delta_y);
    bool IsMouseMotionPending() const;
    void SetMouseButton(u8 mask, bool pressed);
    bool FindMousePointer(const u32* before, const u32* out, const u32* back, int width, int height, int counts_x,
        int counts_y, McpMouseMiss& miss, std::string& reason);
    const McpMousePointer& GetMousePointer() const;
    bool IsMousePointerKnown() const;
    void MoveMousePointerEstimate(int counts_x, int counts_y);
    void ForgetMousePointer();
    bool IsMouseButtonHeld() const;
    json MouseGetCursor();
    json KeyboardKey(const std::string& key, const std::string& action);
    bool GetKeyCode(const std::string& name, GDOX_Keys& key) const;
    json GetInputState();
    void ClearInjectedInput();

    // Rewind
    json GetRewindStatus();
    json RewindSeek(int snapshot);

private:
    void ApplyJoystickState();
    json GetMouseState();
    json GetJoystickState();
    std::string Hex(u32 value, int digits) const;
    json VideoRegisterArray(const u8* values, int count) const;
    std::string Hex32(u32 value) const;
    std::string FormatAreaOffset(const GuiDebugMemoryArea& area, u32 offset) const;
    bool ParseAreaOffset(const GuiDebugMemoryArea& area, const std::string& text, u32& offset, std::string& error);
    bool GetAreaRange(const GuiDebugMemoryArea& area, const json& arguments, u32& start, u32& size,
        std::string& error);
    const char* GetSymbolAt(u32 linear);
    std::string FormatLogical(u16 selector, u32 offset, u32 linear);
    void AddMediaStateChanges(json& result);

private:
    GeardoxCore* m_core;
    u8 m_mouse_buttons;
    GDOX_Joystick_State m_joystick;
    bool m_joystick_axes;
    McpMousePointer m_mouse_pointer;
};

#endif /* MCP_DEBUG_ADAPTER_H */
