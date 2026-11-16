/*
 * Geardox - MS-DOS PC/AT Emulator
 * Copyright (C) 2026  Ignacio Sanchez
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef GDXDBG_H
#define GDXDBG_H

#define GDX_PORT_CONSOLE 0xE9
#define GDX_PORT_CONTROL 0x520
#define GDX_SIGNATURE 0x47
#define GDX_COMMAND_BASED 0x02
#define GDX_COMMAND_OUTPUT 0x04
#define GDX_COMMAND_BREAK 0x05

#ifdef __cplusplus
extern "C" {
#endif

int gdx_present(void);
void gdx_write(const char* text, unsigned int length);
void gdx_puts(const char* text);
void gdx_printf(const char* format, ...);
void gdx_e9(char c);
void gdx_break(void);
void gdx_report_base(void);

#ifdef __cplusplus
}
#endif

#if defined(__DJGPP__) || defined(__GNUC__)
    #define GDX_MAGIC_BREAK() __asm__ __volatile__("xchg %%bx, %%bx" ::: "memory")
#elif defined(__WATCOMC__)
    void gdx_magic_break(void);
    #pragma aux gdx_magic_break = "xchg bx, bx"
    #define GDX_MAGIC_BREAK() gdx_magic_break()
#elif defined(__TURBOC__) || defined(__BORLANDC__)
    #define GDX_MAGIC_BREAK() __emit__(0x87, 0xDB)
#else
    #define GDX_MAGIC_BREAK() _asm { xchg bx, bx }
#endif

#endif /* GDXDBG_H */
