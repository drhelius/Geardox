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

#ifndef VOODOO_REGS_H
#define VOODOO_REGS_H

#include "../common/common.h"

#define VOODOO_REGISTERS 256

static const int k_voodoo_status = 0x00;
static const int k_voodoo_vertex_ax = 0x02;
static const int k_voodoo_vertex_ay = 0x03;
static const int k_voodoo_vertex_bx = 0x04;
static const int k_voodoo_vertex_by = 0x05;
static const int k_voodoo_vertex_cx = 0x06;
static const int k_voodoo_vertex_cy = 0x07;
static const int k_voodoo_start_r = 0x08;
static const int k_voodoo_start_g = 0x09;
static const int k_voodoo_start_b = 0x0A;
static const int k_voodoo_start_z = 0x0B;
static const int k_voodoo_start_a = 0x0C;
static const int k_voodoo_start_s = 0x0D;
static const int k_voodoo_start_t = 0x0E;
static const int k_voodoo_start_w = 0x0F;
static const int k_voodoo_drdx = 0x10;
static const int k_voodoo_dgdx = 0x11;
static const int k_voodoo_dbdx = 0x12;
static const int k_voodoo_dzdx = 0x13;
static const int k_voodoo_dadx = 0x14;
static const int k_voodoo_dsdx = 0x15;
static const int k_voodoo_dtdx = 0x16;
static const int k_voodoo_dwdx = 0x17;
static const int k_voodoo_drdy = 0x18;
static const int k_voodoo_dgdy = 0x19;
static const int k_voodoo_dbdy = 0x1A;
static const int k_voodoo_dzdy = 0x1B;
static const int k_voodoo_dady = 0x1C;
static const int k_voodoo_dsdy = 0x1D;
static const int k_voodoo_dtdy = 0x1E;
static const int k_voodoo_dwdy = 0x1F;
static const int k_voodoo_triangle_cmd = 0x20;
static const int k_voodoo_fvertex_ax = 0x22;
static const int k_voodoo_fvertex_cy = 0x27;
static const int k_voodoo_fstart_r = 0x28;
static const int k_voodoo_fstart_g = 0x29;
static const int k_voodoo_fstart_b = 0x2A;
static const int k_voodoo_fstart_z = 0x2B;
static const int k_voodoo_fstart_a = 0x2C;
static const int k_voodoo_fstart_s = 0x2D;
static const int k_voodoo_fstart_t = 0x2E;
static const int k_voodoo_fstart_w = 0x2F;
static const int k_voodoo_fdrdx = 0x30;
static const int k_voodoo_fdsdx = 0x35;
static const int k_voodoo_fdtdx = 0x36;
static const int k_voodoo_fdwdx = 0x37;
static const int k_voodoo_fdrdy = 0x38;
static const int k_voodoo_fdsdy = 0x3D;
static const int k_voodoo_fdtdy = 0x3E;
static const int k_voodoo_fdwdy = 0x3F;
static const int k_voodoo_ftriangle_cmd = 0x40;
static const int k_voodoo_fbz_color_path = 0x41;
static const int k_voodoo_fog_mode = 0x42;
static const int k_voodoo_alpha_mode = 0x43;
static const int k_voodoo_fbz_mode = 0x44;
static const int k_voodoo_lfb_mode = 0x45;
static const int k_voodoo_clip_left_right = 0x46;
static const int k_voodoo_clip_low_y_high_y = 0x47;
static const int k_voodoo_nop_cmd = 0x48;
static const int k_voodoo_fastfill_cmd = 0x49;
static const int k_voodoo_swapbuffer_cmd = 0x4A;
static const int k_voodoo_fog_color = 0x4B;
static const int k_voodoo_za_color = 0x4C;
static const int k_voodoo_chroma_key = 0x4D;
static const int k_voodoo_stipple = 0x50;
static const int k_voodoo_color0 = 0x51;
static const int k_voodoo_color1 = 0x52;
static const int k_voodoo_fbi_pixels_in = 0x53;
static const int k_voodoo_fbi_chroma_fail = 0x54;
static const int k_voodoo_fbi_zfunc_fail = 0x55;
static const int k_voodoo_fbi_afunc_fail = 0x56;
static const int k_voodoo_fbi_pixels_out = 0x57;
static const int k_voodoo_fog_table = 0x58;
static const int k_voodoo_fog_table_end = 0x77;
static const int k_voodoo_fbi_init4 = 0x80;
static const int k_voodoo_v_retrace = 0x81;
static const int k_voodoo_back_porch = 0x82;
static const int k_voodoo_video_dimensions = 0x83;
static const int k_voodoo_fbi_init0 = 0x84;
static const int k_voodoo_fbi_init1 = 0x85;
static const int k_voodoo_fbi_init2 = 0x86;
static const int k_voodoo_fbi_init3 = 0x87;
static const int k_voodoo_h_sync = 0x88;
static const int k_voodoo_v_sync = 0x89;
static const int k_voodoo_clut_data = 0x8A;
static const int k_voodoo_dac_data = 0x8B;
static const int k_voodoo_max_rgb_delta = 0x8C;
static const int k_voodoo_texture_mode = 0xC0;
static const int k_voodoo_tlod = 0xC1;
static const int k_voodoo_tdetail = 0xC2;
static const int k_voodoo_tex_base_addr = 0xC3;
static const int k_voodoo_tex_base_addr_1 = 0xC4;
static const int k_voodoo_tex_base_addr_2 = 0xC5;
static const int k_voodoo_tex_base_addr_3_8 = 0xC6;
static const int k_voodoo_trex_init0 = 0xC7;
static const int k_voodoo_trex_init1 = 0xC8;
static const int k_voodoo_ncc_table0 = 0xC9;
static const int k_voodoo_ncc_table1 = 0xD5;
static const int k_voodoo_ncc_table_end = 0xE0;

static const u8 k_voodoo_reg_fbi = 0x01;
static const u8 k_voodoo_reg_trex = 0x02;
static const u8 k_voodoo_reg_trex_always = 0x04;
static const u8 k_voodoo_reg_fifo = 0x08;
static const u8 k_voodoo_reg_float = 0x10;
static const u8 k_voodoo_reg_readable = 0x20;

static const u32 k_voodoo_status_fifo_free = 0x0000003F;
static const u32 k_voodoo_status_vretrace = 0x00000040;
static const u32 k_voodoo_status_fbi_busy = 0x00000080;
static const u32 k_voodoo_status_trex_busy = 0x00000100;
static const u32 k_voodoo_status_busy = 0x00000200;

static const u32 k_voodoo_fbz_cp_texture = 0x08000000;
static const u32 k_voodoo_fbz_cp_param_adjust = 0x04000000;

static const u32 k_voodoo_fbz_clip = 0x00000001;
static const u32 k_voodoo_fbz_chroma = 0x00000002;
static const u32 k_voodoo_fbz_stipple = 0x00000004;
static const u32 k_voodoo_fbz_wbuffer = 0x00000008;
static const u32 k_voodoo_fbz_depth = 0x00000010;
static const u32 k_voodoo_fbz_dither = 0x00000100;
static const u32 k_voodoo_fbz_rgb_write = 0x00000200;
static const u32 k_voodoo_fbz_aux_write = 0x00000400;
static const u32 k_voodoo_fbz_dither_2x2 = 0x00000800;
static const u32 k_voodoo_fbz_stipple_pattern = 0x00001000;
static const u32 k_voodoo_fbz_alpha_mask = 0x00002000;
static const u32 k_voodoo_fbz_depth_bias = 0x00010000;
static const u32 k_voodoo_fbz_y_origin = 0x00020000;
static const u32 k_voodoo_fbz_alpha_planes = 0x00040000;
static const u32 k_voodoo_fbz_dither_subtract = 0x00080000;
static const u32 k_voodoo_fbz_depth_source = 0x00100000;

static const u32 k_voodoo_alpha_test = 0x00000001;
static const u32 k_voodoo_alpha_blend = 0x00000010;

static const u32 k_voodoo_lfb_pipeline = 0x00000100;
static const u32 k_voodoo_lfb_word_swap = 0x00000800;
static const u32 k_voodoo_lfb_byte_swizzle = 0x00001000;
static const u32 k_voodoo_lfb_y_origin = 0x00002000;
static const u32 k_voodoo_lfb_w_select = 0x00004000;
static const u32 k_voodoo_lfb_read_word_swap = 0x00008000;
static const u32 k_voodoo_lfb_read_byte_swizzle = 0x00010000;

static const u32 k_voodoo_init0_vga_pass = 0x00000001;
static const u32 k_voodoo_init0_graphics_reset = 0x00000002;
static const u32 k_voodoo_init0_fifo_reset = 0x00000004;
static const u32 k_voodoo_init0_swizzle = 0x00000008;
static const u32 k_voodoo_init0_lfb_mem_fifo = 0x00000800;
static const u32 k_voodoo_init0_tex_mem_fifo = 0x00001000;
static const u32 k_voodoo_init0_mem_fifo = 0x00002000;
static const u32 k_voodoo_init1_lfb_reads = 0x00000008;
static const u32 k_voodoo_init1_video_reset = 0x00000100;
static const u32 k_voodoo_init1_blank = 0x00001000;
static const u32 k_voodoo_init2_triple = 0x00000010;
static const u32 k_voodoo_init3_remap = 0x00000001;
static const u32 k_voodoo_init3_no_texture = 0x00000040;

static const u32 k_voodoo_enable_init_writes = 0x00000001;
static const u32 k_voodoo_enable_fifo_writes = 0x00000002;
static const u32 k_voodoo_enable_dac_read = 0x00000004;

INLINE s32 voodoo_float_to_s32(u32 data, int fraction_bits)
{
    int exponent = (int)((data >> 23) & 0xFF) - 127 - 23 + fraction_bits;
    s32 result = (s32)((data & 0x7FFFFF) | 0x800000);

    if (exponent < 0)
        result = exponent > -32 ? result >> -exponent : 0;
    else
        result = exponent < 8 ? result << exponent : 0x7FFFFFFF;

    return (data & 0x80000000) != 0 ? -result : result;
}

INLINE s64 voodoo_float_to_s64(u32 data, int fraction_bits)
{
    int exponent = (int)((data >> 23) & 0xFF) - 127 - 23 + fraction_bits;
    s64 result = (s64)((data & 0x7FFFFF) | 0x800000);

    if (exponent < 0)
        result = exponent > -64 ? result >> -exponent : 0;
    else
        result = exponent < 40 ? result << exponent : 0x7FFFFFFFFFFFFFFFLL;

    return (data & 0x80000000) != 0 ? -result : result;
}

#endif /* VOODOO_REGS_H */
