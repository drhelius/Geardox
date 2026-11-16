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

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "gdxdbg.h"

#if defined(__DJGPP__)
#include <sys/nearptr.h>
#endif

static int gdx_state = -1;

#if defined(__GNUC__) && defined(__i386__)

static unsigned char gdx_in(unsigned short port)
{
    unsigned char value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void gdx_out(unsigned short port, unsigned char value)
{
    __asm__ __volatile__("outb %0, %1" :: "a"(value), "Nd"(port) : "memory");
}

static void gdx_output(const char* text, unsigned int length)
{
    __asm__ __volatile__("outb %%al, %%dx" :: "a"(GDX_COMMAND_OUTPUT), "d"(GDX_PORT_CONTROL), "S"(text),
        "c"(length) : "memory");
}

static void gdx_based(unsigned long base, unsigned short code, unsigned short data)
{
    __asm__ __volatile__("outb %%al, %%dx" :: "a"(GDX_COMMAND_BASED), "d"(GDX_PORT_CONTROL), "b"(base),
        "c"((unsigned long)code), "S"((unsigned long)data), "D"(32UL) : "memory");
}

#elif defined(__GNUC__) && defined(__ia16__)

static unsigned char gdx_in(unsigned short port)
{
    unsigned char value;
    __asm__ __volatile__("inb %1, %0" : "=Ral"(value) : "d"(port));
    return value;
}

static void gdx_out(unsigned short port, unsigned char value)
{
    __asm__ __volatile__("outb %0, %1" :: "Ral"(value), "d"(port) : "memory");
}

static void gdx_output(const char* text, unsigned int length)
{
    __asm__ __volatile__("outb %%al, %%dx" :: "a"(GDX_COMMAND_OUTPUT), "d"(GDX_PORT_CONTROL), "S"(text),
        "c"(length) : "memory");
}

#elif defined(__WATCOMC__) && defined(__386__)

unsigned char gdx_in(unsigned short port);
#pragma aux gdx_in = "in al, dx" parm [dx] value [al]
void gdx_out(unsigned short port, unsigned char value);
#pragma aux gdx_out = "out dx, al" parm [dx] [al]
void gdx_output(const char* text, unsigned int length);
#pragma aux gdx_output = "mov dx, 520h" "mov al, 4" "out dx, al" parm [esi] [ecx] modify [eax edx]

#elif defined(__WATCOMC__)

unsigned char gdx_in(unsigned short port);
#pragma aux gdx_in = "in al, dx" parm [dx] value [al]
void gdx_out(unsigned short port, unsigned char value);
#pragma aux gdx_out = "out dx, al" parm [dx] [al]
void gdx_output(const char* text, unsigned int length);
#pragma aux gdx_output = "mov dx, 520h" "mov al, 4" "out dx, al" parm [si] [cx] modify [ax dx]

#else

#include <dos.h>

static unsigned char gdx_in(unsigned short port)
{
    return (unsigned char)inportb(port);
}

static void gdx_out(unsigned short port, unsigned char value)
{
    outportb(port, value);
}

static void gdx_output(const char* text, unsigned int length)
{
    _SI = (unsigned int)text;
    _CX = length;
    _DX = GDX_PORT_CONTROL;
    _AL = GDX_COMMAND_OUTPUT;
    __emit__(0xEE);
}

#endif

int gdx_present(void)
{
    if (gdx_state < 0)
        gdx_state = gdx_in(GDX_PORT_CONTROL) == GDX_SIGNATURE;

    return gdx_state;
}

void gdx_write(const char* text, unsigned int length)
{
    if (gdx_present() && (length > 0))
        gdx_output(text, length);
}

void gdx_puts(const char* text)
{
    gdx_write(text, (unsigned int)strlen(text));
    gdx_write("\n", 1);
}

void gdx_printf(const char* format, ...)
{
    char buffer[512];
    va_list args;
    int length;

    va_start(args, format);
    length = vsprintf(buffer, format, args);
    va_end(args);

    if (length > 0)
        gdx_write(buffer, (unsigned int)length);
}

void gdx_e9(char c)
{
    gdx_out(GDX_PORT_CONSOLE, (unsigned char)c);
}

void gdx_break(void)
{
    if (gdx_present())
        gdx_out(GDX_PORT_CONTROL, GDX_COMMAND_BREAK);
}

void gdx_report_base(void)
{
#if defined(__DJGPP__)
    unsigned short code;
    unsigned short data;

    if (!gdx_present())
        return;

    __asm__ __volatile__("movw %%cs, %0" : "=r"(code));
    __asm__ __volatile__("movw %%ds, %0" : "=r"(data));
    gdx_based((unsigned long)__djgpp_base_address, code, data);
#endif
}

#if defined(__DJGPP__)

__attribute__((constructor)) static void gdx_startup(void)
{
    gdx_report_base();
}

#endif
