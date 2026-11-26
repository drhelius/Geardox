# Geardox

[![GitHub Workflow Status](https://img.shields.io/github/actions/workflow/status/drhelius/Geardox/geardox.yml)](https://github.com/drhelius/Geardox/actions/workflows/geardox.yml)
[![GitHub Releases)](https://img.shields.io/github/v/tag/drhelius/Geardox?label=version)](https://github.com/drhelius/Geardox/releases)
[![commits)](https://img.shields.io/github/commit-activity/t/drhelius/Geardox)](https://github.com/drhelius/Geardox/commits/main)
[![GitHub contributors](https://img.shields.io/github/contributors/drhelius/Geardox)](https://github.com/drhelius/Geardox/graphs/contributors)
[![GitHub Sponsors](https://img.shields.io/github/sponsors/drhelius)](https://github.com/sponsors/drhelius)
[![License](https://img.shields.io/github/license/drhelius/Geardox)](https://github.com/drhelius/Geardox/blob/main/LICENSE)
[![Twitter Follow](https://img.shields.io/twitter/follow/drhelius)](https://x.com/drhelius)

Geardox is a gaming oriented, cross-platform, MS-DOS PC emulator written in C++ that runs on Windows, macOS, Linux, BSD and RetroArch, with an embedded MCP server for AI debugging and development.

It emulates 386, 486 and Pentium PCs accurately, comes with FreeDOS built in, and runs MS-DOS 5.0-6.22, Windows 3.1, and Windows 95 games.

This is an open source project with its ongoing development made possible thanks to the support by these awesome [backers](backers.md). If you find it useful, please consider [sponsoring](https://github.com/sponsors/drhelius).

Don't hesitate to report bugs or ask for new features by [opening an issue](https://github.com/drhelius/Geardox/issues).

## Downloads

<table>
  <thead>
    <tr>
      <th>Platform</th>
      <th>Architecture</th>
      <th>Download Link</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td rowspan="2"><strong>Windows</strong></td>
      <td>Desktop x64</td>
      <td><a href="https://github.com/drhelius/Geardox/releases/download/0.0.2/Geardox-0.0.2-desktop-windows-x64.zip">Geardox-0.0.2-desktop-windows-x64.zip</a></td>
    </tr>
    <tr>
      <td>Desktop ARM64</td>
      <td><a href="https://github.com/drhelius/Geardox/releases/download/0.0.2/Geardox-0.0.2-desktop-windows-arm64.zip">Geardox-0.0.2-desktop-windows-arm64.zip</a></td>
    </tr>
    <tr>
      <td rowspan="3"><strong>macOS</strong></td>
      <td>Homebrew</td>
      <td><code>brew install --cask drhelius/geardome/geardox</code></td>
    </tr>
    <tr>
      <td>Desktop Apple Silicon</td>
      <td><a href="https://github.com/drhelius/Geardox/releases/download/0.0.2/Geardox-0.0.2-desktop-macos-arm64.zip">Geardox-0.0.2-desktop-macos-arm64.zip</a></td>
    </tr>
    <tr>
      <td>Desktop Intel</td>
      <td><a href="https://github.com/drhelius/Geardox/releases/download/0.0.2/Geardox-0.0.2-desktop-macos-intel.zip">Geardox-0.0.2-desktop-macos-intel.zip</a></td>
    </tr>
    <tr>
      <td rowspan="6"><strong>Linux</strong></td>
      <td>Ubuntu PPA</td>
      <td><a href="https://github.com/drhelius/ppa-geardome">drhelius/ppa-geardome</a></td>
    </tr>
    <tr>
      <td>Fedora RPM</td>
      <td><a href="https://github.com/drhelius/rpm-geardome">drhelius/rpm-geardome</a></td>
    </tr>
    <tr>
      <td>Desktop Ubuntu 26.04 x64</td>
      <td><a href="https://github.com/drhelius/Geardox/releases/download/0.0.2/Geardox-0.0.2-desktop-ubuntu26.04-x64.zip">Geardox-0.0.2-desktop-ubuntu26.04-x64.zip</a></td>
    </tr>
    <tr>
      <td>Desktop Ubuntu 26.04 ARM64</td>
      <td><a href="https://github.com/drhelius/Geardox/releases/download/0.0.2/Geardox-0.0.2-desktop-ubuntu26.04-arm64.zip">Geardox-0.0.2-desktop-ubuntu26.04-arm64.zip</a></td>
    </tr>
    <tr>
      <td>Desktop Ubuntu 24.04 x64</td>
      <td><a href="https://github.com/drhelius/Geardox/releases/download/0.0.2/Geardox-0.0.2-desktop-ubuntu24.04-x64.zip">Geardox-0.0.2-desktop-ubuntu24.04-x64.zip</a></td>
    </tr>
    <tr>
      <td>Desktop Ubuntu 24.04 ARM64</td>
      <td><a href="https://github.com/drhelius/Geardox/releases/download/0.0.2/Geardox-0.0.2-desktop-ubuntu24.04-arm64.zip">Geardox-0.0.2-desktop-ubuntu24.04-arm64.zip</a></td>
    </tr>
    <tr>
      <td><strong>BSD</strong></td>
      <td>Desktop x64</td>
      <td><a href="https://github.com/drhelius/Geardox/releases/download/0.0.2/Geardox-0.0.2-desktop-bsd-x64.zip">Geardox-0.0.2-desktop-bsd-x64.zip</a></td>
    </tr>
    <tr>
      <td><strong>MCPB</strong></td>
      <td>All platforms</td>
      <td><a href="MCP_README.md">MCP Readme</a></td>
    </tr>
    <tr>
      <td><strong>RetroArch</strong></td>
      <td>Libretro Linux x64</td>
      <td><a href="https://github.com/drhelius/Geardox/releases/download/0.0.2/Geardox-0.0.2-libretro-linux-x64.zip">Geardox-0.0.2-libretro-linux-x64.zip</a></td>
    </tr>
    <tr>
      <td><strong>Dev Builds</strong></td>
      <td>All platforms</td>
      <td><a href="https://github.com/drhelius/Geardox/actions/workflows/geardox.yml">GitHub Actions</a></td>
    </tr>
  </tbody>
</table>

**Notes:**

- **Windows**: May need [Visual C++ Redistributable](https://go.microsoft.com/fwlink/?LinkId=746572) and [OpenGL Compatibility Pack](https://apps.microsoft.com/detail/9nqpsl29bfff)
- **Homebrew**: If Homebrew asks you to trust the third-party tap, run `brew trust --tap drhelius/geardome`
- **Linux**: May need `libsdl3`

## Features

- Very accurate 386, 486 and Pentium core that passes [test386](https://github.com/barotto/test386.asm) and [SingleStepTests/80386](https://github.com/singlesteptests/80386), with dynarec and 80-bit x87 FPU.
- 386SX-20 and 386DX-33 (ISA), 486SX-33, 486DX2-66 and 486DX4-100 (VL-Bus), and Pentium 75 to 200 (Intel 430FX PCI) machines, with selectable CPU speed, RAM (1-128 MB), FPU and Turbo.
- Built-in SeaBIOS, SeaVGABIOS and FreeDOS: no BIOS or DOS files needed, and a ready-to-play DOS hard disk boots when nothing is attached. Optional import of MS-DOS 5.0-6.22 in any language.
- [Game descriptors](GDOX_README.md) (`gdox`) and a built-in database of over 300 games: open a game folder or ZIP and it runs on the right machine and DOS, with its saves kept.
- Host folders and ZIP files as FAT16 hard disks, read in place and written back.
- IBM CGA (RGB and composite), IBM EGA, VGA, and Cirrus Logic GD5428 (ISA, VL-Bus) and GD5446 (PCI) SVGA with 2 MB, VESA modes up to 1280x1024, BitBLT engine, hardware cursor and memory-mapped registers.
- Windows 3.1, Windows for Workgroups 3.11 and Windows 95 with the Cirrus Logic accelerated display drivers, and DirectDraw games under DirectX.
- 3dfx Voodoo Graphics with 4 MB as a second PCI card for DOS games with 3dfx support.
- PC speaker, AdLib, Sound Blaster 1.5, 2.0, Pro, Pro 2 and 16 (OPL2, dual OPL2, OPL3), Gravis UltraSound, CD audio, and MPU-401 with built-in MT-32 and General MIDI (SoundFont) synths or MIDI out to host synths on Windows, macOS and Linux.
- ATAPI CD-ROM drive (1x-8x) with `cue/bin`, `iso`, `chd`, GOG `ins/gog`, `zip` and `m3u` playlists, and physical discs on Windows, macOS and Linux.
- Two floppy drives (360 KB-2.88 MB) with `img`, `ima`, `vfd`, `flp`, `dsk`, `d77`, `d88`, `hdm`, `xdf`, HxC `mfm`, `zip` and `m3u`, write protection and optional change persistence.
- Up to four IDE hard disks with raw, fixed `vhd` and `chd` images, and blank disk creation.
- Keyboard with DOS layouts, PS/2 and serial mice, game port joysticks (standard, Gravis GamePad, CH FlightStick Pro), and up to two gamepads as keyboard and mouse (mapped per game) or joystick.
- Per-game keyboard mapper, mouse capture, Game Focus and clipboard paste as keystrokes.
- Save states (5 slots), rewind and run-ahead.
- AVI video recording, screenshots and VGM recording (OPL2, OPL3).
- Standalone: Windows, Linux, BSD and macOS.
- Libretro: Windows, Linux, macOS, Raspberry Pi, Android, iOS, tvOS, webOS, PlayStation 3, Nintendo GameCube, Nintendo WiiU, Nintendo Switch, Emscripten, Classic Mini systems (NES, SNES, C64, ...), OpenDingux, RetroFW and QNX.
- Full debugger:
  - Just-in-time disassembler, call stack, debug symbols and automatic labels.
  - Execute, data, I/O and interrupt breakpoints.
  - Memory editor, trace logger and profiler.
  - i386 registers, descriptor tables, paging and x87 FPU.
  - VGA and Cirrus registers, BitBLT engine and hardware cursor, palettes, VRAM, 3dfx Voodoo registers, pipeline and buffers, OPL, Sound Blaster, Gravis UltraSound voices and DRAM, CD-ROM, floppy, IDE, interrupts, timers, DMA, system control, RTC, BIOS log, keyboard, mouse, joystick, serial and parallel ports.
- [MS-DOS development](DEV_README.md): run programs built on the host through GDXDBG, read their output and exit code, debug them with gdb or VS Code, and test them headless.
- MCP server for AI-assisted debugging with Claude Code, Codex and similar.
- *Portable Mode*, [programmable shader chain](platforms/shared/desktop/shaders/README.md), command line and drag & drop loading, and modern game controllers through [gamecontrollerdb.txt](https://github.com/mdqinc/SDL_GameControllerDB) beside the binary.

## Tips

### Basic Usage
- **Getting Started**: [INSTRUCTIONS.md](INSTRUCTIONS.md) covers playing games, the built-in DOS disks, the DOS prompt, Windows 3.11 and Windows 95, disks and discs, machine settings, MIDI, keyboard, mouse, gamepads and where files are kept.
- **Games**: [GDOX_README.md](GDOX_README.md) covers game descriptors, the game database, ZIPs and CDs, saves, per-game settings, key and gamepad mapping, and importing MS-DOS, the Gravis UltraSound software and the 3dfx Voodoo driver.
- **MIDI Music**: The built-in synths need your own MT-32 ROMs and a SoundFont sound bank, set in **Audio → Built-in Synths**. See [MIDI music](INSTRUCTIONS.md#midi-music).
- **Portable Mode**: Run with `--portable`, or create an empty file named `portable.ini` in the same directory as the application binary. On macOS, place the file next to the `.app` bundle.

### Keyboard
Keys go to the PC. Geardox keeps only its hotkeys, `Ctrl+Shift`, `Ctrl` with an F-key or a digit, and `Alt+Enter` (`Cmd` and `Option` replace `Ctrl` and `Alt` on macOS), set in **Emulator → Hotkeys**. `Ctrl+F11` or fullscreen captures the mouse with every hotkey still working. `Ctrl+F10` enters Game Focus, which locks the mouse and keeps only `Ctrl+F10`, `Alt+Enter`, `Ctrl+F1` (keyboard mapper) and `Ctrl+F2` (key changes on and off). See [Keyboard, mouse and joystick](INSTRUCTIONS.md#keyboard-mouse-and-joystick), and [`[keys]`](GDOX_README.md#keys) for the key names.

The mapper's **This Game** and **All Games** buttons choose where your changes go. It opens on **This Game**, for what you're running: a game keeps them in its `Keys.ini`, and a disc, floppy or hard disk image opened without a game keeps them in a `.keys` file named after it. **All Games** is for fixes to your own keyboard: they apply to everything you run and are kept in `PC.keys`, and a game's own change to the same key wins over them. With nothing loaded, only All Games is available. `Ctrl+F2` turns off the game's changes, never the All Games ones.

### Disks and Discs
[Attaching disks and discs](INSTRUCTIONS.md#attaching-disks-and-discs) explains what each file does and where its changes go. Also:
- **Hard Disk Positions**: `h0`-`h3` images go to Primary Master, Primary Slave, Secondary Master or Secondary Slave. Other images replace the built-in C: or take the first free position. The CD-ROM drive takes one position.
- **Disk Writes**: Saved about 2 seconds after the machine stops writing. A `chd` hard disk gets a raw working copy in the `Save Files` folder at its first write.
- **Save States**: Save states, rewind and run-ahead hold no disk data. Loading one undoes the disk writes made since then (the last 32 MB per disk), and warns when it can't.
- **CD-ROM**: CUE audio tracks can be raw BIN, WAV (44100 Hz, 16-bit, stereo) or Ogg Vorbis. **Drives → CD-ROM** also inserts physical discs and can preload images in RAM.
- **Floppy Drives**: **Emulator → Machine → Floppy A** and **Floppy B** on **Auto** fit the smallest drive for the disk at power on, and a 1.44 MB drive for an empty B:.

### Libretro
- **Content**: A `.gdox`, a game folder or ZIP, a DOS program, a CD, floppy or hard disk image, or an `m3u` playlist mixing them. Disk control swaps floppies and CDs. Hard disk images write to a working copy in the save directory. A game's folder is read in place and its changes go back into it when the content closes, a ZIP's to the game's own folder in the save directory.
- **DOS**: `FreeDOS.hdd` and `MS-DOS.hdd` live in the save directory. For your MS-DOS, put its Setup disk images, or a ZIP of them, in `geardox/msdos` inside the system directory, the Gravis UltraSound install disks in `geardox/ultrasnd`, and the 3dfx Voodoo driver's ZIP, or `GLIDE2X.OVL`, in `geardox/3dfx`.
- **Options**: The machine, drives, game and DOS settings are core options, most applied at restart.
- **Input**: Port 1 is **Keyboard and Mouse** (default) or **PC Joystick**, port 2 **Keyboard and Mouse**. Turn on RetroArch's Game Focus (Scroll Lock by default) for the keyboard and the mapper hotkeys. `.keys` files, and each game's `Keys.ini`, go to the save directory; the All Games changes are `geardox.keys` there.
- **Run-Ahead**: Use RetroArch's single instance mode or preemptive frames. Second instance mode isn't supported: both instances would use the same disks, so the game's files can be damaged.
- **MIDI**: The built-in synths take MT-32 ROMs from `geardox/mt32` (or the system directory itself) and a sound bank from `geardox/soundfonts`. For a synth outside RetroArch, set **MIDI Out** to **External Port** and turn on RetroArch's MIDI output in **Settings → Audio → MIDI**.

### Debugging Features
- **Docking Windows**: In debug mode, hold SHIFT and drag a window onto another.
- **Multi-viewport**: On Windows and macOS, enable it in the Debug menu and restart to drag debugger windows outside the main window.
- **Single Instance**: With it on in the `Emulator` menu, files opened while Geardox runs go to the running instance.
- **Dynarec**: Breakpoints, CPU tracing and the profiler run on the interpreter. **Emulator → Machine → CPU Dynarec** turns the recompiler off.
- **BIOS Messages**: **Debug → System → Show BIOS Log** shows the SeaBIOS log. **BIOS Debug Console** fits port 402h, where the BIOS writes it, and **Log BIOS Messages** copies it to the emulator log.
- **Debug Symbols**: Loaded with a CD-ROM or floppy image from the `.sym` file of the same name (`game.cue` → `game.sym`), or from the GUI or the CLI.
  - Lines are `address name`, `name = address`, `name EQU address` or GNU `nm` output (`address type name`, as in `nm -n program.elf > program.sym`).
  - Addresses are linear or `selector:offset`, and comments start with `;` or `#`.
  - GNU ld and NASM map files load too.
- **MS-DOS Development**: Geardox runs a program you build on the host through GDXDBG, shows where DOS loaded it, what it printed and its exit code in **Debug → Program**, loads its map symbols where DOS put it, and serves gdb from **Debug → GDB Server**. Headless runs work as tests. See [DEV_README.md](DEV_README.md).

### Command Line Usage
```
geardox [options] [media_file] [symbol_file]

Arguments:
  media_file                Game descriptor (.gdox, or a folder or ZIP with
                            one), CD-ROM (.cue .chd .iso), floppy (.img .ima
                            .vfd .flp .dsk .d77 .d88 .hdm .xdf .mfm), .m3u
                            playlist, .zip, or hard disk (.hdd .h0-.h3, .img
                            and .hdm over 8 MB, fixed .vhd, hard disk .chd)
  symbol_file               Symbol file for debugging

Media:
      --cd FILE             CD-ROM image or .m3u playlist
      --fd1 FILE            Floppy image in drive 1
      --fd2 FILE            Floppy image in drive 2
      --load-state SLOT     Load save state slot 1-5, or a state file, once the
                            media is loaded
      --target TARGET       What a game descriptor runs: game, voodoo (its 3dfx
                            program), setup, prompt or debug (its program
                            through GDXDBG)

Machine (for this run only, the saved settings stay):
      --model MODEL         386sx_20, 386dx_33, 486sx_33, 486dx2_66,
                            486dx4_100, pentium_75, pentium_90, pentium_133,
                            pentium_166, pentium_200
      --cpu-mhz MHZ         auto, 16, 20, 25, 33, 40, 50, 66, 75, 90, 100, 133,
                            166, 200
      --ram MB              1, 2, 4, 8, 16, 32, 64, 128
      --fpu FPU             none, 80387
      --sound CARD          none, adlib, sb15, sb20, sbpro, sbpro2, sb16
      --gus STATE           off, on (Gravis UltraSound at 240h)
      --video CARD          auto, cga, ega, vga, gd5428, gd5428_vlb, gd5446
      --mouse MOUSE         none, ps2, serial
      --cdrom DRIVE         none, pm, ps, sm, ss (primary or secondary, master
                            or slave)
      --joystick DEVICE     none, standard, gamepad, flightstick

Development (Debug turns on for this run only):
      --run COMMAND         Runs COMMAND in the game descriptor's folder
                            through GDXDBG, which reports it to the debugger
      --stop-at-entry       Stops in the debugger at the program's first
                            instruction
      --exit-on-exit        Quits when the program exits, with its exit code;
                            its console output goes to stdout
      --timeout SECONDS     Quits with exit code 124 when the program hasn't
                            exited after SECONDS
      --boot-speed SPEED    max runs unthrottled until the program starts,
                            normal (default) doesn't
      --speed SPEED         max runs unthrottled throughout, normal doesn't
                            (default normal, max for headless runs)
      --e9                  Port E9h prints the program's characters to the
                            program console
      --magic-break         XCHG BX,BX stops in the debugger
      --no-write-back       The program's writes stay in the save folder, not
                            the game's folder
      --gdb                 Starts the GDB remote server on 127.0.0.1
      --gdb-port N          Port of the GDB remote server (default: 1234)
      --gdb-wait            Starts the GDB server and holds its first reply
                            until the program starts

Options:
  -f, --fullscreen          Start in fullscreen mode
  -w, --windowed            Start in windowed mode with menu visible
      --mcp-stdio           Auto-start MCP server with stdio transport
      --mcp-http            Auto-start MCP server with HTTP transport
      --mcp-router          Enable compact MCP tool routing
      --mcp-http-address A  HTTP bind address (default: 127.0.0.1)
      --mcp-http-port N     HTTP port for MCP server (default: 7777)
      --headless            Run without GUI (requires MCP, --run or --gdb)
      --portable            Store configuration and user data beside the
                            application
  -v, --version             Display version information
  -h, --help                Display this help message
```

### MCP Server

Geardox includes a [Model Context Protocol](https://modelcontextprotocol.io/introduction) (MCP) server that enables AI-assisted debugging through AI agents like Claude Code, Codex and similar. The server provides tools for execution control, memory inspection, breakpoints, disassembly, hardware status, game descriptors, rewind and more. STDIO and HTTP transports are supported, with STDIO preferred.

For complete setup instructions and tool documentation, see [MCP_README.md](MCP_README.md).

### Agent Skills

Geardox provides [Agent Skills](https://agentskills.io/) that teach AI assistants how to effectively use the emulator for specific tasks:

- **[geardox-debugging](skills/geardox-debugging/SKILL.md)** — Game and program debugging, code tracing, breakpoint management, hardware inspection, and reverse engineering.
- **[geardox-romhacking](skills/geardox-romhacking/SKILL.md)** — Cheat creation, memory searching, data modification, text translation, and game patching.
- **[geardox-dosdev](skills/geardox-dosdev/SKILL.md)** — MS-DOS program development: build, run through GDXDBG, test headless, read output and exit codes, and debug with the MCP debugger or gdb.

Install with `npx skills add drhelius/geardox`. See the [skills README](skills/README.md) for details.

## Build Instructions

### Windows

- Install Microsoft Visual Studio Community 2026 or later.
- Download the latest SDL3 VC development libraries from [SDL3 Releases](https://github.com/libsdl-org/SDL/releases) (the file named `SDL3-devel-x.y.z-VC.zip`).
- Extract the archive and rename the resulting folder (e.g. `SDL3-x.y.z`) to `SDL3`.
- Place the `SDL3` folder inside `platforms/windows/dependencies/` so that the include path is `platforms/windows/dependencies/SDL3/include/SDL3/`.
- Open the Geardox Visual Studio solution `platforms/windows/Geardox.sln` and build.

### macOS

- Install Xcode and run `xcode-select --install` in the terminal for the compiler to be available on the command line.
- Run these commands to generate a Mac *app* bundle:

``` shell
brew install sdl3
cd platforms/macos
make dist
```

### Linux

- Ubuntu / Debian / Raspberry Pi (Raspbian):

If you are using Ubuntu 26.04, you can install SDL3 directly. Use the following commands to build:

``` shell
sudo apt install build-essential pkg-config libsdl3-dev libgl-dev libasound2-dev
cd platforms/linux
make
```

For Ubuntu 24.04, you need to build SDL3 from source first. Use the following commands to build both SDL3 and Geardox:

``` shell
sudo apt install build-essential cmake git curl jq pkg-config libasound2-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev \
  libxi-dev libxss-dev libxkbcommon-dev libwayland-dev libdecor-0-dev \
  libdrm-dev libgbm-dev libgl1-mesa-dev libegl1-mesa-dev libdbus-1-dev libudev-dev libxtst-dev
SDL3_TAG=$(curl -s https://api.github.com/repos/libsdl-org/SDL/releases/latest | jq -r '.tag_name')
git clone --depth 1 --branch "$SDL3_TAG" https://github.com/libsdl-org/SDL.git /tmp/SDL3
cmake -S /tmp/SDL3 -B /tmp/SDL3/build -DCMAKE_INSTALL_PREFIX=/usr -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
cmake --build /tmp/SDL3/build -j$(nproc)
sudo cmake --install /tmp/SDL3/build
cd platforms/linux
make
```

- Fedora:

``` shell
sudo dnf install @development-tools gcc-c++ SDL3-devel alsa-lib-devel
cd platforms/linux
make
```

- Arch Linux:

``` shell
sudo pacman -S base-devel sdl3 alsa-lib
cd platforms/linux
make
```

### BSD

- FreeBSD:

``` shell
su root -c "pkg install -y git gmake pkgconf sdl3"
cd platforms/bsd
USE_CLANG=1 gmake
```

- NetBSD:

``` shell
su root -c "pkgin install gmake pkgconf SDL3"
cd platforms/bsd
gmake
```

- OpenBSD

``` shell
doas pkg_add gmake sdl3
cd platforms/bsd
LDFLAGS=-L/usr/X11R6/lib/ USE_CLANG=1 gmake
```

### Libretro

- Ubuntu / Debian / Raspberry Pi (Raspbian):

``` shell
sudo apt install build-essential
cd platforms/libretro
make
```

- Fedora:

``` shell
sudo dnf install @development-tools gcc-c++
cd platforms/libretro
make
```

## Contributors

Thank you to all the people who have already contributed to Geardox!

[![Contributors](https://contrib.rocks/image?repo=drhelius/geardox)](https://github.com/drhelius/geardox/graphs/contributors)

## License

Geardox is licensed under the GNU General Public License v3.0 License, see [LICENSE](LICENSE) for more information.
