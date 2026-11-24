org 0

header:
    dd -1
    dw 8000h
    dw strategy
    dw interrupt
    db 'GDXIDLE$'

request dd 0
old_2a dd 0
old_2f dd 0

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

int_2f:
    cmp ax, 1680h
    jne .chain
    push bp
    mov bp, sp
    push ax
    push bx
    push es
    mov ax, 4800h
    int 2Fh
    or al, al
    jz .done
    mov ax, es
    cmp ax, [bp + 4]
    jne .done
    sti
    hlt
.done:
    pop es
    pop bx
    pop ax
    pop bp
.chain:
    jmp far [cs:old_2f]

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
    mov ax, [es:2Fh * 4]
    mov [cs:old_2f], ax
    mov ax, [es:2Fh * 4 + 2]
    mov [cs:old_2f + 2], ax
    mov word [es:2Fh * 4], int_2f
    mov [es:2Fh * 4 + 2], cs
    sti
    mov word [bx + 14], resident_end
    mov [bx + 16], cs
    pop es
    ret
