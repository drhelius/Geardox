org 100h

    mov ax, cs
    cmp ax, 07F0h
    jae .exit
    mov dx, 0800h
    sub dx, ax
    mov ax, 3100h
    int 21h
.exit:
    mov ax, 4C00h
    int 21h
