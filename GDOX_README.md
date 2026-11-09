# Geardox Game Descriptors

A game descriptor is a small text file, `name.gdox`, that tells Geardox how to run one DOS game. Opening it builds a
hard disk with the game on C: and FreeDOS set up for it, powers the machine on and starts the game. What the game
saves goes back to its folder when the session ends.

## Quick Start

1. Put a file named `Master of Orion.gdox` in the game's folder:

   ```ini
   [geardox]
   version = 1
   name = Master of Orion

   [drives]
   dir = orion

   [run]
   exe = ORION.EXE
   ```

2. Open it: drop it on Geardox, or use **Game > Open Game...**. Dropping or opening the game's folder works too.

The game boots straight into `C:\ORION\ORION.EXE`. Only `version` and `dir` are required: everything else is optional.

## Opening a Game

| Where | How |
| --- | --- |
| Desktop | **Game > Open Game...** (a `.gdox` or a ZIP), **Game > Open Game Folder...**, drag and drop, **Game > Recent Games**, double click in the OS |
| Command line | `geardox "Master of Orion.gdox"`, or a folder or ZIP holding one. `--target setup` or `--target prompt` runs the setup program or stops at the DOS prompt |
| RetroArch | Load the `.gdox`, or a ZIP with one in its root |

A folder or ZIP with several descriptors in its root holds variants of one game (episodes, for example): the desktop
asks which one, RetroArch and MCP take the first by name. A folder or ZIP with none is attached as a plain hard disk,
as before.

## Where the Descriptor Goes

| Placement | `c` key | C: holds |
| --- | --- | --- |
| In the game's folder | Leave it out | That folder |
| In the root of a ZIP | Leave it out | The ZIP |
| Beside a ZIP, or anywhere else | `c = game.zip`, `c = ../games/orion`, or an absolute path | The folder or ZIP it names |

Relative paths are relative to the descriptor's folder. Read-only collections can keep their descriptors elsewhere,
pointing at the games with `c`. **Game > New Game Descriptor...** writes one for you: pick the game's folder, then the
program that starts it.

## File Format

- INI: `[section]` headers and `key = value` lines. Section and key names ignore case.
- A value is the rest of the line, trimmed. No quotes and no escapes: `args = -nosound ; "C:\X"` is taken as is.
- Comments are whole lines starting with `;` or `#`. There are no comments at the end of a line.
- `[config_sys]` and `[autoexec]` take their lines as they are. Use `REM` for comments there.
- Paths take `/` or `\`. Lists are comma separated.
- A key left out takes Geardox's setting. There is no `auto` value.
- An unknown section or key is a warning and is ignored. A bad value, a key given twice or a missing `version` stops
  the game with the line number. A file made for a newer Geardox says so.
- UTF-8, LF or CRLF, up to 64 KB.

## Keys

### `[geardox]`

| Key | Values | Default |
| --- | --- | --- |
| `version` | `1` | **Required** |
| `name` | The game's title: window title, save state names, volume label | The file's name |

### `[drives]`

| Key | Values | Default |
| --- | --- | --- |
| `dir` | Where the game goes on C:. `.` for the root, `orion` for `C:\ORION`, `games\orion` for `C:\GAMES\ORION`. DOS names, 8.3. Not `FREEDOS` or `TEMP` | **Required** |
| `c` | The game's folder or ZIP | The descriptor's folder, or its ZIP |
| `free` | Free space on C:, MB, 16 to 1024 | `64` |
| `d` | A folder, ZIP or hard disk image for D: (Primary Slave). Its writes stay in its image | None |
| `a`, `b` | Floppy images for A: and B:, comma separated. The first goes in, the rest can be swapped | None |
| `cd` | CD images (`.cue`, `.iso`, `.chd`) or one `.m3u`, comma separated. Adds a CD-ROM drive on the Secondary Master. Images inside the game's ZIP are taken out by themselves | None |
| `cd_speed` | `1x`, `2x`, `4x`, `8x` | Geardox's setting (4x) |

### `[run]`

Paths are from the game's folder (`dir`).

| Key | Values | Default |
| --- | --- | --- |
| `exe` | The program that starts the game: `ORION.EXE`, `GAME\GAME.EXE`, `PLAY.BAT` | None: the DOS prompt |
| `args` | Its arguments | None |
| `cwd` | The directory it runs in | `exe`'s directory |
| `setup` | The game's setup program, with arguments: `SETUP.EXE`, `INSTALL.EXE -S` | None |
| `exit` | `prompt`: back to DOS when the game quits. `poweroff`: the machine powers off and the session ends. **Game > Quit When a Game Powers Off** closes Geardox too | `prompt` |
| `boot` | `c`, or `a` for booter games that start from floppy A: (C: stays the game) | `c` |

### `[machine]`

| Key | Values | Default |
| --- | --- | --- |
| `model` | `386sx_20`, `386dx_33`, `486dx2_66` | Geardox's setting (486DX2-66) |
| `cpu_mhz` | `16`, `20`, `25`, `33`, `40`, `50`, `66`, `100` | The model's clock |
| `turbo` | `on`, `off`. Off runs the CPU at a third of its clock | `on` |
| `ram` | MB: `1`, `2`, `4`, `8`, `16` on the 386s; `4`, `8`, `16`, `32`, `64` on the 486 | 8, or more when `ems` needs it |
| `fpu` | `none`, `80387` (the 486DX2 has one built in) | `80387` |
| `floppy_a`, `floppy_b` | `360k`, `1200k`, `720k`, `1440k`, `2880k` | Fitted to the inserted disk |
| `video` | `vga`, `gd5428` (Cirrus ISA), `gd5428_vlb` (Cirrus VL-Bus, 486 only) | Cirrus, VL-Bus on the 486 |

### `[sound]`

| Key | Values | Default |
| --- | --- | --- |
| `card` | `none`, `adlib`, `sb15`, `sb20`, `sbpro`, `sbpro2`, `sb16` | `sb16` |
| `irq` | `5`, `7` | `5` |
| `dma` | `1`, `3` | `1` |

The port is always 220h, the SB16's high DMA 5 and its MPU-401 330h. `BLASTER` is set from these.

### `[input]`

| Key | Values | Default |
| --- | --- | --- |
| `mouse` | `none`, `ps2`, `serial` (COM1). The mouse driver loads only with a mouse | `ps2` |
| `joystick` | `none`, `joystick`, `gamepad`, `flightstick` | Geardox's setting |

### `[dos]`

| Key | Values | Default |
| --- | --- | --- |
| `memory` | `none`, `xms`, `umb`, `ems` (see [Memory](#memory)) | `xms`, or `ems` when `ems` is set |
| `ems` | The EMS the game needs, KB, 16 to 32768. Turns `memory` to `ems` and raises RAM to fit | None |
| `ems_frame` | `auto`, `none`, or a segment `C000` to `E000` | `auto` |
| `files` | `FILES=`, 8 to 255 | `40` |
| `buffers` | `BUFFERS=`, 1 to 99 | `20` |
| `version` | The DOS version to report, `x.yy`, for games that check it | FreeDOS' |
| `freedos` | `visible` or `hidden`: whether FreeDOS' files in the root of C: show in `DIR` | `visible` |

### `[env]`

Each `NAME = value` becomes `SET NAME=value`, after Geardox's own variables, so it can replace them, `BLASTER` too.

```ini
[env]
DOS4G = QUIET
```

### `[config_sys]` and `[autoexec]`

Extra lines for `FDCONFIG.SYS`, before its `SHELL=` line, and for `FDAUTO.BAT`, run in the game's folder before the
game starts.

```ini
[config_sys]
STACKS=9,128

[autoexec]
LH MYDRIVER.COM
```

## The C: Drive

With `dir = orion` and FreeDOS visible:

```
C:\KERNEL.SYS     FreeDOS' kernel
C:\FDCONFIG.SYS   The CONFIG.SYS, made from the descriptor
C:\FDAUTO.BAT     The AUTOEXEC.BAT, made from the descriptor
C:\FREEDOS\BIN    COMMAND.COM, drivers and tools (EDIT, MEM, XCOPY, CHKDSK, ...)
C:\TEMP           The TEMP directory
C:\ORION          The game
```

- `freedos = hidden` hides the first five, so `DIR C:\` shows only the game. `DIR /A` still shows them.
- `FDCONFIG.SYS` and `FDAUTO.BAT` are made again from the descriptor every time: changes made to them in DOS don't
  last. Put extra lines in `[config_sys]` and `[autoexec]`.
- A game's own `CONFIG.SYS` and `AUTOEXEC.BAT` stay in its folder untouched. FreeDOS reads `FDCONFIG.SYS`.
- With `dir = .`, a game entry in the root named like one of FreeDOS' is left out.
- D: is the `d` drive, then the CD-ROM takes the next free letter.

## Game, Setup or Prompt

What runs at boot, chosen without rebuilding anything:

| Target | Desktop | Command line | RetroArch option |
| --- | --- | --- | --- |
| The game | **Game > Boot To > Game** | `--target game` | **Game Runs: Game** |
| Its setup | **Game > Boot To > Setup** | `--target setup` | **Game Runs: Setup** |
| The DOS prompt in its folder | **Game > Boot To > DOS Prompt** | `--target prompt` | **Game Runs: DOS Prompt** |

Each restarts the machine. Setup always ends at the prompt, so you see what it did. Many games need their setup run
once before the first game, to choose a sound card or write a config file.

## Saves

The disk Geardox builds is a cache: the game's folder is what counts. When a session ends (power off, another game,
quitting, or the game powering off), what DOS changed goes back as files:

| The game is | Desktop default | **Game > Save Games To > Geardox's Save Folder** | RetroArch default | **Game Saves: Game Folder** |
| --- | --- | --- | --- | --- |
| A folder | The folder | The save folder | The save folder | The folder |
| A read-only folder | The save folder | The save folder | The save folder | The save folder |
| A ZIP | The save folder | The save folder | The save folder | The save folder |

- In the save folder, a game's saves live in `<game>.<id>.gdox/`, laid over the game every time it runs. A `.deleted`
  file there lists what the game deleted.
- Deletes are applied, new files take the name DOS gave them, and changed files keep their host names.
- Nothing that changed on your side during the session is overwritten: the game's copy goes to `.conflicts/` in the
  save folder, and Geardox tells you.
- Files the game writes outside its `dir` are not kept, and Geardox says so.
- If Geardox or the computer stops mid-session, the next launch writes the session back first. If DOS was cut
  mid-write and the disk doesn't add up, nothing is written: it's kept as `.refused` in the save folder, you're told,
  and the game starts again from its folder.
- **Game > Last Save Report...** shows what went back, **Game > Game Files** opens the game's folder and its saves
  folder. If saving failed, **Game > Retry Saving** tries again. Save states work as usual.
- Desktop: the save folder is Geardox's config folder, or the custom one in **Emulator > Directories > Save Files**.
  RetroArch: its save directory.

## Machine Settings

From strongest to weakest:

1. Your changes in the Machine menu while the game is open: kept for that game only, in `<config>/games/`.
   **Game > Game Files > Forget Machine Changes** removes them, **Game > Game Files > Save Descriptor As...** writes
   them into a descriptor.
2. The descriptor.
3. Your settings.
4. Geardox's defaults: 486DX2-66 at 66 MHz, 8 MB, FPU, Cirrus VL-Bus video, Sound Blaster 16 at IRQ 5 DMA 1, PS/2 mouse,
   CD-ROM at 4x.

In RetroArch the core options a descriptor sets are hidden while its game runs. **Game Descriptors: Disabled** turns
descriptors off.

**Games too fast for a 486.** Some games from before 1992 crash on a fast CPU ("Runtime error 200", "Divide error") or
hang at their loading screen. Real machines did the same. Give them `model = 386dx_33`, or `turbo = off`.

## Memory

Free memory with the mouse and CD drivers loaded:

| `memory` | Free conventional | What the game gets |
| --- | --- | --- |
| `none` | 498K | No memory manager. Real mode |
| `xms` | 606K | HIMEMX: XMS. Real mode. For DOS/4GW and most games |
| `umb` | 620K | JEMMEX without EMS: XMS, upper memory, VCPI |
| `ems` | 620K | JEMMEX: XMS, EMS with a page frame, upper memory, VCPI |

- EMS is the RAM less 1.5 MB, and 1 MB less again with 16 MB or more and a Cirrus card. `ems = N` raises the RAM to
  the smallest size that gives it.
- A game that asks for EMS by name (it looks for `EMMXXXX0`) needs `memory = ems` or `ems`. `umb` hides EMS on purpose.
- A game that refuses V86 mode wants `xms` or `none`.

## Sound

`BLASTER` is set to match the machine, for example `A220 I5 D1 H5 P330 T6` on the Sound Blaster 16. Games that come
already set up often have IRQ 7 in their config files (`SOUND.CFG`, `SETUP.CFG`, `DEFAULT.CFG`): give them `irq = 7`, or
run their setup again.

## Examples

A game in a subfolder of its ZIP, with the descriptor beside the ZIP:

```ini
[geardox]
version = 1
name = Alien Breed - Tower Assault

[drives]
c = Alien_Breed_-_Tower_Assault_1994.zip
dir = .

[run]
exe = AlienBrT\TA.EXE
setup = AlienBrT\SETSOUND.EXE
```

A game that needs EMS and a fixed IRQ, quitting back to the host:

```ini
[geardox]
version = 1
name = Master of Orion

[dos]
ems = 1024

[sound]
irq = 7

[drives]
dir = orion

[run]
exe = ORION.EXE
setup = INSTALL.EXE -S
exit = poweroff
```

A CD game, its disc inside its ZIP:

```ini
[geardox]
version = 1
name = Warcraft II

[drives]
c = WAR2.zip
dir = WAR2
cd = DISC/WAR2.cue

[run]
exe = WAR2.EXE
setup = SETUP.EXE
```

A speed-sensitive game, with its episodes as variants in one folder (`Arctic Adventure 1.gdox`,
`Arctic Adventure 2.gdox`, ...):

```ini
[geardox]
version = 1
name = Arctic Adventure 1

[machine]
model = 386dx_33

[drives]
dir = arctic

[run]
exe = AA1.EXE
```

A DOS/4GW game with more RAM and FreeDOS out of sight:

```ini
[geardox]
version = 1
name = Shadow Warrior

[machine]
ram = 16

[dos]
freedos = hidden

[drives]
dir = sw

[env]
DOS4G = QUIET

[run]
exe = SW.EXE
setup = SETUP.EXE
```

## Troubleshooting

| What you see | What to do |
| --- | --- |
| "no dir in [drives]" | Add `dir = .` or a folder name |
| "Bad command or filename" | `exe` is wrong, or not relative to the game's folder |
| "Please run SETUP", "CFG file not found" | **Game > Boot To > Setup** once |
| "Runtime error 200", "Divide error", stuck loading | `model = 386dx_33` or `turbo = off` |
| "Not enough memory", "requires 9 MB" | `ram = 16` |
| "needs expanded memory", "EMS" | `ems = 1024` (or what the game asks for) |
| No sound, or sound hangs | `irq = 7`, or run the game's sound setup |
| "Please insert the CD" | Add the disc with `cd` |
| The game's own boot disk | `a = disk.img` and `boot = a` |
