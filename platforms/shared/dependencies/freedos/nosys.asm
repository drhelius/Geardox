; The code of a boot sector with no system, at offset 3Eh of its BPB. Says so, waits for a key, tries the next device
org 7C3Eh
    cli
    xor ax, ax
    mov ds, ax
    mov ss, ax
    mov sp, 7C00h
    sti
    cld
    mov si, msg
.p: lodsb
    or  al, al
    jz  .k
    mov ah, 0Eh
    mov bx, 7
    int 10h
    jmp .p
.k: xor ah, ah
    int 16h
    int 18h
msg db 'Not a system disk. Press a key for the next boot device.', 13, 10, 0
