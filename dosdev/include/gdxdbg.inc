; Geardox - MS-DOS PC/AT Emulator
; Copyright (C) 2026  Ignacio Sanchez
;
; Permission is hereby granted, free of charge, to any person obtaining a copy
; of this software and associated documentation files (the "Software"), to deal
; in the Software without restriction, including without limitation the rights
; to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
; copies of the Software, and to permit persons to whom the Software is
; furnished to do so, subject to the following conditions:
;
; The above copyright notice and this permission notice shall be included in all
; copies or substantial portions of the Software.
;
; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
; IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
; FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
; AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
; LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
; OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
; SOFTWARE.

%ifndef GDXDBG_INC
%define GDXDBG_INC

%define GDX_PORT_CONSOLE 0E9h
%define GDX_PORT_CONTROL 520h

%macro GDX_BREAK 0
    xchg bx, bx
%endmacro

%macro GDX_STOP 0
    push ax
    push dx
    mov dx, GDX_PORT_CONTROL
    mov al, 5
    out dx, al
    pop dx
    pop ax
%endmacro

%macro GDX_WRITE 2
    push ax
    push cx
    push dx
    push si
    mov si, %1
    mov cx, %2
    mov dx, GDX_PORT_CONTROL
    mov al, 4
    out dx, al
    pop si
    pop dx
    pop cx
    pop ax
%endmacro

%macro GDX_E9 1
    push ax
    push dx
    mov al, %1
    mov dx, GDX_PORT_CONSOLE
    out dx, al
    pop dx
    pop ax
%endmacro

%endif
