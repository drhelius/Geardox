; POWEROFF.COM: turns the PC off through the APM BIOS. Installation check, real mode connect, driver version 1.2 or
; 1.1, then Set Power State, off, for all devices: a BIOS keeps a connection in 1.0 mode until the driver version call,
; and 1.0 has no off state (APM 1.2 spec pp 36-37, 50 and 71-72)
org 100h
    mov ax, 5300h
    xor bx, bx
    int 15h
    jc .fail
    mov ax, 5301h
    xor bx, bx
    int 15h
    mov ax, 530Eh
    xor bx, bx
    mov cx, 0102h
    int 15h
    jnc .off
    mov ax, 530Eh
    xor bx, bx
    mov cx, 0101h
    int 15h
.off:
    mov ax, 5307h
    mov bx, 0001h
    mov cx, 0003h
    int 15h
.fail:
    mov dx, message
    mov ah, 9
    int 21h
    mov ax, 4C01h
    int 21h
message db 'This PC has no APM power off', 13, 10, '$'
