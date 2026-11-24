# Geardox Agent Skills

[Agent Skills](https://agentskills.io/) for the Geardox MS-DOS PC emulator MCP server. These skills teach AI agents how to effectively use Geardox's MCP tools for debugging, game hacking and DOS program development.

## Prerequisites

All skills require the **Geardox emulator** running as an MCP server. The emulator must be configured in your AI client (Claude Code, Codex, etc.) so the agent can access the MCP tools.

Geardox has its BIOS built in, so there are no firmware files to set up. Get a desktop build from the
[downloads](../README.md#downloads), [build it from source](../README.md#build-instructions), or let each skill's
`scripts/install.sh` install it.

See [MCP_README.md](../MCP_README.md) for complete setup instructions (STDIO, HTTP, Claude Code, Codex).

## Installation

The recommended way to install the skills is using the [`skills`](https://skills.sh/docs) CLI, which requires no prior installation:

```bash
npx skills add drhelius/geardox
```

Or install a specific skill:

```bash
npx skills add drhelius/geardox --skill geardox-debugging
npx skills add drhelius/geardox --skill geardox-romhacking
npx skills add drhelius/geardox --skill geardox-dosdev
```

This downloads and configures the skills for use with your AI agent. See the [skills CLI reference](https://skills.sh/docs/cli) for more details.

## Available Skills

### geardox-debugging

**Purpose**: Game development, debugging, and tracing of MS-DOS PC games and software.

**What it covers**:
- Loading games from `.gdox` descriptors, folders and ZIPs, the built-in DOS disks, floppies, CD images and hard disks
- Intel 80386, 486 and Pentium register, flag, mode, and segment descriptor inspection (real, protected, and VM86 modes) with the x87 FPU
- Linear, `SR:offset` and `selector:offset` addressing and translation through segmentation and paging
- Setting execution, read, write, access, I/O port, range, interrupt and IRQ breakpoints
- Stepping through code (into, over, out, frame, run-to)
- Execution tracing with interleaved hardware events (interrupts, DOS and BIOS calls, I/O, DMA, FM, CD-ROM, floppy, IDE, keyboard)
- Hardware inspection: 8259A pair, 8254, 8237 pair, MC146818, 8042, VGA, Cirrus GD5428 and GD5446, CGA and EGA video, OPL2 and OPL3, Sound Blaster DSP and mixers, MPU-401 and MIDI out, PC speaker, ATAPI CD-ROM, 82077 floppy, IDE disks, serial ports
- Reading DOS text screens, decoding video memory and capturing screenshots
- Common scenarios: SeaBIOS boot path, INT 21h and BIOS handlers, DOS extenders (DOS/4GW, DJGPP, PMODE/W), EMS and XMS, Sound Blaster detection, VESA mode sets, CD and floppy games
- Call stack analysis and CPU profiling
- Organizing debug sessions with symbols, bookmarks, and watches

**Key MCP tools used**: `debug_pause`, `debug_step_into`, `debug_step_over`, `debug_step_out`, `set_breakpoint`, `set_breakpoint_on_interrupt`, `get_i386_status`, `get_i386_descriptors`, `translate_address`, `get_disassembly`, `get_call_stack`, `set_trace_log`, `get_trace_log`, `get_text_screen`, `get_vga_registers`, `get_pic_status`, `get_opl_status`, `get_sound_status`, `get_bios_log`, `add_symbol`, `get_screenshot`

**Example prompts**:
- "Find the keyboard interrupt handler and analyze what it does"
- "Break on INT 21h and show me the file name the game opens"
- "Set a breakpoint at CS:2140 and step through the code"
- "The game has corrupted graphics — diagnose the VGA registers and video memory"
- "The game switches to protected mode through its DOS extender — stop after it loads the GDT and decode every descriptor"
- "Why does the game not detect my Sound Blaster? Trace the DSP reset and the BLASTER settings"
- "Debug the CD-ROM loading sequence and trace the ATAPI packets"

### geardox-romhacking

**Purpose**: Creating modifications, cheats, translations, and hacks for MS-DOS PC games.

**What it covers**:
- Memory search workflows (capture → change → compare cycle)
- Finding game variables (lives, health, score, position)
- Creating cheats (infinite lives, frozen watches, score modification, etc.)
- ASCII and code page 437 string discovery for translations, and font and graphics data in video memory
- Executable patching: MZ relocations, packed EXEs (LZEXE, PKLITE), LE/LX objects under DOS extenders, run-time disassembly of packed code
- Data table and structure reverse engineering
- Reading CD, floppy and hard disk sectors to locate data on the media
- Patching files in a game folder and relaunching, and where save games go
- Save state management for safe experimentation
- Fast forwarding to reach specific game states

**Key MCP tools used**: `memory_search_capture`, `memory_search`, `memory_find`, `read_memory`, `write_memory`, `translate_address`, `set_breakpoint` (write type), `add_memory_watch` (freeze), `add_memory_bookmark`, `save_state`, `load_state`, `toggle_fast_forward`, `get_screenshot`, `get_text_screen`, `get_frame_buffer`, `read_cdrom_sector`, `relaunch_game`, `keyboard_key`, `joystick_set`

**Example prompts**:
- "Find the lives counter and give me infinite lives"
- "Search for the score variable in memory"
- "Find all the text strings in this game for translation"
- "Locate the font the game uses to draw its menus"
- "Patch the copy protection check in the EXE and relaunch the game from its folder"
- "Create a cheat for maximum health in this CD-ROM game"

### geardox-dosdev

**Purpose**: Develop, run, test and debug MS-DOS programs with Geardox: build on the host, run through GDXDBG, read output and exit codes, debug with the MCP debugger or gdb.

**What it covers**:
- Project folders with a `.gdox` descriptor, started from `dosdev/new_project.sh`
- Building on the host with DJGPP, NASM, Open Watcom 2 or gcc-ia16, or inside DOS with Borland and Watcom
- Running a program through GDXDBG with `run_program`, and again after a rebuild with `relaunch_game`
- Reading its console output, `gdx_printf` and port E9h text, where DOS loaded it, its exit code and DOS's memory chain
- Headless tests whose exit code is the program's
- Map and symbol files rebased to where DOS loaded the program, stopping at its entry, the magic breakpoint
- Source-level debugging of DJGPP and NASM programs in gdb and VS Code, through the GDB remote server

**Key MCP tools used**: `run_program`, `relaunch_game`, `get_program_status`, `get_program_output`, `get_dos_status`, `load_symbols`, `load_media`, `get_game_info`, `set_breakpoint`, `debug_continue`, `get_i386_status`, `get_text_screen`

**Example prompts**:
- "Build this program, run it in Geardox and tell me its exit code and what it printed"
- "Rebuild the program and run it again, then stop at its first instruction"
- "The program crashes after startup — find the instruction that faults"
- "Run the test program on a 386 and on a Pentium and compare the output"
- "Show me how much conventional memory is free when the program starts"
