%include "gdxdbg.inc"
cpu 386
bits 16
section .text
global start

start:
    mov dx, message
    mov ah, 9
    int 21h
    GDX_WRITE note, note_length
    xor cx, cx
    xor ax, ax
sum:
    add ax, cx
    inc cx
    cmp cx, 10
    jbe sum
    cmp ax, 55
    jne failed
    mov ax, 4C00h
    int 21h
failed:
    mov ax, 4C01h
    int 21h

message db 'Hello from NASM', 13, 10, '$'
note db 'the sum is ready', 10
note_length equ $ - note
