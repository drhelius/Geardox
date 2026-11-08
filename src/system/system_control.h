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


#ifndef SYSTEM_CONTROL_H
#define SYSTEM_CONTROL_H

#include <iostream>
#include "../common/common.h"

class Memory;
class PIC;
class PIT;
class StateSerializer;
class TraceLogger;
class X87;

class SystemControl
{
public:
    struct SystemControl_State
    {
        u8 reset_cause;
        bool reset_pending;
        u32 reset_count;
        u8 last_reset_cause;
        bool nmi_mask;
        u8 port_61;
        u8 port_92;
        u8 post_code;
        bool fpu_error;
        bool keyboard_a20;
        bool reset_a20;
        u8 shadow_rom_end;
        u8 shadow_free_end;
        bool shadow_locked;
    };

public:
    SystemControl();
    ~SystemControl();
    void Init(Memory* memory, PIC* pic, PIT* pit);
    void SetTraceLogger(TraceLogger* trace_logger);
    void SetPowerOnA20(bool port_92);
    void SetFPU(X87* fpu);
    void SetShadowControl(bool enabled);
    void ApplyShadow();
    void Reset();
    bool Decodes(u16 port) const;
    u8 Read(u16 port, u64 clocks);
    u8 Peek(u16 port, u64 clocks) const;
    void Write(u16 port, u8 value, u64 clocks);
    u64 GetReadStableUntil(u16 port, u64 clocks) const;
    void RequestCPUReset(u8 cause);
    bool IsCPUResetPending() const;
    void AcknowledgeCPUReset();
    void SetFPUError();
    void SetKeyboardA20(bool enabled);
    bool IsKeyboardA20Enabled() const;
    bool IsNMIMasked() const;
    bool IsA20Enabled() const;
    u8 GetPOSTCode() const;
    SystemControl_State* GetState();
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream, int version = GDOX_SAVESTATE_VERSION);

private:
    void UpdateA20();
    void TraceWrite(u16 port, u8 value, u8 previous);
    void Serialize(StateSerializer& serializer);
    void SanitizeState();

private:
    SystemControl_State m_state;
    Memory* m_memory;
    PIC* m_pic;
    PIT* m_pit;
    X87* m_x87;
    bool m_shadow_control;
    TraceLogger* m_trace_logger;
    bool m_power_on_a20_port_92;
};

static const u8 k_system_control_reset_fast = 0x01;
static const u8 k_system_control_reset_shutdown = 0x02;
static const u8 k_system_control_reset_keyboard = 0x04;
static const int k_system_control_fpu_irq = 13;
static const u32 k_system_control_shadow_start = 0xC0000;
static const u32 k_system_control_shadow_bios = 0xF0000;
static const u32 k_system_control_shadow_id = 0x584F4447;

#include "system_control_inline.h"

#endif /* SYSTEM_CONTROL_H */
