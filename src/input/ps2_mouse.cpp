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


#include "ps2_mouse.h"
#include "input.h"
#include "keyboard.h"
#include "../common/trace_logger.h"
#include "../common/state_serializer.h"

static const u8 k_ps2_mouse_rates[7] = { 10, 20, 40, 60, 80, 100, 200 };
static const s32 k_ps2_mouse_scaling[6] = { 0, 1, 1, 3, 6, 9 };

PS2Mouse::PS2Mouse()
{
    InitPointer(m_input);
    InitPointer(m_trace_logger);
    memset(&m_state, 0, sizeof(m_state));
}

PS2Mouse::~PS2Mouse()
{
}

void PS2Mouse::Init(Input* input)
{
    m_input = input;
    Reset(0);
}

void PS2Mouse::SetTraceLogger(TraceLogger* trace_logger)
{
    m_trace_logger = trace_logger;
}

// The self-test at power on ends with the same AAh and ID as a reset command. The BIOS talks to the mouse long after,
// so a command that finds them still held drops them instead of being ignored
void PS2Mouse::Reset(u64 clocks)
{
    static const u8 k_power_on[2] = { k_ps2_mouse_self_test_passed, k_ps2_mouse_id };

    memset(&m_state, 0, sizeof(m_state));
    m_state.receive_clocks = GDOX_NO_EVENT;
    m_state.send_clocks = GDOX_NO_EVENT;
    m_state.sample_clocks = clocks + GDOX_MACHINE_CLOCK_RATE / k_ps2_mouse_default_rate;
    SetDefaults();
    QueueGroup(k_power_on, 2, true);
}

bool PS2Mouse::HandleEvent(u64 clocks, u8& value)
{
    if (m_state.receive_clocks <= clocks)
    {
        m_state.receive_clocks = GDOX_NO_EVENT;
        ProcessByte(m_state.received, clocks);
    }
    else if (m_state.send_clocks <= clocks)
    {
        m_state.send_clocks = GDOX_NO_EVENT;
        value = PopByte();
        UpdateSend(clocks);
        return true;
    }
    else if (m_state.sample_clocks <= clocks)
    {
        m_state.sample_clocks = clocks + GDOX_MACHINE_CLOCK_RATE / m_state.rate;
        Sample(clocks);
    }

    UpdateSend(clocks);
    return false;
}

void PS2Mouse::SetClock(bool enabled, u64 clocks)
{
    m_state.clock_enabled = enabled;
    UpdateSend(clocks);
}

// The line carries one byte at a time, the one on its way out starts over afterwards
void PS2Mouse::Receive(u8 value, u64 clocks)
{
    m_state.received = value;
    m_state.receive_clocks = clocks + k_keyboard_byte_clocks;
    m_state.send_clocks = GDOX_NO_EVENT;
}

// Commands before the AAh and ID of a reset are ignored, and an ACK supersedes any other output
void PS2Mouse::ProcessByte(u8 value, u64 clocks)
{
    if (m_state.resetting)
        return;

    if (m_state.wrap && value != 0xFF && value != 0xEC)
    {
        QueueReply(value);
        TraceEvent(TRACE_INPUT_MOUSE_COMMAND, &value, 1);
        return;
    }

    u8 command = m_state.command;
    m_state.command = 0;
    m_state.reply_count = 0;

    bool valid = command != 0 ? ProcessParameter(command, value) : ProcessCommand(value, clocks);

    if (valid)
        m_state.errors = 0;
    else
    {
        m_state.command = command;
        Reject();
    }

    u8 trace[PS2_MOUSE_GROUP_SIZE] = { value, 0, 0, 0 };
    u8 count = (u8)MIN((int)m_state.reply_count, PS2_MOUSE_GROUP_SIZE - 1);
    memcpy(trace + 1, m_state.reply, count);
    TraceEvent(TRACE_INPUT_MOUSE_COMMAND, trace, (u8)(count + 1));
}

bool PS2Mouse::ProcessParameter(u8 command, u8 value)
{
    bool valid = false;

    if (command == 0xF3)
    {
        for (int i = 0; i < 7 && !valid; i++)
            valid = value == k_ps2_mouse_rates[i];

        if (valid)
            m_state.rate = value;
    }
    else if (command == 0xE8)
    {
        valid = value <= 3;

        if (valid)
            m_state.resolution = value;
    }

    if (valid)
    {
        ClearCounts();
        QueueReply(k_ps2_mouse_ack);
    }

    return valid;
}

// The count accumulators clear after any command but a resend
bool PS2Mouse::ProcessCommand(u8 value, u64 clocks)
{
    UNUSED(clocks);

    switch (value)
    {
        case 0xFF:
        {
            u8 reply[3] = { k_ps2_mouse_ack, k_ps2_mouse_self_test_passed, k_ps2_mouse_id };
            SetDefaults();
            QueueGroup(reply, 3, true);
            m_state.resetting = true;
            return true;
        }
        case 0xFE:
            QueueGroup(m_state.last, m_state.last_count, false);
            return true;
        case 0xF6:
            SetDefaults();
            break;
        case 0xF5:
            m_state.enabled = false;
            break;
        case 0xF4:
            m_state.enabled = true;
            break;
        case 0xF3:
        case 0xE8:
            m_state.command = value;
            break;
        case 0xF2:
        {
            u8 reply[2] = { k_ps2_mouse_ack, k_ps2_mouse_id };
            ClearCounts();
            QueueGroup(reply, 2, true);
            return true;
        }
        case 0xF0:
            m_state.remote = true;
            break;
        case 0xEE:
            m_state.wrap_remote = m_state.remote;
            m_state.wrap = true;
            break;
        case 0xEC:
            if (m_state.wrap)
            {
                m_state.wrap = false;
                m_state.remote = m_state.wrap_remote;

                if (!m_state.remote)
                    m_state.enabled = false;
            }
            break;
        case 0xEB:
            QueueReply(k_ps2_mouse_ack);
            QueuePacket(false);
            return true;
        case 0xEA:
            m_state.remote = false;
            break;
        case 0xE9:
            QueueStatus();
            ClearCounts();
            return true;
        case 0xE7:
            m_state.scaling = true;
            break;
        case 0xE6:
            m_state.scaling = false;
            break;
        default:
            return false;
    }

    ClearCounts();
    QueueReply(k_ps2_mouse_ack);
    return true;
}

void PS2Mouse::SetDefaults()
{
    m_state.enabled = false;
    m_state.remote = false;
    m_state.wrap = false;
    m_state.scaling = false;
    m_state.resolution = k_ps2_mouse_default_resolution;
    m_state.rate = k_ps2_mouse_default_rate;
    m_state.command = 0;
    m_state.errors = 0;
    ClearCounts();
}

// Movement is counted at the sample rate in every mode, and reported in stream mode when it or a button changed.
// A host count is 1/8 mm, the 200 dpi of a serial mouse (LOGIMOUSE C7 p10), so both report alike at 8 counts per mm
void PS2Mouse::Sample(u64 clocks)
{
    s32 x = 0;
    s32 y = 0;

    m_input->TakeMouseDelta(clocks, k_input_mouse_limit, x, y);

    s64 total_x = ((s64)x << m_state.resolution) + m_state.remainder_x;
    s64 total_y = ((s64)y << m_state.resolution) + m_state.remainder_y;
    s64 counts_x = total_x / 8;
    s64 counts_y = total_y / 8;

    m_state.remainder_x = (s32)(total_x - counts_x * 8);
    m_state.remainder_y = (s32)(total_y - counts_y * 8);
    m_state.x = (s32)CLAMP((s64)m_state.x + counts_x, -k_ps2_mouse_count_limit, k_ps2_mouse_count_limit);
    m_state.y = (s32)CLAMP((s64)m_state.y - counts_y, -k_ps2_mouse_count_limit, k_ps2_mouse_count_limit);

    if (!m_state.enabled || m_state.remote || m_state.wrap || m_state.reply_count != 0)
        return;

    u8 buttons = m_input->GetMouseButtons() & (GDOX_MOUSE_LEFT | GDOX_MOUSE_RIGHT);

    if (m_state.x == 0 && m_state.y == 0 && buttons == m_state.buttons)
        return;

    QueuePacket(true);
}

void PS2Mouse::ClearCounts()
{
    m_state.x = 0;
    m_state.y = 0;
}

void PS2Mouse::QueueGroup(const u8* bytes, u8 count, bool remember)
{
    for (int i = 0; i < count && m_state.reply_count < PS2_MOUSE_REPLY_SIZE; i++)
        m_state.reply[m_state.reply_count++] = bytes[i];

    if (!remember)
        return;

    m_state.last_count = (u8)MIN((int)count, PS2_MOUSE_GROUP_SIZE);
    memcpy(m_state.last, bytes, m_state.last_count);
}

void PS2Mouse::QueueReply(u8 value)
{
    QueueGroup(&value, 1, true);
}

// The counts past nine bits are reported at the limit with the overflow bit, and the rest is lost
void PS2Mouse::QueuePacket(bool scaled)
{
    u8 buttons = m_input->GetMouseButtons() & (GDOX_MOUSE_LEFT | GDOX_MOUSE_RIGHT);
    s32 x = scaled && m_state.scaling ? Scale(m_state.x) : m_state.x;
    s32 y = scaled && m_state.scaling ? Scale(m_state.y) : m_state.y;
    u8 status = (u8)(0x08 | buttons);

    if (x > 255 || x < -256)
    {
        x = x > 0 ? 255 : -256;
        status |= 0x40;
    }

    if (y > 255 || y < -256)
    {
        y = y > 0 ? 255 : -256;
        status |= 0x80;
    }

    status |= (u8)((x < 0 ? 0x10 : 0x00) | (y < 0 ? 0x20 : 0x00));

    u8 packet[3] = { status, (u8)x, (u8)y };
    QueueGroup(packet, 3, true);
    m_state.buttons = buttons;
    ClearCounts();
    TraceEvent(TRACE_INPUT_MOUSE_PACKET, packet, 3);
}

void PS2Mouse::QueueStatus()
{
    u8 buttons = m_input->GetMouseButtons();
    u8 status = (u8)((m_state.remote ? 0x40 : 0x00) | (m_state.enabled ? 0x20 : 0x00) |
        (m_state.scaling ? 0x10 : 0x00) | ((buttons & GDOX_MOUSE_LEFT) != 0 ? 0x04 : 0x00) |
        ((buttons & GDOX_MOUSE_RIGHT) != 0 ? 0x01 : 0x00));
    u8 reply[4] = { k_ps2_mouse_ack, status, m_state.resolution, m_state.rate };

    QueueGroup(reply, 4, true);
}

// One bad byte gets a resend, a second one in a row an error
void PS2Mouse::Reject()
{
    u8 value = m_state.errors != 0 ? k_ps2_mouse_error : k_ps2_mouse_resend;

    m_state.errors = m_state.errors != 0 ? 0 : 1;
    QueueGroup(&value, 1, false);
}

s32 PS2Mouse::Scale(s32 value) const
{
    s32 magnitude = value < 0 ? -value : value;
    s32 scaled = magnitude < 6 ? k_ps2_mouse_scaling[magnitude] : magnitude * 2;

    return value < 0 ? -scaled : scaled;
}

void PS2Mouse::UpdateSend(u64 clocks)
{
    if (m_state.reply_count == 0 || !m_state.clock_enabled || IsReceiving())
        m_state.send_clocks = GDOX_NO_EVENT;
    else if (m_state.send_clocks == GDOX_NO_EVENT)
        m_state.send_clocks = clocks + k_keyboard_byte_clocks;
}

u8 PS2Mouse::PopByte()
{
    if (m_state.reply_count == 0)
        return 0;

    u8 value = m_state.reply[0];
    m_state.reply_count--;
    memmove(m_state.reply, m_state.reply + 1, m_state.reply_count);

    if (m_state.reply_count == 0)
        m_state.resetting = false;

    return value;
}

void PS2Mouse::TraceEvent(u8 event, const u8* data, u8 count)
{
    if (!IsValidPointer(m_trace_logger) || !m_trace_logger->IsEventEnabled(TRACE_INPUT, event))
        return;

    GDOX_Trace_Entry entry = {};
    entry.type = TRACE_INPUT;
    entry.event = event;
    entry.input.device = TRACE_INPUT_DEVICE_PS2_MOUSE;
    entry.input.count = (u8)MIN((int)count, 4);
    memcpy(entry.input.data, data, entry.input.count);
    m_trace_logger->TraceLog(entry);
}

void PS2Mouse::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
}

void PS2Mouse::LoadState(std::istream& stream, int version)
{
    UNUSED(version);
    StateSerializer serializer(stream);
    Serialize(serializer);
    SanitizeState();
}

void PS2Mouse::Serialize(StateSerializer& serializer)
{
    G_SERIALIZE_ARRAY(serializer, m_state.reply, PS2_MOUSE_REPLY_SIZE);
    G_SERIALIZE(serializer, m_state.reply_count);
    G_SERIALIZE_ARRAY(serializer, m_state.last, PS2_MOUSE_GROUP_SIZE);
    G_SERIALIZE(serializer, m_state.last_count);
    G_SERIALIZE(serializer, m_state.command);
    G_SERIALIZE(serializer, m_state.received);
    G_SERIALIZE(serializer, m_state.errors);
    G_SERIALIZE(serializer, m_state.clock_enabled);
    G_SERIALIZE(serializer, m_state.enabled);
    G_SERIALIZE(serializer, m_state.remote);
    G_SERIALIZE(serializer, m_state.wrap);
    G_SERIALIZE(serializer, m_state.wrap_remote);
    G_SERIALIZE(serializer, m_state.scaling);
    G_SERIALIZE(serializer, m_state.resetting);
    G_SERIALIZE(serializer, m_state.resolution);
    G_SERIALIZE(serializer, m_state.rate);
    G_SERIALIZE(serializer, m_state.buttons);
    G_SERIALIZE(serializer, m_state.x);
    G_SERIALIZE(serializer, m_state.y);
    G_SERIALIZE(serializer, m_state.remainder_x);
    G_SERIALIZE(serializer, m_state.remainder_y);
    G_SERIALIZE(serializer, m_state.receive_clocks);
    G_SERIALIZE(serializer, m_state.send_clocks);
    G_SERIALIZE(serializer, m_state.sample_clocks);
}

void PS2Mouse::SanitizeState()
{
    bool valid = false;

    for (int i = 0; i < 7 && !valid; i++)
        valid = m_state.rate == k_ps2_mouse_rates[i];

    if (!valid)
        m_state.rate = k_ps2_mouse_default_rate;

    m_state.reply_count = (u8)MIN((int)m_state.reply_count, PS2_MOUSE_REPLY_SIZE);
    m_state.last_count = (u8)MIN((int)m_state.last_count, PS2_MOUSE_GROUP_SIZE);
    m_state.resolution &= 0x03;
    m_state.remainder_x = CLAMP(m_state.remainder_x, -3, 3);
    m_state.remainder_y = CLAMP(m_state.remainder_y, -3, 3);
    m_state.x = CLAMP(m_state.x, -k_ps2_mouse_count_limit, k_ps2_mouse_count_limit);
    m_state.y = CLAMP(m_state.y, -k_ps2_mouse_count_limit, k_ps2_mouse_count_limit);
}
