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

#ifndef OPL_H
#define OPL_H

#include <iostream>
#include "../common/common.h"

#define OPL_CHANNEL_COUNT 18
#define OPL_OPERATOR_COUNT 36
#define OPL2_CHANNEL_COUNT 9
#define OPL2_OPERATOR_COUNT 18
#define OPL_PENDING_CHANNELS 12
#define OPL_TIMERS 2

class StateSerializer;

class OPL
{
public:
    enum OPL_Model
    {
        OPL_MODEL_OPL2 = 0,
        OPL_MODEL_OPL3
    };

    enum OPL_Envelope_State
    {
        OPL_ENVELOPE_ATTACK = 0,
        OPL_ENVELOPE_DECAY,
        OPL_ENVELOPE_SUSTAIN,
        OPL_ENVELOPE_RELEASE
    };

    enum OPL_Channel_Type
    {
        OPL_CHANNEL_2OP = 0,
        OPL_CHANNEL_4OP,
        OPL_CHANNEL_4OP_PAIR,
        OPL_CHANNEL_DRUM
    };

    struct OPL_Operator
    {
        u32 phase;
        u16 phase_output;
        u16 envelope;
        u16 envelope_output;
        s16 previous_output;
        u8 envelope_state;
        u8 key_scale_attenuation;
        u8 key;
        u8 phase_reset;
        u8 muted;
        u8 modulation;
        u8 channel;

        u8 tremolo;
        u8 vibrato;
        u8 sustain;
        u8 key_scale_rate;
        u8 multiple;
        u8 key_scale_level;
        u8 total_level;
        u8 attack_rate;
        u8 decay_rate;
        u8 sustain_level;
        u8 release_rate;
        u8 waveform;
    };

    struct OPL_Channel
    {
        u16 f_number;
        u16 pan_left;
        u16 pan_right;
        u8 block;
        u8 key_scale_value;
        u8 feedback;
        u8 connection;
        u8 algorithm;
        u8 type;
        u8 pair;
        u8 operators[2];
        u8 outputs[4];
    };

    struct OPL_State
    {
        OPL_Operator operators[OPL_OPERATOR_COUNT];
        OPL_Channel channels[OPL_CHANNEL_COUNT];
        s16 signals[2 * OPL_OPERATOR_COUNT + 1];
        u8 registers[2][256];
        u16 address;
        u8 new_mode;
        u8 note_select;
        u8 rhythm;
        u8 waveform_enable;
        u8 csm_enable;
        u8 csm_key;

        u16 timer;
        u8 timer_counter[OPL_TIMERS];
        u8 timer_reload[OPL_TIMERS];
        u8 timer_start[OPL_TIMERS];
        u8 timer_mask[OPL_TIMERS];
        u8 timer_flag[OPL_TIMERS];

        u64 envelope_timer;
        u8 envelope_timer_carry;
        u8 envelope_tick;
        u8 envelope_add;
        u8 envelope_timer_low;

        u8 vibrato_position;
        u8 vibrato_shift;
        u8 tremolo;
        u8 tremolo_position;
        u8 tremolo_shift;

        u32 noise;
        u8 hihat_bits;
        u8 cymbal_bits;

        u16 pending_low;
        u16 pending_high;
        u8 pending_low_value[OPL_PENDING_CHANNELS];
        u8 pending_high_value[OPL_PENDING_CHANNELS];

        u64 active_operators;
        u32 native_cycle;
        u64 elapsed_cycles;
        s32 mix;
        s16 left_sample;
        s16 right_sample;
        s16 previous_left_sample;
        s16 previous_right_sample;
    };

public:
    OPL();
    ~OPL();
    void Init(OPL_Model model);
    void SetModel(OPL_Model model);
    void Reset();
    void Clock(u32 cycles);
    void Synchronize();
    void Sample(s16& left, s16& right);
    void Write(u8 port, u8 value);
    u8 Read(u8 port);
    u8 Peek(u8 port) const;
    u64 GetCyclesToTimerFlag() const;
    OPL_Model GetModel() const;
    OPL_State* GetState();
    s16 GetChannelOutput(int channel) const;
    u8 GetRegister(u16 address) const;
    u16 GetSelectedAddress() const;
    void SetChannelMute(int channel, bool mute);
    bool IsChannelMuted(int channel) const;
    void SaveState(std::ostream& stream);
    void LoadState(std::istream& stream);

private:
    void RunCycles(u64 cycles);
    void GenerateNativeSample();
    void GenerateOPL2();
    void GenerateOPL3();
    void RunOperators(int first, int last);
    void RunOperator(int index);
    void UpdateEnvelope(OPL_Operator& op, const OPL_Channel& channel);
    void UpdatePhase(OPL_Operator& op, const OPL_Channel& channel, int index);
    s16 CalculateWave(u16 phase, u16 envelope, u8 waveform) const;
    s16 CalculateExponent(u32 level) const;
    s32 MixOPL2() const;
    s32 MixOPL3(bool left) const;
    s16 ConvertDAC(s32 sample) const;
    void UpdateTimers();
    void UpdateModulators();
    void UpdateEnvelopeTimer();
    void ApplyPending(int index);

    void WriteRegister(u16 address, u8 value);
    void WriteOperator(u8 group, int index, u8 value);
    void WriteTimerControl(u8 value);
    void WriteFrequencyLow(int index, u8 value);
    void WriteFrequencyHigh(int index, u8 value);
    void WriteConnection(int index, u8 value);
    void WriteRhythm(u8 value);
    void WriteFourOperator(u8 value);
    void UpdateKeyScale(int channel);
    void UpdateKeyScaleAttenuation(OPL_Operator& op, const OPL_Channel& channel);
    void UpdateAlgorithm(int channel);
    void SetupAlgorithm(int channel);
    void SetKey(int op, u8 type, bool on);
    void SetChannelKey(int channel, bool on);

    void Serialize(StateSerializer& serializer);
    void SanitizeState();

private:
    OPL_State m_state;
    OPL_Model m_model;
    u16 m_channel_mask[OPL_CHANNEL_COUNT];
};

// 72 master clocks of 3.58 MHz on an OPL2, 288 of 14.32 MHz on an OPL3: 576 machine clocks on both
static const u32 k_opl_native_sample_cycles = 576;
static const u32 k_opl_native_sample_rate = 49716;
static const u16 k_opl_envelope_silent = 0x1FF;
static const u8 k_opl_rhythm_enable = 0x20;
static const int k_opl_feedback_signal = OPL_OPERATOR_COUNT;
static const int k_opl_zero_signal = 2 * OPL_OPERATOR_COUNT;

#include "opl_inline.h"

#endif /* OPL_H */
