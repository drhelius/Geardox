; GDXGUS.COM [BASE,DRAM,ADC,GF1,MIDI]: resets the UltraSound that ULTRASND (or the argument) names, programs its IRQ and
; DMA latches and enables its output. Leaves nothing resident. Exits with 1 if the value is missing or wrong, 2 if no card
%macro put 2
    mov dx, [base]
    add dx, %1
    mov al, %2
    out dx, al
%endmacro
%macro putb 2
    put 103h, %1
    put 105h, %2
%endmacro
%macro putw 2
    put 103h, %1
    put 104h, (%2) & 0FFh
    put 105h, (%2) >> 8
%endmacro
%macro wait 0
    mov cx, 10
%%w:
    call delay
    loop %%w
%endmacro
%macro readb 1
    put 103h, %1
    mov dx, [base]
    add dx, 105h
    in al, dx
%endmacro
org 100h
    cld
    mov si, 81h
.skip:
    lodsb
    cmp al, ' '
    je .skip
    cmp al, 9
    je .skip
    cmp al, 13
    je .env
    mov di, si
    dec di
    push ds
    pop es
    jmp .parse
.env:
    mov es, [2Ch]
    xor di, di
.scan:
    cmp byte [es:di], 0
    je bad
    mov si, name
    mov cx, name_end - name
    mov dx, di
    repe cmpsb
    je .parse
    mov di, dx
    xor al, al
    mov cx, 0FFFFh
    repne scasb
    jmp .scan
.parse:
    mov bx, 16
    call number
    jc bad
    mov [base], ax
    mov bx, 10
    mov bp, values
    mov si, 4
.values:
    cmp byte [es:di], ','
    jne bad
    inc di
    call number
    jc bad
    mov [bp], al
    inc bp
    dec si
    jnz .values
    mov ax, [base]
    test al, 0Fh
    jnz bad
    cmp ax, 200h
    jb bad
    cmp ax, 3F0h
    ja bad
    cmp byte [values], 8
    jae bad
    cmp byte [values + 1], 8
    jae bad
    cmp byte [values + 2], 16
    jae bad
    cmp byte [values + 3], 16
    jae bad
    mov bx, dma_latch
    mov si, values
    call latch
    mov [dma_control], al
    mov bx, irq_latch
    mov si, values + 2
    call latch
    mov [irq_control], al
    putb 4Ch, 0
    wait
    putb 4Ch, 1
    wait
    putw 43h, 0
    putb 44h, 0
    put 107h, 0AAh
    call peek
    cmp al, 0AAh
    jne none
    put 107h, 55h
    call peek
    cmp al, 55h
    jne none
    put 107h, 0
    putw 43h, 1
    put 107h, 0
    put 100h, 3
    wait
    put 100h, 0
    putb 41h, 0
    putb 45h, 0
    putb 49h, 0
    putb 0Eh, 0CDh
    call clear
    xor bp, bp
.voice:
    mov dx, [base]
    add dx, 102h
    mov ax, bp
    out dx, al
    putb 0, 3
    putb 0Dh, 3
    call delay
    mov si, voice_registers
.register:
    lodsb
    cmp al, 0FFh
    je .next
    mov bl, al
    lodsb
    mov bh, al
    lodsw
    mov cx, ax
    mov dx, [base]
    add dx, 103h
    mov al, bl
    out dx, al
    inc dx
    test bh, bh
    jz .byte
    mov al, cl
    out dx, al
    jmp short .high
.byte:
    mov ch, cl
.high:
    inc dx
    mov al, ch
    out dx, al
    jmp .register
.next:
    inc bp
    cmp bp, 14
    jb .voice
    call clear
    put 0Fh, 5
    put 0, 0Bh
    put 0Bh, 0
    put 0Fh, 0
    mov ah, 2
.twice:
    push ax
    put 0, 0Bh
    mov al, [dma_control]
    cmp ah, 2
    jne .first
    or al, 80h
.first:
    mov dx, [base]
    add dx, 0Bh
    out dx, al
    put 0, 4Bh
    mov al, [irq_control]
    mov dx, [base]
    add dx, 0Bh
    out dx, al
    pop ax
    dec ah
    jnz .twice
    put 102h, 0
    put 0, 0Bh
    put 102h, 0
    putb 4Ch, 7
    put 0, 9
    mov ax, 4C00h
    int 21h
none:
    mov ax, 4C02h
    int 21h
bad:
    mov dx, message
    mov ah, 9
    int 21h
    mov ax, 4C01h
    int 21h
number:
    xor ax, ax
    xor cx, cx
.digit:
    mov dl, [es:di]
    or dl, 20h
    sub dl, '0'
    cmp dl, 10
    jb .value
    sub dl, 39
    cmp dl, 10
    jb .end
    cmp dl, 16
    jae .end
.value:
    cmp dl, bl
    jae .end
    push dx
    mul bx
    pop dx
    xor dh, dh
    add ax, dx
    inc di
    inc cx
    jmp .digit
.end:
    cmp cx, 1
    ret
latch:
    mov al, [si]
    xlat
    mov ah, al
    mov al, [si + 1]
    xlat
    mov cl, 3
    shl al, cl
    mov ch, [si]
    cmp ch, [si + 1]
    jne .done
    or ch, ch
    jz .done
    mov al, 40h
.done:
    or al, ah
    ret
delay:
    push ax
    push cx
    push dx
    mov dx, [base]
    add dx, 107h
    mov cx, 7
.read:
    in al, dx
    loop .read
    pop dx
    pop cx
    pop ax
    ret
peek:
    mov dx, [base]
    add dx, 107h
    in al, dx
    ret
clear:
    mov dx, [base]
    add dx, 6
    in al, dx
    readb 41h
    readb 49h
    readb 8Fh
    ret
name db 'ULTRASND='
name_end:
message db 'GDXGUS: ULTRASND is missing or wrong', 13, 10, '$'
dma_latch db 0, 1, 0, 2, 0, 3, 4, 5
irq_latch db 0, 0, 1, 3, 0, 2, 0, 4, 0, 0, 0, 5, 6, 0, 0, 7
voice_registers:
    db 01h, 1
    dw 0400h
    db 02h, 1
    dw 0
    db 03h, 1
    dw 0
    db 04h, 1
    dw 0
    db 05h, 1
    dw 0
    db 06h, 0
    dw 01h
    db 07h, 0
    dw 10h
    db 08h, 0
    dw 0E0h
    db 09h, 1
    dw 0
    db 0Ah, 1
    dw 0
    db 0Bh, 1
    dw 0
    db 0Ch, 0
    dw 07h
    db 0FFh
base dw 0
dma_control db 0
irq_control db 0
values db 0, 0, 0, 0
