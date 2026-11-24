---
name: geardox-romhacking
description: >-
  Hack, modify, and translate MS-DOS PC games and software using the Geardox
  emulator MCP server. Provides workflows for memory searching, value
  discovery, cheat creation, data modification, text and font finding,
  executable patching, and translation patching. Use when the user wants to
  create cheats, find game values in memory, modify game data, translate a DOS
  game, patch game behavior or an EXE, create game hacks, discover hidden
  content, change graphics or fonts, find ASCII or code page 437 text strings,
  do infinite lives or health hacks, search for score or item counters, or
  reverse engineer data structures in MS-DOS games, including DOS extender
  games (DOS/4GW, DJGPP, PMODE/W) and CD-ROM and floppy games. Also use for
  any game hacking, memory poking, or game modification task involving
  Geardox.
compatibility: >-
  Requires the Geardox MCP server. Direct tool mode is the default. Before
  installing or configuring, call debug_get_status to check if the server is
  already connected. If --mcp-router is enabled, use get_tool_info and
  execute_tool for routed tools.
metadata:
  author: drhelius
  version: "1.0"
---

# MS-DOS Game Hacking with Geardox

## Overview

Hack, modify, and translate MS-DOS PC games using the Geardox emulator as an MCP server. Search memory for game variables, create cheats, find text strings for translation, locate graphics and font data, patch executables, and reverse engineer data structures — all through MCP tool calls. Use save states as checkpoints and fast forward to reach specific game states.

## MCP Server Prerequisite

**IMPORTANT — Check before installing:** Before attempting any installation or configuration, you MUST first verify if the Geardox MCP server is already connected in your current session. In the default mode, call `debug_get_status` directly. If Geardox was intentionally started with `--mcp-router`, call `get_tool_info` with `{"name":"debug_get_status"}`, then call `execute_tool` with `{"name":"debug_get_status","arguments":{}}`. A valid response from either workflow means the server is active and ready.

Only if neither workflow is available or the call fails, you need to help install and configure the Geardox MCP server:

### Installing Geardox

Run the bundled install script (macOS/Linux):

```bash
bash scripts/install.sh
```

The script prints the path of an existing Geardox (the `GEARDOX_BIN` environment variable first, then `geardox` on `PATH`). Otherwise it installs the latest release: via Homebrew on macOS, or from the PPA or the GitHub release on Linux. You can set `INSTALL_DIR` to control where a downloaded binary goes (default: `~/.local/bin`).

You can also get a desktop build from the [downloads](../../README.md#downloads) or
[build it from source](../../README.md#build-instructions). Set `GEARDOX_BIN` to the executable's full path.

### Connecting as MCP Server

Configure your AI client to run Geardox as an MCP server via STDIO transport. For Claude Code:
```bash
claude mcp add --transport stdio --scope user geardox -- /path/to/geardox --mcp-stdio
```
For Codex:
```bash
codex mcp add geardox -- /path/to/geardox --mcp-stdio
```
Replace `/path/to/geardox` with the actual binary path from the install script. Add `--headless` before `--mcp-stdio` on headless machines.

### Firmware and Media

Geardox has SeaBIOS and SeaVGABIOS built in, so there is no firmware to set. Load the game with `load_media`: a `.gdox` descriptor, a game **folder** or **ZIP** (with a descriptor, or one the built-in game database recognises), or a `.exe`, `.com` or `.bat`. The game's files go on the emulated C: and the machine powers on; `get_game_info` shows the descriptor, the machine, the DOS it boots, the disk and where saves go. `target` chooses `game` (default), `setup` or `prompt`. CD images (`.cue`, `.chd`, `.iso`), floppies (`insert_floppy`) and hard disk images (`attach_hard_disk`) load too. `get_media_info` confirms what is loaded.

### Hardware Documentation (References)

MS-DOS PC hardware documentation is available in the [references/](references/) directory. Load them into your context when you need data formats, memory layout, or hardware details.

| Reference | File | Load when... |
|---|---|---|
| BIOS and DOS Services | [references/dos_bios_services.md](references/dos_bios_services.md) | Low memory, BIOS data area, INT 21h, PSP, MCB chain, EXEC layout, XMS, EMS, DPMI |
| Intel 80386 CPU | [references/i386_cpu.md](references/i386_cpu.md) | Registers, modes, segmentation, paging, address forms |
| x87 FPU | [references/x87_fpu.md](references/x87_fpu.md) | Floating point values, FPU state images, precision |
| PC/AT Board | [references/pc_at_board.md](references/pc_at_board.md) | Physical memory map, A20, I/O ports, timers, CMOS |
| Pentium and PCI | [references/pentium_pci.md](references/pentium_pci.md) | Pentium models, CPUID, RDTSC, PCI devices |
| Video | [references/pc_video.md](references/pc_video.md) | VGA planes and modes, palettes, Cirrus linear modes, VBE, text modes |
| Sound | [references/pc_sound.md](references/pc_sound.md) | OPL, Sound Blaster DSP, MPU-401, PC speaker |
| Floppy and DMA | [references/pc_floppy_dma.md](references/pc_floppy_dma.md) | Floppy disk images, sector layout, DMA |
| IDE and CD-ROM | [references/pc_ide_cdrom.md](references/pc_ide_cdrom.md) | Hard disk sectors, CD sector addressing (LBA, MSF), data and audio tracks |
| Input, Serial, Parallel | [references/pc_input_serial_parallel.md](references/pc_input_serial_parallel.md) | Mouse, joystick and serial port behaviour |

---

## Core Technique: Memory Search

Memory search is the primary tool for game hacking. It uses a capture → change → compare cycle to isolate memory addresses holding game values. Search the **Main RAM** area (use `list_memory_areas` to get its ID); offsets in Main RAM are physical addresses starting at 0, which for real-mode games below 1 MB are the same as `segment * 16 + offset`.

### The Search Loop

```
1. memory_search_capture    → snapshot current memory state
2. (change the value in-game using keyboard_key, keyboard_type, controller_macro, joystick_set, fast forward, etc.)
3. memory_search            → compare against snapshot to find changed addresses
4. Repeat 2-3 until only a few candidates remain
5. read_memory / write_memory → verify and modify the found addresses
```

### Search Operators and Types

`memory_search` supports these **operators**: `<`, `>`, `==`, `!=`, `<=`, `>=`

**Compare types** (`compare_type`):
- `previous` — compare current value to last captured snapshot (most common)
- `value` — compare current value to a specific number (`compare_value`)
- `address` — compare current value to value at another address (`compare_value` is the area offset)

**Data types** (`data_type`): `hex`, `signed`, `unsigned`

**Value width**: `memory_search_capture` takes `width` of 8 (default), 16 or 32 bits, matching aligned little-endian values. Real-mode games mostly keep counters in bytes and 16-bit words; DOS extender games use 32-bit integers. Capture with the width you expect. Use `start` and `size` to limit the captured range, for example the game's data segment, and skip the video memory, the BIOS data area and the interrupt vector table, which change constantly.

### Example: Finding the Lives Counter

```
1. memory_search_capture                         → snapshot with 3 lives
2. Lose a life in-game (play, or drive the game with keyboard_key or joystick_set)
3. memory_search (operator: <, compare_type: previous) → values that decreased
4. memory_search_capture                         → snapshot with 2 lives
5. Lose another life
6. memory_search (operator: <, compare_type: previous) → narrow further
7. Or use: memory_search (operator: ==, compare_type: value, compare_value: 1)
   → find addresses holding exactly 1
8. write_memory on the candidate address to set lives to 99
9. get_screenshot to verify the change took effect
```

### Example: Finding a Score Counter

Score values are often stored as multi-byte (16 or 32-bit little-endian on the 80386):

```
1. memory_search_capture (width: 32)                          → snapshot at score 0
2. Score some points in-game
3. memory_search (operator: >, compare_type: previous)        → values that increased
4. memory_search_capture (width: 32)
5. Score more points
6. memory_search (operator: >, compare_type: previous)        → narrow down
7. read_memory on candidates — look for values matching current score
8. write_memory to set a custom score
```

For multi-byte values: the low byte is at address N, high byte at N+1 (the 80386 is little-endian). Many DOS games keep the score as BCD digits, as ASCII digits or as separate digit bytes instead of a binary number, and some store a value shifted or multiplied (health as a percentage times 256, a score divided by 10). If a binary search finds nothing, search for the digits individually, or search for "changed" and "unchanged" instead of exact values.

### Memory Areas and Addresses

Real-mode games use `segment:offset` addresses: the LINEAR area accepts `DS:offset`, `0040:006C` and `SSSS:OOOO` forms in `read_memory`, `write_memory`, `memory_search_capture` and `memory_find`. Games running in protected mode under a DOS extender use selectors and paging, so a linear address may map to a different physical address. Use `translate_address` on a `selector:offset` or linear address from the disassembler to get the physical address and Main RAM offset, and use the LINEAR area for `read_memory` and `write_memory` when you only know the program's address. Memory above 1 MB (extended memory, XMS blocks) is in Main RAM at its physical address.

---

## Fast Forward for Efficiency

Use fast forward to speed through gameplay when you need to trigger in-game changes:

```
toggle_fast_forward (enabled: true, speed: 4 = unlimited)
(play through the game section)
toggle_fast_forward (enabled: false)
```

This is essential when you need to reach specific game states without waiting in real-time, such as long loading and intro sequences. `set_turbo` slows the CPU to a third of its clock for games that run too fast to play by hand.

---

## Save States as Checkpoints

Save states are critical for game hacking — they let you save your position and retry modifications:

```
save_state (slot: 1-5)           → save current state in a slot
(try modifications)
load_state (slot: 1-5)           → revert if something breaks
```

Use different slots for different game states (e.g., slot 1 = start, slot 2 = boss fight, slot 3 = specific level).

`list_save_state_slots` shows all slots with media name, timestamp, and validity. `save_state` and `load_state` with `file_path` instead of `slot` use explicit files, handy for keeping many checkpoints. A load also undoes the disk writes made since the state was saved, so a game's own save files go back too.

### Rewind as an Alternative

The emulator also records continuous snapshots into a rewind ring buffer. Use `get_rewind_status` to check availability, then `rewind_seek` to jump to any recorded point without manual save/load. This is especially useful for quickly reverting after a failed memory write — pause, seek back a few snapshots, and retry.

---

## Finding and Modifying Game Data

### Text and String Discovery

DOS games store text as single bytes: ASCII, or code page 437 for the box-drawing characters and accented letters of the PC character set (é is 82h, ñ is A4h, ü is 81h). Text is often held in data files (`.DAT`, `.TXT`, `.MSG`, an archive such as `.PAK`, `.WAD` or `.GRP`) and loaded into RAM when needed, sometimes compressed. Strings may end with a NUL byte, with a `$` (what INT 21h function 09h prints), with the high bit set on the last character, or be length-prefixed. To find text strings for translation or modification:

1. `get_text_screen` for a text-mode screen, or `get_screenshot` for a graphics screen, and note the visible text
2. Search for the text with `memory_find` and `text` (ASCII, up to 512 bytes, optional `case_sensitive`), or with `hex_bytes` when it contains CP437 bytes or the game stores it in another form. Search Main RAM, and the media image areas for a CD or floppy
3. If nothing matches, the text may be compressed, shifted by a per-character offset, packed in 5 or 6 bits, or drawn from a different table; trace the text routine instead (see the Translation Workflow)
4. Set read breakpoints on suspected text addresses with `set_breakpoint` (type: read) to confirm they're used for rendering
5. `read_memory` around a hit to see the surrounding strings, and `get_text_screen` or `get_screenshot` to correlate displayed text with memory contents

For a game opened from a folder, the text files are on the host: search them there with host tools (a hex editor, `grep -a`, `strings`) and use the emulator to confirm which file and offset the game actually reads, with a read breakpoint on the loaded copy or an INT 21h trace (`cpu.software_ints`). To look at the media itself, `read_cdrom_sector` (by `lba`, `user` mode for the 2048-byte payload), `read_floppy_sector` and `read_hard_disk_sector` return raw sector data as hex; `list_cdrom_tracks` and `read_floppy_sector` without `sector` describe the layout.

### Graphics and Font Data

1. `get_screenshot` to see the screen, and `get_video_output_status` and `get_vga_registers` for the mode (text, 16-color planar, mode 13h 256-color, or a Cirrus linear mode)
2. `get_frame_buffer` with `plane0`-`plane3`, `planar`, `256` or `linear` (`pitch`, `format`, `start`) to view the whole video memory, including off-screen areas where games keep sprites and tiles for page flipping
3. `get_palettes` for the 16 attribute colors and the 256 DAC entries; many games change the palette by writing to ports 3C8h and 3C9h, so an I/O write breakpoint there finds the routine and the palette data
4. `read_memory` on the VGA planes and the CPU's video window (`list_memory_areas`) to analyze pixel data; in mode 13h each byte is a pixel at `A000:0000 + y * 320 + x`
5. Set write breakpoints on video memory to find the code that draws graphics, and read breakpoints on the source data to find where graphics are loaded from. Sprites on the PC are drawn by the CPU, so the blit routine is where to look
6. In text modes the screen is at `B800:0000` as character and attribute byte pairs; `get_text_screen` with `attributes` shows both. Game fonts in graphics modes are bitmaps in the data files or in the game's RAM, commonly 8x8 or 8x16 bytes per glyph; the BIOS font a game copies from INT 10h is in the video BIOS
7. `get_screenshot` before/after modifications to see visual changes

Reference the video doc ([references/pc_video.md](references/pc_video.md)) for plane layout, modes and VBE.

### Data Tables and Structures

1. `debug_pause` → `get_disassembly` around code that loads data
2. Look for MOV/LEA instructions with absolute or indexed addressing (`[SI+BX]`, `[ESI+EAX*4]`, `[table+EBX]`, `ES:[DI]`) — these point to data tables
3. `read_memory` at the target addresses to dump the table contents (use `translate_address` if the address is linear or segmented)
4. `add_memory_bookmark` to mark discovered data regions
5. `add_symbol` to label data table entry points for future reference

---

## Creating Cheats

### Infinite Lives / Health

```
1. Find the address using the search loop (above)
2. Set a write breakpoint: set_breakpoint (type: write) on the address
3. debug_continue → when it hits, get_disassembly to see the decrement code
4. Note the instruction (e.g., DEC BYTE PTR [1234] or MOV [ESI+0C], EAX)
5. Option A: add_memory_watch with freeze: true (a poke every frame, no patching)
6. Option B: Identify the decrement routine for a NOP patch (90 per byte)
```

Write breakpoints stop after the instruction that wrote, so look at the instructions just before the reported PC.

### Freezing and Watching Values

`add_memory_watch` on a discovered address (`size` of 8, 16, 32 or 64 bits) tracks it in the emulator's GUI memory editor. With `freeze: true` it holds the value it reads now and writes it back every frame, like a cheat, until `remove_memory_watch`. Set the value you want first with `write_memory` (lives to 99), then add the frozen watch. `list_memory_watches` shows the values and which are frozen. Watches are useful for verifying cheats work across different game situations.

### Write Breakpoint Technique

The most powerful cheat-finding technique:

1. Find the variable address via memory search
2. `set_breakpoint` (type: write) on that address
3. `debug_continue` — the emulator stops when the game writes to that address
4. `get_i386_status` + `get_disassembly` reveals the exact code modifying the value
5. `get_call_stack` shows what triggered the write
6. You now know exactly where and how the game manages that variable

---

## Translation Workflow

### 1. Identify the Text System

1. `get_text_screen` or `get_screenshot` of a screen with text
2. Find text rendering code by setting write breakpoints on video memory (`B800:0000` in text modes, the VGA window in graphics modes), or breakpoints on INT 21h function 09h and INT 10h teletype calls (`set_breakpoint_on_interrupt`, `source: software`) when the game prints through DOS or the BIOS
3. Trace back to find the character code and the routine that converts it to a glyph or a font table entry
4. `get_frame_buffer` and `read_memory` to inspect the glyph bitmaps the game draws or keeps in RAM
5. `add_symbol` to label the font routine and the text drawing routine

### 2. Find String Data

1. `memory_find` with `text` for ASCII strings, or `hex_bytes` with the CP437 or game-specific encoding of known text
2. Check whether the game loads text from a file: trace the DOS file calls with `set_trace_log` (`filters: ["cpu.software_ints"]`) and read the file name at `DS:DX` of each open (INT 21h function 3Dh), or for CD and floppy games trace the media (`filters: ["cpu", "cdrom", "fdc", "dma"]`) while the text appears
3. Use `read_cdrom_sector`, `read_floppy_sector` or `read_hard_disk_sector` to inspect the source data
4. `add_memory_bookmark` to mark each string location

### 3. Measure Space Constraints

Translations must fit within existing space:

1. `read_memory` to determine how much space each string occupies
2. Check for string terminators (commonly 00h, `$`, FFh, or length-prefixed) and whether there is a pointer table that also needs updating
3. If the translation is longer, look for unused space, abbreviate, or move the text to the end of the file and repoint the table
4. Check whether the text uses a fixed 8-pixel font, a proportional font or a width table, and whether accented letters exist in the game's font; a missing glyph needs a font patch too

### 4. Apply and Test

1. `write_memory` to patch translated strings into memory
2. `get_text_screen` or `get_screenshot` to verify rendering
3. `save_state` before each change so you can `load_state` if it breaks
4. Test all screens that display modified text
5. Make the change permanent in the game's files (see Persisting Changes)

---

## Patching Executables

### Finding the Code

1. Find the routine with a write breakpoint on the variable, or an execute breakpoint on a known entry, and `get_disassembly` around it
2. Note the bytes of the instructions to change: `get_disassembly` shows the bytes and `read_memory` gives the raw hex. A short unique run of bytes is the key to locating the same place in the file
3. Patch live first: `write_memory` the new bytes at the instruction (NOP is 90h; a `JNZ` becomes `JZ` by flipping bit 0 of the opcode, 75h to 74h) and run the game to check the effect before touching any file

### Real-Mode MZ Executables

An `.EXE` starts with `MZ`. The header (size in paragraphs at offset 08h, relocation table at the offset in 18h) is followed by the load module, which DOS loads at the segment after the PSP. The file offset of an address is `header size + (seg * 16 + off) - (PSP segment + 10h) * 16`, with the PSP segment from the program tools (`get_program_status`), from INT 21h function 62h or from BX after it. Two things make memory differ from the file:

- **Relocations**: the segment words listed in the relocation table are patched at load time with the load segment, so a `MOV AX, seg` that reads 1234h in memory is a different number in the file. Search the file for the bytes around the relocated word, not the word, or use the relocation table to find which words are fixed up
- **Packed executables**: `LZEXE`, `PKLITE`, `EXEPACK` and similar compress the program and unpack it at start. The code in memory after the unpacker finishes is the real code, so the live disassembly is the right place to find it, but the bytes on disk are compressed and cannot be patched directly. Unpack the file with a host tool first, patch the unpacked program, and repack it only if the file size matters

A `.COM` program has no header and loads at offset 100h of its segment, so file offset = address offset - 100h.

### DOS Extender Executables

DOS/4GW and similar extenders load a 32-bit program (an `LE` or `LX` image behind an `MZ` stub) into protected-mode memory at their own addresses. Use `get_i386_status` and `get_i386_descriptors` (`table: gdt`) to find the code and data selectors, `translate_address` to find the physical address, and read the object table of the LE/LX header in the file to map an object's offset to a file position (page tables, with the data pages stored in order and possibly compressed). DJGPP programs are COFF with a flat layout, and a stub that loads the program, so the symbols and the layout are simpler. `load_symbols` with a GNU `nm` or map file names the code.

### Packed and Encrypted Code

Where the code is not visible in the file, run the game until the unpacker has finished, then disassemble the live memory: `get_disassembly` reads what is in memory now, and `read_memory` in chunks of up to 65536 bytes dumps it for use with a host disassembler. Execution breakpoints on the entry after the unpacker's last jump find the original entry point.

---

## Memory Map Quick Reference

Use `list_memory_areas` to get the full list with IDs and sizes. Common areas:

| Area | Description | Use |
|---|---|---|
| Main RAM | System RAM at physical 0 (1 to 128 MB) | Game variables, code, data, stack, extended memory |
| LINEAR / PHYSICAL | 4GB address spaces | Read/write by program address; translate with `translate_address` |
| VGA Plane 0-3 / VGA Window | VGA video memory | Pixels, tiles, text screens at B800h |
| Linear VRAM | Cirrus linear aperture (when on) | High color and VESA linear modes |
| CMOS RAM | MC146818 battery backed RAM | System settings, clock |
| Upper Memory / Shadow ROMs | UMB area, option ROM and BIOS shadow | Drivers loaded high, video BIOS |
| System BIOS / BIOS Copy | SeaBIOS (read-only) | BIOS code |
| Media images | Loaded CD image, inserted floppy images, IDE sector buffers | Raw disc and disk data |
| I/O PORTS | Hardware registers (read-only) | Device state without side effects |

Low memory has fixed structures worth knowing: the interrupt vector table at 0000:0000 (1 KB), the BIOS data area at 0040:0000 (the tick counter at 0040:006C, the keyboard buffer at 0040:001E) and the PSP of the running program (environment segment at offset 2Ch). See [references/dos_bios_services.md](references/dos_bios_services.md). Linear addresses seen in protected-mode code do not always equal Main RAM offsets, so verify with `translate_address`.

---

## Bookmarks and Organization

Keep your hacking session organized:

- `add_memory_bookmark` — mark discovered data regions, variable locations, string tables
- `add_memory_watch` — track values that change during gameplay
- `add_symbol` — label addresses in disassembly for readability
- `add_disassembler_bookmark` — mark code routines you've identified

Use `list_memory_bookmarks`, `list_memory_watches`, `list_symbols`, `list_disassembler_bookmarks` to review.

---

## Persisting Changes

Changes made via `write_memory` are applied to the emulator's live memory only — they are **not** written to any file, and ROM and media image areas cannot be written. To make a patch permanent, change the game's files:

- **Game opened from a folder or `.gdox`**: the files live on the host, so a file patch is a host edit followed by `relaunch_game`, which rebuilds the game's disk from its folder and runs the same program again (the disk is rebuilt when the folder has changed). Edit with host tools (a hex editor, a script, `xdelta` or `ips` style patchers) on the file in the folder, then call `relaunch_game`, or `load_media` on the folder again. `get_game_info` shows the source folder, whether the disk was `built`, and `left_out` for files DOS could not reach. Work on a copy of the folder, and keep the original files
- **Game opened from a ZIP**: the ZIP is never changed, and saves go to Geardox's save folder. Extract the ZIP, patch the files, and open the folder
- **Save games**: while the game runs, saved games and setup changes are written to the emulated hard disk, and Geardox copies them back to the game's folder (or to the save folder, for a ZIP, a read-only folder or when **Save Games To** is the save folder) when the machine powers off, another game opens or Geardox quits. `get_game_info` reports `saves_to`, `write_back_pending` and the last write-back with its conflicts. Do the host edit after the game has written back, or a stale write-back can replace the edited file. A save game file can be hacked the same way as any data file: find the value in the file with the host tools after saving, edit it, and load it in the game
- **CD-ROM images**: locate the data inside the disc (`read_cdrom_sector`, `list_cdrom_tracks` help map RAM contents back to sectors) and patch a copy of the image with a binary patch script. A `cdrom` entry that points at a folder in the descriptor is rebuilt from the folder each time it changes, so patching the folder is enough
- **Floppy and hard disk images**: `read_floppy_sector` and `read_hard_disk_sector` map RAM contents back to sectors; patch a copy of the image file. A floppy keeps its changes in a working copy, and `set_floppy_write_protect` stops the game from writing to it. `flush_hard_disks` writes hard disk changes to the image now

Test the patched files with a fresh load rather than a save state, because a state holds the old code and data in memory.
