org 0

header:
    dd -1
    dw 8000h
    dw strategy
    dw interrupt
    db 'GDXIDLE$'

request dd 0
old_2a dd 0

strategy:
    mov [cs:request], bx
    mov [cs:request + 2], es
    retf

interrupt:
    push ax
    push bx
    push ds
    lds bx, [cs:request]
    mov word [bx + 3], 0100h
    cmp byte [bx + 2], 0
    jne .done
    call init
.done:
    pop ds
    pop bx
    pop ax
    retf

int_2a:
    cmp ah, 84h
    jne .chain
    sti
    hlt
.chain:
    jmp far [cs:old_2a]

resident_end:

init:
    push es
    xor ax, ax
    mov es, ax
    cli
    mov ax, [es:2Ah * 4]
    mov [cs:old_2a], ax
    mov ax, [es:2Ah * 4 + 2]
    mov [cs:old_2a + 2], ax
    mov word [es:2Ah * 4], int_2a
    mov [es:2Ah * 4 + 2], cs
    sti
    mov word [bx + 14], resident_end
    mov [bx + 16], cs
    pop es
    ret
