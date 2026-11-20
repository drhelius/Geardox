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


#include "i8042.h"
#include "keyboard.h"
#include "ps2_mouse.h"
#include "../system/pic.h"
#include "../system/scheduler.h"
#include "../system/system_control.h"
#include "../common/trace_logger.h"
#include "../common/state_serializer.h"

// Set 2 to set 1, as in the 8042 firmware. An F0h prefix sets bit 7 of the next code
static const u8 k_i8042_translate[256] =
{
    0xFF, 0x43, 0x41, 0x3F, 0x3D, 0x3B, 0x3C, 0x58, 0x64, 0x44, 0x42, 0x40, 0x3E, 0x0F, 0x29, 0x59,
    0x65, 0x38, 0x2A, 0x70, 0x1D, 0x10, 0x02, 0x5A, 0x66, 0x71, 0x2C, 0x1F, 0x1E, 0x11, 0x03, 0x5B,
    0x67, 0x2E, 0x2D, 0x20, 0x12, 0x05, 0x04, 0x5C, 0x68, 0x39, 0x2F, 0x21, 0x14, 0x13, 0x06, 0x5D,
    0x69, 0x31, 0x30, 0x23, 0x22, 0x15, 0x07, 0x5E, 0x6A, 0x72, 0x32, 0x24, 0x16, 0x08, 0x09, 0x5F,
    0x6B, 0x33, 0x25, 0x17, 0x18, 0x0B, 0x0A, 0x60, 0x6C, 0x34, 0x35, 0x26, 0x27, 0x19, 0x0C, 0x61,
    0x6D, 0x73, 0x28, 0x74, 0x1A, 0x0D, 0x62, 0x6E, 0x3A, 0x36, 0x1C, 0x1B, 0x75, 0x2B, 0x63, 0x76,
    0x55, 0x56, 0x77, 0x78, 0x79, 0x7A, 0x0E, 0x7B, 0x7C, 0x4F, 0x7D, 0x4B, 0x47, 0x7E, 0x7F, 0x6F,
    0x52, 0x53, 0x50, 0x4C, 0x4D, 0x48, 0x01, 0x45, 0x57, 0x4E, 0x51, 0x4A, 0x37, 0x49, 0x46, 0x54,
    0x80, 0x81, 0x82, 0x41, 0x54, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
    0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
    0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
    0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF,
    0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
    0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
    0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF
};

I8042::I8042()
{
    InitPointer(m_pic);
    InitPointer(m_scheduler);
    InitPointer(m_system_control);
    InitPointer(m_keyboard);
    InitPointer(m_mouse);
    m_trace_logger = TraceLogger::GetDisabled();
    memset(&m_state, 0, sizeof(m_state));
}

I8042::~I8042()
{
}

void I8042::Init(PIC* pic, Scheduler* scheduler, SystemControl* system_control, Keyboard* keyboard)
{
    m_pic = pic;
    m_scheduler = scheduler;
    m_system_control = system_control;
    m_keyboard = keyboard;
    Reset();
}

// The PS/2 mouse, or nothing on the auxiliary port
void I8042::SetMouse(PS2Mouse* mouse)
{
    m_mouse = mouse;
}

void I8042::SetTraceLogger(TraceLogger* trace_logger)
{
    m_trace_logger = IsValidPointer(trace_logger) ? trace_logger : TraceLogger::GetDisabled();
}

void I8042::Reset()
{
    memset(&m_state, 0, sizeof(m_state));
    m_state.output_port = (u8)(k_i8042_output_port | (m_system_control->IsKeyboardA20Enabled() ? 0x02 : 0x00));
    m_state.input_clocks = GDOX_NO_EVENT;
    m_state.timeout_clocks = GDOX_NO_EVENT;
    m_state.result_clocks = GDOX_NO_EVENT;
    UpdateDeviceClocks(m_scheduler->GetClocks());
    UpdateIRQ();
    UpdateNextEvent();
}

u8 I8042::Read(u16 port, u64 clocks)
{
    Synchronize(clocks);

    if (port == 0x0064)
        return GetStatus();

    u8 value = m_state.output_buffer;
    u8 aux = m_state.output_aux ? 0x01 : 0x00;
    bool full = m_state.output_full;

    m_state.output_full = false;
    m_state.output_aux = false;
    m_state.timeout = false;

    // The controller reloads the buffer a moment later, so the next byte is a new IRQ edge
    if (m_state.result_pending && m_state.result_clocks == GDOX_NO_EVENT)
        m_state.result_clocks = clocks + k_i8042_command_clocks;

    UpdateDeviceClocks(clocks);
    UpdateIRQ();
    UpdateNextEvent();

    if (unlikely(m_trace_logger->IsEnabled(TRACE_KEYBOARD)))
        TraceEvent(full ? TRACE_KEYBOARD_READ : TRACE_KEYBOARD_READ_EMPTY, value, aux, 0);

    return value;
}

// A second write before the controller takes the first replaces it
void I8042::Write(u16 port, u8 value, u64 clocks)
{
    Synchronize(clocks);

    m_state.input_buffer = value;
    m_state.input_command = port == 0x0064;

    if (m_state.input_full)
        return;

    m_state.input_full = true;
    m_state.input_clocks = clocks + k_i8042_command_clocks;
    UpdateNextEvent();
}

bool I8042::KeyPressed(GDOX_Keys key)
{
    u64 clocks = m_scheduler->GetClocks();
    Synchronize(clocks);
    bool held = m_keyboard->KeyPressed(key, clocks);
    UpdateNextEvent();
    return held;
}

void I8042::KeyReleased(GDOX_Keys key)
{
    u64 clocks = m_scheduler->GetClocks();
    Synchronize(clocks);
    m_keyboard->KeyReleased(key, clocks);
    UpdateNextEvent();
}

void I8042::ReleaseAllKeys()
{
    u64 clocks = m_scheduler->GetClocks();
    Synchronize(clocks);
    m_keyboard->ReleaseAllKeys(clocks);
    UpdateNextEvent();
}

// Every timer due by the clock settles in order, each from the clock it was due at
void I8042::Synchronize(u64 clocks)
{
    u64 next = GetNextClocks();

    if (next > clocks)
        return;

    while (next <= clocks)
    {
        if (next == m_state.input_clocks)
        {
            TakeInput(next);
        }
        else if (next == m_state.timeout_clocks)
        {
            m_state.timeout_clocks = GDOX_NO_EVENT;
            m_state.timeout = true;
            PutOutput(0xFE, true);
        }
        else if (next == m_state.result_clocks)
        {
            m_state.result_clocks = GDOX_NO_EVENT;
            FlushResult();
            UpdateIRQ();
        }
        else if (next == m_keyboard->GetNextEventClocks())
        {
            u8 value = 0;

            if (m_keyboard->HandleEvent(next, value))
                ReceiveKeyboardByte(value);
        }
        else if (IsValidPointer(m_mouse))
        {
            u8 value = 0;

            if (m_mouse->HandleEvent(next, value))
                PutOutput(value, true);
        }

        UpdateDeviceClocks(next);
        next = GetNextClocks();
    }

    UpdateNextEvent();
}

u64 I8042::GetNextClocks() const
{
    u64 next = MIN(MIN(MIN(m_state.input_clocks, m_state.timeout_clocks), m_state.result_clocks),
        m_keyboard->GetNextEventClocks());

    return IsValidPointer(m_mouse) ? MIN(next, m_mouse->GetNextEventClocks()) : next;
}

// The cable carries one byte at a time, so a byte for the keyboard waits for the one before it
void I8042::TakeInput(u64 clocks)
{
    u8 value = m_state.input_buffer;
    u8 command = m_state.pending_command;

    if (!m_state.input_command && command == 0 && m_keyboard->IsReceiving())
    {
        m_state.input_clocks = m_keyboard->GetReceiveClocks() + k_i8042_command_clocks;
        return;
    }

    if (!m_state.input_command && command == 0xD4 && IsValidPointer(m_mouse) && m_mouse->IsReceiving())
    {
        m_state.input_clocks = m_mouse->GetReceiveClocks() + k_i8042_command_clocks;
        return;
    }

    m_state.input_full = false;
    m_state.input_clocks = GDOX_NO_EVENT;
    m_state.pending_command = 0;

    if (m_state.input_command)
    {
        if (unlikely(m_trace_logger->IsEventEnabled(TRACE_KEYBOARD, TRACE_KEYBOARD_COMMAND)))
            TraceEvent(TRACE_KEYBOARD_COMMAND, value, TRACE_KEYBOARD_TARGET_8042, m_state.ram[0]);

        ExecuteCommand(value);
    }
    else if (command != 0)
    {
        if (unlikely(m_trace_logger->IsEventEnabled(TRACE_KEYBOARD, TRACE_KEYBOARD_COMMAND)) && command != 0xD1)
            TraceEvent(TRACE_KEYBOARD_COMMAND, value, TRACE_KEYBOARD_TARGET_8042_PARAMETER, command);

        ExecuteParameter(command, value, clocks);
    }
    else
    {
        m_state.ram[0] &= (u8)~k_i8042_keyboard_disabled;
        m_keyboard->Receive(value, clocks);
    }
}

void I8042::ExecuteCommand(u8 value)
{
    if ((value & 0xE0) == 0x20)
    {
        PutOutput(m_state.ram[value & (I8042_RAM_SIZE - 1)], false);
        return;
    }

    if ((value & 0xE0) == 0x60)
    {
        m_state.pending_command = value;
        return;
    }

    // Bits 0-3 low pulse those output port lines, only the reset line reaches anything in 6 us
    if ((value & 0xF0) == 0xF0)
    {
        if ((value & 0x01) == 0)
            m_system_control->RequestCPUReset(k_system_control_reset_keyboard, 0x0064);

        return;
    }

    switch (value)
    {
        case 0xA7:
            m_state.ram[0] |= k_i8042_aux_disabled;
            break;
        case 0xA8:
            m_state.ram[0] &= (u8)~k_i8042_aux_disabled;
            break;
        case 0xA9:
        case 0xAB:
            PutOutput(0x00, false);
            break;
        case 0xAA:
            m_state.ram[0] |= k_i8042_system_flag | k_i8042_keyboard_disabled | k_i8042_aux_disabled;
            PutOutput(0x55, false);
            break;
        case 0xAD:
            m_state.ram[0] |= k_i8042_keyboard_disabled;
            break;
        case 0xAE:
            m_state.ram[0] &= (u8)~k_i8042_keyboard_disabled;
            break;
        case 0xC0:
            PutOutput(k_i8042_input_port, false);
            break;
        case 0xD0:
            PutOutput(GetOutputPort(), false);
            break;
        case 0xD1:
        case 0xD2:
        case 0xD3:
        case 0xD4:
            m_state.pending_command = value;
            break;
        case 0xE0:
            PutOutput((m_state.ram[0] & k_i8042_keyboard_disabled) != 0 ? 0x02 : 0x03, false);
            break;
        default:
            break;
    }
}

void I8042::ExecuteParameter(u8 command, u8 value, u64 clocks)
{
    switch (command)
    {
        case 0xD1:
            WriteOutputPort(value);
            break;
        case 0xD2:
            PutOutput(value, false);
            break;
        case 0xD3:
            PutOutput(value, true);
            break;
        case 0xD4:
            if (IsValidPointer(m_mouse))
                m_mouse->Receive(value, clocks);
            else
                m_state.timeout_clocks = clocks + k_i8042_timeout_clocks;
            break;
        default:
            m_state.ram[command & (I8042_RAM_SIZE - 1)] = value;
            UpdateIRQ();
            break;
    }
}

// The reset line goes back up once the board has seen it low
void I8042::WriteOutputPort(u8 value)
{
    u8 previous = m_state.output_port;

    m_state.output_port = value | 0x01;
    m_system_control->SetKeyboardA20((value & 0x02) != 0);

    if ((value & 0x01) == 0)
        m_system_control->RequestCPUReset(k_system_control_reset_keyboard, 0x0064);

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_KEYBOARD, TRACE_KEYBOARD_OUTPUT_PORT)))
        TraceEvent(TRACE_KEYBOARD_OUTPUT_PORT, value, 0, previous);
}

void I8042::ReceiveKeyboardByte(u8 value)
{
    if ((m_state.ram[0] & k_i8042_translate_enabled) != 0)
    {
        if (value == 0xF0)
        {
            m_state.break_prefix = true;
            return;
        }

        value = (u8)(k_i8042_translate[value] | (m_state.break_prefix ? 0x80 : 0x00));
        m_state.break_prefix = false;
    }

    PutOutput(value, false);
}

// A byte waits for the output buffer to empty, a later one takes its place
void I8042::PutOutput(u8 value, bool aux)
{
    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_KEYBOARD, TRACE_KEYBOARD_BYTE)) && m_state.result_pending)
        TraceEvent(TRACE_KEYBOARD_BYTE, m_state.result, m_state.result_aux ? 0x03 : 0x02, 0);

    m_state.result = value;
    m_state.result_aux = aux;
    m_state.result_pending = true;
    FlushResult();
    UpdateIRQ();
}

void I8042::FlushResult()
{
    if (!m_state.result_pending || m_state.output_full || m_state.result_clocks != GDOX_NO_EVENT)
        return;

    m_state.output_buffer = m_state.result;
    m_state.output_aux = m_state.result_aux;
    m_state.output_full = true;
    m_state.result_pending = false;

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_KEYBOARD, TRACE_KEYBOARD_BYTE)))
        TraceEvent(TRACE_KEYBOARD_BYTE, m_state.output_buffer, m_state.output_aux ? 0x01 : 0x00, 0);
}

// Each device holds its bytes while the output buffer is busy or its clock is held low, the keyboard goes first
void I8042::UpdateDeviceClocks(u64 clocks)
{
    bool free = !m_state.output_full && !m_state.result_pending;

    m_keyboard->SetClock(free && (m_state.ram[0] & k_i8042_keyboard_disabled) == 0, clocks);

    if (IsValidPointer(m_mouse))
        m_mouse->SetClock(free && (m_state.ram[0] & k_i8042_aux_disabled) == 0, clocks);
}

void I8042::UpdateIRQ()
{
    bool full = m_state.output_full;
    bool aux = m_state.output_aux;

    m_pic->SetIRQLine(k_i8042_keyboard_irq, full && !aux && (m_state.ram[0] & k_i8042_keyboard_interrupt) != 0);
    m_pic->SetIRQLine(k_i8042_aux_irq, full && aux && (m_state.ram[0] & k_i8042_aux_interrupt) != 0);
}

void I8042::UpdateNextEvent()
{
    m_scheduler->Schedule(SCHEDULER_EVENT_KEYBOARD, GetNextClocks());
}

void I8042::TraceEvent(u8 event, u8 value, u8 flags, u8 key)
{
    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_KEYBOARD, event);
    entry->keyboard.value = value;
    entry->keyboard.flags = flags;
    entry->keyboard.key = key;
    entry->keyboard.pending = m_keyboard->GetState()->buffer_count;
    entry->keyboard.previous = m_state.ram[0];

    if (event == TRACE_KEYBOARD_READ || event == TRACE_KEYBOARD_READ_EMPTY || event == TRACE_KEYBOARD_BYTE)
    {
        entry->keyboard.flags = 0;
        entry->flags |= (flags & 0x01) != 0 ? TRACE_KEYBOARD_FLAG_AUX : 0;
        entry->flags |= (flags & 0x02) != 0 ? TRACE_KEYBOARD_FLAG_LOST : 0;
        entry->flags |= event == TRACE_KEYBOARD_BYTE && m_state.timeout ? TRACE_KEYBOARD_FLAG_TIMEOUT : 0;
    }
}

void I8042::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
}

void I8042::LoadState(std::istream& stream, int version)
{
    UNUSED(version);
    StateSerializer serializer(stream);
    Serialize(serializer);
    SanitizeState();
}

void I8042::Serialize(StateSerializer& serializer)
{
    G_SERIALIZE_ARRAY(serializer, m_state.ram, I8042_RAM_SIZE);
    G_SERIALIZE(serializer, m_state.output_port);
    G_SERIALIZE(serializer, m_state.output_buffer);
    G_SERIALIZE(serializer, m_state.output_full);
    G_SERIALIZE(serializer, m_state.output_aux);
    G_SERIALIZE(serializer, m_state.result);
    G_SERIALIZE(serializer, m_state.result_aux);
    G_SERIALIZE(serializer, m_state.result_pending);
    G_SERIALIZE(serializer, m_state.input_buffer);
    G_SERIALIZE(serializer, m_state.input_full);
    G_SERIALIZE(serializer, m_state.input_command);
    G_SERIALIZE(serializer, m_state.pending_command);
    G_SERIALIZE(serializer, m_state.break_prefix);
    G_SERIALIZE(serializer, m_state.timeout);
    G_SERIALIZE(serializer, m_state.input_clocks);
    G_SERIALIZE(serializer, m_state.timeout_clocks);
    G_SERIALIZE(serializer, m_state.result_clocks);
}

void I8042::SanitizeState()
{
    if (!m_state.input_full)
        m_state.input_clocks = GDOX_NO_EVENT;

    if (!m_state.result_pending)
        m_state.result_clocks = GDOX_NO_EVENT;

    UpdateIRQ();
    UpdateNextEvent();
}
