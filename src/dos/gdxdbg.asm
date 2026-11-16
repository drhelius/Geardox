; GDXDBG.COM [/S] [/Q] PROGRAM [ARGS]: loads a program with INT 21h AX=4B01h, reports it to Geardox's development port,
; runs it, mirrors its console output through INT 29h and reports its exit code. With no program it runs Geardox's
; run command. /S stops in the debugger at the program's entry, /Q leaves the console out
cpu 386
org 100h
    cld
    mov sp, stack_top
    mov dx, 520h
    in al, dx
    cmp al, 'G'
    je .on
    mov dx, msg_off
    mov ah, 9
    int 21h
    mov ax, 4CFFh
    int 21h
.on:
    call dos_info
    mov ah, 4Ah
    mov bx, image_end + 15
    shr bx, 4
    int 21h
    mov si, 81h
    xor ch, ch
    mov cl, [80h]
    mov di, line
    rep movsb
    xor al, al
    stosb
    mov si, line
    call skip
    cmp byte [si], 0
    jne .options
    mov al, 6
    mov dx, 520h
    out dx, al
    inc dx
    mov di, line
    mov cx, line_size - 1
.fetch:
    in al, dx
    or al, al
    jz .fetched
    stosb
    loop .fetch
.fetched:
    xor al, al
    stosb
    mov si, line
.options:
    call skip
    cmp byte [si], '/'
    jne .program
    mov al, [si + 1]
    and al, 0DFh
    cmp al, 'S'
    jne .quiet
    or byte [flags], 1
    jmp .next
.quiet:
    cmp al, 'Q'
    jne .program
    mov byte [quiet], 1
.next:
    add si, 2
    jmp .options
.program:
    cmp byte [si], 0
    jne .name
    mov dx, msg_usage
    mov ah, 9
    int 21h
    mov ax, 4CFFh
    int 21h
.name:
    mov di, name
    xor bx, bx
.copy:
    lodsb
    cmp al, ' '
    jbe .copied
    cmp al, '.'
    jne .slash
    mov bx, di
.slash:
    cmp al, '\'
    je .path
    cmp al, ':'
    jne .store
.path:
    xor bx, bx
.store:
    stosb
    cmp di, name + name_size - 5
    jb .copy
    inc si
.copied:
    dec si
    mov [name_end], di
    mov byte [di], 0
    or bx, bx
    jnz .tail
    mov bx, ext_com
    call try_ext
    jnc .tail
    mov bx, ext_exe
    call try_ext
    jnc .tail
    mov di, [name_end]
    mov byte [di], 0
.tail:
    mov di, tail + 1
    xor cx, cx
.tail_copy:
    lodsb
    or al, al
    jz .tail_done
    stosb
    inc cx
    cmp cx, 126
    jb .tail_copy
.tail_done:
    mov byte [di], 0Dh
    mov [tail], cl
    mov si, tail + 1
    mov di, fcb1
    mov ax, 2901h
    int 21h
    mov di, fcb2
    mov ax, 2901h
    int 21h
    mov word [block], 0
    mov word [block + 2], tail
    mov [block + 4], cs
    mov word [block + 6], fcb1
    mov [block + 8], cs
    mov word [block + 0Ah], fcb2
    mov [block + 0Ch], cs
    mov si, name
    mov di, truename
    mov ah, 60h
    int 21h
    jnc .hook
    mov si, name
    mov di, truename
    mov cx, name_size
    rep movsb
.hook:
    cmp byte [quiet], 0
    jne .load
    mov ax, 3529h
    int 21h
    mov [old29], bx
    mov [old29 + 2], es
    push cs
    pop es
    mov dx, int29
    mov ax, 2529h
    int 21h
.load:
    mov [saved_sp], sp
    mov [saved_ss], ss
    mov dx, name
    mov bx, block
    mov ax, 4B01h
    int 21h
    cli
    mov ss, [cs:saved_ss]
    mov sp, [cs:saved_sp]
    sti
    push cs
    pop ds
    push cs
    pop es
    jnc .loaded
    call unhook
    mov dx, msg_load
    mov ah, 9
    int 21h
    mov bx, 0FFFFh
    mov al, 3
    mov dx, 520h
    out dx, al
    mov ax, 4CFFh
    int 21h
.loaded:
    mov ah, 62h
    int 21h
    mov es, bx
    mov word [es:0Ah], terminated
    mov [es:0Ch], cs
    call detect_coff
    mov ecx, [block + 12h]
    mov edi, [block + 0Eh]
    mov si, truename
    mov ah, [flags]
    mov al, 1
    mov dx, 520h
    out dx, al
    mov ds, bx
    cli
    mov ss, [cs:block + 10h]
    mov sp, [cs:block + 0Eh]
    sti
    pop ax
    jmp far [cs:block + 12h]

terminated:
    cli
    mov ss, [cs:saved_ss]
    mov sp, [cs:saved_sp]
    sti
    push cs
    pop ds
    push cs
    pop es
    cld
    mov ah, 4Dh
    int 21h
    mov [result], ax
    call unhook
    mov bx, [result]
    mov al, 3
    mov dx, 520h
    out dx, al
    mov al, [result]
    mov ah, 4Ch
    int 21h

int29:
    push ax
    push cx
    push dx
    push si
    push ds
    push cs
    pop ds
    mov [char], al
    mov si, char
    mov cx, 1
    mov al, 4
    mov dx, 520h
    out dx, al
    pop ds
    pop si
    pop dx
    pop cx
    pop ax
    jmp far [cs:old29]

unhook:
    cmp byte [quiet], 0
    jne .done
    push ds
    lds dx, [old29]
    mov ax, 2529h
    int 21h
    pop ds
.done:
    ret

dos_info:
    push es
    mov ah, 52h
    int 21h
    mov [lol], bx
    mov [lol + 2], es
    mov ah, 34h
    int 21h
    mov [indos], bx
    mov [indos + 2], es
    mov ah, 30h
    int 21h
    mov [dos_version], ax
    pop es
    mov ebx, [lol]
    mov ecx, [indos]
    mov si, [dos_version]
    mov al, 7
    mov dx, 520h
    out dx, al
    ret

detect_coff:
    push bx
    mov dx, name
    mov ax, 3D00h
    int 21h
    jc .done
    mov bx, ax
    mov dx, header
    mov cx, 6
    mov ah, 3Fh
    int 21h
    jc .close
    cmp word [header], 'MZ'
    jne .close
    mov ax, [header + 4]
    mov dx, 512
    mul dx
    mov dx, [header + 2]
    or dx, dx
    jz .seek
    sub ax, 512
    add ax, dx
.seek:
    mov dx, ax
    xor cx, cx
    mov ax, 4200h
    int 21h
    jc .close
    mov dx, header
    mov cx, 2
    mov ah, 3Fh
    int 21h
    jc .close
    cmp word [header], 014Ch
    jne .close
    or byte [flags], 4
    test byte [flags], 1
    jz .close
    and byte [flags], 0FEh
    or byte [flags], 2
.close:
    mov ah, 3Eh
    int 21h
.done:
    pop bx
    ret

skip:
    cmp byte [si], ' '
    je .space
    cmp byte [si], 9
    jne .done
.space:
    inc si
    jmp skip
.done:
    ret

try_ext:
    push si
    mov di, [name_end]
    mov si, bx
    mov cx, 5
    rep movsb
    pop si
    mov dx, name
    mov ax, 4300h
    int 21h
    ret

msg_off db 'GDXDBG: Geardox development tools are off', 13, 10, '$'
msg_usage db 'GDXDBG [/S] [/Q] PROGRAM [ARGS]', 13, 10, '$'
msg_load db 'GDXDBG: the program could not be loaded', 13, 10, '$'
ext_com db '.COM', 0
ext_exe db '.EXE', 0
flags db 0
quiet db 0
char db 0

section .bss
line_size equ 128
name_size equ 80
line resb line_size
name resb name_size
name_end resw 1
truename resb 128
tail resb 128
fcb1 resb 37
fcb2 resb 37
block resb 16h
old29 resd 1
saved_sp resw 1
saved_ss resw 1
result resw 1
header resb 6
lol resd 1
indos resd 1
dos_version resw 1
stack resb 512
stack_top:
image_end:
