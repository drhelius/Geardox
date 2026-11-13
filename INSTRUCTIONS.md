# Getting Started with Geardox

Geardox is an early-90s PC in a window: a 386 or 486 with a Sound Blaster, VGA or SVGA graphics, floppy drives,
hard disks and a CD-ROM drive. A real PC needs DOS to run games, so Geardox comes with **FreeDOS built in**. You never
have to install DOS yourself. If you own MS-DOS, games can run on it instead: see
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

Geardox builds a fresh C: drive with the game and FreeDOS on it, turns the PC on and starts the game. When you quit the
game, you're at the DOS prompt. Your saves go back into the game's folder, so they're there next time. A game in a ZIP
saves to Geardox's save folder instead, and the ZIP is never changed.

If the game has no `.gdox` yet:

1. Choose **Game > New Game Descriptor...**.
2. Pick the game's folder.
3. Pick the program that starts it, such as `GAME.EXE` or `PLAY.BAT`.

Geardox writes the `.gdox` and starts the game. Next time, it's in **Game > Recent Games**.

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

Geardox turns the PC on when it opens. If no hard disk is attached, it uses the **FreeDOS disk** as C: and boots it,
and you get a DOS prompt: `C:\>`.

The FreeDOS disk is a file, `FreeDOS.hdd`, in [Geardox's settings folder](#where-geardox-keeps-its-files). It's a
hard disk of about 500 MB. Geardox makes it the first time and then it's yours: anything you copy or install on it
stays there, session after session. Updating Geardox never touches it.

The disk comes ready for games:

- The mouse driver and the CD-ROM driver load at every boot.
- `BLASTER` is set for the default Sound Blaster 16 (`A220 I5 D1 H5 P330`).
- Extended memory (XMS) is available through HIMEMX.

To start over, choose **Drives > Hard Disks > Recreate FreeDOS Disk...**. It makes a clean disk and erases everything
on the old one.

To stop using it, turn off **Drives > Hard Disks > Boot FreeDOS With No Disks**. With nothing attached, the PC then
has nothing to boot. **Drives > Hard Disks > Attach FreeDOS Disk** puts it back in any free position, for example next
to a disk of your own.

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
| A folder or ZIP with no `.gdox` | It becomes a second hard disk, **D:**, next to FreeDOS on C:. Type `D:` and run the game. DOS can write to D:, but those changes stay in Geardox's copy and your folder is never touched. |
| A hard disk image (`.hdd`, `.img`, fixed-size `.vhd`, `.chd`) | It **replaces FreeDOS as C:** and the PC boots from it. This is for a DOS PC you've already set up. If C: is already one of your own disks, the image takes the next free position. |
| A floppy image (`.img`, `.ima`, `.vfd`, `.flp`, `.dsk`) | It goes into drive A: right away, with no restart. |
| A ZIP of floppy images | One disk goes into A: and the next into B:. **Drives > Floppies > Drive 1 > Disk in Image** swaps in any other. |
| A CD image (`.cue`, `.iso`, `.chd`) or an `.m3u` list | It goes into the CD drive right away. Under FreeDOS it gets the next free letter, D: or E:. |

Where the changes go:

| Disk | What DOS writes |
| --- | --- |
| `FreeDOS.hdd` and your `.hdd`, `.img` and `.vhd` images | Written into the image itself. |
| `.chd` hard disks and floppy images | Kept in a working copy in the save folder. The original file stays as it was. Use **Save As...** in the drive's menu to write a floppy out. |
| A folder or ZIP attached as a disk | Kept in Geardox's copy of the folder. If the folder changes on your computer, **Rebuild From Folder...** in the disk's menu picks that up and drops what DOS wrote. |
| A game opened with a `.gdox` | Copied back to the game's folder, or to the save folder, when the game ends. |

A folder becomes a FAT16 disk, which holds up to 2015 MB. Files buried too deep for DOS to reach are left out, and
Geardox tells you.

## Installing a Game from Floppies or a CD

You can install a game onto the FreeDOS disk, as on a real PC:

1. Start Geardox with nothing attached, so you're at `C:\>`.
2. Drop the first install floppy, or a ZIP with all of them.
3. Type `A:` and then the installer's name, usually `INSTALL` or `SETUP`.
4. When it asks for the next disk, use **Drives > Floppies > Drive 1 > Disk in Image**, or eject and insert the next
   image.
5. Once it's done, go to the folder it installed to, for example `C:` and then `CD \GAMES\NAME`, and run the game.

For a CD, drop the image, type the CD's letter (`D:` with just the FreeDOS disk) and run its installer. Games that
play from the CD need it inserted every time you play.

The game stays on the FreeDOS disk, so it's there the next time you open Geardox.

## Two Rules That Explain Most Surprises

1. **Hard disks only change at a reset.** If the PC is running, a new hard disk waits, and the notice says "At the
   next reset". Use **Drives > Hard Disks > Reset to Apply Changes**, or **Geardox > Reset** (Ctrl+R). Floppies and
   CDs can be swapped at any time, like on a real PC.
2. **The PC tries the floppy first, then the hard disk.** A disk left in A: is tried first at a reset. To boot from
   something else, use **Geardox > Reset and Boot From > Floppy 1, Hard Disk or CD-ROM**. With
   **Emulator > Machine > BIOS Boot Menu** on, pressing Esc in the first seconds of a boot shows a boot menu.

## While a Game Is Open

The game takes over the PC. It has its own C: with its own copy of DOS, FreeDOS or your imported MS-DOS, so
`FreeDOS.hdd` isn't used, and anything you installed there isn't available. Your other hard disks are removed, and they don't come back on their own when the
game ends. Once no disk is attached, the next power on brings the FreeDOS disk back.

The game's `.gdox` can bring its own floppies, extra hard disks and CD. If your CD-ROM drive is set to a position the
game's hard disks use, it moves to the Secondary Master while the game is open.

**Game > Close** followed by the game's name turns the PC off and saves what the game changed. Opening another game,
or quitting Geardox, does the same. **Game > Last Save Report...** shows what was saved.

**Emulator > Machine** changes made while a game is open are remembered for that game only. So is the DOS chosen in
**Game > DOS**, where **Import MS-DOS...** and **Remove MS-DOS** also are.

## Machine Settings

**Emulator > Machine** chooses the PC. Most changes take effect at the next reset, and the menu offers
**Reset to Apply Changes**. The defaults suit most games:

| Setting | Default | Notes |
| --- | --- | --- |
| Model | 486DX2 at 66 MHz | The 386 models help with old games that run too fast or crash on a 486. |
| Main RAM | 8 MB | Some games want 16 MB. |
| Sound Card | Sound Blaster 16, IRQ 5, DMA 1 | Match it in the game's setup. |
| Video Card | Auto | Cirrus Logic SVGA, with VESA modes. |
| Mouse | PS/2 | |
| CD-ROM Drive | Secondary Master, 4x | |
| Drive Timing | Auto | Fast for games: seeks, the wait for a sector and a CD's spin-up take a sixteenth of the time. Real keeps the drives' mechanics. |

**Turbo** works at once: off runs the CPU at a third of its speed, for games that are too fast. It's Ctrl+T when the
keyboard isn't captured, and a "TURBO OFF" badge shows while it's off.

If you change the sound card's IRQ or DMA, the FreeDOS disk doesn't follow. Edit the `SET BLASTER` line with
`EDIT C:\FDAUTO.BAT`. Games opened with a `.gdox` get the right `BLASTER` automatically.

## Keyboard, Mouse and Joystick

**The keyboard is captured by default.** Keys go to the PC, so Ctrl+C and the like reach DOS, and only the Quit,
Fullscreen (F12), Capture Mouse and Capture Keyboard hotkeys work. Press **Ctrl+Shift+K** to release the keyboard and
get every hotkey back, such as Ctrl+S and Ctrl+L for save states or Ctrl+R to reset. Press it again to capture.
**Input > Keyboard > Paste as Keystrokes** types the clipboard into the PC.

**If your keyboard isn't a United States one,** choose its layout in **Input > Keyboard > DOS Layout**. Games then
get DOS set up for it, so `:`, `/` and the rest are where your keys say. See
[Keyboard layout](GDOX_README.md#keyboard-layout).

**The mouse** moves the PC's mouse while the pointer is over the screen. For games that need free movement, press
**Ctrl+Shift+M** to lock it inside the window, and again to let it go.

**A gamepad** drives the PC's joystick port. **Input > Joystick** picks what the game sees: a standard joystick, a
Gravis GamePad (the default) or a CH FlightStick Pro.

All hotkeys can be changed in **Emulator > Hotkeys**.

## Where Geardox Keeps Its Files

Settings, the FreeDOS disk and save files all live in one folder:

| System | Folder |
| --- | --- |
| Windows | `%APPDATA%\Geardome\Geardox` |
| macOS | `~/Library/Application Support/Geardome/Geardox` |
| Linux | `~/.local/share/Geardome/Geardox` |

To keep everything beside the program instead, start it with `--portable`, or put an empty file named `portable.ini`
next to it. On macOS, it goes next to the `.app`.

In this folder:

- `FreeDOS.hdd` is the FreeDOS disk.
- `msdos.zip` is your imported MS-DOS, when you've imported it.
- `<name>.<id>.geardox.hdd` files are the disks made from folders and ZIPs.
- `config.ini` holds your settings.

**Emulator > Directories** moves save files, save states, screenshots and recordings somewhere else.

## Quick Fixes

| Problem | Try |
| --- | --- |
| A hard disk I attached isn't there | Reset. Hard disks only attach at a reset. |
| The PC boots from a floppy I forgot | Eject it from **Drives > Floppies**, or use **Geardox > Reset and Boot From > Hard Disk**. |
| My FreeDOS games are missing | A game opened with a `.gdox` has its own C:. Close it and choose **Geardox > Power On** to get the FreeDOS disk back. |
| Hotkeys don't work | The keyboard is captured. Press Ctrl+Shift+K. |
| The game runs too fast | Turn off **Turbo** (Ctrl+T), or pick a 386 in **Emulator > Machine**. |
| No sound | Run the game's setup and choose Sound Blaster 16, port 220, IRQ 5, DMA 1. |
| The game asks for expanded memory (EMS) | Open it with a `.gdox` and set `ems_kb`. See [Memory](GDOX_README.md#memory). |
| The FreeDOS disk is broken | **Drives > Hard Disks > Recreate FreeDOS Disk...**. It erases what was on it. |
