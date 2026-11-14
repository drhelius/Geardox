# Running DOS Games with GDOX Files

A **game descriptor** (`.gdox`) is a text file that records a game's location, startup command and settings.
Opening it in Geardox prepares an emulated hard disk with the game and DOS, then launches the game. The DOS is the
included FreeDOS, or [your own MS-DOS](#use-your-own-ms-dos) once you import it.

- [Start with a game folder](#start-with-a-game-folder)
- [Create a GDOX file by hand](#create-a-gdox-file-by-hand)
- [Use a ZIP or CD image](#use-a-zip-or-cd-image)
- [Run setup or use the DOS prompt](#run-setup-or-use-the-dos-prompt)
- [Find your saves](#find-your-saves)
- [Adjust a game's settings](#adjust-a-games-settings)
- [Use your own MS-DOS](#use-your-own-ms-dos)
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

[game]
name = Master of Orion
folder = C:\ORION

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
- `folder = C:\ORION` places the game's files in `C:\ORION` on the emulated hard disk.
- `exe = ORION.EXE` starts that program from the game's folder, giving `C:\ORION\ORION.EXE` in this example.

Only `version` and `folder` are required. Without `exe`, Geardox starts at the DOS prompt instead of launching a
game.

### Game Location: `source` and `folder`

`source` selects the game's files on your computer. `folder` sets their destination on the emulated C: drive:

| Setting | Meaning | Example |
| --- | --- | --- |
| `source` | Where the game's files already exist on your computer | `source = ../Games/Master of Orion` |
| `folder` | Where Geardox places those files on the DOS C: drive | `folder = C:\ORION` |

Geardox creates the DOS folder specified by `folder`. It accepts short DOS names such as `C:\ORION` (up to 8
characters, without spaces). Use `folder = C:\` to place the game files directly in the root of C:.

If you leave out `source`, Geardox uses the folder containing the descriptor. If the descriptor is inside a ZIP, it
uses that ZIP instead. Set `source` when you want to keep the descriptor somewhere else.

Paths in `source` and external disk image entries are relative to the descriptor's location, unless you specify a
full path. For example, a descriptor at `Games/Orion.gdox` with `source = Orion` uses the folder `Games/Orion`. Paths
with spaces are written without surrounding quotes.

Paths in `exe`, `setup` and `cwd` are relative to the game's DOS folder. For example, `exe = GAME\ORION.EXE`
with `folder = C:\ORION` starts `C:\ORION\GAME\ORION.EXE`.

## Use a ZIP or CD Image

### A Game in a ZIP

You can keep the descriptor beside the game's ZIP and point to it with `source`:

```text
Games/
    Master of Orion.gdox
    Orion.zip
```

```ini
[geardox]
version = 1

[game]
name = Master of Orion
source = Orion.zip
folder = C:\ORION

[run]
exe = ORION.EXE
```

This example uses a ZIP with `ORION.EXE` at its top level. Geardox preserves the ZIP's folder structure: if the
program is stored as `ORION/ORION.EXE` in the archive, set `exe = ORION\ORION.EXE`.

You can also put the descriptor at the top level of the ZIP, alongside the game files. In that case, leave out
`source` and open the ZIP itself. Saves go into Geardox's save folder; the ZIP is not modified.

### A Game That Needs a CD

Add `cdrom` under `[drives]` to attach a disc image. Supported formats are `.cue`, `.iso` and `.chd`.
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

[game]
name = Warcraft II
source = WAR2.zip
folder = C:\WAR2

[drives]
cdrom = Discs/WAR2.cue

[run]
exe = WAR2.EXE
setup = SETUP.EXE
```

Here, `WAR2.zip` contains the installed game, with `WAR2.EXE` and `SETUP.EXE` at its top level. `Discs/WAR2.cue`
provides the game's CD. A CUE image also requires its referenced track files, such as `WAR2.bin` in this example.

The CD normally appears as D: in DOS. If you also add hard disks with `hdd_primary_slave` or `hdd_secondary_slave`,
the CD uses the next free letter after them. Geardox loads the CD driver automatically.

Disc images can also be inside the game's ZIP: use their path inside the ZIP, such as `cdrom = Discs/WAR2.cue`.
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

By default, quitting a game returns to DOS. To turn off the emulated PC when the game exits, add
`on_exit = poweroff` under `[run]`. To close Geardox as well, enable **Game > Quit When a Game Powers Off**.

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

On desktop, **Emulator > Machine** changes made while a game is open are saved as settings for that game.
You can also record settings in the descriptor, for example:

```ini
[machine]
model = 386dx_33
```

This selects a slower PC, which can help with older games that run too fast or fail on a 486.

Geardox uses settings in this order:

1. **Emulator > Machine** changes remembered for this game.
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
ram_mb = 16
```

Games requiring **expanded memory (EMS)** also need a DOS memory manager. The `ems_kb` setting enables it and
specifies the required amount in kilobytes (KB). For example, 1 MB of EMS:

```ini
[dos]
ems_kb = 1024
```

When `ems_kb` is specified and `ram_mb` is omitted, Geardox can increase the emulated RAM to meet the EMS
requirement.
The advanced `memory` and `ems_frame` settings control the DOS memory manager; see [Memory modes](#memory-modes).

## Remap Keys

A game with keys hardcoded, such as a dungeon crawler that moves on the numeric keypad, can get the cursor keys and
WASD too. Each line of `[keys]` names one of your keys and the PC key it presses:

```ini
[keys]
up = kp_8
down = kp_2
left = kp_7
right = kp_9
w = kp_8
s = kp_2
a = kp_4
d = kp_6
q = kp_7
e = kp_9
```

Keys without a line keep their usual job, so the keypad still works. A key can press up to four PC keys together,
joined with `+`, such as `f13 = left_ctrl + left_alt + delete`, or nothing at all: `grave = none`.

The same can be done while the game runs: **Ctrl+F1** (**Cmd+F1** on macOS) opens the keyboard mapper, a picture of
the PC keyboard where you click a PC key and press the key of yours that should drive it. Those changes are kept for
that game in a `.keys` file in Geardox's save folder (RetroArch: its save directory), and win over the descriptor's
lines for the same key. **Save Descriptor As...** writes both into its `[keys]`.

Keys bound like WASD above no longer type their letters. **Ctrl+F2** (**Cmd+F2** on macOS) turns the game's key
changes off and on, to name a character or a saved game.

## Use a Gamepad

Gamepads press the PC's keys and move its mouse. Without settings, the D-pad sends the cursor keys, the left stick
moves the mouse, A and B click, X is Space, Y is Left Ctrl, L1 is Left Alt, R1 is Left Shift, Back is Esc and Start
is Enter. `[pad1]` changes the first gamepad for the game, here for the same dungeon crawler:

```ini
[pad1]
dpad_up = kp_8
dpad_down = kp_2
dpad_left = kp_7
dpad_right = kp_9
l1 = kp_4
r1 = kp_6
```

Buttons without a line keep your own settings, so the left stick still moves the mouse and A and B still click.
`[pad2]` is the second gamepad, for games where two players share the keyboard. For a game played with a joystick,
`use = joystick` in `[pad1]` makes the first gamepad the PC's joystick instead.

On desktop, **Input > Host Gamepad 1 > Key Mapping** does the same while the game runs: click a button's row, then
press the PC key or choose a mouse button. Changes made during a game are kept with its settings, and **Save
Descriptor As...** writes them into its `[pad1]` and `[pad2]`.

RetroArch ignores `[pad1]` and `[pad2]`. Set the port's device type to **Keyboard and Mouse** in **Quick Menu >
Controls > Port 1 Controls**, give each button a key there, and save a game remap file; those keys also go through the
game's `[keys]`. The options **Gamepad 1 Moves Mouse With** and **Gamepad 1 Mouse Buttons** pick what drives the
mouse. For joystick games, choose **PC Joystick** as the device type.

## Use Your Own MS-DOS

Geardox includes FreeDOS and needs nothing else. If you own MS-DOS, games can run on it instead. Geardox doesn't
include MS-DOS: you import your own Setup disks once.

1. Choose **Game > DOS > Import MS-DOS...**.
2. Select one of the Setup disk images, or a ZIP containing them. When you select an image, Geardox reads every
   image in its folder.

Geardox expands the files as MS-DOS Setup does and keeps its own copy, `msdos.zip`, in its settings folder. Your
disk images are never changed. Any floppy image format Geardox can open works, and the disks can be in any order.

From the next game you open, every game runs on MS-DOS unless its descriptor asks for FreeDOS. The game's C: drive
then has MS-DOS's files instead of FreeDOS's, and the same `CONFIG.SYS` and `AUTOEXEC.BAT`, generated from the
descriptor (see [Files on the DOS C: drive](#files-on-the-dos-c-drive)). Saves are unaffected: they stay in the game's
folder or save location whichever DOS runs.

- **One game on FreeDOS:** while the game is open, choose **Game > DOS > FreeDOS**. Geardox closes the game, saving
  its changes, and opens it again on FreeDOS. The choice is kept for that game, and **Forget Machine Changes**
  clears it. **Game > DOS** and your MS-DOS's name, such as **MS-DOS 6.22**, switches it back.
- **In a descriptor:** `system = freedos` or `system = msdos` under `[dos]`. A game that asks for MS-DOS doesn't
  start until MS-DOS is imported.
- **Removing it:** **Game > DOS > Remove MS-DOS** deletes Geardox's copy. Games run on FreeDOS again from the next one
  opened.

**MS-DOS 5.0, 6.0, 6.2, 6.21 and 6.22** can be imported, in any language. Geardox installs the files MS-DOS Setup
installs, as the Setup disks list them, and leaves out supplemental disks, which Setup never installs. The name shown,
such as "MS-DOS 6.22", comes from your copy's own `COMMAND.COM`. A disk or file that's missing, a disk that can't be
read, or a compressed file that doesn't expand is reported with the disk it should be on. A damaged byte inside a
compressed file can't be detected, because MS-DOS's compression has no checksum. MS-DOS 4 and older aren't supported.

MS-DOS 5.0 has no CD-ROM extensions, so a game's CD uses SHSUCDX, Geardox's own, as FreeDOS does. Its EMM386 sets the
whole of its EMS aside when it loads: with `memory = ems`, a game gets all the extended memory as EMS and has little
XMS left, unless `ems_kb` asks for less. `cache = on` loads nothing, as 5.0 has no SMARTDRV.EXE.

In RetroArch, put the Setup disk images, or a ZIP of them, in `geardox/msdos` inside RetroArch's system folder.
Geardox imports them the next time a game loads, and again whenever they change. Removing them goes back to FreeDOS.
The **Game DOS (restart)** core option set to **FreeDOS** runs games on FreeDOS without removing anything, and
RetroArch's per-game options make that choice for one game.

## Troubleshooting

| Problem | What to check |
| --- | --- |
| The descriptor will not open | The error message identifies invalid or missing settings. Every descriptor requires `version = 1` in `[geardox]` and `folder` in `[game]`, written as a DOS path such as `C:\ORION`. |
| Geardox cannot find the game folder or ZIP | Check `source` under `[game]`. Relative paths start from the descriptor's folder, and paths should not have surrounding quotes. |
| "Bad command or filename" | Check `exe` under `[run]`. It starts from the game's folder inside DOS; include any subfolder inside your folder or ZIP. |
| "Please run SETUP" or a missing configuration file | Run the game's setup using **Game > Boot To > Setup**, or start it from **DOS Prompt**. |
| The game runs too fast, gives "Runtime error 200" or "Divide error", or hangs while loading | Try `model = 386dx_33` under `[machine]`. You can also try `turbo = off`. These errors can have other causes. |
| The game asks for more RAM | Try `ram_mb = 16` under `[machine]`, or the amount the game requires. |
| The game asks for expanded memory or EMS | Add `ems_kb = 1024` under `[dos]`, or the required amount in KB. |
| No sound, or the game hangs when playing sound | Run the game's sound setup and match its card, IRQ and DMA to Geardox. |
| "Please insert the CD" | Check `cdrom` under `[drives]` and the CD drive letter expected by the game's setup. |
| Descriptor changes seem to be ignored | Use **Game > Game Files > Forget Machine Changes** and restart the game. |
| Saves are not in the game's folder | Check **Game > Game Files > Show Saves Folder**. ZIPs, read-only folders and RetroArch use a separate save location by default. |
| "This game needs MS-DOS, and none has been imported" | The descriptor has `system = msdos` under `[dos]`. Import your MS-DOS, see [Use your own MS-DOS](#use-your-own-ms-dos), or remove the line to use FreeDOS. |
| A game works on FreeDOS but not on MS-DOS, or the other way round | Choose the other one with **Game > DOS** while the game is open. The two DOSes leave different amounts of memory, see [Memory modes](#memory-modes). |
| After closing, a message says the game changed `AUTOEXEC.BAT` or `CONFIG.SYS` | The game's installer edited them. Geardox writes both again at every launch, so the message lists the lines it added. Copy the ones the game needs into `[autoexec]` or `[config_sys]`. |

## Other Ways to Open a Game

On the command line, pass a descriptor, a folder containing one, or a ZIP containing one:

```sh
geardox "Master of Orion.gdox"
geardox --target setup "Master of Orion.gdox"
geardox --target prompt "Master of Orion.gdox"
```

In RetroArch, load the `.gdox` file or a ZIP containing one at its top level. Use **Game Runs (restart)** to choose
the game, setup or DOS prompt, then restart the content. Machine options controlled by the descriptor are hidden
while the game runs. **Game Descriptors (restart): Disabled** turns descriptor handling off. **Game DOS (restart)**
chooses between your own MS-DOS, when imported, and FreeDOS.

A folder or ZIP can contain several descriptors at its top level, for example for different episodes of a game.
The desktop asks which one to use; RetroArch selects the first in name order. A folder or ZIP without a descriptor
is opened as a plain hard disk source, without the automatic game setup described here.

## Settings Reference

Every setting a descriptor accepts, section by section. Only two are required: `version` in `[geardox]` and `folder`
in `[game]`. Leave out any other setting the game doesn't need, and Geardox uses the value in the **Default** column.

### Sections

| Section | Required | Contains |
| --- | --- | --- |
| `[geardox]` | Yes | The descriptor's format version |
| `[game]` | Yes | The game's name, where its files are on your computer, and where they go on the DOS C: drive |
| `[run]` | No | The program that starts the game, its setup program, and what happens when it quits |
| `[drives]` | No | Floppy disks, extra hard disks and CDs, as in the **Drives** menu |
| `[machine]` | No | The emulated PC: model, CPU speed, RAM, coprocessor, floppy and CD-ROM drive types, and video card |
| `[sound]` | No | The sound card and its IRQ and DMA |
| `[input]` | No | The mouse and the joystick |
| `[keys]` | No | Your keys and the PC keys they press |
| `[pad1]`, `[pad2]` | No | The first and second gamepads: the PC keys and mouse buttons they press, and what moves the mouse |
| `[dos]` | No | DOS memory management and other DOS settings |
| `[env]` | No | DOS environment variables |
| `[config_sys]`, `[autoexec]` | No | Extra DOS startup lines |

### Editing Rules

- Each section starts with a `[section]` heading, followed by `key = value` lines. Section and key names are not
  case-sensitive, and sections can appear in any order.
- Paths are written without surrounding quotes and accept `/` or `\`. Paths to files on your computer, such as
  `source` and disk images, are relative to the descriptor's folder. Program paths in `[run]` are relative to
  `folder` on the DOS C: drive.
- Spaces within a value are preserved; leading and trailing spaces are removed. Lists of disk images are separated
  by commas.
- There is no `auto` value, except for `ems_frame`: to let Geardox choose, leave the setting out.
- Comments occupy a whole line starting with `;` or `#`. Text after a value on the same line is part of that value.
- Unknown settings produce a warning and are ignored. Invalid values, duplicate settings and missing required
  settings prevent loading. Descriptors requiring a newer format version cannot be loaded.
- Save as UTF-8 plain text, using Windows or Unix line endings. The maximum file size is 64 KB.

### `[geardox]`

| Key | Required | Description | Values | Default | Example |
| --- | --- | --- | --- | --- | --- |
| `version` | Yes | The version of the GDOX format the descriptor uses. Not the DOS version, which is `version` in `[dos]` | `1` | — | `version = 1` |

### `[game]`

| Key | Required | Description | Values | Default | Example |
| --- | --- | --- | --- | --- | --- |
| `name` | No | The game's title, shown in the window title and used for save state names and the C: volume label | Text | The descriptor's file name | `name = Master of Orion` |
| `folder` | Yes | The folder on the DOS C: drive where Geardox places the game's files | `C:\` for the root, or a path below it. Each folder name has up to 8 characters, plus an optional 3-character extension, and no spaces. The first folder cannot be `FREEDOS` or `TEMP` | — | `folder = C:\ORION` |
| `source` | No | The game's files on your computer | A folder or a `.zip` file | The folder or ZIP containing the descriptor | `source = Orion.zip` |
| `free_mb` | No | Free space on C: for saves and other files the game creates, in MB | `16` to `1024` | `64` | `free_mb = 256` |

### `[run]`

Program paths are relative to `folder`: with `folder = C:\ORION`, `exe = ORION.EXE` runs `C:\ORION\ORION.EXE`.

| Key | Required | Description | Values | Default | Example |
| --- | --- | --- | --- | --- | --- |
| `exe` | No | The program that starts the game | A `.EXE`, `.COM` or `.BAT` path | None: Geardox stops at the DOS prompt | `exe = ORION.EXE` |
| `args` | No | Options passed to `exe` | Text | None | `args = -nosound` |
| `cwd` | No | The folder the game starts in, for games that must run from a folder other than their program's | A DOS path, relative to `folder` | The folder containing `exe` | `cwd = DATA` |
| `setup` | No | The game's setup program, with any options. **Game > Boot To > Setup** runs it | A program path, optionally followed by options | None: **Boot To > Setup** is unavailable | `setup = INSTALL.EXE -S` |
| `on_exit` | No | What happens when the game quits | `prompt`: return to the DOS prompt. `poweroff`: power off the emulated PC. With **Game > Quit When a Game Powers Off**, Geardox closes too | `prompt` | `on_exit = poweroff` |
| `boot` | No | The drive the emulated PC starts from | `c`: the hard disk. `a`: the floppy in A:, for games that boot directly from a floppy | `c` | `boot = a` |

### `[drives]`

The game's own disk is always C:, on the Primary Master. Extra hard disks take the next letters in the order of their
positions, D: then E:, and the CD drive the letter after them. If your CD-ROM drive setting puts it on a position the
game's hard disks use, Geardox moves it to the Secondary Master for that game.

| Key | Required | Description | Values | Default | Example |
| --- | --- | --- | --- | --- | --- |
| `floppy_a`, `floppy_b` | No | Floppy disk images for drives A: and B:. The first disk is inserted; the others can be swapped in | Image paths, comma separated | None | `floppy_a = Disk1.img, Disk2.img` |
| `hdd_primary_slave` | No | An extra hard disk on the Primary Slave, D:. Its changes stay in its own disk image instead of being copied back to the folder or ZIP | A folder, ZIP or hard disk image | None | `hdd_primary_slave = Extras.zip` |
| `hdd_secondary_slave` | No | An extra hard disk on the Secondary Slave: E:, or D: without `hdd_primary_slave`. Its changes stay in its own disk image, as with `hdd_primary_slave` | A folder, ZIP or hard disk image | None | `hdd_secondary_slave = Data` |
| `cdrom` | No | CD images for a CD-ROM drive on the Secondary Master. Images inside the game's ZIP are extracted automatically | `.cue`, `.iso` or `.chd` paths, comma separated, or one `.m3u` playlist | None | `cdrom = Discs/WAR2.cue` |

### `[machine]`

When left out, these settings follow your own Geardox settings: **Emulator > Machine** on desktop, or the core
options in RetroArch. The **Default** column shows what those are unless you changed them.

| Key | Required | Description | Values | Default | Example |
| --- | --- | --- | --- | --- | --- |
| `model` | No | The emulated PC | `386sx_20`, `386dx_33`, `486sx_33`, `486dx2_66`, `486dx4_100` | Your setting (`486dx2_66`) | `model = 386dx_33` |
| `cpu_mhz` | No | The CPU clock, in MHz | `16`, `20`, `25`, `33`, `40`, `50`, `66`, `100` | Your setting (the model's own clock: 20, 33, 66 or 100) | `cpu_mhz = 25` |
| `turbo` | No | The turbo switch. `off` runs the CPU at a third of its clock, for games that run too fast | `on`, `off` | Your setting (`on`) | `turbo = off` |
| `ram_mb` | No | Memory, in MB | `1`, `2`, `4`, `8`, `16` on the 386s; `4`, `8`, `16`, `32`, `64` on the 486s | Your setting (`8`), raised if needed to fit `ems_kb` | `ram_mb = 16` |
| `fpu` | No | The math coprocessor on the 386s, or the 487SX on the 486SX. The 486DX2 and 486DX4 always have one built in | `none`, `80387` | Your setting (`80387`) | `fpu = none` |
| `floppy_a_type`, `floppy_b_type` | No | The floppy drive types | `360k`, `720k`, `1200k`, `1440k`, `2880k` | Your setting (detected from the disk in the drive) | `floppy_a_type = 1200k` |
| `cdrom_speed` | No | The CD-ROM drive speed | `1x`, `2x`, `4x`, `8x` | Your setting (`4x`) | `cdrom_speed = 2x` |
| `drive_timing` | No | How long the drives' mechanics take: seeks, the wait for a sector to come round, and spinning up a CD. `fast` takes a sixteenth of the time, so games load sooner; `real` is for programs that time the drive. Transfers keep their speed either way | `real`, `fast` | Your setting (Auto, which is `fast` for games) | `drive_timing = real` |
| `video` | No | The video card. `cga` and `ega` are IBM's own cards, for games written for them | `vga`, `gd5428` (Cirrus ISA), `gd5428_vlb` (Cirrus VL-Bus, 486s only), `cga`, `ega` | Your setting (`gd5428` on the 386s, `gd5428_vlb` on the 486s) | `video = cga` |
| `cga_output` | No | The monitor on the CGA. `auto` shows composite colour when the game turns on 640x200 with the colour burst, and RGB otherwise; `composite` is for games drawn for a composite monitor | `auto`, `rgb`, `composite` | Your setting (`auto`) | `cga_output = composite` |

### `[sound]`

Like `[machine]`, these settings follow your own Geardox settings when left out.

| Key | Required | Description | Values | Default | Example |
| --- | --- | --- | --- | --- | --- |
| `card` | No | The sound card | `none`, `adlib`, `sb15` (Sound Blaster 1.5), `sb20` (2.0), `sbpro` (Pro), `sbpro2` (Pro 2), `sb16` (16) | Your setting (`sb16`) | `card = sbpro` |
| `irq` | No | The sound card's IRQ | `5`, `7` | Your setting (`5`) | `irq = 7` |
| `dma` | No | The sound card's 8-bit DMA channel | `1`, `3` | Your setting (`1`) | `dma = 3` |
| `mpu` | No | The MPU-401 MIDI port at 330. The Sound Blaster 16 has its own, on the card's IRQ. With the other cards it's a separate MPU-401 card on IRQ 2 (9) | `on`, `off` | Your setting (`on`) | `mpu = off` |
| `midi` | No | The synth the game was set up for in its setup program, which picks the MIDI port it plays through on desktop | `gm` (General MIDI), `mt32` (Roland MT-32) | Your setting (`gm`) | `midi = mt32` |
| `midi_port` | No | A MIDI port for this game, by its name on your computer. **Audio > MIDI Out > Game Port** writes it to the game's own settings; a `.gdox` for other computers is better without it | Any port name | The port `midi` picks | `midi_port = SC-55` |

The sound port is 220 (hexadecimal). The Sound Blaster 16 uses high DMA 5 and MIDI port 330. Geardox sets the
DOS `BLASTER` variable automatically to match the selected card, IRQ and DMA.

MIDI music plays through a synthesizer outside Geardox: a software synth or emulator that offers a MIDI port, or a
real module. On desktop, choose a port for each kind in **Audio > MIDI Out**; `midi` picks which one the game uses,
and `midi_port` can name any other. In RetroArch, turn on MIDI output in **Settings > Audio > MIDI**; `midi` and
`midi_port` have no effect there.

### `[input]`

Like `[machine]`, these settings follow your own Geardox settings when left out. On desktop, the joystick is
**Input > Emulated Joystick**.

| Key | Required | Description | Values | Default | Example |
| --- | --- | --- | --- | --- | --- |
| `mouse` | No | The mouse. The DOS mouse driver loads only when there is one | `none`, `ps2`, `serial` (COM1) | Your setting (`ps2`) | `mouse = serial` |
| `joystick` | No | The joystick on the game port, which the first gamepad drives with `use = joystick` in `[pad1]` | `none`, `standard` (2 buttons), `gamepad` (Gravis GamePad), `flightstick` (CH FlightStick Pro) | Your setting (`gamepad`) | `joystick = flightstick` |

### `[keys]`

Each line names one of your keys, as a USB keyboard reports it, whatever your layout. See [Remap keys](#remap-keys).
A line with a name Geardox doesn't know, or more than four PC keys, is ignored with a warning.

| Key | Required | Description | Values | Default | Example |
| --- | --- | --- | --- | --- | --- |
| A key of yours | No | The PC keys it presses, in order, released in reverse | One to four PC keys joined with `+`, or `none` | Its own PC key | `up = kp_8` |

Your keys: `a` to `z`, `0` to `9`, `f1` to `f24`, `return`, `escape`, `backspace`, `tab`, `space`, `minus`, `equals`,
`left_bracket`, `right_bracket`, `backslash`, `non_us_hash`, `semicolon`, `apostrophe`, `grave`, `comma`, `period`,
`slash`, `non_us_backslash`, `caps_lock`, `print_screen`, `scroll_lock`, `pause`, `insert`, `home`, `page_up`,
`delete`, `end`, `page_down`, `up`, `down`, `left`, `right`, `num_lock`, `kp_0` to `kp_9`, `kp_divide`,
`kp_multiply`, `kp_minus`, `kp_plus`, `kp_enter`, `kp_period`, `kp_equals`, `kp_comma`, `kp_00`, `kp_000`, `menu`,
`help`, `sysreq`, `international1` to `international5`, `lang1` to `lang5`, `left_ctrl`, `right_ctrl`, `left_shift`,
`right_shift`, `left_alt`, `right_alt`, `left_gui` and `right_gui` (Windows or Command).

PC keys: the same names for the keys a PC keyboard has, `left_gui`, `right_gui` and `menu` being its Windows keys,
plus `sysrq` (Alt+Print Screen). A PC has no `f13` to `f24`, `kp_equals`, `kp_comma`, `kp_00`, `kp_000`, `help`,
`international` or `lang` keys, and `non_us_hash` is its `backslash`.

### `[pad1]` and `[pad2]`

The first and second gamepads, on desktop only. See [Use a gamepad](#use-a-gamepad). Settings left out follow your
own, from **Input > Host Gamepad 1** and **Host Gamepad 2**. A button with a name Geardox doesn't know is ignored with a warning.

| Key | Required | Description | Values | Default | Example |
| --- | --- | --- | --- | --- | --- |
| `use` | No | What the gamepad drives | `keys` (keyboard and mouse); `joystick` in `[pad1]`, `off` in `[pad2]` | Your setting (`keys`) | `use = joystick` |
| `mouse` | No | What moves the PC's mouse. It sends no keys while it does | `off`, `left_stick`, `right_stick`, `dpad` | Your setting (`left_stick`) | `mouse = off` |
| A button | No | The PC key it presses, or a mouse button | A PC key as in `[keys]`, `mouse_left`, `mouse_right`, `mouse_middle`, or `none` | Your setting | `a = space` |

Buttons, by their place on the gamepad as on an Xbox one: `a`, `b`, `x`, `y`, `dpad_up`, `dpad_down`, `dpad_left`,
`dpad_right`, `l1`, `r1`, `l2`, `r2`, `l3`, `r3`, `back`, `start`, `left_stick_up`, `left_stick_down`,
`left_stick_left`, `left_stick_right`, `right_stick_up`, `right_stick_down`, `right_stick_left` and
`right_stick_right`.

### `[dos]`

| Key | Required | Description | Values | Default | Example |
| --- | --- | --- | --- | --- | --- |
| `memory` | No | The DOS memory manager; see [Memory modes](#memory-modes) | `xms`, `ems`, `umb`, `none` | `xms`, or `ems` when `ems_kb` is set | `memory = umb` |
| `ems_kb` | No | The expanded memory (EMS) the game needs, in KB. Turns on EMS unless `memory` is set, and raises the RAM if needed when `ram_mb` is left out | `16` to `32768` | None: no EMS | `ems_kb = 1024` |
| `ems_frame` | No | The address of the EMS page frame | `auto`, `none`, or a hexadecimal segment from `C000` to `E000` in steps of `0400` | `auto` | `ems_frame = D000` |
| `files` | No | DOS `FILES=`: how many files can be open at once | `8` to `255` | `40` | `files = 60` |
| `buffers` | No | DOS `BUFFERS=`: the number of disk buffers | `1` to `99` | `20` | `buffers = 30` |
| `system` | No | The DOS the game runs on; see [Use your own MS-DOS](#use-your-own-ms-dos) | `freedos`, `msdos` | MS-DOS when imported, FreeDOS otherwise | `system = freedos` |
| `version` | No | The DOS version FreeDOS reports to programs, for games that check it. Ignored on MS-DOS, which has only `SETVER` | `x.yy` | The DOS's own | `version = 5.00` |
| `system_files` | No | Whether the DOS's files in the root of C: appear in `DIR` | `visible`, `hidden` | `visible` | `system_files = hidden` |
| `keyboard` | No | The keyboard layout DOS uses for this game, overriding your own setting; see [Keyboard layout](#keyboard-layout) | `us`, `uk`, `fr`, `gr` (German), `it`, `sp` (Spain), `la` (Latin America), `be`, `nl`, `sv`, `su` (Finland), `sf` and `sg` (Swiss French and German), `br` | Your setting (`us`) | `keyboard = sp` |
| `cache` | No | Loads the DOS's disk cache: SMARTDRV on MS-DOS, LBACACHE on FreeDOS. It caches reads only, so a save is never held back, and uses 1 to 2 MB of extended memory. Not loaded with `memory = none` | `off`, `on` | `off` | `cache = on` |

### `[env]`

Each line sets a DOS environment variable. They are applied after Geardox's own variables, so they can also
replace values such as `BLASTER`.

| Key | Required | Description | Values | Default | Example |
| --- | --- | --- | --- | --- | --- |
| Any variable name | No | `NAME = value` becomes `SET NAME=value` | Text | None | `DOS4G = QUIET` |

### `[config_sys]` and `[autoexec]`

These advanced sections hold DOS startup lines instead of `key = value` settings. Each line is used as written; use
`REM` for comments.

| Section | Required | Contents | Where the lines go | Example |
| --- | --- | --- | --- | --- |
| `[config_sys]` | No | `CONFIG.SYS` lines | `C:\CONFIG.SYS`, before its `SHELL=` line | `STACKS=9,128` |
| `[autoexec]` | No | DOS commands | `C:\AUTOEXEC.BAT`, run in the game's folder before the game starts | `LH MYDRIVER.COM` |

## Advanced DOS Details

### Keyboard Layout

Geardox sends the PC the position of each key you press, as a real keyboard does. DOS assumes a United States
keyboard, so on another layout symbols such as `:`, `/` and `-` land elsewhere until DOS is told the layout. Choose
yours in **Input > Keyboard > DOS Layout**, or with the **DOS Keyboard Layout (restart)** option in RetroArch. Geardox
then loads KEYB with it in each game's `AUTOEXEC.BAT`, on FreeDOS or MS-DOS, from the next game opened. KEYB loads into
upper memory when there is some, and costs no conventional memory then.

`keyboard` under `[dos]` sets a layout for one game, over your setting. The layouts are the ones both DOSes support with
the VGA's own character set, which has the letters and accents of these languages, such as ñ, ç, ü or å.

Games that read the keyboard directly, as most action games do, use the keys' positions and aren't affected. The
layout matters at the DOS prompt and in games that read typed text.

The FreeDOS disk, `FreeDOS.hdd`, is yours and Geardox doesn't change it. Add a line such as
`KEYB SP,437,C:\FREEDOS\BIN\KEYBOARD.SYS`, with your layout's code, to its `FDAUTO.BAT` once.

### Memory Modes

`memory` in `[dos]` selects how DOS makes memory available to games. Each DOS uses its own memory managers:

| Value | Use | FreeDOS | MS-DOS |
| --- | --- | --- | --- |
| `xms` | Default. Extended memory (XMS), suitable for DOS/4GW and many other games. | HIMEMX | HIMEM |
| `ems` | Expanded memory (EMS), XMS and upper memory. | JEMMEX | HIMEM and EMM386 `RAM` |
| `umb` | XMS and upper memory, with EMS disabled. | JEMMEX `NOEMS` | HIMEM and EMM386 `NOEMS` |
| `none` | No memory manager. | | |

Upper memory lets DOS load drivers outside the conventional memory area used by many games. `umb` and `ems`
use V86 mode and provide VCPI; if a game refuses V86 mode, try `xms` or `none`.

Free conventional memory for a game, with the mouse and CD drivers loaded:

| `memory` | FreeDOS | MS-DOS 6.22 | MS-DOS 5.0 |
| --- | --- | --- | --- |
| `none` | 498 KB | 567 KB | 568 KB |
| `xms` | 606 KB | 585 KB | 606 KB |
| `umb` | 620 KB | 615 KB | 603 KB |
| `ems` | 620 KB | 615 KB | 609 KB |

`ems_kb = N` is a request for enough memory, not a cap on the EMS DOS exposes. Available EMS is RAM minus about
1.5 MB on either DOS, with another 1 MB reserved when using at least 16 MB of RAM and a Cirrus card. On MS-DOS,
extended memory stops below that 1 MB, so EMS doesn't grow past about 13.5 MB however much RAM there is. An explicit
`ram_mb` setting prevents automatic RAM selection, and an explicit `memory` setting takes precedence over the mode
implied by `ems_kb`.

### Files on the DOS C: Drive

With `folder = C:\ORION`, the prepared drive contains, on FreeDOS:

```text
C:\KERNEL.SYS     FreeDOS kernel
C:\CONFIG.SYS     DOS configuration generated from the descriptor
C:\AUTOEXEC.BAT   Startup commands generated from the descriptor
C:\FREEDOS\BIN    DOS commands, drivers and tools
C:\TEMP           Temporary files
C:\ORION          Your game's files
```

and on MS-DOS:

```text
C:\IO.SYS         MS-DOS system files, hidden as MS-DOS Setup leaves them
C:\MSDOS.SYS
C:\COMMAND.COM    MS-DOS command interpreter
C:\WINA20.386     As MS-DOS Setup installs it
C:\CONFIG.SYS     DOS configuration generated from the descriptor
C:\AUTOEXEC.BAT   Startup commands generated from the descriptor
C:\DOS            Everything MS-DOS Setup installs there
C:\GEARDOX        Geardox's tools: the mouse and CD drivers, and the programs that start the game and power off
C:\TEMP           Temporary files
C:\ORION          Your game's files
```

`system_files = hidden` under `[dos]` hides these files and folders from a normal `DIR` listing. They remain
available and can be seen with `DIR /A`.

Geardox writes `CONFIG.SYS` and `AUTOEXEC.BAT` again whenever it prepares the disk. If a game or its installer changes
them, the change takes effect when the PC restarts during that session, and is gone the next time the game opens.
Geardox then lists the lines the game added, so you can copy the ones it needs into `[config_sys]` and `[autoexec]`,
which is how lasting startup changes are made.

With `folder = C:\`, game files share the root of C: with DOS. Game entries named like DOS's own files or folders
are left out, including a game's own `CONFIG.SYS` and `AUTOEXEC.BAT`. Using a named game folder avoids that overlap.
A game folder can't be one of DOS's folders: `FREEDOS`, `DOS`, `GEARDOX` or `TEMP`.

For a game designed to boot directly from a floppy, set `floppy_a = disk.img` under `[drives]` and `boot = a` under
`[run]`.

### Save Recovery and File Conflicts

The prepared hard disk is a working copy. Geardox copies changes from the game's DOS folder back to the game
folder or separate save location when the session ends. Files written outside `folder` are not kept, and Geardox
reports them. Hard disks added with `hdd_primary_slave` or `hdd_secondary_slave` keep their changes in their own
disk images.

Separate saves are stored in a folder named `<game>.<id>.gdox`. A `.deleted` file records files the game removed,
so those deletions are also applied the next time it runs. New files use their DOS names; changed files retain
their original names on your computer.

If you change a file on your computer while the game is running, Geardox avoids overwriting it with the game's
copy. It keeps the conflicting copy in `.conflicts/` in the save folder and reports the conflict.

After an unexpected shutdown, Geardox attempts to recover the previous session's changes the next time you open
that game. If the disk is damaged, it keeps the disk image with a `.refused` suffix, reports the problem, and starts
again from the game source instead of copying potentially damaged files back.
