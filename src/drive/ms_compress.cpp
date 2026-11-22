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

#include "ms_compress.h"

static const u8 k_ms_kwaj_signature[8] = { 'K', 'W', 'A', 'J', 0x88, 0xF0, 0x27, 0xD1 };
static const u32 k_ms_kwaj_header_size = 14;
static const u16 k_ms_kwaj_method_lzh = 3;
static const u8 k_ms_szdd_signature[8] = { 'S', 'Z', 'D', 'D', 0x88, 0xF0, 0x27, 0x33 };
static const u32 k_ms_szdd_header_size = 14;
static const u32 k_ms_window = 4096;

struct Kwaj_Bits
{
    const u8* data;
    size_t size;
    size_t bit;
};

struct Kwaj_Huffman
{
    u16 count[16];
    u16 symbol[256];
};

static u32 kwaj_bits(Kwaj_Bits* bits, int count)
{
    u32 value = 0;

    for (int i = 0; i < count; i++)
    {
        size_t byte = bits->bit >> 3;
        u32 bit = byte < bits->size ? (bits->data[byte] >> (7 - (bits->bit & 7))) & 1 : 0;
        value = (value << 1) | bit;
        bits->bit++;
    }

    return value;
}

// Canonical codes: shorter first, and in the order of the symbols within a length
static bool kwaj_build(Kwaj_Huffman* huffman, const u8* lengths, int symbols)
{
    u16 offsets[16];
    int left = 1;
    memset(huffman->count, 0, sizeof(huffman->count));

    for (int i = 0; i < symbols; i++)
        huffman->count[lengths[i]]++;

    for (int length = 1; length < 16; length++)
    {
        left = (left << 1) - huffman->count[length];

        if (left < 0)
            return false;
    }

    offsets[1] = 0;

    for (int length = 1; length < 15; length++)
        offsets[length + 1] = (u16)(offsets[length] + huffman->count[length]);

    for (int i = 0; i < symbols; i++)
    {
        if (lengths[i] != 0)
            huffman->symbol[offsets[lengths[i]]++] = (u16)i;
    }

    return true;
}

static int kwaj_decode(Kwaj_Bits* bits, const Kwaj_Huffman* huffman)
{
    int code = 0;
    int first = 0;
    int index = 0;

    for (int length = 1; length < 16; length++)
    {
        code |= (int)kwaj_bits(bits, 1);
        int count = huffman->count[length];

        if (code - count < first)
            return huffman->symbol[index + (code - first)];

        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }

    return -1;
}

// The four ways a tree's code lengths are stored, from libmspack's SZDD and KWAJ format page
static bool kwaj_lengths(Kwaj_Bits* bits, u32 type, int symbols, Kwaj_Huffman* huffman)
{
    u8 lengths[256];
    int value = 0;

    for (int i = 0; i < symbols; i++)
    {
        switch (type)
        {
            case 0:
                value = symbols == 16 ? 4 : symbols == 32 ? 5 : symbols == 64 ? 6 : 8;
                break;
            case 1:
                if (i == 0)
                    value = (int)kwaj_bits(bits, 4);
                else if (kwaj_bits(bits, 1) != 0)
                    value = kwaj_bits(bits, 1) == 0 ? value + 1 : (int)kwaj_bits(bits, 4);
                break;
            case 2:
                if (i == 0)
                    value = (int)kwaj_bits(bits, 4);
                else
                {
                    u32 selector = kwaj_bits(bits, 2);
                    value = selector == 3 ? (int)kwaj_bits(bits, 4) : value + (int)selector - 1;
                }
                break;
            case 3:
                value = (int)kwaj_bits(bits, 4);
                break;
            default:
                return false;
        }

        if ((value < 0) || (value > 15))
            return false;

        lengths[i] = (u8)value;
    }

    return kwaj_build(huffman, lengths, symbols);
}

// KWAJ method 3, written from libmspack's SZDD and KWAJ format page. It stops at the length in the header,
// as the format has no end marker
bool ms_expand_kwaj(const u8* data, size_t size, u32 max_size, std::string* out)
{
    out->clear();

    if ((size < k_ms_kwaj_header_size + 4) || (memcmp(data, k_ms_kwaj_signature, sizeof(k_ms_kwaj_signature)) != 0))
        return false;

    u16 method = read_u16_le(data + 8);
    u16 offset = read_u16_le(data + 10);
    u16 flags = read_u16_le(data + 12);
    u32 length = read_u32_le(data + k_ms_kwaj_header_size);

    if ((method != k_ms_kwaj_method_lzh) || ((flags & 0x01) == 0) || (offset > size) || (length > max_size))
        return false;

    static const int k_symbols[5] = { 16, 16, 32, 64, 256 };
    Kwaj_Huffman trees[5];
    Kwaj_Bits bits = { data + offset, size - offset, 0 };
    u32 types[6];
    u8 window[k_ms_window];
    u32 position = k_ms_window - 17;
    int table = 0;
    memset(window, ' ', sizeof(window));

    for (int i = 0; i < 6; i++)
        types[i] = kwaj_bits(&bits, 4);

    for (int i = 0; i < 5; i++)
    {
        if (!kwaj_lengths(&bits, types[i], k_symbols[i], &trees[i]))
            return false;
    }

    out->reserve(length);

    while (out->size() < length)
    {
        if (bits.bit > (bits.size * 8) + 32)
            return false;

        int code = kwaj_decode(&bits, &trees[table]);

        if (code < 0)
            return false;

        if (code > 0)
        {
            int high = kwaj_decode(&bits, &trees[3]);

            if (high < 0)
                return false;

            u32 distance = ((u32)high << 6) | kwaj_bits(&bits, 6);

            for (int i = 0; (i < code + 2) && (out->size() < length); i++)
            {
                u8 value = window[(position - distance) & (k_ms_window - 1)];
                window[position] = value;
                out->push_back((char)value);
                position = (position + 1) & (k_ms_window - 1);
            }

            table = 0;
            continue;
        }

        int run = kwaj_decode(&bits, &trees[2]);

        if (run < 0)
            return false;

        table = run != 31 ? 1 : 0;

        for (int i = 0; (i <= run) && (out->size() < length); i++)
        {
            int value = kwaj_decode(&bits, &trees[4]);

            if (value < 0)
                return false;

            window[position] = (u8)value;
            out->push_back((char)value);
            position = (position + 1) & (k_ms_window - 1);
        }
    }

    return bits.bit <= (bits.size * 8) + 32;
}

// SZDD, MS-DOS 5's COMPRESS, from the same format page: LZSS over a 4 KB window of spaces. A control byte's bits, low
// first, choose a literal or a 12-bit position and 4-bit length, plus 3
bool ms_expand_szdd(const u8* data, size_t size, u32 max_size, std::string* out)
{
    out->clear();

    if ((size < k_ms_szdd_header_size) || (memcmp(data, k_ms_szdd_signature, sizeof(k_ms_szdd_signature)) != 0) ||
        (data[8] != 'A'))
        return false;

    u32 length = read_u32_le(data + 10);

    if (length > max_size)
        return false;

    u8 window[k_ms_window];
    u32 position = k_ms_window - 16;
    size_t at = k_ms_szdd_header_size;
    memset(window, ' ', sizeof(window));
    out->reserve(length);

    while ((out->size() < length) && (at < size))
    {
        u8 control = data[at++];

        for (int bit = 0; (bit < 8) && (out->size() < length); bit++)
        {
            if ((control & (1 << bit)) != 0)
            {
                if (at >= size)
                    return false;

                window[position] = data[at++];
                out->push_back((char)window[position]);
                position = (position + 1) & (k_ms_window - 1);
                continue;
            }

            if (at + 1 >= size)
                return false;

            u32 from = data[at] | ((data[at + 1] & 0xF0) << 4);
            u32 count = (data[at + 1] & 0x0F) + 3;
            at += 2;

            for (u32 i = 0; (i < count) && (out->size() < length); i++)
            {
                window[position] = window[from];
                out->push_back((char)window[position]);
                position = (position + 1) & (k_ms_window - 1);
                from = (from + 1) & (k_ms_window - 1);
            }
        }
    }

    return out->size() == length;
}

bool ms_is_kwaj(const u8* data, size_t size)
{
    return (size >= 4) && (memcmp(data, k_ms_kwaj_signature, 4) == 0);
}

bool ms_is_szdd(const u8* data, size_t size)
{
    return (size >= 4) && (memcmp(data, k_ms_szdd_signature, 4) == 0);
}
