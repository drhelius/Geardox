# Getting Started with Geardox

Geardox is an early-90s PC in a window: a 386 or 486 with a Sound Blaster, VGA or SVGA graphics, floppy drives,
hard disks and a CD-ROM drive. A real PC needs DOS to run games, so Geardox comes with **FreeDOS built in**. You never
have to install DOS yourself. If you own MS-DOS, import it once and games and the built-in disk run on it instead: see
[Use your own MS-DOS](GDOX_README.md#use-your-own-ms-dos).

- [Play a game](#play-a-game)
- [Starting with nothing attached](#starting-with-nothing-attached)
- [Using the DOS prompt](#using-the-dos-prompt)
- [Attaching disks and discs](#attaching-disks-and-discs)
- [Installing a game from floppies or a CD](#installing-a-game-from-floppies-or-a-cd)
- [Two rules that explain most surprises](#two-rules-that-explain-most-surprises)
- [While a game is open](#while-a-game-is-open)
- [Machine settings](#machine-settings)
- [Keyboard, mouse and joystick](#keyboard-mouse-and-joystick)
- [Where Geardox keeps its files](#where-geardox-keeps-its-files)
- [Quick fixes](#quick-fixes)

## Play a Game

The easiest way to play is with a **`.gdox` file**: a small text file that says where a game is, what to run and what
the PC needs. Drag it onto Geardox, or choose **Game > Open Game...**. If the `.gdox` sits inside the game's folder or
ZIP, you can drop the folder or ZIP instead, or use **Game > Open Game Folder...**.

Geardox builds a fresh C: drive with the game and DOS on it, turns the PC on and starts the game. When you quit the
game, you're at the DOS prompt. Your saves go back into the game's folder, so they're there next time. A game in a ZIP
saves to Geardox's save folder instead, and the ZIP is never changed.

Many games need no `.gdox` at all. When a folder or ZIP has none, Geardox looks the game up in its built-in game
database and, when it recognises it, starts it with the right settings and says **Recognised from the game database**.
A `.gdox` in the folder always wins, and **Game > Use Game Database** turns recognition off. You can also open a game's
program, its `.EXE`, `.COM` or `.BAT`: Geardox opens the game in its folder, or runs that program if it doesn't know
the game. When a folder holds more than one game, such as a collection, Geardox asks which one to play.

If Geardox doesn't recognise the game and it has no `.gdox` yet:

1. Choose **Game > New Game Descriptor...**.
2. Pick the game's folder.
3. Pick the program that starts it, such as `GAME.EXE` or `PLAY.BAT`.

Geardox writes the `.gdox` into the game's folder and starts the game. Next time, open the folder again or pick it in
**Game > Recent Games**. If the folder is read only, or **Game > Save Games To** is **Geardox's Save Folder**, the
`.gdox` goes to the save folder and Recent Games brings it back.

Many games need their setup program run once, to choose a sound card. **Game > Boot To** restarts the PC into one of
three places:

| Choice | What it does |
| --- | --- |
| **Game** | Starts the game. This is the normal choice. |
| **Setup** | Runs the game's setup program, when the `.gdox` names one. |
| **DOS Prompt** | Leaves you at the DOS prompt in the game's folder. |

With the default sound card, choose **Sound Blaster 16** in the game's setup: port `220`, IRQ `5`, DMA `1`, and high
DMA `5` if it asks.

Everything a `.gdox` can do, from CDs and floppies to memory and sound settings, is in
[Running DOS Games with GDOX Files](GDOX_README.md).

## Starting with Nothing Attached

Geardox turns the PC on when it opens. If no hard disk is attached, it uses a **built-in disk** as C: and boots it,
and you get a DOS prompt: `C:\>`. There are two, and both can live side by side:

- **The FreeDOS disk,** `FreeDOS.hdd`.
- **The MS-DOS disk,** `MS-DOS.hdd`, once you've imported your own MS-DOS (see
  [Use your own MS-DOS](GDOX_README.md#use-your-own-ms-dos)). It's then the one that boots.

**Drives > Hard Disks > Default Disk** chooses which one boots: your MS-DOS, FreeDOS, your own hard disk image picked
with **Choose Image...**, or **None**, which boots only floppies and CD-ROMs. If the old default is the only hard disk
attached, the new one takes its place at the next reset. If your own image is missing, FreeDOS boots instead.

Each is a file in [Geardox's settings folder](#where-geardox-keeps-its-files), a hard disk of about 500 MB. Geardox
makes it the first time it's needed and then it's yours: anything you copy or install on it stays there, session after
session. Updating Geardox never touches it, and neither does importing or removing MS-DOS.

Both disks come ready for games, with the same `CONFIG.SYS` and `AUTOEXEC.BAT` for their own DOS:

- The mouse driver and the CD-ROM driver load at every boot.
- `BLASTER` is set for the default Sound Blaster 16 (`A220 I5 D1 H5 P330`).
- Extended memory (XMS) and upper memory are on, and the drivers load high: about 615K is free on MS-DOS and 620K
  on FreeDOS. A program that won't run with a memory manager runs from a `.gdox` with `memory = xms`; see
  [Memory](GDOX_README.md#memory).
- KEYB loads your layout from **DOS > Keyboard Layout**, when it isn't United States. The disk takes the layout chosen
  when it's made.
- The PC rests while DOS waits for a key, so an idle prompt doesn't keep your computer busy.

To start over, choose **DOS > Recreate Built-in MS-DOS Disk...** or **DOS > Recreate Built-in FreeDOS Disk...**. It
makes a clean disk and erases everything on the old one. A new MS-DOS disk uses the MS-DOS you imported last.

Each position in **Drives > Hard Disks** has **Attach Built-in MS-DOS** and **Attach Built-in FreeDOS**, to put one
there, for example next to a disk of your own, or both together to copy games from one to the other.

## Using the DOS Prompt

DOS works by typing commands and pressing Enter. These are enough to get around:

| Type | What it does |
| --- | --- |
| `D:` | Switches to drive D:. Use `A:` for the floppy and `C:` for the hard disk. |
| `DIR` | Lists the files in the current folder. `DIR /P` stops at each screen. |
| `CD GAMES` | Enters the `GAMES` folder. `CD \` goes back to the top of the drive. |
| `GAME` | Runs `GAME.EXE`, `GAME.COM` or `GAME.BAT` in the current folder. |
| `MD GAMES` | Makes a folder. |
| `XCOPY A:\*.* C:\GAMES\NAME /S /I` | Copies a disk, with its subfolders, to a new folder on the hard disk. |
| `EDIT FILE.TXT` | Opens a text editor. |
| `MEM` | Shows how much memory programs can use. |

DOS names are short: up to eight characters, a dot and three more, with no spaces. Geardox shortens longer names when
it copies a folder from your computer.

## Attaching Disks and Discs

Drag a file onto the window, or use the **Drives** menu. What happens depends on what you drop:

| You drop | What happens |
| --- | --- |
| A folder or ZIP with a `.gdox` | The game opens, as in [Play a game](#play-a-game). |
| A folder or ZIP with no `.gdox` | It becomes a second hard disk, **D:**, next to the built-in disk on C:. Type `D:` and run the game. DOS can write to D:, but those changes stay in Geardox's copy and your folder is never touched. |
| A hard disk image (`.hdd`, `.img`, fixed-size `.vhd`, `.chd`) | It **replaces the built-in disk as C:** and the PC boots from it. This is for a DOS PC you've already set up. If C: is already one of your own disks, the image takes the next free position. |
| A floppy image (`.img`, `.ima`, `.vfd`, `.flp`, `.dsk`) | It goes into drive A: right away, with no restart. |
| A ZIP of floppy images | One disk goes into A: and the next into B:. **Drives > Floppies > Drive 1 > Disk in Image** swaps in any other. |
| A CD image (`.cue`, `.iso`, `.chd`) or an `.m3u` list | It goes into the CD drive right away. On a built-in disk it gets the next free letter, D: or E:. |

Where the changes go:

| Disk | What DOS writes |
| --- | --- |
| `FreeDOS.hdd`, `MS-DOS.hdd` and your `.hdd`, `.img` and `.vhd` images | Written into the image itself. |
| `.chd` hard disks and floppy images | Kept in a working copy in the save folder. The original file stays as it was. Use **Save As...** in the drive's menu to write a floppy out. |
| A folder or ZIP attached as a disk | Kept in Geardox's copy of the folder. If the folder changes on your computer, **Rebuild From Folder...** in the disk's menu picks that up and drops what DOS wrote. |
| A game opened with a `.gdox` | Copied back to the game's folder, or to the save folder, when the game ends. |

A folder becomes a FAT16 disk, which holds up to 2015 MB. Files buried too deep for DOS to reach are left out, and
Geardox tells you.

## Installing a Game from Floppies or a CD

You can install a game onto the built-in disk, as on a real PC:

1. Start Geardox with nothing attached, so you're at `C:\>`.
2. Drop the first install floppy, or a ZIP with all of them.
3. Type `A:` and then the installer's name, usually `INSTALL` or `SETUP`.
4. When it asks for the next disk, use **Drives > Floppies > Drive 1 > Disk in Image**, or eject and insert the next
   image.
5. Once it's done, go to the folder it installed to, for example `C:` and then `CD \GAMES\NAME`, and run the game.

For a CD, drop the image, type the CD's letter (`D:` with just the built-in disk) and run its installer. Games that
play from the CD need it inserted every time you play.

The game stays on the built-in disk, so it's there the next time you open Geardox.

## Windows 3.11 and Windows 95

Geardox runs Windows for Workgroups 3.11 and Windows 95 with the Cirrus Logic cards' own accelerated drivers. Give
Windows a hard disk image of its own, attached as the Primary Master (C:). It's an ordinary hard disk: Windows writes
into the image, and everything you install stays there.

| Windows | Model | Main RAM | Video Card | Display driver |
| --- | --- | --- | --- | --- |
| Windows for Workgroups 3.11 | 486DX2 at 66 MHz | 16 MB | GD5428 VL-Bus | Cirrus Logic's GD5426/5428 Windows 3.1 driver |
| Windows 95 | Any Pentium | 32 MB | GD5446 PCI | Windows 95's own Cirrus Logic 5446 driver |

- **Windows 3.11:** install the Cirrus driver with **Main > Windows Setup > Options > Change System Settings**,
  choosing **Other display** in the Display list and typing the folder with the driver files. Change resolution and
  colours the same way, from the "GD5426/28" entries. Cirrus's SETRES utility only offers 640x480 in 16 colours for
  now.
- **Windows 95** finds the card by itself. Change resolution and colours in the desktop's **Properties > Settings**;
  a colour change needs the restart Windows offers. Windows 95 also runs on the GD5428, with its own Cirrus driver.
  **Shut Down** turns the PC off.
- **DirectX games** install DirectX from their own CD. DirectX 5's setup offers a newer Cirrus 5446 display driver:
  accept it and restart. If Windows then starts in Safe Mode, restart once more from the Start menu.

## Two Rules That Explain Most Surprises

1. **Hard disks only change at a reset.** If the PC is running, a new hard disk waits, and the notice says "At the
   next reset". Use **Drives > Hard Disks > Reset to Apply Changes**, or **Geardox > Reset** (Ctrl+Shift+R). Floppies and
   CDs can be swapped at any time, like on a real PC.
2. **The PC tries the floppy first, then the hard disk.** A disk left in A: is tried first at a reset. To boot from
   something else, use **Geardox > Reset and Boot From > Floppy 1, Hard Disk or CD-ROM**. With
   **Emulator > Machine > BIOS Boot Menu** on, pressing Esc in the first seconds of a boot shows a boot menu.

## While a Game Is Open

The game takes over the PC. It has its own C: with its own copy of DOS, FreeDOS or your imported MS-DOS, so the
built-in disks aren't used, and anything you installed there isn't available. Your other hard disks are removed, and
they don't come back on their own when the game ends. Once no disk is attached, the next power on brings the built-in
disk back.

The game's `.gdox` can bring its own floppies, extra hard disks and CD. If your CD-ROM drive is set to a position the
game's hard disks use, it moves to the Secondary Master while the game is open.

**Game > Close** followed by the game's name turns the PC off and saves what the game changed. Opening another game,
or quitting Geardox, does the same. **Game > Last Save Report...** shows what was saved.

**Emulator > Machine** changes made while a game is open are remembered for that game only, and Geardox says so. So
are **Video > CGA Output**, **Input > Emulated Joystick**, the **Host Gamepad** choices, **Audio > MIDI Out > Game
Synth** and the DOS chosen in **DOS > Game**. While the game is open, the menus show its settings in colour: orange
for your changes, violet for what its `.gdox` sets, blue for what the game database sets. **Game Default**, first in
each list, undoes one change, and **Game > Game Files > Forget This Game's Changes** undoes them all. The **DOS** menu
also has **Import MS-DOS...** and **Remove MS-DOS**.

## Machine Settings

**Emulator > Machine** chooses the PC. Most changes take effect at the next reset, and the menu offers
**Reset to Apply Changes**. The defaults suit most games:

| Setting | Default | Notes |
| --- | --- | --- |
| Model | 486DX2 at 66 MHz | The 386 models help with old games that run too fast or crash on a 486. The Pentiums, 75 to 200 MHz on a PCI board, are for games like Quake that want more speed. |
| Main RAM | 8 MB, 16 MB on the Pentiums | Some games want 16 MB. |
| Sound Card | Sound Blaster 16, IRQ 5, DMA 1 | Match it in the game's setup. |
| Gravis UltraSound | Off | A second sound card for games, trackers and demos made for it, at port 240 next to the Sound Blaster. The game's setup finds it on its own. Music played with Gravis's patches needs **Audio > Import UltraSound Software...** once: see [Use a Gravis UltraSound](GDOX_README.md#use-a-gravis-ultrasound). |
| MPU-401 | Intelligent | The MIDI port at 330 for General MIDI and MT-32 music. Intelligent is the MPU-401's own sequencer, which older MT-32 games need; UART Only is the plain port, for a game that misbehaves with it. The Sound Blaster 16 has its own; with the other cards it's a separate card on IRQ 2 (9). |
| Video Card | Auto | Cirrus Logic SVGA with 2 MB, VESA modes up to 1280x1024 and accelerated Windows drivers: the GD5428 on the 386s and 486s, the PCI GD5446 on the Pentiums. The IBM CGA and EGA are for games written for those cards. |
| Mouse | PS/2 | |
| CD-ROM Drive | Secondary Master, 4x | |
| Drive Timing | Auto | Fast for games: seeks, the wait for a sector and a CD's spin-up take a sixteenth of the time. Real keeps the drives' mechanics. |

**Turbo** works at once: off runs the CPU at a third of its speed, for games that are too fast. It's Ctrl+Shift+T, and
a "TURBO OFF" badge shows while it's off.

With the CGA, **Video > CGA Output** chooses the monitor: Auto shows composite colour when a game asks for it and RGB
otherwise, and Composite Monitor suits games drawn for a television. It applies at once.

If you change the sound card's IRQ or DMA, the built-in disks don't follow. Edit the `SET BLASTER` line with
`EDIT C:\AUTOEXEC.BAT`. The built-in disks also set up a Gravis UltraSound at IRQ 7 and DMA 3 when it's on, which
suits the Sound Blaster's defaults; the `GDXGUS` and `SET ULTRASND` lines are the ones to edit. Games opened with a `.gdox` get the right `BLASTER` automatically.

## MIDI Music

Games set up for **General MIDI** (including GS and XG) or a **Roland MT-32** send their music to the MPU-401 at port
`330`. Geardox passes it to a synthesizer outside it, so one has to be running: a software synth or emulator that
offers a MIDI port, the General MIDI synth Windows includes, or a real module on a USB MIDI interface.

Then in **Audio > MIDI Out**:

1. Choose the port for each kind of music: **General MIDI Port** for General MIDI, GS and XG synths, and **MT-32 Port**
   for an MT-32, CM-32L or LAPC-I, or an emulator of one. The lists are read again each time they open, so a synth
   started after Geardox shows up. With only one synth, set just its port: every game plays through it.
2. In **Game Synth**, pick what the game was set up for in its setup program, General MIDI or MT-32. With a game open,
   the choice is only that game's, and it can also be any port, for example an SC-55 for one game and an MU2000 for
   another. With no game open, it's the default for games. A `.gdox` can set it with `midi` or `midi_port` under
   `[sound]`.

The last line of the menu shows where the music is playing in green, or the missing port in red.

A synth that's closed and opened again gets its port back within two seconds, with the game's instruments sent again.
Pausing, fast-forwarding, rewinding and loading states stop hanging notes. If a game plays MIDI and its port isn't
set, Geardox says so.

The music plays in the synth's own audio: Geardox's volume sliders and recordings don't include it, and
**Audio > Mute MIDI** or **Mute All Audio** stop its notes.

## Keyboard, Mouse and Joystick

**Keys go to the PC** while Geardox's window has focus, so Ctrl+C and the like reach DOS. Geardox keeps only its
hotkeys, which use Ctrl+Shift or Ctrl with an F-key. On macOS, Cmd replaces Ctrl in all of them; on a Mac keyboard the
F-keys may need Fn. **Input > Keyboard > Paste as Keystrokes** types the clipboard into the PC.

**Game Focus is for playing.** Press **Ctrl+F10**, or go fullscreen with **Alt+Enter**. The mouse is then locked in
the window, and every hotkey is off except Ctrl+F10, Alt+Enter, Ctrl+F1 and Ctrl+F2, so even a game's own Ctrl and
F-key combinations reach it. The window title shows it's on. **Ctrl+F10** leaves it, and so do switching to another
window and leaving fullscreen.

| Hotkey | Does |
| --- | --- |
| Ctrl+F10 | Game Focus on and off |
| Alt+Enter | Fullscreen |
| Ctrl+F1, Ctrl+F2 | Keyboard mapper, key changes off and on |
| Ctrl+F9 | Quit (Cmd+Q on macOS) |
| Ctrl+Shift+S, Ctrl+Shift+L | Save state, load state |
| Ctrl+Shift+R, Ctrl+Shift+P | Reset, pause |

**If your keyboard isn't a United States one,** choose its layout in **DOS > Keyboard Layout**. Games then get DOS
set up for it, so `:`, `/` and the rest are where your keys say, and so does a built-in disk made after the change. See
[Keyboard layout](GDOX_README.md#keyboard-layout).

**To move a key,** press **Ctrl+F1** (**Cmd+F1** on macOS). The PC stops and a picture of its keyboard appears. Click a
PC key, then press the key of your keyboard that should drive it, on its own. The PC key stays selected for more keys of
yours; click it again to deselect it. Each PC key lists the keys that drive it, built-in ones first: click the **x** on
one to remove it, or right click the PC key to remove all but the built-in ones. Red keys are ones no key of yours
reaches. Press the same hotkey again, or click **Close**, to go back to the game. Changes are kept for that game only,
beside its other files. A descriptor can set them too, including one key that sends several, such as Ctrl, Alt and Del;
see [Keys](GDOX_README.md#remap-keys).

**Ctrl+F2** (**Cmd+F2** on macOS) turns the game's key changes off and on, for instance to type a character's name in
a game that moves on letters. Every game starts with them on.

**The mouse** moves the PC's mouse while the pointer is over the screen. For games that need free movement, Game Focus
locks it inside the window.

**A gamepad** presses the PC's keys and moves its mouse. Out of the box, the D-pad sends the cursor keys, the left
stick moves the mouse, A and B click, X is Space, Y is Ctrl, L1 is Alt, R1 is Shift, Back is Esc and Start is Enter.
**Input > Host Gamepad 1 > Key Mapping** changes them: click a button's row, then press the PC key or choose a mouse
button. **Mouse** in the same menu picks what moves the mouse, or nothing. During a game, these changes are for that
game only, and a descriptor can set them too; see [Gamepads](GDOX_README.md#use-a-gamepad). **Host Gamepad 2** works
the same, for games where two players share the keyboard.

**For joystick games,** set **Input > Host Gamepad 1 > Use As** to **Joystick**. That gamepad then drives the PC's
joystick port, and **Input > Emulated Joystick** picks what the game sees: a standard joystick, a Gravis GamePad (the
default) or a CH FlightStick Pro.

All hotkeys can be changed in **Emulator > Hotkeys**, and its **Reset to Defaults** brings these back.

## Where Geardox Keeps Its Files

Settings, the built-in disks and save files all live in one folder:

| System | Folder |
| --- | --- |
| Windows | `%APPDATA%\Geardome\Geardox` |
| macOS | `~/Library/Application Support/Geardome/Geardox` |
| Linux | `~/.local/share/Geardome/Geardox` |

To keep everything beside the program instead, start it with `--portable`, or put an empty file named `portable.ini`
next to it. On macOS, it goes next to the `.app`.

In this folder:

- `FreeDOS.hdd` is the built-in FreeDOS disk.
- `MS-DOS.hdd` is the built-in MS-DOS disk, made from your import the first time it's needed.
- `msdos.zip` is your imported MS-DOS, when you've imported it.
- `<name>.<id>.geardox.hdd` files are the disks made from folders and ZIPs.
- `<name>.<id>.gdox.hdd` files are the C: drives games build, and `<name>.<id>.gdox.ini` and `<name>.<id>.keys` keep
  each game's own settings and key map.
- `config.ini` holds your settings.

**Emulator > Directories** moves save files, save states, screenshots and recordings somewhere else. Save Files takes
the games' disks, settings and key maps with it.

## Quick Fixes

| Problem | Try |
| --- | --- |
| A hard disk I attached isn't there | Reset. Hard disks only attach at a reset. |
| The PC boots from a floppy I forgot | Eject it from **Drives > Floppies**, or use **Geardox > Reset and Boot From > Hard Disk**. |
| The games I installed are missing | A game opened with a `.gdox` has its own C:. Close it and choose **Geardox > Power On** to get the built-in disk back. Games installed on the FreeDOS disk aren't on the MS-DOS one: **Drives > Hard Disks > Default Disk** switches back, or attach both to copy them. |
| Hotkeys don't work | Game Focus is on, as the window title says. Press Ctrl+F10. |
| The mouse stops at the window's edge | Press Ctrl+F10 for Game Focus, which locks it in the window. |
| The game runs too fast | Turn off **Turbo** (Ctrl+Shift+T), or pick a 386 in **Emulator > Machine**. |
| No sound | Run the game's setup and choose Sound Blaster 16, port 220, IRQ 5, DMA 1. |
| No music with General MIDI or MT-32 | Start the synth and choose its port in **Audio > MIDI Out**. See [MIDI Music](#midi-music). |
| The game asks for expanded memory (EMS) | Open it with a `.gdox` and set `ems_kb`. See [Memory](GDOX_README.md#memory). |
| A built-in disk is broken | **DOS > Recreate Built-in MS-DOS Disk...** or **Recreate Built-in FreeDOS Disk...**. It erases what was on it. |
