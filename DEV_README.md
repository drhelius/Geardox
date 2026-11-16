# Geardox for MS-DOS Development

Write a DOS program on your computer, build it with a modern toolchain, and run it in Geardox the moment it builds.
Geardox tells you what the program printed and how it exited, and lets you debug it in VS Code, in gdb, in Geardox's
own debugger, or with an AI agent.

1. [Set up, once](#1-set-up-once)
2. [Your first program](#2-your-first-program)
3. [Tutorial: VS Code](#3-tutorial-vs-code)
4. [How a project works](#4-how-a-project-works)
5. [Printing from your program](#5-printing-from-your-program)
6. [Tests and scripts](#6-tests-and-scripts)
7. [Geardox's debugger](#7-geardoxs-debugger)
8. [gdb in a terminal](#8-gdb-in-a-terminal)
9. [AI agents](#9-ai-agents)
10. [Command line reference](#10-command-line-reference)
11. [Limits](#11-limits)

## 1. Set Up, Once

You need Geardox, a DOS toolchain and, to debug at source level, gdb. The commands are for macOS; Linux and Windows
have the same tools in their package managers.

1. **Geardox.** Build it from this repository (see [README.md](README.md)) or install a release, and note where the
   `geardox` program is. The examples call `geardox`, so either put it on your `PATH` or give its full path where
   asked below.
2. **A toolchain.** Pick one to start with:
   - **DJGPP** (C and C++, 32-bit): unpack a [release](https://github.com/andrewwutw/build-djgpp/releases) and add its
     `bin` folder to `PATH`. DJGPP programs also need `CWSDPMI.EXE` from `csdpmi7b.zip` (DJGPP's `v2misc` folder):
     put it in your project folder and the Makefile copies it next to the program.
   - **NASM** (assembly): `brew install nasm i686-elf-binutils`.
3. **gdb:** `brew install gdb`. On Linux and Windows, install `gdb-multiarch`.
4. **VS Code** with the **C/C++** extension (`ms-vscode.cpptools`), if you want to debug in VS Code.

## 2. Your First Program

```sh
dosdev/new_project.sh djgpp ~/dos/hello     # or: new_project.sh nasm ~/dos/hello
cd ~/dos/hello
make            # builds build/hello.exe
make run        # Geardox opens, boots DOS, runs the program and closes when it ends
make test       # the same without a window: the result is the program's exit code
```

`make run` shows a Geardox window with DOS booting and then your program. What the program prints also appears in the
terminal. When the program ends, Geardox closes. `make test` does the same without a window, in about a second.

If `geardox` isn't on your `PATH`, pass it: `make run GEARDOX=/path/to/geardox`.

## 3. Tutorial: VS Code

This walks through building, running, testing and debugging the DJGPP example in VS Code. The NASM example works the
same way; the differences are listed at the end.

### Step 1: Open the project

```sh
dosdev/new_project.sh djgpp ~/dos/hello
code ~/dos/hello
```

The project has:

| File | What it is |
|------|------------|
| `src/hello.c` | The program |
| `include/gdxdbg.h`, `include/gdxdbg.c` | Geardox helpers: printing to the debugger, and the DJGPP base report gdb needs |
| `hello.gdox` | Tells Geardox how to run the project (see [How a project works](#4-how-a-project-works)) |
| `Makefile` | `make`, `make run`, `make test`, `make clean` |
| `.vscode/` | Tasks, debugger launch settings, paths, and the MCP server for Copilot |

### Step 2: Tell VS Code where Geardox and gdb are

Open `.vscode/settings.json` and set the two paths. Plain names work when the programs are on your `PATH`:

```json
{
    "geardox.path": "/Users/you/geardox/platforms/macos/geardox",
    "geardox.gdb": "/opt/homebrew/bin/gdb"
}
```

### Step 3: Build

Press **⇧⌘B** (**Ctrl+Shift+B** on Linux and Windows). That runs the **Build** task, `make`. Compiler errors appear in
the **Problems** panel; click one to jump to the line.

### Step 4: Run

Open the Command Palette (**⇧⌘P**), choose **Tasks: Run Task**, then **Run**. A Geardox window opens, DOS boots, and the
program runs. Its output shows in VS Code's terminal panel:

```
total 140
Hello from DJGPP, the squares add up to 140
```

Geardox closes by itself when the program ends.

### Step 5: Test

Choose **Tasks: Run Test Task** from the Command Palette. It runs the program without a window. The task succeeds when
the program returns 0, and fails otherwise. The example returns 1 if its sum is wrong.

### Step 6: Debug

1. Open `src/hello.c`.
2. Click left of line 8 (`return value * value;`) to set a breakpoint. A red dot appears.
3. Press **F5**. VS Code builds the program, starts Geardox in the background and waits. The Geardox window boots DOS
   and loads the program, then VS Code stops at your breakpoint with the line highlighted.
4. Look around:
   - **Variables** shows `value`. Hover over a name in the editor to see its value.
   - **Watch**: add `squares` or `squares[2]`.
   - **Call Stack** shows `square` called from `main`.
5. Step through the code:

   | Key | Action |
   |-----|--------|
   | **F10** | Step over: run the next line |
   | **F11** | Step into a function |
   | **⇧F11** | Step out of the function |
   | **F5** | Continue to the next breakpoint |
   | **⇧F5** | Stop debugging |

6. Remove the breakpoint and press **F5** again. The program runs to its end. Its output appears in the **Debug
   Console**, the session ends with its exit code, and Geardox closes.

The **Debug Console** also takes gdb commands after `-exec`. These ask Geardox itself:

```
-exec monitor program       where DOS loaded the program, its base and its exit code
-exec monitor screen        what the DOS screen shows, as text
-exec monitor output        everything the program printed
```

### Step 7: Change, rebuild, debug again

Edit `src/hello.c`, then press **F5**. VS Code rebuilds, Geardox picks up the new program, and you debug the new
version. Your breakpoints stay where you put them.

### The NASM example

`new_project.sh nasm` gives the same tasks and the same keys. You set breakpoints on lines of `src/hello.asm`, and
**F10** steps one source line. Registers are under **Variables › Registers**, and you can watch `$ax` or `$cx`. The
debugger reads `build/hello.elf`, which the Makefile builds alongside `build/hello.com` and which holds the line
information.

### If something goes wrong

| What you see | What to do |
|--------------|------------|
| "Unable to start debugging" mentioning gdb | `geardox.gdb` in `.vscode/settings.json` doesn't point to gdb |
| "file format is ambiguous" or no source lines | The gdb in use lacks DJGPP's COFF format: use Homebrew's `gdb` or `gdb-multiarch`, not `x86_64-elf-gdb` |
| VS Code waits and never stops | Look at the Geardox window: DOS may show an error. Check `folder` and `exe` in `hello.gdox` |
| "Load error: no DPMI" in the Geardox window | `CWSDPMI.EXE` is missing: put it in the project folder and build again |
| The port is in use | Another Geardox is serving gdb: close it, or change `--gdb-port` in the **Geardox: gdb** task and `miDebuggerServerAddress` in `.vscode/launch.json` |
| Geardox stays open after **⇧F5** | Stopping the debugger leaves the program running; close the window, or let the program finish |

## 4. How a Project Works

A project is a folder with a **game descriptor**, a `.gdox` file ([GDOX_README.md](GDOX_README.md) has every key):

```ini
[geardox]
version = 1

[game]
name = Hello DJGPP
folder = C:\HELLODJ

[run]
exe = BUILD\HELLO.EXE
```

- **The disk.** Geardox turns the whole folder into the C: drive of a PC with DOS. `folder` is where it lands, here
  `C:\HELLODJ`. `exe` is the program, relative to that folder. Names become 8.3 and upper case, so `build/hello.exe`
  is `BUILD\HELLO.EXE`.
- **Rebuilds.** The disk is rebuilt whenever a file in the folder changes, so each run sees your latest build.
- **The program's own files.** Files the program writes come back to the folder when Geardox closes.
- **GDXDBG.** Geardox runs the program through **GDXDBG.COM**, a small loader on its DOS disks. GDXDBG tells
  Geardox where DOS loaded the program, copies what it prints, and reports its exit code.

## 5. Printing From Your Program

Everything your program prints with DOS (`printf`, `puts`, DOS's character and string calls) goes to the **program
console**. You see it in the terminal, in VS Code's Debug Console, in **Debug → Program → Show Program**, and through
MCP. Programs that draw on the screen directly don't reach it.

To print to the console without touching the screen, for example from a game's main loop, use the helpers:

```c
#include "gdxdbg.h"

gdx_printf("frame %d, player at %d,%d\n", frame, x, y);
```

In NASM, `%include "gdxdbg.inc"` and use `GDX_WRITE text, length`. The helpers do nothing when the program runs outside
Geardox. `gdxdbg.h`, `gdxdbg.c` and `gdxdbg.inc` are under the MIT license, so they can go into any program.

Port E9h also prints, one character at a time, when **Debug → Program → E9h Console** is on or with `--e9`.

## 6. Tests and Scripts

```sh
geardox --headless --run "BUILD\TEST.EXE -v" --exit-on-exit --timeout 60 --no-write-back project.gdox
echo $?
```

- **The exit code** is the program's. 124 means the time ran out, and 255 means the program couldn't be loaded.
- **stdout** holds only what the program printed. Geardox's own messages go to stderr.
- **Settings:** a headless run leaves Geardox's settings untouched.
- **Speed:** a headless run goes at full host speed.
- **`--run`** picks the program and its arguments, relative to the project's folder. Without it, `--target debug`
  runs the descriptor's `exe`.
- **`--no-write-back`** keeps the program's files out of the project folder, so several runs can go at once.

## 7. Geardox's Debugger

Turn on **Debug → Enable**. Then:

- **Debug → Program → Show Program** shows the program, where DOS loaded it, its symbols, its exit code and what it
  printed. **Run Again** rebuilds the disk and runs the program again.
- **Symbols load by themselves.** A map file next to the program with the same name (`hello.map`, `.sym` or `.nm`)
  loads when the program starts, moved to where DOS loaded it. That covers NASM, GNU ld, `nm`, Borland TLINK `/m`,
  Microsoft LINK `/MAP` and Watcom `option map` files, so the disassembler, breakpoints and call stack show your
  function names. They go away when the program exits.
- **Stop at the first instruction:** `--stop-at-entry`, or `GDXDBG /S PROGRAM.EXE` at a DOS prompt.
- **Break from your code:**
  - `gdx_break()` stops the debugger.
  - With **Debug → Program → Magic Breakpoint** on (or `--magic-break`), the instruction `XCHG BX,BX` stops it too.
    `GDX_MAGIC_BREAK()` in C and `GDX_BREAK` in NASM write it.

While a breakpoint, a data watch or the trace logger is set, the machine runs one instruction at a time, which is
slower. Without any of them, a program runs at full speed under the debugger.

## 8. gdb in a Terminal

Start Geardox with the gdb server, then connect gdb:

```sh
geardox --target debug --gdb-wait --exit-on-exit project.gdox &
gdb -ex "set gnutarget coff-go32-exe" -ex "file build/hello.exe" -ex "target remote 127.0.0.1:1234"
```

- **`--gdb-wait`** makes gdb wait until the program has started, so it sees your program and not DOS booting.
- **Starting it from the menu:** **Debug → GDB Server** starts the same server in a running Geardox, on port 1234 or
  the one you set.
- **`set gnutarget coff-go32-exe`** is only for DJGPP programs. A NASM project's `build/hello.elf` loads without it.
- **What gdb sees:** your program's own addresses, the ones in its symbol file. `monitor mode linear` switches to
  plain linear addresses, for code outside your program.

| Command | What it does |
|---------|--------------|
| `monitor program` | The program, its base address and its exit code |
| `monitor screen` | The DOS screen as text |
| `monitor output` | Everything the program printed |
| `monitor relaunch` | Rebuild the disk and run the program again, keeping breakpoints |
| `monitor reset` | Reset the PC |
| `monitor mode linear`, `program`, `auto` | The address mode |
| `monitor base ADDRESS`, `auto` | Set the program's base by hand |
| `monitor symbols FILE` | Load symbols into Geardox's debugger |

## 9. AI Agents

Geardox's [MCP server](MCP_README.md) gives an agent the same loop:

1. **Run:** `run_program` rebuilds and runs the program.
2. **Read:** `get_program_status` and `get_program_output` say what happened.
3. **Inspect:** `get_dos_status` reads DOS's memory, and every debugger tool is there for the rest.

The examples are set up for each agent:

| Agent | File |
|-------|------|
| GitHub Copilot in VS Code | `.vscode/mcp.json` |
| Claude Code | `.mcp.json` (`claude-mcp.json` in the repository's examples) |
| Codex | `codex.toml`, to add to `~/.codex/config.toml` |

The **geardox-dosdev** [skill](skills/README.md) teaches an agent the whole loop:

```sh
npx skills add drhelius/geardox --skill geardox-dosdev
```

## 10. Command Line Reference

| Option | What it does |
|--------|--------------|
| `--target debug` | Runs the descriptor's program through GDXDBG |
| `--run "COMMAND"` | Runs COMMAND instead, relative to the project's folder |
| `--exit-on-exit` | Quits when the program exits, with its exit code |
| `--timeout SECONDS` | Quits with exit code 124 if the program is still running |
| `--stop-at-entry` | Stops in the debugger at the program's first instruction |
| `--boot-speed max` | Boots at full host speed, then runs the program at normal speed |
| `--speed max`, `normal` | Full host speed or normal speed throughout; headless runs default to `max` |
| `--no-write-back` | The program's files stay in Geardox's save folder |
| `--e9` | Port E9h prints to the program console |
| `--magic-break` | `XCHG BX,BX` stops in the debugger |
| `--gdb` | Starts the gdb server on 127.0.0.1 |
| `--gdb-port N` | Its port, 1234 by default |
| `--gdb-wait` | Starts the gdb server and holds gdb until the program starts |
| `--headless` | No window; needs `--run`, `--target debug`, `--gdb` or MCP |

Any of them turns the debugger on for that run only.

| Toolchain | Debugging |
|-----------|-----------|
| DJGPP | Source level in gdb and VS Code, with `gdxdbg.c` linked |
| NASM | Source level in gdb and VS Code through an ELF with debug information, as in the example |
| Open Watcom 2 | Map symbols in Geardox's debugger |
| gcc-ia16 | Tiny and small model code in gdb |
| Borland C++, Turbo Pascal, Watcom 10.x inside DOS | Map symbols in Geardox's debugger |

## 11. Limits

- **16-bit medium and large model programs:** gdb has no notion of real mode segments, so they debug instruction
  by instruction in gdb, with their names in Geardox's debugger.
- **Small model data:** a small model program's data segment is read in Geardox's debugger or in gdb's linear mode.
- **LE and LX programs:** programs run by DOS/4GW, PMODE/W or DOS/32A get map symbols in Geardox's debugger, but no
  source level in gdb.
- **Programs started by your program:** GDXDBG follows the program it starts, not the programs that one runs.
- **Programs that draw directly:** their output reaches the screen but not the program console.
