# Running DOS Games with GDOX Files

A **game descriptor** (`.gdox`) is a text file that records a game's location, startup command and settings.
Opening it in Geardox prepares an emulated hard disk with the game and the included FreeDOS operating system,
then launches the game.

- [Start with a game folder](#start-with-a-game-folder)
- [Create a GDOX file by hand](#create-a-gdox-file-by-hand)
- [Use a ZIP or CD image](#use-a-zip-or-cd-image)
- [Run setup or use the DOS prompt](#run-setup-or-use-the-dos-prompt)
- [Find your saves](#find-your-saves)
- [Adjust a game's settings](#adjust-a-games-settings)
- [Troubleshooting](#troubleshooting)
- [Settings reference](#settings-reference)

## Start with a Game Folder

To create a descriptor for an installed DOS game:

1. Choose **Game > New Game Descriptor...** in Geardox.
2. Select the game's folder.
3. Select the program that starts the game, such as `ORION.EXE` or `PLAY.BAT`.

Geardox saves the descriptor automatically and starts the game. To play again, choose it from
**Game > Recent Games**. To save a copy of the descriptor in a location of your choice, use
**Game > Game Files > Save Descriptor As...**.

If the game asks you to run setup first, see [Run setup or use the DOS prompt](#run-setup-or-use-the-dos-prompt).

## Create a GDOX File by Hand

Save the following text as `Master of Orion.gdox` in the game's folder, alongside `ORION.EXE`:

```ini
[geardox]
version = 1
name = Master of Orion

[drives]
dir = orion

[run]
exe = ORION.EXE
```

The descriptor sits alongside the game files:

```text
Master of Orion/
    Master of Orion.gdox
    ORION.EXE
    ...
```

Open the `.gdox` file with **Game > Open Game...**, or drag it onto Geardox. You can also drop the whole folder onto
Geardox or choose **Game > Open Game Folder...**.

- `version = 1` selects version 1 of the GDOX file format.
- `name` is the title Geardox displays for the game.
- `dir = orion` places the game's files in `C:\ORION` on the emulated hard disk.
- `exe = ORION.EXE` starts that program from the game's folder, giving `C:\ORION\ORION.EXE` in this example.

Only `version` and `dir` are required. Without `exe`, Geardox starts at the DOS prompt instead of launching a game.

### Game Location: `c` and `dir`

`c` selects the source files on your computer. `dir` sets their destination on the emulated C: drive:

| Setting | Meaning | Example |
| --- | --- | --- |
| `c` | Where the game's files already exist on your computer | `c = ../Games/Master of Orion` |
| `dir` | Where Geardox places those files on the DOS C: drive | `dir = orion` gives `C:\ORION` |

Geardox creates the DOS folder specified by `dir`. It accepts short DOS names such as `orion` (up to 8 characters,
without spaces). Use `dir = .` to place the game files directly in `C:\`.

If you leave out `c`, Geardox uses the folder containing the descriptor. If the descriptor is inside a ZIP, it uses
that ZIP instead. Set `c` when you want to keep the descriptor somewhere else.

Paths in `c` and external disk image entries are relative to the descriptor's location, unless you specify a full
path. For example, a descriptor at `Games/Orion.gdox` with `c = Orion` uses the folder `Games/Orion`. Paths with
spaces are written without surrounding quotes.

Paths in `exe`, `setup` and `cwd` are relative to the game's DOS folder. For example, `exe = GAME\ORION.EXE`
with `dir = orion` starts `C:\ORION\GAME\ORION.EXE`.

## Use a ZIP or CD Image

### A Game in a ZIP

You can keep the descriptor beside the game's ZIP and point to it with `c`:

```text
Games/
    Master of Orion.gdox
    Orion.zip
```

```ini
[geardox]
version = 1
name = Master of Orion

[drives]
c = Orion.zip
dir = orion

[run]
exe = ORION.EXE
```

This example uses a ZIP with `ORION.EXE` at its top level. Geardox preserves the ZIP's folder structure: if the
program is stored as `ORION/ORION.EXE` in the archive, set `exe = ORION\ORION.EXE`.

You can also put the descriptor at the top level of the ZIP, alongside the game files. In that case, leave out `c`
and open the ZIP itself. Saves go into Geardox's save folder; the ZIP is not modified.

### A Game That Needs a CD

Add `cd` under `[drives]` to attach a disc image. Supported formats are `.cue`, `.iso` and `.chd`.
For example, with this layout:

```text
Games/
    Warcraft II.gdox
    WAR2.zip
    Discs/
        WAR2.cue
        WAR2.bin
```

Use:

```ini
[geardox]
version = 1
name = Warcraft II

[drives]
c = WAR2.zip
dir = WAR2
cd = Discs/WAR2.cue

[run]
exe = WAR2.EXE
setup = SETUP.EXE
```

Here, `WAR2.zip` contains the installed game, with `WAR2.EXE` and `SETUP.EXE` at its top level. `Discs/WAR2.cue`
provides the game's CD. A CUE image also requires its referenced track files, such as `WAR2.bin` in this example.

The CD normally appears as D: in DOS. If you also configure a second hard disk with `d`, the CD uses the next free
letter. Geardox loads the CD driver automatically.

Disc images can also be inside the game's ZIP: use their path inside the ZIP, such as `cd = Discs/WAR2.cue`.
For multiple discs, list them separated by commas, or use one `.m3u` playlist.

## Run Setup or Use the DOS Prompt

Many games have a separate setup program for choosing a sound card or controls. Add its filename to the existing
`[run]` section:

```ini
[run]
exe = ORION.EXE
setup = INSTALL.EXE -S
```

The setup command depends on the game; common programs include `SETUP.EXE`, `INSTALL.EXE` and `SETSOUND.EXE`.
Choose **Game > Boot To > Setup** to run it. When setup finishes, Geardox returns to the DOS prompt.
Choose **Game > Boot To > Game** to start the game with the saved setup choices.

**Game > Boot To > DOS Prompt** opens a DOS prompt in the game's folder. From there you can run its setup
program or other DOS commands without adding them to the descriptor.

Each **Boot To** choice restarts the emulated PC.

By default, quitting a game returns to DOS. To turn off the emulated PC when the game exits, add `exit = poweroff`
under `[run]`. To close Geardox as well, enable **Game > Quit When a Game Powers Off**.

## Find Your Saves

During play, saved games and setup changes are written to the emulated hard disk. Geardox copies those changes
to the location below when you power off the emulated PC, open another game, or quit Geardox.

| How you play | Default location for changed files |
| --- | --- |
| Desktop, from a writable game folder | Back in that game folder |
| Desktop, from a ZIP or read-only folder | Geardox's save folder |
| RetroArch | RetroArch's save directory |

On desktop, choose **Game > Game Files > Show Saves Folder** to find the location used by the current game.
**Game > Last Save Report...** shows what was saved. If saving failed, use **Game > Retry Saving**.

To keep the original game folder unchanged, select **Game > Save Games To > Geardox's Save Folder** before opening
the game. Geardox stores the changed files there and uses them the next time you play. ZIPs and read-only folders
always use this separate save location.

To change the desktop save location, use **Emulator > Directories > Save Files**. The default is the folder where
Geardox stores its settings. In RetroArch, **Game Saves (restart)** selects the save directory or game folder;
ZIPs and read-only folders always use the save directory.

These locations apply to the game's files. Geardox save states are stored separately.

## Adjust a Game's Settings

On desktop, **Machine** menu changes made while a game is open are saved as settings for that game.
You can also record settings in the descriptor, for example:

```ini
[machine]
model = 386dx_33
```

This selects a slower PC, which can help with older games that run too fast or fail on a 486.

Geardox uses settings in this order:

1. Machine menu changes remembered for this game.
2. Settings in the descriptor.
3. Your general Geardox settings.
4. Built-in defaults for anything still unspecified.

If an edit to the descriptor seems to have no effect, use **Game > Game Files > Forget Machine Changes**, then
restart the game. To export the descriptor with your remembered changes included, use
**Game > Game Files > Save Descriptor As...**.

With default settings, Geardox uses a 486DX2 at 66 MHz, 8 MB of RAM, Cirrus VL-Bus video, a Sound Blaster 16
(IRQ 5, DMA 1), a PS/2 mouse and 4x CD-ROM speed.

### Sound Setup

The game's sound settings must match Geardox's emulated sound card. With the defaults, choose Sound Blaster 16
in the game's setup program, with port `220`, IRQ `5`, DMA `1`, and high DMA `5` if asked.

Some games arrive configured for IRQ 7. Either run their setup and choose IRQ 5, or add this to the descriptor:

```ini
[sound]
irq = 7
```

### Memory

Set the emulated RAM in megabytes (MB). For example, for a game requiring 16 MB:

```ini
[machine]
ram = 16
```

Games requiring **expanded memory (EMS)** also need a DOS memory manager. The `ems` setting enables it and
specifies the required amount in kilobytes (KB). For example, 1 MB of EMS:

```ini
[dos]
ems = 1024
```

When `ems` is specified and `ram` is omitted, Geardox can increase the emulated RAM to meet the EMS requirement.
The advanced `memory` and `ems_frame` settings control the DOS memory manager; see [Memory modes](#memory-modes).

## Troubleshooting

| Problem | What to check |
| --- | --- |
| The descriptor will not open | The error message identifies invalid or missing settings. Every descriptor requires `version = 1` in `[geardox]` and `dir` in `[drives]`. |
| Geardox cannot find the game folder or ZIP | Check `c`. Relative paths start from the descriptor's folder, and paths should not have surrounding quotes. |
| "Bad command or filename" | Check `exe` under `[run]`. It starts from the game's folder inside DOS; include any subfolder inside your folder or ZIP. |
| "Please run SETUP" or a missing configuration file | Run the game's setup using **Game > Boot To > Setup**, or start it from **DOS Prompt**. |
| The game runs too fast, gives "Runtime error 200" or "Divide error", or hangs while loading | Try `model = 386dx_33` under `[machine]`. You can also try `turbo = off`. These errors can have other causes. |
| The game asks for more RAM | Try `ram = 16` under `[machine]`, or the amount the game requires. |
| The game asks for expanded memory or EMS | Add `ems = 1024` under `[dos]`, or the required amount in KB. |
| No sound, or the game hangs when playing sound | Run the game's sound setup and match its card, IRQ and DMA to Geardox. |
| "Please insert the CD" | Check `cd` under `[drives]` and the CD drive letter expected by the game's setup. |
| Descriptor changes seem to be ignored | Use **Game > Game Files > Forget Machine Changes** and restart the game. |
| Saves are not in the game's folder | Check **Game > Game Files > Show Saves Folder**. ZIPs, read-only folders and RetroArch use a separate save location by default. |

## Other Ways to Open a Game

On the command line, pass a descriptor, a folder containing one, or a ZIP containing one:

```sh
geardox "Master of Orion.gdox"
geardox --target setup "Master of Orion.gdox"
geardox --target prompt "Master of Orion.gdox"
```

In RetroArch, load the `.gdox` file or a ZIP containing one at its top level. Use **Game Runs (restart)** to choose
the game, setup or DOS prompt, then restart the content. Machine options controlled by the descriptor are hidden
while the game runs. **Game Descriptors (restart): Disabled** turns descriptor handling off.

A folder or ZIP can contain several descriptors at its top level, for example for different episodes of a game.
The desktop asks which one to use; RetroArch selects the first in name order. A folder or ZIP without a descriptor
is opened as a plain hard disk source, without the automatic game setup described here.

## Settings Reference

The tables below list all supported settings and their behavior when omitted.

### Editing Rules

- Use `[section]` headings followed by `key = value` lines. Section and key names are not case-sensitive.
- Leave optional settings out to use the behavior in the **When omitted** column. Machine settings inherit your
  Geardox preferences; other settings have the defaults listed below.
- The value `auto` is supported only by `ems_frame`.
- File paths are written without surrounding quotes. Spaces within a value are preserved; leading and trailing
  spaces are removed.
- Comments occupy a whole line starting with `;` or `#`. Text after a value on the same line is part of that value.
- `[config_sys]` and `[autoexec]` contain DOS instructions rather than ordinary settings. Use `REM` for comments there.
- Paths accept `/` or `\`. Use commas to separate multiple disk images.
- Unknown settings produce a warning and are ignored. Invalid values, duplicate settings and missing required
  settings prevent loading. Descriptors requiring a newer format version cannot be loaded.
- Save as UTF-8 plain text, using Windows or Unix line endings. The maximum file size is 64 KB.

### `[geardox]`

| Key | Values | When omitted |
| --- | --- | --- |
| `version` | `1` | **Required** |
| `name` | The game's title: window title, save state names, volume label | The file's name |

### `[drives]`

| Key | Values | When omitted |
| --- | --- | --- |
| `dir` | Folder to create inside DOS: `orion` gives `C:\ORION`; `.` uses `C:\`. Use short DOS names (up to 8 characters, optionally a 3-character extension), without spaces. The first folder cannot be `FREEDOS` or `TEMP` | **Required** |
| `c` | Existing game folder or ZIP on your computer | The folder or ZIP containing the descriptor |
| `free` | Extra space for saves and other game files on C:, in MB: 16 to 1024 | `64` |
| `d` | A folder, ZIP or hard disk image for D: (Primary Slave). Changes stay in its disk image, rather than being copied back to the source folder or ZIP | None |
| `a`, `b` | Floppy images for A: and B:, comma separated. The first disk is inserted; the others are available to swap in | None |
| `cd` | CD images (`.cue`, `.iso`, `.chd`) or one `.m3u`, comma separated. Adds a CD-ROM drive on the Secondary Master. Images inside the game's ZIP are extracted automatically | None |
| `cd_speed` | `1x`, `2x`, `4x`, `8x` | Geardox's setting (4x) |

### `[run]`

Program paths are relative to `dir`: with `dir = orion`, `exe = ORION.EXE` runs `C:\ORION\ORION.EXE`.

| Key | Values | When omitted |
| --- | --- | --- |
| `exe` | The program that starts the game: `ORION.EXE`, `GAME\GAME.EXE`, `PLAY.BAT` | None: the DOS prompt |
| `args` | Extra options passed to the game, such as `-nosound` | None |
| `cwd` | Working folder used when starting the game, relative to `dir` | `exe`'s directory |
| `setup` | The game's setup program, with arguments: `SETUP.EXE`, `INSTALL.EXE -S` | None |
| `exit` | `prompt`: back to DOS when the game quits. `poweroff`: the machine powers off and the session ends. **Game > Quit When a Game Powers Off** closes Geardox too | `prompt` |
| `boot` | `c` to start from the hard disk, or `a` for games that boot directly from a floppy in A: | `c` |

### `[machine]`

| Key | Values | When omitted |
| --- | --- | --- |
| `model` | `386sx_20`, `386dx_33`, `486dx2_66` | Your machine setting |
| `cpu_mhz` | `16`, `20`, `25`, `33`, `40`, `50`, `66`, `100` | Your CPU speed setting, or the model's normal speed |
| `turbo` | `on`, `off`. Off runs the CPU at a third of its clock | Your turbo setting |
| `ram` | MB: `1`, `2`, `4`, `8`, `16` on the 386s; `4`, `8`, `16`, `32`, `64` on the 486 | Your RAM setting; may increase for `ems` |
| `fpu` | `none`, `80387` (the 486DX2 has one built in) | Your FPU setting |
| `floppy_a`, `floppy_b` | `360k`, `1200k`, `720k`, `1440k`, `2880k` | Your drive setting, or automatic detection |
| `video` | `vga`, `gd5428` (Cirrus ISA), `gd5428_vlb` (Cirrus VL-Bus, 486 only) | Your video setting, or the model's default |

### `[sound]`

| Key | Values | When omitted |
| --- | --- | --- |
| `card` | `none`, `adlib`, `sb15`, `sb20`, `sbpro`, `sbpro2`, `sb16` | Your sound card setting |
| `irq` | `5`, `7` | Your IRQ setting |
| `dma` | `1`, `3` | Your DMA setting |

The sound port is 220 (hexadecimal). The Sound Blaster 16 uses high DMA 5 and MIDI port 330. Geardox sets the
DOS `BLASTER` variable automatically to match the selected card, IRQ and DMA.

### `[input]`

| Key | Values | When omitted |
| --- | --- | --- |
| `mouse` | `none`, `ps2`, `serial` (COM1). The mouse driver loads only with a mouse | Your mouse setting |
| `joystick` | `none`, `joystick`, `gamepad`, `flightstick` | Geardox's setting |

### `[dos]`

| Key | Values | When omitted |
| --- | --- | --- |
| `memory` | `none`, `xms`, `umb`, `ems` (see [Memory modes](#memory-modes)) | `xms`, or `ems` when `ems` is set |
| `ems` | Required expanded memory in KB: 16 to 32768. Selects EMS unless `memory` is explicitly set; can raise RAM if `ram` is omitted | None |
| `ems_frame` | `auto`, `none`, or a hexadecimal address from `C000` to `E000` in steps of `0400` | `auto` |
| `files` | Maximum open files: 8 to 255 | `40` |
| `buffers` | DOS disk buffers: 1 to 99 | `20` |
| `version` | The DOS version to report, `x.yy`, for games that check it | FreeDOS' version |
| `freedos` | `visible` or `hidden`: whether FreeDOS' files in the root of C: show in `DIR` | `visible` |

### `[env]`

Use this section for DOS environment variables required by a game. Each `NAME = value` becomes `SET NAME=value`.
These are applied after Geardox's variables, so they can also replace values such as `BLASTER`.

```ini
[env]
DOS4G = QUIET
```

### `[config_sys]` and `[autoexec]`

These advanced sections add DOS startup commands. `[config_sys]` adds lines to FreeDOS' `FDCONFIG.SYS`, before
its `SHELL=` line. `[autoexec]` adds commands to `FDAUTO.BAT`, run in the game's folder before the game starts.

```ini
[config_sys]
STACKS=9,128

[autoexec]
LH MYDRIVER.COM
```

## Advanced DOS Details

### Memory Modes

`memory` in `[dos]` selects how FreeDOS makes memory available to games:

| Value | Use |
| --- | --- |
| `xms` | Default. Loads HIMEMX for extended memory (XMS), suitable for DOS/4GW and many other games. |
| `ems` | Loads JEMMEX for expanded memory (EMS), XMS and upper memory. |
| `umb` | Loads JEMMEX with EMS disabled, providing XMS and upper memory. |
| `none` | Loads no memory manager. |

Upper memory lets DOS load drivers outside the conventional memory area used by many games. `umb` and `ems`
use V86 mode and provide VCPI; if a game refuses V86 mode, try `xms` or `none`.

`ems = N` is a request for enough memory, not a cap on the EMS DOS exposes. Available EMS is RAM minus about
1.5 MB, with another 1 MB reserved when using at least 16 MB of RAM and a Cirrus card. An explicit `ram` setting
prevents automatic RAM selection, and an explicit `memory` setting takes precedence over the mode implied by `ems`.

### Files on the DOS C: Drive

With `dir = orion`, the prepared drive contains:

```text
C:\KERNEL.SYS     FreeDOS kernel
C:\FDCONFIG.SYS   DOS configuration generated from the descriptor
C:\FDAUTO.BAT     Startup commands generated from the descriptor
C:\FREEDOS\BIN    DOS commands, drivers and tools
C:\TEMP           Temporary files
C:\ORION          Your game's files
```

`freedos = hidden` under `[dos]` hides the FreeDOS files and folders from a normal `DIR` listing. They remain
available and can be seen with `DIR /A`.

Geardox generates `FDCONFIG.SYS` and `FDAUTO.BAT` again when preparing the disk. To make lasting startup changes,
use `[config_sys]` and `[autoexec]` in the descriptor. A game's own `CONFIG.SYS` and `AUTOEXEC.BAT` stay in its
folder; they do not replace FreeDOS' startup files.

With `dir = .`, game files share the root of C: with FreeDOS. Game entries with names reserved for those FreeDOS
files or folders are left out. Using a named game folder avoids that overlap.

For a game designed to boot directly from a floppy, set `a = disk.img` under `[drives]` and `boot = a` under `[run]`.

### Save Recovery and File Conflicts

The prepared hard disk is a working copy. Geardox copies changes from the game's DOS folder back to the game
folder or separate save location when the session ends. Files written outside `dir` are not kept, and Geardox
reports them. A second hard disk configured with `d` keeps its changes in its own disk image.

Separate saves are stored in a folder named `<game>.<id>.gdox`. A `.deleted` file records files the game removed,
so those deletions are also applied the next time it runs. New files use their DOS names; changed files retain
their original names on your computer.

If you change a file on your computer while the game is running, Geardox avoids overwriting it with the game's
copy. It keeps the conflicting copy in `.conflicts/` in the save folder and reports the conflict.

After an unexpected shutdown, Geardox attempts to recover the previous session's changes the next time you open
that game. If the disk is damaged, it keeps the disk image with a `.refused` suffix, reports the problem, and starts
again from the game source instead of copying potentially damaged files back.
