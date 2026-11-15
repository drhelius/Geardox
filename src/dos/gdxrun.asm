; GDXRUN.COM: exits with the byte of fw_cfg file opt/geardox/target as ERRORLEVEL, 0 game, 1 setup, 2 prompt
; With no fw_cfg or no such file it exits with 0
org 100h
    cld
    mov dx, 510h
    xor ax, ax
    out dx, ax
    inc dx
    mov si, signature
    mov cx, 4
.s: in al, dx
    cmp al, [si]
    jne .none
    inc si
    loop .s
    dec dx
    mov ax, 19h
    out dx, ax
    inc dx
    in al, dx
    in al, dx
    in al, dx
    mov ch, al
    in al, dx
    mov cl, al
    jcxz .none
.e: push cx
    in al, dx
    in al, dx
    in al, dx
    in al, dx
    in al, dx
    mov bh, al
    in al, dx
    mov bl, al
    in al, dx
    in al, dx
    mov si, file
    mov cx, 56
    xor ah, ah
.n: in al, dx
    cmp al, [si]
    je .c
    mov ah, 1
.c: inc si
    loop .n
    pop cx
    or ah, ah
    jz .f
    loop .e
.none:
    xor al, al
    jmp .x
.f: dec dx
    mov ax, bx
    out dx, ax
    inc dx
    in al, dx
.x: mov ah, 4Ch
    int 21h
signature db 'QEMU'
file db 'opt/geardox/target'
    times 56 - ($ - file) db 0
