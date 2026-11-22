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


#include "keyboard.h"
#include "../common/trace_logger.h"
#include "../common/state_serializer.h"

// Set 2 make code of each set 1 code, E0 keys share the low 7 bits with their plain counterpart
static const u8 k_keyboard_set2[0x80] =
{
    0x00, 0x76, 0x16, 0x1E, 0x26, 0x25, 0x2E, 0x36, 0x3D, 0x3E, 0x46, 0x45, 0x4E, 0x55, 0x66, 0x0D,
    0x15, 0x1D, 0x24, 0x2D, 0x2C, 0x35, 0x3C, 0x43, 0x44, 0x4D, 0x54, 0x5B, 0x5A, 0x14, 0x1C, 0x1B,
    0x23, 0x2B, 0x34, 0x33, 0x3B, 0x42, 0x4B, 0x4C, 0x52, 0x0E, 0x12, 0x5D, 0x1A, 0x22, 0x21, 0x2A,
    0x32, 0x31, 0x3A, 0x41, 0x49, 0x4A, 0x59, 0x7C, 0x11, 0x29, 0x58, 0x05, 0x06, 0x04, 0x0C, 0x03,
    0x0B, 0x83, 0x0A, 0x01, 0x09, 0x77, 0x7E, 0x6C, 0x75, 0x7D, 0x7B, 0x6B, 0x73, 0x74, 0x79, 0x69,
    0x72, 0x7A, 0x70, 0x71, 0x84, 0x00, 0x61, 0x78, 0x07, 0x00, 0x00, 0x1F, 0x27, 0x2F, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

Keyboard::Keyboard()
{
    m_trace_logger = TraceLogger::GetDisabled();
    memset(&m_state, 0, sizeof(m_state));
}

Keyboard::~Keyboard()
{
}

void Keyboard::Init()
{
    Reset(0);
}

void Keyboard::SetTraceLogger(TraceLogger* trace_logger)
{
    m_trace_logger = TraceLogger::Resolve(trace_logger);
}

// The self-test at power on ends with the same AAh as a reset command
void Keyboard::Reset(u64 clocks)
{
    memset(&m_state, 0, sizeof(m_state));
    m_state.receive_clocks = GDOX_NO_EVENT;
    m_state.send_clocks = GDOX_NO_EVENT;
    m_state.self_test_clocks = clocks + k_keyboard_power_on_clocks;
    SetDefaults();
}

bool Keyboard::HandleEvent(u64 clocks, u8& value)
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
    else if (m_state.self_test_clocks <= clocks)
    {
        m_state.self_test_clocks = GDOX_NO_EVENT;
        m_state.scanning = true;
        QueueReply(k_keyboard_self_test_passed);
    }
    else if (m_state.repeat_clocks <= clocks)
    {
        RepeatKey(clocks);
    }

    UpdateSend(clocks);
    return false;
}

void Keyboard::SetClock(bool enabled, u64 clocks)
{
    m_state.clock_enabled = enabled;
    UpdateSend(clocks);
}

// The line carries one byte at a time, the one on its way out starts over afterwards
void Keyboard::Receive(u8 value, u64 clocks)
{
    m_state.received = value;
    m_state.receive_clocks = clocks + k_keyboard_byte_clocks;
    m_state.send_clocks = GDOX_NO_EVENT;
}

bool Keyboard::KeyPressed(GDOX_Keys key, u64 clocks)
{
    if (!IsValidKey(key))
        return false;

    if (m_state.keys[key] != 0)
        return true;

    if (!m_state.scanning)
    {
        if (unlikely(m_trace_logger->IsEventEnabled(TRACE_KEYBOARD, TRACE_KEYBOARD_REJECT)))
            TraceReject(key, 0);

        return false;
    }

    u8 flags = GetMakeFlags(key);
    u8 count = m_state.buffer_count;
    QueueMake(key, flags);

    if (m_state.buffer_count > KEYBOARD_BUFFER_SIZE)
    {
        m_state.buffer_count = count;
        QueueOverrun();
        UpdateSend(clocks);

        if (unlikely(m_trace_logger->IsEventEnabled(TRACE_KEYBOARD, TRACE_KEYBOARD_REJECT)))
            TraceReject(key, 1);

        return false;
    }

    m_state.overrun = false;
    m_state.keys[key] = flags;

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_KEYBOARD, TRACE_KEYBOARD_KEY)))
        TraceKey(0x01, key, count);

    if (key == GDOX_KEY_NUM_LOCK)
        m_state.num_lock = !m_state.num_lock;

    if (key == GDOX_KEY_PAUSE)
    {
        m_state.repeat_key = GDOX_KEY_NONE;
        m_state.repeat_clocks = GDOX_NO_EVENT;
    }
    else
    {
        m_state.repeat_key = (u8)key;
        m_state.repeat_clocks = clocks + (((u64)GetTypematicDelay() * GDOX_MACHINE_CLOCK_RATE) / 1000);
    }

    UpdateSend(clocks);
    return true;
}

// A release is never dropped: makes stop at the 16 bytes of the real buffer, the capacity above that is for breaks
void Keyboard::KeyReleased(GDOX_Keys key, u64 clocks)
{
    if (!IsValidKey(key) || m_state.keys[key] == 0)
        return;

    u8 flags = m_state.keys[key];
    u8 count = m_state.buffer_count;
    m_state.keys[key] = 0;

    if (m_state.repeat_key == key)
    {
        m_state.repeat_key = GDOX_KEY_NONE;
        m_state.repeat_clocks = GDOX_NO_EVENT;
    }

    if (!m_state.scanning)
        return;

    QueueBreak(key, flags);

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_KEYBOARD, TRACE_KEYBOARD_KEY)))
        TraceKey(0x00, key, count);

    UpdateSend(clocks);
}

void Keyboard::ReleaseAllKeys(u64 clocks)
{
    for (int i = GDOX_KEY_NONE + 1; i < GDOX_KEY_COUNT; i++)
        KeyReleased((GDOX_Keys)i, clocks);
}

u32 Keyboard::GetTypematicDelay() const
{
    return (((m_state.typematic >> 5) & 0x03) + 1) * 250;
}

// In microseconds: (8 + A) x 2^B x 4.17 ms
u32 Keyboard::GetTypematicPeriod() const
{
    return ((8 + (m_state.typematic & 0x07)) << ((m_state.typematic >> 3) & 0x03)) * 4170;
}

void Keyboard::ProcessByte(u8 value, u64 clocks)
{
    u8 command = m_state.command;
    m_state.command = 0;

    if (unlikely(m_trace_logger->IsEventEnabled(TRACE_KEYBOARD, TRACE_KEYBOARD_COMMAND)))
        TraceCommand(value, command);

    // A command in place of a parameter drops the one that was waiting
    if (command == 0 || (value & 0x80) != 0)
    {
        ProcessCommand(value, clocks);
        return;
    }

    QueueReply(k_keyboard_ack);

    switch (command)
    {
        case 0xED:
            m_state.leds = value & 0x07;
            m_state.num_lock = (value & 0x02) != 0;
            break;
        case 0xF0:
            if (value == 0)
            {
                QueueReply(m_state.scan_set);
                break;
            }

            ClearBuffer();
            m_state.scan_set = value == 1 ? 1 : 2;
            break;
        case 0xF3:
            m_state.typematic = value;
            break;
        case 0xFB:
        case 0xFC:
        case 0xFD:
            m_state.command = command;
            break;
        default:
            break;
    }
}

void Keyboard::ProcessCommand(u8 value, u64 clocks)
{
    switch (value)
    {
        case 0xED:
        case 0xF0:
        case 0xF3:
            m_state.command = value;
            QueueReply(k_keyboard_ack);
            break;
        case 0xEE:
            QueueReply(0xEE);
            break;
        case 0xF2:
            QueueReply(k_keyboard_ack);
            QueueReply(0xAB);
            QueueReply(0x83);
            break;
        case 0xF4:
            ClearBuffer();
            m_state.scanning = true;
            QueueReply(k_keyboard_ack);
            break;
        case 0xF5:
            SetDefaults();
            m_state.scanning = false;
            QueueReply(k_keyboard_ack);
            break;
        case 0xF6:
            SetDefaults();
            QueueReply(k_keyboard_ack);
            break;
        case 0xF7:
        case 0xF8:
        case 0xF9:
        case 0xFA:
            ClearBuffer();
            QueueReply(k_keyboard_ack);
            break;
        case 0xFB:
        case 0xFC:
        case 0xFD:
            ClearBuffer();
            m_state.command = value;
            QueueReply(k_keyboard_ack);
            break;
        case 0xFE:
            QueueReply(m_state.last_byte);
            break;
        case 0xFF:
            SetDefaults();
            memset(m_state.keys, 0, sizeof(m_state.keys));
            m_state.leds = 0;
            m_state.num_lock = false;
            m_state.scanning = false;
            m_state.self_test_clocks = clocks + k_keyboard_self_test_clocks;
            QueueReply(k_keyboard_ack);
            break;
        default:
            QueueReply(k_keyboard_resend);
            break;
    }
}

// The lamps and the keys held down stay as they are
void Keyboard::SetDefaults()
{
    ClearBuffer();
    m_state.scan_set = 2;
    m_state.typematic = k_keyboard_typematic_default;
}

void Keyboard::ClearBuffer()
{
    m_state.buffer_read = 0;
    m_state.buffer_count = 0;
    m_state.overrun = false;
    m_state.repeat_key = GDOX_KEY_NONE;
    m_state.repeat_clocks = GDOX_NO_EVENT;
}

void Keyboard::QueueReply(u8 value)
{
    if (m_state.reply_count < KEYBOARD_REPLY_SIZE)
        m_state.reply[m_state.reply_count++] = value;
}

void Keyboard::QueueByte(u8 value)
{
    if (m_state.buffer_count == KEYBOARD_BUFFER_CAPACITY)
        return;

    u8 write = (u8)(m_state.buffer_read + m_state.buffer_count);
    m_state.buffer[write & (KEYBOARD_BUFFER_CAPACITY - 1)] = value;
    m_state.buffer_count++;
}

void Keyboard::QueueCode(u8 code, bool extended, bool make)
{
    if (extended)
        QueueByte(0xE0);

    if (m_state.scan_set == 1)
    {
        QueueByte(make ? code : (u8)(code | 0x80));
        return;
    }

    if (!make)
        QueueByte(0xF0);

    QueueByte(k_keyboard_set2[code]);
}

void Keyboard::QueueKey(GDOX_Keys key, u8 flags, bool make)
{
    if ((flags & k_keyboard_sysrq) != 0)
        QueueCode(GDOX_KEY_SYSRQ, false, make);
    else
        QueueCode((u8)(key & 0x7F), (key & 0x80) != 0, make);
}

void Keyboard::QueueMake(GDOX_Keys key, u8 flags)
{
    if (key == GDOX_KEY_PAUSE)
    {
        QueuePause();
        return;
    }

    if ((flags & k_keyboard_left_shift_up) != 0)
        QueueCode(GDOX_KEY_LEFT_SHIFT, true, false);

    if ((flags & k_keyboard_right_shift_up) != 0)
        QueueCode(GDOX_KEY_RIGHT_SHIFT, true, false);

    if ((flags & k_keyboard_fake_shift) != 0)
        QueueCode(GDOX_KEY_LEFT_SHIFT, true, true);

    QueueKey(key, flags, true);
}

// A shift released while the key was down has already gone out for real, so it comes back only if still held
void Keyboard::QueueBreak(GDOX_Keys key, u8 flags)
{
    if (key == GDOX_KEY_PAUSE)
        return;

    QueueKey(key, flags, false);

    if ((flags & k_keyboard_fake_shift) != 0)
        QueueCode(GDOX_KEY_LEFT_SHIFT, true, false);

    if ((flags & k_keyboard_right_shift_up) != 0 && m_state.keys[GDOX_KEY_RIGHT_SHIFT] != 0)
        QueueCode(GDOX_KEY_RIGHT_SHIFT, true, true);

    if ((flags & k_keyboard_left_shift_up) != 0 && m_state.keys[GDOX_KEY_LEFT_SHIFT] != 0)
        QueueCode(GDOX_KEY_LEFT_SHIFT, true, true);
}

// Pause sends its make and break on press, nothing on release. With Ctrl it is Break
void Keyboard::QueuePause()
{
    if (IsHeld(GDOX_KEY_LEFT_CTRL, GDOX_KEY_RIGHT_CTRL))
    {
        QueueCode(GDOX_KEY_SCROLL_LOCK, true, true);
        QueueCode(GDOX_KEY_SCROLL_LOCK, true, false);
        return;
    }

    QueueByte(0xE1);
    QueueCode(GDOX_KEY_LEFT_CTRL, false, true);
    QueueCode(GDOX_KEY_NUM_LOCK, false, true);
    QueueByte(0xE1);
    QueueCode(GDOX_KEY_LEFT_CTRL, false, false);
    QueueCode(GDOX_KEY_NUM_LOCK, false, false);
}

void Keyboard::QueueOverrun()
{
    if (m_state.overrun)
        return;

    m_state.overrun = true;
    QueueByte(m_state.scan_set == 1 ? 0xFF : 0x00);
}

// Only the key itself repeats, without its fake shifts, and an inhibited interface stores no repeats
void Keyboard::RepeatKey(u64 clocks)
{
    GDOX_Keys key = (GDOX_Keys)m_state.repeat_key;
    u8 count = m_state.buffer_count;

    if (m_state.clock_enabled)
    {
        QueueKey(key, m_state.keys[key], true);

        if (m_state.buffer_count > KEYBOARD_BUFFER_SIZE)
            m_state.buffer_count = count;
        else if (unlikely(m_trace_logger->IsEventEnabled(TRACE_KEYBOARD, TRACE_KEYBOARD_KEY)))
            TraceKey(0x02, key, count);
    }

    m_state.repeat_clocks = clocks + (((u64)GetTypematicPeriod() * GDOX_MACHINE_CLOCK_RATE) / 1000000);
}

void Keyboard::UpdateSend(u64 clocks)
{
    bool pending = m_state.reply_count != 0 || m_state.buffer_count != 0;

    if (!pending || !m_state.clock_enabled || IsReceiving())
        m_state.send_clocks = GDOX_NO_EVENT;
    else if (m_state.send_clocks == GDOX_NO_EVENT)
        m_state.send_clocks = clocks + k_keyboard_byte_clocks;
}

// With NumLock on the grey keys pretend Shift is down, with Shift down they pretend it is up, and both cancel out
u8 Keyboard::GetMakeFlags(GDOX_Keys key) const
{
    bool left = m_state.keys[GDOX_KEY_LEFT_SHIFT] != 0;
    bool right = m_state.keys[GDOX_KEY_RIGHT_SHIFT] != 0;
    bool shift = left || right;
    u8 flags = k_keyboard_held;
    u8 shift_up = (u8)((left ? k_keyboard_left_shift_up : 0) | (right ? k_keyboard_right_shift_up : 0));

    if (key == GDOX_KEY_PRINT_SCREEN)
    {
        if (IsHeld(GDOX_KEY_LEFT_ALT, GDOX_KEY_RIGHT_ALT))
            flags |= k_keyboard_sysrq;
        else if (!shift && !IsHeld(GDOX_KEY_LEFT_CTRL, GDOX_KEY_RIGHT_CTRL))
            flags |= k_keyboard_fake_shift;
    }
    else if (key == GDOX_KEY_KP_DIVIDE)
    {
        flags |= shift_up;
    }
    else if (IsNavigationKey(key))
    {
        if (shift && !m_state.num_lock)
            flags |= shift_up;
        else if (!shift && m_state.num_lock)
            flags |= k_keyboard_fake_shift;
    }

    return flags;
}

bool Keyboard::IsValidKey(GDOX_Keys key) const
{
    return key > GDOX_KEY_NONE && key < GDOX_KEY_COUNT && k_keyboard_set2[key & 0x7F] != 0;
}

bool Keyboard::IsNavigationKey(GDOX_Keys key) const
{
    switch (key)
    {
        case GDOX_KEY_HOME:
        case GDOX_KEY_UP:
        case GDOX_KEY_PAGE_UP:
        case GDOX_KEY_LEFT:
        case GDOX_KEY_RIGHT:
        case GDOX_KEY_END:
        case GDOX_KEY_DOWN:
        case GDOX_KEY_PAGE_DOWN:
        case GDOX_KEY_INSERT:
        case GDOX_KEY_DELETE:
            return true;
        default:
            return false;
    }
}

bool Keyboard::IsHeld(GDOX_Keys left, GDOX_Keys right) const
{
    return m_state.keys[left] != 0 || m_state.keys[right] != 0;
}

u8 Keyboard::PopByte()
{
    u8 value = 0;

    if (m_state.reply_count != 0)
    {
        value = m_state.reply[0];
        m_state.reply_count--;
        memmove(m_state.reply, m_state.reply + 1, m_state.reply_count);
    }
    else if (m_state.buffer_count != 0)
    {
        value = m_state.buffer[m_state.buffer_read];
        m_state.buffer_read = (m_state.buffer_read + 1) & (KEYBOARD_BUFFER_CAPACITY - 1);
        m_state.buffer_count--;
    }

    if (value != k_keyboard_resend)
        m_state.last_byte = value;

    return value;
}

void Keyboard::TraceKey(u8 kind, GDOX_Keys key, u8 count)
{
    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_KEYBOARD, TRACE_KEYBOARD_KEY);
    u8 bytes = (u8)(m_state.buffer_count - count);
    entry->keyboard.value = bytes;
    entry->keyboard.flags = kind;
    entry->keyboard.key = (u8)key;
    entry->keyboard.pending = m_state.buffer_count;
    entry->keyboard.scan_set = m_state.scan_set;
    entry->keyboard.count = (u8)MIN(bytes, (u8)8);

    for (u8 i = 0; i < entry->keyboard.count; i++)
        entry->keyboard.bytes[i] = m_state.buffer[(u8)(m_state.buffer_read + count + i) & (KEYBOARD_BUFFER_CAPACITY - 1)];
}

void Keyboard::TraceReject(GDOX_Keys key, u8 reason)
{
    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_KEYBOARD, TRACE_KEYBOARD_REJECT);
    entry->keyboard.key = (u8)key;
    entry->keyboard.reason = reason;
    entry->keyboard.pending = m_state.buffer_count;
}

void Keyboard::TraceCommand(u8 value, u8 command)
{
    GDOX_Trace_Entry* entry = m_trace_logger->Begin(TRACE_KEYBOARD, TRACE_KEYBOARD_COMMAND);
    entry->keyboard.value = value;
    entry->keyboard.flags = TRACE_KEYBOARD_TARGET_KEYBOARD;
    entry->keyboard.key = command;
    entry->keyboard.pending = m_state.buffer_count;
    entry->keyboard.scan_set = m_state.scan_set;

    if (command != 0 && (value & 0x80) != 0)
        entry->flags |= TRACE_KEYBOARD_FLAG_AS_COMMAND;
}

void Keyboard::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
}

void Keyboard::LoadState(std::istream& stream, int version)
{
    UNUSED(version);
    StateSerializer serializer(stream);
    Serialize(serializer);
    SanitizeState();
}

void Keyboard::Serialize(StateSerializer& serializer)
{
    G_SERIALIZE_ARRAY(serializer, m_state.buffer, KEYBOARD_BUFFER_CAPACITY);
    G_SERIALIZE(serializer, m_state.buffer_read);
    G_SERIALIZE(serializer, m_state.buffer_count);
    G_SERIALIZE(serializer, m_state.overrun);
    G_SERIALIZE_ARRAY(serializer, m_state.reply, KEYBOARD_REPLY_SIZE);
    G_SERIALIZE(serializer, m_state.reply_count);
    G_SERIALIZE(serializer, m_state.last_byte);
    G_SERIALIZE(serializer, m_state.command);
    G_SERIALIZE(serializer, m_state.received);
    G_SERIALIZE(serializer, m_state.clock_enabled);
    G_SERIALIZE(serializer, m_state.scanning);
    G_SERIALIZE(serializer, m_state.scan_set);
    G_SERIALIZE(serializer, m_state.leds);
    G_SERIALIZE(serializer, m_state.num_lock);
    G_SERIALIZE(serializer, m_state.typematic);
    G_SERIALIZE_ARRAY(serializer, m_state.keys, GDOX_KEY_COUNT);
    G_SERIALIZE(serializer, m_state.repeat_key);
    G_SERIALIZE(serializer, m_state.repeat_clocks);
    G_SERIALIZE(serializer, m_state.receive_clocks);
    G_SERIALIZE(serializer, m_state.send_clocks);
    G_SERIALIZE(serializer, m_state.self_test_clocks);
}

void Keyboard::SanitizeState()
{
    m_state.buffer_read &= KEYBOARD_BUFFER_CAPACITY - 1;
    m_state.buffer_count = (u8)MIN(m_state.buffer_count, KEYBOARD_BUFFER_CAPACITY);
    m_state.reply_count = (u8)MIN(m_state.reply_count, KEYBOARD_REPLY_SIZE);
    m_state.leds &= 0x07;
    m_state.typematic &= 0x7F;

    if (m_state.scan_set != 1)
        m_state.scan_set = 2;

    if (!IsValidKey((GDOX_Keys)m_state.repeat_key) || m_state.keys[m_state.repeat_key] == 0)
    {
        m_state.repeat_key = GDOX_KEY_NONE;
        m_state.repeat_clocks = GDOX_NO_EVENT;
    }
}
