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


#ifndef I8042_H
#define I8042_H

#include <iostream>
#include "../common/common.h"

#define I8042_RAM_SIZE 32

class Keyboard;
class PIC;
class PS2Mouse;
class Scheduler;
class StateSerializer;
class SystemControl;
class TraceLogger;

class I8042
{
public:
    struct I8042_State
    {
        u8 ram[I8042_RAM_SIZE];
        u8 output_port;
        u8 output_buffer;
        bool output_full;
        bool output_aux;
        u8 result;
        bool result_aux;
        bool result_pending;
        u8 input_buffer;
        bool input_full;
        bool input_command;
        u8 pending_command;
        bool break_prefix;
        bool timeout;
        u64 input_clocks;
        u64 timeout_clocks;
        u64 result_clocks;
    };

public:
    I8042();
    ~I8042();
    void Init(PIC* pic, Scheduler* scheduler, SystemControl* system_control, Keyboard* keyboard);
    void SetMouse(PS2Mouse* mouse);
    void SetTraceLogger(TraceLogger* trace_logger);
    void Reset();
    u8 Read(u16 port, u64 clocks);
    u8 Peek(u16 port) const;
    void Write(u16 port, u8 value, u64 clocks);
    u64 GetReadStableUntil(u16 port) const;
    void HandleEvent(u64 clocks);
    void KeyPressed(GDOX_Keys key);
    void KeyReleased(GDOX_Keys key);
    void ReleaseAllKeys();
    u8 GetStatus() const;
    u8 GetOutputPort() const;
    I8042_State* GetState();
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream, int version = GDOX_SAVESTATE_VERSION);

private:
    void Synchronize(u64 clocks);
    u64 GetNextClocks() const;
    void TakeInput(u64 clocks);
    void ExecuteCommand(u8 value);
    void ExecuteParameter(u8 command, u8 value, u64 clocks);
    void WriteOutputPort(u8 value);
    void ReceiveKeyboardByte(u8 value);
    void PutOutput(u8 value, bool aux);
    void FlushResult();
    void UpdateDeviceClocks(u64 clocks);
    void UpdateIRQ();
    void UpdateNextEvent();
    void TraceEvent(u8 event, u8 value, u8 flags, u8 key);
    void Serialize(StateSerializer& serializer);
    void SanitizeState();

private:
    PIC* m_pic;
    Scheduler* m_scheduler;
    SystemControl* m_system_control;
    Keyboard* m_keyboard;
    PS2Mouse* m_mouse;
    TraceLogger* m_trace_logger;
    I8042_State m_state;
};

static const u64 k_i8042_command_clocks = GDOX_MACHINE_CLOCK_RATE / 50000;
static const u64 k_i8042_timeout_clocks = ((u64)GDOX_MACHINE_CLOCK_RATE * 15) / 1000;
static const int k_i8042_keyboard_irq = 1;
static const int k_i8042_aux_irq = 12;
static const u8 k_i8042_input_port = 0xB0;
static const u8 k_i8042_output_port = 0xCD;
static const u8 k_i8042_keyboard_interrupt = 0x01;
static const u8 k_i8042_aux_interrupt = 0x02;
static const u8 k_i8042_system_flag = 0x04;
static const u8 k_i8042_keyboard_disabled = 0x10;
static const u8 k_i8042_aux_disabled = 0x20;
static const u8 k_i8042_translate_enabled = 0x40;

#include "i8042_inline.h"

#endif /* I8042_H */
