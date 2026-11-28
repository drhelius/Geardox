; GDXIPX.COM [INT]: IPX over the packet driver at INT 60h-80h (found by its signature, or INT in hex). Stays resident
cpu 386
org 100h
FRAME equ 1514
DATA equ FRAME - 14
SOCKETS equ 16
STACK equ 256
%macro stack_on 0
    mov bx, ss
    mov dx, sp
    mov ax, cs
    cmp bx, ax
    je %%same
    mov ss, ax
    mov sp, stack_top
%%same:
    push bx
    push dx
%endmacro
%macro stack_off 0
    pop dx
    pop bx
    mov ax, cs
    cmp bx, ax
    je %%same
    mov ss, bx
    mov sp, dx
%%same:
%endmacro
    jmp init
old_1c dd 0
old_21 dd 0
old_2f dd 0
pkt_vec dd 0
listen_head dd 0
aes_head dd 0
mac dw 0, 0, 0
ticks dw 0
next_sock dw 4000h
rx_len dw 0
send_mode db 0
table dw f_open, f_close, f_target, f_send3, f_listen, f_sched5, f_cancel, f_sched7, f_ticks, f_addr, f_nop, f_nop
sock_num times SOCKETS dw 0
sock_psp times SOCKETS dw 0

int_7a:
    push cs
    call entry
    iret

entry:
    pushf
    cli
    pusha
    push ds
    push es
    push fs
    mov bp, sp
    cld
    push cs
    pop ds
    and bh, 7Fh
    cmp bx, 0Bh
    ja .extra
    shl bx, 1
    call [table + bx]
.done:
    pop fs
    pop es
    pop ds
    popa
    popf
    retf
.extra:
    cmp bx, 0Dh
    je .size
    cmp bx, 1Ah
    je .size
    cmp bx, 0Fh
    je .raw
    mov al, 0FFh
    cmp bx, 10h
    jne .set
    xor al, al
.set:
    mov [bp + 20], al
    jmp .done
.size:
    call f_size
    jmp .done
.raw:
    call f_sendf
    jmp .done

f_open:
    mov dx, [bp + 16]
    or dx, dx
    jnz .have
.dynamic:
    mov dx, [next_sock]
    inc word [next_sock]
    cmp word [next_sock], 8000h
    jb .swap
    mov word [next_sock], 4000h
.swap:
    xchg dl, dh
    mov ax, dx
    call lookup
    jz .dynamic
    jmp short .new
.have:
    mov ax, dx
    call lookup
    jz .used
.new:
    xor ax, ax
    call lookup
    jnz .full
    mov [di - 2], dx
    mov word [di - 2 + SOCKETS * 2], 0
    cmp byte [bp + 20], 0
    jne .set
    push dx
    push di
    mov ah, 62h
    int 21h
    pop di
    pop dx
    cli
    mov [di - 2 + SOCKETS * 2], bx
.set:
    mov [bp + 16], dx
    mov byte [bp + 20], 0
    ret
.used:
    mov byte [bp + 20], 0FFh
    ret
.full:
    mov byte [bp + 20], 0FEh
    ret

f_close:
    mov dx, [bp + 16]
    mov ax, dx
    call lookup
    jnz f_nop
    mov word [di - 2], 0
    call cancel_socket
f_nop:
    ret

f_target:
    mov ax, [es:si]
    or ax, [es:si + 2]
    jnz .none
    push ds
    push es
    pop ds
    add si, 4
    mov cx, 3
    rep movsw
    pop ds
    mov word [bp + 18], 1
    mov byte [bp + 20], 0
    ret
.none:
    mov byte [bp + 20], 0FAh
    ret

f_send3:
    mov byte [send_mode], 1
    jmp short send
f_sendf:
    mov byte [send_mode], 0
send:
    mov byte [bp + 20], 0
    mov bx, si
    mov ax, es
    mov fs, ax
    mov ax, [fs:bx + 22h]
    add bx, 24h
    push cs
    pop es
    mov di, tx_buf + 14
    xor dx, dx
.gather:
    or ax, ax
    jz .gathered
    mov cx, [fs:bx + 4]
    cmp cx, DATA
    ja .bad
    add dx, cx
    cmp dx, DATA
    ja .bad
    lds si, [fs:bx]
    add bx, 6
    rep movsb
    push cs
    pop ds
    dec ax
    jmp .gather
.gathered:
    cmp dx, 30
    jb .bad
    mov cx, tx_buf + 60
    sub cx, di
    jbe .padded
    xor al, al
    rep stosb
.padded:
    mov fs, [bp + 2]
    mov bx, [bp + 8]
    mov si, mac
    mov di, tx_buf + 6
    mov cx, 3
    rep movsw
    cmp byte [send_mode], 0
    je .header
    cmp word [fs:bx + 28h], 30
    jb .bad
    mov word [tx_buf + 14], 0FFFFh
    mov ax, dx
    xchg al, ah
    mov [tx_buf + 14 + 2], ax
    xor ax, ax
    mov [tx_buf + 14 + 4], al
    mov [tx_buf + 14 + 18], ax
    mov [tx_buf + 14 + 20], ax
    mov si, mac
    mov di, tx_buf + 14 + 22
    mov cx, 3
    rep movsw
    mov ax, [fs:bx + 0Ah]
    mov [tx_buf + 14 + 28], ax
    les di, [fs:bx + 24h]
    mov si, tx_buf + 14
    mov cx, 15
    rep movsw
    push cs
    pop es
.header:
    push ds
    mov ax, fs
    mov ds, ax
    lea si, [bx + 1Ch]
    mov di, tx_buf
    mov cx, 3
    rep movsw
    pop ds
    mov word [tx_buf + 12], 3781h
    mov ax, [tx_buf]
    or ax, [tx_buf + 2]
    or ax, [tx_buf + 4]
    jz .ok
    mov si, tx_buf
    mov di, mac
    mov cx, 3
    repe cmpsw
    je .loop
    mov cx, dx
    add cx, 14
    cmp cx, 60
    jae .long
    mov cx, 60
.long:
    mov si, tx_buf
    mov ah, 4
    pushf
    call far [pkt_vec]
    mov al, 0
    jnc .complete
    mov al, 0FFh
    jmp short .complete
.loop:
    mov bx, tx_buf
    mov cx, dx
    add cx, 14
    push bp
    call deliver
    pop bp
.ok:
    xor al, al
    jmp short .complete
.bad:
    push cs
    pop ds
    mov al, 0FDh
.complete:
    mov fs, [bp + 2]
    mov bx, [bp + 8]
    mov dl, 0FFh
    jmp post

f_listen:
    mov dx, [es:si + 0Ah]
    mov ax, dx
    call lookup
    jnz .none
    mov byte [es:si + 8], 0FEh
    mov di, listen_head
    call append
    mov byte [bp + 20], 0
    ret
.none:
    mov byte [bp + 20], 0FFh
    ret

f_sched5:
    mov dl, 0FFh
    jmp short sched
f_sched7:
    xor dl, dl
sched:
    mov [es:si + 9], dl
    mov ah, 62h
    int 21h
    cli
    mov ax, bx
    call field
    mov [es:bx + si], ax
    mov ax, [bp + 20]
    mov [es:si + 0Ch], ax
    mov byte [es:si + 8], 0FDh
    mov di, aes_head
    jmp append

f_cancel:
    mov al, [es:si + 8]
    or al, al
    jz .fail
    mov di, listen_head
    cmp al, 0FEh
    je .unlink
    mov di, aes_head
    cmp al, 0FDh
    mov al, 0F9h
    jne .set
.unlink:
    call unlink
    jnc .fail
    mov byte [es:si + 8], 0
    mov byte [es:si + 9], 0FCh
    xor al, al
    jmp short .set
.fail:
    mov al, 0FFh
.set:
    mov [bp + 20], al
    ret

f_ticks:
    mov ax, [ticks]
    mov [bp + 20], ax
    ret

f_addr:
    mov di, si
    xor ax, ax
    stosw
    stosw
    mov si, mac
    mov cx, 3
    rep movsw
    ret

f_size:
    mov word [bp + 20], DATA
    mov word [bp + 18], 5
    ret

lookup:
    push es
    push cs
    pop es
    mov di, sock_num
    mov cx, SOCKETS
    repne scasw
    pop es
    ret

find:
    mov si, [listen_head]
    mov es, [listen_head + 2]
.next:
    mov cx, es
    or cx, si
    jz .none
    cmp [es:si + 0Ah], ax
    je .found
    les si, [es:si]
    jmp .next
.none:
    clc
    ret
.found:
    stc
    ret

append:
    xor ax, ax
    mov [es:si], ax
    mov [es:si + 2], ax
    mov bx, di
    push ds
    pop fs
.next:
    mov ax, [fs:bx]
    or ax, [fs:bx + 2]
    jz .tail
    mov ax, [fs:bx]
    mov fs, [fs:bx + 2]
    mov bx, ax
    jmp .next
.tail:
    mov [fs:bx], si
    mov [fs:bx + 2], es
    ret

unlink:
    mov bx, di
    push ds
    pop fs
.next:
    mov ax, [fs:bx]
    or ax, [fs:bx + 2]
    jz .none
    cmp si, [fs:bx]
    jne .step
    mov ax, es
    cmp ax, [fs:bx + 2]
    je .found
.step:
    mov ax, [fs:bx]
    mov fs, [fs:bx + 2]
    mov bx, ax
    jmp .next
.found:
    mov ax, [es:si]
    mov [fs:bx], ax
    mov ax, [es:si + 2]
    mov [fs:bx + 2], ax
    stc
    ret
.none:
    clc
    ret

cancel_socket:
    mov ax, dx
    call find
    jnc .done
    mov di, listen_head
    call unlink
    mov byte [es:si + 8], 0
    mov byte [es:si + 9], 0FCh
    jmp cancel_socket
.done:
    xor ax, ax
purge:
.scan:
    mov si, [aes_head]
    mov es, [aes_head + 2]
.look:
    mov bx, es
    or bx, si
    jz .done
    cmp byte [es:si + 9], 0FFh
    jne .owner
    or dx, dx
    jz .owner
    cmp [es:si + 0Ah], dx
    je .hit
.owner:
    call field
    cmp [es:bx + si], ax
    je .hit
    les si, [es:si]
    jmp .look
.hit:
    push ax
    mov di, aes_head
    call unlink
    pop ax
    mov word [es:si + 8], 0FC00h
    jmp .scan
.done:
    ret

field:
    movzx bx, byte [es:si + 9]
    and bl, 6
    add bx, 0Ah
    ret

post:
    mov byte [fs:bx + 8], 0
    mov [fs:bx + 9], al
    mov ax, [fs:bx + 4]
    or ax, [fs:bx + 6]
    jz .done
    mov ax, fs
    mov es, ax
    mov si, bx
    mov al, dl
    call far [fs:bx + 4]
.done:
    ret

deliver:
    sub cx, 14
    cmp cx, 30
    jb .out
    mov dx, [bx + 16]
    xchg dl, dh
    cmp dx, 30
    jb .out
    cmp dx, cx
    ja .out
    mov ax, [bx + 30]
    call find
    jnc .out
    mov di, listen_head
    push bx
    call unlink
    pop bx
    mov di, si
    add di, 1Ch
    lea si, [bx + 6]
    movsw
    movsw
    movsw
    mov bp, di
    sub bp, 22h
    mov ax, es
    mov fs, ax
    mov ax, [es:di]
    lea si, [bx + 14]
    lea bx, [bp + 24h]
.scatter:
    or dx, dx
    jz .whole
    or ax, ax
    jz .over
    mov cx, [fs:bx + 4]
    les di, [fs:bx]
    add bx, 6
    cmp cx, dx
    jbe .copy
    mov cx, dx
.copy:
    sub dx, cx
    rep movsb
    dec ax
    jmp .scatter
.whole:
    xor al, al
    jmp short .post
.over:
    mov al, 0FDh
.post:
    mov bx, bp
    mov dl, 0FFh
    call post
.out:
    ret

recv:
    or ax, ax
    jnz .second
    mov di, rx_buf
    push cs
    pop es
    cmp cx, FRAME
    jbe .give
    xor di, di
    mov es, di
.give:
    retf
.second:
    pushf
    cli
    pusha
    push ds
    push es
    push fs
    cld
    push cs
    pop ds
    mov [rx_len], cx
    stack_on
    push cs
    pop es
    mov si, rx_buf + 6
    mov di, mac
    mov cx, 3
    repe cmpsw
    je .skip
    mov bx, rx_buf
    mov cx, [rx_len]
    call deliver
.skip:
    stack_off
    pop fs
    pop es
    pop ds
    popa
    popf
    retf

int_1c:
    inc word [cs:ticks]
    push ax
    mov ax, [cs:aes_head]
    or ax, [cs:aes_head + 2]
    pop ax
    jnz .work
    jmp far [cs:old_1c]
.work:
    pusha
    push ds
    push es
    push fs
    cli
    cld
    push cs
    pop ds
    stack_on
    mov si, [aes_head]
    mov es, [aes_head + 2]
.count:
    mov ax, es
    or ax, si
    jz .counted
    mov ax, [es:si + 0Ch]
    or ax, ax
    jz .step
    dec ax
    mov [es:si + 0Ch], ax
.step:
    les si, [es:si]
    jmp .count
.counted:
.scan:
    mov si, [aes_head]
    mov es, [aes_head + 2]
.look:
    mov ax, es
    or ax, si
    jz .end
    cmp word [es:si + 0Ch], 0
    je .fire
    les si, [es:si]
    jmp .look
.fire:
    mov di, aes_head
    call unlink
    mov ax, es
    mov fs, ax
    mov bx, si
    mov dl, [fs:bx + 9]
    xor al, al
    call post
    jmp .scan
.end:
    stack_off
    pop fs
    pop es
    pop ds
    popa
    jmp far [cs:old_1c]

int_21:
    cmp ah, 4Ch
    je .exit
    or ah, ah
    jnz .chain
.exit:
    pusha
    push ds
    push es
    push fs
    cld
    push cs
    pop ds
    mov ah, 62h
    int 21h
    cli
    xor si, si
.socket:
    mov dx, [sock_num + si]
    or dx, dx
    jz .next
    cmp [sock_psp + si], bx
    jne .next
    mov word [sock_num + si], 0
    push bx
    push si
    call cancel_socket
    pop si
    pop bx
.next:
    add si, 2
    cmp si, SOCKETS * 2
    jb .socket
    mov ax, bx
    xor dx, dx
    call purge
    pop fs
    pop es
    pop ds
    popa
.chain:
    jmp far [cs:old_21]

int_2f:
    cmp ax, 7A00h
    jne .chain
    mov al, 0FFh
    mov di, entry
    push cs
    pop es
    iret
.chain:
    jmp far [cs:old_2f]

resident_end:
tx_buf equ resident_end
rx_buf equ tx_buf + FRAME
stack_top equ rx_buf + FRAME + STACK

init:
    cld
    mov ax, 7A00h
    int 2Fh
    cmp al, 0FFh
    mov dx, message_installed
    mov bl, 2
    je fail
    mov si, 81h
.blank:
    lodsb
    cmp al, ' '
    je .blank
    cmp al, 9
    je .blank
    cmp al, 13
    je .scan
    xor bx, bx
    cmp al, '0'
    jne .digit
    mov ah, [si]
    or ah, 20h
    cmp ah, 'x'
    jne .digit
    inc si
    lodsb
.digit:
    or al, 20h
    sub al, '0'
    cmp al, 9
    jbe .value
    sub al, 'a' - '0' - 10
    cmp al, 10
    jb .missing
    cmp al, 15
    ja .missing
.value:
    shl bx, 4
    or bl, al
    lodsb
    cmp al, 13
    je .given
    cmp al, ' '
    jne .digit
.given:
    mov al, bl
    cmp al, 60h
    jb .missing
    cmp al, 80h
    ja .missing
    mov [pkt_int], al
    call probe
    je .found
.missing:
    mov dx, message_driver
    mov bl, 1
    jmp fail
.scan:
    mov al, 60h
.try:
    mov [pkt_int], al
    call probe
    je .found
    mov al, [pkt_int]
    inc al
    cmp al, 80h
    jbe .try
    jmp .missing
.found:
    mov [pkt_vec], bx
    mov [pkt_vec + 2], es
    push cs
    pop es
    mov ax, 0201h
    mov bx, 0FFFFh
    xor dl, dl
    mov si, ether_type
    mov cx, 2
    mov di, recv
    pushf
    call far [pkt_vec]
    mov dx, message_refused
    mov bl, 3
    jc fail
    mov [handle], ax
    mov bx, ax
    push cs
    pop es
    mov ah, 6
    mov cx, 6
    mov di, mac
    pushf
    call far [pkt_vec]
    jc .release
    cmp cx, 6
    je .ready
.release:
    mov bx, [handle]
    mov ah, 3
    pushf
    call far [pkt_vec]
    mov dx, message_refused
    mov bl, 3
    jmp fail
.ready:
    push cs
    pop es
    mov si, mac
    mov di, node
    mov cx, 6
.hex:
    lodsb
    call hex
    loop .hex
    mov al, [pkt_int]
    mov di, int_text
    call hex
    mov dx, message_ok
    mov ah, 9
    int 21h
    mov es, [2Ch]
    mov ah, 49h
    int 21h
    mov ax, 351Ch
    int 21h
    mov [old_1c], bx
    mov [old_1c + 2], es
    mov ax, 3521h
    int 21h
    mov [old_21], bx
    mov [old_21 + 2], es
    mov ax, 352Fh
    int 21h
    mov [old_2f], bx
    mov [old_2f + 2], es
    mov dx, int_1c
    mov ax, 251Ch
    int 21h
    mov dx, int_21
    mov ax, 2521h
    int 21h
    mov dx, int_2f
    mov ax, 252Fh
    int 21h
    mov dx, int_7a
    mov ax, 257Ah
    int 21h
    mov dx, stack_top + 15
    shr dx, 4
    mov ax, 3100h
    int 21h
fail:
    mov ah, 9
    int 21h
    mov al, bl
    mov ah, 4Ch
    int 21h
probe:
    mov ah, 35h
    int 21h
    mov di, bx
    add di, 3
    mov si, signature
    mov cx, 9
    repe cmpsb
    ret
hex:
    mov ah, al
    shr al, 4
    call .nibble
    mov al, ah
    and al, 0Fh
.nibble:
    cmp al, 10
    sbb al, 69h
    das
    stosb
    ret
pkt_int db 0
handle dw 0
ether_type db 81h, 37h
signature db 'PKT DRVR', 0
message_ok db 'GDXIPX: node '
node db '000000000000, INT '
int_text db '00h', 13, 10, '$'
message_driver db 'GDXIPX: no packet driver', 13, 10, '$'
message_installed db 'GDXIPX: already installed', 13, 10, '$'
message_refused db 'GDXIPX: packet driver refused', 13, 10, '$'
init_end:
