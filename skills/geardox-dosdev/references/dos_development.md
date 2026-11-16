# MS-DOS Development with Geardox — Program Tracking, Ports and gdb

How Geardox runs, follows and debugs a program you are writing: GDXDBG.COM, the development ports, the program
tools, the magic breakpoint and the GDB remote server. Everything here is off unless **Debug > Enable** is on or a
development session is running (`--run`, `--target debug`, `--gdb`), so games never see it.

---

## 1. The loop

1. Keep the program's sources and build output in a folder with a `.gdox` descriptor whose `folder` is the program's
   DOS folder and whose `exe` is the program, such as `exe = BUILD\HELLO.EXE`.
2. Build on the host.
3. Run it through GDXDBG: `run_program` (MCP), `--run "BUILD\HELLO.EXE args"` or `--target debug` (command line), or
   **Debug > Program > Run Program Again**. Each run rebuilds the game's C: disk from the folder when it changed, boots
   DOS and runs `GDXDBG` from the generated `AUTOEXEC.BAT`.
4. Read what happened: `get_program_status` (state, load address, exit code), `get_program_output` (console text),
   `get_text_screen` (the screen).
5. Debug: `run_program` with `stop_at_entry`, breakpoints at program symbols, stepping, or gdb.

A test needs no MCP at all:

```sh
geardox --headless --run "BUILD\TEST.EXE -v" --exit-on-exit --timeout 60 project.gdox; echo $?
```

Geardox's exit code is the program's, 124 when the timeout passed and 255 when GDXDBG could not load the program.
The program's console text is the only thing on stdout, Geardox's log goes to stderr. A headless run is unthrottled
unless `--speed normal` is given, and it writes no `config.ini`.

---

## 2. GDXDBG.COM

`GDXDBG [/S] [/Q] PROGRAM [ARGS]` sits next to GDXRUN on the built-in DOS disks (`\FREEDOS\BIN` or `C:\GEARDOX`). With no
program it asks Geardox for the run command. `/S` stops at the program's entry, `/Q` leaves the console out.

| Step | What it does | Source |
|------|--------------|--------|
| Load | INT 21h AX=4B01h, load but don't execute, with the EXEC parameter block; the block returns the child's SS:SP at 0Eh and CS:IP at 12h, AH=62h its PSP | RBIL INT 21h AH=4Bh, Table 01590 |
| Return | Child PSP:0Ah, the stored INT 22h termination address, points back into GDXDBG | RBIL Table 01378 |
| Start | Switches to the child's stack, pops the AX value DOS left on it, sets DS=ES=PSP and jumps to CS:IP | RBIL INT 21h AH=4Bh notes |
| Exit | Reads the exit code and termination type with AH=4Dh and exits with the same code | RBIL INT 21h AH=4Dh |
| Console | Hooks INT 29h, the fast console output DOS uses for CON, and copies each character to the program console | FreeDOS kernel `chario.c`, `fast_put_char` |
| DJGPP | A program whose MZ stub is followed by a COFF image (magic 014Ch) is flagged; its stop waits until `gdxdbg.c` reports the base | — |

Programs that write video memory directly don't reach the console; read them with `get_text_screen`.

---

## 3. The development ports

| Port | Width | Read | Write | Enabled |
|------|-------|------|-------|---------|
| 00E9h | 8 | E9h | One character to the program console | **Debug > Program > E9h Console** or `--e9` |
| 0520h | 8 | 47h (`G`), the signature | A hypercall: AL is the command, the other registers its arguments | Debug enabled or a development session |
| 0521h | 8 | The next byte of the run command, 0 at its end | — | as 0520h |

Disabled, they read FFh and ignore writes like any free port. Pointers use DS; offsets are 32-bit in a 32-bit code
segment and 16-bit otherwise.

| AL | Command | Arguments |
|----|---------|-----------|
| 01h | Program loaded | AH flags (bit 0 stop at entry, bit 1 stop when the base is reported, bit 2 DJGPP COFF), BX PSP, ECX entry CS:IP (CS high), EDI initial SS:SP (SS high), DS:SI the full DOS path |
| 02h | Program base | EBX linear base, CX code selector, SI data selector, DI code size (16 or 32) |
| 03h | Program exited | BL exit code, BH termination type |
| 04h | Output | DS:SI text, CX length (up to 4096) |
| 05h | Break | Stops in the debugger after the `OUT` |
| 06h | Run command | Rewinds port 0521h to the first byte of the run command |
| 07h | DOS | EBX list of lists (INT 21h AH=52h), ECX InDOS flag (AH=34h), SI version (AH=30h) |

`gdxdbg.h` and `gdxdbg.c` (C: DJGPP, Open Watcom, gcc-ia16, Borland) and `gdxdbg.inc` (NASM) wrap them:
`gdx_present`, `gdx_write`, `gdx_puts`, `gdx_printf`, `gdx_e9`, `gdx_break`, `gdx_report_base` and
`GDX_MAGIC_BREAK()`; `GDX_WRITE`, `GDX_E9`, `GDX_STOP` and `GDX_BREAK` in NASM. In a DJGPP program `gdxdbg.c` reports
`__djgpp_base_address`, the linear base of the program's data segment, with CS and DS before `main` (DJGPP libc
reference, `__djgpp_nearptr_enable`).

---

## 4. Program state and symbols

`get_program_status` answers `state` (`none`, `running`, `exited`), `path`, `host_path`, `psp`, `entry`, `stack`,
`load_segment` (PSP for a COM, PSP + 10h for an EXE), `base`, `base_reported`, `code_size`, `coff`, `symbol_file`,
`symbols`, `exit_code` and `termination` (`normal`, `ctrl_c`, `critical_error`, `resident`, `not_loaded`).

When the program loads, Geardox looks for `NAME.map`, `NAME.sym` or `NAME.nm` next to it in the game's folder, in any
case, and loads it as program symbols, dropped when the program exits:

| Format | Addresses | Linear address |
|--------|-----------|----------------|
| Borland TLINK `/m`, Microsoft LINK `/MAP`, Watcom WLINK `option map` | `SSSS:OOOO`, relative to the image | (SSSS + load segment) × 16 + OOOO |
| NASM map, GNU ld map, `nm` output, `ADDRESS NAME` | plain | base + address |

`load_symbols` with `program: true` loads another file the same way. The base is the code segment × 16 at load and
the reported base once a DJGPP program sends it, which reloads the symbols.

`get_dos_status` reads DOS without calling it, once GDXDBG has run: the version, the InDOS flag, the MCB chain from
the list of lists' word at -2 (RBIL Table 01627) with each block's owner, size and name (Table 01628), free
conventional memory and the running program's PSP.

---

## 5. The magic breakpoint

With **Debug > Program > Magic Breakpoint** or `--magic-break`, `XCHG BX,BX` (87h DBh, any operand size) stops in the
debugger after it runs, like a breakpoint; `debug_get_status` reports `kind: magic`. The break hypercall reports
`kind: program`. Turning the option on or off drops the compiled code, the check is made when code is compiled, so
with it off nothing changes.

---

## 6. The GDB remote server

`--gdb` (port `--gdb-port N`, 1234 by default) or **Debug > GDB Server** serves the GDB Remote Serial Protocol on
127.0.0.1, one client at a time. `--gdb-wait` holds the first reply until the program stops at its entry (or for a
DJGPP program with `gdxdbg.c`, when it reports its base), for up to 120 seconds.

- **Registers:** the `org.gnu.gdb.i386.core` feature: EAX-EDI, EIP, EFLAGS, CS-GS, ST0-ST7 and the x87 control
  registers. The architecture is `i8086` for 16-bit code and `i386` for 32-bit code.
- **Breakpoints:** `Z0`/`Z1` are Geardox execute breakpoints, never `INT 3` in memory; `Z2`-`Z4` are write, read
  and access breakpoints. Stops report `swbreak`, `hwbreak`, `watch`, `rwatch` or `awatch`.
- **Output:** the program console reaches gdb as `O` packets while the program runs.
- **Exit:** the exit code ends the session with `W`.

**Addresses.** gdb has one flat address space. In `program` mode, the default once a program runs, a gdb address is an
offset from the program's base and EIP is reported as CS base + EIP - base: a COM's offsets from its PSP, a small
EXE's from its code segment, a DJGPP program's from `__djgpp_base_address`, so the symbol file's addresses work as they
are. In `linear` mode addresses are linear and EIP is CS base + EIP. Data in another segment, a small model's DGROUP
for example, is read with the Geardox debugger or in linear mode.

| Monitor command | Effect |
|-----------------|--------|
| `monitor program` | The program, its base and exit code |
| `monitor mode linear\|program\|auto` | The address mode |
| `monitor base ADDRESS\|auto` | The base by hand |
| `monitor relaunch` | Rebuilds the disk and runs the program again, gdb's breakpoints stay |
| `monitor reset` | Resets the machine |
| `monitor screen` | The text screen |
| `monitor output` | The program console |
| `monitor symbols FILE` | Program symbols for the Geardox debugger |

A DJGPP executable is COFF: `set gnutarget coff-go32-exe` before `file`, in a gdb built with every target
(Homebrew's `gdb`, `gdb-multiarch`). A NASM program debugs at source level when assembled to ELF with
`-f elf32 -g -F dwarf`, linked at 100h and converted to the COM with `objcopy -O binary`, keeping the ELF for gdb.

---

## 7. Speed

A breakpoint, data watch or trace makes every frame run one instruction at a time, as with the debugger alone; with
none of them the machine runs at full speed even with gdb attached and continuing.
