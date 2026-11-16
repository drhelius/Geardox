---
name: geardox-debugging
description: >-
  Debug and trace MS-DOS PC games and software using the Geardox emulator MCP
  server. Provides workflows for Intel 80386, 486 and Pentium CPU debugging,
  breakpoint management, hardware inspection, disassembly analysis, and
  execution tracing. Use when the user wants to debug a DOS game, trace code
  execution, inspect i386 registers, segment descriptors, paging or hardware
  state, set breakpoints, analyze interrupts, step through 80386 instructions,
  reverse engineer game code, examine VGA/Cirrus/OPL/Sound Blaster/IDE/floppy
  registers, read a DOS text screen, view the call stack, or diagnose
  rendering, audio, or timing issues. Also use when the user mentions MS-DOS
  or FreeDOS development, DOS extenders such as DOS/4GW, DJGPP or PMODE/W,
  protected mode or VM86 code, EMS and XMS, INT 21h or BIOS interrupt
  handlers, Sound Blaster detection, VESA mode sets, CD-ROM game debugging,
  floppy boot debugging, or DOS debugging with Geardox.
compatibility: >-
  Requires the Geardox MCP server. Direct tool mode is the default. Before
  installing or configuring, call debug_get_status to check if the server is
  already connected. If --mcp-router is enabled, use get_tool_info and
  execute_tool for routed tools.
metadata:
  author: drhelius
  version: "1.0"
---

# MS-DOS Game Debugging with Geardox

## Overview

Debug MS-DOS PC games and software using the Geardox emulator as an MCP server. Control execution (pause, step, breakpoints), inspect the Intel 80386, 486 and Pentium CPU with its x87 FPU and the hardware (8259A, 8254, 8237, MC146818, 8042, VGA and Cirrus video, OPL FM, Sound Blaster, MPU-401, ATAPI CD-ROM, 82077 floppy, IDE disks), read/write memory, disassemble code, trace instructions, read DOS text screens and capture screenshots — all through MCP tool calls. Hardware documentation is available in the [references/](references/) directory.

## MCP Server Prerequisite

**IMPORTANT — Check before installing:** Before attempting any installation or configuration, you MUST first verify if the Geardox MCP server is already connected in your current session. In the default mode, call `debug_get_status` directly. If Geardox was intentionally started with `--mcp-router`, call `get_tool_info` with `{"name":"debug_get_status"}`, then call `execute_tool` with `{"name":"debug_get_status","arguments":{}}`. A valid response from either workflow means the server is active and ready.

Only if neither workflow is available or the call fails, you need to help install and configure the Geardox MCP server:

### Installing Geardox

Run the bundled install script (macOS/Linux):

```bash
bash scripts/install.sh
```

The script prints the path of an existing Geardox (the `GEARDOX_BIN` environment variable first, then `geardox` on `PATH`). Otherwise it installs the latest release: via Homebrew on macOS, or from the PPA or the GitHub release on Linux. You can set `INSTALL_DIR` to control where a downloaded binary goes (default: `~/.local/bin`).

Geardox has no release yet. Until there is one, the script prints how to build it from source: clone [https://github.com/drhelius/geardox](https://github.com/drhelius/geardox), run `make -C platforms/macos` on macOS or `make -C platforms/linux` on Linux, and use the binary at `platforms/<os>/geardox`. Set `GEARDOX_BIN` to that path.

### Connecting as MCP Server

Configure your AI client to run Geardox as an MCP server via STDIO transport. For Claude Code:
```bash
claude mcp add --transport stdio --scope user geardox -- /path/to/geardox --mcp-stdio
```
For Codex:
```bash
codex mcp add geardox -- /path/to/geardox --mcp-stdio
```
Replace `/path/to/geardox` with the actual binary path from the install script. Add `--headless` before `--mcp-stdio` on headless machines. Machine options such as `--model`, `--ram`, `--sound` and `--video` apply to that run only.

### Firmware

Nothing to set up. Geardox has SeaBIOS and SeaVGABIOS built in, so there are no BIOS files to find or load. With no media attached it boots a built-in FreeDOS hard disk. `get_bios_log` returns what SeaBIOS wrote during POST and `get_system_status` shows the machine, the BIOS images and where each setting came from.

### Hardware Documentation (References)

MS-DOS PC hardware documentation is available in the [references/](references/) directory. Load them into your context when investigating specific hardware.

| Reference | File | Load when... |
|---|---|---|
| Intel 80386 CPU | [references/i386_cpu.md](references/i386_cpu.md) | Registers, real/protected/VM86 modes, segmentation, paging, exceptions, IDT, PC/AT IRQ vectors |
| x87 FPU | [references/x87_fpu.md](references/x87_fpu.md) | 80387 and integrated FPU, register stack, control and status words, IRQ13 path, FPU detection |
| PC/AT Board | [references/pc_at_board.md](references/pc_at_board.md) | Memory map, A20, I/O map, 8259A pair, 8254, resets, SeaBIOS, firmware config, MC146818 CMOS |
| Pentium and PCI | [references/pentium_pci.md](references/pentium_pci.md) | CPUID, CR4, RDTSC and MSRs, pairing timing, PCI configuration, 430FX, PIIX, PCI GD5446 |
| Video | [references/pc_video.md](references/pc_video.md) | CGA, EGA, VGA ports, Cirrus GD5428 and GD5446 extensions, VBE |
| Sound | [references/pc_sound.md](references/pc_sound.md) | OPL2 and OPL3, Sound Blaster DSP and mixers, MPU-401, PC speaker |
| Floppy and DMA | [references/pc_floppy_dma.md](references/pc_floppy_dma.md) | 82077 floppy controller, commands, disk images, 8237A DMA channels and pages |
| IDE and CD-ROM | [references/pc_ide_cdrom.md](references/pc_ide_cdrom.md) | IDE registers, ATAPI packet protocol and commands, tray and disc, CD audio |
| Input, Serial, Parallel | [references/pc_input_serial_parallel.md](references/pc_input_serial_parallel.md) | PS/2 and serial mouse, 16550A serial ports, game port joysticks, parallel port |
| BIOS and DOS Services | [references/dos_bios_services.md](references/dos_bios_services.md) | IVT, BIOS data area, INT 10h-1Ah, INT 21h, XMS, EMS, DPMI, PSP, MCB chain, memory managers, boot order |

---

## Debugging Workflow

### 1. Load and Orient

```
load_media → get_game_info / get_media_info → get_i386_status → get_text_screen or get_screenshot
```

Start every session by checking `debug_get_status` (powered on, paused, mode) and `get_system_status` (model, CPU, RAM, FPU, video, sound). Then load what you want to debug with `load_media`:

- A `.gdox` game descriptor, or a **folder** or **ZIP** with one in its root, opens the game: its disk on C: is built or reused and the machine powers on. A folder or ZIP the built-in game database recognises, or a `.exe`, `.com` or `.bat`, opens as a game too. `target` chooses what runs at boot: `game` (default), `setup` or `prompt` (a DOS prompt in the game's folder). `get_game_info` describes the open game: descriptor, machine in use and where each value came from, the DOS it boots (FreeDOS or an imported MS-DOS), the disk and where saves go.
- A CD image (`.cue`, `.chd`, `.iso`) goes in the ATAPI CD-ROM drive. A floppy image (`.img`, `.ima`, `.vfd`, `.flp`, `.dsk`, `.d77`, `.d88`, `.hdm`, `.xdf`, `.mfm`, `.m3u` or `.zip`) goes in drive 0, or use `insert_floppy` for drive 0 or 1. A hard disk image (`.hdd`, `.h0`-`.h3`, `.img` or `.hdm` over 8 MB, a fixed `.vhd`, a hard disk `.chd`) attaches to its IDE slot or the first free one and powers on an empty machine; `attach_hard_disk` also takes a folder or ZIP as a FAT16 disk and can `create_size_mb` a blank one.
- With nothing attached, the built-in DOS disks (FreeDOS, or MS-DOS once imported) boot to a DOS prompt, handy for running a program by hand with `keyboard_type`.

`get_media_info` confirms what is loaded and `list_recent_media` lists recently opened images. `debug_reset` takes an optional `boot` (`floppy1`, `floppy2`, `hard_disk`, `cdrom`) to pick the boot device for one reset.

Load symbols with `load_symbols` (a text file with `ADDRESS NAME` or `NAME = ADDRESS` per line, GNU `nm` output, GNU ld and NASM maps) or add individual labels with `add_symbol`. A `.sym` file beside a CD or floppy image loads with it.

### 2. Pause and Inspect

Always call `debug_pause` before inspecting state. While paused:

- **CPU state**: `get_i386_status` — general registers, EIP, EFLAGS bits, CS:EIP with linear and physical PC, CR0/CR2/CR3, mode (real, protected, vm86), CPL, IOPL, code and stack size, the last exception, segment descriptor caches, GDTR/IDTR/LDTR/TR, debug registers, CR4, TSC and MSRs on a Pentium, and an `fpu` section (stack, control word, status word, tags, last instruction) when an FPU is fitted
- **Disassembly**: `get_disassembly` with `start_address` and `end_address` or `count` — decodes the current contents of memory; use `code_size` (`auto`, `16`, `32`) to force 16-bit or 32-bit decoding and `detailed` for control flow, targets, I/O port names, interrupt vector names and the BIOS or DOS function an `INT` is about to call
- **Call stack**: `get_call_stack` — calls, interrupts and exceptions with their vectors
- **Memory**: `read_memory` with a memory area ID (use `list_memory_areas` to discover available areas and their IDs)
- **Text screen**: `get_text_screen` — what a DOS text screen says, with the cursor and optionally `attributes`; faster and clearer than a screenshot when the game is in a text mode
- **Debugger state**: `debug_get_status` — paused state, what stopped execution, linear and logical PC, CPU mode, halted

### Addresses

Every tool that takes an Intel 80386 address accepts:

- `1234ABCD`, `0x1234ABCD` or `$1234ABCD` — a linear address
- `CS:1234` — a segment register (CS, DS, ES, FS, GS, SS) and an offset, using its cached base
- `0008:00001234` — a selector in protected mode (read from the GDT or LDT) or a segment in real and VM86 mode (`B800:0000`, `0040:006C`), and an offset

In real and VM86 mode a segment is multiplied by 16; in protected mode a selector goes through the GDT or LDT and then through paging when CR0.PG is set. Use `translate_address` to walk an address through segmentation and paging (PDE, PTE, page flags, physical address, region) or to find out why it fails. `get_page_directory` lists the present page tables. `get_i386_descriptors` decodes GDT, LDT and IDT entries (in real mode the IDT is the interrupt vector table).

### 3. Set Breakpoints

Use breakpoints to stop execution at points of interest:

| Breakpoint Type | Tool | Use Case |
|---|---|---|
| Execution | `set_breakpoint` (type: execute) | Stop before the instruction at the address runs |
| Read | `set_breakpoint` (type: read) | Stop when memory is read |
| Write | `set_breakpoint` (type: write) | Stop when memory is written |
| Access | `set_breakpoint` (type: access) | Stop on any read or write |
| I/O port | `set_breakpoint` (space: io) | Stop on IN/OUT/INS/OUTS to a port |
| Range | `set_breakpoint` (end_address) | Cover an address range (same type and space) |
| Interrupt | `set_breakpoint_on_interrupt` (vector) | Stop on entry to an interrupt vector (0-255) |
| IRQ | `set_breakpoint_on_interrupt` (irq) | Stop when the PIC delivers an IRQ line (0-15), whatever its vector |

Breakpoints support three address spaces (`space`): `linear` (default), `physical` and `io`. Execute breakpoints are linear or physical. Physical read and write breakpoints also see DMA transfers. `enabled: false` adds or keeps a breakpoint disabled, and `set_breakpoints_active` turns all of them on or off at once. Interrupt breakpoints by `vector` take a `source` (`any`, `exception`, `hardware`, `software`) and stop before the handler's first instruction. Use `source: software` for INT 10h to 1Fh: vectors below 20h are also named as CPU exceptions.

**Important**: Read, write and access breakpoints stop *after* the instruction that made the memory access, so the PC is at the following instruction. Execute breakpoints stop before the instruction runs. `debug_get_status` reports in `breakpoint` what stopped execution (kind, space, address, size, `dma` when a transfer made it, `port_name` for an I/O port, or the vector, source and, for a known BIOS or DOS call, its `function`).

Manage breakpoints with `list_breakpoints` (address, interrupt and IRQ breakpoints), `remove_breakpoint` and `clear_breakpoint_on_interrupt` (by `vector` and `source`, or by `irq`).

### 4. Step Through Code

After hitting a breakpoint or pausing:

| Action | Tool | Behavior |
|---|---|---|
| Step Into | `debug_step_into` | Execute one instruction, enter calls and interrupts (a `REP` string instruction runs all its elements) |
| Step Over | `debug_step_over` | Execute one instruction, run through CALL subroutines and INT handlers |
| Step Out | `debug_step_out` | Run until the current subroutine or interrupt handler returns |
| Step Frame | `debug_step_frame` | Run `frames` (default 1) with breakpoints active, then pause; answers with the status when done, or at once with `wait: false` |
| Run To | `debug_run_to_cursor` | Continue until PC reaches target address |
| Continue | `debug_continue` | Resume normal execution |
| Reset | `debug_reset` | Reset the emulated PC |

After each step, call `get_i386_status` and `get_disassembly` to see where you are. `toggle_fast_forward` speeds through loading screens and intros before you pause.

### 5. Trace Execution

The trace logger records executed instructions (CS:EIP, linear address, mode, bytes, Intel syntax, registers) interleaved with hardware events (IRQs, software INTs, I/O port accesses, DMA, FM, mixer, MIDI, CD-ROM packets, floppy and IDE commands, keyboard and input events, VSYNC).

1. `set_trace_log` with `enabled: true` to start recording (optionally set `filters` and `output`)
2. Let the game run or step through code (the trace records while the debugger runs the machine)
3. `set_trace_log` with `enabled: false` to stop (entries are preserved)
4. `get_trace_log` to read recorded entries (`start` negative reads the last N lines, `count` up to 1000)

Trace filters are event streams such as `cpu.instructions`, `cpu.software_ints`, `cpu.irqs`, `io.writes`, `video.vsync` or `cdrom.packets`, or a whole group: `cpu`, `io`, `pic`, `timer`, `dma`, `video`, `fm`, `pcm`, `mixer`, `midi`, `cdrom`, `fdc`, `ide`, `keyboard`, `input`, `serial`, `parallel`, `system` (the tool schema lists every stream). `cpu.software_ints` logs each `INT n` with its BIOS or DOS function name and AX, which is the quickest way to see what a game asks of DOS, but it is a lot of lines on a running game: start it at the point of interest. `system.a20`, `system.post` and `keyboard.keys` follow A20, the POST code and key events. `video.missed_vblank` with `vblank_watch_address` detects frames where a game missed its vertical retrace wait. `output: "disk"` also streams the trace to a text file (`output_path`, `disk_size`) for long captures; `memory_size` sets how many entries are kept in memory.

Tracing is essential for understanding timing-sensitive code, interrupt handlers, and hardware interaction sequences. Entry times are machine clocks, not CPU cycles.

### 6. Profile

`set_profiler` (`action`: `start`, `stop`, `reset`) collects calls and CPU cycles per function and interrupt vector while the debugger runs the machine. `get_profiler_data` returns the results (`sort`: `inclusive`, `exclusive`, `calls`, `average`, `max`; `count`; `filter`). Use it to find the hot routines of a game before stepping through them.

---

## Hardware Inspection

### CPU (80386, 486 and Pentium)

- `get_i386_status` — full CPU state: registers, EFLAGS, CR0/CR2/CR3, mode, CPL, IOPL, descriptor caches, GDTR/IDTR/LDTR/TR, debug and test registers; on a 486 the AC flag and CR0 bits CD, NW, AM, WP, NE; on a Pentium CR4, the TSC and the MSRs; and the x87 FPU when fitted
- `write_i386_register` — modify a register live (EAX-EDI, EIP, EFLAGS, CR0, CR2, CR3, CR4 on a Pentium, DR0-DR7, TR3-TR7, CS-GS, LDTR, TR, the descriptor table bases and limits). Segment writes load the descriptor like the CPU does
- `get_i386_descriptors` — decode `gdt`, `ldt` or `idt` entries (`table`, `start`, `count`)
- `get_page_directory` — present page-directory entries, or with `index` the present pages of that page table

### Board: Interrupts, Timers, DMA, Clock, Keyboard Controller

- `get_pic_status` — both 8259A PICs: per-IRQ source, level, request, in-service, mask and vector; ICW1-ICW4 and modes
- `get_pit_status` — the 8254: the three counters with gate, mode, reload, count, OUT and period, plus the speaker and refresh bits of port 0061
- `get_dma_status` — both 8237 DMA controllers: command, status, mask, requests, and each channel's device, mode, page, address and count (channel 2 is the floppy, channel 1 or 5 usually the Sound Blaster)
- `get_rtc_status` — MC146818 clock and CMOS registers: date, time, alarm, periodic rate, interrupt enables
- `get_system_status` — model, CPU clock and Turbo, RAM, FPU, video card, floppy drives, A20, port 0092 and 0061, POST code, CPU resets and the last reset cause, the memory map and the built-in BIOS images
- `set_turbo` — the board's Turbo button: `turbo: false` runs the CPU at a third of its clock without a reset, useful for games that run too fast
- `get_keyboard_status` — 8042 and MF2 keyboard: status, command byte, ports, output buffer, A20 sources, scan code set, LEDs, queued bytes and held keys
- `get_serial_status` — both 16550A serial ports: what is attached, divisor and baud, all registers decoded, FIFOs and the IRQ line
- `get_parallel_status` — LPT1 data latch, status and control
- `get_bios_log` — what SeaBIOS wrote to the debug console at port 0402 (POST messages, boot attempts, option ROMs); `max_bytes` returns the tail

### Video (VGA, Cirrus, CGA, EGA)

- `get_crtc_status` — mode, timing, frame size, beam position, cursor and the vertical interrupt (a CGA's 6845 has none)
- `get_crtc_registers` — the CRTC registers with names and the CR00-CR07 write protection
- `write_crtc_register` — write a CRTC register like the CPU does
- `get_video_output_status` — the VGA paths around the CRTC: memory window, chain-4, odd/even, write pipeline, attribute controller, DAC. With a Cirrus card (GD5428 on ISA or VL-Bus, GD5446 on PCI) it adds the extended mode, pixel format, clocks, banks, linear aperture, pitch and start; EGA and CGA have their own objects
- `get_vga_registers` — miscellaneous, sequencer, graphics, CRTC and attribute registers (plus the Cirrus extensions)
- `get_palettes` — the 16 attribute colors and the 256 DAC entries as RGB888 (`palette`: `all`, `attribute`, `dac`)
- `get_frame_buffer` — decode video memory as PNG (`buffer`: `plane0`-`plane3`, `planar`, `256`, `text`, `linear`, with `pitch`; `linear` on a Cirrus card also takes `format` 8, 15, 16 or 24 and `start`); shows off-screen graphics that the screenshot does not. There are no sprites on the PC: games draw into video memory
- `get_text_screen` — a text mode screen as rows of characters, with cursor and `attributes`
- `get_screenshot` — the current rendered frame as PNG

### Audio (OPL, Sound Blaster, MPU-401, PC Speaker)

- `get_sound_status` — the sound card: OPL chips, the Sound Blaster DSP (version, IRQ, DMA, speaker, sample rate, the transfer in progress with format, auto-init, high-speed, stereo, block size), mixer levels (`voice`, `fm`, `cd`), the MPU-401 and MIDI out (messages sent, SysEx log, synth and port)
- `get_opl_status` — OPL2 or OPL3: timers, rhythm, and per channel frequency, note, key, type (2op, 4op, drum), connection, feedback, pan and both operators' parameters, envelope and level (`chip`, `channel` optional)
- `get_opl_registers` — one OPL register bank as last written, with register names (`chip`, `bank`)
- `set_audio_mute` — mute `fm` (or one `channel`), `pcm` (the Sound Blaster DSP's digital audio), `cdda` or `buzzer` (the PC speaker) in the debugger without changing emulated state; useful to isolate a sound source
- `get_pit_status` shows counter 2 and port 0061, which drive the PC speaker

### CD-ROM, Floppy and Hard Disks

- `get_cdrom_status` — ATAPI CD-ROM drive: tray, disc, ready or spinning up, last packet, sense, audio status and volumes, CD-DA playback (`audio`) and the `.m3u` `playlist`
- `list_cdrom_tracks` — the disc TOC: track types, start and end MSF, LBA, length
- `read_cdrom_sector` — read one sector of the image by `lba` (`mode`: `user` 2048 bytes or `raw` 2352 bytes). Does not move the drive head. `eject_cdrom`, `close_cdrom_tray` and `insert_playlist_disc` change discs as the user would
- `get_fdc_status` — 82077 floppy controller: DOR, MSR, change line, data rate, phase, command and result bytes, PCN per drive, INT and DRQ
- `list_floppy_drives` — both drives: image, media, geometry, write protect, modified, head position, motor
- `read_floppy_sector` — read a sector by ID (`drive`, `cylinder`, `head`, `sector`); without `sector` it lists the track's sector IDs
- `get_ide_status` — both IDE channels: task file registers, command, phase, transfer position, per device status, error, geometry and translation; the CD-ROM slot shows its ATAPI command and packet
- `list_hard_disks` — IDE slots 0-3: image, size, geometry, BIOS translation, write protect, unwritten changes and where the disk came from (`game`, `msdos`, `freedos`, `image`, `folder`, `zip`)
- `attach_hard_disk` / `detach_hard_disk` — change a slot's disk (at the next reset on a running machine); `create_size_mb` makes a blank image first
- `read_hard_disk_sector` — read a 512-byte sector by `id` and `lba` as the machine sees it, without bus traffic
- `flush_hard_disks` — write pending changes to the image files now

### Screenshots

- `get_screenshot` — current rendered frame as PNG; right after `load_state` it is the frame saved in the state or, when it can't be restored, an error until a frame runs

Use screenshots after stepping or continuing to see the visual impact of changes.

---

## Common Debugging Scenarios

### Finding the Boot Path

1. `load_media` with a floppy, CD or `.gdox` and `debug_pause` immediately, or `debug_reset` with `boot` to pick the boot device. The CPU starts at the reset vector (FFFFFFF0h) in the System BIOS and jumps into the BIOS copy below 1 MB
2. `get_bios_log` — SeaBIOS's POST messages and which devices it tried to boot, in order (floppy, hard disk, CD-ROM)
3. `get_disassembly` at `CS:EIP`, and set an execution breakpoint at `0000:7C00` to catch the boot sector after SeaBIOS loads it through INT 19h
4. `set_trace_log` with `filters: ["cpu.software_ints", "system.post", "fdc", "ide"]` to see the INT 13h reads and the controller commands behind them
5. Once the DOS kernel and the game load, `get_call_stack` and `add_symbol` to label the entry points

### Finding an Interrupt Handler

1. `get_pic_status` to read the vectors programmed into the master and slave PICs (08h-0Fh and 70h-77h by default; a DOS extender may move them)
2. `set_breakpoint_on_interrupt` with the `irq` line (it follows the line whatever vector the PIC uses: 0 timer, 1 keyboard, 4 COM1, 5 or 7 Sound Blaster, 6 floppy, 12 PS/2 mouse, 13 FPU, 14 and 15 IDE), or with that `vector` (`source`: `hardware`), or read the handler address with `get_i386_descriptors` (`table: idt`) and set an execution breakpoint there
3. `debug_continue` to run until the IRQ fires
4. `get_i386_status` + `get_disassembly` to see the handler code
5. `get_call_stack` to see how deep you are
6. `add_symbol` to label the handler address and any subroutines it calls

### Following INT 21h and BIOS Calls

1. `set_breakpoint_on_interrupt` with `vector: 33` (21h) and `source: software`, or `vector: 16` (10h) for video, 19 (13h) for disks, 22 (16h) for the keyboard
2. `debug_continue`; `debug_get_status` names the `function` ("Open file", "Allocate memory"...) of the call that stopped
3. Read the parameters from `get_i386_status` (AH is the function, DS:DX often a file name) and `read_memory` on `DS:DX`
4. `debug_step_out` or `get_call_stack` (`return` is where the game resumes) to see the result in AX and the carry flag
5. To see every call instead of stopping on each, trace with `cpu.software_ints`
6. A game that hooks INT 21h itself shows up with a write breakpoint on 84h-87h (the vector in the IVT); `get_i386_descriptors` with `table: idt` decodes the IVT in real mode

### DOS Extenders and Protected Mode

DOS runs in real mode, and a game built for DOS/4GW, DJGPP (CWSDPMI), PMODE/W or another extender starts as an ordinary `.EXE` whose first code is the extender, then switches to 32-bit protected mode with selectors and often paging.

1. `get_i386_status` — check `mode` (real, protected, vm86), CPL, CR0.PE and CR0.PG. Under EMM386 or JEMMEX, and under an extender that found a VCPI server, real-mode DOS code runs in `vm86`
2. `get_i386_descriptors` (`table: gdt`) — decode the selectors the extender loaded; `get_page_directory` if paging is on
3. `translate_address` on an `0008:00001234` style address to get the linear and physical address
4. `get_disassembly` with `code_size: 32` (or `auto`) for 32-bit code
5. To catch the switch into protected mode, step until CR0 bit 0 becomes set (a `MOV CR0` or an `LMSW`), then read the GDT. In protected mode the IVT no longer answers: `get_i386_descriptors` with `table: idt` shows the IDT
6. Extenders reach DOS through INT 21h and INT 31h (DPMI): `set_breakpoint_on_interrupt` on 49 (31h) shows the descriptor, memory and real-mode call services the game asks for
7. For your own DJGPP or Watcom builds, `load_symbols` with a map or `nm` output in `SSSS:OOOOOOOO` or linear form names the code, see also the program tools below

### EMS, XMS and Upper Memory

1. `get_game_info` shows the DOS memory mode the game runs under (`dos.memory`: `none`, `xms`, `umb`, `ems`) and the CONFIG.SYS it got
2. XMS: set an execution breakpoint on the driver entry (the far address `INT 2Fh AX=4310h` returns) and read the function in AH; the `system.a20` trace (or an I/O breakpoint on port 92h) shows who turns A20 on for the HMA
3. EMS: the page frame is four 16 KB windows (at DC00h on VGA and Cirrus cards); breakpoint on `INT 67h` (vector 103) for the EMM API. `list_memory_areas` shows Upper Memory and the shadow ROM regions
4. `read_memory` on the MCB chain (16 bytes at `SEG-1:0000`, step by size + 1 paragraphs until the type byte is 5Ah) shows who owns conventional memory; `get_dos_status` reads it for you when the program tools are in use

### Sound Blaster Detection and Digital Audio

1. `get_sound_status` — the card, its IRQ, DMA and port, and whether a DSP transfer is running
2. The `BLASTER` environment variable (`A220 I5 D1 H5 P330` on the built-in disks) is what most games read. Check it with `get_game_info` and the memory at the environment segment (the PSP's word at offset 2Ch)
3. Set an I/O breakpoint (`space: io`) on the DSP ports 226h (reset) and 22Ch (write) to find the detection routine; 388h and 389h are the OPL address and data ports
4. `set_trace_log` with `filters: ["io.writes", "pcm", "dma", "fm"]` to follow the DSP commands, the 8237 setup and the register writes
5. `get_dma_status` for the DSP's channel, address and count; `get_pic_status` for the IRQ
6. `get_opl_status` / `get_opl_registers` — see which FM channels play and their notes; `set_audio_mute` to isolate FM, digital audio, CD audio or the PC speaker
7. `get_sound_status` also reports the MPU-401 and MIDI out: messages sent and the SysEx log show what a game sends to a General MIDI or MT-32 synth

### VESA Mode Sets and Graphics Corruption

1. `debug_pause` → `get_video_output_status` and `get_vga_registers` — check the video card, the mode, chain-4, odd/even and, on a Cirrus card, the extended mode, pixel format and aperture
2. `get_crtc_status` and `get_crtc_registers` — verify timing, offset and the start address
3. `get_palettes` — verify the DAC contents
4. `get_screenshot` and `get_frame_buffer` (`planar`, `256`, `linear`) — compare what is on screen with what is in video memory, including off-screen areas used for page flips
5. For a VESA mode set, break on `INT 10h` (vector 16, `source: software`) and read AX (4F02h is the mode set) and BX (the mode number, bit 14 for the linear frame buffer); `get_disassembly` with `detailed` names the function
6. Set write breakpoints on VGA memory (use `list_memory_areas` for the planes, the window and the Cirrus "Linear VRAM") to catch the corruption source
7. Pick the video card in the machine settings or with `--video` (`cga`, `ega`, `vga`, `gd5428`, `gd5428_vlb`, `gd5446`) when a game depends on one; see [references/pc_video.md](references/pc_video.md)

### Analyzing a Subroutine

1. `set_breakpoint` at the subroutine entry point
2. `debug_continue` → when hit, `get_i386_status`
3. Step through with `debug_step_into` / `debug_step_over`
4. After each step: check registers, read relevant memory
5. `add_symbol` for the routine and any called subroutines
6. `add_disassembler_bookmark` to mark interesting locations

### Tracking a Variable

1. `add_memory_watch` on the variable's address (8, 16, 32 or 64 bits)
2. Set a write breakpoint with `set_breakpoint` (type: write) on that address
3. When hit, `get_disassembly` around the previous instructions reveals what code is modifying it
4. `get_call_stack` shows the call chain leading to the write

### Timing Analysis

1. `set_trace_log` with `enabled: true` and `filters` such as `cpu`, `video.vsync`, `timer`, `io` to start recording
2. Let the game run through the section of interest
3. `get_trace_log` to see the interleaved CPU + hardware events
4. Check `get_pit_status` for the timer reload (a game that reprograms counter 0 sets its own tick rate) and `get_crtc_status` for VSYNC timing
5. Correlate IRQs and I/O accesses with code execution in the trace; use `set_profiler` for per-function cycle costs
6. A game that runs too fast or too slow: `set_turbo`, or a slower model (`--model 386dx_33`) for the next run, and see [references/pentium_pci.md](references/pentium_pci.md) for the Pentium timing

### CD-ROM Game Debugging

1. `load_media` with a `.cue`, `.chd` or `.iso` file, or a `.gdox` with a `cdrom` entry, to load a CD image
2. `list_cdrom_tracks` to see the TOC and `get_cdrom_status` to verify the drive state
3. `set_trace_log` with `filters: ["cpu", "cdrom", "ide"]` to trace the ATAPI packets (READ, READ TOC, PLAY AUDIO) the CD driver sends
4. `get_ide_status` to inspect the packet in progress; the CD driver (SHSUCDX/MSCDEX on top of UDVD2) shows as INT 2Fh calls with `cpu.software_ints`
5. Set breakpoints on the destination RAM (write type) to catch data loading, and `read_cdrom_sector` to compare against the disc contents
6. `get_cdrom_status` (`audio`) to inspect CD-DA playback, and `get_sound_status` for the mixer's CD level

### Floppy Boot Debugging

1. `insert_floppy` with the disk image in `drive` 0 (use `set_floppy_write_protect` to keep the image untouched, or `create_blank_floppy` for a scratch disk)
2. `list_floppy_drives` and `read_floppy_sector` without `sector` to confirm the image geometry and sector IDs
3. `debug_reset` with `boot: floppy1` and `set_trace_log` with `filters: ["cpu", "fdc", "dma"]` to trace controller commands and IRQ 6
4. `get_fdc_status` to inspect the 82077 command and registers while paused, and `get_dma_status` for channel 2
5. `read_floppy_sector` to compare what the game read with the image contents
6. `eject_floppy` or `swap_floppies` to change disks, as the game would ask for

### Programs Run Through GDXDBG

When you build and run your own DOS program from a game folder, the program tools do the loading and reporting: `run_program` (or `load_media` with `target: debug`) boots the game's disk and runs a program through GDXDBG, `get_program_status` reports where DOS loaded it and its exit code, `get_program_output` reads its console output, `get_dos_status` reads DOS's memory chain, and `relaunch_game` rebuilds the disk after a host rebuild while keeping your breakpoints. `load_symbols` with `program: true` rebases a map file to where DOS loaded the program. The geardox-dosdev skill covers that workflow.

### Keyboard, Mouse and Joystick Input

- `keyboard_key` presses, releases or taps a key of the PC keyboard by name (RETURN, SPACE, A, 1, F1, LEFT SHIFT, LEFT CTRL, ESCAPE, UP, KP ENTER...)
- `keyboard_type` types ASCII text on the US layout, useful at DOS prompts (`DIR`, then a newline as RETURN); `frames_per_key` slows it down for software that drops keys
- `controller_macro` runs a frame-timed script of `tap`, `press`, `release` and `wait` over keyboard keys, mouse buttons and joystick controls, for input a game needs on a schedule
- `host_key` presses a host key (USB names: `a`, `up`, `kp_8`, `left_ctrl`...) through the game's key map, as the host keyboard would; `keyboard_key` and `keyboard_type` skip the map
- `get_key_map` shows the per-game bindings and the PC keys no host key reaches; `set_key_binding` changes one (`none`, `default`) and `set_key_mapping` turns the game's bindings off
- `open_key_mapper` and `close_key_mapper` show and hide the in-game keyboard mapper; the machine stops while it's open
- `joystick_set` drives the game port joystick: axes `ax`, `ay`, `bx`, `by`, `buttons`, `hat`, `action` (`press`, `release`, `press_and_release`), and `device` (`none`, `standard`, `gamepad`, `flightstick`) to plug in another one; `reset` gives the host's input back
- `mouse_click` with `x`, `y` clicks at a screen pixel from `get_screenshot` (`double_click`, `right` button, `press_frames`/`release_frames`); `drag` with `to_x`, `to_y` drags in one call. `mouse_move` places the pointer, by pixel or by `dx`, `dy` counts, and `mouse_get_cursor` reads where it was last measured. They need a mouse in the machine (PS/2 or serial) and a driver that reads it
- `get_input_state` shows the mouse and joystick state, including the game port byte at 0201 and each axis pulse
- `get_keyboard_status` shows what the 8042 holds and the keys still held; `get_serial_status` shows a serial mouse

---

## Memory Areas

Use `list_memory_areas` to get the full list with IDs, sizes and the `physical_base` of mapped regions. Common areas:

| Area | Description | Typical Size |
|---|---|---|
| LINEAR | 4 GB linear space; also accepts `SR:offset` and `selector:offset` addresses | 4GB |
| PHYSICAL | 4 GB physical space (after paging) | 4GB |
| I/O PORTS | I/O port space, read without side effects (read-only); `write_io_port` writes a port | 64KB |
| Main RAM | System RAM at physical 0 (conventional memory, extended memory) | 1-128MB |
| System BIOS / BIOS Copy | SeaBIOS image at the top of the address space and its copy below 1 MB | 64-256KB |
| CMOS RAM | MC146818 battery backed RAM | 114 bytes |
| VGA Plane 0-3 | The four VGA planes | 256KB |
| VGA Window / CGA Memory | The CPU's window onto video memory at A0000h-BFFFFh | Varies |
| Linear VRAM | Cirrus linear aperture (only when it is on) | 1-2MB |
| Shadow ROMs / Shadow BIOS | Shadow RAM over the option ROM and BIOS areas | Varies |
| Upper Memory | UMB area between the video BIOS and the BIOS copy | Varies |
| Media Image | Loaded CD image | Varies |
| Floppy N Image | Inserted floppy images | Varies |
| IDE Disk N Buffer | The sector buffer of an IDE disk | Varies |

Offsets in region areas are 0-based. ROMs, media images and I/O PORTS cannot be written. Real-mode data lives low: the IVT is 1024 bytes at 0, the BIOS data area is at `0040:0000` and the text screen is at `B800:0000`. See [references/dos_bios_services.md](references/dos_bios_services.md) for the layout of low memory and [references/pc_at_board.md](references/pc_at_board.md) for the physical map.

---

## Organizing Your Debug Session

- **Symbols**: Use `add_symbol` liberally to label addresses you've identified — makes disassembly readable. Use `list_symbols` (with `filter`, an exact `name` or an `address`) to review
- **Bookmarks**: Use `add_disassembler_bookmark` for code locations and `add_memory_bookmark` for data regions
- **Watches**: Use `add_memory_watch` for variables you're tracking across steps (8, 16, 32 or 64 bits)
- **Save states**: Use `save_state` / `load_state` to snapshot and restore emulator state at interesting points; a load puts the saved frame back on screen (`frame_restored`) and undoes disk writes made since the state
- **Rewind**: Use `get_rewind_status` + `rewind_seek` to scrub back through recent execution history without manual save states
- **Screenshots**: Capture visual state with `get_screenshot` after significant changes, or read `get_text_screen` for a DOS text screen
- **Recording**: `start_video_recording` and `stop_video_recording` capture video and audio of a run to an AVI file

---

## Rewind (Time Travel Debugging)

The emulator continuously records snapshots into a ring buffer during gameplay. You can seek to any recorded snapshot to restore full emulator state at that point in time — like time travel debugging.

### Workflow

1. **Check availability**: `get_rewind_status` — returns snapshot count, capacity, buffered seconds
2. **Pause**: `debug_pause` — the emulator must be paused before seeking
3. **Seek**: `rewind_seek` with a snapshot number (1 = oldest, snapshot_count = newest)
4. **Inspect**: `get_i386_status`, `get_disassembly`, `get_screenshot`, `read_memory`, etc.
5. **Iterate**: Seek to different snapshots to narrow down when a bug first appeared
6. **Resume or continue debugging**: `debug_continue` to resume from the seeked state

### Tools

| Tool | Description |
|---|---|
| `get_rewind_status` | Snapshot count, capacity, buffered seconds, configuration |
| `rewind_seek` | Jump to snapshot N (1=oldest, count=newest). Non-destructive — can seek repeatedly |

### Key Details

- **Non-destructive seeking**: `rewind_seek` loads a snapshot without removing it. You can seek to the same snapshot multiple times, or jump between different snapshots freely.
- **Snapshot numbering**: Snapshot 1 is the oldest available, snapshot_count is the newest (most recent).
- **Buffer size**: Configured by the user. When full, oldest snapshots are overwritten.
- **Granularity**: Snapshots are taken every N frames (configurable).
- **Disks**: Snapshots hold no disk data. Loading a state undoes the disk writes made since it was saved, up to a limit.

### Bug Reproduction with Rewind

1. Let the game run past the bug occurrence
2. `debug_pause` → `get_rewind_status` to see how far back you can go
3. Binary search with `rewind_seek`: try the midpoint, check if the bug is visible (`get_screenshot` or `get_text_screen`), then narrow the range
4. Once you find the exact snapshot where the bug appears, inspect CPU/memory state
5. Set breakpoints at the relevant code, then `rewind_seek` to a snapshot just before the bug and `debug_continue`
