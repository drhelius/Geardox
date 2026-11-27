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

#ifndef SERIAL_PORTS_H
#define SERIAL_PORTS_H

#include <iostream>
#include "../common/common.h"

#define SERIAL_PORTS 2

class Input;
class NetworkPort;
class PIC;
class SerialLink;
class Scheduler;
class SerialMouse;
class StateSerializer;
class TraceLogger;
class UART;

// COM1 and COM2, with the serial mouse on COM1 when it's the machine's mouse
class SerialPorts
{
public:
    SerialPorts();
    ~SerialPorts();
    void Init(PIC* pic, Scheduler* scheduler, Input* input, NetworkPort* port);
    void SetLinks(int com1, int com2);
    void PollLinks(u64 clocks);
    SerialLink* GetLink(int index);
    int GetLinkKind(int index) const;
    void SetTraceLogger(TraceLogger* trace_logger);
    void SetMouse(bool attached);
    void Reset();
    bool Decodes(u16 port) const;
    u8 Read(u16 port, u64 clocks);
    u8 Peek(u16 port) const;
    void Write(u16 port, u8 value, u64 clocks);
    u64 GetReadStableUntil(u16 port) const;
    void HandleEvent(u64 clocks);
    UART* GetUART(int index);
    SerialMouse* GetMouse();
    bool IsMouseAttached() const;
    bool GetIRQ(int index) const;
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream, int version = GDOX_SAVESTATE_VERSION);

private:
    int GetIndex(u16 port) const;
    void Synchronize(u64 clocks);
    u64 GetNextClocks() const;
    void Transmitted(int index, u8 value, u64 clocks);
    void UpdateModemLines(int index, u64 clocks);
    void UpdateIRQ();
    void UpdateEvent();
    NO_INLINE void WriteTraced(int index, u8 reg, u8 value, u64 clocks);
    NO_INLINE void TraceEvent(u8 event, int index, u8 reg, u8 value, u8 flags);
    void Serialize(StateSerializer& serializer);

private:
    UART* m_uart[SERIAL_PORTS];
    SerialLink* m_links[SERIAL_PORTS];
    NetworkPort* m_port;
    SerialMouse* m_mouse;
    PIC* m_pic;
    Scheduler* m_scheduler;
    TraceLogger* m_trace_logger;
    bool m_mouse_attached;
    bool m_irq[SERIAL_PORTS];
    u8 m_modem_outputs[SERIAL_PORTS];
};

static const u16 k_serial_ports_base[SERIAL_PORTS] = { 0x03F8, 0x02F8 };
static const int k_serial_ports_irq[SERIAL_PORTS] = { 4, 3 };

#endif /* SERIAL_PORTS_H */
