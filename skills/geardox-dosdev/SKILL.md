---
name: geardox-dosdev
description: >-
  Develop, run, test and debug MS-DOS programs with the Geardox emulator MCP
  server. Provides workflows for building DOS programs on the host (DJGPP,
  NASM, Open Watcom, gcc-ia16) or inside DOS (Borland, Watcom), running them
  through GDXDBG, reading their console output and exit codes, writing
  headless tests, loading their symbols, stopping at their entry point and
  debugging them with the MCP debugger or gdb. Use when the user writes or
  ports an MS-DOS program, game or demo, wants to run or test DOS code they
  build, asks for printf debugging in DOS, wants source-level debugging of a
  DJGPP or NASM program, or sets up VS Code, Copilot, Claude Code or Codex for
  DOS development with Geardox.
compatibility: >-
  Requires the Geardox MCP server. Direct tool mode is the default. Before
  installing or configuring, call debug_get_status to check if the server is
  already connected. If --mcp-router is enabled, use get_tool_info and
  execute_tool for routed tools. Building needs the program's own toolchain on
  the host.
metadata:
  author: drhelius
  version: "1.0"
---

# MS-DOS Development with Geardox

## Overview

Geardox runs the DOS program you are building the moment you rebuild it. A project folder with a `.gdox` descriptor
becomes the C: drive of a PC that boots DOS and runs the program through GDXDBG, a small loader that tells Geardox
where DOS put the program, copies its console output and reports its exit code. From there the MCP tools give you the
program's state and output, its symbols in the debugger, and every debugger and hardware tool for the moment it
misbehaves. The [references/dos_development.md](references/dos_development.md) page has the full details.

## MCP Server Prerequisite

**IMPORTANT — Check before installing:** Before attempting any installation or configuration, you MUST first verify
if the Geardox MCP server is already connected in your current session. In the default mode, call `debug_get_status`
directly. If Geardox was intentionally started with `--mcp-router`, call `get_tool_info` with
`{"name":"debug_get_status"}`, then call `execute_tool` with `{"name":"debug_get_status","arguments":{}}`. A valid
response from either workflow means the server is active and ready.

Only if neither workflow is available or the call fails, install and configure Geardox:

```bash
bash scripts/install.sh
```

Then add it to your client with the project's descriptor, so it opens the project:

```bash
claude mcp add --transport stdio geardox -- /path/to/geardox --mcp-stdio /path/to/project/project.gdox
codex mcp add geardox -- /path/to/geardox --mcp-stdio /path/to/project/project.gdox
```

For VS Code with Copilot, `.vscode/mcp.json` in the project starts it the same way. Leave `--headless` out so the user
can watch the PC; add it on machines without a display.

## Project Layout

Start from an example: `dosdev/new_project.sh djgpp DIR` or `... nasm DIR` in the Geardox repository copies one
with its Makefile, `.gdox`, VS Code tasks and launch settings, MCP configurations and the `gdxdbg` helpers.

```ini
[geardox]
version = 1

[game]
name = My Program
folder = C:\MYPROG

[run]
exe = BUILD\MYPROG.EXE
```

The whole folder becomes `C:\MYPROG`, in 8.3 names. `exe` is relative to `folder`. The program's writes come back to
the folder when the session ends.

| Toolchain | Where it runs | Debugging |
|-----------|---------------|-----------|
| DJGPP (`i586-pc-msdosdjgpp-gcc`) | Host | Source level in gdb with `gdxdbg.c` linked; needs `CWSDPMI.EXE` next to the program |
| NASM | Host | Source level in gdb when assembled to ELF with DWARF and converted with `objcopy`; map symbols otherwise |
| Open Watcom 2 (`wcl`, `wcl386`) | Host | WLINK map symbols in the Geardox debugger |
| gcc-ia16 | Host | Tiny and small model code in gdb, map or `nm` symbols |
| Borland C++, Turbo Pascal, Watcom 10.x | Inside DOS | TLINK or WLINK map symbols in the Geardox debugger |

## The Loop

1. Build on the host (`make`).
2. Run: `run_program` with the program and its arguments, relative to the game's folder, or with none for the
   descriptor's. It rebuilds the C: disk from the folder, boots and returns at once.
3. Poll `get_program_status` until `state` is `exited` (or `running` for an interactive program), then read
   `exit_code`.
4. Read the output with `get_program_output`; pass its `next` back as `since` to read only new text. Use
   `get_text_screen` for programs that draw on the screen directly, and `get_screenshot` for graphics.
5. Fix, rebuild, and `relaunch_game` to run the same program again.

Interactive programs take keys with `keyboard_type` and `keyboard_key`, and the mouse with `mouse_move` and
`mouse_click`.

## Tests Without MCP

```bash
geardox --headless --run "BUILD\TEST.EXE --fast" --exit-on-exit --timeout 60 --no-write-back project.gdox; echo $?
```

- **Exit code:** the program's; 124 is a timeout and 255 means GDXDBG could not load the program.
- **Output:** stdout holds only the program's console output; Geardox's log goes to stderr.
- **Speed:** headless runs go unthrottled unless `--speed normal` is given.
- **Parallel runs:** `--no-write-back` keeps the program's writes in Geardox's save folder, so runs don't share one.

## Printf Debugging

- `gdx_printf`, `gdx_puts` and `gdx_write` from `gdxdbg.h` write to the program console from any mode: real mode,
  DJGPP and DOS/4GW alike. They don't need the screen.
- With `--e9` or **Debug > Program > E9h Console**, bytes written to port E9h also go there.
- In NASM, use `GDX_WRITE` and `GDX_E9` from `gdxdbg.inc`.

## Debugging

- **Stop at entry:** `run_program` with `stop_at_entry: true`. `debug_get_status` then shows `breakpoint.kind`
  `run_to` at the entry point. A DJGPP program stops when `gdxdbg.c` reports its base, before `main`.
- **Symbols:** `NAME.map`, `NAME.sym` or `NAME.nm` next to the program loads by itself, rebased to where DOS loaded
  it. `get_program_status` shows `symbol_file` and `symbols`, and `list_symbols` finds a function. Load another file
  with `load_symbols` and `program: true`.
- **Breakpoints:** `set_breakpoint` at a symbol's linear address, or at `SR:offset` (`CS:1234`). Data watches use
  `type` `write`, `read` or `access`.
- **Breaks in code:** `GDX_MAGIC_BREAK()` (`XCHG BX,BX`) stops when **Magic Breakpoint** or `--magic-break` is on,
  reported as `kind: magic`. `gdx_break()` always stops, reported as `kind: program`.
- **Inspect:** `get_i386_status`, `get_disassembly`, `get_call_stack`, `read_memory` and `translate_address`, as in
  any debugging session.
- **DOS:** `get_dos_status` shows the MCB chain, free memory and the InDOS flag, for out-of-memory and memory
  corruption problems.

## gdb and VS Code

For source-level debugging start Geardox as the example's **Geardox: gdb** task does:

```bash
geardox --target debug --gdb-wait --gdb-port 1234 --e9 --exit-on-exit project.gdox
```

then connect gdb:

```gdb
set gnutarget coff-go32-exe
file build/myprog.exe
target remote 127.0.0.1:1234
break main
continue
```

- **The gnutarget line:** use it only for DJGPP executables. NASM's ELF loads as it is.
- **When gdb gets an answer:** `--gdb-wait` holds the first answer until the program stops at its entry, so gdb
  sees the program itself.
- **Monitor commands:** `monitor program`, `monitor screen` and `monitor output` show the program, its text screen
  and its console. `monitor relaunch` runs it again with the breakpoints kept.
- **VS Code:** the example's `.vscode/launch.json` uses `cppdbg` with `miDebuggerServerAddress`.
  `.vscode/settings.json` holds `geardox.path` and `geardox.gdb`.

## Common Problems

| Symptom | Check |
|---------|-------|
| `run_program` says no game is open | `load_media` the project's `.gdox` first |
| `state` stays `none` | `get_text_screen`: DOS may show an error before GDXDBG runs, or the descriptor's `folder` doesn't match |
| `termination` is `not_loaded` | The command's path: it is relative to the game's folder and needs no `C:` |
| DJGPP: "Load error: no DPMI" | `CWSDPMI.EXE` must be in the program's folder or on `PATH` |
| No symbols | The map must sit next to the program with the same name; `program` mode in `load_symbols` loads it by hand |
| gdb shows wrong code | The address mode: `monitor program` shows the base; `monitor mode linear` for code outside the program |
| Output missing | Programs that write video memory bypass the console: `get_text_screen` |
