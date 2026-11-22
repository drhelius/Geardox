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

#include <string.h>
#include "opl.h"
#include "audio_common.h"
#include "../common/state_serializer.h"

// Die ROM contents: round(-log2(sin((i + 0.5) * pi / 512)) * 256)
static const u16 k_opl_log_sine[256] =
{
/* 0x00 */ 0x859, 0x6C3, 0x607, 0x58B, 0x52E, 0x4E4, 0x4A6, 0x471, 0x443, 0x41A, 0x3F5, 0x3D3, 0x3B5, 0x398, 0x37E, 0x365,
/* 0x10 */ 0x34E, 0x339, 0x324, 0x311, 0x2FF, 0x2ED, 0x2DC, 0x2CD, 0x2BD, 0x2AF, 0x2A0, 0x293, 0x286, 0x279, 0x26D, 0x261,
/* 0x20 */ 0x256, 0x24B, 0x240, 0x236, 0x22C, 0x222, 0x218, 0x20F, 0x206, 0x1FD, 0x1F5, 0x1EC, 0x1E4, 0x1DC, 0x1D4, 0x1CD,
/* 0x30 */ 0x1C5, 0x1BE, 0x1B7, 0x1B0, 0x1A9, 0x1A2, 0x19B, 0x195, 0x18F, 0x188, 0x182, 0x17C, 0x177, 0x171, 0x16B, 0x166,
/* 0x40 */ 0x160, 0x15B, 0x155, 0x150, 0x14B, 0x146, 0x141, 0x13C, 0x137, 0x133, 0x12E, 0x129, 0x125, 0x121, 0x11C, 0x118,
/* 0x50 */ 0x114, 0x10F, 0x10B, 0x107, 0x103, 0x0FF, 0x0FB, 0x0F8, 0x0F4, 0x0F0, 0x0EC, 0x0E9, 0x0E5, 0x0E2, 0x0DE, 0x0DB,
/* 0x60 */ 0x0D7, 0x0D4, 0x0D1, 0x0CD, 0x0CA, 0x0C7, 0x0C4, 0x0C1, 0x0BE, 0x0BB, 0x0B8, 0x0B5, 0x0B2, 0x0AF, 0x0AC, 0x0A9,
/* 0x70 */ 0x0A7, 0x0A4, 0x0A1, 0x09F, 0x09C, 0x099, 0x097, 0x094, 0x092, 0x08F, 0x08D, 0x08A, 0x088, 0x086, 0x083, 0x081,
/* 0x80 */ 0x07F, 0x07D, 0x07A, 0x078, 0x076, 0x074, 0x072, 0x070, 0x06E, 0x06C, 0x06A, 0x068, 0x066, 0x064, 0x062, 0x060,
/* 0x90 */ 0x05E, 0x05C, 0x05B, 0x059, 0x057, 0x055, 0x053, 0x052, 0x050, 0x04E, 0x04D, 0x04B, 0x04A, 0x048, 0x046, 0x045,
/* 0xA0 */ 0x043, 0x042, 0x040, 0x03F, 0x03E, 0x03C, 0x03B, 0x039, 0x038, 0x037, 0x035, 0x034, 0x033, 0x031, 0x030, 0x02F,
/* 0xB0 */ 0x02E, 0x02D, 0x02B, 0x02A, 0x029, 0x028, 0x027, 0x026, 0x025, 0x024, 0x023, 0x022, 0x021, 0x020, 0x01F, 0x01E,
/* 0xC0 */ 0x01D, 0x01C, 0x01B, 0x01A, 0x019, 0x018, 0x017, 0x017, 0x016, 0x015, 0x014, 0x014, 0x013, 0x012, 0x011, 0x011,
/* 0xD0 */ 0x010, 0x00F, 0x00F, 0x00E, 0x00D, 0x00D, 0x00C, 0x00C, 0x00B, 0x00A, 0x00A, 0x009, 0x009, 0x008, 0x008, 0x007,
/* 0xE0 */ 0x007, 0x007, 0x006, 0x006, 0x005, 0x005, 0x005, 0x004, 0x004, 0x004, 0x003, 0x003, 0x003, 0x002, 0x002, 0x002,
/* 0xF0 */ 0x002, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000
};

// Die ROM contents: round((2^(i / 256) - 1) * 1024)
static const u16 k_opl_exp[256] =
{
/* 0x00 */ 0x000, 0x003, 0x006, 0x008, 0x00B, 0x00E, 0x011, 0x014, 0x016, 0x019, 0x01C, 0x01F, 0x022, 0x025, 0x028, 0x02A,
/* 0x10 */ 0x02D, 0x030, 0x033, 0x036, 0x039, 0x03C, 0x03F, 0x042, 0x045, 0x048, 0x04B, 0x04E, 0x051, 0x054, 0x057, 0x05A,
/* 0x20 */ 0x05D, 0x060, 0x063, 0x066, 0x069, 0x06C, 0x06F, 0x072, 0x075, 0x078, 0x07B, 0x07E, 0x082, 0x085, 0x088, 0x08B,
/* 0x30 */ 0x08E, 0x091, 0x094, 0x098, 0x09B, 0x09E, 0x0A1, 0x0A4, 0x0A8, 0x0AB, 0x0AE, 0x0B1, 0x0B5, 0x0B8, 0x0BB, 0x0BE,
/* 0x40 */ 0x0C2, 0x0C5, 0x0C8, 0x0CC, 0x0CF, 0x0D2, 0x0D6, 0x0D9, 0x0DC, 0x0E0, 0x0E3, 0x0E7, 0x0EA, 0x0ED, 0x0F1, 0x0F4,
/* 0x50 */ 0x0F8, 0x0FB, 0x0FF, 0x102, 0x106, 0x109, 0x10C, 0x110, 0x114, 0x117, 0x11B, 0x11E, 0x122, 0x125, 0x129, 0x12C,
/* 0x60 */ 0x130, 0x134, 0x137, 0x13B, 0x13E, 0x142, 0x146, 0x149, 0x14D, 0x151, 0x154, 0x158, 0x15C, 0x160, 0x163, 0x167,
/* 0x70 */ 0x16B, 0x16F, 0x172, 0x176, 0x17A, 0x17E, 0x181, 0x185, 0x189, 0x18D, 0x191, 0x195, 0x199, 0x19C, 0x1A0, 0x1A4,
/* 0x80 */ 0x1A8, 0x1AC, 0x1B0, 0x1B4, 0x1B8, 0x1BC, 0x1C0, 0x1C4, 0x1C8, 0x1CC, 0x1D0, 0x1D4, 0x1D8, 0x1DC, 0x1E0, 0x1E4,
/* 0x90 */ 0x1E8, 0x1EC, 0x1F0, 0x1F5, 0x1F9, 0x1FD, 0x201, 0x205, 0x209, 0x20E, 0x212, 0x216, 0x21A, 0x21E, 0x223, 0x227,
/* 0xA0 */ 0x22B, 0x230, 0x234, 0x238, 0x23C, 0x241, 0x245, 0x249, 0x24E, 0x252, 0x257, 0x25B, 0x25F, 0x264, 0x268, 0x26D,
/* 0xB0 */ 0x271, 0x276, 0x27A, 0x27F, 0x283, 0x288, 0x28C, 0x291, 0x295, 0x29A, 0x29E, 0x2A3, 0x2A8, 0x2AC, 0x2B1, 0x2B5,
/* 0xC0 */ 0x2BA, 0x2BF, 0x2C4, 0x2C8, 0x2CD, 0x2D2, 0x2D6, 0x2DB, 0x2E0, 0x2E5, 0x2E9, 0x2EE, 0x2F3, 0x2F8, 0x2FD, 0x302,
/* 0xD0 */ 0x306, 0x30B, 0x310, 0x315, 0x31A, 0x31F, 0x324, 0x329, 0x32E, 0x333, 0x338, 0x33D, 0x342, 0x347, 0x34C, 0x351,
/* 0xE0 */ 0x356, 0x35B, 0x360, 0x365, 0x36A, 0x370, 0x375, 0x37A, 0x37F, 0x384, 0x38A, 0x38F, 0x394, 0x399, 0x39F, 0x3A4,
/* 0xF0 */ 0x3A9, 0x3AE, 0x3B4, 0x3B9, 0x3BF, 0x3C4, 0x3C9, 0x3CF, 0x3D4, 0x3DA, 0x3DF, 0x3E4, 0x3EA, 0x3EF, 0x3F5, 0x3FA
};

static const u8 k_opl_multiple[16] = { 1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30 };
static const u8 k_opl_key_scale_rom[16] = { 0, 32, 40, 45, 48, 51, 53, 55, 56, 58, 59, 60, 61, 62, 63, 64 };
static const u8 k_opl_key_scale_shift[4] = { 8, 1, 2, 0 };
static const u8 k_opl_envelope_step[4][4] =
{
    { 0, 0, 0, 0 },
    { 1, 0, 0, 0 },
    { 1, 0, 1, 0 },
    { 1, 1, 1, 0 }
};

// Register low bits to operator within an array, slot numbering runs in groups of 3 with gaps
static const s8 k_opl_register_operator[0x20] =
{
    0, 1, 2, 3, 4, 5, -1, -1, 6, 7, 8, 9, 10, 11, -1, -1,
    12, 13, 14, 15, 16, 17, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

// On an OPL2 the decoder doesn't look at every bit, so 9-B and E-F land on low channels
static const s8 k_opl2_register_channel[0x10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 0, 1, 2, -1, -1, 0, 1 };
static const s8 k_opl2_register_pending[0x10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, -1, -1, 0, 1 };
static const u8 k_opl_channel_operator[OPL_CHANNEL_COUNT] =
{
    0, 1, 2, 6, 7, 8, 12, 13, 14, 18, 19, 20, 24, 25, 26, 30, 31, 32
};

static const int k_opl_hihat_operator = 13;
static const int k_opl_snare_operator = 16;
static const int k_opl_cymbal_operator = 17;
static const u64 k_opl_rhythm_operators = (1ULL << k_opl_hihat_operator) | (1ULL << k_opl_cymbal_operator);
static const u64 k_opl_all_operators = (1ULL << OPL_OPERATOR_COUNT) - 1;
static const u64 k_opl_first_operators = 0x1C71C71C7ULL;
static const u64 k_opl_paired_operators = 0x00FC003FULL;
static const u8 k_opl_key_normal = 0x01;
static const u8 k_opl_key_drum = 0x02;
static const u64 k_opl2_envelope_timer_limit = 0x3FFFF;
static const u64 k_opl3_envelope_timer_limit = 0xFFFFFFFFFULL;

OPL::OPL()
{
    m_model = OPL_MODEL_OPL3;

    for (int i = 0; i < OPL_CHANNEL_COUNT; i++)
        m_channel_mask[i] = 0xFFFF;

    Reset();
}

OPL::~OPL()
{
}

void OPL::Init(OPL_Model model)
{
    m_model = model;
    Reset();
}

void OPL::SetModel(OPL_Model model)
{
    m_model = model;
}

void OPL::Reset()
{
    memset(&m_state, 0, sizeof(m_state));

    for (int i = 0; i < OPL_OPERATOR_COUNT; i++)
    {
        OPL_Operator& op = m_state.operators[i];
        op.modulation = k_opl_zero_signal;
        op.envelope = k_opl_envelope_silent;
        op.envelope_output = k_opl_envelope_silent;
        op.envelope_state = OPL_ENVELOPE_RELEASE;
        op.muted = 1;
    }

    for (int i = 0; i < OPL_CHANNEL_COUNT; i++)
    {
        OPL_Channel& channel = m_state.channels[i];
        int position = i % 9;
        channel.operators[0] = k_opl_channel_operator[i];
        channel.operators[1] = k_opl_channel_operator[i] + 3;
        channel.pair = (u8)(position < 3 ? i + 3 : position < 6 ? i - 3 : i);
        channel.type = OPL_CHANNEL_2OP;
        channel.pan_left = 0xFFFF;
        channel.pan_right = 0xFFFF;

        for (int j = 0; j < 4; j++)
            channel.outputs[j] = k_opl_zero_signal;

        m_state.operators[channel.operators[0]].channel = (u8)i;
        m_state.operators[channel.operators[1]].channel = (u8)i;
        SetupAlgorithm(i);
    }

    m_state.noise = 1;
    m_state.tremolo_shift = 4;
    m_state.vibrato_shift = 1;
}

void OPL::Synchronize()
{
    u64 cycles = m_state.elapsed_cycles;
    m_state.elapsed_cycles = 0;
    RunCycles(cycles);
}

void OPL::Sample(s16& left, s16& right)
{
    Synchronize();

    s32 phase = (s32)m_state.native_cycle;
    s32 left_delta = m_state.left_sample - m_state.previous_left_sample;
    s32 right_delta = m_state.right_sample - m_state.previous_right_sample;
    left = (s16)(m_state.previous_left_sample + (left_delta * phase) / (s32)k_opl_native_sample_cycles);
    right = (s16)(m_state.previous_right_sample + (right_delta * phase) / (s32)k_opl_native_sample_cycles);
}

void OPL::RunCycles(u64 cycles)
{
    while (cycles >= k_opl_native_sample_cycles - m_state.native_cycle)
    {
        cycles -= k_opl_native_sample_cycles - m_state.native_cycle;
        m_state.native_cycle = 0;
        m_state.previous_left_sample = m_state.left_sample;
        m_state.previous_right_sample = m_state.right_sample;
        GenerateNativeSample();
    }

    m_state.native_cycle += (u32)cycles;
}

void OPL::GenerateNativeSample()
{
    if (m_model == OPL_MODEL_OPL2)
        GenerateOPL2();
    else
        GenerateOPL3();

    UpdateTimers();
    UpdateModulators();
    UpdateEnvelopeTimer();
}

// The mix is taken partway through the slots, so the last ones land in the next sample
void OPL::GenerateOPL2()
{
    s16 output = ConvertDAC(m_state.mix);

    m_state.left_sample = output;
    m_state.right_sample = output;

    RunOperators(0, 15);
    m_state.mix = MixOPL2();
    RunOperators(15, OPL2_OPERATOR_COUNT);
    m_state.pending_low = 0;
    m_state.pending_high = 0;
}

// Each side is taken at its own point in the slots, and the right one comes out a sample later
void OPL::GenerateOPL3()
{
    s32 right = m_state.mix;

    RunOperators(0, 15);
    s32 left = MixOPL3(true);
    RunOperators(15, 33);
    m_state.mix = MixOPL3(false);
    RunOperators(33, OPL_OPERATOR_COUNT);

    m_state.left_sample = clamp_s16(left);
    m_state.right_sample = clamp_s16(right);
}

// A channel that has settled can only start again through a key on, which resets its phases
// A settled operator still feeds a channel that sounds, and the drums' phase bits free run in two slots
void OPL::RunOperators(int first, int last)
{
    u64 running = m_state.active_operators | k_opl_rhythm_operators;

    running |= ((running & k_opl_first_operators) << 3) | ((running >> 3) & k_opl_first_operators);

    if (m_state.registers[1][0x04] != 0)
        running |= ((running & k_opl_paired_operators) << 6) | ((running >> 6) & k_opl_paired_operators);

    for (int i = first; i < last; i++)
    {
        if ((((running | m_state.active_operators) >> i) & 1) != 0)
            RunOperator(i);
        else
        {
            m_state.signals[i] = 0;
            m_state.signals[k_opl_feedback_signal + i] = 0;
            m_state.operators[i].previous_output = 0;
            m_state.operators[i].muted = 1;
        }

        u32 noise = m_state.noise;
        m_state.noise = (noise >> 1) | ((((noise >> 14) ^ noise) & 0x01) << 22);

        if ((m_state.pending_low | m_state.pending_high) != 0)
            ApplyPending(i);
    }
}

void OPL::RunOperator(int index)
{
    OPL_Operator& op = m_state.operators[index];
    const OPL_Channel& channel = m_state.channels[op.channel];
    s16 output = m_state.signals[index];

    m_state.signals[k_opl_feedback_signal + index] =
        channel.feedback != 0 ? (s16)((op.previous_output + output) >> (9 - channel.feedback)) : 0;
    op.previous_output = output;

    UpdateEnvelope(op, channel);
    UpdatePhase(op, channel, index);

    m_state.signals[index] =
        CalculateWave((u16)(op.phase_output + m_state.signals[op.modulation]), op.envelope_output, op.waveform);

    if ((op.key | m_state.csm_key) == 0 && op.envelope_state == OPL_ENVELOPE_RELEASE && op.envelope == k_opl_envelope_silent)
        m_state.active_operators &= ~(1ULL << index);
}

void OPL::UpdateEnvelope(OPL_Operator& op, const OPL_Channel& channel)
{
    u8 key = op.key | m_state.csm_key;
    bool reset = (key != 0) && (op.envelope_state == OPL_ENVELOPE_RELEASE);
    u8 register_rate = 0;

    op.envelope_output = (u16)(op.envelope + (op.total_level << 2) +
        (op.key_scale_attenuation >> k_opl_key_scale_shift[op.key_scale_level]) + (op.tremolo ? m_state.tremolo : 0));

    if (reset)
        register_rate = op.attack_rate;
    else
    {
        switch (op.envelope_state)
        {
            case OPL_ENVELOPE_ATTACK:
                register_rate = op.attack_rate;
                break;
            case OPL_ENVELOPE_DECAY:
                register_rate = op.decay_rate;
                break;
            case OPL_ENVELOPE_SUSTAIN:
                register_rate = op.sustain ? 0 : op.release_rate;
                break;
            default:
                register_rate = op.release_rate;
                break;
        }
    }

    op.phase_reset = reset ? 1 : 0;

    u8 key_scale = channel.key_scale_value >> ((op.key_scale_rate ^ 1) << 1);
    u8 rate = (u8)(key_scale + (register_rate << 2));
    u8 rate_high = rate >> 2;
    u8 rate_low = rate & 0x03;
    u8 shift = 0;

    if ((rate_high & 0x10) != 0)
        rate_high = 0x0F;

    if (register_rate != 0)
    {
        if (rate_high < 12)
        {
            if (m_state.envelope_tick != 0)
            {
                switch (rate_high + m_state.envelope_add)
                {
                    case 12:
                        shift = 1;
                        break;
                    case 13:
                        shift = (rate_low >> 1) & 0x01;
                        break;
                    case 14:
                        shift = rate_low & 0x01;
                        break;
                    default:
                        break;
                }
            }
        }
        else
        {
            shift = (u8)((rate_high & 0x03) + k_opl_envelope_step[rate_low][m_state.envelope_timer_low]);

            if ((shift & 0x04) != 0)
                shift = 0x03;

            if (shift == 0)
                shift = m_state.envelope_tick;
        }
    }

    u16 envelope = op.envelope;
    s16 increment = 0;
    bool off = (op.envelope & 0x1F8) == 0x1F8;

    if (reset && (rate_high == 0x0F))
        envelope = 0;

    op.muted = (op.envelope_state != OPL_ENVELOPE_ATTACK) && !reset && off;

    if (op.muted)
        envelope = k_opl_envelope_silent;

    switch (op.envelope_state)
    {
        case OPL_ENVELOPE_ATTACK:
            if (op.envelope == 0)
                op.envelope_state = OPL_ENVELOPE_DECAY;
            else if ((key != 0) && (shift > 0) && (rate_high != 0x0F))
                increment = (s16)(~op.envelope >> (4 - shift));
            break;
        case OPL_ENVELOPE_DECAY:
            if ((op.envelope >> 4) == op.sustain_level)
                op.envelope_state = OPL_ENVELOPE_SUSTAIN;
            else if (!off && !reset && (shift > 0))
                increment = (s16)(1 << (shift - 1));
            break;
        default:
            if (!off && !reset && (shift > 0))
                increment = (s16)(1 << (shift - 1));
            break;
    }

    op.envelope = (u16)((envelope + increment) & 0x1FF);

    if (reset)
        op.envelope_state = OPL_ENVELOPE_ATTACK;

    if (key == 0)
        op.envelope_state = OPL_ENVELOPE_RELEASE;
}

void OPL::UpdatePhase(OPL_Operator& op, const OPL_Channel& channel, int index)
{
    u16 f_number = channel.f_number;

    if (op.vibrato != 0)
    {
        s8 range = (s8)((f_number >> 7) & 0x07);
        u8 position = m_state.vibrato_position;

        if ((position & 0x03) == 0)
            range = 0;
        else if ((position & 0x01) != 0)
            range >>= 1;

        range >>= m_state.vibrato_shift;

        if ((position & 0x04) != 0)
            range = (s8)-range;

        f_number = (u16)(f_number + range);
    }

    u32 base = ((u32)f_number << channel.block) >> 1;
    u16 phase = (u16)(op.phase >> 9);

    if (op.phase_reset != 0)
        op.phase = 0;

    op.phase += (base * k_opl_multiple[op.multiple]) >> 1;
    op.phase_output = phase;

    if (index == k_opl_hihat_operator)
        m_state.hihat_bits = (u8)(((phase >> 2) & 0x03) | (((phase >> 7) & 0x03) << 2));
    else if ((index == k_opl_cymbal_operator) && ((m_state.rhythm & k_opl_rhythm_enable) != 0))
        m_state.cymbal_bits = (u8)(((phase >> 3) & 0x01) | (((phase >> 5) & 0x01) << 1));

    if (((m_state.rhythm & k_opl_rhythm_enable) == 0) || (index < k_opl_hihat_operator) || (index > k_opl_cymbal_operator))
        return;

    u8 hihat = m_state.hihat_bits;
    u8 hihat_2 = hihat & 0x01;
    u8 hihat_3 = (hihat >> 1) & 0x01;
    u8 hihat_7 = (hihat >> 2) & 0x01;
    u8 hihat_8 = (hihat >> 3) & 0x01;
    u8 cymbal_3 = m_state.cymbal_bits & 0x01;
    u8 cymbal_5 = (m_state.cymbal_bits >> 1) & 0x01;
    u8 mixed = (hihat_2 ^ hihat_7) | (hihat_3 ^ cymbal_5) | (cymbal_3 ^ cymbal_5);
    u8 noise = m_state.noise & 0x01;

    if (index == k_opl_hihat_operator)
        op.phase_output = (u16)((mixed << 9) | ((mixed ^ noise) != 0 ? 0xD0 : 0x34));
    else if (index == k_opl_snare_operator)
        op.phase_output = (u16)((hihat_8 << 9) | ((hihat_8 ^ noise) << 8));
    else if (index == k_opl_cymbal_operator)
        op.phase_output = (u16)((mixed << 9) | (m_model == OPL_MODEL_OPL2 ? 0x100 : 0x80));
}

s16 OPL::CalculateExponent(u32 level) const
{
    if (level > 0x1FFF)
        level = 0x1FFF;

    return (s16)(((k_opl_exp[(level & 0xFF) ^ 0xFF] | 0x400) << 1) >> (level >> 8));
}

s16 OPL::CalculateWave(u16 phase, u16 envelope, u8 waveform) const
{
    u32 attenuation = (u32)envelope << 3;
    u32 level = 0;
    u16 negate = 0;

    switch (waveform)
    {
        case 0:
            negate = (phase & 0x200) != 0 ? 0xFFFF : 0;
            level = k_opl_log_sine[(phase & 0x100) != 0 ? (phase & 0xFF) ^ 0xFF : phase & 0xFF];
            break;
        case 1:
            level = (phase & 0x200) != 0 ? 0x1000 : k_opl_log_sine[(phase & 0x100) != 0 ? (phase & 0xFF) ^ 0xFF : phase & 0xFF];
            break;
        case 2:
            level = k_opl_log_sine[(phase & 0x100) != 0 ? (phase & 0xFF) ^ 0xFF : phase & 0xFF];
            break;
        case 3:
            level = (phase & 0x100) != 0 ? 0x1000 : k_opl_log_sine[phase & 0xFF];
            break;
        case 4:
            negate = (phase & 0x300) == 0x100 ? 0xFFFF : 0;
            level = (phase & 0x200) != 0 ? 0x1000 : k_opl_log_sine[(((phase & 0x80) != 0 ? phase ^ 0xFF : phase) << 1) & 0xFF];
            break;
        case 5:
            level = (phase & 0x200) != 0 ? 0x1000 : k_opl_log_sine[(((phase & 0x80) != 0 ? phase ^ 0xFF : phase) << 1) & 0xFF];
            break;
        case 6:
            negate = (phase & 0x200) != 0 ? 0xFFFF : 0;
            break;
        default:
            phase &= 0x3FF;

            if ((phase & 0x200) != 0)
            {
                negate = 0xFFFF;
                phase = (phase & 0x1FF) ^ 0x1FF;
            }

            level = (u32)phase << 3;
            break;
    }

    return (s16)(CalculateExponent(level + attenuation) ^ negate);
}

// With the drums on, their five slots enter at double level and the bass drum's modulator stays out
s32 OPL::MixOPL2() const
{
    bool rhythm = (m_state.rhythm & k_opl_rhythm_enable) != 0;
    int melodic = rhythm ? 6 : OPL2_CHANNEL_COUNT;
    s32 mix = 0;

    for (int i = 0; i < melodic; i++)
    {
        const OPL_Channel& channel = m_state.channels[i];
        int modulator = channel.operators[0];
        int carrier = channel.operators[1];

        if (m_channel_mask[i] == 0)
            continue;

        if ((channel.connection != 0) && (m_state.operators[modulator].muted == 0))
            mix += m_state.signals[modulator];

        if (m_state.operators[carrier].muted == 0)
            mix += m_state.signals[carrier];
    }

    if (rhythm)
    {
        for (int i = k_opl_hihat_operator; i < OPL2_OPERATOR_COUNT; i++)
        {
            if ((m_state.operators[i].muted == 0) && (m_channel_mask[m_state.operators[i].channel] != 0))
                mix += m_state.signals[i] * 2;
        }
    }

    return mix;
}

s32 OPL::MixOPL3(bool left) const
{
    s32 mix = 0;

    for (int i = 0; i < OPL_CHANNEL_COUNT; i++)
    {
        const OPL_Channel& channel = m_state.channels[i];
        s16 output = (s16)(m_state.signals[channel.outputs[0]] + m_state.signals[channel.outputs[1]] +
            m_state.signals[channel.outputs[2]] + m_state.signals[channel.outputs[3]]);
        mix += (s16)(output & (left ? channel.pan_left : channel.pan_right) & m_channel_mask[i]);
    }

    return mix;
}

// YM3014: a 10-bit mantissa and a 3-bit exponent, so the bits under the mantissa are lost
s16 OPL::ConvertDAC(s32 sample) const
{
    sample = clamp_s16(sample);

    int magnitude = (sample >> 9) & 0x3F;

    if (sample < 0)
        magnitude ^= 0x3F;

    int shift = 16 - (int)l_zero16((u16)magnitude);

    return (s16)((sample >> shift) * (1 << shift));
}

// Both prescalers run all the time, so a timer's first tick comes early by up to one tick
void OPL::UpdateTimers()
{
    m_state.csm_key = 0;

    for (int i = 0; i < OPL_TIMERS; i++)
    {
        u16 prescaler = i == 0 ? 0x03 : 0x0F;

        if ((m_state.timer_start[i] == 0) || ((m_state.timer & prescaler) != prescaler))
            continue;

        m_state.timer_counter[i]++;

        if (m_state.timer_counter[i] != 0)
            continue;

        m_state.timer_counter[i] = m_state.timer_reload[i];

        if (m_state.timer_mask[i] == 0)
            m_state.timer_flag[i] = 1;

        if ((i == 0) && (m_state.csm_enable != 0))
        {
            m_state.csm_key = 1;
            m_state.active_operators = k_opl_all_operators;
        }
    }
}

void OPL::UpdateModulators()
{
    if ((m_state.timer & 0x3F) == 0x3F)
        m_state.tremolo_position = (u8)((m_state.tremolo_position + 1) % 210);

    u8 position = m_state.tremolo_position;
    m_state.tremolo = (position < 105 ? position : 210 - position) >> m_state.tremolo_shift;

    if ((m_state.timer & 0x3FF) == 0x3FF)
        m_state.vibrato_position = (m_state.vibrato_position + 1) & 0x07;

    m_state.timer++;
}

void OPL::UpdateEnvelopeTimer()
{
    if (m_state.envelope_tick != 0)
    {
        int shift = (int)t_zero32((u32)m_state.envelope_timer | 0x2000U);

        m_state.envelope_add = (u8)(shift > 12 ? 0 : shift + 1);
        m_state.envelope_timer_low = (u8)(m_state.envelope_timer & 0x03);
    }

    if (m_model == OPL_MODEL_OPL2)
    {
        if (m_state.envelope_tick != 0)
        {
            if (m_state.envelope_timer == k_opl2_envelope_timer_limit)
            {
                m_state.envelope_timer = 0;
                m_state.envelope_timer_carry = 1;
            }
            else
            {
                m_state.envelope_timer += 1 + m_state.envelope_timer_carry;
                m_state.envelope_timer_carry = 0;
            }
        }
    }
    else if ((m_state.envelope_timer_carry != 0) || (m_state.envelope_tick != 0))
    {
        if (m_state.envelope_timer == k_opl3_envelope_timer_limit)
        {
            m_state.envelope_timer = 0;
            m_state.envelope_timer_carry = 1;
        }
        else
        {
            m_state.envelope_timer++;
            m_state.envelope_timer_carry = 0;
        }
    }

    m_state.envelope_tick ^= 1;
}

// An OPL2 only reaches a channel's frequency when its slot comes around
void OPL::ApplyPending(int index)
{
    if (index >= OPL_PENDING_CHANNELS)
        return;

    int channel = index % 9;

    if (IS_SET_BIT(m_state.pending_low, index))
        WriteFrequencyLow(channel, m_state.pending_low_value[index]);

    if (IS_SET_BIT(m_state.pending_high, index))
    {
        WriteFrequencyHigh(channel, m_state.pending_high_value[index]);
        SetChannelKey(channel, (m_state.pending_high_value[index] & 0x20) != 0);
    }
}

void OPL::Write(u8 port, u8 value)
{
    Synchronize();

    if ((port & 0x01) != 0)
    {
        WriteRegister(m_state.address, value);
        return;
    }

    m_state.address = value;

    if ((m_model == OPL_MODEL_OPL3) && ((port & 0x02) != 0) && ((value == 0x05) || (m_state.new_mode != 0)))
        m_state.address |= 0x100;
}

u8 OPL::Read(u8 port)
{
    Synchronize();
    return Peek(port);
}

// Status as of the last catch-up. Creative's tables name a status at A1 too, the data ports read nothing
u8 OPL::Peek(u8 port) const
{
    if ((port & 0x01) != 0)
        return 0xFF;

    u8 flags = (u8)((m_state.timer_flag[0] << 6) | (m_state.timer_flag[1] << 5));
    u8 status = flags != 0 ? (u8)(flags | 0x80) : 0;

    return m_model == OPL_MODEL_OPL2 ? (u8)(status | 0x06) : status;
}

u64 OPL::GetCyclesToTimerFlag() const
{
    u32 samples = 0xFFFFFFFF;

    for (int i = 0; i < OPL_TIMERS; i++)
    {
        u32 prescaler = i == 0 ? 0x03 : 0x0F;

        if ((m_state.timer_start[i] == 0) || (m_state.timer_mask[i] != 0) || (m_state.timer_flag[i] != 0))
            continue;

        u32 to_tick = prescaler - (m_state.timer & prescaler) + 1;
        samples = MIN(samples, to_tick + (255 - m_state.timer_counter[i]) * (prescaler + 1));
    }

    if (samples == 0xFFFFFFFF)
        return GDOX_NO_EVENT;

    return ((u64)(samples - 1) * k_opl_native_sample_cycles) + (k_opl_native_sample_cycles - m_state.native_cycle);
}

void OPL::WriteRegister(u16 address, u8 value)
{
    int bank = (address >> 8) & 0x01;
    u8 reg = (u8)address;
    bool opl2 = m_model == OPL_MODEL_OPL2;
    u8 group = reg & 0xF0;

    m_state.registers[bank][reg] = value;

    if (group == 0x00)
    {
        if (bank != 0)
        {
            if (reg == 0x04)
                WriteFourOperator(value);
            else if (reg == 0x05)
                m_state.new_mode = value & 0x01;

            return;
        }

        switch (reg)
        {
            case 0x01:
                m_state.waveform_enable = (value >> 5) & 0x01;
                break;
            case 0x02:
                m_state.timer_reload[0] = value;
                break;
            case 0x03:
                m_state.timer_reload[1] = value;
                break;
            case 0x04:
                WriteTimerControl(value);
                break;
            case 0x08:
                m_state.note_select = (value >> 6) & 0x01;
                m_state.csm_enable = opl2 ? (value >> 7) & 0x01 : 0;
                break;
            default:
                break;
        }

        return;
    }

    if ((group >= 0x20 && group <= 0x90) || group >= 0xE0)
    {
        int index = k_opl_register_operator[reg & 0x1F];

        if (index >= 0)
            WriteOperator(group & 0xE0, index + 18 * bank, value);

        return;
    }

    if ((reg == 0xBD) && (bank == 0))
    {
        m_state.tremolo_shift = (u8)((((value >> 7) ^ 1) << 1) + 2);
        m_state.vibrato_shift = ((value >> 6) & 0x01) ^ 1;
        WriteRhythm(value);
        return;
    }

    int low = reg & 0x0F;

    if (opl2)
    {
        int pending = k_opl2_register_pending[low];

        if (group == 0xA0 && pending >= 0)
        {
            m_state.pending_low |= (u16)(1 << pending);
            m_state.pending_low_value[pending] = value;
        }
        else if (group == 0xB0 && pending >= 0)
        {
            m_state.pending_high |= (u16)(1 << pending);
            m_state.pending_high_value[pending] = value;
        }
        else if (group == 0xC0 && k_opl2_register_channel[low] >= 0)
            WriteConnection(k_opl2_register_channel[low], value);

        return;
    }

    if (low >= 9)
        return;

    int channel = 9 * bank + low;

    if (group == 0xA0)
        WriteFrequencyLow(channel, value);
    else if (group == 0xB0)
    {
        WriteFrequencyHigh(channel, value);
        SetChannelKey(channel, (value & 0x20) != 0);
    }
    else if (group == 0xC0)
        WriteConnection(channel, value);
}

void OPL::WriteOperator(u8 group, int index, u8 value)
{
    OPL_Operator& op = m_state.operators[index];

    switch (group)
    {
        case 0x20:
            op.tremolo = (value >> 7) & 0x01;
            op.vibrato = (value >> 6) & 0x01;
            op.sustain = (value >> 5) & 0x01;
            op.key_scale_rate = (value >> 4) & 0x01;
            op.multiple = value & 0x0F;
            break;
        case 0x40:
            op.key_scale_level = (value >> 6) & 0x03;
            op.total_level = value & 0x3F;
            UpdateKeyScaleAttenuation(op, m_state.channels[op.channel]);
            break;
        case 0x60:
            op.attack_rate = (value >> 4) & 0x0F;
            op.decay_rate = value & 0x0F;
            break;
        case 0x80:
            op.sustain_level = (value >> 4) & 0x0F;

            if (op.sustain_level == 0x0F)
                op.sustain_level = 0x1F;

            op.release_rate = value & 0x0F;
            break;
        default:
            if (m_model == OPL_MODEL_OPL3)
                op.waveform = value & (m_state.new_mode != 0 ? 0x07 : 0x03);
            else if (m_state.waveform_enable != 0)
                op.waveform = value & 0x03;
            break;
    }
}

// D7 clears both flags and ignores the rest. A masked flag is "always 0", and a stopped timer keeps its flag
// An OPL3 counts its first tick on start, which makes its period 255 - N where an OPL2's is 256 - N
void OPL::WriteTimerControl(u8 value)
{
    if ((value & 0x80) != 0)
    {
        m_state.timer_flag[0] = 0;
        m_state.timer_flag[1] = 0;
        return;
    }

    for (int i = 0; i < OPL_TIMERS; i++)
    {
        u8 start = (value >> i) & 0x01;

        m_state.timer_mask[i] = (value >> (6 - i)) & 0x01;

        if (m_state.timer_mask[i] != 0)
            m_state.timer_flag[i] = 0;

        if ((m_state.timer_start[i] == 0) && (start != 0))
        {
            m_state.timer_counter[i] = m_state.timer_reload[i];

            if (m_model == OPL_MODEL_OPL3)
            {
                m_state.timer_counter[i]++;

                if (m_state.timer_counter[i] == 0)
                {
                    m_state.timer_counter[i] = m_state.timer_reload[i];
                    m_state.timer_flag[i] = m_state.timer_mask[i] == 0 ? 1 : 0;
                }
            }
        }

        m_state.timer_start[i] = start;
    }
}

void OPL::WriteFrequencyLow(int index, u8 value)
{
    OPL_Channel& channel = m_state.channels[index];

    if ((m_state.new_mode != 0) && (channel.type == OPL_CHANNEL_4OP_PAIR))
        return;

    channel.f_number = (u16)((channel.f_number & 0x300) | value);
    UpdateKeyScale(index);
}

void OPL::WriteFrequencyHigh(int index, u8 value)
{
    OPL_Channel& channel = m_state.channels[index];

    if ((m_state.new_mode != 0) && (channel.type == OPL_CHANNEL_4OP_PAIR))
        return;

    channel.f_number = (u16)((channel.f_number & 0xFF) | ((value & 0x03) << 8));
    channel.block = (value >> 2) & 0x07;

    if ((m_state.new_mode != 0) && (channel.type == OPL_CHANNEL_4OP))
        m_state.channels[channel.pair].block = channel.block;

    UpdateKeyScale(index);
}

void OPL::UpdateKeyScale(int index)
{
    OPL_Channel& channel = m_state.channels[index];

    channel.key_scale_value = (u8)((channel.block << 1) | ((channel.f_number >> (9 - m_state.note_select)) & 0x01));
    UpdateKeyScaleAttenuation(m_state.operators[channel.operators[0]], channel);
    UpdateKeyScaleAttenuation(m_state.operators[channel.operators[1]], channel);

    if ((m_state.new_mode != 0) && (channel.type == OPL_CHANNEL_4OP))
    {
        OPL_Channel& pair = m_state.channels[channel.pair];

        pair.f_number = channel.f_number;
        pair.key_scale_value = channel.key_scale_value;
        UpdateKeyScaleAttenuation(m_state.operators[pair.operators[0]], pair);
        UpdateKeyScaleAttenuation(m_state.operators[pair.operators[1]], pair);
    }
}

void OPL::UpdateKeyScaleAttenuation(OPL_Operator& op, const OPL_Channel& channel)
{
    s16 attenuation = (s16)((k_opl_key_scale_rom[channel.f_number >> 6] << 2) - ((8 - channel.block) << 5));
    op.key_scale_attenuation = attenuation < 0 ? 0 : (u8)attenuation;
}

void OPL::WriteConnection(int index, u8 value)
{
    OPL_Channel& channel = m_state.channels[index];

    channel.feedback = (value >> 1) & 0x07;
    channel.connection = value & 0x01;
    UpdateAlgorithm(index);

    if (m_model == OPL_MODEL_OPL2)
        return;

    channel.pan_left = (m_state.new_mode == 0) || ((value & 0x10) != 0) ? 0xFFFF : 0;
    channel.pan_right = (m_state.new_mode == 0) || ((value & 0x20) != 0) ? 0xFFFF : 0;
}

void OPL::WriteRhythm(u8 value)
{
    m_state.rhythm = value & 0x3F;

    if ((m_state.rhythm & k_opl_rhythm_enable) == 0)
    {
        for (int i = 6; i < 9; i++)
        {
            m_state.channels[i].type = OPL_CHANNEL_2OP;
            SetupAlgorithm(i);
            SetKey(m_state.channels[i].operators[0], k_opl_key_drum, false);
            SetKey(m_state.channels[i].operators[1], k_opl_key_drum, false);
        }

        return;
    }

    OPL_Channel* channels = m_state.channels;

    channels[6].outputs[0] = channels[6].operators[1];
    channels[6].outputs[1] = channels[6].operators[1];
    channels[6].outputs[2] = k_opl_zero_signal;
    channels[6].outputs[3] = k_opl_zero_signal;

    for (int i = 7; i < 9; i++)
    {
        channels[i].outputs[0] = channels[i].operators[0];
        channels[i].outputs[1] = channels[i].operators[0];
        channels[i].outputs[2] = channels[i].operators[1];
        channels[i].outputs[3] = channels[i].operators[1];
    }

    for (int i = 6; i < 9; i++)
    {
        channels[i].type = OPL_CHANNEL_DRUM;
        SetupAlgorithm(i);
    }

    SetKey(channels[7].operators[0], k_opl_key_drum, (m_state.rhythm & 0x01) != 0);
    SetKey(channels[8].operators[1], k_opl_key_drum, (m_state.rhythm & 0x02) != 0);
    SetKey(channels[8].operators[0], k_opl_key_drum, (m_state.rhythm & 0x04) != 0);
    SetKey(channels[7].operators[1], k_opl_key_drum, (m_state.rhythm & 0x08) != 0);
    SetKey(channels[6].operators[0], k_opl_key_drum, (m_state.rhythm & 0x10) != 0);
    SetKey(channels[6].operators[1], k_opl_key_drum, (m_state.rhythm & 0x10) != 0);
}

void OPL::WriteFourOperator(u8 value)
{
    for (int bit = 0; bit < 6; bit++)
    {
        int index = bit < 3 ? bit : bit + 6;
        OPL_Channel& channel = m_state.channels[index];
        OPL_Channel& pair = m_state.channels[index + 3];

        if (((value >> bit) & 0x01) != 0)
        {
            channel.type = OPL_CHANNEL_4OP;
            pair.type = OPL_CHANNEL_4OP_PAIR;
            UpdateAlgorithm(index);
        }
        else
        {
            channel.type = OPL_CHANNEL_2OP;
            pair.type = OPL_CHANNEL_2OP;
            UpdateAlgorithm(index);
            UpdateAlgorithm(index + 3);
        }
    }
}

void OPL::UpdateAlgorithm(int index)
{
    OPL_Channel& channel = m_state.channels[index];
    OPL_Channel& pair = m_state.channels[channel.pair];

    channel.algorithm = channel.connection;

    if ((m_state.new_mode != 0) && (channel.type == OPL_CHANNEL_4OP))
    {
        pair.algorithm = (u8)(0x04 | (channel.connection << 1) | pair.connection);
        channel.algorithm = 0x08;
        SetupAlgorithm(channel.pair);
    }
    else if ((m_state.new_mode != 0) && (channel.type == OPL_CHANNEL_4OP_PAIR))
    {
        channel.algorithm = (u8)(0x04 | (pair.connection << 1) | channel.connection);
        pair.algorithm = 0x08;
        SetupAlgorithm(index);
    }
    else
        SetupAlgorithm(index);
}

// Which signal feeds each operator, and which ones the channel adds up
void OPL::SetupAlgorithm(int index)
{
    OPL_Channel& channel = m_state.channels[index];
    OPL_Operator* operators = m_state.operators;
    u8 first = channel.operators[0];
    u8 second = channel.operators[1];
    u8 none = k_opl_zero_signal;

    if (channel.type == OPL_CHANNEL_DRUM)
    {
        if ((index == 7) || (index == 8))
        {
            operators[first].modulation = none;
            operators[second].modulation = none;
            return;
        }

        operators[first].modulation = (u8)(k_opl_feedback_signal + first);
        operators[second].modulation = (channel.algorithm & 0x01) != 0 ? none : first;
        return;
    }

    if ((channel.algorithm & 0x08) != 0)
        return;

    u8* outputs = channel.outputs;

    if ((channel.algorithm & 0x04) != 0)
    {
        OPL_Channel& pair = m_state.channels[channel.pair];
        u8 pair_first = pair.operators[0];
        u8 pair_second = pair.operators[1];
        u8 connection = channel.algorithm & 0x03;

        for (int i = 0; i < 4; i++)
        {
            pair.outputs[i] = none;
            outputs[i] = none;
        }

        operators[pair_first].modulation = (u8)(k_opl_feedback_signal + pair_first);
        operators[pair_second].modulation = (connection & 0x02) != 0 ? none : pair_first;
        operators[first].modulation = connection == 1 ? none : pair_second;
        operators[second].modulation = connection == 3 ? none : first;

        switch (connection)
        {
            case 0:
                outputs[0] = second;
                break;
            case 1:
                outputs[0] = pair_second;
                outputs[1] = second;
                break;
            case 2:
                outputs[0] = pair_first;
                outputs[1] = second;
                break;
            default:
                outputs[0] = pair_first;
                outputs[1] = first;
                outputs[2] = second;
                break;
        }

        return;
    }

    for (int i = 0; i < 4; i++)
        outputs[i] = none;

    operators[first].modulation = (u8)(k_opl_feedback_signal + first);

    if ((channel.algorithm & 0x01) != 0)
    {
        operators[second].modulation = none;
        outputs[0] = first;
        outputs[1] = second;
    }
    else
    {
        operators[second].modulation = first;
        outputs[0] = second;
    }
}

void OPL::SetKey(int index, u8 type, bool on)
{
    OPL_Operator& op = m_state.operators[index];

    if (on)
    {
        op.key |= type;
        m_state.active_operators |= 1ULL << index;
    }
    else
        op.key &= (u8)~type;
}

void OPL::SetChannelKey(int index, bool on)
{
    OPL_Channel& channel = m_state.channels[index];

    if ((m_model == OPL_MODEL_OPL3) && (m_state.new_mode != 0))
    {
        if (channel.type == OPL_CHANNEL_4OP_PAIR)
            return;

        if (channel.type == OPL_CHANNEL_4OP)
        {
            SetKey(m_state.channels[channel.pair].operators[0], k_opl_key_normal, on);
            SetKey(m_state.channels[channel.pair].operators[1], k_opl_key_normal, on);
        }
    }

    SetKey(channel.operators[0], k_opl_key_normal, on);
    SetKey(channel.operators[1], k_opl_key_normal, on);
}

void OPL::SaveState(std::ostream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
}

void OPL::LoadState(std::istream& stream)
{
    StateSerializer serializer(stream);
    Serialize(serializer);
    SanitizeState();
}

void OPL::Serialize(StateSerializer& serializer)
{
    for (int i = 0; i < OPL_OPERATOR_COUNT; i++)
    {
        OPL_Operator& op = m_state.operators[i];
        G_SERIALIZE(serializer, op.phase);
        G_SERIALIZE(serializer, op.phase_output);
        G_SERIALIZE(serializer, op.envelope);
        G_SERIALIZE(serializer, op.envelope_output);
        G_SERIALIZE(serializer, op.previous_output);
        G_SERIALIZE(serializer, op.envelope_state);
        G_SERIALIZE(serializer, op.key_scale_attenuation);
        G_SERIALIZE(serializer, op.key);
        G_SERIALIZE(serializer, op.phase_reset);
        G_SERIALIZE(serializer, op.muted);
        G_SERIALIZE(serializer, op.modulation);
        G_SERIALIZE(serializer, op.channel);
        G_SERIALIZE(serializer, op.tremolo);
        G_SERIALIZE(serializer, op.vibrato);
        G_SERIALIZE(serializer, op.sustain);
        G_SERIALIZE(serializer, op.key_scale_rate);
        G_SERIALIZE(serializer, op.multiple);
        G_SERIALIZE(serializer, op.key_scale_level);
        G_SERIALIZE(serializer, op.total_level);
        G_SERIALIZE(serializer, op.attack_rate);
        G_SERIALIZE(serializer, op.decay_rate);
        G_SERIALIZE(serializer, op.sustain_level);
        G_SERIALIZE(serializer, op.release_rate);
        G_SERIALIZE(serializer, op.waveform);
    }

    for (int i = 0; i < OPL_CHANNEL_COUNT; i++)
    {
        OPL_Channel& channel = m_state.channels[i];
        G_SERIALIZE(serializer, channel.f_number);
        G_SERIALIZE(serializer, channel.pan_left);
        G_SERIALIZE(serializer, channel.pan_right);
        G_SERIALIZE(serializer, channel.block);
        G_SERIALIZE(serializer, channel.key_scale_value);
        G_SERIALIZE(serializer, channel.feedback);
        G_SERIALIZE(serializer, channel.connection);
        G_SERIALIZE(serializer, channel.algorithm);
        G_SERIALIZE(serializer, channel.type);
        G_SERIALIZE(serializer, channel.pair);
        G_SERIALIZE_ARRAY(serializer, channel.operators, 2);
        G_SERIALIZE_ARRAY(serializer, channel.outputs, 4);
    }

    G_SERIALIZE_ARRAY(serializer, m_state.signals, 2 * OPL_OPERATOR_COUNT + 1);
    G_SERIALIZE_ARRAY(serializer, &m_state.registers[0][0], sizeof(m_state.registers));
    G_SERIALIZE(serializer, m_state.address);
    G_SERIALIZE(serializer, m_state.new_mode);
    G_SERIALIZE(serializer, m_state.note_select);
    G_SERIALIZE(serializer, m_state.rhythm);
    G_SERIALIZE(serializer, m_state.waveform_enable);
    G_SERIALIZE(serializer, m_state.csm_enable);
    G_SERIALIZE(serializer, m_state.csm_key);
    G_SERIALIZE(serializer, m_state.timer);
    G_SERIALIZE_ARRAY(serializer, m_state.timer_counter, OPL_TIMERS);
    G_SERIALIZE_ARRAY(serializer, m_state.timer_reload, OPL_TIMERS);
    G_SERIALIZE_ARRAY(serializer, m_state.timer_start, OPL_TIMERS);
    G_SERIALIZE_ARRAY(serializer, m_state.timer_mask, OPL_TIMERS);
    G_SERIALIZE_ARRAY(serializer, m_state.timer_flag, OPL_TIMERS);
    G_SERIALIZE(serializer, m_state.envelope_timer);
    G_SERIALIZE(serializer, m_state.envelope_timer_carry);
    G_SERIALIZE(serializer, m_state.envelope_tick);
    G_SERIALIZE(serializer, m_state.envelope_add);
    G_SERIALIZE(serializer, m_state.envelope_timer_low);
    G_SERIALIZE(serializer, m_state.vibrato_position);
    G_SERIALIZE(serializer, m_state.vibrato_shift);
    G_SERIALIZE(serializer, m_state.tremolo);
    G_SERIALIZE(serializer, m_state.tremolo_position);
    G_SERIALIZE(serializer, m_state.tremolo_shift);
    G_SERIALIZE(serializer, m_state.noise);
    G_SERIALIZE(serializer, m_state.hihat_bits);
    G_SERIALIZE(serializer, m_state.cymbal_bits);
    G_SERIALIZE(serializer, m_state.pending_low);
    G_SERIALIZE(serializer, m_state.pending_high);
    G_SERIALIZE_ARRAY(serializer, m_state.pending_low_value, OPL_PENDING_CHANNELS);
    G_SERIALIZE_ARRAY(serializer, m_state.pending_high_value, OPL_PENDING_CHANNELS);
    G_SERIALIZE(serializer, m_state.active_operators);
    G_SERIALIZE(serializer, m_state.native_cycle);
    G_SERIALIZE(serializer, m_state.elapsed_cycles);
    G_SERIALIZE(serializer, m_state.mix);
    G_SERIALIZE(serializer, m_state.left_sample);
    G_SERIALIZE(serializer, m_state.right_sample);
    G_SERIALIZE(serializer, m_state.previous_left_sample);
    G_SERIALIZE(serializer, m_state.previous_right_sample);
}

void OPL::SanitizeState()
{
    for (int i = 0; i < OPL_OPERATOR_COUNT; i++)
    {
        OPL_Operator& op = m_state.operators[i];

        if (op.modulation > k_opl_zero_signal)
            op.modulation = k_opl_zero_signal;

        op.channel %= OPL_CHANNEL_COUNT;
        op.envelope &= 0x1FF;
        op.envelope_state &= 0x03;
        op.multiple &= 0x0F;
        op.key_scale_level &= 0x03;
        op.waveform &= 0x07;
    }

    for (int i = 0; i < OPL_CHANNEL_COUNT; i++)
    {
        OPL_Channel& channel = m_state.channels[i];

        channel.f_number &= 0x3FF;
        channel.block &= 0x07;
        channel.feedback &= 0x07;
        channel.pair %= OPL_CHANNEL_COUNT;

        for (int j = 0; j < 2; j++)
            channel.operators[j] %= OPL_OPERATOR_COUNT;

        for (int j = 0; j < 4; j++)
        {
            if (channel.outputs[j] > k_opl_zero_signal)
                channel.outputs[j] = k_opl_zero_signal;
        }
    }

    m_state.signals[k_opl_zero_signal] = 0;
    m_state.native_cycle %= k_opl_native_sample_cycles;
    m_state.envelope_timer_low &= 0x03;
    m_state.vibrato_position &= 0x07;
    m_state.vibrato_shift &= 0x01;
    m_state.tremolo_position %= 210;
    m_state.tremolo_shift = CLAMP(m_state.tremolo_shift, 2, 4);
    m_state.noise &= 0x7FFFFF;
    m_state.pending_low &= 0x0FFF;
    m_state.pending_high &= 0x0FFF;
    m_state.active_operators &= k_opl_all_operators;
}
