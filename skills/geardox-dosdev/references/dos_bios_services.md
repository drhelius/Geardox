# BIOS and DOS Services — What MS-DOS Software Relies On

## 1. Overview

A DOS game talks to the machine through software interrupts. The BIOS answers INT 10h to 1Ah, DOS answers INT 20h to
2Fh, and the drivers DOS loads add INT 33h, INT 67h and more. Geardox has no DOS in its core. What runs is the built-in
SeaBIOS with SeaVGABIOS, and the DOS on the C: disk: FreeDOS 1.4, or the user's own MS-DOS imported from Setup disks,
with the drivers its `CONFIG.SYS` and `AUTOEXEC.BAT` load. The chips are in `pc_at_board`, `pc_video`,
`pc_input_serial_parallel`, `pc_ide_cdrom` and `i386_cpu`. This resource is about the software on top of them.

- **Who answers what:** SeaBIOS the vectors up to 1Ah (INT 10h is the card's VGA BIOS at C000h), the DOS kernel INT 20h to
  2Fh, CTMOUSE INT 33h, MSCDEX or SHSUCDX the CD-ROM part of INT 2Fh, HIMEM or HIMEMX the XMS part, EMM386 or JEMMEX
  INT 67h and INT 4Bh. No DPMI host is built in (section 5.2).
- **The mode:** real mode, or virtual-8086 under EMM386 or JEMMEX; both use the IVT at linear 0. A DOS extender goes to
  protected mode, where its own IDT answers. `debug_get_status` shows `real`, `vm86` or `protected`.

---

## 2. Low Memory

### 2.1 Interrupt Vector Table

1024 bytes at 0000:0000, four bytes a vector, offset then segment, INT n at linear n × 4: INT 21h at 84h, INT 33h at CCh.
SeaBIOS fills it at POST and DOS and drivers hook it as they load. The segment of a vector says who answers: F000h the
BIOS, C000h the video BIOS, a low segment DOS or a resident program.

- **Tables, not code:** INT 1Dh video parameters, 1Eh diskette parameters, 1Fh the 8x8 font (characters 128–255 in modes
  4–6), 41h and 46h the fixed disk parameters, 43h the graphics font a game replaces to draw its own glyphs.
- **Finding a hook:** a write breakpoint on a vector (section 7) stops the program that hooks it, right after the store.

### 2.2 BIOS Data Area

256 bytes at 0040:0000, linear 400h–4FFh. Programs read it directly and some write it.

| Address | Size | Field |
|---|---|---|
| 0040:0000 | 4 words | COM1–COM4 base ports, 0 when absent (LPT1–3 follow at 0040:0008) |
| 0040:000E | word | Segment of the extended BIOS data area (EBDA) |
| 0040:0010 | word | Equipment list, as INT 11h returns it |
| 0040:0013 | word | Conventional memory in KB, as INT 12h returns it |
| 0040:0017 | byte | Keyboard flags: 7 Insert, 6 Caps, 5 Num, 4 Scroll, 3 Alt, 2 Ctrl, 1 Left Shift, 0 Right Shift |
| 0040:001A | word | Keyboard buffer head: offset of the next key to read |
| 0040:001C | word | Keyboard buffer tail: offset of the first free slot |
| 0040:001E | 16 words | The keyboard buffer, a key a word: scan code high, ASCII low |
| 0040:0049 | byte | Current video mode; columns at 0040:004A, cursors at 0040:0050, CRTC port at 0040:0063 |
| 0040:006C | dword | Timer ticks since midnight, 18.2 a second; 1800B0h make a day |
| 0040:0072 | word | POST reset flag: 0000h cold boot, 1234h warm boot (restart address at 0040:0067) |
| 0040:0075 | byte | Number of fixed disks |
| 0040:007B | byte | Bit 5 set when a VDS provider is present |
| 0040:0080 | 2 words | Keyboard buffer start and end+1 offsets, normally 1Eh and 3Eh |
| 0040:0084 | byte | Text rows minus one (EGA, VGA); diskette media state of drives 0 and 1 at 0040:0090 |

- **Keyboard:** empty when head equals tail; INT 16h AH=00h advances the head by 2 and wraps at the end. A game that polls
  the keyboard itself reads 0040:001A and 0040:001C.
- **Timer:** INT 08h increments the dword at 46Ch; a read breakpoint on 0040:006C finds a game's delay loops.
- **Equipment word:** bit 0 floppy drives, bit 1 coprocessor, bits 5–4 initial video (00 for a VGA or EGA), bits 7–6
  floppy drives minus one, bits 11–9 serial ports, bit 12 game port, bits 15–14 parallel ports. Here SeaBIOS sets bit 1
  only when an 80387 answers and bit 12 when port 201h's axis bits read 0, as a game port's do.

- **EBDA:** where a BIOS allocates one, it takes it from the top of conventional memory and lowers 0040:0013 by its size.
  The word at 0040:000E is its segment and its first byte its size in KB. Read both when memory looks short of 640 KB.

---

## 3. Interrupt Vectors

### 3.1 BIOS Services

AH selects the function. All are SeaBIOS's, INT 10h is the VGA BIOS of the card.

| Vector | Service | Functions |
|---|---|---|
| 08h | IRQ0 timer | Increments 0040:006C, counts down the diskette motor, calls INT 1Ch |
| 09h | IRQ1 keyboard | Reads the scan code, calls INT 15h AH=4Fh, queues the key; Ctrl-Break calls INT 1Bh |
| 10h | Video | AH=00h set mode, 01h cursor shape, 02h cursor, 05h page, 06h–07h scroll, 09h–0Ah write char, 0Ch–0Dh pixel, 0Eh teletype, 0Fh get mode (AL mode, AH columns, BH page), 10h palette (AX=1010h one DAC register, 1012h a block), 11h fonts, 13h write string, 1Ah display combination |
| 10h | VBE | AX=4F00h controller info, 4F01h mode info, 4F02h set mode, 4F05h window, 4F07h display start; modes in `pc_video` |
| 11h, 12h | Equipment, memory | The word at 0040:0010, the word at 0040:0013 (KB from 0), in AX |
| 13h | Disk | DL bit 7 selects a hard disk. AH=00h reset, 01h status, 02h read, 03h write, 04h verify, 05h format, 08h parameters, 15h type, 16h change line. CHS: CH cylinder low, CL sector and cylinder high, DH head |
| 16h | Keyboard | AH=00h read, halting the CPU until a key (a blocked game costs no host time), 01h peek (ZF set when empty, a polling game spins here), 02h shift flags, 05h store key, 10h–12h the extended versions |
| 1Ah | Clock | AH=00h ticks in CX:DX and the midnight flag in AL, 02h and 04h RTC time and date; AH=B1h PCI BIOS on the PCI board |
| 1Bh, 1Ch | Hooks | 1Bh Ctrl-Break, 1Ch the timer tick: what a game chains to run code 18.2 times a second |

### 3.2 INT 15h

| AH or AX | Function | Notes |
|---|---|---|
| AX=2400h–2403h | A20 gate | Disable, enable, status, support |
| 53h | APM | `POWEROFF.COM` uses AX=5300h, 5301h, 530Eh and 5307h to turn the PC off |
| 84h | Joystick | DX=0 buttons, DX=1 axes; added to SeaBIOS for this board |
| 86h | Wait | CX:DX microseconds, the delay games use |
| 87h, 88h | Extended memory | 87h copies CX words through a GDT at ES:SI. 88h returns KB from 1 MB, up to 16 MB |
| C2h | Pointing device | PS/2 mouse, the first mouse traffic of a boot |
| E801h, E820h | Above 16 MB | E801h: AX and CX KB from 1–16 MB, BX and DX 64 KB blocks above. E820h: EDX='SMAP', 20-byte entries, EBX continues |

- **The hole:** with a Cirrus card and 16 MB or more SeaBIOS here reports extended memory only up to the end of the RAM
  below the 15–16 MB hole, and E820h has the map from the board's firmware config (`pc_at_board`). XMS drivers hook AH=87h
  and 88h, and HIMEM returns 0 from 88h to protect the HMA, so a game that gets less than it expects is looking at a hook.

### 3.3 DOS and Drivers

Which of these exist depends on what the disk loads (section 6).

| Vector | Provided by | Functions |
|---|---|---|
| 20h–27h | DOS | 20h terminate, 21h the services below, 22h–24h terminate, Ctrl-C and critical error handlers, 27h TSR |
| 28h, 2Ah | DOS | 28h idle, called while DOS waits for input; 2Ah AH=84h in the keyboard wait loop, 80h–82h critical sections |
| 2Fh | DOS, drivers | The multiplex interrupt, below |
| 33h | CTMOUSE | AX=0000h reset (AX=FFFFh when installed, BX buttons), 0001h show, 0002h hide, 0003h position and buttons, 0004h set position, 0007h–0008h range, 000Ch user handler |
| 4Bh | JEMMEX, EMM386 | VDS, AX=8102h–810Ch, announced by 0040:007B bit 5 |
| 67h | JEMMEX, EMM386 | EMS, and VCPI at AH=DEh |

- **INT 21h:** AH=25h and 35h set and get a vector, 31h and 4Ch terminate, 3Dh–42h files, 48h–4Ah memory, 4Bh EXEC, 62h the
  current PSP in BX, 52h the list of lists, 58h memory strategy and UMB link. The debugger names every one.
- **INT 2Fh:** AX=4300h XMS driver present (AL=80h), 4310h XMS entry in ES:BX, 1500h MSCDEX installed (BX drive letters used,
  CX first, 0 for A:), 150Bh drive check, 150Dh drive letters, 1510h device request, 1680h release time slice, 1687h DPMI
  host check (AX=0 when present).
- **EMS:** INT 67h AH=40h status, 41h page frame segment, 42h page counts, 43h allocate, 44h map, 45h free. A program
  finds the manager by opening the device `EMMXXXX0`, or by the name at offset 0Ah of INT 67h's segment.
- **CD-ROM:** UDVD2.SYS is the device driver, named `FDCD0001`; the drive letter comes from the redirector on top: MSCDEX
  on MS-DOS 6, SHSUCDX on FreeDOS and MS-DOS 5.0. The packets are in `pc_ide_cdrom`.

---

## 4. DOS Structures

### 4.1 PSP and Environment

The 256 bytes at the start of a program's memory: 00h `CD 20` (INT 20h, the mark of a valid PSP), 02h the segment past the
memory allocated to it, 0Ah–12h the saved INT 22h, 23h and 24h vectors, 16h the parent PSP, 18h the job file table (a byte
a handle, FFh closed), 2Ch the environment segment, 5Ch and 6Ch the default FCBs, 80h the command tail (length, text,
0Dh), which is also the default DTA.

The environment is `NAME=value` strings ending in a zero byte each, then one more zero. From DOS 3 a word follows
(normally 1) and the full path of the owning program, which is how to get its name.

### 4.2 Memory Control Blocks

Every block DOS hands out has a 16-byte header one paragraph before it: 00h is 4Dh, or 5Ah on the last block, 01h the
owner's PSP segment (0 free, 8 DOS), 03h the size in paragraphs, 08h the program name, 8 characters, from DOS 4 (DOS's own
blocks hold "SC" or "SD"). The next block is at segment + size + 1. INT 21h AH=52h returns the list of lists in ES:BX and
the segment of the first MCB is the word at ES:[BX-2]. With `DOS=UMB` and the link on (INT 21h AX=5803h BX=1) the chain
goes on through the UMBs.

To find the running program: INT 21h AH=62h returns its PSP segment in BX (or walk the chain for the block whose owner is
its own segment), PSP:002C gives the environment, the path follows its double zero and count word, and the short name is
at PSP-1:0008.

### 4.3 How EXEC Loads a Program

INT 21h AH=4Bh AL=00h loads and runs: DS:DX the name, ES:BX the parameter block. DOS ignores the extension: `MZ` or `ZM` in
the first two bytes is an .EXE, anything else a .COM. The caller must free memory first (AH=49h or 4Ah) or EXEC fails.

| | .COM | .EXE |
|---|---|---|
| Memory | All of the largest free block | The image plus the header's minimum to maximum extra paragraphs |
| Code | At PSP:0100h | The load module after the PSP, at PSP+10h |
| Registers | CS=DS=ES=SS=PSP, IP=100h | DS=ES=PSP, CS:IP and SS:SP from the header, relative to the load segment |

.EXE header: 00h signature, 02h and 04h size (last page, 512-byte pages), 06h relocation count, 08h header paragraphs, 0Ah
and 0Ch minimum and maximum extra paragraphs, 0Eh SS, 10h SP, 14h IP then CS, 18h relocation table offset (40h or more
marks a new-format executable). A game built for a DOS extender starts as an ordinary .EXE whose first code is the
extender (section 5.2).

---

## 5. Memory Managers and Extenders

### 5.1 What the Disks Configure

A game picks `memory` in its descriptor: `xms` by default, `ems` when `ems_kb` is set. The built-in disks use `umb`.

| Mode | FreeDOS | MS-DOS |
|---|---|---|
| `none` | No driver | No driver |
| `xms` | `DOS=HIGH`, HIMEMX | `DOS=HIGH`, HIMEM.SYS |
| `umb` | `DOS=HIGH,UMB`, `DOSDATA=UMB`, JEMMEX `NOEMS` | `DOS=HIGH,UMB`, HIMEM.SYS, EMM386 `NOEMS` |
| `ems` | The `umb` lines, JEMMEX with `FRAME=` | The `umb` lines, EMM386 `RAM` with `FRAME=` |

- **HMA:** `DOS=HIGH` moves the kernel to FFFF:0010h–FFFF:FFFFh, which needs A20. SeaBIOS leaves A20 off when it jumps to
  the boot sector and the XMS driver turns it on, so the `system.a20` trace shows who does it.
- **UMB:** blocks between the VGA BIOS and the BIOS copy that SeaBIOS leaves free, where `DEVICEHIGH` and `LH` load.
  EMM386 and JEMMEX run the CPU in virtual-8086 mode, so `debug_get_status` shows `vm86` under them.
- **XMS:** the entry from INT 2Fh AX=4310h is a far call with the function in AH: 00h version, 01h–02h HMA, 03h–07h A20,
  08h free memory, 09h–0Bh allocate, free, move, 10h–12h UMBs. HIMEM stops at the 15–16 MB hole; JEMMEX reaches above it.
- **EMS:** a page frame of four 16 KB physical pages, at DC00h on the VGA and both Cirrus cards, mapped onto 16 KB logical
  pages. With `NOEMS` there is no page frame: EMM386 gives no EMS, JEMMEX's device is `EMMQXXX0` so the usual detection
  fails and its INT 67h API still works.
- **VCPI** is AH=DEh on INT 67h from V86 mode: DE00h presence (BX=0100h), DE01h protected-mode interface, DE03h–DE05h 4 KB
  pages, DE0Ah–DE0Bh the PIC bases, DE0Ch switch mode. EMM386 provides it even with `NOEMS` unless `NOVCPI` is given,
  JEMMEX too, with VDS. A server leaves A20 on.

### 5.2 DOS Extenders and DPMI

An extender checks for a DPMI host, a VCPI server, XMS and raw INT 15h memory in that order, as the specifications say;
PMODE/W checks VCPI first by default. DOS/4GW takes all the extended memory unless `DOS16M` limits it and forces VCPI on
when it finds a server.

On these disks that means: with `none` or `xms` it takes INT 15h or XMS memory and switches itself, with no V86 monitor;
with `umb` or `ems` it finds a VCPI server on INT 67h and the CPU stays under a V86 monitor.

- **No DPMI host is installed.** JEMM and JEMMEX refuse to load when one is there. A game brings its own, or none. INT 2Fh
  AX=1687h returns nonzero until one runs. A host returns AX=0, BX bit 0 for 32-bit programs, CL the CPU, DH.DL the
  version, ES:DI the mode-switch entry. The client calls it and lands in protected mode with 64 KB selectors.
- **INT 31h:** 0000h–000Ch descriptors, 0100h–0102h DOS memory, 0200h–0205h vectors (an extender hooks INT 21h with 0204h
  and 0205h), 0300h–0303h real-mode calls, 0400h version, 0500h–0503h memory, 0800h physical mapping. In protected mode
  the IVT no longer answers: `get_i386_descriptors` shows the IDT.

---

## 6. What Geardox Ships

### 6.1 Boot Order

SeaBIOS tries the floppy, then the hard disks, then the CD-ROM, unless the boot device setting puts another first;
`debug_reset` with `boot` sets it for one reset. A disk with no system prints "Not a system disk. Press a key for the
next boot device." and calls INT 18h. A game opened from a `.gdox` descriptor boots its C:, which runs it from
`AUTOEXEC.BAT`.

### 6.2 The Disks and Their Startup

`FreeDOS.hdd` and `MS-DOS.hdd` are made the first time they are needed; with no hard disk attached the PC boots one,
MS-DOS when imported. Game disks are built per game from the descriptor.

| File | Built-in disks | Game disks |
|---|---|---|
| `CONFIG.SYS` | `SWITCHES=/F`, `LASTDRIVE=Z`, `BUFFERS=20`, `FILES=40`, the `umb` drivers, the idle driver, UDVD2 high, `SHELL=` with `/E:1024 /P` | The mode's lines, the descriptor's `files` and `buffers`, `[config_sys]`, UDVD2 only with a CD |
| `AUTOEXEC.BAT` | `PATH`, `TEMP=C:\TEMP`, `DIRCMD=/OGN`, `BLASTER=A220 I5 D1 H5 P330`, `PROMPT $P$G`, KEYB, CTMOUSE, SHSUCDX or MSCDEX | The same, `BLASTER` from the sound card in use, then the game's lines, `GDXRUN` and the game |

- **PATH** is `C:\FREEDOS\BIN` on FreeDOS and `C:\DOS;C:\GEARDOX` on MS-DOS, where `\GEARDOX` holds CTMOUSE, UDVD2,
  GDXIDLE, GDXRUN, GDXDBG, POWEROFF and, for MS-DOS 5.0, SHSUCDX. CTMOUSE finds the PS/2 or the serial mouse.
- **GDXRUN.COM** reads the firmware config file `opt/geardox/target` and exits with ERRORLEVEL 0 for the game, 1 for setup,
  2 for the prompt and 3 for debug, which the batch file branches on. `load_media` `target` sets it. **POWEROFF.COM** turns
  the PC off through APM when a game's `on_exit` asks.
- **GDXDBG.COM** runs a program for the development tools: debug runs the game's program, or `run_program`'s command,
  through it. It loads the program with INT 21h AX=4B01h and reports it, its console output and its exit code to
  Geardox; the `dos_development` resource has the details.

### 6.3 GDXIDLE.SYS

MS-DOS keeps a host core busy at its prompt. GDXIDLE is Geardox's device driver, 75 bytes, loaded with
`DEVICEHIGH=C:\GEARDOX\GDXIDLE.SYS` on `MS-DOS.hdd` and on MS-DOS game disks. It hooks INT 2Ah: AH=84h, which DOS calls in
its wait-for-a-key loop, runs `STI` and `HLT` and then chains to the old handler. Every other AH chains straight away, since
DOS calls AH=80h to 82h constantly for its critical sections. FreeDOS has `IDLEHALT=1` for the same and loads no driver.
It covers the prompt and programs that wait for a key through DOS; a program that polls INT 16h itself, as most games do,
still runs the CPU flat out.

### 6.4 Debug Console and BIOS Log

SeaBIOS writes its log to port 402h, which reads back E9h so firmware knows it is there, at debug level 1
(`bios.config`). `get_bios_log` returns up to the last 64 KB. The POST code goes to port 80h (`system.post` trace) and
`get_system_status` shows the firmware config and debug console ports.

---

## 7. Driving it from MCP

### 7.1 Stopping on a Service

- **`set_breakpoint_on_interrupt`** takes `vector` and `source` (`software` for an INT n, `hardware` for a PIC IRQ,
  `exception` or `any`, the default), or `irq`, which follows the line if a program moves the PIC base. It stops before the
  handler's first instruction. Use `source: software` for INT 10h to 1Fh: vectors below 20h are named as CPU exceptions,
  so with `any` INT 10h also matches exception 16, and IRQ0–7's vectors 08h–0Fh want `hardware` or `irq`.
  `clear_breakpoint_on_interrupt` and `list_breakpoints` manage them.
- **A stop:** `debug_get_status` has `breakpoint` with `kind: interrupt`, `vector`, `source` and, for INT 21h and MSCDEX's
  INT 2Fh, a `function` such as "Open file". For the BIOS read AH from `get_i386_status`.
- **`set_breakpoint`** takes `address`, `end_address`, `type` (`execute`, `read`, `write`, `access`) and `space` (`linear`,
  `physical`, `io`). Useful ones:
  - `write` at 84h–87h: whoever hooks INT 21h; at CCh–CFh, INT 33h. `write` at 400h–4FFh: whoever changes the BIOS data.
  - `read` at 46Ch: the game's tick waits; at 41Ah–41Dh, its keyboard polling. `io` at 402h: the BIOS log.
- **Trace filters** for `set_trace_log`: `cpu.software_ints` records each INT n as `INT $21 DOS function call: Open file
  AX:$3D00 From:$... To:$...`, with AX and the caller. `cpu.irqs` records hardware interrupts with the line; it and
  `cpu.exceptions` are on by default. Add `system.a20`, `system.post`, `keyboard.keys` as needed. `cpu.software_ints`
  on a running game is a lot of lines, so start it at the point of interest.

### 7.2 Reading State

- **`list_memory_areas`** gives `LINEAR` (id 0), `PHYSICAL` (id 1), `I/O PORTS` (id 2) and the regions. `read_memory` takes
  `area`, `offset` (`0040:006C` or linear hex in LINEAR) and `size` up to 65536.
  - **IVT:** area 0, offset `0`, size 1024. **BDA:** offset `0040:0000`, size 256.
  - **PSP:** offset `SEG:0000`, size 256, with SEG from BX after INT 21h AH=62h. The environment segment is at 2Ch.
  - **MCBs:** 16 bytes at `SEG-1:0000`, the first from the word at ES:[BX-2] after INT 21h AH=52h; step `size + 1`
    paragraphs until the type byte is 5Ah.
- **`translate_address`** on any SEG:OFF gives the linear and physical address, the memory region, and with paging on the
  PDE and PTE.
- **`get_call_stack`** lists calls, interrupts and exceptions with `from`, `to` and `return`, the interrupts with
  `vector` and `source`: `return` is where the game resumes after the DOS or BIOS call.
- **`list_symbols`** lists user symbols and the debugger's `SUB_` and `LOC_` labels, not DOS's or the BIOS's. Name a
  game's entries with `add_symbol` or `load_symbols`. `get_disassembly` adds `vector` and, for the next INT 21h or 2Fh
  instruction, its `function`.
- **`get_game_info`** shows `system` (`freedos` or `msdos`), the descriptor `keys` such as `dos.memory` and `dos.files`, the
  `target`, the disk `image` and whether it was `built`: which CONFIG.SYS the game ran. `get_text_screen` reads a DOS prompt.

### 7.3 Driving the Software

- **`load_media`** with `file_path` a `.gdox` and `target`: `game` (default), `setup` or `prompt`, which opens a DOS prompt in
  the game's folder. It builds or reuses the disk and powers on.
- **`keyboard_type`** types ASCII text, at most 256 characters, `frames_per_key` frames a key (default 3), newline as RETURN:
  the way to use the prompt. `keyboard_key` presses a named key with `press`, `release` or `press_and_release`.
- **`add_memory_watch`** takes `area`, `address`, `size` in bits and `notes`; `list_memory_watches` shows values. With `freeze`
  it holds the byte, word or dword at the value it had, like a cheat, and the debugger writes it back each frame: a game's
  lives or energy at a known address stays put.
- **`controller_macro`** runs a frame-timed script of tap, press, release and wait over PC keys, mouse buttons and joystick
  controls, for input a game needs on a schedule. The keyboard buffer at 0040:001E shows what arrived.
- **No mouse:** a vector at CCh of 0000:0000, or pointing at an IRET, means CTMOUSE isn't loaded. `mouse_move`,
  `mouse_click` and `joystick_set` feed the mouse and the game port.
