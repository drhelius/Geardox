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

#include <algorithm>
#include <ctype.h>
#include <functional>
#include <math.h>
#include <iomanip>
#include <sstream>
#include <stdlib.h>
#include "mcp_debug_adapter.h"
#include "common/log.h"
#include "drive/floppy_disk.h"
#include "drive/hard_disk_image.h"
#include "system/dma.h"
#include "system/pic.h"
#include "system/pit.h"
#include "system/system_control.h"
#include "input/i8042.h"
#include "input/game_port.h"
#include "input/input.h"
#include "input/keyboard.h"
#include "input/ps2_mouse.h"
#include "input/serial_mouse.h"
#include "system/parallel_port.h"
#include "system/serial_ports.h"
#include "system/uart.h"
#include "system/debug_console.h"
#include "system/firmware_config.h"
#include "system/mc146818.h"
#include "system/scheduler.h"
#include "system/io.h"
#include "audio/audio.h"
#include "video/video.h"
#include "video/mc6845.h"
#include "cdrom/cdrom_audio.h"
#include "cdrom/cdrom_media.h"
#include "drive/fdc.h"
#include "drive/ide_cdrom.h"
#include "drive/ide_controller.h"
#include "system/machine_profiles.h"
#include "drive/floppy_image.h"
#include "../config.h"
#include "../emu.h"
#include "../gui.h"
#include "../gui_actions.h"
#include "../gui_floppy.h"
#include "../emu_hard_disk.h"
#include "../emu_game.h"
#include "../emu_dev.h"
#include "../emu_midi.h"
#include "../midi_queue.h"
#include "../gui_game.h"
#include "../rewind.h"
#include "../debug/gui_debug_cdrom.h"
#include "../debug/gui_debug_constants.h"
#include "../debug/gui_debug_floppy.h"
#include "../debug/gui_debug_disassembler.h"
#include "../debug/gui_debug_i386_tables.h"
#include "../debug/gui_debug_x87.h"
#include "../debug/gui_debug_profiler.h"
#include "../debug/gui_debug_rewind.h"
#include "../debug/gui_debug_trace_logger.h"
#include "../debug/trace_logger_formatter.h"
#include "../emu_floppy.h"
#include "../utils.h"
#include "../video_recorder.h"

struct McpFlagName
{
    const char* name;
    u32 mask;
};

static const McpFlagName k_mcp_eflags[] =
{
    { "cf", I386_FLAG_CF }, { "pf", I386_FLAG_PF }, { "af", I386_FLAG_AF }, { "zf", I386_FLAG_ZF },
    { "sf", I386_FLAG_SF }, { "tf", I386_FLAG_TF }, { "if", I386_FLAG_IF }, { "df", I386_FLAG_DF },
    { "of", I386_FLAG_OF }, { "nt", I386_FLAG_NT }, { "rf", I386_FLAG_RF }, { "vm", I386_FLAG_VM }
};

static const McpFlagName k_mcp_cr0_flags[] =
{
    { "pe", 0x00000001 }, { "mp", 0x00000002 }, { "em", 0x00000004 }, { "ts", 0x00000008 }, { "et", 0x00000010 },
    { "pg", 0x80000000 }
};

static const McpFlagName k_mcp_cr0_486_flags[] =
{
    { "ne", 0x00000020 }, { "wp", 0x00010000 }, { "am", 0x00040000 }, { "nw", 0x20000000 }, { "cd", 0x40000000 }
};

static const char* k_mcp_segment_names[I386_SEGMENT_COUNT] = { "es", "cs", "ss", "ds", "fs", "gs" };
static const int k_mcp_segment_order[I386_SEGMENT_COUNT] =
{
    I386_SEGMENT_CS, I386_SEGMENT_DS, I386_SEGMENT_ES, I386_SEGMENT_SS, I386_SEGMENT_FS, I386_SEGMENT_GS
};

static const char* k_mcp_area_flags[] =
{
    "readable", "writable", "executable", "rom", "mapped", "mmio", "video", "audio", "overlay"
};

static const int k_mcp_max_read_size = 0x10000;
static const int k_mcp_max_disassembly_lines = 1000;
static const int k_mcp_max_symbols = 1000;

static bool parse_hex_text(const std::string& text, u32& value)
{
    std::string trimmed = text;
    trimmed.erase(0, trimmed.find_first_not_of(" \t"));
    trimmed.erase(trimmed.find_last_not_of(" \t") + 1);
    return parse_hex_with_prefix(trimmed, &value);
}

static std::string to_lower(const std::string& text)
{
    std::string result = text;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

static std::string to_upper(const std::string& text)
{
    std::string result = text;
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    return result;
}

static std::string hex_text(u32 value, int digits)
{
    char text[16];
    snprintf(text, sizeof(text), "%0*X", digits, value);
    return text;
}

static const char* execution_mode_name(I386_Execution_Mode mode)
{
    switch (mode)
    {
        case I386_MODE_PROTECTED: return "protected";
        case I386_MODE_VM86: return "vm86";
        default: return "real";
    }
}

static bool is_prefix_word(const std::string& word)
{
    return word == "LOCK" || word == "REP" || word == "REPE" || word == "REPNE" || word == "ADDR16" ||
        word == "ADDR32" || word == "ES" || word == "CS" || word == "SS" || word == "DS" || word == "FS" ||
        word == "GS";
}

static void split_instruction(const char* text, std::string& mnemonic, std::string& operands)
{
    std::istringstream stream(text);
    std::string word;
    mnemonic.clear();
    operands.clear();

    while (stream >> word)
    {
        if (!mnemonic.empty())
            mnemonic += " ";

        mnemonic += word;

        if (!is_prefix_word(word))
            break;
    }

    std::getline(stream, operands);
    operands.erase(0, operands.find_first_not_of(" "));
}

static int get_opcode_index(const I386_Disassembler_Record* record)
{
    int index = 0;

    while (index < record->size)
    {
        u8 byte = record->opcodes[index];

        if (byte != 0x26 && byte != 0x2E && byte != 0x36 && byte != 0x3E && byte != 0x64 && byte != 0x65 &&
            byte != 0x66 && byte != 0x67 && byte != 0xF0 && byte != 0xF2 && byte != 0xF3)
            break;

        index++;
    }

    return index;
}

static bool load_segment(I386_State& state, int index, u32 value, std::string& error)
{
    char text[GDOX_DEBUG_MEMORY_REASON_SIZE];

    if (!gui_debug_i386_load_segment(state, index, value, state.segments[index], text, sizeof(text)))
    {
        error = text;
        return false;
    }

    return true;
}

DebugAdapter::DebugAdapter(GeardoxCore* core)
{
    m_core = core;
    m_mouse_buttons = 0;
    memset(&m_joystick, 0, sizeof(m_joystick));
    m_joystick_axes = false;
    m_joystick_directions = 0;
    memset(m_keys, 0, sizeof(m_keys));
    m_mouse_pointer.valid = false;
    m_mouse_pointer.generation = 0;
    m_mouse_pointer.estimated = false;
}

void DebugAdapter::Pause()
{
    config_debug.debug = true;
    emu_debug_break();
}

void DebugAdapter::Resume()
{
    config_debug.debug = true;
    emu_debug_continue();
}

void DebugAdapter::StepInto()
{
    config_debug.debug = true;
    emu_debug_step_into();
}

void DebugAdapter::StepOver()
{
    config_debug.debug = true;
    emu_debug_step_over();
}

bool DebugAdapter::StepOut()
{
    config_debug.debug = true;
    return emu_debug_step_out();
}

void DebugAdapter::StepFrames(int frames)
{
    config_debug.debug = true;
    emu_debug_step_frames(frames);
}

json DebugAdapter::Reset(const std::string& boot)
{
    static const char* const devices[GDOX_BOOT_DEVICE_COUNT] = { "", "floppy1", "floppy2", "hard_disk", "cdrom" };
    int device = GDOX_BOOT_DEVICE_AUTO;

    for (int i = 1; boot.size() && (i < GDOX_BOOT_DEVICE_COUNT); i++)
    {
        if (boot == devices[i])
            device = i;
    }

    if (!boot.empty() && (device == GDOX_BOOT_DEVICE_AUTO))
        return {{"error", "Unknown boot device"}};

    if ((device == GDOX_BOOT_DEVICE_CDROM) && (m_core->GetPendingMachineConfig().cdrom == GDOX_CDROM_NONE))
        return {{"error", "No CD-ROM drive is fitted"}};

    m_core->SetBootDevice((GDOX_Boot_Device)device);
    emu_reset();
    ClearInjectedInput();
    return {{"success", true}};
}

json DebugAdapter::GetDebugStatus()
{
    json result;

    if (!m_core)
    {
        result["error"] = "Core not initialized";
        return result;
    }

    I386* cpu = m_core->GetI386();
    I386_State* state = cpu->GetState();
    const I386_Segment& cs = state->segments[I386_SEGMENT_CS];
    u32 breakpoint_address = 0;
    bool at_breakpoint = cpu->GetBreakpointHitAddress(breakpoint_address);
    bool code32 = (cs.attributes & I386_SEGMENT_DEFAULT_32) != 0;

    result["paused"] = emu_is_debug_idle() || emu_is_paused();
    result["at_breakpoint"] = at_breakpoint;
    result["pc"] = Hex32(cpu->GetCurrentLinearPC());
    result["logical_pc"] = Hex(cs.selector, 4) + ":" + Hex(state->eip, code32 ? 8 : 4);
    result["mode"] = execution_mode_name(state->execution_mode);
    result["halted"] = state->halted;

    if (at_breakpoint)
        result["breakpoint"] = GetBreakpointHit();
    else if (cpu->RunToBreakpointHit())
        result["breakpoint"] = {{"kind", "run_to"}, {"space", "linear"}, {"address", Hex32(cpu->GetCurrentLinearPC())}};

    result["media_loading"] = emu_is_media_loading();
    result["media_ready"] = m_core->GetMedia()->IsReady();
    result["powered_on"] = m_core->IsPoweredOn();
    result["frame"] = emu_frame_counter;
    return result;
}

json DebugAdapter::RunToAddress(const McpAddress& address)
{
    json result;

    if (emu_is_empty())
    {
        result["error"] = "Emulator is powered off";
        return result;
    }

    config_debug.debug = true;
    gui_debug_runto_address(address.linear);

    result["success"] = true;
    result["address"] = Hex32(address.linear);
    return result;
}

json DebugAdapter::SetFastForwardSpeed(int speed)
{
    json result;

    if (speed < 0 || speed > 4)
    {
        result["error"] = "Invalid speed (must be 0-4: 0=1.5x, 1=2x, 2=2.5x, 3=3x, 4=Unlimited)";
        Log("[MCP] SetFastForwardSpeed failed: Invalid speed %d", speed);
        return result;
    }

    config_emulator.ffwd_speed = speed;

    result["success"] = true;
    result["speed"] = speed;

    const char* speed_names[] = {"1.5x", "2x", "2.5x", "3x", "Unlimited"};
    result["speed_name"] = speed_names[speed];

    return result;
}

json DebugAdapter::ToggleFastForward(bool enabled)
{
    json result;

    config_emulator.ffwd = enabled;
    gui_action_ffwd();

    result["success"] = true;
    result["enabled"] = enabled;
    result["speed"] = config_emulator.ffwd_speed;

    return result;
}

bool DebugAdapter::ParseAddress(const std::string& text, McpAddress& address, std::string& error)
{
    memset(&address, 0, sizeof(address));
    address.segment_register = -1;

    std::string input = text;
    input.erase(0, input.find_first_not_of(" \t"));
    input.erase(input.find_last_not_of(" \t") + 1);
    size_t colon = input.find(':');

    if (colon == std::string::npos)
    {
        if (!parse_hex_text(input, address.linear))
        {
            error = "Invalid address '" + text + "'. Use a linear hex address, SR:offset or SSSS:offset";
            return false;
        }

        return true;
    }

    std::string segment = to_upper(input.substr(0, colon));
    u32 offset = 0;

    if (!parse_hex_text(input.substr(colon + 1), offset))
    {
        error = "Invalid offset in '" + text + "'";
        return false;
    }

    I386_State* state = m_core->GetI386()->GetState();
    address.logical = true;
    address.offset = offset;

    for (int i = 0; i < I386_SEGMENT_COUNT; i++)
    {
        if (segment != to_upper(k_mcp_segment_names[i]))
            continue;

        address.segment_register = i;
        address.segment = state->segments[i].selector;
        address.linear = state->segments[i].base + offset;
        return true;
    }

    u32 selector = 0;

    if (!parse_hex_text(segment, selector) || selector > 0xFFFF)
    {
        error = "Invalid segment in '" + text + "'";
        return false;
    }

    u32 base = 0;
    u32 limit = 0;
    char reason[GDOX_DEBUG_MEMORY_REASON_SIZE];

    if (!gui_debug_i386_selector_base((u16)selector, base, limit, reason, sizeof(reason)))
    {
        error = reason;
        return false;
    }

    UNUSED(limit);
    address.segment = (u16)selector;
    address.linear = base + offset;
    return true;
}

static const char* const k_mcp_breakpoint_spaces[I386_BREAKPOINT_SPACE_COUNT] = { "linear", "physical", "io" };
static const char* const k_mcp_machine_sources[] = { "user", "game", "model", "media" };
static const char* const k_mcp_interrupt_sources[I386_INTERRUPT_SOURCE_COUNT] =
{
    "any", "exception", "hardware", "software"
};

static const char* breakpoint_type_name(u8 type)
{
    if (type == I386_BREAKPOINT_EXECUTE)
        return "execute";

    if (type == (I386_BREAKPOINT_READ | I386_BREAKPOINT_WRITE))
        return "access";

    return type == I386_BREAKPOINT_WRITE ? "write" : "read";
}

static std::string breakpoint_address_text(u32 address, u8 space)
{
    return space == I386_BREAKPOINT_IO ? hex_text(address, 4) : hex_text(address, 8);
}

static int parse_interrupt_source(const std::string& source);

json DebugAdapter::SetBreakpoint(u32 address, u32 end_address, bool range, u8 type, u8 space)
{
    if (!m_core->GetI386()->AddBreakpoint(address, range ? end_address : address, type, space))
        return {{"error", "Invalid breakpoint: execute breakpoints are linear or physical and I/O ports end at FFFF"}};

    json result = {
        {"success", true},
        {"type", breakpoint_type_name(type)},
        {"space", k_mcp_breakpoint_spaces[space]},
        {"address", breakpoint_address_text(address, space)}
    };

    if (range)
        result["end_address"] = breakpoint_address_text(end_address, space);

    return result;
}

json DebugAdapter::RemoveBreakpoint(u32 address, u32 end_address, bool range, u8 type, u8 space)
{
    bool removed = m_core->GetI386()->RemoveBreakpoint(address, range ? end_address : address, type, space);
    json result = {
        {"success", true},
        {"removed", removed},
        {"type", breakpoint_type_name(type)},
        {"space", k_mcp_breakpoint_spaces[space]},
        {"address", breakpoint_address_text(address, space)}
    };

    if (range)
        result["end_address"] = breakpoint_address_text(end_address, space);

    return result;
}

json DebugAdapter::ListBreakpoints()
{
    json breakpoints = json::array();
    std::vector<I386_Breakpoint>* items = m_core->GetI386()->GetBreakpoints();

    for (size_t i = 0; i < items->size(); i++)
    {
        const I386_Breakpoint& item = (*items)[i];
        u8 space = item.space % I386_BREAKPOINT_SPACE_COUNT;
        json breakpoint = {
            {"enabled", item.enabled},
            {"type", breakpoint_type_name(item.type)},
            {"space", k_mcp_breakpoint_spaces[space]},
            {"address", breakpoint_address_text(item.address1, space)},
            {"range", item.range}
        };

        if (item.range)
            breakpoint["end_address"] = breakpoint_address_text(item.address2, space);
        else if (space == I386_BREAKPOINT_IO && IsValidPointer(gui_debug_port_label((u16)item.address1)))
            breakpoint["port_name"] = gui_debug_port_label((u16)item.address1);
        else if (space == I386_BREAKPOINT_LINEAR && IsValidPointer(GetSymbolAt(item.address1)))
            breakpoint["symbol"] = GetSymbolAt(item.address1);

        breakpoints.push_back(breakpoint);
    }

    return {{"breakpoints", breakpoints}, {"count", breakpoints.size()}, {"all_disabled", emu_debug_disable_breakpoints}};
}

json DebugAdapter::EnableBreakpoint(u32 address, u32 end_address, bool range, u8 type, u8 space, bool enabled)
{
    std::vector<I386_Breakpoint>* items = m_core->GetI386()->GetBreakpoints();
    u32 end = range ? end_address : address;
    bool found = false;

    for (size_t i = 0; i < items->size(); i++)
    {
        I386_Breakpoint& item = (*items)[i];

        if (item.address1 == address && item.address2 == end && item.type == type && item.space == space)
        {
            item.enabled = enabled;
            found = true;
        }
    }

    json result = {
        {"success", true},
        {"found", found},
        {"enabled", enabled},
        {"type", breakpoint_type_name(type)},
        {"space", k_mcp_breakpoint_spaces[space]},
        {"address", breakpoint_address_text(address, space)}
    };

    if (range)
        result["end_address"] = breakpoint_address_text(end_address, space);

    return result;
}

json DebugAdapter::EnableInterruptBreakpoint(int vector, const std::string& source, bool enabled)
{
    int index = parse_interrupt_source(source);

    if (vector < 0 || vector > 0xFF || index < 0)
        return {{"error", "vector must be 0-255 and source any, exception, hardware or software"}};

    std::vector<I386_Interrupt_Breakpoint>* items = m_core->GetI386()->GetInterruptBreakpoints();
    bool found = false;

    for (size_t i = 0; i < items->size(); i++)
    {
        if ((*items)[i].vector == vector && (*items)[i].source == index)
        {
            (*items)[i].enabled = enabled;
            found = true;
        }
    }

    return {{"success", true}, {"found", found}, {"enabled", enabled}, {"vector", hex_text(vector, 2)},
        {"source", source}};
}

json DebugAdapter::EnableIRQBreakpoint(int irq, bool enabled)
{
    if (irq < 0 || irq > 15)
        return {{"error", "irq must be 0-15"}};

    bool found = m_core->GetI386()->IsIRQBreakpoint(irq);
    m_core->GetI386()->EnableIRQBreakpoint(irq, enabled);
    return {{"success", true}, {"found", found}, {"enabled", enabled}, {"irq", irq}};
}

// Like Disable All in the debugger, every breakpoint keeps its own enabled state
json DebugAdapter::SetBreakpointsActive(bool active)
{
    emu_debug_disable_breakpoints = !active;
    return {{"success", true}, {"active", active}};
}

static int parse_interrupt_source(const std::string& source)
{
    for (int i = 0; i < I386_INTERRUPT_SOURCE_COUNT; i++)
    {
        if (source == k_mcp_interrupt_sources[i])
            return i;
    }

    return -1;
}

json DebugAdapter::SetInterruptBreakpoint(int vector, const std::string& source)
{
    int index = parse_interrupt_source(source);

    if (vector < 0 || vector > 0xFF || index < 0)
        return {{"error", "vector must be 0-255 and source any, exception, hardware or software"}};

    m_core->GetI386()->AddInterruptBreakpoint((u8)vector, (u8)index);
    char name[16];
    char description[64];
    gui_debug_i386_vector_name((u8)vector, name, sizeof(name), description, sizeof(description), true, index);

    return {{"success", true}, {"vector", hex_text(vector, 2)}, {"vector_name", name},
        {"vector_description", description}, {"source", source}};
}

json DebugAdapter::ClearInterruptBreakpoint(int vector, const std::string& source)
{
    int index = parse_interrupt_source(source);

    if (vector < 0 || vector > 0xFF || index < 0)
        return {{"error", "vector must be 0-255 and source any, exception, hardware or software"}};

    bool removed = m_core->GetI386()->RemoveInterruptBreakpoint((u8)vector, (u8)index);
    return {{"success", true}, {"removed", removed}, {"vector", hex_text(vector, 2)}, {"source", source}};
}

json DebugAdapter::ListInterruptBreakpoints()
{
    json list = json::array();
    std::vector<I386_Interrupt_Breakpoint>* items = m_core->GetI386()->GetInterruptBreakpoints();

    for (size_t i = 0; i < items->size(); i++)
    {
        const I386_Interrupt_Breakpoint& item = (*items)[i];
        char name[16];
        char description[64];
        gui_debug_i386_vector_name(item.vector, name, sizeof(name), description, sizeof(description), true, item.source);
        list.push_back({{"enabled", item.enabled}, {"vector", hex_text(item.vector, 2)}, {"vector_name", name},
            {"vector_description", description},
            {"source", k_mcp_interrupt_sources[item.source % I386_INTERRUPT_SOURCE_COUNT]}});
    }

    return {{"interrupt_breakpoints", list}, {"count", list.size()}};
}

json DebugAdapter::SetIRQBreakpoint(int irq)
{
    if (irq < 0 || irq > 15)
        return {{"error", "irq must be 0-15"}};

    m_core->GetI386()->SetIRQBreakpoint(irq, true);
    return {{"success", true}, {"irq", irq}, {"irq_name", k_debug_irq_sources[irq]}};
}

json DebugAdapter::ClearIRQBreakpoint(int irq)
{
    if (irq < 0 || irq > 15)
        return {{"error", "irq must be 0-15"}};

    bool removed = m_core->GetI386()->IsIRQBreakpoint(irq);
    m_core->GetI386()->SetIRQBreakpoint(irq, false);
    return {{"success", true}, {"removed", removed}, {"irq", irq}};
}

json DebugAdapter::ListIRQBreakpoints()
{
    json list = json::array();

    for (int i = 0; i < 16; i++)
    {
        if (m_core->GetI386()->IsIRQBreakpoint(i))
            list.push_back({{"irq", i}, {"irq_name", k_debug_irq_sources[i]},
                {"enabled", m_core->GetI386()->IsIRQBreakpointEnabled(i)}});
    }

    return {{"irq_breakpoints", list}, {"count", list.size()}};
}

json DebugAdapter::GetBreakpointHit()
{
    I386_Breakpoint_Hit hit;

    if (!m_core->GetI386()->GetBreakpointHit(hit))
        return json();

    if (hit.request != I386_HIT_REQUEST_NONE)
        return {{"kind", hit.request == I386_HIT_REQUEST_MAGIC ? "magic" : "program"}, {"space", "linear"},
            {"address", Hex32(hit.address)}};

    if (hit.interrupt)
    {
        char name[16];
        char description[64];
        gui_debug_i386_vector_name(hit.vector, name, sizeof(name), description, sizeof(description), true, hit.source);
        json result = {{"kind", "interrupt"}, {"vector", hex_text(hit.vector, 2)}, {"vector_name", name},
            {"vector_description", description},
            {"source", k_mcp_interrupt_sources[hit.source % I386_INTERRUPT_SOURCE_COUNT]}};

        if (hit.line < 16)
        {
            result["irq"] = hit.line;
            result["irq_name"] = k_debug_irq_sources[hit.line];
        }

        const char* function = hit.source == I386_INTERRUPT_SOFTWARE ?
            gui_debug_i386_interrupt_function(hit.vector, hit.ax) : NULL;

        if (IsValidPointer(function))
            result["function"] = function;

        return result;
    }

    u8 space = hit.space % I386_BREAKPOINT_SPACE_COUNT;
    json result = {
        {"kind", breakpoint_type_name(hit.type)},
        {"space", k_mcp_breakpoint_spaces[space]},
        {"address", breakpoint_address_text(hit.address, space)}
    };

    if (hit.type != I386_BREAKPOINT_EXECUTE)
        result["size"] = hit.size;

    if (hit.dma)
        result["dma"] = true;

    if (space == I386_BREAKPOINT_IO && IsValidPointer(gui_debug_port_label((u16)hit.address)))
        result["port_name"] = gui_debug_port_label((u16)hit.address);

    return result;
}

json DebugAdapter::ListMemoryAreas()
{
    json areas = json::array();
    int count = gui_debug_memory_get_area_count();

    for (int i = 0; i < count; i++)
    {
        GuiDebugMemoryArea area;

        if (!gui_debug_memory_get_area_at(i, area) || area.size == 0)
            continue;

        json flags = json::array();

        for (int f = 0; f < (int)(sizeof(k_mcp_area_flags) / sizeof(k_mcp_area_flags[0])); f++)
        {
            if (IS_SET_BIT(area.flags, f))
                flags.push_back(k_mcp_area_flags[f]);
        }

        json item = {
            {"id", area.id},
            {"name", area.name},
            {"size", area.size},
            {"unit_size", 1},
            {"flags", flags}
        };

        if ((area.flags & (GDOX_DEBUG_REGION_MAPPED | GDOX_DEBUG_REGION_OVERLAY)) != 0)
            item["physical_base"] = Hex32(area.physical_base);

        if (area.source.space == GDOX_DEBUG_MEMORY_REGION)
        {
            const char* group = gui_debug_memory_region_group(area.source.region);
            const char* description = gui_debug_memory_region_description(area.source.region);

            if (IsValidPointer(group))
                item["group"] = group;

            if (IsValidPointer(description))
                item["description"] = description;
        }
        else
            item["group"] = "address_space";

        areas.push_back(item);
    }

    return {{"areas", areas}, {"count", areas.size()}};
}

bool DebugAdapter::GetMemoryArea(int area, GuiDebugMemoryArea& info, std::string& error)
{
    if (!gui_debug_memory_get_area(area, info))
    {
        error = "Invalid memory area " + std::to_string(area) + ", use list_memory_areas";
        return false;
    }

    return true;
}

json DebugAdapter::ReadMemory(int area, const std::string& offset_text, size_t size)
{
    GuiDebugMemoryArea info;
    std::string error;
    u32 offset = 0;

    if (!GetMemoryArea(area, info, error) || !ParseAreaOffset(info, offset_text, offset, error))
        return {{"error", error}};

    if (size == 0 || size > (size_t)k_mcp_max_read_size)
        return {{"error", "size must be 1-65536 bytes"}};

    if ((u64)offset >= info.size)
        return {{"error", "offset is outside the memory area"}};

    if ((u64)offset + size > info.size)
        size = (size_t)(info.size - offset);

    std::vector<u8> data(size);
    std::vector<GDOX_Debug_Memory_Status> status(size);
    GDOX_Debug_Memory_Address address = info.source;
    address.address = offset;
    gui_debug_memory_read(address, &data[0], &status[0], (u32)size);

    std::ostringstream hex;
    int unavailable = 0;

    for (size_t i = 0; i < size; i++)
    {
        if (i > 0)
            hex << " ";

        if (status[i] == GDOX_DEBUG_MEMORY_VALID || status[i] == GDOX_DEBUG_MEMORY_READ_ONLY)
            hex << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << (int)data[i];
        else
        {
            hex << "??";
            unavailable++;
        }
    }

    json result = {
        {"area", area},
        {"offset", FormatAreaOffset(info, offset)},
        {"size", size},
        {"data", hex.str()}
    };

    if (unavailable > 0)
    {
        result["unavailable_bytes"] = unavailable;
        result["note"] = "?? marks unmapped bytes or bytes that cannot be read without side effects";
    }

    return result;
}

json DebugAdapter::WriteMemory(int area, const std::string& offset_text, const std::vector<u8>& data)
{
    GuiDebugMemoryArea info;
    std::string error;
    u32 offset = 0;

    if (!GetMemoryArea(area, info, error) || !ParseAreaOffset(info, offset_text, offset, error))
        return {{"error", error}};

    if (info.source.space == GDOX_DEBUG_MEMORY_IO)
        return {{"error", "The I/O PORTS area is read-only, writes would have side effects"}};

    if (data.empty())
        return {{"error", "bytes is empty"}};

    if ((u64)offset + data.size() > info.size)
        return {{"error", "write goes past the end of the memory area"}};

    GDOX_Debug_Memory_Address address = info.source;
    address.address = offset;

    if (!gui_debug_memory_write(address, &data[0], (u32)data.size()))
        return {{"error", "Write rejected: the range is read-only, unmapped or unavailable"}};

    return {{"success", true}, {"area", area}, {"offset", FormatAreaOffset(info, offset)},
        {"bytes_written", data.size()}};
}

json DebugAdapter::WriteIOPort(u32 port, u32 value, int size)
{
    if (emu_is_empty())
        return {{"error", "Emulator is powered off"}};

    if ((size != 8) && (size != 16) && (size != 32))
        return {{"error", "size must be 8, 16 or 32"}};

    if (port + (u32)(size / 8) > 0x10000)
        return {{"error", "port must be 0-FFFF and the write must end by FFFF"}};

    if ((size < 32) && ((value >> size) != 0))
        return {{"error", "value must fit in " + std::to_string(size) + " bits"}};

    m_core->DebugWritePort((u16)port, value, size);

    return {{"success", true}, {"port", Hex(port, 4)}, {"value", Hex(value, size / 4)}, {"size", size}};
}

json DebugAdapter::TranslateAddress(const std::string& text)
{
    McpAddress address;
    std::string error;

    if (!ParseAddress(text, address, error))
        return {{"error", error}};

    I386* cpu = m_core->GetI386();
    GDOX_Debug_Memory_Translation translation;
    memset(&translation, 0, sizeof(translation));
    bool translated = cpu->DebugTranslateLinear(address.linear, translation);
    json result;

    if (address.logical)
    {
        result["logical"] = Hex(address.segment, 4) + ":" + Hex32(address.offset);

        if (address.segment_register >= 0)
            result["segment_register"] = to_upper(k_mcp_segment_names[address.segment_register]);
    }

    result["linear"] = Hex32(address.linear);
    result["paging"] = (cpu->GetState()->cr0 & 0x80000000U) != 0;

    if (result["paging"].get<bool>())
    {
        result["pde_index"] = address.linear >> 22;
        result["pte_index"] = (address.linear >> 12) & 0x3FF;

        if (translation.page_directory_entry != 0)
        {
            u32 value = 0;
            u8 bytes[4];
            GDOX_Debug_Memory_Status status[4];
            GDOX_Debug_Memory_Address physical = {};
            physical.space = GDOX_DEBUG_MEMORY_PHYSICAL;
            physical.segment_register = -1;
            physical.address = translation.page_directory_entry;
            gui_debug_memory_read(physical, bytes, status, 4);
            value = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | ((u32)bytes[3] << 24);
            result["pde_address"] = Hex32(translation.page_directory_entry);
            result["pde"] = Hex32(value);

            if (translation.page_table_entry != 0)
            {
                physical.address = translation.page_table_entry;
                gui_debug_memory_read(physical, bytes, status, 4);
                value = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | ((u32)bytes[3] << 24);
                result["pte_address"] = Hex32(translation.page_table_entry);
                result["pte"] = Hex32(value);
            }
        }

        if (translated)
        {
            u32 pte = translation.page_flags >> 12;
            u32 pde = translation.page_flags & 0xFFF;
            result["flags"] = {
                {"present", true},
                {"user", (pde & pte & 0x04) != 0},
                {"writable", (pde & pte & 0x02) != 0},
                {"accessed", (pte & 0x20) != 0},
                {"dirty", (pte & 0x40) != 0}
            };
        }
    }

    if (!translated)
    {
        result["success"] = false;
        result["reason"] = translation.reason;
        return result;
    }

    result["success"] = true;
    result["physical"] = Hex32(translation.physical);

    if (translation.bus_valid)
        result["bus"] = Hex32(translation.bus);

    if (translation.region_valid)
    {
        result["region"] = translation.region_name;
        result["region_offset"] = Hex32(translation.region_offset);
    }

    return result;
}

json DebugAdapter::SelectMemoryRange(int area, u32 start_address, u32 end_address)
{
    GuiDebugMemoryArea info;
    std::string error;

    if (!GetMemoryArea(area, info, error))
        return {{"error", error}};

    if (start_address > end_address)
        std::swap(start_address, end_address);

    if ((u64)end_address >= info.size)
        return {{"error", "Selection range outside memory area"}};

    if (!gui_debug_memory_select_range(info.source, start_address, end_address))
        return {{"error", "Unable to apply memory selection"}};

    return {
        {"success", true},
        {"area", area},
        {"start_address", FormatAreaOffset(info, start_address)},
        {"end_address", FormatAreaOffset(info, end_address)}
    };
}

json DebugAdapter::GetMemorySelection(int area)
{
    GuiDebugMemoryArea info;
    std::string error;

    if (!GetMemoryArea(area, info, error))
        return {{"error", error}};

    u32 start = 0;
    u32 end = 0;
    json result = {{"area", area}};

    if (gui_debug_memory_get_selection(info.source, start, end))
    {
        result["selected"] = true;
        result["start"] = FormatAreaOffset(info, start);
        result["end"] = FormatAreaOffset(info, end);
        result["size"] = (u64)end - start + 1;
    }
    else
    {
        result["selected"] = false;
        result["size"] = 0;
    }

    return result;
}

json DebugAdapter::AddMemoryBookmark(int area, u32 address, const std::string& name)
{
    GuiDebugMemoryArea info;
    std::string error;

    if (!GetMemoryArea(area, info, error))
        return {{"error", error}};

    if ((u64)address >= info.size)
        return {{"error", "Bookmark address outside memory area"}};

    GDOX_Debug_Memory_Address bookmark = info.source;
    bookmark.address = address;
    gui_debug_memory_add_bookmark(bookmark, address, name.c_str());

    return {{"success", true}, {"area", area}, {"address", FormatAreaOffset(info, address)},
        {"name", name.empty() ? "auto-generated" : name}};
}

json DebugAdapter::RemoveMemoryBookmark(int area, u32 address)
{
    GuiDebugMemoryArea info;
    std::string error;

    if (!GetMemoryArea(area, info, error))
        return {{"error", error}};

    GDOX_Debug_Memory_Address bookmark = info.source;
    bookmark.address = address;
    bool removed = gui_debug_memory_remove_bookmark(bookmark);

    return {{"success", true}, {"removed", removed}, {"area", area}, {"address", FormatAreaOffset(info, address)}};
}

json DebugAdapter::ListMemoryBookmarks(int area)
{
    GuiDebugMemoryArea info;
    std::string error;

    if (!GetMemoryArea(area, info, error))
        return {{"error", error}};

    std::vector<GuiDebugMemoryBookmark> bookmarks;
    gui_debug_memory_get_bookmarks(info.source, bookmarks);
    json items = json::array();

    for (size_t i = 0; i < bookmarks.size(); i++)
    {
        items.push_back({
            {"address", FormatAreaOffset(info, bookmarks[i].address.address)},
            {"name", bookmarks[i].name}
        });
    }

    return {{"area", area}, {"bookmarks", items}, {"count", items.size()}};
}

json DebugAdapter::AddMemoryWatch(int area, u32 address, const std::string& notes, int size, bool freeze)
{
    GuiDebugMemoryArea info;
    std::string error;

    if (!GetMemoryArea(area, info, error))
        return {{"error", error}};

    if ((u64)address >= info.size)
        return {{"error", "Watch address outside memory area"}};

    GDOX_Debug_Memory_Address watch = info.source;
    watch.address = address;

    if (!gui_debug_memory_add_watch(watch, notes.c_str(), size / 8, freeze))
        return {{"error", "Unable to add memory watch, size must be 8, 16, 32 or 64"}};

    if (freeze)
        config_debug.debug = true;

    return {{"success", true}, {"area", area}, {"address", FormatAreaOffset(info, address)}, {"notes", notes},
        {"size", size}, {"frozen", freeze}};
}

json DebugAdapter::RemoveMemoryWatch(int area, u32 address)
{
    GuiDebugMemoryArea info;
    std::string error;

    if (!GetMemoryArea(area, info, error))
        return {{"error", error}};

    GDOX_Debug_Memory_Address watch = info.source;
    watch.address = address;
    bool removed = gui_debug_memory_remove_watch(watch);

    return {{"success", true}, {"removed", removed}, {"area", area}, {"address", FormatAreaOffset(info, address)}};
}

json DebugAdapter::ListMemoryWatches(int area)
{
    GuiDebugMemoryArea info;
    std::string error;

    if (!GetMemoryArea(area, info, error))
        return {{"error", error}};

    std::vector<GuiDebugMemoryWatch> watches;
    gui_debug_memory_get_watches(info.source, watches);
    json items = json::array();

    for (size_t i = 0; i < watches.size(); i++)
    {
        json item = {
            {"address", FormatAreaOffset(info, watches[i].address.address)},
            {"notes", watches[i].name},
            {"size", watches[i].size * 8},
            {"frozen", watches[i].freeze}
        };

        if (watches[i].valid)
        {
            std::ostringstream value;
            value << std::hex << std::uppercase << std::setfill('0') << std::setw(watches[i].size * 2) <<
                (unsigned long long)watches[i].value;
            item["value"] = value.str();
        }
        else
            item["value"] = json();

        items.push_back(item);
    }

    return {{"area", area}, {"watches", items}, {"count", items.size()}};
}

json DebugAdapter::MemorySearchCapture(int area, const json& arguments)
{
    GuiDebugMemoryArea info;
    std::string error;
    u32 start = 0;
    u32 size = 0;

    if (!GetMemoryArea(area, info, error) || !GetAreaRange(info, arguments, start, size, error))
        return {{"error", error}};

    int width = arguments.value("width", 8);

    if (width != 8 && width != 16 && width != 32)
        return {{"error", "width must be 8, 16 or 32"}};

    if (!gui_debug_memory_search_capture(info.source, start, size, width / 8))
        return {{"error", "Unable to capture the memory range"}};

    return {{"success", true}, {"area", area}, {"start", FormatAreaOffset(info, start)}, {"size", size},
        {"width", width}};
}

json DebugAdapter::MemorySearch(int area, const std::string& op, const std::string& compare_type, u64 compare_value,
    const std::string& data_type)
{
    GuiDebugMemoryArea info;
    std::string error;

    if (!GetMemoryArea(area, info, error))
        return {{"error", error}};

    int op_index = 0;

    if (op == "<") op_index = 0;
    else if (op == ">") op_index = 1;
    else if (op == "==") op_index = 2;
    else if (op == "!=") op_index = 3;
    else if (op == "<=") op_index = 4;
    else if (op == ">=") op_index = 5;
    else
        return {{"error", "Invalid operator"}};

    bool previous = compare_type == "previous";

    if (compare_type == "address")
    {
        if (compare_value >= info.size)
            return {{"error", "Compare address outside memory area"}};

        u8 bytes[4] = { };
        GDOX_Debug_Memory_Status status[4];
        GDOX_Debug_Memory_Address address = info.source;
        address.address = (u32)compare_value;
        gui_debug_memory_read(address, bytes, status, 4);
        compare_value = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | ((u64)bytes[3] << 24);
    }
    else if (!previous && compare_type != "value")
        return {{"error", "Invalid compare_type"}};

    std::vector<GuiDebugMemorySearchResult> results;
    int count = gui_debug_memory_search(info.source, op_index, previous, compare_value, data_type == "signed", results);

    if (count < 0)
        return {{"error", "No search captured in this area, call memory_search_capture first"}};

    json items = json::array();
    int max_results = MIN((int)results.size(), 1000);

    for (int i = 0; i < max_results; i++)
    {
        json item = json::array();
        item.push_back(FormatAreaOffset(info, results[i].address));

        if (data_type == "hex")
        {
            item.push_back(Hex((u32)results[i].current, 2));
            item.push_back(Hex((u32)results[i].previous, 2));
        }
        else
        {
            item.push_back(results[i].current);
            item.push_back(results[i].previous);
        }

        items.push_back(item);
    }

    json result = {{"area", area}, {"count", count}, {"fields", json::array({"address", "value", "previous"})},
        {"results", items}};

    if (count > max_results)
        result["total_matches"] = count;

    return result;
}

json DebugAdapter::MemoryFind(int area, const std::string& value, bool text, bool case_sensitive, const json& arguments)
{
    GuiDebugMemoryArea info;
    std::string error;
    u32 start = 0;
    u32 size = 0;

    if (!GetMemoryArea(area, info, error) || !GetAreaRange(info, arguments, start, size, error))
        return {{"error", error}};

    if (value.empty())
        return {{"error", text ? "text is empty" : "hex_bytes is empty"}};

    std::vector<u8> pattern;

    if (text)
    {
        if (value.size() > 512)
            return {{"error", "text must not exceed 512 bytes"}};

        pattern.assign(value.begin(), value.end());
    }
    else
    {
        std::string compact;

        for (size_t i = 0; i < value.size(); i++)
        {
            if (!isspace((unsigned char)value[i]))
                compact += value[i];
        }

        if (compact.empty() || (compact.size() & 1) != 0)
            return {{"error", "hex_bytes must contain valid hex byte pairs"}};

        for (size_t i = 0; i < compact.size(); i += 2)
        {
            u8 byte = 0;

            if (!parse_hex_string(compact.c_str() + i, 2, &byte))
                return {{"error", "hex_bytes must contain valid hex byte pairs"}};

            pattern.push_back(byte);
        }

        case_sensitive = true;
    }

    std::vector<u32> addresses;
    int count = gui_debug_memory_find(info.source, start, size, pattern, case_sensitive, addresses, 100);

    if (count < 0)
        return {{"error", "Pattern does not fit in the searched range"}};

    json items = json::array();

    for (size_t i = 0; i < addresses.size(); i++)
        items.push_back(FormatAreaOffset(info, addresses[i]));

    json result = {{"area", area}, {"count", count}, {"addresses", items}};

    if (count > 100)
        result["total_matches"] = count;

    return result;
}

json DebugAdapter::GetI386Status()
{
    I386* cpu = m_core->GetI386();
    I386_State state;
    cpu->CopyState(state);

    const I386_Segment& cs = state.segments[I386_SEGMENT_CS];
    const I386_Segment& ss = state.segments[I386_SEGMENT_SS];
    bool code32 = (cs.attributes & I386_SEGMENT_DEFAULT_32) != 0;
    bool stack32 = (ss.attributes & I386_SEGMENT_DEFAULT_32) != 0;
    static const char* register_names[I386_REG_COUNT] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
    json status;

    for (int i = 0; i < I386_REG_COUNT; i++)
        status[register_names[i]] = Hex32(state.registers[i].value);

    status["eip"] = Hex32(state.eip);
    status["eflags"] = Hex32(state.eflags);

    json flags;

    for (size_t i = 0; i < sizeof(k_mcp_eflags) / sizeof(k_mcp_eflags[0]); i++)
        flags[k_mcp_eflags[i].name] = (state.eflags & k_mcp_eflags[i].mask) != 0;

    // On a 386 these bits don't exist, and the 386DX reads CR0 bits 5-30 as 1
    if (cpu->IsI486())
        flags["ac"] = (state.eflags & I386_FLAG_AC) != 0;

    status["flags"] = flags;
    status["cs_eip"] = Hex(cs.selector, 4) + ":" + Hex(state.eip, code32 ? 8 : 4);
    status["ss_esp"] = Hex(ss.selector, 4) + ":" + Hex(state.registers[I386_REG_ESP].value, stack32 ? 8 : 4);

    GDOX_Debug_Memory_Translation pc;
    memset(&pc, 0, sizeof(pc));
    cpu->DebugTranslateLogical(I386_SEGMENT_CS, state.eip, pc);
    status["linear_pc"] = pc.linear_valid ? json(Hex32(pc.linear)) : json(NULL);
    status["physical_pc"] = pc.physical_valid ? json(Hex32(pc.physical)) : json(NULL);

    status["cr0"] = Hex32(state.cr0);
    status["cr2"] = Hex32(state.cr2);
    status["cr3"] = Hex32(state.cr3);

    if (cpu->IsI586())
    {
        u64 tsc = cpu->GetTimeStamp();
        status["cr4"] = Hex32(state.cr4);
        status["tsc"] = Hex32((u32)(tsc >> 32)) + Hex32((u32)tsc);
    }

    json cr0_flags;

    for (size_t i = 0; i < sizeof(k_mcp_cr0_flags) / sizeof(k_mcp_cr0_flags[0]); i++)
        cr0_flags[k_mcp_cr0_flags[i].name] = (state.cr0 & k_mcp_cr0_flags[i].mask) != 0;

    for (size_t i = 0; cpu->IsI486() && (i < sizeof(k_mcp_cr0_486_flags) / sizeof(k_mcp_cr0_486_flags[0])); i++)
        cr0_flags[k_mcp_cr0_486_flags[i].name] = (state.cr0 & k_mcp_cr0_486_flags[i].mask) != 0;

    status["cr0_flags"] = cr0_flags;
    status["mode"] = execution_mode_name(state.execution_mode);
    status["cpl"] = state.current_privilege_level;
    status["iopl"] = (state.eflags & I386_FLAG_IOPL) >> 12;
    status["code_size"] = code32 ? 32 : 16;
    status["stack_size"] = stack32 ? 32 : 16;

    // Only exceptions the CPU raised, interrupts and INT n don't replace it
    const I386_Exception_Info& exception = cpu->GetLastException();

    if (!exception.valid)
        status["last_exception"] = json();
    else
    {
        char name[16];
        char description[64];
        gui_debug_i386_vector_name(exception.vector, name, sizeof(name), description, sizeof(description), true,
            I386_INTERRUPT_EXCEPTION);
        status["last_exception"] = {
            {"vector", Hex(exception.vector, 2)},
            {"name", name},
            {"description", description},
            {"return_address", Hex(exception.cs, 4) + ":" + Hex32(exception.eip)}
        };

        if (exception.has_error_code)
            status["last_exception"]["error_code"] = Hex(exception.error_code, 4);
    }

    status["halted"] = state.halted;
    status["shutdown"] = state.shutdown;

    json segments = json::array();

    for (int i = 0; i < I386_SEGMENT_COUNT; i++)
    {
        int index = k_mcp_segment_order[i];
        const I386_Segment& segment = state.segments[index];
        segments.push_back({
            {"name", to_upper(k_mcp_segment_names[index])},
            {"selector", Hex(segment.selector, 4)},
            {"base", Hex32(segment.base)},
            {"limit", Hex32(segment.limit)},
            {"access", Hex(segment.attributes, 4)},
            {"dpl", segment.dpl},
            {"present", (segment.attributes & I386_SEGMENT_PRESENT) != 0},
            {"size", (segment.attributes & I386_SEGMENT_DEFAULT_32) != 0 ? 32 : 16}
        });
    }

    status["segments"] = segments;
    status["gdtr"] = {{"base", Hex32(state.gdtr.base)}, {"limit", Hex(state.gdtr.limit, 4)}};
    status["idtr"] = {{"base", Hex32(state.idtr.base)}, {"limit", Hex(state.idtr.limit, 4)}};
    status["ldtr"] = {{"selector", Hex(state.ldtr.selector, 4)}, {"base", Hex32(state.ldtr.base)},
        {"limit", Hex32(state.ldtr.limit)}, {"access", Hex(state.ldtr.attributes, 4)}, {"dpl", state.ldtr.dpl}};
    status["tr"] = {{"selector", Hex(state.task_register.selector, 4)}, {"base", Hex32(state.task_register.base)},
        {"limit", Hex32(state.task_register.limit)}, {"access", Hex(state.task_register.attributes, 4)},
        {"dpl", state.task_register.dpl}};
    status["debug_registers"] = {
        {"dr0", Hex32(state.debug_registers[0])}, {"dr1", Hex32(state.debug_registers[1])},
        {"dr2", Hex32(state.debug_registers[2])}, {"dr3", Hex32(state.debug_registers[3])},
        {"dr6", Hex32(state.debug_registers[6])}, {"dr7", Hex32(state.debug_registers[7])}
    };
    status["test_registers"] = {{"tr6", Hex32(state.test_registers[0])}, {"tr7", Hex32(state.test_registers[1])}};

    if (cpu->IsI486() && !cpu->IsI586())
    {
        const u32* cache = cpu->GetCacheTestRegisters();
        status["test_registers"]["tr3"] = Hex32(cache[0]);
        status["test_registers"]["tr4"] = Hex32(cache[1]);
        status["test_registers"]["tr5"] = Hex32(cache[2]);
    }

    if (cpu->IsI586())
    {
        status["test_registers"] = json();
        json msrs = json::object();

        for (u32 i = 0; i < I386_MSR_COUNT; i++)
        {
            u64 value = 0;

            if (cpu->PeekMSR(i, value))
                msrs[Hex(i, 2)] = Hex32((u32)(value >> 32)) + Hex32((u32)value);
        }

        status["msrs"] = msrs;
    }
    status["fpu"] = m_core->GetMachineConfig().fpu ? GetX87Status() : json();

    return status;
}

json DebugAdapter::GetX87Status()
{
    X87* fpu = m_core->GetX87();
    X87::X87_State* state = fpu->GetState();
    u16 tags = fpu->GetTagWord();
    int top = (state->status >> 11) & 7;
    json stack = json::array();

    for (int i = 0; i < X87_REGISTER_COUNT; i++)
    {
        int physical = (top + i) & 7;
        int tag = (tags >> (physical * 2)) & 3;
        const X87::X87_Register& value = state->registers[physical];
        char text[64];
        gui_debug_x87_format_value(value.sign_exponent, value.significand, text, sizeof(text));
        stack.push_back({
            {"st", i},
            {"register", physical},
            {"tag", to_lower(gui_debug_x87_tag_name(tag))},
            {"raw", Hex(value.sign_exponent, 4) + Hex((u32)(value.significand >> 32), 8) + Hex((u32)value.significand, 8)},
            {"value", tag == 3 ? json() : json(text)}
        });
    }

    u16 control = state->control;
    u16 status = state->status;

    return {
        {"control", Hex(control, 4)},
        {"masks", {{"im", (control & 0x01) != 0}, {"dm", (control & 0x02) != 0}, {"zm", (control & 0x04) != 0},
            {"om", (control & 0x08) != 0}, {"um", (control & 0x10) != 0}, {"pm", (control & 0x20) != 0}}},
        {"precision", to_lower(gui_debug_x87_precision_name(control))},
        {"rounding", to_lower(gui_debug_x87_rounding_name(control))},
        {"status", Hex(status, 4)},
        {"top", top},
        {"conditions", {{"c0", (status & 0x0100) != 0}, {"c1", (status & 0x0200) != 0},
            {"c2", (status & 0x0400) != 0}, {"c3", (status & 0x4000) != 0}}},
        {"exceptions", {{"ie", (status & 0x01) != 0}, {"de", (status & 0x02) != 0}, {"ze", (status & 0x04) != 0},
            {"oe", (status & 0x08) != 0}, {"ue", (status & 0x10) != 0}, {"pe", (status & 0x20) != 0},
            {"sf", (status & 0x40) != 0}, {"es", (status & 0x80) != 0}, {"busy", (status & 0x8000) != 0}}},
        {"tag", Hex(tags, 4)},
        {"stack", stack},
        {"last_instruction", Hex(state->instruction_selector, 4) + ":" + Hex32(state->instruction_offset)},
        {"last_opcode", Hex(0xD8 | ((state->opcode >> 8) & 7), 2) + " " + Hex(state->opcode & 0xFF, 2)},
        {"last_operand", Hex(state->operand_selector, 4) + ":" + Hex32(state->operand_offset)}
    };
}

json DebugAdapter::WriteI386Register(const std::string& name, u32 value)
{
    I386* cpu = m_core->GetI386();
    I386_State state;
    cpu->CopyState(state);
    std::string reg = to_upper(name);
    std::string error;
    bool pc_changed = false;
    int general = -1;
    int segment = -1;
    static const char* register_names[I386_REG_COUNT] = { "EAX", "ECX", "EDX", "EBX", "ESP", "EBP", "ESI", "EDI" };

    for (int i = 0; i < I386_REG_COUNT; i++)
    {
        if (reg == register_names[i])
            general = i;
    }

    for (int i = 0; i < I386_SEGMENT_COUNT; i++)
    {
        if (reg == to_upper(k_mcp_segment_names[i]))
            segment = i;
    }

    if (general >= 0)
        state.registers[general].value = value;
    else if (segment >= 0)
    {
        if (!load_segment(state, segment, value, error))
            return {{"error", error}};

        pc_changed = segment == I386_SEGMENT_CS;
    }
    else if (reg == "EIP")
    {
        state.eip = value;
        state.halted = false;
        pc_changed = true;
    }
    else if (reg == "EFLAGS")
    {
        state.eflags = (value & (0x00037FD5U | (cpu->IsI486() ? I386_FLAG_AC : 0))) | I386_FLAG_FIXED;
        pc_changed = true;
    }
    else if (reg == "CR0")
    {
        state.cr0 = value;
        pc_changed = true;
    }
    else if (reg == "CR2")
        state.cr2 = value;
    else if (reg == "CR3")
    {
        state.cr3 = value;
        pc_changed = true;
    }
    else if (reg == "CR4" && cpu->IsI586())
        state.cr4 = value & k_i386_cr4_writable;
    else if (reg.size() == 3 && reg[0] == 'D' && reg[1] == 'R' && reg[2] >= '0' && reg[2] <= '7')
        state.debug_registers[reg[2] - '0'] = value;
    else if (reg == "TR6" || reg == "TR7")
        state.test_registers[reg[2] - '6'] = value;
    else if ((reg == "TR3" || reg == "TR4" || reg == "TR5") && cpu->IsI486())
        cpu->SetCacheTestRegister(reg[2] - '3', value);
    else if (reg == "GDTR_BASE")
        state.gdtr.base = value;
    else if (reg == "GDTR_LIMIT" && value <= 0xFFFF)
        state.gdtr.limit = (u16)value;
    else if (reg == "IDTR_BASE")
        state.idtr.base = value;
    else if (reg == "IDTR_LIMIT" && value <= 0xFFFF)
        state.idtr.limit = (u16)value;
    else if (reg == "LDTR" || reg == "TR")
    {
        char text[GDOX_DEBUG_MEMORY_REASON_SIZE];
        bool task = reg == "TR";

        // Like LLDT and LTR, the selector loads its descriptor from the GDT
        if (!gui_debug_i386_load_system_segment(state, task, value, task ? state.task_register : state.ldtr, text, sizeof(text)))
            return {{"error", text}};
    }
    else
        return {{"error", "Invalid register name or value, use EAX-EDI, EIP, EFLAGS, CR0, CR2, CR3, CR4 (Pentium), DR0-DR7, "
            "TR3-TR5 (486), TR6, TR7, CS-GS, LDTR, TR, GDTR_BASE, GDTR_LIMIT, IDTR_BASE or IDTR_LIMIT (limits are 16-bit)"}};

    state.repeat.active = false;
    cpu->SetState(state);

    if (pc_changed)
        emu_debug_pc_changed = true;

    return {{"success", true}, {"register", reg}, {"value", Hex32(value)}};
}

json DebugAdapter::GetDisassembly(u32 start_address, u32 end_address, int count, int code_size, bool resolve_symbols,
    bool detailed)
{
    I386* cpu = m_core->GetI386();
    I386_State* state = cpu->GetState();
    I386_Segment code = state->segments[I386_SEGMENT_CS];
    bool inside_cs = start_address - code.base <= code.limit;

    if (!inside_cs)
    {
        code.selector = 0;
        code.base = 0;
        code.limit = 0xFFFFFFFF;
        code.attributes = (code.attributes & I386_SEGMENT_DEFAULT_32) | I386_SEGMENT_PRESENT | I386_SEGMENT_READABLE |
            I386_SEGMENT_EXECUTABLE;
    }

    if (code_size == 16)
        code.attributes &= ~I386_SEGMENT_DEFAULT_32;
    else if (code_size == 32)
        code.attributes |= I386_SEGMENT_DEFAULT_32;

    u32 address = start_address;
    json lines = json::array();

    // A passive decode, so the GUI disassembler and its labels don't change
    while ((count > 0 ? (int)lines.size() < count : address <= end_address) && (int)lines.size() < k_mcp_max_disassembly_lines)
    {
        u32 eip = address - code.base;
        I386_Disassembler_Record decoded;
        I386_Disassembler_Record* record = eip <= code.limit && cpu->DisassemblePassive(code, eip, decoded) ? &decoded : NULL;

        if (!IsValidPointer(record) || record->name[0] == 0 || record->size <= 0)
        {
            lines.push_back({{"linear", Hex32(address)}, {"bytes", ""}, {"mnemonic", "??"}, {"operands", ""},
                {"length", 1}});

            if (address == 0xFFFFFFFFU)
                break;

            address++;
            continue;
        }

        std::string mnemonic;
        std::string operands;
        split_instruction(record->name, mnemonic, operands);

        json line = {
            {"linear", Hex32(record->linear)},
            {"bytes", record->bytes},
            {"mnemonic", mnemonic},
            {"operands", operands},
            {"length", record->size}
        };

        if (record->cs != 0 || inside_cs)
            line["logical"] = Hex(record->cs, 4) + ":" + Hex(record->eip, record->default32 ? 8 : 4);

        if (resolve_symbols && IsValidPointer(GetSymbolAt(record->linear)))
            line["symbol"] = GetSymbolAt(record->linear);

        if (detailed)
        {
            int index = get_opcode_index(record);
            u8 opcode = index < record->size ? record->opcodes[index] : 0;
            const char* flow = NULL;

            if (opcode == 0xCF)
                flow = "iret";
            else if (record->returns)
                flow = "return";
            else if (opcode == 0xCC || opcode == 0xCD || opcode == 0xCE || opcode == 0xF1)
                flow = "int";
            else if (record->subroutine)
                flow = "call";
            else if (record->jump)
                flow = record->unconditional ? "jump" : "conditional";

            if (IsValidPointer(flow))
                line["flow"] = flow;

            if (record->jump && record->jump_target_known)
            {
                json target = {{"linear", Hex32(record->jump_linear)}};

                if (IsValidPointer(GetSymbolAt(record->jump_linear)))
                    target["symbol"] = GetSymbolAt(record->jump_linear);

                line["target"] = target;
            }

            if (opcode >= 0xE4 && opcode <= 0xE7 && index + 1 < record->size)
            {
                u16 port = record->opcodes[index + 1];
                const char* label = gui_debug_port_label(port);
                line["port"] = Hex(port, 4);

                if (IsValidPointer(label))
                    line["port_name"] = label;
            }

            if ((opcode == 0xCD && index + 1 < record->size) || opcode == 0xCC || opcode == 0xCE || opcode == 0xF1)
            {
                u8 vector = opcode == 0xCD ? record->opcodes[index + 1] : opcode == 0xCC ? 3 : opcode == 0xCE ? 4 : 1;
                char name[16];
                char description[64];
                gui_debug_i386_vector_name(vector, name, sizeof(name), description, sizeof(description), true,
                    I386_INTERRUPT_SOFTWARE);
                line["vector"] = Hex(vector, 2);
                line["vector_name"] = name;
                line["vector_description"] = description;

                // AX only names the function for the instruction about to run
                if (record->linear == m_core->GetI386()->GetCurrentLinearPC())
                {
                    I386_Debug_State state;
                    m_core->GetI386()->CopyDebugState(state);
                    const char* function = gui_debug_i386_interrupt_function(vector, state.eax);

                    if (IsValidPointer(function))
                        line["function"] = function;
                }
            }
        }

        lines.push_back(line);

        u32 next = address + (u32)record->size;

        if (next <= address)
            break;

        address = next;
    }

    json result = {
        {"start_address", Hex32(start_address)},
        {"code_size", (code.attributes & I386_SEGMENT_DEFAULT_32) != 0 ? 32 : 16},
        {"lines", lines},
        {"count", lines.size()}
    };

    if (count <= 0)
        result["end_address"] = Hex32(end_address);

    if (!inside_cs)
        result["note"] = "The range is outside CS, decoded as flat code with the selected size";

    return result;
}

json DebugAdapter::ListCallStack()
{
    I386* cpu = m_core->GetI386();
    const std::vector<I386_CallStackEntry>& entries = cpu->GetDisassemblerCallStack();
    static const char* kind_names[] = { "call", "interrupt", "interrupt", "exception" };
    json stack = json::array();

    for (int i = (int)entries.size() - 1; i >= 0; i--)
    {
        const I386_CallStackEntry& entry = entries[i];
        json item = {
            {"kind", kind_names[entry.type & 3]},
            {"from", {{"logical", FormatLogical(entry.src_cs, entry.src, entry.src_linear)},
                {"linear", Hex32(entry.src_linear)}}},
            {"to", {{"logical", FormatLogical(entry.dest_cs, entry.dest, entry.dest_linear)},
                {"linear", Hex32(entry.dest_linear)}}},
            {"return", {{"logical", FormatLogical(entry.back_cs, entry.back, entry.back_linear)},
                {"linear", Hex32(entry.back_linear)}}}
        };

        if (entry.type != I386_CALL)
        {
            item["vector"] = Hex(entry.vector, 2);
            item["source"] = entry.type == I386_CALL_SOFTWARE_INTERRUPT ? "software" :
                entry.type == I386_CALL_HARDWARE_INTERRUPT ? "hardware" : "exception";

            char name[16];
            char description[64];
            gui_debug_i386_vector_name(entry.vector, name, sizeof(name), description, sizeof(description), true,
                entry.type == I386_CALL_SOFTWARE_INTERRUPT ? I386_INTERRUPT_SOFTWARE :
                entry.type == I386_CALL_HARDWARE_INTERRUPT ? I386_INTERRUPT_HARDWARE : I386_INTERRUPT_EXCEPTION);
            item["vector_name"] = name;
            item["vector_description"] = description;
        }

        if (IsValidPointer(GetSymbolAt(entry.dest_linear)))
            item["symbol"] = GetSymbolAt(entry.dest_linear);

        stack.push_back(item);
    }

    return {{"stack", stack}, {"depth", stack.size()}};
}

json DebugAdapter::AddDisassemblerBookmark(u32 address, const std::string& name)
{
    gui_debug_add_disassembler_bookmark(address, name.c_str());
    return {{"success", true}, {"address", Hex32(address)}, {"name", gui_debug_get_disassembler_bookmarks()->back().name}};
}

json DebugAdapter::RemoveDisassemblerBookmark(u32 address)
{
    bool removed = gui_debug_remove_disassembler_bookmark(address);
    return {{"success", true}, {"removed", removed}, {"address", Hex32(address)}};
}

json DebugAdapter::ListDisassemblerBookmarks()
{
    std::vector<DisassemblerBookmark>* bookmarks = gui_debug_get_disassembler_bookmarks();
    json items = json::array();

    for (size_t i = 0; i < bookmarks->size(); i++)
        items.push_back({{"address", Hex32((*bookmarks)[i].address)}, {"name", (*bookmarks)[i].name}});

    return {{"bookmarks", items}, {"count", items.size()}};
}

// Pages through user symbols, then automatic labels, an address with a user symbol doesn't list its label
json DebugAdapter::ListSymbols(const std::string& filter, int start, int count)
{
    json symbols = json::array();
    std::string needle = to_lower(filter);
    const std::map<u32, std::string>& users = gui_debug_get_user_symbols();
    std::map<u32, std::string>::const_iterator user;
    int total = 0;

    start = MAX(start, 0);
    count = CLAMP(count, 1, k_mcp_max_symbols);

    for (user = users.begin(); user != users.end(); user++)
    {
        if (!needle.empty() && to_lower(user->second).find(needle) == std::string::npos)
            continue;

        if (total >= start && (int)symbols.size() < count)
            symbols.push_back({{"address", Hex32(user->first)}, {"name", user->second}, {"type", "user"}});

        total++;
    }

    const std::map<u32, I386_Disassembler_Record>& records = m_core->GetI386()->GetDisassemblerRecords();
    std::map<u32, I386_Disassembler_Record>::const_iterator record;

    for (record = records.begin(); record != records.end(); record++)
    {
        if (record->second.auto_symbol[0] == 0 || users.find(record->first) != users.end())
            continue;

        if (!needle.empty() && to_lower(record->second.auto_symbol).find(needle) == std::string::npos)
            continue;

        if (total >= start && (int)symbols.size() < count)
        {
            symbols.push_back({
                {"address", Hex32(record->first)},
                {"name", record->second.auto_symbol},
                {"type", "automatic"}
            });
        }

        total++;
    }

    return {{"symbols", symbols}, {"count", symbols.size()}, {"start", start}, {"total", total},
        {"has_more", start + (int)symbols.size() < total}};
}

json DebugAdapter::AddSymbol(u32 address, const std::string& name)
{
    if (!gui_debug_add_user_symbol(address, name.c_str()))
        return {{"error", "Invalid symbol name: letters, digits, _ . @ ? $, not starting with a digit, up to 63 characters"}};

    return {{"success", true}, {"address", Hex32(address)}, {"name", name}};
}

json DebugAdapter::RemoveSymbol(u32 address)
{
    return {{"success", true}, {"removed", gui_debug_remove_user_symbol(address)}, {"address", Hex32(address)}};
}

json DebugAdapter::LoadSymbols(const std::string& file_path, bool program)
{
    if (program)
    {
        Dev_Program_Info info;
        emu_dev_get_program(&info);

        if (info.state != DEV_PORT_PROGRAM_RUNNING)
            return {{"error", "No program is running: start one with run_program first"}};

        int loaded = emu_dev_load_program_symbols(file_path.c_str());

        if (loaded < 0)
            return {{"error", "Unable to read " + file_path}};

        return {{"success", true}, {"file_path", file_path}, {"loaded", loaded}, {"program", true},
            {"base", Hex32(info.base)}, {"load_segment", Hex(info.load_segment, 4)}};
    }

    int skipped = 0;
    int count = gui_debug_load_symbols(file_path.c_str(), &skipped);

    if (count < 0)
        return {{"error", "Unable to read " + file_path}};

    return {{"success", true}, {"file_path", file_path}, {"loaded", count}, {"skipped", skipped}};
}

json DebugAdapter::RunProgram(const std::string& command, bool stop_at_entry)
{
    if (!emu_game_is_open())
        return {{"error", "No game descriptor is open: load_media a .gdox for the program's folder first"}};

    if (!emu_dev_run_program(command.c_str(), stop_at_entry))
        return {{"error", std::string("Unable to relaunch the game: ") + emu_game_get_error()}};

    return {{"success", true}, {"command", command.empty() ? std::string("the descriptor's program") : command},
        {"stop_at_entry", stop_at_entry}, {"note", "The game's disk is rebuilt from its folder and the machine boots: "
        "poll get_program_status until state is running or exited"}};
}

json DebugAdapter::RelaunchGame()
{
    if (!emu_dev_relaunch())
        return {{"error", "No game descriptor is open"}};

    return {{"success", true}, {"note", "Rebuilding the game's disk from its folder and running its program again"}};
}

json DebugAdapter::GetProgramStatus()
{
    static const char* const k_states[3] = { "none", "running", "exited" };
    Dev_Program_Info info;
    emu_dev_get_program(&info);
    json result = {
        {"state", k_states[info.state % 3]},
        {"runs", info.runs},
        {"output_bytes", emu_dev_get_output_count()}
    };

    if (info.runs == 0)
        return result;

    result["path"] = info.dos_path;
    result["host_path"] = info.host_path;
    result["psp"] = Hex(info.psp, 4);
    result["entry"] = Hex(info.entry_cs, 4) + ":" + Hex(info.entry_ip, 4);
    result["stack"] = Hex(info.stack_ss, 4) + ":" + Hex(info.stack_sp, 4);
    result["load_segment"] = Hex(info.load_segment, 4);
    result["base"] = Hex32(info.base);
    result["base_reported"] = info.based;
    result["code_size"] = info.code_size;
    result["coff"] = (info.load_flags & DEV_PORT_LOAD_COFF) != 0;

    if (info.based)
    {
        result["code_selector"] = Hex(info.code_selector, 4);
        result["data_selector"] = Hex(info.data_selector, 4);
    }

    if (!info.symbol_path.empty())
    {
        result["symbol_file"] = info.symbol_path;
        result["symbols"] = info.symbol_count;
    }

    if (info.state == DEV_PORT_PROGRAM_EXITED)
    {
        static const char* const k_types[4] = { "normal", "ctrl_c", "critical_error", "resident" };
        result["exit_code"] = info.exit_code;
        result["termination"] = (info.exit_type >= 0) && (info.exit_type < 4) ? k_types[info.exit_type] :
            info.exit_type == 0xFF ? "not_loaded" : "unknown";
    }

    return result;
}

json DebugAdapter::GetProgramOutput(u64 since, int max_bytes)
{
    u64 next = since;
    std::string text = emu_dev_get_output(since, (u32)CLAMP(max_bytes, 1, DEBUG_CONSOLE_SIZE), &next);
    u64 written = emu_dev_get_output_count();

    return {{"text", text}, {"next", next}, {"written", written}, {"has_more", next < written}};
}

json DebugAdapter::GetDosStatus()
{
    DevPort::DevPort_State* state = m_core->GetDevPort()->GetState();

    if (!state->dos_known)
        return {{"error", "DOS isn't known yet: run a program through GDXDBG (run_program or the debug target) first"}};

    I386* cpu = m_core->GetI386();
    u16 list_segment = (u16)(state->dos_list >> 16);
    u16 list_offset = (u16)state->dos_list;
    u32 list = ((u32)list_segment << 4) + list_offset;
    u8 low = 0;
    u8 high = 0;
    u8 indos = 0;

    if (!cpu->TryPeekLinear(list - 2, low) || !cpu->TryPeekLinear(list - 1, high))
        return {{"error", "The DOS list of lists can't be read"}};

    cpu->TryPeekLinear(((state->dos_indos >> 16) << 4) + (state->dos_indos & 0xFFFF), indos);

    u16 segment = (u16)(low | (high << 8));
    json blocks = json::array();
    u32 free_paragraphs = 0;
    u32 largest_free = 0;
    bool valid = true;

    for (int i = 0; i < 1024; i++)
    {
        u32 base = (u32)segment << 4;
        u8 bytes[16];

        for (int b = 0; b < 16; b++)
        {
            if (!cpu->TryPeekLinear(base + b, bytes[b]))
                valid = false;
        }

        if (!valid || ((bytes[0] != 0x4D) && (bytes[0] != 0x5A)))
        {
            valid = false;
            break;
        }

        u16 owner = (u16)(bytes[1] | (bytes[2] << 8));
        u16 size = (u16)(bytes[3] | (bytes[4] << 8));
        std::string name;

        for (int b = 8; (b < 16) && (bytes[b] >= 0x20) && (bytes[b] < 0x7F); b++)
            name += (char)bytes[b];

        json block = {{"mcb", Hex(segment, 4)}, {"owner", Hex(owner, 4)}, {"paragraphs", size}, {"bytes", (u32)size * 16}};

        if (owner == 0)
        {
            block["free"] = true;
            free_paragraphs += size;
            largest_free = MAX(largest_free, (u32)size);
        }
        else if (owner == 8)
            block["owner_name"] = "DOS";
        else if (!name.empty())
            block["owner_name"] = name;

        blocks.push_back(block);

        if (bytes[0] == 0x5A)
            break;

        segment = (u16)(segment + size + 1);
    }

    Dev_Program_Info info;
    emu_dev_get_program(&info);
    json result = {
        {"dos_version", std::to_string(state->dos_version & 0xFF) + "." + std::to_string(state->dos_version >> 8)},
        {"list_of_lists", Hex(list_segment, 4) + ":" + Hex(list_offset, 4)},
        {"indos", indos},
        {"memory_blocks", blocks},
        {"chain_valid", valid},
        {"free_bytes", free_paragraphs * 16},
        {"largest_free_bytes", largest_free * 16}
    };

    if (info.state == DEV_PORT_PROGRAM_RUNNING)
        result["program_psp"] = Hex(info.psp, 4);

    return result;
}

// Exact names, or with partial a case-insensitive substring
json DebugAdapter::LookupSymbolByName(const std::string& name, bool partial)
{
    json matches = json::array();
    std::string needle = to_lower(name);
    const std::map<u32, std::string>& users = gui_debug_get_user_symbols();
    std::map<u32, std::string>::const_iterator user;

    for (user = users.begin(); user != users.end() && (int)matches.size() < k_mcp_max_symbols; user++)
    {
        bool match = partial ? to_lower(user->second).find(needle) != std::string::npos : name == user->second;

        if (match)
            matches.push_back({{"address", Hex32(user->first)}, {"name", user->second}, {"type", "user"}});
    }

    const std::map<u32, I386_Disassembler_Record>& records = m_core->GetI386()->GetDisassemblerRecords();
    std::map<u32, I386_Disassembler_Record>::const_iterator record;

    for (record = records.begin(); record != records.end() && (int)matches.size() < k_mcp_max_symbols; record++)
    {
        if (record->second.auto_symbol[0] == 0)
            continue;

        bool match = partial ? to_lower(record->second.auto_symbol).find(needle) != std::string::npos :
            name == record->second.auto_symbol;

        if (match)
        {
            matches.push_back({
                {"address", Hex32(record->first)},
                {"name", record->second.auto_symbol},
                {"type", "automatic"}
            });
        }
    }

    return {{"matches", matches}, {"count", matches.size()}};
}

json DebugAdapter::LookupSymbolAtAddress(u32 address)
{
    I386_Disassembler_Record* record = m_core->GetI386()->GetDisassemblerRecord(address);
    json result = {{"address", Hex32(address)}, {"found", false}};
    const char* user = gui_debug_get_user_symbol(address);

    if (IsValidPointer(user))
    {
        result["found"] = true;
        result["name"] = user;
        result["type"] = "user";
    }
    else if (IsValidPointer(record) && record->auto_symbol[0] != 0)
    {
        result["found"] = true;
        result["name"] = record->auto_symbol;
        result["type"] = "automatic";
    }

    return result;
}

json DebugAdapter::GetI386Descriptors(const std::string& table, int start, int count)
{
    GuiDebugDescriptorTable id = table == "gdt" ? GuiDebugDescriptorTable_GDT : table == "ldt" ? GuiDebugDescriptorTable_LDT :
        GuiDebugDescriptorTable_IDT;

    if (table != "gdt" && table != "ldt" && table != "idt")
        return {{"error", "table must be gdt, ldt or idt"}};

    I386_State* state = m_core->GetI386()->GetState();
    u32 total = gui_debug_i386_table_entry_count(id);
    bool ivt = id == GuiDebugDescriptorTable_IDT && gui_debug_i386_idt_is_ivt();
    count = CLAMP(count, 1, 256);
    json entries = json::array();

    for (u32 i = (u32)MAX(start, 0); i < total && (int)entries.size() < count; i++)
    {
        GuiDebugDescriptor descriptor;
        bool readable = gui_debug_i386_read_table_entry(id, i, descriptor);
        json entry = {{"index", i}};

        if (id != GuiDebugDescriptorTable_IDT)
            entry["selector"] = hex_text((i << 3) | (id == GuiDebugDescriptorTable_LDT ? 4 : 0), 4);
        else
        {
            char name[16];
            char description[64];
            gui_debug_i386_vector_name((u8)i, name, sizeof(name), description, sizeof(description));
            entry["vector_name"] = name;
            entry["vector_description"] = description;
        }

        if (!readable)
        {
            entry["available"] = false;
            entries.push_back(entry);
            continue;
        }

        if (ivt)
        {
            entry["target"] = hex_text(descriptor.gate_selector, 4) + ":" + hex_text(descriptor.gate_offset, 4);
            entry["linear"] = Hex32(descriptor.base);
            entries.push_back(entry);
            continue;
        }

        char type[16];
        char flags[16];
        gui_debug_i386_descriptor_type(descriptor, type, sizeof(type));
        gui_debug_i386_descriptor_flags(descriptor, flags, sizeof(flags));
        entry["raw"] = Hex32(descriptor.high) + Hex32(descriptor.low);
        entry["type"] = (descriptor.low | descriptor.high) == 0 ? "NULL" : type;
        entry["dpl"] = descriptor.dpl;
        entry["present"] = descriptor.present;
        entry["flags"] = flags;

        if (descriptor.gate)
        {
            u32 base = 0;
            u32 limit = 0;
            char reason[GDOX_DEBUG_MEMORY_REASON_SIZE];
            entry["target"] = hex_text(descriptor.gate_selector, 4) + ":" + Hex32(descriptor.gate_offset);

            if (descriptor.type != 0x05 &&
                gui_debug_i386_descriptor_base(descriptor.gate_selector, base, limit, reason, sizeof(reason)))
            {
                entry["target_linear"] = Hex32(base + descriptor.gate_offset);

                if (IsValidPointer(GetSymbolAt(base + descriptor.gate_offset)))
                    entry["symbol"] = GetSymbolAt(base + descriptor.gate_offset);
            }
        }
        else
        {
            entry["base"] = Hex32(descriptor.base);
            entry["limit"] = Hex32(descriptor.limit);
        }

        entries.push_back(entry);
    }

    u32 base = id == GuiDebugDescriptorTable_GDT ? state->gdtr.base : id == GuiDebugDescriptorTable_LDT ? state->ldtr.base :
        state->idtr.base;
    u32 limit = id == GuiDebugDescriptorTable_GDT ? state->gdtr.limit : id == GuiDebugDescriptorTable_LDT ?
        state->ldtr.limit : state->idtr.limit;

    return {
        {"table", table},
        {"format", ivt ? "real_mode_ivt" : "descriptors"},
        {"base", Hex32(base)},
        {"limit", Hex32(limit)},
        {"entry_count", total},
        {"entries", entries}
    };
}

json DebugAdapter::GetPageDirectory(int index)
{
    I386_State* state = m_core->GetI386()->GetState();

    if ((state->cr0 & 0x80000000U) == 0)
        return {{"paging", false}, {"cr3", Hex32(state->cr3)}, {"note", "Paging is off: linear addresses are physical"}};

    u32 directory = state->cr3 & 0xFFFFF000U;
    json entries = json::array();
    char flags[16];

    if (index < 0)
    {
        for (u32 i = 0; i < 1024; i++)
        {
            u32 entry = 0;

            if (!gui_debug_i386_read_page_entry(directory, i, entry) || (entry & 1) == 0)
                continue;

            gui_debug_i386_page_flags(entry, flags, sizeof(flags));
            entries.push_back({{"index", hex_text(i, 3)}, {"linear_start", Hex32(i << 22)}, {"pde", Hex32(entry)},
                {"table", Hex32(entry & 0xFFFFF000U)}, {"flags", flags}});
        }

        return {{"paging", true}, {"cr3", Hex32(state->cr3)}, {"cr2", Hex32(state->cr2)}, {"present_entries", entries}};
    }

    if (index > 1023)
        return {{"error", "index must be 0-1023 (3FF)"}};

    u32 directory_entry = 0;

    if (!gui_debug_i386_read_page_entry(directory, (u32)index, directory_entry) || (directory_entry & 1) == 0)
        return {{"error", "Page-directory entry " + hex_text(index, 3) + " is not present"}};

    for (u32 i = 0; i < 1024; i++)
    {
        u32 entry = 0;

        if (!gui_debug_i386_read_page_entry(directory_entry, i, entry) || (entry & 1) == 0)
            continue;

        gui_debug_i386_page_flags(entry, flags, sizeof(flags));
        entries.push_back({{"index", hex_text(i, 3)}, {"linear", Hex32(((u32)index << 22) | (i << 12))},
            {"physical", Hex32(entry & 0xFFFFF000U)}, {"flags", flags}});
    }

    gui_debug_i386_page_flags(directory_entry, flags, sizeof(flags));
    return {
        {"paging", true},
        {"index", hex_text(index, 3)},
        {"pde", Hex32(directory_entry)},
        {"table", Hex32(directory_entry & 0xFFFFF000U)},
        {"pde_flags", flags},
        {"present_pages", entries}
    };
}

json DebugAdapter::GetPICStatus()
{
    PIC* pic = m_core->GetPIC();
    I8259::I8259_State* chips[2] = { pic->GetMaster()->GetState(), pic->GetSlave()->GetState() };
    json irqs = json::array();

    for (int irq = 0; irq < 16; irq++)
    {
        const I8259::I8259_State* chip = chips[irq >> 3];
        u8 bit = (u8)(1 << (irq & 7));
        u8 vector = (u8)((chip->icw2 & 0xF8) | (irq & 7));

        irqs.push_back({
            {"irq", irq},
            {"source", k_debug_irq_sources[irq]},
            {"level", (chip->input_levels & bit) != 0},
            {"requested", (chip->irr & bit) != 0},
            {"in_service", (chip->isr & bit) != 0},
            {"masked", (chip->imr & bit) != 0},
            {"vector", Hex(vector, 2)}
        });
    }

    json controllers = json::array();

    for (int i = 0; i < 2; i++)
    {
        const I8259::I8259_State* chip = chips[i];
        json item = {
            {"name", i == 0 ? "master" : "slave"},
            {"icw1", Hex(chip->icw1, 2)},
            {"icw2", Hex(chip->icw2, 2)},
            {"icw3", Hex(chip->icw3, 2)},
            {"icw4", Hex(chip->icw4, 2)},
            {"trigger", (chip->icw1 & k_i8259_icw1_ltim) ? "level" : "edge"},
            {"cascade", (chip->icw1 & k_i8259_icw1_sngl) == 0},
            {"vector_base", Hex(chip->icw2 & 0xF8, 2)},
            {"upm_8086", (chip->icw4 & k_i8259_icw4_upm) != 0},
            {"auto_eoi", (chip->icw4 & k_i8259_icw4_aeoi) != 0},
            {"buffered", (chip->icw4 & 0x08) != 0},
            {"special_fully_nested", (chip->icw4 & k_i8259_icw4_sfnm) != 0},
            {"init", chip->init_step == I8259::I8259_INIT_READY ? "ready" :
                std::string("waiting ") + to_lower(k_debug_pic_init_names[chip->init_step & 3])},
            {"read_register", chip->read_isr ? "isr" : "irr"},
            {"special_mask", chip->special_mask},
            {"poll_armed", chip->poll_pending},
            {"highest_priority_line", (chip->lowest_priority + 1) & 7},
            {"rotate_on_aeoi", chip->rotate_on_aeoi},
            {"int_output", chip->int_output},
            {"irr", Hex(chip->irr, 2)},
            {"isr", Hex(chip->isr, 2)},
            {"imr", Hex(chip->imr, 2)}
        };

        if (i == 0)
            item["slave_lines"] = Hex(chip->icw3, 2);
        else
            item["slave_id"] = chip->icw3 & 0x07;

        controllers.push_back(item);
    }

    return {{"irqs", irqs}, {"master", controllers[0]}, {"slave", controllers[1]}};
}

json DebugAdapter::GetPITStatus()
{
    PIT* pit = m_core->GetPIT();
    PIT::PIT_State* state = pit->GetState();
    u64 clocks = m_core->GetScheduler()->GetClocks();
    I8254* chip = pit->GetPIT();
    u64 tick = pit->GetTick(clocks);
    double rate = (double)GDOX_MASTER_CLOCK_RATE / 12.0;
    json counters = json::array();

    for (int channel = 0; channel < I8254_COUNTER_COUNT; channel++)
    {
        const I8254::I8254_Counter& counter = chip->GetState()->counters[channel];
        json item = {
            {"channel", channel},
            {"port", Hex(0x0040 + channel, 4)},
            {"use", to_lower(k_debug_pit_uses[channel])},
            {"clock_hz", rate},
            {"gate", counter.gate},
            {"programmed", counter.programmed}
        };

        if (counter.programmed)
        {
            u32 count = counter.bcd ? ((counter.reload >> 12) & 0x0F) * 1000 + ((counter.reload >> 8) & 0x0F) * 100 +
                ((counter.reload >> 4) & 0x0F) * 10 + (counter.reload & 0x0F) : counter.reload;

            if (count == 0)
                count = counter.bcd ? 10000 : 0x10000;

            item["mode"] = counter.mode;
            item["mode_name"] = to_lower(k_debug_pit_mode_names[counter.mode % 6]);
            item["access"] = to_lower(k_debug_pit_access_names[counter.access & 3]);
            item["bcd"] = counter.bcd;
            item["reload"] = Hex(counter.reload, 4);
            item["count"] = Hex(chip->PeekCount(channel, tick), 4);
            item["out"] = chip->PeekOutput(channel, tick);
            item["counting"] = counter.counting;
            item["null_count"] = tick < counter.null_until;

            if (counter.counting && (counter.mode == 2 || counter.mode == 3))
                item["frequency_hz"] = rate / count;
            else if (counter.counting && (counter.mode == 0 || counter.mode == 4))
                item["period_ms"] = (count * 1000.0) / rate;
        }

        counters.push_back(item);
    }

    bool out2 = pit->GetChannel2Output(clocks);

    return {
        {"speaker", {
            {"gate", state->speaker_gate},
            {"data", state->speaker_data},
            {"out2", out2},
            {"output", state->speaker_data && out2},
            {"refresh_toggle", pit->GetRefreshToggle(clocks)}
        }},
        {"counters", counters}
    };
}

static const char* video_mode_name(Video* video)
{
    if (video->IsCGA())
    {
        u8 mode = video->GetState()->cga_mode;

        if ((mode & 0x02) == 0)
            return (mode & 0x01) != 0 ? "text_80" : "text_40";

        return (mode & 0x10) != 0 ? "graphics_640x200" : "graphics_320x200";
    }

    if (video->IsTextMode())
        return "text";

    switch (video->GetShiftFormat())
    {
        case Video::VIDEO_FORMAT_PLANAR:
            return video->IsPacked() ? "planar_8bit" : "planar";
        case Video::VIDEO_FORMAT_CGA:
            return video->IsPacked() ? "cga_8bit" : "cga";
        default:
            return video->IsPacked() ? "256_colors" : "256_shift_4bit";
    }
}

json DebugAdapter::VideoRegisterArray(const u8* values, int count) const
{
    json result = json::array();

    for (int i = 0; i < count; i++)
        result.push_back(Hex(values[i], 2));

    return result;
}

json DebugAdapter::GetCRTCStatus()
{
    Video* video = m_core->GetVideo();
    Video::Video_State* state = video->GetState();
    u64 clocks = m_core->GetScheduler()->GetClocks();
    u8 status = video->GetSyncStatus(clocks);
    const u8* crtc = state->crtc;

    json result = {
        {"running", state->running},
        {"mode", video_mode_name(video)},
        {"block", (state->misc & 0x01) != 0 ? "color" : "mono"},
        {"blank", video->IsScreenBlank()},
        {"timing", {
            {"dot_clock_hz", state->frame_dot_clock},
            {"line_dots", state->frame_line_dots},
            {"display_dots", state->frame_display_dots},
            {"lines", state->frame_lines},
            {"display_lines", state->display_lines},
            {"retrace_start", state->retrace_start},
            {"retrace_end", state->retrace_end},
            {"frame_ms", video->GetFrameTime()},
            {"frame_count", state->frame_count}
        }},
        {"size", {
            {"frame_width", video->GetFrameWidth()},
            {"frame_height", video->GetFrameHeight()},
            {"display_width", video->GetDisplayWidth()},
            {"display_height", video->GetDisplayHeight()}
        }},
        {"beam", {
            {"line", video->GetBeamLine(clocks)},
            {"dot", video->GetBeamDot(clocks)},
            {"display_enable", (status & 0x01) == 0},
            {"vertical_retrace", (status & 0x08) != 0}
        }},
        {"scanout", {
            {"start_address", Hex(video->GetStartAddress(), 4)},
            {"latched_start", Hex(state->start_address, 4)},
            {"row_address", Hex(state->row_address & 0xFFFF, 4)},
            {"row_scan", state->row_scan},
            {"offset", crtc[k_video_cr_offset]},
            {"line_compare", video->GetLineCompare()},
            {"pixel_pan", state->attribute[k_video_ar_panning]},
            {"byte_pan", (crtc[k_video_cr_preset_row] >> 5) & 0x03}
        }},
        {"cursor", {
            {"address", Hex(video->GetCursorAddress(), 4)},
            {"start", crtc[k_video_cr_cursor_start] & 0x1F},
            {"end", crtc[k_video_cr_cursor_end] & 0x1F},
            {"disabled", (crtc[k_video_cr_cursor_start] & 0x20) != 0}
        }},
        {"vertical_interrupt", {
            {"pending", state->vertical_interrupt},
            {"enabled", (crtc[k_video_cr_retrace_end] & 0x20) == 0}
        }}
    };

    if (video->IsCGA())
        return GetCGACRTCStatus(result);

    return result;
}

json DebugAdapter::GetCGACRTCStatus(json result)
{
    Video* video = m_core->GetVideo();
    Video::Video_State* state = video->GetState();
    MC6845* mc6845 = video->GetMC6845();
    const u8* r = mc6845->GetState()->registers;

    result["block"] = "color";
    result["scanout"] = {
        {"start_address", Hex(mc6845->GetStartAddress(), 4)},
        {"latched_start", Hex(state->start_address, 4)},
        {"row_address", Hex(state->row_address & 0x3FFF, 4)},
        {"row_scan", state->row_scan},
        {"row_lines", mc6845->GetRowLines()}
    };
    result["cursor"] = {
        {"address", Hex(mc6845->GetCursorAddress(), 4)},
        {"start", r[k_mc6845_cursor_start] & 0x1F},
        {"end", r[k_mc6845_cursor_end]},
        {"disabled", !mc6845->IsCursorShown()}
    };
    result.erase("vertical_interrupt");
    return result;
}

json DebugAdapter::GetCRTCRegisters()
{
    Video* video = m_core->GetVideo();
    Video::Video_State* state = video->GetState();
    json registers = json::array();

    if (video->IsCGA())
    {
        const MC6845::MC6845_State* crtc = video->GetMC6845()->GetState();

        for (int i = 0; i < MC6845_REGISTER_COUNT; i++)
            registers.push_back({{"index", Hex(i, 2)}, {"name", k_debug_mc6845_register_names[i]},
                {"value", Hex(crtc->registers[i], 2)}});

        return {{"chip", "6845"}, {"index", Hex(crtc->index, 2)}, {"registers", registers}};
    }

    for (int i = 0; i < VIDEO_CRTC_COUNT; i++)
        registers.push_back({{"index", Hex(i, 2)}, {"name", k_debug_crtc_register_names[i]}, {"value", Hex(state->crtc[i], 2)}});

    return {
        {"index", Hex(state->crtc_index, 2)},
        {"protected", (state->crtc[k_video_cr_retrace_end] & 0x80) != 0},
        {"registers", registers}
    };
}

json DebugAdapter::WriteCRTCRegister(int reg, u8 value)
{
    Video* video = m_core->GetVideo();
    u64 clocks = m_core->GetScheduler()->GetClocks();

    if (video->IsCGA())
    {
        if (reg < 0 || reg >= MC6845_REGISTER_COUNT)
            return {{"error", "Register must be 0-17"}};

        MC6845* mc6845 = video->GetMC6845();
        u8 saved = mc6845->GetIndex();

        video->Write(0x3D4, (u8)reg, clocks);
        video->Write(0x3D5, value, clocks);
        video->Write(0x3D4, saved, clocks);

        return {
            {"success", true},
            {"register", Hex(reg, 2)},
            {"name", k_debug_mc6845_register_names[reg]},
            {"value", Hex(mc6845->GetRegister(reg), 2)}
        };
    }

    if (reg < 0 || reg >= VIDEO_CRTC_COUNT)
        return {{"error", "Register must be 0-24"}};

    Video::Video_State* state = video->GetState();
    u16 port = (state->misc & 0x01) != 0 ? 0x3D4 : 0x3B4;
    u8 saved = state->crtc_index;

    video->Write(port, (u8)reg, clocks);
    video->Write((u16)(port + 1), value, clocks);
    video->Write(port, saved, clocks);

    return {
        {"success", true},
        {"register", Hex(reg, 2)},
        {"name", k_debug_crtc_register_names[reg]},
        {"value", Hex(state->crtc[reg], 2)}
    };
}

json DebugAdapter::GetVideoOutputStatus()
{
    static const char* const k_windows[4] = { "A0000-BFFFF", "A0000-AFFFF", "B0000-B7FFF", "B8000-BFFFF" };

    Video* video = m_core->GetVideo();
    Video::Video_State* state = video->GetState();
    u8 graphics_mode = state->graphics[k_video_gr_mode];
    u8 memory_mode = state->sequencer[k_video_sr_memory_mode];
    u8 attribute_mode = state->attribute[k_video_ar_mode];
    json result = {
        {"card", {{"id", k_video_cards[video->GetCard()].id}, {"name", k_video_cards[video->GetCard()].name},
            {"vram_kb", video->GetVRAMSize() / 1024}}},
        {"misc", Hex(state->misc, 2)},
        {"memory", {
            {"enabled", (state->misc & 0x02) != 0},
            {"window", k_windows[(state->graphics[k_video_gr_misc] >> 2) & 0x03]},
            {"chain4", (memory_mode & 0x08) != 0},
            {"odd_even_write", (memory_mode & 0x04) == 0},
            {"odd_even_read", (graphics_mode & 0x10) != 0},
            {"map_mask", Hex(state->sequencer[k_video_sr_map_mask], 2)},
            {"direct_writes", video->IsDirectMapped()},
            {"latch", Hex(state->latch, 8)}
        }},
        {"pipeline", {
            {"write_mode", graphics_mode & 0x03},
            {"read_mode", (graphics_mode >> 3) & 0x01},
            {"rotate", state->graphics[k_video_gr_rotate] & 0x07},
            {"logic_op", (state->graphics[k_video_gr_rotate] >> 3) & 0x03},
            {"set_reset", Hex(state->graphics[k_video_gr_set_reset], 2)},
            {"enable_set_reset", Hex(state->graphics[k_video_gr_enable_set_reset], 2)},
            {"bit_mask", Hex(state->graphics[k_video_gr_bit_mask], 2)},
            {"read_map", state->graphics[k_video_gr_read_map] & 0x03},
            {"color_compare", Hex(state->graphics[k_video_gr_color_compare], 2)},
            {"color_dont_care", Hex(state->graphics[k_video_gr_color_dont_care], 2)}
        }},
        {"attribute", {
            {"index", Hex(state->attribute_index, 2)},
            {"flip_flop_data", state->attribute_data},
            {"palette_access", (state->attribute_index & 0x20) == 0},
            {"graphics", (attribute_mode & 0x01) != 0},
            {"mono", (attribute_mode & 0x02) != 0},
            {"line_graphics", (attribute_mode & 0x04) != 0},
            {"blink", (attribute_mode & 0x08) != 0},
            {"pan_reset_at_split", (attribute_mode & 0x20) != 0},
            {"packed", (attribute_mode & 0x40) != 0},
            {"plane_enable", Hex(state->attribute[k_video_ar_plane_enable], 2)},
            {"overscan", Hex(state->attribute[k_video_ar_overscan], 2)},
            {"color_select", Hex(state->attribute[k_video_ar_color_select], 2)}
        }},
        {"dac", {
            {"pel_mask", Hex(state->pel_mask, 2)},
            {"address", Hex(state->dac_address, 2)},
            {"index", Hex(state->dac_index, 2)},
            {"component", state->dac_component},
            {"state", state->dac_state}
        }}
    };

    if (video->IsEGA())
    {
        static const char* const k_mux[4] = { "red_blue", "secondary_blue_green", "secondary_red_secondary_green", "none" };

        result["ega"] = {
            {"monitor_lines", video->IsEGA350Lines() ? 350 : 200},
            {"monitor_colors", video->IsEGA350Lines() ? 64 : 16},
            {"switches", Hex(video->GetEGASwitches(), 1)},
            {"sensed_switch", 4 - ((state->misc >> 2) & 0x03)},
            {"status_mux", k_mux[(state->attribute[k_video_ar_plane_enable] >> 4) & 0x03]},
            {"graphics_position", {state->ega_position[0], state->ega_position[1]}},
            {"dot_clock_hz", video->GetCurrentDotClock()}
        };
    }

    if (video->IsCGA())
    {
        u8 mode = state->cga_mode;
        u8 color = state->cga_color;

        result.erase("memory");
        result.erase("pipeline");
        result.erase("attribute");
        result.erase("dac");
        result["cga"] = {
            {"mode", Hex(mode, 2)},
            {"color", Hex(color, 2)},
            {"columns_80", (mode & 0x01) != 0},
            {"graphics", (mode & 0x02) != 0},
            {"black_and_white", (mode & 0x04) != 0},
            {"video_enabled", (mode & 0x08) != 0},
            {"high_resolution", (mode & 0x10) != 0},
            {"blink", (mode & 0x20) != 0},
            {"color_index", color & 0x0F},
            {"intense_palette", (color & 0x10) != 0},
            {"palette", (color >> 5) & 0x01},
            {"output", video->IsCGAComposite() ? "composite" : "rgb"},
            {"output_setting", k_cga_outputs[video->GetCGAOutput()].id}
        };
    }

    if (video->IsCirrus())
    {
        static const char* const k_formats[8] = { "palette", "grey", "332", "555", "555_mixed", "565", "888", "blank" };

        json clocks = json::array();

        for (int i = 0; i < 4; i++)
            clocks.push_back(video->GetVideoClock(i));

        result["cirrus"] = {
            {"unlocked", video->IsUnlocked()},
            {"extended_mode", video->IsExtendedMode()},
            {"format", k_formats[video->GetExtendedFormat()]},
            {"bytes_per_pixel", video->GetExtendedBytesPerPixel()},
            {"hidden_dac", Hex(state->hidden_dac, 2)},
            {"pixel_path", (state->sequencer[k_video_sr_extended] >> 1) & 0x07},
            {"vclk_hz", clocks},
            {"vclk_selected", (state->misc >> 2) & 0x03},
            {"dot_clock_hz", video->GetCurrentDotClock()},
            {"bank_0", Hex(video->GetBankOffset(0), 6)},
            {"bank_1", Hex(video->GetBankOffset(1), 6)},
            {"dual_page", video->IsDualPage()},
            {"aperture_base", video->GetApertureBase() != 0 ? json(Hex32(video->GetApertureBase())) : json()},
            {"wide_counter", video->GetCounterMask() != 0xFFFF},
            {"offset", video->GetOffsetRegister()},
            {"pitch_bytes", video->GetOffsetRegister() * 8},
            {"start_address", Hex(video->GetStartAddress(), 5)},
            {"start_byte", Hex(video->GetStartAddress() * 4, 6)}
        };
    }

    return result;
}

json DebugAdapter::GetVGARegisters()
{
    Video* video = m_core->GetVideo();
    Video::Video_State* state = video->GetState();

    if (video->IsCGA())
    {
        const MC6845::MC6845_State* crtc = video->GetMC6845()->GetState();

        return {
            {"card", k_video_cards[video->GetCard()].id},
            {"crtc6845", {{"index", Hex(crtc->index, 2)}, {"registers", VideoRegisterArray(crtc->registers,
                MC6845_REGISTER_COUNT)}}},
            {"mode", Hex(state->cga_mode, 2)},
            {"color", Hex(state->cga_color, 2)}
        };
    }

    json result = {
        {"card", k_video_cards[video->GetCard()].id},
        {"misc", Hex(state->misc, 2)},
        {"feature", Hex(state->feature, 2)},
        {"sequencer", VideoRegisterArray(state->sequencer, VIDEO_SEQUENCER_COUNT)},
        {"graphics", VideoRegisterArray(state->graphics, VIDEO_GRAPHICS_COUNT)},
        {"crtc", VideoRegisterArray(state->crtc, VIDEO_CRTC_COUNT)},
        {"attribute", VideoRegisterArray(state->attribute, VIDEO_ATTRIBUTE_COUNT)}
    };

    if (video->IsEGA())
        result["ega"] = {
            {"graphics_position", {state->ega_position[0], state->ega_position[1]}},
            {"readable", "only CR0C-CR0F, the input status registers and the light pen read back on the card"}
        };

    if (video->IsCirrus())
    {
        result["cirrus"] = {
            {"unlocked", video->IsUnlocked()},
            {"sequencer", VideoRegisterArray(state->sequencer, VIDEO_SEQUENCER_EXT_COUNT)},
            {"graphics", VideoRegisterArray(state->graphics, VIDEO_GRAPHICS_EXT_COUNT)},
            {"crtc_first", 0x19},
            {"crtc", VideoRegisterArray(state->crtc + 0x19, 0x27 - 0x19 + 1)},
            {"hidden_dac", Hex(state->hidden_dac, 2)}
        };
    }

    return result;
}

json DebugAdapter::GetPalettes(const std::string& palette)
{
    Video* video = m_core->GetVideo();
    Video::Video_State* state = video->GetState();
    json result;
    char color[8];

    if (palette != "all" && palette != "attribute" && palette != "dac")
        return {{"error", "palette must be all, attribute or dac"}};

    if (video->IsCGA())
    {
        json entries = json::array();

        for (int i = 0; i < 16; i++)
        {
            snprintf(color, sizeof(color), "#%06X", video->GetCGAColor(i) & 0xFFFFFF);
            entries.push_back({{"index", i}, {"rgb", color}});
        }

        result["cga"] = entries;
        result["color"] = Hex(state->cga_color, 2);
        return result;
    }

    if (palette != "dac")
    {
        json entries = json::array();

        for (int i = 0; i < 16; i++)
        {
            snprintf(color, sizeof(color), "#%06X", video->GetAttributeColor(i) & 0xFFFFFF);
            entries.push_back({
                {"index", i},
                {"value", Hex(state->attribute[i], 2)},
                {"rgb", color}
            });
        }

        result["attribute"] = entries;
    }

    if (palette != "attribute")
    {
        json entries = json::array();

        for (int i = 0; video->IsEGA() && i < VIDEO_EGA_COLOR_COUNT; i++)
        {
            snprintf(color, sizeof(color), "#%06X", video->GetDACColor(i) & 0xFFFFFF);
            entries.push_back({{"index", i}, {"rgb", color}});
        }

        for (int i = 0; !video->IsEGA() && i < VIDEO_DAC_COUNT; i++)
        {
            snprintf(color, sizeof(color), "#%06X", video->GetDACColor(i) & 0xFFFFFF);
            entries.push_back({
                {"index", i},
                {"red", state->dac[i][0]},
                {"green", state->dac[i][1]},
                {"blue", state->dac[i][2]},
                {"rgb", color}
            });
        }

        if (video->IsEGA())
        {
            result["monitor"] = entries;
            result["monitor_lines"] = video->IsEGA350Lines() ? 350 : 200;
        }
        else
        {
            result["dac"] = entries;
            result["pel_mask"] = Hex(state->pel_mask, 2);
        }
    }

    return result;
}

json DebugAdapter::GetFrameBuffer(const std::string& buffer, int pitch, int format, int start)
{
    static const char* const k_buffers[Emu_Debug_Buffer_Count] = { "plane0", "plane1", "plane2", "plane3", "planar", "256",
        "text", "linear" };
    static const int k_depths[4] = { 8, 15, 16, 24 };

    if (emu_is_empty())
        return {{"error", "Emulator is powered off"}};

    int index = -1;

    for (int i = 0; i < Emu_Debug_Buffer_Count; i++)
    {
        if (buffer == k_buffers[i])
            index = i;
    }

    if (index < 0)
        return {{"error", "buffer must be plane0, plane1, plane2, plane3, planar, 256, text or linear"}};

    int max_pitch = index == Emu_Debug_Buffer_Linear ? 8192 : 512;

    if (pitch < 0 || pitch > max_pitch)
        return {{"error", index == Emu_Debug_Buffer_Linear ? "pitch must be 0-8192" : "pitch must be 0-512"}};

    int linear_format = Emu_Debug_Linear_Auto;

    if (format != 0)
    {
        for (int i = 0; i < 4; i++)
        {
            if (format == k_depths[i])
                linear_format = Emu_Debug_Linear_8 + i;
        }

        if (linear_format == Emu_Debug_Linear_Auto)
            return {{"error", "format must be 8, 15, 16 or 24"}};
    }

    if (start < -1 || start >= VIDEO_VRAM_SIZE)
        return {{"error", "start must be a byte offset inside the video memory"}};

    unsigned char* png = NULL;
    Emu_Debug_Buffer_Info info;
    int size = emu_get_debug_buffer_png(index, pitch, linear_format, start, &png, info);

    if (size <= 0 || !png)
        return {{"error", "Failed to decode video memory"}};

    std::string base64_png = base64_encode(png, size);
    free(png);

    json result;
    result["__mcp_image"] = true;
    result["data"] = base64_png;
    result["mimeType"] = "image/png";
    result["width"] = info.width;
    result["height"] = info.height;
    return result;
}

json DebugAdapter::GetTextScreen(bool attributes)
{
    Video* video = m_core->GetVideo();
    Video::Video_State* state = video->GetState();

    if (video->IsCGA())
        return GetCGATextScreen(attributes);

    if (!state->running || !video->IsTextMode())
        return {{"error", "The VGA is not in a text mode"}};

    const u8* crtc = state->crtc;
    int columns = crtc[k_video_cr_horizontal_display] + 1;
    int scans = ((crtc[k_video_cr_max_scan] & 0x1F) + 1) << ((crtc[k_video_cr_max_scan] >> 7) & 0x01);
    int rows = (int)state->display_lines / scans;
    int pitch = crtc[k_video_cr_offset] * 2;
    u32 start = video->GetStartAddress();
    u32 cursor = video->GetCursorAddress();
    json lines = json::array();
    json colors = json::array();

    for (int row = 0; row < rows; row++)
    {
        std::string line;
        std::string attribute_line;

        for (int column = 0; column < columns; column++)
        {
            u32 offset = video->GetCRTCOffset(start + (u32)(row * pitch + column), 0);
            u8 character = video->PeekPlane(0, offset);

            line += character >= 0x20 && character < 0x7F ? (char)character : (character == 0 ? ' ' : '.');

            if (attributes)
                attribute_line += Hex(video->PeekPlane(1, offset), 2);
        }

        line.erase(line.find_last_not_of(' ') + 1);
        lines.push_back(line);

        if (attributes)
            colors.push_back(attribute_line);
    }

    json result = {
        {"columns", columns},
        {"rows", rows},
        {"cursor", {
            {"disabled", (crtc[k_video_cr_cursor_start] & 0x20) != 0},
            {"column", pitch != 0 ? (int)((cursor - start) & 0xFFFF) % pitch : 0},
            {"row", pitch != 0 ? (int)((cursor - start) & 0xFFFF) / pitch : 0}
        }},
        {"lines", lines}
    };

    if (attributes)
        result["attributes"] = colors;

    return result;
}

json DebugAdapter::GetCGATextScreen(bool attributes)
{
    Video* video = m_core->GetVideo();
    Video::Video_State* state = video->GetState();
    MC6845* mc6845 = video->GetMC6845();

    if (!state->running || video->IsCGAGraphics())
        return {{"error", "The CGA is not in a text mode"}};

    const u8* vram = video->GetVRAM();
    int columns = (int)mc6845->GetHorizontalDisplayed();
    int rows = (int)(state->display_lines / mc6845->GetRowLines());
    u32 start = mc6845->GetStartAddress();
    u32 cursor = mc6845->GetCursorAddress();
    json lines = json::array();
    json colors = json::array();

    for (int row = 0; row < rows; row++)
    {
        std::string line;
        std::string attribute_line;

        for (int column = 0; column < columns; column++)
        {
            u32 offset = ((start + (u32)(row * columns + column)) * 2) & (VIDEO_CGA_VRAM_SIZE - 1);
            u8 character = vram[offset];

            line += character >= 0x20 && character < 0x7F ? (char)character : (character == 0 ? ' ' : '.');

            if (attributes)
                attribute_line += Hex(vram[offset + 1], 2);
        }

        line.erase(line.find_last_not_of(' ') + 1);
        lines.push_back(line);

        if (attributes)
            colors.push_back(attribute_line);
    }

    json result = {
        {"columns", columns},
        {"rows", rows},
        {"cursor", {
            {"disabled", !mc6845->IsCursorShown()},
            {"column", columns != 0 ? (int)((cursor - start) & 0x3FFF) % columns : 0},
            {"row", columns != 0 ? (int)((cursor - start) & 0x3FFF) / columns : 0}
        }},
        {"lines", lines}
    };

    if (attributes)
        result["attributes"] = colors;

    return result;
}

json DebugAdapter::GetFDCStatus()
{
    FDC* fdc = m_core->GetFDC();
    I82077* chip = fdc->GetController();
    I82077::I82077_State* state = chip->GetState();
    json results = json::array();
    json pcn = json::array();
    json sense = json::array();

    for (int i = 0; i < state->result_count; i++)
        results.push_back(Hex(state->results[i], 2));

    for (int i = 0; i < I82077_DRIVES; i++)
        pcn.push_back(state->pcn[i]);

    for (int i = 0; i < state->sense_count; i++)
        sense.push_back({{"st0", Hex(state->sense_st0[i], 2)}, {"pcn", state->sense_pcn[i]}});

    return {
        {"dor", Hex(state->dor, 2)},
        {"msr", Hex(chip->GetMSR(), 2)},
        {"dir", Hex(fdc->Peek(0x03F7), 2)},
        {"selected_drive", fdc->GetSelectedDrive()},
        {"rate", k_debug_fdc_rate_names[state->rate & 3]},
        {"phase", k_debug_fdc_phase_names[state->phase & 3]},
        {"command", k_debug_fdc_command_names[MIN(state->command, (u8)20)]},
        {"results", results},
        {"pcn", pcn},
        {"sense_queue", sense},
        {"st0", Hex(state->st0, 2)},
        {"st1", Hex(state->st1, 2)},
        {"st2", Hex(state->st2, 2)},
        {"interrupt", chip->IsInterruptOut()},
        {"dma_request", chip->IsRequestOut()},
        {"non_dma", state->non_dma},
        {"srt", state->srt},
        {"hut", state->hut},
        {"hlt", state->hlt},
        {"implied_seek", state->implied_seek},
        {"fifo_enabled", !state->fifo_disabled},
        {"fifo_threshold", state->fifo_threshold + 1},
        {"fifo_count", state->fifo_count},
        {"polling_disabled", state->poll_disabled},
        {"lock", state->lock},
        {"cylinder", state->cylinder},
        {"head", state->head},
        {"sector", state->sector}
    };
}

json DebugAdapter::GetIDEStatus()
{
    static const char* const status_names[8] = { "err", NULL, NULL, "drq", "dsc", NULL, "drdy", "bsy" };
    static const char* const control_names[8] = { NULL, "nien", "srst", NULL, NULL, NULL, NULL, NULL };

    IdeController* ide = m_core->GetIDE();
    IdeController::IdeController_State* state = ide->GetState();
    json channels = json::array();

    for (int i = 0; i < IDE_CHANNELS; i++)
    {
        json disks = json::array();
        json control_bits = json::array();

        for (int bit = 7; bit >= 0; bit--)
        {
            if (IsValidPointer(control_names[bit]) && IS_SET_BIT(state->control[i], bit))
                control_bits.push_back(control_names[bit]);
        }

        for (int j = 0; j < IDE_CHANNEL_DISKS; j++)
        {
            int id = (i * IDE_CHANNEL_DISKS) + j;
            IdeDisk* disk = ide->GetDisk(id);

            if (id == ide->GetCdRomSlot())
            {
                IdeCdRom* cdrom = ide->GetCdRom();
                IdeCdRom::IdeCdRom_State* c = cdrom->GetState();
                json cdrom_bits = json::array();

                for (int bit = 7; bit >= 0; bit--)
                {
                    if (IsValidPointer(status_names[bit]) && IS_SET_BIT(c->status, bit))
                        cdrom_bits.push_back(status_names[bit]);
                }

                disks.push_back({
                    {"id", id},
                    {"type", "cdrom"},
                    {"status", Hex(c->status, 2)},
                    {"status_bits", cdrom_bits},
                    {"error", Hex(c->error, 2)},
                    {"interrupt_reason", Hex(c->reason, 2)},
                    {"sector_number", Hex(c->sector_number, 2)},
                    {"byte_count", Hex((c->byte_count_high << 8) | c->byte_count_low, 4)},
                    {"device_head", Hex(c->device_head, 2)},
                    {"interrupt_pending", c->interrupt},
                    {"command", Hex(c->command, 2)},
                    {"command_name", GetCdRomAtaName(c->command)},
                    {"packet", Hex(c->packet[0], 2)},
                    {"packet_name", GetCdRomPacketName(c->packet[0])},
                    {"phase", k_debug_cdrom_phase_names[c->phase % IDE_CDROM_PHASE_COUNT]},
                    {"position", c->position},
                    {"length", c->length},
                    {"remaining", c->remaining}
                });
                continue;
            }

            if (!disk->IsAttached())
                continue;

            IdeDisk::IdeDisk_State* d = disk->GetState();
            json status_bits = json::array();

            for (int bit = 7; bit >= 0; bit--)
            {
                if (IsValidPointer(status_names[bit]) && IS_SET_BIT(d->status, bit))
                    status_bits.push_back(status_names[bit]);
            }

            disks.push_back({
                {"id", id},
                {"status", Hex(d->status, 2)},
                {"status_bits", status_bits},
                {"error", Hex(d->error, 2)},
                {"sector_count", Hex(d->sector_count, 2)},
                {"sector_number", Hex(d->sector_number, 2)},
                {"cylinder_low", Hex(d->cylinder_low, 2)},
                {"cylinder_high", Hex(d->cylinder_high, 2)},
                {"device_head", Hex(d->device_head, 2)},
                {"interrupt_pending", d->interrupt},
                {"command", Hex(d->command, 2)},
                {"command_name", gui_debug_ide_command_name(d->command)},
                {"phase", k_debug_ide_phase_names[d->phase % IDE_PHASE_COUNT]},
                {"lba", d->lba},
                {"remaining", d->remaining},
                {"position", d->position},
                {"length", d->length},
                {"current_geometry", {{"cylinders", d->current_cylinders}, {"heads", d->current_heads},
                    {"sectors", d->current_sectors}}},
                {"multiple", d->multiple},
                {"head_cylinder", d->head_cylinder},
                {"read_ahead_first", d->ahead_lba},
                {"read_ahead_end", d->ahead_end}
            });
        }

        channels.push_back({
            {"channel", i},
            {"irq", k_ide_irq[i]},
            {"irq_out", ide->IsInterruptOut(i)},
            {"control", Hex(state->control[i], 2)},
            {"control_bits", control_bits},
            {"features", Hex(state->features[i], 2)},
            {"selected", state->selected[i]},
            {"disks", disks}
        });
    }

    return {{"channels", channels}};
}

json DebugAdapter::GetDMAStatus()
{
    DMA* dma = m_core->GetDMA();
    json controllers = json::array();
    json channels = json::array();

    for (int i = 0; i < DMA_CHIPS; i++)
    {
        const I8237::I8237_State* chip = dma->GetController(i)->GetState();
        json command = json::array();

        for (int bit = 0; bit < 8; bit++)
        {
            if (IS_SET_BIT(chip->command, bit))
                command.push_back(k_debug_dma_command_names[bit]);
        }

        controllers.push_back({
            {"chip", i + 1},
            {"command", Hex(chip->command, 2)},
            {"command_bits", command},
            {"disabled", (chip->command & k_i8237_command_disable) != 0},
            {"status", Hex(dma->Peek(i == 0 ? 0x0008 : 0x00D0), 2)},
            {"mask", Hex(chip->mask & 0x0F, 1)},
            {"software_requests", Hex(chip->software_requests & 0x0F, 1)},
            {"request_levels", Hex(chip->request_levels & 0x0F, 1)},
            {"terminal_count", Hex(chip->status_tc & 0x0F, 1)},
            {"flip_flop", chip->flip_flop}
        });
    }

    for (int channel = 0; channel < DMA_CHANNELS; channel++)
    {
        const I8237::I8237_State* chip = dma->GetController(channel >> 2)->GetState();
        const I8237::I8237_Channel& item = chip->channels[channel & 0x03];
        u8 bit = (u8)(1 << (channel & 0x03));
        u8 mode = item.mode;

        channels.push_back({
            {"channel", channel},
            {"device", k_debug_dma_devices[channel]},
            {"mode", Hex(mode, 2)},
            {"direction", k_debug_dma_direction_names[(mode >> 2) & 3]},
            {"unit", channel >= 4 ? "word" : "byte"},
            {"service", to_lower(k_debug_dma_service_names[(mode >> 6) & 3])},
            {"auto_initialize", (mode & 0x10) != 0},
            {"decrement", (mode & 0x20) != 0},
            {"page", Hex(dma->GetState()->pages[k_dma_channel_pages[channel]], 2)},
            {"physical_address", Hex32(dma->GetPhysicalAddress(channel))},
            {"current_address", Hex(item.current_address, 4)},
            {"base_address", Hex(item.base_address, 4)},
            {"current_count", Hex(item.current_count, 4)},
            {"base_count", Hex(item.base_count, 4)},
            {"masked", (chip->mask & bit) != 0},
            {"request", ((chip->request_levels | chip->software_requests) & bit) != 0},
            {"terminal_count", (chip->status_tc & bit) != 0}
        });
    }

    return {
        {"controllers", controllers},
        {"active_channel", dma->GetState()->active_channel},
        {"stalled", Hex(dma->GetState()->stalled, 2)},
        {"channels", channels}
    };
}

json DebugAdapter::GetRTCStatus()
{
    u64 clocks = m_core->GetScheduler()->GetClocks();
    MC146818 rtc(*m_core->GetRTC());
    rtc.Synchronize(clocks);
    MC146818::MC146818_State* state = rtc.GetState();
    const u8* ram = state->ram;
    u8 status_b = ram[k_mc146818_status_b];
    u8 status_c = rtc.GetStatusC();
    u32 period = rtc.GetPeriodTicks();
    char date[16];
    char time[16];
    json registers = json::array();

    snprintf(date, sizeof(date), "%02d%02d-%02d-%02d", state->century, state->year, state->month, state->day);
    snprintf(time, sizeof(time), "%02d:%02d:%02d", state->hours, state->minutes, state->seconds);

    for (int i = 0; i <= k_mc146818_status_d; i++)
    {
        registers.push_back({{"index", Hex(i, 2)}, {"name", k_debug_rtc_register_names[i]},
            {"value", Hex(i == k_mc146818_status_c ? status_c : ram[i], 2)}});
    }

    return {
        {"date", date},
        {"time", time},
        {"weekday", k_debug_weekday_names[(state->weekday + 6) % 7]},
        {"hour_24", (status_b & k_mc146818_b_24_hour) != 0},
        {"binary", (status_b & k_mc146818_b_binary) != 0},
        {"set", (status_b & k_mc146818_b_set) != 0},
        {"divider_running", rtc.IsDividerRunning()},
        {"update_in_progress", rtc.IsUpdating(clocks)},
        {"periodic_hz", period != 0 ? (double)k_mc146818_chain_rate / period : 0.0},
        {"alarm", {
            {"hours", Hex(ram[k_mc146818_hours_alarm], 2)},
            {"minutes", Hex(ram[k_mc146818_minutes_alarm], 2)},
            {"seconds", Hex(ram[k_mc146818_seconds_alarm], 2)}
        }},
        {"interrupts", {
            {"periodic_enabled", (status_b & k_mc146818_b_periodic) != 0},
            {"alarm_enabled", (status_b & k_mc146818_b_alarm) != 0},
            {"update_enabled", (status_b & k_mc146818_b_update) != 0},
            {"periodic_flag", (status_c & k_mc146818_c_periodic) != 0},
            {"alarm_flag", (status_c & k_mc146818_c_alarm) != 0},
            {"update_flag", (status_c & k_mc146818_c_update) != 0},
            {"irq", (status_c & k_mc146818_c_irq) != 0}
        }},
        {"index", Hex(state->index, 2)},
        {"registers", registers}
    };
}

json DebugAdapter::GetKeyboardStatus()
{
    I8042* controller = m_core->GetI8042();
    I8042::I8042_State* state = controller->GetState();
    Keyboard* keyboard = m_core->GetKeyboard();
    Keyboard::Keyboard_State* keys = keyboard->GetState();
    SystemControl* system_control = m_core->GetSystemControl();
    json buffer = json::array();
    json held = json::array();

    for (int i = 0; i < keys->buffer_count; i++)
        buffer.push_back(Hex(keys->buffer[(keys->buffer_read + i) & (KEYBOARD_BUFFER_CAPACITY - 1)], 2));

    for (int i = GDOX_KEY_NONE + 1; i < GDOX_KEY_COUNT; i++)
    {
        if (keys->keys[i] != 0)
            held.push_back(gui_debug_key_name(i));
    }

    return {
        {"controller", {
            {"status", Hex(controller->GetStatus(), 2)},
            {"command_byte", Hex(state->ram[0], 2)},
            {"input_port", Hex(k_i8042_input_port, 2)},
            {"output_port", Hex(controller->GetOutputPort(), 2)},
            {"output_buffer", Hex(state->output_buffer, 2)},
            {"output_full", state->output_full},
            {"output_source", state->output_aux ? "aux" : "keyboard"},
            {"input_full", state->input_full},
            {"pending_command", state->pending_command != 0 ? json(Hex(state->pending_command, 2)) : json()},
            {"translate", (state->ram[0] & k_i8042_translate_enabled) != 0},
            {"keyboard_enabled", (state->ram[0] & k_i8042_keyboard_disabled) == 0},
            {"aux_enabled", (state->ram[0] & k_i8042_aux_disabled) == 0},
            {"timeout", state->timeout}
        }},
        {"a20", {
            {"enabled", system_control->IsA20Enabled()},
            {"keyboard_controller", system_control->IsKeyboardA20Enabled()},
            {"port_92", (system_control->GetState()->port_92 & 0x02) != 0},
            {"held_open_by_reset", system_control->GetState()->reset_a20}
        }},
        {"keyboard", {
            {"scanning", keys->scanning},
            {"self_test", keys->self_test_clocks != GDOX_NO_EVENT},
            {"scan_set", keys->scan_set},
            {"leds", {{"scroll", (keys->leds & 0x01) != 0}, {"num", (keys->leds & 0x02) != 0},
                {"caps", (keys->leds & 0x04) != 0}}},
            {"typematic_delay_ms", keyboard->GetTypematicDelay()},
            {"typematic_period_us", keyboard->GetTypematicPeriod()},
            {"repeat_key", keys->repeat_key != GDOX_KEY_NONE ? json(gui_debug_key_name(keys->repeat_key)) : json()},
            {"buffer", buffer},
            {"overrun", keys->overrun},
            {"held_keys", held}
        }}
    };
}

json DebugAdapter::GetSerialStatus()
{
    static const int k_rx_trigger_levels[] = { 1, 4, 8, 14 };
    SerialPorts* ports = m_core->GetSerialPorts();
    json list = json::array();

    for (int i = 0; i < SERIAL_PORTS; i++)
    {
        UART* uart = ports->GetUART(i);
        UART::UART_State* state = uart->GetState();
        u32 divisor = uart->GetDivisor();
        u8 lcr = state->lcr;
        u8 mcr = state->mcr;
        u8 lsr = uart->GetLineStatus();
        u8 msr = state->msr;
        u8 data_bits = (u8)((lcr & 0x03) + 5);
        const char* stop_bits = (lcr & 0x04) == 0 ? "1" : (data_bits == 5 ? "1.5" : "2");
        const char* parity = (lcr & 0x08) == 0 ? "none" : ((lcr & 0x20) != 0 ?
            ((lcr & 0x10) != 0 ? "space" : "mark") : ((lcr & 0x10) != 0 ? "even" : "odd"));
        const char* attached = (i == 0) && ports->IsMouseAttached() ? "serial mouse" : "none";
        char name[8];
        snprintf(name, sizeof(name), "COM%d", i + 1);

        list.push_back({
            {"port", name},
            {"base", Hex(k_serial_ports_base[i], 4)},
            {"irq", k_serial_ports_irq[i]},
            {"irq_line", ports->GetIRQ(i)},
            {"attached", attached},
            {"dll", Hex(state->dll, 2)},
            {"dlm", Hex(state->dlm, 2)},
            {"divisor", divisor},
            {"baud", divisor != 0 ? json(k_uart_clock / (16 * divisor)) : json()},
            {"ier", Hex(state->ier, 2)},
            {"iir", Hex(uart->GetInterruptIdentification(), 2)},
            {"fcr", Hex(state->fcr, 2)},
            {"lcr", Hex(lcr, 2)},
            {"mcr", Hex(mcr, 2)},
            {"lsr", Hex(lsr, 2)},
            {"msr", Hex(msr, 2)},
            {"scr", Hex(state->scr, 2)},
            {"frame", {{"data_bits", data_bits}, {"parity", parity}, {"stop_bits", stop_bits},
                {"dlab", (lcr & 0x80) != 0}, {"break", (lcr & 0x40) != 0}}},
            {"interrupt_enable", {{"received_data", (state->ier & 0x01) != 0}, {"thr_empty", (state->ier & 0x02) != 0},
                {"line_status", (state->ier & 0x04) != 0}, {"modem_status", (state->ier & 0x08) != 0}}},
            {"modem_control", {{"dtr", (mcr & k_uart_dtr) != 0}, {"rts", (mcr & k_uart_rts) != 0},
                {"out1", (mcr & k_uart_out1) != 0}, {"out2", (mcr & k_uart_out2) != 0},
                {"loopback", (mcr & k_uart_loopback) != 0}}},
            {"line_status", {{"data_ready", (lsr & 0x01) != 0}, {"overrun", (lsr & 0x02) != 0},
                {"parity_error", (lsr & 0x04) != 0}, {"framing_error", (lsr & 0x08) != 0},
                {"break", (lsr & 0x10) != 0}, {"thr_empty", (lsr & 0x20) != 0},
                {"transmitter_empty", (lsr & 0x40) != 0}, {"fifo_error", (lsr & 0x80) != 0}}},
            {"modem_status", {{"dcts", (msr & 0x01) != 0}, {"ddsr", (msr & 0x02) != 0}, {"teri", (msr & 0x04) != 0},
                {"ddcd", (msr & 0x08) != 0}, {"cts", (msr & 0x10) != 0}, {"dsr", (msr & 0x20) != 0},
                {"ri", (msr & 0x40) != 0}, {"dcd", (msr & 0x80) != 0}}},
            {"fifo", {{"enabled", (state->fcr & 0x01) != 0}, {"rx_count", state->rx_count},
                {"tx_count", state->tx_count}, {"rx_trigger", k_rx_trigger_levels[state->fcr >> 6]},
                {"rbr", Hex(state->rbr, 2)}, {"shift_register_full", state->tsr_full}}},
            {"pending", {{"thr_empty", state->thre_pending}, {"timeout", state->timeout_pending}}}
        });
    }

    return {{"ports", list}};
}

json DebugAdapter::GetParallelStatus()
{
    ParallelPort* port = m_core->GetParallelPort();
    u8 data = port->Read(k_parallel_port_base);
    u8 status = port->Read(k_parallel_port_base + 1);
    u8 control = port->Read(k_parallel_port_base + 2);

    return {
        {"port", "LPT1"},
        {"base", Hex(k_parallel_port_base, 4)},
        {"irq", 7},
        {"attached", "none"},
        {"data", Hex(data, 2)},
        {"status", Hex(status, 2)},
        {"control", Hex(control, 2)},
        {"status_bits", {{"error_n", (status & 0x08) != 0}, {"select", (status & 0x10) != 0},
            {"paper_end", (status & 0x20) != 0}, {"ack_n", (status & 0x40) != 0}, {"busy_n", (status & 0x80) != 0}}},
        {"control_bits", {{"strobe", (control & 0x01) != 0}, {"autofeed", (control & 0x02) != 0},
            {"init_n", (control & 0x04) != 0}, {"select_in", (control & 0x08) != 0},
            {"irq_enable", (control & 0x10) != 0}}}
    };
}

json DebugAdapter::GetSystemStatus()
{
    const GDOX_Machine_Config& machine = m_core->GetMachineConfig();
    const GDOX_Machine_Report& report = m_core->GetMachineReport();
    const GDOX_Machine_Settings& settings = m_core->GetMachineSettings();
    const GDOX_Machine_Profile& profile = k_machine_profiles[machine.model];
    json model_setting = json(k_machine_profiles[settings.model < 0 ? k_machine_default_model : settings.model].key);
    json cpu_setting = settings.cpu_mhz <= 0 ? json("model_default") : json(settings.cpu_mhz);
    json ram_setting = json(settings.ram_mb <= 0 ? k_machine_default_ram_mb : settings.ram_mb);
    json fpu_setting = settings.fpu == 0 ? json("none") : json("80387");
    json video_setting = settings.video_card < 0 ? json("model_default") : json(k_video_cards[settings.video_card].id);
    json warnings = json::array();
    SystemControl* system_control = m_core->GetSystemControl();
    SystemControl::SystemControl_State* board = system_control->GetState();
    Firmware* firmware = m_core->GetFirmware();
    const GDOX_Firmware_Info& bios = firmware->GetInfo(GDOX_FIRMWARE_SYSTEM_BIOS);
    const GDOX_Firmware_Info& vga = firmware->GetInfo(GDOX_FIRMWARE_VGA_BIOS);
    u64 clocks = m_core->GetScheduler()->GetClocks();

    for (int i = 0; i < report.warning_count; i++)
        warnings.push_back(report.warnings[i]);

    return {
        {"machine", {
            {"model", profile.name},
            {"key", profile.key},
            {"cpu", k_machine_cpus[machine.cpu].name},
            {"cpu_clock_hz", machine.cpu_clock_rate},
            {"turbo", m_core->IsTurbo()},
            {"cpu_clock_effective_hz", m_core->GetEffectiveCPUClockRate()},
            {"cpu_cache_serving", m_core->GetI386()->IsBusCached()},
            {"ram_kb", machine.ram_size / 1024},
            {"fpu", machine.fpu ? json(get_machine_fpu_name(machine.cpu)) : json()},
            {"video", {{"card", k_video_cards[machine.video_card].name}, {"id", k_video_cards[machine.video_card].id},
                {"vram_kb", m_core->GetVideo()->GetVRAMSize() / 1024}}},
            {"floppy_drives", machine.floppy_drives},
            {"settings", {{"model", model_setting}, {"cpu_speed_mhz", cpu_setting}, {"ram_mb", ram_setting},
                {"fpu", fpu_setting}, {"video", video_setting}}},
            {"source", {{"model", k_mcp_machine_sources[report.model]},
                {"cpu_clock", k_mcp_machine_sources[report.cpu_clock]}, {"ram", k_mcp_machine_sources[report.ram]},
                {"fpu", k_mcp_machine_sources[report.fpu]}}},
            {"game", IsValidPointer(report.title) ? json(report.title) : json()},
            {"warnings", warnings},
            {"reset_pending", m_core->IsMachineConfigPending()}
        }},
        {"board", {
            {"a20", system_control->IsA20Enabled()},
            {"port_92", Hex(board->port_92, 2)},
            {"port_61", Hex(system_control->Peek(0x0061, clocks), 2)},
            {"nmi_masked", board->nmi_mask},
            {"post_code", Hex(board->post_code, 2)},
            {"fpu_error_irq13", board->fpu_error},
            {"cpu_resets", board->reset_count},
            {"last_reset_cause", board->reset_count == 0 ? json() :
                json((board->last_reset_cause & k_system_control_reset_shutdown) != 0 ? "shutdown" :
                (board->last_reset_cause & k_system_control_reset_keyboard) != 0 ? "keyboard_controller" : "port_92")},
            {"memory_map", k_machine_memory_maps[profile.map].name}
        }},
        {"bios", {
            {"system_bios", bios.name},
            {"system_bios_crc", Hex32(bios.crc)},
            {"system_bios_kb", bios.size / 1024},
            {"vga_bios", vga.name},
            {"vga_bios_crc", Hex32(vga.crc)},
            {"firmware_config", m_core->GetFirmwareConfig()->IsEnabled()},
            {"debug_console", m_core->GetDebugConsole()->IsEnabled()}
        }}
    };
}

json DebugAdapter::GetBiosLog(int max_bytes)
{
    DebugConsole* console = m_core->GetDebugConsole();
    std::vector<char> text(DEBUG_CONSOLE_SIZE + 1);
    u32 length = console->CopyText(&text[0], (u32)text.size());
    u32 count = (u32)CLAMP(max_bytes, 1, DEBUG_CONSOLE_SIZE);
    u32 start = length > count ? length - count : 0;

    return {
        {"enabled", console->IsEnabled()},
        {"port", Hex(k_debug_console_port, 4)},
        {"bytes", length},
        {"written", console->GetWrittenCount()},
        {"text", std::string(&text[start], length - start)}
    };
}

json DebugAdapter::GetSoundStatus()
{
    Audio* audio = m_core->GetAudio();
    const GDOX_Machine_Config& machine = m_core->GetMachineConfig();
    SoundBlaster* blaster = audio->GetSoundBlaster();
    SoundBlaster::SoundBlaster_State* dsp = blaster->GetState();
    SoundBlaster::SoundBlaster_Transfer& transfer = dsp->transfer;
    MPU401::MPU401_State* mpu = audio->GetMPU401()->GetState();
    static const char* const formats[6] = { "pcm8", "pcm16", "adpcm2", "adpcm3", "adpcm4", "silence" };
    json chips = json::array();

    for (int chip = 0; chip < audio->GetOPLChips(); chip++)
    {
        OPL* opl = audio->GetOPL(chip);
        OPL::OPL_State* state = opl->GetState();
        bool opl3 = opl->GetModel() == OPL::OPL_MODEL_OPL3;
        int count = opl3 ? OPL_CHANNEL_COUNT : OPL2_CHANNEL_COUNT;
        json channels = json::array();

        for (int i = 0; i < count; i++)
        {
            OPL::OPL_Channel& channel = state->channels[i];
            OPL::OPL_Operator& carrier = state->operators[channel.operators[1]];

            channels.push_back({{"channel", i + 1}, {"f_number", channel.f_number}, {"block", channel.block},
                {"key", carrier.key != 0}, {"connection", channel.connection}, {"feedback", channel.feedback},
                {"type", channel.type}, {"envelope", carrier.envelope}, {"output", opl->GetChannelOutput(i)}});
        }

        chips.push_back({{"model", opl3 ? "YMF262" : "YM3812"}, {"status", Hex(opl->Peek(0), 2)},
            {"new_mode", state->new_mode != 0}, {"rhythm", Hex(state->rhythm, 2)},
            {"timer_1", {{"reload", state->timer_reload[0]}, {"running", state->timer_start[0] != 0},
                {"masked", state->timer_mask[0] != 0}, {"flag", state->timer_flag[0] != 0}}},
            {"timer_2", {{"reload", state->timer_reload[1]}, {"running", state->timer_start[1] != 0},
                {"masked", state->timer_mask[1] != 0}, {"flag", state->timer_flag[1] != 0}}},
            {"channels", channels}});
    }

    json result = {{"card", k_sound_cards[machine.sound_card].name}, {"opl", chips}};

    if (blaster->HasDSP())
    {
        result["dsp"] = {
            {"version", std::to_string(blaster->GetVersionMajor()) + "." +
                (blaster->GetVersionMinor() < 10 ? "0" : "") + std::to_string(blaster->GetVersionMinor())},
            {"irq", dsp->irq_line}, {"dma_8", dsp->dma_8}, {"dma_16", dsp->dma_16},
            {"speaker", dsp->speaker}, {"rate_hz", blaster->GetSampleRate()},
            {"irq_status", Hex(blaster->GetInterruptStatus(), 2)}, {"filter_hz", blaster->GetFilterCutoff()},
            {"midi_uart", dsp->midi_uart}, {"last_command", Hex(dsp->command, 2)},
            {"transfer", {{"active", transfer.active}, {"paused", transfer.paused},
                {"format", formats[MIN(transfer.format, (u8)5)]}, {"auto_init", transfer.auto_init},
                {"input", transfer.input}, {"high_speed", transfer.high_speed}, {"stereo", blaster->IsStereo()},
                {"signed", transfer.is_signed}, {"block", transfer.length}, {"remaining", transfer.remaining}}}};
    }

    if (blaster->HasMixer())
        result["mixer"] = {
            {"voice", {blaster->GetGain(SoundBlaster::SOUND_BLASTER_SOURCE_VOICE, false) / 32768.0,
                blaster->GetGain(SoundBlaster::SOUND_BLASTER_SOURCE_VOICE, true) / 32768.0}},
            {"fm", {blaster->GetGain(SoundBlaster::SOUND_BLASTER_SOURCE_FM, false) / 32768.0,
                blaster->GetGain(SoundBlaster::SOUND_BLASTER_SOURCE_FM, true) / 32768.0}},
            {"cd", {blaster->GetGain(SoundBlaster::SOUND_BLASTER_SOURCE_CD, false) / 32768.0,
                blaster->GetGain(SoundBlaster::SOUND_BLASTER_SOURCE_CD, true) / 32768.0}}};

    if (audio->HasMPU())
        result["mpu401"] = {{"uart", mpu->uart}, {"pending", mpu->count},
            {"standalone", audio->IsMPUStandalone()}, {"irq", audio->IsMPUStandalone() ? 9 : machine.sound_irq}};

    MidiOut* midi = audio->GetMidiOut();
    const MidiOut::MidiOut_Stats* stats = midi->GetStats();
    const MidiOut::MidiOut_Synth& synth = midi->GetState()->machine;
    static const char* const k_captures[3] = { "off", "on", "silent" };
    result["midi"] = {{"capture", k_captures[midi->GetCapture()]}, {"messages", stats->messages}, {"bytes", stats->bytes},
        {"dropped", stats->dropped + midi_queue_get_dropped()}, {"sysex_log", synth.log_size},
        {"sysex_log_full", synth.log_full}, {"synth", k_midi_synths[emu_midi_get_synth()].id},
        {"port", emu_midi_get_port()}, {"wanted_port", emu_midi_get_wanted_port()}};

    return result;
}

static const char* const k_mcp_opl_channel_types[4] = { "2op", "4op", "4op_pair", "drum" };
static const char* const k_mcp_opl_envelope_states[4] = { "attack", "decay", "sustain", "release" };
static const double k_mcp_opl_timer_tick_us[OPL_TIMERS] = { 4.0 * 1000000.0 / k_opl_native_sample_rate,
    16.0 * 1000000.0 / k_opl_native_sample_rate };

static bool opl_register_name(bool opl3, int bank, u8 address, char* name, size_t size)
{
    static const char* const k_operator_names[5] = { "AM / VIB / EG-TYP / KSR / MULT", "KSL / TL", "AR / DR", "SL / RR",
        "WAVE SELECT" };

    if (((address >= 0x20) && (address < 0xA0)) || ((address >= 0xE0) && (address < 0xF6)))
    {
        int offset = address & 0x1F;
        int position = offset & 0x07;

        if ((offset > 0x15) || (position > 5))
            return false;

        int group = address >= 0xE0 ? 4 : (address >> 5) - 1;
        int channel = (offset >> 3) * 3 + (position % 3) + (bank * 9) + 1;
        snprintf(name, size, "%s  CH%d OP%d", k_operator_names[group], channel, (position / 3) + 1);
        return true;
    }

    if ((address >= 0xA0) && (address < 0xC9) && ((address & 0x0F) < 9))
    {
        int channel = (address & 0x0F) + (bank * 9) + 1;
        const char* text = address < 0xB0 ? "F-NUMBER LOW" : address < 0xC0 ? "KEY ON / BLOCK / F-NUMBER HIGH" :
            opl3 ? "OUTPUT / FEEDBACK / CONNECTION" : "FEEDBACK / CONNECTION";
        snprintf(name, size, "%s  CH%d", text, channel);
        return true;
    }

    const char* text = NULL;

    if (bank == 0)
    {
        switch (address)
        {
            case 0x01: text = opl3 ? "LSI TEST" : "TEST / WAVE SELECT ENABLE"; break;
            case 0x02: text = "TIMER 1"; break;
            case 0x03: text = "TIMER 2"; break;
            case 0x04: text = "TIMER CONTROL"; break;
            case 0x08: text = opl3 ? "NOTE SELECT" : "CSM / NOTE SELECT"; break;
            case 0xBD: text = "AM DEPTH / VIB DEPTH / RHYTHM"; break;
            default: break;
        }
    }
    else
    {
        switch (address)
        {
            case 0x01: text = "LSI TEST"; break;
            case 0x04: text = "4-OP CONNECTION SELECT"; break;
            case 0x05: text = "NEW"; break;
            default: break;
        }
    }

    if (!IsValidPointer(text))
        return false;

    snprintf(name, size, "%s", text);
    return true;
}

static json opl_operator_json(OPL::OPL_State* state, int index)
{
    OPL::OPL_Operator& op = state->operators[index];
    u32 attenuation = MIN((u32)op.envelope_output, (u32)k_opl_envelope_silent);

    return {
        {"slot", index + 1},
        {"am", op.tremolo != 0},
        {"vib", op.vibrato != 0},
        {"eg_type", op.sustain != 0},
        {"ksr", op.key_scale_rate != 0},
        {"mult", op.multiple},
        {"ksl", op.key_scale_level},
        {"tl", op.total_level},
        {"ar", op.attack_rate},
        {"dr", op.decay_rate},
        {"sl", op.sustain_level & 0x0F},
        {"rr", op.release_rate},
        {"waveform", op.waveform},
        {"key", op.key != 0},
        {"envelope_state", k_mcp_opl_envelope_states[op.envelope_state & 0x03]},
        {"envelope", hex_text(op.envelope, 3)},
        {"level_percent", (int)((k_opl_envelope_silent - attenuation) * 100 / k_opl_envelope_silent)}
    };
}

static json opl_channel_json(OPL* opl, int index)
{
    OPL::OPL_State* state = opl->GetState();
    OPL::OPL_Channel& channel = state->channels[index];
    json operators = json::array();

    for (int i = 0; i < 2; i++)
        operators.push_back(opl_operator_json(state, channel.operators[i]));

    json result = {
        {"channel", index + 1},
        {"muted", opl->IsChannelMuted(index)},
        {"type", k_mcp_opl_channel_types[channel.type & 0x03]},
        {"f_number", channel.f_number},
        {"block", channel.block},
        {"note_hz", channel.f_number * (double)k_opl_native_sample_rate * pow(2.0, (double)channel.block - 20.0)},
        {"key", state->operators[channel.operators[1]].key != 0},
        {"connection", channel.connection},
        {"feedback", channel.feedback},
        {"output", opl->GetChannelOutput(index)},
        {"operators", operators}
    };

    if (opl->GetModel() == OPL::OPL_MODEL_OPL3)
    {
        result["pan_left"] = channel.pan_left != 0;
        result["pan_right"] = channel.pan_right != 0;
    }

    if ((channel.type == OPL::OPL_CHANNEL_4OP) || (channel.type == OPL::OPL_CHANNEL_4OP_PAIR))
        result["pair_channel"] = channel.pair + 1;

    return result;
}

json DebugAdapter::GetOPLStatus(int chip, int channel)
{
    Audio* audio = m_core->GetAudio();
    int chips = audio->GetOPLChips();

    if (chips == 0)
        return {{"error", "The sound card has no FM chip"}};

    if ((chip >= chips) || (chip < -1))
        return {{"error", chips > 1 ? "chip must be 0 or 1" : "chip must be 0, the card has one FM chip"}};

    json result_chips = json::array();

    for (int i = 0; i < chips; i++)
    {
        if ((chip >= 0) && (chip != i))
            continue;

        OPL* opl = audio->GetOPL(i);
        OPL::OPL_State* state = opl->GetState();
        bool opl3 = opl->GetModel() == OPL::OPL_MODEL_OPL3;
        int count = opl3 ? OPL_CHANNEL_COUNT : OPL2_CHANNEL_COUNT;

        if (channel > count)
            return {{"error", "channel must be 1-" + std::to_string(count)}};

        u8 rhythm = opl->GetRegister(0xBD);
        json timers = json::array();
        json channels = json::array();

        for (int t = 0; t < OPL_TIMERS; t++)
        {
            timers.push_back({{"timer", t + 1}, {"reload", Hex(state->timer_reload[t], 2)},
                {"period_us", (256 - state->timer_reload[t]) * k_mcp_opl_timer_tick_us[t]},
                {"running", state->timer_start[t] != 0}, {"masked", state->timer_mask[t] != 0},
                {"flag", state->timer_flag[t] != 0}});
        }

        for (int c = 0; c < count; c++)
        {
            if ((channel == 0) || (channel == c + 1))
                channels.push_back(opl_channel_json(opl, c));
        }

        json item = {
            {"chip", i},
            {"model", opl3 ? "YMF262" : "YM3812"},
            {"status", Hex(opl->Peek(0), 2)},
            {"address_latch", Hex(opl->GetSelectedAddress(), 3)},
            {"rhythm", {{"enabled", (rhythm & 0x20) != 0}, {"am_depth", (rhythm & 0x80) != 0},
                {"vib_depth", (rhythm & 0x40) != 0}, {"bd", (rhythm & 0x10) != 0}, {"sd", (rhythm & 0x08) != 0},
                {"tom", (rhythm & 0x04) != 0}, {"tc", (rhythm & 0x02) != 0}, {"hh", (rhythm & 0x01) != 0}}},
            {"note_select", state->note_select != 0},
            {"timers", timers},
            {"channels", channels}
        };

        if (opl3)
            item["new_mode"] = state->new_mode != 0;
        else
        {
            item["waveform_enable"] = state->waveform_enable != 0;
            item["csm"] = state->csm_enable != 0;
        }

        result_chips.push_back(item);
    }

    return {{"card", k_sound_cards[m_core->GetMachineConfig().sound_card].name},
        {"fm_muted", audio->IsSourceMuted(Audio::AUDIO_SOURCE_FM)},
        {"native_sample_rate_hz", k_opl_native_sample_rate}, {"chips", result_chips}};
}

json DebugAdapter::GetOPLRegisters(int chip, int bank)
{
    Audio* audio = m_core->GetAudio();
    int chips = audio->GetOPLChips();

    if (chips == 0)
        return {{"error", "The sound card has no FM chip"}};

    if ((chip < 0) || (chip >= chips))
        return {{"error", chips > 1 ? "chip must be 0 or 1" : "chip must be 0, the card has one FM chip"}};

    OPL* opl = audio->GetOPL(chip);
    bool opl3 = opl->GetModel() == OPL::OPL_MODEL_OPL3;

    if ((bank < 0) || (bank > 1) || ((bank == 1) && !opl3))
        return {{"error", opl3 ? "bank must be 0 or 1" : "bank must be 0, bank 1 is the OPL3's"}};

    json registers = json::array();

    for (int address = 0x01; address < 0x100; address++)
    {
        char name[64];

        if (!opl_register_name(opl3, bank, (u8)address, name, sizeof(name)))
            continue;

        u16 full = (u16)((bank << 8) | address);
        registers.push_back({{"address", Hex(full, 3)}, {"name", name}, {"value", Hex(opl->GetRegister(full), 2)}});
    }

    return {{"chip", chip}, {"model", opl3 ? "YMF262" : "YM3812"}, {"bank", bank},
        {"address_latch", Hex(opl->GetSelectedAddress(), 3)}, {"registers", registers}};
}

json DebugAdapter::SetTurbo(bool turbo)
{
    emu_set_turbo(turbo);
    return {{"success", true}, {"turbo", m_core->IsTurbo()}, {"cpu_clock_effective_hz", m_core->GetEffectiveCPUClockRate()}};
}

json DebugAdapter::SetAudioMute(const std::string& source, int channel, bool mute)
{
    Audio* audio = m_core->GetAudio();

    if ((source != "fm") && (source != "pcm") && (source != "cdda") && (source != "buzzer"))
        return {{"error", "source must be fm, pcm, cdda or buzzer"}};

    if ((source != "fm") && (channel != 0))
        return {{"error", "Only fm has channels"}};

    if (source == "fm")
    {
        int chips = audio->GetOPLChips();
        int count = chips == 0 ? 0 : audio->GetOPL(0)->GetModel() == OPL::OPL_MODEL_OPL3 ? OPL_CHANNEL_COUNT :
            OPL2_CHANNEL_COUNT * chips;

        if (channel == 0)
            audio->SetSourceMute(Audio::AUDIO_SOURCE_FM, mute);
        else if (count == 0)
            return {{"error", "The sound card has no FM chip"}};
        else if ((channel < 1) || (channel > count))
            return {{"error", "FM channel must be 1-" + std::to_string(count)}};
        else if (chips > 1)
            audio->GetOPL((channel - 1) / OPL2_CHANNEL_COUNT)->SetChannelMute((channel - 1) % OPL2_CHANNEL_COUNT, mute);
        else
            audio->GetOPL(0)->SetChannelMute(channel - 1, mute);
    }
    else if (source == "pcm")
        audio->SetSourceMute(Audio::AUDIO_SOURCE_PCM, mute);
    else if (source == "cdda")
        audio->SetSourceMute(Audio::AUDIO_SOURCE_CDDA, mute);
    else
        audio->SetSourceMute(Audio::AUDIO_SOURCE_BUZZER, mute);

    json result = {{"success", true}, {"source", source}, {"muted", mute}};

    if (channel != 0)
        result["channel"] = channel;

    return result;
}

static std::string msf_text(u32 lba)
{
    GDOX_CdRomMSF msf;
    char text[16];
    LbaToMsf(lba, &msf);
    snprintf(text, sizeof(text), "%02u:%02u:%02u", msf.minutes, msf.seconds, msf.frames);
    return text;
}

static std::string hex_bytes(const u8* data, u32 size)
{
    std::string text;
    char byte[4];

    for (u32 i = 0; i < size; i++)
    {
        snprintf(byte, sizeof(byte), i == 0 ? "%02X" : " %02X", data[i]);
        text += byte;
    }

    return text;
}

static const char* cdrom_sense_key_name(u8 key)
{
    switch (key)
    {
        case 0x00: return "no sense";
        case 0x02: return "not ready";
        case 0x03: return "medium error";
        case 0x04: return "hardware error";
        case 0x05: return "illegal request";
        case 0x06: return "unit attention";
        case 0x0B: return "aborted command";
        default: return "other";
    }
}

static const char* cdrom_audio_status_name(u8 status)
{
    switch (status)
    {
        case IDE_CDROM_AUDIO_PLAYING: return "playing";
        case IDE_CDROM_AUDIO_PAUSED: return "paused";
        case IDE_CDROM_AUDIO_COMPLETED: return "completed";
        case IDE_CDROM_AUDIO_ERROR: return "error";
        case IDE_CDROM_AUDIO_NONE: return "none";
        default: return "invalid";
    }
}

json DebugAdapter::GetCDROMStatus()
{
    CdRomMedia* media = m_core->GetCDROMMedia();
    CdRomAudio* audio = m_core->GetCDROMAudio();
    IdeCdRom* cdrom = m_core->GetCDROM();
    bool ready = media->IsReady();
    bool fitted = IsValidPointer(cdrom) && cdrom->IsAttached();
    u32 head = gui_debug_cdrom_head();
    json drive = {{"fitted", fitted}, {"state", audio->IsPlaying() ? "playing" : audio->IsPaused() ? "paused" : "idle"}};

    if (fitted)
    {
        IdeCdRom::IdeCdRom_State* state = cdrom->GetState();
        int position = m_core->GetIDE()->GetCdRomSlot() + 1;
        bool drive_ready = cdrom->IsReady(m_core->GetScheduler()->GetClocks());
        char sense[16];
        snprintf(sense, sizeof(sense), "%02X/%02X/%02X", state->sense_key, state->sense_asc, state->sense_ascq);
        json packet = {
            {"opcode", state->packet[0]},
            {"name", GetCdRomPacketName(state->packet[0])},
            {"bytes", hex_bytes(state->packet, IDE_CDROM_PACKET_SIZE)}
        };
        json ports = json::array();

        for (int i = 0; i < 2; i++)
            ports.push_back({{"port", i}, {"channel", state->port_channel[i]}, {"volume", state->port_volume[i]}});

        drive["position"] = k_cdrom_positions[CLAMP(position, 0, GDOX_CDROM_POSITION_COUNT - 1)].id;
        drive["position_name"] = k_cdrom_positions[CLAMP(position, 0, GDOX_CDROM_POSITION_COUNT - 1)].name;
        drive["slot"] = position - 1;
        drive["speed"] = cdrom->GetSpeed();
        drive["tray_open"] = state->tray_open;
        drive["disc"] = cdrom->HasDisc();
        drive["ready"] = drive_ready;
        drive["spinning_up"] = !state->tray_open && cdrom->HasDisc() && !drive_ready;
        drive["motor_on"] = state->motor_on;
        drive["unit_attention_pending"] = state->unit_attention != 0;

        if (state->unit_attention != 0)
        {
            char attention[16];
            snprintf(attention, sizeof(attention), "06/%02X/00", state->unit_attention);
            drive["unit_attention"] = attention;
        }

        drive["prevent_removal"] = state->prevent;
        drive["busy"] = cdrom->IsBusy();
        drive["phase"] = state->phase;
        drive["access_count"] = cdrom->GetAccessCount();
        drive["head_lba"] = state->head_lba;
        drive["head_msf"] = msf_text(state->head_lba + 150);
        drive["read_ahead_start_lba"] = state->ahead_lba;
        drive["read_ahead_end_lba"] = state->ahead_end;
        drive["last_packet"] = packet;
        drive["sense"] = {{"key", state->sense_key}, {"asc", state->sense_asc}, {"ascq", state->sense_ascq},
            {"text", sense}, {"key_name", cdrom_sense_key_name(state->sense_key)}};
        drive["audio_status"] = {{"code", cdrom->PeekAudioStatus()},
            {"name", cdrom_audio_status_name(cdrom->PeekAudioStatus())}};
        drive["page_0e"] = {{"sotc", state->sotc}, {"ports", ports}};
    }
    else
        drive["position"] = "none";

    if (ready)
    {
        s32 track = media->FindTrackFromLBA(head, true);
        drive["audio_head_lba"] = head;
        drive["audio_head_msf"] = msf_text(head + 150);

        if (track >= 0)
            drive["head_track"] = track + 1;
    }

    json media_json = {{"ready", ready}};

    if (ready)
    {
        GDOX_CdRomMSF length = media->GetCdRomLength();
        char text[16];
        snprintf(text, sizeof(text), "%02u:%02u:%02u", length.minutes, length.seconds, length.frames);
        media_json["type"] = media->GetFileExtension();
        media_json["tracks"] = media->GetTrackCount();
        media_json["length"] = text;
        media_json["sectors"] = media->GetSectorCount();
    }

    return {
        {"drive", drive},
        {"media", media_json}
    };
}

json DebugAdapter::ListCDROMTracks()
{
    CdRomMedia* media = m_core->GetCDROMMedia();

    if (!media->IsReady())
        return {{"error", "No CD-ROM inserted"}};

    const std::vector<CdRomImage::Track>& tracks = media->GetTracks();
    s32 current = media->FindTrackFromLBA(gui_debug_cdrom_head(), true);
    json list = json::array();

    for (size_t i = 0; i < tracks.size(); i++)
    {
        const CdRomImage::Track& track = tracks[i];
        json entry = {
            {"track", (int)i + 1},
            {"type", track.type == GDOX_CDROM_AUDIO_TRACK ? "audio" : "data"},
            {"format", TrackTypeName(track.type)},
            {"start_msf", msf_text(track.start_lba + 150)},
            {"end_msf", msf_text(track.end_lba + 150)},
            {"length", msf_text(track.sector_count)},
            {"start_lba", track.start_lba},
            {"end_lba", track.end_lba},
            {"sectors", track.sector_count},
            {"current", (s32)i == current}
        };

        if (track.has_lead_in)
            entry["pregap_sectors"] = track.start_lba - track.lead_in_lba;

        list.push_back(entry);
    }

    GDOX_CdRomMSF length = media->GetCdRomLength();
    char text[16];
    snprintf(text, sizeof(text), "%02u:%02u:%02u", length.minutes, length.seconds, length.frames);

    return {
        {"file_name", media->GetFileName()},
        {"length", text},
        {"sectors", media->GetSectorCount()},
        {"note", "MSF values are absolute (LBA + 150), as the drive reports them"},
        {"tracks", list}
    };
}

json DebugAdapter::GetCDROMAudioStatus()
{
    Audio* audio = m_core->GetAudio();
    CdRomAudio::CdRomAudio_State* state = m_core->GetCDROMAudio()->GetState();
    CdRomMedia* media = m_core->GetCDROMMedia();
    static const char* states[3] = { "idle", "playing", "paused" };
    bool active = state->play_state != CdRomAudio::CDROM_AUDIO_IDLE;
    json result = {
        {"state", states[state->play_state % 3]},
        {"cdda_muted", audio->IsSourceMuted(Audio::AUDIO_SOURCE_CDDA)}
    };

    if (active)
    {
        s32 track = media->IsReady() ? media->FindTrackFromLBA(state->current_lba, true) : -1;
        result["end"] = state->repeat ? "repeat" : "stop";
        result["start_lba"] = state->start_lba;
        result["start_msf"] = msf_text(state->start_lba + 150);
        result["stop_lba"] = state->end_lba;
        result["stop_msf"] = msf_text(state->end_lba + 150);
        result["current_lba"] = state->current_lba;
        result["current_msf"] = msf_text(state->current_lba + 150);

        if (track >= 0)
        {
            u32 position = state->current_lba - MIN(state->current_lba, media->GetTracks()[track].start_lba);
            result["track"] = track + 1;
            result["track_position_lba"] = position;
            result["track_position"] = msf_text(position);
        }
    }

    return result;
}

// A failed read on a physical drive would mark the disc as failed for the game too
json DebugAdapter::ReadCDROMSector(u32 lba, const std::string& mode)
{
    CdRomMedia* media = m_core->GetCDROMMedia();

    if (!media->IsReady())
        return {{"error", "No CD-ROM inserted"}};

    if (m_core->GetMedia()->IsPhysicalCdRom())
        return {{"error", "Not available for physical CD drives: a failed read would stop the drive for the game"}};

    if (lba >= media->GetSectorCount())
        return {{"error", "lba must be below " + std::to_string(media->GetSectorCount())}};

    if (mode != "user" && mode != "raw")
        return {{"error", "mode must be user or raw"}};

    s32 track = media->FindTrackFromLBA(lba);
    bool audio = media->IsAudioSector(lba);

    if (mode == "user" && audio)
        return {{"error", "Audio sectors have no user data; use mode raw"}};

    u8 buffer[2352];
    u32 head = media->GetCurrentSector();
    bool ok = mode == "raw" ? media->ReadRawSector2352(lba, buffer) : media->ReadSector(lba, buffer);
    media->SetCurrentSector(head);

    if (!ok)
        return {{"error", "Failed to read the sector"}};

    u32 size = mode == "raw" ? 2352 : 2048;
    json result = {
        {"lba", lba},
        {"msf", msf_text(lba + 150)},
        {"mode", mode},
        {"type", audio ? "audio" : "data"},
        {"size", size},
        {"data", hex_bytes(buffer, size)}
    };

    if (track >= 0)
        result["track"] = track + 1;

    return result;
}

json DebugAdapter::ListFloppyDrives()
{
    FDC* fdc = m_core->GetFDC();
    int present = m_core->GetMachineConfig().floppy_drives;
    json drives = json::array();

    for (int drive = 0; drive < FDC_DRIVES; drive++)
    {
        FloppyDisk* disk = fdc->GetDisk(drive);
        json entry = {{"drive", drive}, {"present", drive < present}};

        if (drive >= present)
        {
            drives.push_back(entry);
            continue;
        }

        entry["drive_type"] = k_debug_floppy_type_names[fdc->GetDriveType(drive)];
        entry["inserted"] = disk->IsInserted();
        entry["head_cylinder"] = fdc->GetState()->cylinders[drive];
        entry["disk_changed"] = fdc->GetState()->disk_changed[drive];

        if (disk->IsInserted())
        {
            Emu_FloppyInfo info;
            int cylinders = 0;
            int heads = 0;
            int sectors = 0;
            int sector_size = 0;
            u32 total_size = 0;
            bool mixed = false;

            char name[64] = { };
            gui_debug_floppy_disk_name(disk, name, sizeof(name));
            entry["disk_name"] = name;

            if (emu_floppy_get_info(drive, &info))
                entry["image"] = info.path;

            entry["media"] = k_debug_floppy_media_names[disk->GetMedia() % 4];
            entry["rpm"] = disk->GetRPM();
            entry["write_protected"] = disk->IsWriteProtected();
            entry["modified"] = disk->IsDirty();

            if (gui_debug_floppy_geometry(disk, cylinders, heads, sectors, sector_size, total_size, mixed))
                entry["geometry"] = {{"cylinders", cylinders}, {"heads", heads}, {"sectors", sectors},
                    {"sector_size", sector_size}, {"total_size", total_size}, {"mixed", mixed}};
        }

        drives.push_back(entry);
    }

    return {{"drives", drives}};
}

static FloppyDisk* get_floppy_disk(GeardoxCore* core, int drive, std::string& error)
{
    if (drive < 0 || drive >= MIN(FDC_DRIVES, core->GetMachineConfig().floppy_drives))
    {
        error = "This machine has no drive " + std::to_string(drive);
        return NULL;
    }

    FloppyDisk* disk = core->GetFDC()->GetDisk(drive);

    if (!disk->IsInserted())
    {
        error = "No disk in drive " + std::to_string(drive);
        return NULL;
    }

    return disk;
}

json DebugAdapter::ListFloppySectors(int drive, int cylinder, int head)
{
    std::string error;
    FloppyDisk* disk = get_floppy_disk(m_core, drive, error);

    if (!IsValidPointer(disk))
        return {{"error", error}};

    if (cylinder < 0 || cylinder >= k_floppy_tracks / 2 || head < 0 || head > 1)
        return {{"error", "cylinder must be 0-81 and head 0-1"}};

    FloppyDisk_Sector sectors[k_floppy_max_sectors];
    int count = disk->GetSectors(cylinder * 2 + head, sectors);
    json list = json::array();

    for (int i = 0; i < count; i++)
    {
        const FloppyDisk_Sector& sector = sectors[i];
        list.push_back({
            {"index", i},
            {"c", hex_text(sector.id[0], 2)},
            {"h", hex_text(sector.id[1], 2)},
            {"r", hex_text(sector.id[2], 2)},
            {"n", hex_text(sector.id[3], 2)},
            {"size", sector.size},
            {"density", sector.fm ? "fm" : "mfm"},
            {"deleted", sector.deleted},
            {"status", to_lower(gui_debug_floppy_status_name(sector.status))},
            {"image_offset", hex_text(sector.header + k_floppy_sector_header_size, 6)}
        });
    }

    return {{"drive", drive}, {"cylinder", cylinder}, {"head", head}, {"formatted", count > 0}, {"sectors", list}};
}

json DebugAdapter::ReadFloppySector(int drive, int cylinder, int head, int sector)
{
    std::string error;
    FloppyDisk* disk = get_floppy_disk(m_core, drive, error);

    if (!IsValidPointer(disk))
        return {{"error", error}};

    if (cylinder < 0 || cylinder >= k_floppy_tracks / 2 || head < 0 || head > 1 || sector < 0 || sector > 255)
        return {{"error", "cylinder must be 0-81, head 0-1 and sector 0-255"}};

    FloppyDisk_Sector sectors[k_floppy_max_sectors];
    int count = disk->GetSectors(cylinder * 2 + head, sectors);

    for (int i = 0; i < count; i++)
    {
        const FloppyDisk_Sector& entry = sectors[i];

        if (entry.id[2] != sector)
            continue;

        u32 offset = entry.header + k_floppy_sector_header_size;
        u32 size = MIN((u32)entry.size, disk->GetImageSize() > offset ? disk->GetImageSize() - offset : 0);

        return {
            {"drive", drive},
            {"cylinder", cylinder},
            {"head", head},
            {"id", {{"c", hex_text(entry.id[0], 2)}, {"h", hex_text(entry.id[1], 2)}, {"r", hex_text(entry.id[2], 2)},
                {"n", hex_text(entry.id[3], 2)}}},
            {"density", entry.fm ? "fm" : "mfm"},
            {"deleted", entry.deleted},
            {"status", to_lower(gui_debug_floppy_status_name(entry.status))},
            {"image_offset", hex_text(offset, 6)},
            {"size", size},
            {"data", hex_bytes(disk->GetImage() + offset, size)}
        };
    }

    return {{"error", "Sector R=" + hex_text(sector, 2) + " not found on that track"}};
}

json DebugAdapter::InsertFloppy(int drive, const std::string& file_path, const json& write_protected)
{
    if (drive < 0 || drive >= MIN(FDC_DRIVES, m_core->GetMachineConfig().floppy_drives))
        return {{"error", "This machine has no drive " + std::to_string(drive)}};

    if (!emu_floppy_insert(drive, file_path.c_str(), false))
        return {{"error", "Failed to insert the floppy image"}};

    if (write_protected.is_boolean())
        emu_floppy_set_write_protected(drive, write_protected.get<bool>());

    return {{"success", true}, {"drive", drive}, {"file_path", file_path},
        {"write_protected", m_core->GetFDC()->GetDisk(drive)->IsWriteProtected()}};
}

static const char* const k_blank_floppy_types[FLOPPY_BLANK_COUNT] = { "360kb", "720kb", "1200kb", "1440kb", "2880kb" };

json DebugAdapter::CreateBlankFloppy(int drive, const std::string& file_path, const std::string& type, bool formatted)
{
    if (drive < 0 || drive >= MIN(FDC_DRIVES, m_core->GetMachineConfig().floppy_drives))
        return {{"error", "This machine has no drive " + std::to_string(drive)}};

    const char* path = file_path.c_str();
    bool d77 = ends_with_no_case(path, ".d77");

    if (!is_absolute_path(path))
        return {{"error", "file_path must be an absolute path"}};

    if (!d77 && !ends_with_no_case(path, ".img") && !ends_with_no_case(path, ".ima"))
        return {{"error", "file_path must end in .d77, .img or .ima"}};

    if (!formatted && !d77)
        return {{"error", "An unformatted disk can only be saved as .d77"}};

    if (path_exists(path))
        return {{"error", "A file already exists at " + file_path}};

    int blank = -1;

    for (int i = 0; i < FLOPPY_BLANK_COUNT; i++)
    {
        if (type == k_blank_floppy_types[i])
            blank = i;
    }

    if (blank < 0)
        return {{"error", "Unknown disk type: " + type}};

    if (!emu_floppy_create_blank(drive, path, blank, formatted, false))
        return {{"error", "Failed to create the blank disk"}};

    return {{"success", true}, {"drive", drive}, {"file_path", file_path}, {"type", type}, {"formatted", formatted},
        {"write_protected", m_core->GetFDC()->GetDisk(drive)->IsWriteProtected()}};
}

json DebugAdapter::EjectFloppy(int drive)
{
    if (drive < 0 || drive >= MIN(FDC_DRIVES, m_core->GetMachineConfig().floppy_drives))
        return {{"error", "This machine has no drive " + std::to_string(drive)}};

    if (!m_core->GetFDC()->GetDisk(drive)->IsInserted())
        return {{"error", "No disk in drive " + std::to_string(drive)}};

    if (!emu_floppy_eject(drive, false))
        return {{"error", "Failed to keep the disk changes; the disk stays inserted"}};

    return {{"success", true}, {"drive", drive}};
}

static json hard_disk_json(int id)
{
    Emu_HardDiskInfo info;
    emu_hard_disk_get_info(id, &info);
    json disk = {{"id", id}, {"attached", info.attached}};

    if (info.attached)
    {
        disk["file_path"] = info.path;
        disk["blocks"] = info.blocks;
        disk["size_mb"] = (double)info.blocks / 2048.0;
        disk["write_protected"] = info.write_protected;
        disk["unwritten_blocks"] = info.unflushed;

        IdeDisk* ide_disk = emu_get_core()->GetIDE()->GetDisk(id);
        IdeDisk::IdeDisk_State* state = ide_disk->GetState();
        disk["position"] = k_debug_ide_slot_names[id];
        disk["geometry"] = {{"cylinders", state->cylinders}, {"heads", state->heads}, {"sectors", state->sectors}};
        disk["bios_translation"] = ide_disk->GetTranslation() != 0 ? "LBA" : "NONE";
    }

    if (info.attached || info.pending_attached)
    {
        bool zip = ends_with_no_case(info.source, ".zip");
        disk["source"] = info.game ? "game" : info.builtin ? (info.builtin_system == GDOX_GAME_SYSTEM_MSDOS ? "msdos" : "freedos") : (info.source[0] == 0 ? "image" : (zip ? "zip" : "folder"));

        if (info.source[0] != 0)
            disk["source_path"] = info.source;
    }

    if (info.pending)
        disk["at_next_reset"] = {{"attached", info.pending_attached}, {"file_path", info.pending_path},
            {"write_protected", info.pending_write_protected}};

    return disk;
}

json DebugAdapter::ListHardDisks()
{
    json disks = json::array();

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
        disks.push_back(hard_disk_json(i));

    return {{"disks", disks}};
}

json DebugAdapter::AttachHardDisk(int id, const std::string& file_path, const json& write_protected,
    const json& create_size_mb, bool formatted)
{
    if ((id < 0) || (id >= GDOX_MAX_HARD_DISKS))
        return {{"error", "id must be 0-3"}};

    const char* path = file_path.c_str();
    bool protect = write_protected.is_boolean() ? write_protected.get<bool>() :
        config_emulator.hard_disk_write_protected[id] != 0;

    if (!is_absolute_path(path))
        return {{"error", "file_path must be an absolute path"}};

    if (create_size_mb.is_number_integer())
    {
        int size_mb = create_size_mb.get<int>();

        if ((size_mb < 1) || (size_mb > 2047))
            return {{"error", "create_size_mb must be 1-2047"}};

        if (path_exists(path))
            return {{"error", "A file already exists at " + file_path}};

        if (formatted && (size_mb < 33))
            return {{"error", "A formatted disk needs 33 MB or more"}};

        if (!emu_hard_disk_create(id, path, size_mb, formatted, protect))
            return {{"error", "Failed to create the hard disk image"}};
    }
    else if (emu_hard_disk_is_source(path))
    {
        if (emu_hard_disk_is_source_attached(path))
            return {{"error", "This folder is attached already"}};

        emu_hard_disk_prepare_source(path, false);
        const FatImage_Report& report = emu_hard_disk_get_prepared_source()->report;

        if (report.result == FAT_IMAGE_TOO_BIG)
            return {{"error", "This doesn't fit in a FAT16 disk of 2015 MB"}};

        if ((report.result == FAT_IMAGE_ERROR) || !emu_hard_disk_attach_prepared_source(id, protect))
            return {{"error", "Failed to make a hard disk from this folder or ZIP"}};
    }
    else if (!emu_hard_disk_attach(id, path, protect))
        return {{"error", "Failed to attach the hard disk image"}};

    json result = hard_disk_json(id);
    result["success"] = true;

    if (emu_hard_disk_is_source(path))
    {
        const FatImage_Report& report = emu_hard_disk_get_prepared_source()->report;
        result["left_out"] = report.left_out;
        result["changed_on_host"] = report.result == FAT_IMAGE_CHANGED;
    }

    return result;
}

json DebugAdapter::DetachHardDisk(int id)
{
    if ((id < 0) || (id >= GDOX_MAX_HARD_DISKS))
        return {{"error", "id must be 0-3"}};

    emu_hard_disk_detach(id);
    json result = hard_disk_json(id);
    result["success"] = true;
    return result;
}

json DebugAdapter::FlushHardDisks()
{
    if (!emu_hard_disk_flush())
        return {{"error", "Unable to write the hard disk changes, they stay in memory"}};

    return {{"success", true}};
}

json DebugAdapter::ReadHardDiskSector(int id, s64 lba)
{
    if ((id < 0) || (id >= GDOX_MAX_HARD_DISKS))
        return {{"error", "id must be 0-3"}};

    HardDiskImage* image = m_core->GetHardDisk(id);

    if (!image->IsOpen())
        return {{"error", "No hard disk on ID " + std::to_string(id)}};

    if ((lba < 0) || (lba >= (s64)image->GetBlocks()))
        return {{"error", "lba must be 0-" + std::to_string(image->GetBlocks() - 1)}};

    u8 buffer[k_hard_disk_block_size];

    if (!image->ReadBlock((u32)lba, buffer))
        return {{"error", "Unable to read the image file"}};

    return {{"id", id}, {"lba", lba}, {"data", hex_bytes(buffer, k_hard_disk_block_size)}};
}

json DebugAdapter::SwapFloppies()
{
    if (m_core->GetMachineConfig().floppy_drives < 2)
        return {{"error", "This machine has a single drive"}};

    emu_floppy_swap();
    return {{"success", true}};
}

json DebugAdapter::SetFloppyWriteProtect(int drive, bool write_protected)
{
    std::string error;
    FloppyDisk* disk = get_floppy_disk(m_core, drive, error);

    if (!IsValidPointer(disk))
        return {{"error", error}};

    if (!emu_floppy_set_write_protected(drive, write_protected))
        return {{"error", "This disk comes from a save state and stays write protected"}};

    return {{"success", true}, {"drive", drive}, {"write_protected", disk->IsWriteProtected()}};
}

json DebugAdapter::GetTraceLog(s64 start, int count)
{
    json result;
    const char* stop_reason = gui_debug_trace_logger_get_stop_reason();
    result["enabled"] = gui_debug_trace_logger_is_enabled();

    if (IsValidPointer(stop_reason))
        result["stopped_by"] = stop_reason;

    TraceLogger* tl = m_core->GetTraceLogger();
    u32 retained = tl->GetCount();
    u64 total = tl->GetSequence();
    u64 oldest = total - retained;

    if (count < 1)
        count = 100;

    if (count > 1000)
        count = 1000;

    u64 actual_start;
    bool overrun = false;

    if (start < 0)
    {
        u64 tail = (u64)(-(start + 1)) + 1;
        actual_start = (total - oldest > tail) ? (total - tail) : oldest;
    }
    else
    {
        actual_start = (u64)start;

        if (actual_start < oldest)
        {
            actual_start = oldest;
            overrun = true;
        }
    }

    if (actual_start >= total)
    {
        result["total_entries"] = retained;
        result["total_logged"] = total;
        result["oldest_sequence"] = oldest;
        result["start"] = actual_start;
        result["next_sequence"] = actual_start;
        result["count"] = 0;
        result["overrun"] = overrun;
        result["lines"] = json::array();
        return result;
    }

    u32 actual_count = (u32)count;

    if ((u64)actual_count > total - actual_start)
        actual_count = (u32)(total - actual_start);

    u32 buffer_start = (u32)(actual_start - oldest);
    json lines = json::array();

    for (u32 i = 0; i < actual_count; i++)
    {
        const GDOX_Trace_Entry& entry = tl->GetEntry(buffer_start + i);
        char buf[GDOX_TRACE_FORMAT_BUFFER_SIZE];

        GDOX_Trace_Format_Options options = {};
        options.linear = true;
        options.registers = true;
        options.segments = true;
        options.flags = true;
        options.bytes = true;
        options.cycles = true;
        options.previous_cycle_valid = (buffer_start + i) > 0;
        options.previous_cycle = options.previous_cycle_valid ? tl->GetEntry(buffer_start + i - 1).cycle : 0;
        trace_logger_format_entry(entry, options, buf, sizeof(buf));
        lines.push_back(buf);
    }

    result["total_entries"] = retained;
    result["total_logged"] = total;
    result["oldest_sequence"] = oldest;
    result["start"] = actual_start;
    result["next_sequence"] = actual_start + actual_count;
    result["count"] = actual_count;
    result["overrun"] = overrun;
    result["lines"] = lines;
    return result;
}

json DebugAdapter::SetTraceLog(bool enabled, u32 flags, const std::string& output, const std::string& memory_size,
    const std::string& disk_size, const std::string& output_path, const u32* event_filters,
    const std::string& vblank_watch_address, const std::string& vblank_watch_operation)
{
    static const char* const k_vblank_watch_operations[] = { "read", "write", "read_write" };
    json result;
    TraceLogger* tl = m_core->GetTraceLogger();

    if (!enabled)
    {
        if (!gui_debug_trace_logger_stop())
        {
            result["error"] = "Unable to stop trace logger cleanly";
            return result;
        }

        tl->SetEnabledFlags(0);
        result["status"] = "stopped";
        result["total_entries"] = tl->GetCount();
        return result;
    }

    bool was_enabled = gui_debug_trace_logger_is_enabled();
    int output_value;

    if (output.empty())
        output_value = was_enabled ? config_debug.trace_output : gui_TraceOutput_Memory;
    else if (output == "memory")
        output_value = gui_TraceOutput_Memory;
    else if (output == "disk")
        output_value = gui_TraceOutput_Disk;
    else
    {
        result["error"] = "Invalid trace output";
        return result;
    }

    int memory_size_value = config_debug.trace_capacity;

    if (!memory_size.empty())
    {
        memory_size_value = gui_debug_trace_logger_memory_size_index(memory_size.c_str());

        if (memory_size_value < 0)
        {
            result["error"] = "Invalid trace memory size";
            return result;
        }
    }

    int disk_size_value = config_debug.trace_disk_size;

    if (!disk_size.empty())
    {
        disk_size_value = gui_debug_trace_logger_disk_size_index(disk_size.c_str());

        if (disk_size_value < 0)
        {
            result["error"] = "Invalid trace disk size";
            return result;
        }
    }

    int vblank_watch_address_value = config_debug.trace_vblank_watch_address;

    if (!vblank_watch_address.empty())
    {
        u32 address = 0;

        if (!parse_hex_with_prefix(vblank_watch_address, &address))
        {
            result["error"] = "Invalid vblank watch address";
            return result;
        }

        vblank_watch_address_value = (int)address;
    }

    int vblank_watch_operation_value = config_debug.trace_vblank_watch_operation;

    if (!vblank_watch_operation.empty())
    {
        vblank_watch_operation_value = -1;

        for (int i = 0; i < 3; i++)
        {
            if (vblank_watch_operation == k_vblank_watch_operations[i])
                vblank_watch_operation_value = i;
        }

        if (vblank_watch_operation_value < 0)
        {
            result["error"] = "Invalid vblank watch operation";
            return result;
        }
    }

    bool configuration_changed = output_value != config_debug.trace_output;

    if (output_value == gui_TraceOutput_Memory)
        configuration_changed = configuration_changed || memory_size_value != config_debug.trace_capacity;
    else
    {
        configuration_changed = configuration_changed || disk_size_value != config_debug.trace_disk_size;

        if (!output_path.empty())
            configuration_changed = configuration_changed ||
                config_debug.trace_disk_dir_option != Directory_Location_Custom ||
                output_path != config_debug.trace_disk_path;
    }

    if (was_enabled && configuration_changed && !gui_debug_trace_logger_stop())
    {
        result["error"] = "Unable to stop trace logger cleanly";
        return result;
    }

    if (!gui_debug_trace_logger_is_enabled() &&
        !gui_debug_trace_logger_configure(output_value, memory_size_value, disk_size_value, output_path.c_str()))
    {
        result["error"] = "Unable to configure trace logger";
        return result;
    }

    gui_debug_trace_logger_set_event_filters(event_filters);
    config_debug.trace_vblank_watch_address = vblank_watch_address_value;
    config_debug.trace_vblank_watch_operation = vblank_watch_operation_value;

    if (!gui_debug_trace_logger_start(flags))
    {
        result["error"] = "Unable to start trace logger";
        return result;
    }

    // Only the debugger run records, so starting a trace turns it on, like the profiler
    if (!config_debug.debug)
    {
        config_debug.debug = true;
        emu_debug_continue();
    }

    result["status"] = "started";
    result["output"] = config_debug.trace_output == gui_TraceOutput_Disk ? "disk" : "memory";
    result["memory_size"] = gui_debug_trace_logger_memory_size_name(config_debug.trace_capacity);
    result["disk_size"] = gui_debug_trace_logger_disk_size_name(config_debug.trace_disk_size);

    if (config_debug.trace_output == gui_TraceOutput_Disk)
        result["output_path"] = gui_debug_trace_logger_get_output_path();

    u32 enabled_flags = tl->GetEnabledFlags();
    json event_filter_list = json::array();

    for (int i = 0; i < k_mcp_trace_filter_count; i++)
    {
        const McpTraceFilter& filter = k_mcp_trace_filters[i];
        u32 events = tl->GetEventFilter(filter.type);

        if (IS_SET_BIT(enabled_flags, filter.type) && (events & filter.mask) == filter.mask)
            event_filter_list.push_back(filter.name);
    }

    result["filters"] = event_filter_list;

    if ((enabled_flags & TRACE_FLAG_VIDEO) != 0 && (tl->GetEventFilter(TRACE_VIDEO) & TRACE_VIDEO_EVENT_MISSED_VBLANK) != 0)
    {
        char address[16];
        snprintf(address, sizeof(address), "%08X", (u32)config_debug.trace_vblank_watch_address);
        result["vblank_watch_address"] = address;
        result["vblank_watch_operation"] = k_vblank_watch_operations[config_debug.trace_vblank_watch_operation];
    }

    result["total_entries"] = tl->GetCount();
    return result;
}

struct ProfilerEntry
{
    u32 index;
    u64 key;
    std::string name;
    const char* symbol;
};

static const char* const k_profiler_type_names[] = { "root", "call", "interrupt" };

static bool profiler_entry_compare(const ProfilerEntry& a, const ProfilerEntry& b)
{
    if (a.key != b.key)
        return a.key > b.key;

    return a.index < b.index;
}

static double profiler_round(double value)
{
    return floor((value * 100.0) + 0.5) / 100.0;
}

json DebugAdapter::SetProfiler(const std::string& action)
{
    json result;

    Profiler* profiler = m_core->GetProfiler();

    if (!IsValidPointer(profiler))
    {
        result["error"] = "Profiler not available";
        return result;
    }

    if (action == "start")
    {
        if (!config_debug.debug)
        {
            config_debug.debug = true;
            emu_debug_continue();
        }

        gui_debug_profiler_show(true);
    }
    else if (action == "stop")
        gui_debug_profiler_show(false);
    else if (action == "reset")
    {
        profiler->Reset();
        gui_debug_profiler_reset();
    }
    else
    {
        result["error"] = "Invalid profiler action";
        return result;
    }

    result["success"] = true;
    result["action"] = action;
    result["window_open"] = config_debug.show_profiler;
    return result;
}

json DebugAdapter::GetProfilerData(const std::string& sort, int count, const std::string& filter)
{
    json result;

    Profiler* profiler = m_core->GetProfiler();

    if (!IsValidPointer(profiler) || !IsValidPointer(profiler->GetFunctions()))
    {
        result["error"] = "Profiler not available";
        return result;
    }

    if (count < 1)
        count = 50;

    if (count > 1000)
        count = 1000;

    profiler->Sync();

    const GDOX_Profiler_Function* functions = profiler->GetFunctions();
    u32 function_count = profiler->GetFunctionCount();
    u64 total = profiler->GetTotalCycles();
    u32 frames = profiler->GetFrames();

    std::string filter_upper = filter;
    std::transform(filter_upper.begin(), filter_upper.end(), filter_upper.begin(), ::toupper);

    std::vector<ProfilerEntry> entries;

    for (u32 i = 0; i < function_count; i++)
    {
        const GDOX_Profiler_Function& function = functions[i];
        bool pseudo = (function.type == PROFILER_FUNCTION_ROOT);

        ProfilerEntry entry;
        entry.index = i;
        entry.key = 0;
        entry.symbol = "none";

        if (function.type == PROFILER_FUNCTION_ROOT)
            entry.name = "[Root]";
        else
        {
            bool is_manual = false;
            const char* name = gui_debug_get_symbol_name(function.address, &is_manual);

            if (IsValidPointer(name))
            {
                entry.name = name;
                entry.symbol = is_manual ? "manual" : "auto";
            }
        }

        if (!filter_upper.empty())
        {
            std::string name_upper = entry.name;
            std::transform(name_upper.begin(), name_upper.end(), name_upper.begin(), ::toupper);

            char address[16];
            snprintf(address, sizeof(address), "%08X", function.address);

            bool name_match = (name_upper.find(filter_upper) != std::string::npos);
            bool address_match = !pseudo && (std::string(address).find(filter_upper) != std::string::npos);

            if (!name_match && !address_match)
                continue;
        }

        if (sort == "exclusive")
            entry.key = function.exclusive_cycles;
        else if (sort == "calls")
            entry.key = function.calls;
        else if (sort == "average")
            entry.key = (function.completed > 0) ? function.inclusive_cycles / function.completed : 0;
        else if (sort == "max")
            entry.key = (function.completed > 0) ? function.max_cycles : 0;
        else
            entry.key = function.inclusive_cycles;

        entries.push_back(entry);
    }

    std::sort(entries.begin(), entries.end(), profiler_entry_compare);

    json functions_array = json::array();

    for (size_t i = 0; (i < entries.size()) && (i < (size_t)count); i++)
    {
        const GDOX_Profiler_Function& function = functions[entries[i].index];
        bool root = (function.type == PROFILER_FUNCTION_ROOT);
        json item;
        char text[16];

        item["name"] = entries[i].name;
        item["type"] = k_profiler_type_names[function.type];

        if (!root)
        {
            item["symbol"] = entries[i].symbol;
            snprintf(text, sizeof(text), "%08X", function.address);
            item["address"] = text;
        }

        if (function.type == PROFILER_FUNCTION_INTERRUPT)
        {
            char name[16];
            char description[64];
            gui_debug_i386_vector_name(function.vector, name, sizeof(name), description, sizeof(description));
            snprintf(text, sizeof(text), "%02X", function.vector);
            item["vector"] = text;
            item["vector_name"] = name;
            item["vector_description"] = description;
        }

        if (!root)
        {
            item["calls"] = function.calls;
            item["calls_per_frame"] = (frames > 0) ? profiler_round((double)function.calls / (double)frames) : 0.0;
            item["inclusive_cycles"] = function.inclusive_cycles;
            item["inclusive_percent"] = (total > 0) ?
                profiler_round(((double)function.inclusive_cycles * 100.0) / (double)total) : 0.0;
        }

        item["exclusive_cycles"] = function.exclusive_cycles;
        item["exclusive_percent"] = (total > 0) ?
            profiler_round(((double)function.exclusive_cycles * 100.0) / (double)total) : 0.0;

        if (!root && (function.completed > 0))
        {
            item["average_cycles"] = function.inclusive_cycles / function.completed;
            item["min_cycles"] = function.min_cycles;
            item["max_cycles"] = function.max_cycles;
        }

        functions_array.push_back(item);
    }

    result["collecting"] = profiler->IsEnabled();
    result["window_open"] = config_debug.show_profiler;
    result["total_cycles"] = total;
    result["frames"] = frames;
    result["function_count"] = function_count - 1;
    result["sort"] = sort;
    result["count"] = functions_array.size();
    result["functions"] = functions_array;

    return result;
}

json DebugAdapter::GetScreenshot()
{
    json result;

    if (!m_core || emu_is_empty())
    {
        result["error"] = "Emulator is powered off";
        return result;
    }

    if (emu_frame_stale)
    {
        result["error"] = "No frame since the state load: the state has no saved frame or the screen size changed. "
            "Run a frame first (debug_step_frame)";
        return result;
    }

    GDOX_Runtime_Info runtime;
    emu_get_runtime(runtime);

    unsigned char* png_buffer = NULL;
    int png_size = emu_get_screenshot_png(&png_buffer);

    if (png_size <= 0 || !png_buffer)
    {
        result["error"] = "Failed to capture screenshot";
        return result;
    }

    std::string base64_png = base64_encode(png_buffer, png_size);
    free(png_buffer);

    result["__mcp_image"] = true;
    result["data"] = base64_png;
    result["mimeType"] = "image/png";
    result["width"] = runtime.screen_width;
    result["height"] = runtime.screen_height;
    return result;
}

static const char* const k_video_recording_ratios[] = { "screen", "square", "4:3", "16:9", "16:10" };
static const char* const k_video_recording_qualities[] = { "low", "medium", "high", "lossless", "lossless_compressed" };
static const int k_video_recording_ratio_count = sizeof(k_video_recording_ratios) / sizeof(k_video_recording_ratios[0]);
static const int k_video_recording_quality_count = sizeof(k_video_recording_qualities) / sizeof(k_video_recording_qualities[0]);

static int find_video_recording_option(const std::string& value, const char* const* options, int count)
{
    for (int i = 0; i < count; i++)
    {
        if (value == options[i])
            return i;
    }
    return -1;
}

json DebugAdapter::StartVideoRecording(const std::string& file_path, int scale, const std::string& aspect_ratio,
    const std::string& quality)
{
    json result;

    if (!m_core || emu_is_empty())
    {
        result["error"] = "Emulator is powered off";
        Log("[MCP] StartVideoRecording failed: Emulator is powered off");
        return result;
    }

    if (emu_is_video_recording())
    {
        result["error"] = "Video recording is already active";
        return result;
    }

    int ratio = aspect_ratio.empty() ? config_video.recording_ratio :
        find_video_recording_option(aspect_ratio, k_video_recording_ratios, k_video_recording_ratio_count);
    int quality_index = quality.empty() ? config_video.recording_quality :
        find_video_recording_option(quality, k_video_recording_qualities, k_video_recording_quality_count);

    if ((scale != 0) && ((scale < 1) || (scale > 20)))
    {
        result["error"] = "Invalid scale";
        return result;
    }

    if ((ratio < 0) || (quality_index < 0))
    {
        result["error"] = "Invalid aspect ratio or quality";
        return result;
    }

    if (scale != 0)
        config_video.recording_scale = scale;
    config_video.recording_ratio = ratio;
    config_video.recording_quality = quality_index;

    if (!gui_action_start_video_recording(file_path.empty() ? NULL : file_path.c_str()))
    {
        result["error"] = "Failed to start video recording";
        Log("[MCP] StartVideoRecording failed: %s", file_path.c_str());
        return result;
    }

    result["success"] = true;
    result["file_path"] = video_recorder_get_file_path();
    result["scale"] = config_video.recording_scale;
    result["aspect_ratio"] = k_video_recording_ratios[config_video.recording_ratio];
    result["quality"] = k_video_recording_qualities[config_video.recording_quality];

    return result;
}

json DebugAdapter::StopVideoRecording()
{
    json result;

    if (!emu_is_video_recording())
    {
        result["error"] = "Video recording is not active";
        return result;
    }

    std::string file_path = video_recorder_get_file_path();
    u32 frames = video_recorder_get_frame_count();

    gui_action_stop_video_recording();

    result["success"] = true;
    result["file_path"] = file_path;
    result["frames"] = frames;

    return result;
}

json DebugAdapter::GetMediaInfo()
{
    Media* media = m_core->GetMedia();
    Firmware* firmware = m_core->GetFirmware();
    json result = {
        {"emulator", GDOX_TITLE},
        {"emulator_version", GDOX_VERSION},
        {"ready", media->IsReady()},
        {"bios_ready", firmware->IsReady()},
        {"firmware_ready", firmware->IsReady()},
        {"file_path", media->GetFilePath()},
        {"file_name", media->GetFileName()},
        {"file_directory", media->GetFileDirectory()},
        {"file_extension", media->GetFileExtension()},
        {"size", media->GetSize()},
        {"crc", media->GetCRC()}
    };

    result["firmware"] = json::array();

    for (int i = 0; i < GDOX_FIRMWARE_COUNT; i++)
    {
        GDOX_Firmware_Type type = (GDOX_Firmware_Type)i;
        const GDOX_Firmware_Info& info = firmware->GetInfo(type);
        result["firmware"].push_back({
            {"type", Firmware::GetComponentName(type)},
            {"name", info.name},
            {"size", info.size},
            {"crc", info.crc},
            {"loaded", info.loaded}
        });
    }

    static const char* media_names[] = { "2D", "2DD", "2HD", "ED" };
    json drives = json::array();

    for (int i = 0; i < config_floppy_drives; i++)
    {
        Emu_FloppyInfo info;
        json drive = {{"drive", i}, {"present", i < m_core->GetMachineConfig().floppy_drives}};

        if (emu_floppy_get_info(i, &info) && info.inserted)
        {
            FloppyDisk* disk = m_core->GetFloppy(i);
            drive["inserted"] = true;
            drive["file_path"] = info.path;
            drive["disk_name"] = IsValidPointer(emu_floppy_get_disk_name(i, info.disk_index)) ?
                emu_floppy_get_disk_name(i, info.disk_index) : "";
            drive["disk_index"] = info.disk_index;
            drive["disk_count"] = info.disk_count;
            drive["media"] = IsValidPointer(disk) ? media_names[disk->GetMedia() % 4] : "";
            drive["write_protected"] = info.write_protected;
            drive["modified"] = info.dirty;
        }
        else
            drive["inserted"] = false;

        drives.push_back(drive);
    }

    result["floppy_drives"] = drives;
    result["hard_disks"] = ListHardDisks()["disks"];
    return result;
}

json DebugAdapter::ListRecentMedia()
{
    json result;
    json recent_media = json::array();

    for (int index = 0; index < config_max_recent_roms; index++)
    {
        const std::string& path = config_emulator.recent_roms[index];

        if (path.empty())
            continue;

        json entry;
        entry["index"] = index;
        entry["file_path"] = path;
        entry["file_name"] = get_filename(path.c_str());
        recent_media.push_back(entry);
    }

    json recent_floppies = json::array();

    for (int index = 0; index < config_max_recent_floppies; index++)
    {
        const std::string& path = config_emulator.recent_floppies[index];

        if (path.empty())
            continue;

        recent_floppies.push_back({
            {"index", index},
            {"file_path", path},
            {"file_name", get_filename(path.c_str())}
        });
    }

    json recent_hard_disks = json::array();

    for (int index = 0; index < config_max_recent_hard_disks; index++)
    {
        const std::string& path = config_emulator.recent_hard_disks[index];

        if (path.empty())
            continue;

        recent_hard_disks.push_back({
            {"index", index},
            {"file_path", path},
            {"file_name", get_filename(path.c_str())}
        });
    }

    result["count"] = recent_media.size();
    result["recent_media"] = recent_media;
    result["recent_floppies"] = recent_floppies;
    result["recent_hard_disks"] = recent_hard_disks;

    return result;
}

json DebugAdapter::EjectCdrom()
{
    if (gui_is_rom_loading() || emu_is_media_loading())
        return {{"error", "Another media load is in progress"}};

    if (!m_core->GetMedia()->IsReady())
        return {{"error", "No CD in the drive"}};

    gui_action_eject_media();

    if (m_core->GetMedia()->IsReady())
        return {{"error", "Unable to eject the CD"}};

    return {{"success", true}};
}

json DebugAdapter::CloseCdromTray()
{
    IdeCdRom* cdrom = m_core->GetCDROM();

    if (!IsValidPointer(cdrom) || !cdrom->IsAttached())
        return {{"error", "No CD-ROM drive is fitted"}};

    if (gui_is_rom_loading() || emu_is_media_loading())
        return {{"error", "Another media load is in progress"}};

    bool was_open = cdrom->IsTrayOpen();
    cdrom->CloseTray(m_core->GetScheduler()->GetClocks());

    return {{"success", true}, {"was_open", was_open}, {"disc", cdrom->HasDisc()}};
}

json DebugAdapter::ListCdromPlaylist()
{
    json discs = json::array();
    int count = emu_cdrom_playlist_get_count();
    int current = emu_cdrom_playlist_get_index();

    for (int i = 0; i < count; i++)
    {
        discs.push_back({
            {"index", i},
            {"name", emu_cdrom_playlist_get_name(i)},
            {"file_path", emu_cdrom_playlist_get_path(i)},
            {"current", i == current}
        });
    }

    return {{"discs", discs}, {"drive_empty", !m_core->GetMedia()->IsReady()}};
}

json DebugAdapter::StartInsertPlaylistDisc(int index)
{
    if ((index < 0) || (index >= emu_cdrom_playlist_get_count()))
        return {{"error", "No disc " + std::to_string(index) + " in the playlist"}};

    if (gui_is_rom_loading() || emu_is_media_loading())
        return {{"error", "Another media load is already in progress"}};

    if (m_core->GetMedia()->IsReady())
        return {{"error", "Eject the CD first, as the CD-ROM menu does"}};

    if (!gui_load_playlist_disc(index))
        return {{"error", "Failed to load the playlist disc"}};

    return {{"file_path", emu_cdrom_playlist_get_path(index)}};
}

// A game descriptor, or a folder or ZIP with some, opens its game; with several the first by name, as in libretro
json DebugAdapter::StartLoadMedia(const std::string& file_path, const std::string& target)
{
    if (file_path.empty())
        return {{"error", "File path is required"}};

    if (gui_is_rom_loading())
        return {{"error", "Another media load is already in progress"}};

    std::vector<GameDisk_Found> found;

    if (emu_game_find(file_path.c_str(), &found))
    {
        int game_target = target == "setup" ? GDOX_GAME_TARGET_SETUP : target == "prompt" ? GDOX_GAME_TARGET_PROMPT :
            target == "debug" ? GDOX_GAME_TARGET_DEBUG : GDOX_GAME_TARGET_GAME;

        if (!target.empty() && (target != "game") && (target != "setup") && (target != "prompt") && (target != "debug"))
            return {{"error", "target is game, setup, prompt or debug"}};

        if (!gui_start_game(found[0], game_target))
            return {{"error", std::string("Unable to open the game: ") + emu_game_get_error()}};

        return {{"file_path", file_path}};
    }

    if (!target.empty())
        return {{"error", "target only applies to a game descriptor"}};

    if (!gui_load_rom(file_path.c_str()))
        return {{"error", "Failed to load media file"}};

    return {{"file_path", file_path}};
}

json DebugAdapter::GetGameInfo()
{
    GameDisk* disk = emu_game_get_disk();

    if (!disk->IsOpen())
        return {{"open", false}};

    static const char* const k_targets[GDOX_GAME_TARGET_COUNT] = { "game", "setup", "prompt", "debug" };
    static const char* const k_sources[] = { "user", "game", "model", "media" };
    static const char* const k_keys[][2] = {
        { "game", "source" }, { "game", "folder" }, { "game", "free_mb" }, { "machine", "model" },
        { "machine", "cpu_mhz" }, { "machine", "turbo" }, { "machine", "ram_mb" }, { "machine", "fpu" },
        { "machine", "floppy_a_type" }, { "machine", "floppy_b_type" }, { "machine", "cdrom_speed" },
        { "machine", "drive_timing" }, { "machine", "video" }, { "sound", "card" }, { "sound", "irq" }, { "sound", "dma" },
        { "sound", "mpu" }, { "sound", "midi" }, { "sound", "midi_port" }, { "input", "mouse" },
        { "input", "joystick" }, { "dos", "memory" }, { "dos", "ems_kb" }, { "dos", "ems_frame" }, { "dos", "files" },
        { "dos", "buffers" }, { "dos", "version" }, { "dos", "system" }, { "dos", "system_files" }, { "dos", "cache" }, { "dos", "keyboard" },
        { "drives", "floppy_a" }, { "drives", "floppy_b" },
        { "drives", "hdd_primary_slave" }, { "drives", "hdd_secondary_slave" }, { "drives", "cdrom" }, { "run", "exe" },
        { "run", "args" }, { "run", "cwd" }, { "run", "setup" }, { "run", "on_exit" }, { "run", "boot" } };
    const GameDescriptor& descriptor = disk->GetDescriptor();
    const GameDescriptor& overrides = disk->GetOverrides();
    const GDOX_Machine_Config& config = m_core->GetMachineConfig();
    const GDOX_Machine_Report& report = m_core->GetMachineReport();
    json keys = json::object();

    for (size_t i = 0; i < sizeof(k_keys) / sizeof(k_keys[0]); i++)
    {
        if (!descriptor.Has(k_keys[i][0], k_keys[i][1]))
            continue;

        std::string name = std::string(k_keys[i][0]) + "." + k_keys[i][1];
        keys[name] = {{"value", descriptor.Get(k_keys[i][0], k_keys[i][1])},
            {"layer", overrides.Has(k_keys[i][0], k_keys[i][1]) ? "override" : "descriptor"}};
    }

    const GameDisk_Result& last = emu_game_get_last_write_back();
    json notes = json::array();

    for (size_t i = 0; i < last.notes.size(); i++)
        notes.push_back(last.notes[i]);

    return {
        {"open", emu_game_is_open()},
        {"name", disk->GetName()},
        {"descriptor_path", disk->GetDescriptorPath()},
        {"descriptor", descriptor.Write()},
        {"overrides_path", disk->GetOverridesPath()},
        {"keys", keys},
        {"machine", {{"model", k_machine_profiles[config.model].key}, {"model_source", k_sources[report.model]},
            {"cpu_mhz", config.cpu_clock_rate / 1000000}, {"cpu_source", k_sources[report.cpu_clock]},
            {"ram_mb", config.ram_size / (1024 * 1024)}, {"ram_source", k_sources[report.ram]},
            {"fpu", config.fpu}, {"fpu_source", k_sources[report.fpu]}, {"turbo", m_core->IsTurbo()},
            {"sound_card", k_sound_cards[config.sound_card].id}, {"sound_irq", config.sound_irq},
            {"sound_dma", config.sound_dma}, {"mpu", config.mpu}, {"video", k_video_cards[config.video_card].id},
            {"mouse", k_mice[config.mouse].id}, {"joystick", emu_game_get_joystick() >= 0 ?
                json(k_joysticks[emu_game_get_joystick()].id) : json()}, {"midi", emu_game_get_midi() >= 0 ?
                json(k_midi_synths[emu_game_get_midi()].id) : json()}, {"midi_port", emu_game_get_midi_port()}}},
        {"target", k_targets[emu_game_get_target()]},
        {"keyboard_layout", disk->GetKeyboardLayout()},
        {"system", {{"name", disk->GetSystemName()}, {"id", disk->GetSystem() == GDOX_GAME_SYSTEM_MSDOS ? "msdos" : "freedos"},
            {"layer", overrides.Has("dos", "system") ? "override" : descriptor.Has("dos", "system") ? "descriptor" :
                disk->HasMSDOS() ? "imported" : "default"}, {"msdos_imported", disk->HasMSDOS()}}},
        {"source", disk->GetSourcePath()},
        {"image", disk->GetImagePath()},
        {"built", disk->WasBuilt()},
        {"recovered", disk->WasRecovered()},
        {"refused_image", disk->GetRefusedPath()},
        {"left_out", disk->GetLeftOut()},
        {"saves_to", disk->IsWritingToOverlay() ? "save_folder" : "game_folder"},
        {"overlay", disk->GetOverlayPath()},
        {"session_open", disk->IsSessionOpen()},
        {"write_back_pending", emu_game_is_write_back_pending()},
        {"last_write_back", {{"done", last.done}, {"refused", last.refused}, {"written", last.written},
            {"deleted", last.deleted}, {"folders", last.directories}, {"conflicts", last.conflicts}, {"kept", last.kept},
            {"startup", last.startup}, {"notes", notes}}}
    };
}

bool DebugAdapter::IsMediaLoading() const
{
    return gui_is_rom_loading() && emu_is_media_loading();
}

json DebugAdapter::FinishLoadMedia(const std::string& file_path)
{
    if (gui_is_rom_loading() && !gui_finish_loading_rom())
        return {{"error", "Failed to load media file"}};

    if (emu_hard_disk_is_image(file_path.c_str()))
    {
        for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
        {
            Emu_HardDiskInfo info;
            emu_hard_disk_get_info(i, &info);

            if (strcmp(info.pending ? info.pending_path : info.path, file_path.c_str()) != 0)
                continue;

            json result = hard_disk_json(i);
            result["success"] = true;
            return result;
        }

        return {{"error", "Failed to attach the hard disk image"}};
    }

    if (emu_floppy_is_image(file_path.c_str()))
    {
        if (gui_floppy_is_pending())
            return {{"error", "The disk in drive 0 has unsaved changes, confirm the insert in the GUI or use insert_floppy"}};

        return {{"success", true}, {"file_path", file_path}, {"drive", 0}};
    }

    if (emu_game_is_open() && (strcmp(emu_game_get_disk()->GetDescriptorPath(), file_path.c_str()) == 0 ||
        emu_game_is_content(file_path.c_str())))
    {
        json result = GetGameInfo();
        result["success"] = true;
        result["file_path"] = file_path;
        return result;
    }

    if (emu_game_is_content(file_path.c_str()))
        return {{"error", std::string("Unable to open the game: ") + emu_game_get_error()}};

    if (!m_core->GetMedia()->IsReady())
        return {{"error", "Failed to load media file"}};

    return {{"success", true}, {"file_path", file_path}};
}

json DebugAdapter::ListSaveStateSlots()
{
    json result;
    json slots = json::array();
    json empty_slots = json::array();

    update_savestates_data();

    for (int i = 0; i < 5; i++)
    {
        json slot;
        slot["slot"] = i + 1;

        if (emu_savestates[i].magic == GDOX_SAVESTATE_MAGIC)
        {
            slot["rom_name"] = emu_savestates[i].rom_name;
            slot["timestamp"] = emu_savestates[i].timestamp;
            slot["version"] = emu_savestates[i].version;
            slot["valid"] = (emu_savestates[i].version >= GDOX_SAVESTATE_MIN_VERSION && emu_savestates[i].version <= GDOX_SAVESTATE_VERSION);
            slot["has_screenshot"] = IsValidPointer(emu_savestates_screenshots[i].data);

            if (emu_savestates[i].emu_build[0] != 0)
                slot["emu_build"] = emu_savestates[i].emu_build;

            slots.push_back(slot);
        }
        else
        {
            empty_slots.push_back(i + 1);
        }
    }

    result["current_slot"] = config_emulator.save_slot + 1;
    result["empty_slots"] = empty_slots;
    result["slots"] = slots;

    return result;
}

json DebugAdapter::SelectSaveStateSlot(int slot)
{
    json result;

    if (slot < 1 || slot > 5)
    {
        result["error"] = "Invalid slot number (must be 1-5)";
        Log("[MCP] SelectSaveStateSlot failed: Invalid slot %d", slot);
        return result;
    }

    config_emulator.save_slot = slot - 1;

    result["success"] = true;
    result["slot"] = slot;

    return result;
}

json DebugAdapter::SaveState()
{
    json result;

    if (!m_core || emu_is_empty())
    {
        result["error"] = "Emulator is powered off";
        Log("[MCP] SaveState failed: Emulator is powered off");
        return result;
    }

    int slot = config_emulator.save_slot + 1;
    emu_save_state_slot(slot);

    result["success"] = true;
    result["slot"] = slot;
    result["rom_name"] = m_core->GetMedia()->GetFileName();

    return result;
}

json DebugAdapter::LoadState()
{
    json result;

    if (!m_core || emu_is_empty())
    {
        result["error"] = "Emulator is powered off";
        Log("[MCP] LoadState failed: Emulator is powered off");
        return result;
    }

    int slot = config_emulator.save_slot + 1;

    update_savestates_data();

    if (emu_savestates[config_emulator.save_slot].magic != GDOX_SAVESTATE_MAGIC)
    {
        result["error"] = "Save state slot is empty";
        Log("[MCP] LoadState failed: Slot %d is empty", slot);
        return result;
    }

    if (!emu_load_state_slot(slot))
    {
        result["error"] = "Failed to load state";
        Log("[MCP] LoadState failed: Slot %d", slot);
        return result;
    }

    result["success"] = true;
    result["slot"] = slot;
    result["frame_restored"] = !emu_frame_stale;
    AddMediaStateChanges(result);

    return result;
}

json DebugAdapter::SaveStateFile(const std::string& file_path)
{
    json result;

    if (file_path.empty())
    {
        result["error"] = "File path is required";
        Log("[MCP] SaveStateFile failed: File path is required");
        return result;
    }

    if (!m_core || emu_is_empty())
    {
        result["error"] = "Emulator is powered off";
        Log("[MCP] SaveStateFile failed: Emulator is powered off");
        return result;
    }

    if (!emu_save_state_file(file_path.c_str()))
    {
        result["error"] = "Failed to save state file";
        Log("[MCP] SaveStateFile failed: %s", file_path.c_str());
        return result;
    }

    result["success"] = true;
    result["file_path"] = file_path;

    return result;
}

json DebugAdapter::LoadStateFile(const std::string& file_path)
{
    json result;

    if (file_path.empty())
    {
        result["error"] = "File path is required";
        Log("[MCP] LoadStateFile failed: File path is required");
        return result;
    }

    if (!m_core || emu_is_empty())
    {
        result["error"] = "Emulator is powered off";
        Log("[MCP] LoadStateFile failed: Emulator is powered off");
        return result;
    }

    if (!emu_load_state_file(file_path.c_str()))
    {
        result["error"] = "Failed to load state file";
        Log("[MCP] LoadStateFile failed: %s", file_path.c_str());
        return result;
    }

    result["success"] = true;
    result["file_path"] = file_path;
    result["frame_restored"] = !emu_frame_stale;
    AddMediaStateChanges(result);

    return result;
}

void DebugAdapter::AddMediaStateChanges(json& result)
{
    u32 changes = m_core->GetHardDiskStateChanges();

    if (changes == 0)
        return;

    json ids = json::array();

    for (int i = 0; i < GDOX_MAX_HARD_DISKS; i++)
    {
        if (IS_SET_BIT(changes, i))
            ids.push_back(i);
    }

    result["hard_disks_changed"] = ids;
}

static const char* const k_joystick_button_names[] = { "1", "2", "3", "4" };
static const char* const k_joystick_hat_names[] = { "up", "down", "left", "right" };

static const struct
{
    const char* name;
    u8 hat;
} k_joystick_hat_directions[] =
{
    { "none", 0 },
    { "up", GDOX_JOYSTICK_HAT_UP },
    { "down", GDOX_JOYSTICK_HAT_DOWN },
    { "left", GDOX_JOYSTICK_HAT_LEFT },
    { "right", GDOX_JOYSTICK_HAT_RIGHT },
    { "up_left", GDOX_JOYSTICK_HAT_UP | GDOX_JOYSTICK_HAT_LEFT },
    { "up_right", GDOX_JOYSTICK_HAT_UP | GDOX_JOYSTICK_HAT_RIGHT },
    { "down_left", GDOX_JOYSTICK_HAT_DOWN | GDOX_JOYSTICK_HAT_LEFT },
    { "down_right", GDOX_JOYSTICK_HAT_DOWN | GDOX_JOYSTICK_HAT_RIGHT }
};

static const char* const k_joystick_axis_names[] = { "ax", "ay", "bx", "by" };
static const char* const k_joystick_control_names[] = { "1", "2", "3", "4", "up", "down", "left", "right", "hat_up",
    "hat_down", "hat_left", "hat_right" };
static const int k_joystick_control_count = sizeof(k_joystick_control_names) / sizeof(k_joystick_control_names[0]);

json DebugAdapter::JoystickPressed(u8 buttons, u8 hat) const
{
    json result = {{"buttons", json::array()}, {"hat", json::array()}};

    for (int i = 0; i < 4; i++)
    {
        if (IS_SET_BIT(buttons, i))
            result["buttons"].push_back(k_joystick_button_names[i]);

        if (IS_SET_BIT(hat, i))
            result["hat"].push_back(k_joystick_hat_names[i]);
    }

    return result;
}

json DebugAdapter::JoystickSet(const json& arguments)
{
    Input* input = m_core->GetInput();
    bool reset = arguments.value("reset", false);
    std::string action = arguments.value("action", "press");
    bool has_axes = false;
    bool has_buttons = arguments.contains("buttons");
    bool has_hat = arguments.contains("hat");
    s32 axes[GDOX_JOYSTICK_AXIS_COUNT] = {};
    u8 buttons = 0;
    u8 hat = 0;
    bool hat_none = false;
    int device = -1;

    if (action != "press" && action != "release" && action != "press_and_release")
        return {{"error", "Invalid action"}};

    for (int i = 0; i < GDOX_JOYSTICK_AXIS_COUNT; i++)
    {
        if (!arguments.contains(k_joystick_axis_names[i]))
            continue;

        s64 value = arguments[k_joystick_axis_names[i]].get<s64>();

        if (value < -32768 || value > 32767)
            return {{"error", std::string(k_joystick_axis_names[i]) + " must be -32768 to 32767"}};

        axes[i] = (s32)value;
        has_axes = true;
    }

    if (has_buttons)
    {
        const json& list = arguments["buttons"];

        for (json::const_iterator it = list.begin(); it != list.end(); ++it)
        {
            int number = it->get<int>();

            if (number < 1 || number > 4)
                return {{"error", "Joystick buttons are 1 to 4"}};

            buttons |= (u8)(1 << (number - 1));
        }
    }

    if (has_hat)
    {
        std::string name = to_lower(arguments["hat"].get<std::string>());
        bool found = false;

        for (size_t i = 0; i < sizeof(k_joystick_hat_directions) / sizeof(k_joystick_hat_directions[0]); i++)
        {
            if (name == k_joystick_hat_directions[i].name)
            {
                hat = k_joystick_hat_directions[i].hat;
                hat_none = hat == 0;
                found = true;
            }
        }

        if (!found)
            return {{"error", "Invalid hat (must be: none, up, down, left, right, up_left, up_right, down_left, "
                "down_right)"}};
    }

    if (arguments.contains("device"))
    {
        std::string name = to_lower(arguments["device"].get<std::string>());

        for (int i = 0; i < GDOX_JOYSTICK_TYPE_COUNT; i++)
        {
            if (name == k_joysticks[i].id)
                device = i;
        }

        if (device < 0)
            return {{"error", "Invalid device (must be: none, standard, gamepad, flightstick)"}};
    }

    json result = {{"success", true}};

    if (device >= 0)
    {
        config_input.joystick_type = device;
        emu_update_joystick_type();
        result["device"] = k_joysticks[input->GetJoystickType()].id;
    }

    if (reset)
    {
        memset(&m_joystick, 0, sizeof(m_joystick));
        m_joystick_axes = false;
        m_joystick_directions = 0;
        result["reset"] = true;
    }

    if (has_axes)
    {
        for (int i = 0; i < GDOX_JOYSTICK_AXIS_COUNT; i++)
        {
            if (arguments.contains(k_joystick_axis_names[i]))
                m_joystick.axes[i] = (s16)axes[i];
        }

        m_joystick_axes = true;
        m_joystick_directions = 0;
    }

    bool delayed_release = false;

    if (has_buttons || has_hat)
    {
        if (action == "release")
        {
            m_joystick.buttons &= (u8)~buttons;
            m_joystick.hat = hat_none ? 0 : (u8)(m_joystick.hat & ~hat);
        }
        else
        {
            m_joystick.buttons |= buttons;
            m_joystick.hat = hat_none ? 0 : (u8)(m_joystick.hat | hat);
            delayed_release = action == "press_and_release" && (buttons != 0 || hat != 0);
        }

        result["action"] = action;
    }

    ApplyJoystickState();

    if (delayed_release)
    {
        result["__delayed_release"] = true;
        result["__release_buttons"] = buttons;
        result["__release_hat"] = hat;
    }

    json state = JoystickPressed(m_joystick.buttons, m_joystick.hat);
    json injected_axes;

    for (int i = 0; i < GDOX_JOYSTICK_AXIS_COUNT; i++)
        injected_axes[k_joystick_axis_names[i]] = m_joystick.axes[i];

    result["injected"] = {{"axes", m_joystick_axes ? injected_axes : json()}, {"buttons", state["buttons"]},
        {"hat", state["hat"]}};
    return result;
}

bool DebugAdapter::HasJoystick() const
{
    return m_core->GetInput()->GetJoystickType() != GDOX_JOYSTICK_NONE;
}

int DebugAdapter::FindJoystickControl(const std::string& name) const
{
    std::string lower = to_lower(name);

    for (int i = 0; i < k_joystick_control_count; i++)
    {
        if (lower == k_joystick_control_names[i])
            return i;
    }

    return -1;
}

void DebugAdapter::SetJoystickControl(int control, bool pressed)
{
    if (control < 4)
    {
        u8 mask = (u8)(1 << control);
        m_joystick.buttons = pressed ? (u8)(m_joystick.buttons | mask) : (u8)(m_joystick.buttons & ~mask);
    }
    else if (control < 8)
    {
        u8 mask = (u8)(1 << (control - 4));
        m_joystick_directions = pressed ? (u8)(m_joystick_directions | mask) : (u8)(m_joystick_directions & ~mask);
        u8 vertical = m_joystick_directions & 0x03;
        u8 horizontal = (m_joystick_directions >> 2) & 0x03;
        m_joystick.axes[0] = horizontal == 0x01 ? -32768 : (horizontal == 0x02 ? 32767 : 0);
        m_joystick.axes[1] = vertical == 0x01 ? -32768 : (vertical == 0x02 ? 32767 : 0);
        m_joystick_axes = true;
    }
    else
    {
        u8 hat = k_joystick_hat_directions[control - 7].hat;
        m_joystick.hat = pressed ? (u8)(m_joystick.hat | hat) : (u8)(m_joystick.hat & ~hat);
    }

    ApplyJoystickState();
}

void DebugAdapter::ReleaseJoystick(u8 buttons, u8 hat)
{
    m_joystick.buttons &= (u8)~buttons;
    m_joystick.hat &= (u8)~hat;
    ApplyJoystickState();
}

void DebugAdapter::ApplyJoystickState()
{
    m_core->GetInput()->SetInjectedJoystickState(m_joystick, m_joystick_axes);
}

bool DebugAdapter::HasMouse() const
{
    return m_core->GetMachineConfig().mouse != GDOX_MOUSE_NONE;
}

bool DebugAdapter::ApplyMouseMotion(int delta_x, int delta_y)
{
    if (!HasMouse())
        return false;

    if (delta_x != 0 || delta_y != 0)
        emu_set_mouse_delta(delta_x, delta_y);

    return true;
}

bool DebugAdapter::IsMouseMotionPending() const
{
    Input::Input_State* state = m_core->GetInput()->GetState();
    return (state->mouse_x != 0) || (state->mouse_y != 0);
}

void DebugAdapter::SetMouseButton(u8 mask, bool pressed)
{
    if (pressed)
        m_mouse_buttons |= mask;
    else
        m_mouse_buttons &= (u8)~mask;

    m_core->GetInput()->SetInjectedMouseButtons(m_mouse_buttons);
}

static const int k_mcp_mouse_max_changed = 16384;
static const size_t k_mcp_mouse_max_search = 40000000;
static const int k_mcp_mouse_min_pixels = 4;
static const int k_mcp_mouse_min_size = 3;
static const double k_mcp_mouse_min_scale = 0.125;
static const double k_mcp_mouse_max_scale = 8.0;

static bool IsMovedPointerPixel(const u32* before, const u32* out, int width, int height, int index, int shift_x, int shift_y)
{
    int x = (index % width) + shift_x;
    int y = (index / width) + shift_y;

    if ((x < 0) || (y < 0) || (x >= width) || (y >= height))
        return false;

    int moved = (y * width) + x;
    return (out[moved] == before[index]) && (out[moved] != before[moved]);
}

static void FindPointerShift(const u32* before, const u32* out, int width, int height, const std::vector<int>& changed,
    int counts_x, int counts_y, int& shift_x, int& shift_y)
{
    int range_x = (abs(counts_x) * 2) + 8;
    int range_y = (abs(counts_y) * 2) + 8;
    int sign_x = counts_x < 0 ? -1 : 1;
    int sign_y = counts_y < 0 ? -1 : 1;
    size_t stride = 1 + ((changed.size() * (size_t)(range_x + 1) * (size_t)(range_y + 1)) / k_mcp_mouse_max_search);
    int best = 0;
    shift_x = 0;
    shift_y = 0;

    for (int y = 0; y <= range_y; y++)
    {
        for (int x = 0; x <= range_x; x++)
        {
            if ((x == 0) && (y == 0))
                continue;

            int score = 0;

            for (size_t i = 0; i < changed.size(); i += stride)
            {
                if (IsMovedPointerPixel(before, out, width, height, changed[i], x * sign_x, y * sign_y))
                    score++;
            }

            if (score > best)
            {
                best = score;
                shift_x = x * sign_x;
                shift_y = y * sign_y;
            }
        }
    }
}

bool DebugAdapter::FindMousePointer(const u32* before, const u32* out, const u32* back, int width, int height,
    int counts_x, int counts_y, McpMouseMiss& miss, std::string& reason)
{
    miss = MCP_MOUSE_MISS_UNSEEN;
    std::vector<int> changed;

    for (int i = 0; i < width * height; i++)
    {
        if ((before[i] != out[i]) && (before[i] == back[i]))
            changed.push_back(i);
    }

    if (changed.empty())
    {
        reason = "nothing on screen moved and came back";
        return false;
    }

    if ((int)changed.size() > k_mcp_mouse_max_changed)
    {
        reason = std::to_string(changed.size()) + " pixels changed, too many for a pointer";
        return false;
    }

    int shift_x = 0;
    int shift_y = 0;
    FindPointerShift(before, out, width, height, changed, counts_x, counts_y, shift_x, shift_y);

    std::vector<int> pointer;
    int left = width;
    int top = height;
    int right = -1;
    int bottom = -1;

    for (size_t i = 0; i < changed.size(); i++)
    {
        if (!IsMovedPointerPixel(before, out, width, height, changed[i], shift_x, shift_y))
            continue;

        int x = changed[i] % width;
        int y = changed[i] / width;
        pointer.push_back(changed[i]);
        left = MIN(left, x);
        top = MIN(top, y);
        right = MAX(right, x);
        bottom = MAX(bottom, y);
    }

    if ((int)pointer.size() < k_mcp_mouse_min_pixels)
    {
        reason = "no group of pixels moved together";
        return false;
    }

    int pointer_width = right - left + 1;
    int pointer_height = bottom - top + 1;
    std::string box = std::to_string(pointer_width) + "x" + std::to_string(pointer_height) + " at " +
        std::to_string(left) + "," + std::to_string(top);

    if ((abs(shift_x) < pointer_width) && (abs(shift_y) < pointer_height))
    {
        miss = MCP_MOUSE_MISS_OVERLAP;
        reason = "the " + box + " pointer moved less than its size";
        return false;
    }

    miss = MCP_MOUSE_MISS_REJECTED;
    bool small_x = (pointer_width < k_mcp_mouse_min_size) && (left > 0) && (right < (width - 1));
    bool small_y = (pointer_height < k_mcp_mouse_min_size) && (top > 0) && (bottom < (height - 1));

    if (small_x || small_y)
    {
        reason = "only a " + box + " box moved, too small for a pointer";
        return false;
    }

    double gain_x = (double)shift_x / counts_x;
    double gain_y = (double)shift_y / counts_y;
    bool edge_x = counts_x > 0 ? ((right + shift_x) >= (width - 1)) : ((left + shift_x) <= 0);
    bool edge_y = counts_y > 0 ? ((bottom + shift_y) >= (height - 1)) : ((top + shift_y) <= 0);
    bool reliable_x = !edge_x && (gain_x >= k_mcp_mouse_min_scale) && (gain_x <= k_mcp_mouse_max_scale);
    bool reliable_y = !edge_y && (gain_y >= k_mcp_mouse_min_scale) && (gain_y <= k_mcp_mouse_max_scale);

    if (!reliable_x && !reliable_y)
    {
        if (edge_x || edge_y)
            reason = "the " + box + " pointer stopped at the screen edge";
        else
            reason = "the " + box + " box moved " + std::to_string(shift_x) + "," + std::to_string(shift_y) +
                " pixels for " + std::to_string(counts_x) + "," + std::to_string(counts_y) +
                " counts, out of range for a pointer";

        return false;
    }

    McpMousePointer& result = m_mouse_pointer;
    bool known = IsMousePointerKnown();

    result.pixels_per_count_x = reliable_x ? gain_x : (known ? result.pixels_per_count_x : gain_y);
    result.pixels_per_count_y = reliable_y ? gain_y : (known ? result.pixels_per_count_y : gain_x);
    result.valid = true;
    result.x = left;
    result.y = top;
    result.width = pointer_width;
    result.height = pointer_height;
    result.screen_width = width;
    result.screen_height = height;
    result.frame = emu_frame_counter;
    result.generation = emu_machine_generation;
    result.estimated = false;
    result.pixels.assign((size_t)pointer_width * pointer_height, 0);
    result.mask.assign((size_t)pointer_width * pointer_height, 0);

    for (size_t i = 0; i < pointer.size(); i++)
    {
        int offset = (((pointer[i] / width) - top) * pointer_width) + (pointer[i] % width) - left;
        result.mask[offset] = 1;
        result.pixels[offset] = before[pointer[i]];
    }

    miss = MCP_MOUSE_MISS_NONE;
    return true;
}

const McpMousePointer& DebugAdapter::GetMousePointer() const
{
    return m_mouse_pointer;
}

bool DebugAdapter::IsMousePointerKnown() const
{
    const McpMousePointer& pointer = m_mouse_pointer;
    GDOX_Runtime_Info runtime;
    emu_get_runtime(runtime);

    return pointer.valid && (pointer.generation == emu_machine_generation) &&
        (pointer.screen_width == runtime.screen_width) && (pointer.screen_height == runtime.screen_height);
}

void DebugAdapter::MoveMousePointerEstimate(int counts_x, int counts_y)
{
    if (!IsMousePointerKnown())
        return;

    McpMousePointer& pointer = m_mouse_pointer;
    int x = pointer.x + (int)floor((counts_x * pointer.pixels_per_count_x) + 0.5);
    int y = pointer.y + (int)floor((counts_y * pointer.pixels_per_count_y) + 0.5);

    pointer.x = CLAMP(x, 0, pointer.screen_width - 1);
    pointer.y = CLAMP(y, 0, pointer.screen_height - 1);
    pointer.estimated = true;
}

void DebugAdapter::ForgetMousePointer()
{
    m_mouse_pointer.valid = false;
}

bool DebugAdapter::IsMouseButtonHeld() const
{
    return m_mouse_buttons != 0;
}

json DebugAdapter::MouseGetCursor()
{
    if (!m_core || emu_is_empty())
        return {{"error", "Emulator is powered off"}};

    if (!HasMouse())
        return {{"error", "The machine has no mouse, set Mouse in the machine configuration"}};

    const McpMousePointer& pointer = m_mouse_pointer;
    json result = {{"success", true}};

    if (!IsMousePointerKnown())
    {
        result["known"] = false;
        return result;
    }

    const u32* frame = (const u32*)emu_frame_buffer;
    int total = 0;
    int matched = 0;

    if (IsValidPointer(frame))
    {
        for (int y = 0; y < pointer.height; y++)
        {
            for (int x = 0; x < pointer.width; x++)
            {
                int offset = (y * pointer.width) + x;

                if (!pointer.mask[offset])
                    continue;

                total++;

                if (frame[((pointer.y + y) * pointer.screen_width) + pointer.x + x] == pointer.pixels[offset])
                    matched++;
            }
        }
    }

    result["known"] = true;
    result["x"] = pointer.x;
    result["y"] = pointer.y;
    result["box"] = {{"x", pointer.x}, {"y", pointer.y}, {"width", pointer.width}, {"height", pointer.height}};
    result["pixels_per_count"] = {{"x", pointer.pixels_per_count_x}, {"y", pointer.pixels_per_count_y}};
    result["measured_frames_ago"] = emu_frame_counter - pointer.frame;
    result["visible"] = (total > 0) && ((matched * 10) >= (total * 9));

    if (pointer.estimated)
        result["estimated"] = true;

    return result;
}

bool DebugAdapter::GetKeyCode(const std::string& name, GDOX_Keys& key) const
{
    std::string upper = to_upper(name);

    if (upper == "ENTER")
        upper = "RETURN";
    else if (upper == "ESC")
        upper = "ESCAPE";

    for (int i = 0; i < k_debug_key_name_count; i++)
    {
        if (upper == k_debug_key_names[i].name)
        {
            key = k_debug_key_names[i].key;
            return true;
        }
    }

    return false;
}

json DebugAdapter::KeyboardKey(const std::string& key, const std::string& action)
{
    GDOX_Keys code = GDOX_KEY_NONE;

    if (!GetKeyCode(key, code))
        return {{"error", "Invalid key name '" + key + "'"}};

    if (action != "press" && action != "release" && action != "press_and_release")
        return {{"error", "Invalid action"}};

    json result = {{"success", true}, {"key", gui_debug_key_name(code)}, {"action", action}};

    if (action == "release")
    {
        ReleaseKey(code);
        return result;
    }

    bool held = PressKey(code);
    result["dropped"] = !held;

    if (!held && !m_core->GetKeyboard()->GetState()->scanning)
        result["reason"] = "The keyboard is not scanning: the software or the 8042 disabled it";
    else if (!held)
        result["reason"] = "The keyboard buffer is full: nothing has read port 60h, so it sent an overrun instead";
    else if (action == "press_and_release")
        result["__delayed_key_release"] = true;

    return result;
}

bool DebugAdapter::PressKey(GDOX_Keys key)
{
    bool held = emu_key_pressed(key);
    m_keys[key] = m_keys[key] || held;
    return held;
}

void DebugAdapter::ReleaseKey(GDOX_Keys key)
{
    m_keys[key] = false;
    emu_key_released(key);
}

void DebugAdapter::ReleaseKeys()
{
    for (int i = 0; i < GDOX_KEY_COUNT; i++)
    {
        if (m_keys[i])
            ReleaseKey((GDOX_Keys)i);
    }
}

static json mouse_button_names(u8 buttons)
{
    json names = json::array();

    if ((buttons & GDOX_MOUSE_LEFT) != 0)
        names.push_back("left");

    if ((buttons & GDOX_MOUSE_RIGHT) != 0)
        names.push_back("right");

    if ((buttons & GDOX_MOUSE_MIDDLE) != 0)
        names.push_back("middle");

    return names;
}

static json hex_bytes(const u8* bytes, int count)
{
    json list = json::array();
    char text[8];

    for (int i = 0; i < count; i++)
    {
        snprintf(text, sizeof(text), "%02X", bytes[i]);
        list.push_back(text);
    }

    return list;
}

json DebugAdapter::GetMouseState()
{
    Input* input = m_core->GetInput();
    Input::Input_State* host = input->GetState();
    int type = m_core->GetMachineConfig().mouse;

    if (type < 0 || type >= GDOX_MOUSE_TYPE_COUNT)
        type = GDOX_MOUSE_NONE;

    json result = {{"type", k_mice[type].id}, {"name", k_mice[type].name}};

    if (type == GDOX_MOUSE_NONE)
        return result;

    result["host"] = {{"pending_x", host->mouse_x}, {"pending_y", host->mouse_y},
        {"buttons", mouse_button_names(input->GetMouseButtons())}};
    result["injected_buttons"] = mouse_button_names(m_mouse_buttons);

    if (type == GDOX_MOUSE_PS2)
    {
        PS2Mouse::PS2Mouse_State* mouse = m_core->GetPS2Mouse()->GetState();
        const char* mode = mouse->wrap ? "wrap" : (mouse->remote ? "remote" : "stream");

        result["ps2"] = {
            {"mode", mode},
            {"reporting", mouse->enabled},
            {"rate", mouse->rate},
            {"resolution", mouse->resolution},
            {"counts_per_mm", 1 << mouse->resolution},
            {"scaling_2_1", mouse->scaling},
            {"clock_enabled", mouse->clock_enabled},
            {"buttons", mouse_button_names(mouse->buttons)},
            {"accumulated", {{"x", mouse->x}, {"y", mouse->y}}},
            {"queued_bytes", hex_bytes(mouse->reply, mouse->reply_count)},
            {"last_packet", hex_bytes(mouse->last, mouse->last_count)},
            {"receiving", m_core->GetPS2Mouse()->IsReceiving()},
            {"resetting", mouse->resetting}
        };
    }
    else
    {
        SerialMouse::SerialMouse_State* mouse = m_core->GetSerialPorts()->GetMouse()->GetState();

        result["serial"] = {
            {"port", "COM1"},
            {"rts", mouse->rts},
            {"ready", mouse->ready},
            {"buttons", mouse_button_names(mouse->buttons)},
            {"queued_bytes", hex_bytes(mouse->queue, mouse->queue_count)},
            {"identification_pending", mouse->ident_clocks != GDOX_NO_EVENT}
        };
    }

    return result;
}

json DebugAdapter::GetJoystickState()
{
    Input* input = m_core->GetInput();
    GamePort* game_port = m_core->GetGamePort();
    GamePort::GamePort_State* port = game_port->GetState();
    const GDOX_Joystick_State& state = input->GetJoystickState();
    int type = input->GetJoystickType();
    u64 clocks = m_core->GetScheduler()->GetClocks();
    u8 value = game_port->Peek(clocks);
    json axes = json::object();
    json pulses = json::array();
    json pressed = JoystickPressed(state.buttons, state.hat);

    if (type < 0 || type >= GDOX_JOYSTICK_TYPE_COUNT)
        type = GDOX_JOYSTICK_NONE;

    for (int i = 0; i < GDOX_JOYSTICK_AXIS_COUNT; i++)
    {
        axes[k_joystick_axis_names[i]] = state.axes[i];

        json pulse = {{"axis", k_joystick_axis_names[i]}, {"bit", i}, {"high", IS_SET_BIT(value, i)}};

        if (port->pulse_end[i] == GDOX_NO_EVENT)
            pulse["pulse_us"] = "open";
        else if (port->pulse_end[i] == 0)
            pulse["pulse_us"] = json();
        else
            pulse["pulse_us"] = (double)((port->pulse_end[i] - port->fire_clocks) * 10000000 /
                GDOX_MACHINE_CLOCK_RATE) / 10.0;

        pulses.push_back(pulse);
    }

    json injected_axes;

    for (int i = 0; i < GDOX_JOYSTICK_AXIS_COUNT; i++)
        injected_axes[k_joystick_axis_names[i]] = m_joystick.axes[i];

    json injected = JoystickPressed(m_joystick.buttons, m_joystick.hat);
    injected["axes"] = m_joystick_axes ? injected_axes : json();

    return {
        {"type", k_joysticks[type].id},
        {"name", k_joysticks[type].name},
        {"axes", axes},
        {"buttons", pressed["buttons"]},
        {"hat", pressed["hat"]},
        {"injected", injected},
        {"game_port", {
            {"port", "0201"},
            {"value", Hex(value, 2)},
            {"switches", Hex(value >> 4, 1)},
            {"fired", port->fire_clocks != 0},
            {"fire_age_us", port->fire_clocks != 0 ?
                json((double)((clocks - port->fire_clocks) * 10000000 / GDOX_MACHINE_CLOCK_RATE) / 10.0) : json()},
            {"pulses", pulses}
        }}
    };
}

static json key_map_layer(KeyMap* key_map, GDOX_Key_Map_Layer layer)
{
    json entries = json::object();

    for (int usage = 0; usage < GDOX_HOST_KEY_COUNT; usage++)
    {
        const GDOX_Key_Map_Entry& entry = key_map->GetLayerEntry(layer, (u16)usage);
        const char* name = KeyMap::GetHostKeyName((u16)usage);

        if (entry.set && IsValidPointer(name))
            entries[name] = KeyMap::FormatTargets(entry);
    }

    return entries;
}

json DebugAdapter::GetKeyMap()
{
    KeyMap* key_map = m_core->GetKeyMap();
    bool reached[GDOX_KEY_COUNT] = { };
    json remapped = json::object();

    for (int usage = 0; usage < GDOX_HOST_KEY_COUNT; usage++)
    {
        GDOX_Key_Map_Layer layer = GDOX_KEY_MAP_BUILT_IN;
        const GDOX_Key_Map_Entry& entry = key_map->GetEntry((u16)usage, &layer);
        const char* name = KeyMap::GetHostKeyName((u16)usage);

        for (int i = 0; i < entry.count; i++)
            reached[entry.keys[i]] = true;

        if ((layer != GDOX_KEY_MAP_BUILT_IN) && IsValidPointer(name))
            remapped[name] = KeyMap::FormatTargets(entry);
    }

    json unreachable = json::array();

    for (int key = GDOX_KEY_NONE + 1; key < GDOX_KEY_COUNT; key++)
    {
        const char* name = KeyMap::GetKeyName((GDOX_Keys)key);

        if (IsValidPointer(name) && !reached[key])
            unreachable.push_back(name);
    }

    json result;
    result["enabled"] = key_map->IsEnabled();
    result["mapper_open"] = m_core->GetKeyMapper()->IsOpen();
    result["file"] = emu_key_map_get_path();
    result["game"] = key_map_layer(key_map, GDOX_KEY_MAP_GAME);
    result["user"] = key_map_layer(key_map, GDOX_KEY_MAP_USER);
    result["remapped"] = remapped;
    result["unreachable"] = unreachable;
    return result;
}

json DebugAdapter::SetKeyBinding(const std::string& host, const std::string& keys)
{
    KeyMap* key_map = m_core->GetKeyMap();
    u16 usage = 0;
    GDOX_Key_Map_Entry entry;
    std::string error;

    if (keys == "default")
    {
        int found = KeyMap::FindHostKey(host.c_str());

        if (found < 0)
            return {{"error", "Unknown host key " + host}};

        GDOX_Key_Map_Entry empty;
        memset(&empty, 0, sizeof(empty));
        key_map->SetEntry(GDOX_KEY_MAP_USER, (u16)found, empty);
        key_map->SetChanged(true);
        usage = (u16)found;
    }
    else
    {
        if (!KeyMap::ParseEntry(host, keys, &usage, &entry, &error))
            return {{"error", error}};

        GDOX_Keys targets[GDOX_KEY_MAP_TARGETS];

        for (int i = 0; i < entry.count; i++)
            targets[i] = (GDOX_Keys)entry.keys[i];

        key_map->Bind(usage, targets, entry.count);
    }

    emu_key_map_save();
    return {{"success", true}, {"host", KeyMap::GetHostKeyName(usage)},
        {"keys", KeyMap::FormatTargets(key_map->GetEntry(usage))}, {"file", emu_key_map_get_path()}};
}

json DebugAdapter::SetKeyMapping(bool enabled)
{
    if (m_core->GetKeyMap()->IsEnabled() != enabled)
        emu_key_mapping_toggle();

    return {{"success", true}, {"enabled", m_core->GetKeyMap()->IsEnabled()}};
}

json DebugAdapter::HostKey(const std::string& key, const std::string& action)
{
    int usage = KeyMap::FindHostKey(to_lower(key).c_str());

    if (usage < 0)
        return {{"error", "Unknown host key '" + key + "'"}};

    bool delayed_release = false;

    if (action == "press")
        emu_host_key_pressed(usage);
    else if (action == "release")
        emu_host_key_released(usage);
    else if (action == "press_and_release")
    {
        emu_host_key_pressed(usage);
        delayed_release = true;
    }
    else
        return {{"error", "Invalid action"}};

    json result = {{"success", true}, {"key", KeyMap::GetHostKeyName((u16)usage)}, {"action", action},
        {"drives", KeyMap::FormatTargets(m_core->GetKeyMap()->GetEntry((u16)usage))}};

    if (delayed_release)
        result["__delayed_host_release"] = usage;

    return result;
}

json DebugAdapter::SetKeyMapperOpen(bool open)
{
    if (emu_is_empty())
        return {{"error", "Emulator is powered off"}};

    if (emu_key_mapper_is_open() != open)
        emu_key_mapper_toggle();

    return {{"success", true}, {"open", emu_key_mapper_is_open()}};
}

json DebugAdapter::GetInputState()
{
    return {{"mouse", GetMouseState()}, {"joystick", GetJoystickState()}};
}

json DebugAdapter::GetRewindStatus()
{
    json result;
    result["enabled"] = config_rewind.enabled;
    result["debug_mode"] = config_debug.debug;
    result["snapshot_count"] = rewind_get_snapshot_count();
    result["frames_per_snapshot"] = rewind_get_frames_per_snapshot();
    result["recorded_seconds"] = rewind_get_recorded_seconds();
    result["memory_usage"] = (u64)rewind_get_memory_usage();
    result["memory_budget"] = (u64)rewind_get_memory_budget();
    return result;
}

json DebugAdapter::RewindSeek(int snapshot)
{
    bool paused = emu_is_paused() || emu_is_debug_idle();

    if (!paused)
        return {{"error", "Pause the emulator before seeking the rewind buffer"}};

    int count = rewind_get_snapshot_count();

    if (count == 0)
        return {{"error", "No rewind snapshots available"}};

    if (snapshot < 1 || snapshot > count)
        return {{"error", "Snapshot out of range (1-" + std::to_string(count) + ")"}};

    int age = count - snapshot;

    if (!gui_debug_rewind_seek(age))
        return {{"error", "Failed to load snapshot"}};

    I386* cpu = m_core->GetI386();
    I386_Debug_State state;
    cpu->CopyDebugState(state);

    json result;
    result["success"] = true;
    result["snapshot"] = snapshot;
    result["total"] = count;
    result["age_seconds"] = rewind_get_age_seconds(age);
    result["cs"] = Hex(state.segment[I386_SEGMENT_CS].selector, 4);
    result["eip"] = Hex32(state.eip);
    result["linear_pc"] = Hex32(cpu->GetCurrentLinearPC());
    AddMediaStateChanges(result);
    return result;
}

void DebugAdapter::ClearInjectedInput()
{
    m_mouse_buttons = 0;
    m_core->GetInput()->SetInjectedMouseButtons(0);
    memset(&m_joystick, 0, sizeof(m_joystick));
    m_joystick_axes = false;
    m_joystick_directions = 0;
    ApplyJoystickState();
    ReleaseKeys();
}

std::string DebugAdapter::Hex(u32 value, int digits) const
{
    std::ostringstream stream;

    stream << std::hex << std::uppercase << std::setfill('0') << std::setw(digits) << value;
    return stream.str();
}

std::string DebugAdapter::Hex32(u32 value) const
{
    return Hex(value, 8);
}

std::string DebugAdapter::FormatAreaOffset(const GuiDebugMemoryArea& area, u32 offset) const
{
    int digits = 1;
    u64 last = area.size > 0 ? area.size - 1 : 0;

    while (last >>= 4)
        digits++;

    return Hex(offset, MAX(digits, 4));
}

bool DebugAdapter::ParseAreaOffset(const GuiDebugMemoryArea& area, const std::string& text, u32& offset, std::string& error)
{
    if (area.source.space == GDOX_DEBUG_MEMORY_LINEAR)
    {
        McpAddress address;

        if (!ParseAddress(text, address, error))
            return false;

        offset = address.linear;
        return true;
    }

    if (!parse_hex_text(text, offset))
    {
        error = "Invalid offset format '" + text + "'";
        return false;
    }

    return true;
}

bool DebugAdapter::GetAreaRange(const GuiDebugMemoryArea& area, const json& arguments, u32& start, u32& size, std::string& error)
{
    const u64 max_size = 0x04000000;
    start = 0;

    if (arguments.contains("start"))
    {
        if (!arguments["start"].is_string() || !ParseAreaOffset(area, arguments["start"].get<std::string>(), start, error))
        {
            if (error.empty())
                error = "start must be a hex string";

            return false;
        }
    }

    u64 range = area.size - MIN((u64)start, area.size);

    if (arguments.contains("size"))
    {
        u32 requested = 0;

        if (!arguments["size"].is_string() || !parse_hex_text(arguments["size"].get<std::string>(), requested))
        {
            error = "size must be a hex string";
            return false;
        }

        range = MIN((u64)requested, range);
    }

    if (range == 0 || range > max_size)
    {
        error = "Give start and size (hex, at most 4000000) for areas larger than 64 MB";
        return false;
    }

    size = (u32)range;
    return true;
}

std::string DebugAdapter::FormatLogical(u16 selector, u32 offset, u32 linear)
{
    I386_Disassembler_Record* record = m_core->GetI386()->GetDisassemblerRecord(linear);
    const I386_Segment& cs = m_core->GetI386()->GetState()->segments[I386_SEGMENT_CS];
    bool code32 = true;

    if (IsValidPointer(record) && record->size > 0)
        code32 = record->default32;
    else if (selector == cs.selector)
        code32 = (cs.attributes & I386_SEGMENT_DEFAULT_32) != 0;

    return Hex(selector, 4) + ":" + Hex(offset, code32 ? 8 : 4);
}

const char* DebugAdapter::GetSymbolAt(u32 linear)
{
    return gui_debug_get_symbol(linear);
}
