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

#ifndef PARALLEL_PORT_H
#define PARALLEL_PORT_H

#include <iostream>
#include "../common/common.h"

class StateSerializer;
class TraceLogger;

// LPT1 at 378h, an output-only port with nothing on its connector
class ParallelPort
{
public:
    struct ParallelPort_State
    {
        u8 data;
        u8 control;
    };

public:
    ParallelPort();
    ~ParallelPort();
    void SetTraceLogger(TraceLogger* trace_logger);
    void Reset();
    bool Decodes(u16 port) const;
    u8 Read(u16 port) const;
    void Write(u16 port, u8 value);
    ParallelPort_State* GetState();
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream, int version = GDOX_SAVESTATE_VERSION);

private:
    void TraceEvent(u8 event, u16 port, u8 value);
    void Serialize(StateSerializer& serializer);

private:
    ParallelPort_State m_state;
    TraceLogger* m_trace_logger;
};

static const u16 k_parallel_port_base = 0x0378;
static const u8 k_parallel_port_status_empty = 0x7F;

#endif /* PARALLEL_PORT_H */
