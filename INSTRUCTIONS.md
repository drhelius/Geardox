# Getting Started with Geardox

Geardox is a 90s PC in a window: a 386, 486 or Pentium with a Sound Blaster, CGA, EGA, VGA or SVGA graphics, floppy
drives, hard disks and a CD-ROM drive. A real PC needs DOS to run games, so Geardox comes with **FreeDOS built in**.
You never have to install DOS yourself. If you own MS-DOS, import it once and games and the built-in disk run on it
instead: see [Use your own MS-DOS](GDOX_README.md#use-your-own-ms-dos).

- [Play a game](#play-a-game)
- [Starting with nothing attached](#starting-with-nothing-attached)
- [Using the DOS prompt](#using-the-dos-prompt)
- [Attaching disks and discs](#attaching-disks-and-discs)
- [Installing a game from floppies or a CD](#installing-a-game-from-floppies-or-a-cd)
- [Windows 3.11 and Windows 95](#windows-311-and-windows-95)
- [Two rules that explain most surprises](#two-rules-that-explain-most-surprises)
- [While a game is open](#while-a-game-is-open)
- [Machine settings](#machine-settings)
- [MIDI music](#midi-music)
- [Keyboard, mouse and joystick](#keyboard-mouse-and-joystick)
- [Where Geardox keeps its files](#where-geardox-keeps-its-files)
- [Quick fixes](#quick-fixes)

## Play a Game

The easiest way to play is with a **`.gdox` file**: a small text file that says where a game is, what to run and what
the PC needs. Drag it onto Geardox, or choose **Game > Open Game...**. If the `.gdox` sits inside the game's folder or
ZIP, you can drop the folder or ZIP instead, or use **Game > Open Game Folder...**.

Geardox makes a C: drive from DOS and the game's folder, reading the game's files where they are, turns the PC on and
starts the game. Quitting the game leaves you at the DOS prompt. When the game closes, because you close it, open
another game or quit Geardox, what DOS changed is written back: the game's saves go into its folder, so they're there
next time, and everything else on C:, `AUTOEXEC.BAT` and `CONFIG.SYS` included, is kept for that game in the
[Save Files folder](#where-geardox-keeps-its-files). A game in a ZIP is unpacked to a temporary folder while it runs,
and its saves are kept in the Save Files folder too, so the ZIP is never changed.

Many games need no `.gdox` at all. When a folder or ZIP has none, Geardox looks the game up in its built-in game
database and, when it recognises it, starts it with the right settings and says **Recognised from the game database**.
A `.gdox` in the folder always wins, and **Game > Use Game Database** turns recognition off. When a folder holds more
than one game, such as a collection, Geardox asks which one to play. A folder or ZIP Geardox doesn't recognise opens the
same way, at the DOS prompt in its folder on C:, so you can run the game yourself. You can also open a game's program,
its `.EXE`, `.COM` or `.BAT`: Geardox opens the game in its folder, or runs that program if it doesn't know the game.

To make a game Geardox doesn't recognise start by itself:

1. Open its folder. The game starts at the DOS prompt.
2. Choose **Game > Game Files > Save Descriptor...**.
3. Pick the program that starts it, such as `GAME.EXE` or `PLAY.BAT`, and save the `.gdox` where Geardox suggests.

Geardox opens the game again from its new `.gdox`, with everything it had saved. From then on, opening the folder or
picking it in **Game > Recent Games** starts the game. Geardox suggests the game's folder, or the game's own folder in
the Save Files folder when the game is in a ZIP or a read-only folder, or when **Game > Save Games To** is
**The Save Files Folder**; Recent Games brings that one back. **Save Descriptor...** works for any open game, and the
`.gdox` it writes holds your changes for the game and its key map.

Many games need their setup program run once, to choose a sound card. **Game > Boot To** restarts the PC into one of
these:

| Choice | What it does |
| --- | --- |
| **Game** | Starts the game. This is the normal choice. |
| **3dfx Voodoo** | Starts the game's 3dfx program with the 3dfx Voodoo fitted, for a game that has one. The game opens again, and Geardox keeps the choice for it. |
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

**Drives > Hard Disks > Default Disk** chooses which one boots: your MS-DOS, FreeDOS, **Custom**, your own hard disk
image picked with **Choose Custom Image...**, or **None**, which boots only floppies and CD-ROMs. If the old default is
the only hard disk attached, the new one takes its place at the next reset. If your own image is missing, FreeDOS
boots instead.

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

DOS names are short: up to eight characters, a dot and three more, with no spaces. Geardox shortens longer names on
the drives it makes from your folders.

## Attaching Disks and Discs

Drag a file onto the window, or use the **Drives** menu. A disc or floppy can go in while another one is in the drive:
Geardox takes the old one out first. What happens depends on what you drop:

| You drop | What happens |
| --- | --- |
| A folder or ZIP | It opens as a game, as in [Play a game](#play-a-game): started for you when it has a `.gdox` or the game database knows it, at the DOS prompt in its folder on C: otherwise. |
| A hard disk image (`.hdd`, an `.img` larger than a floppy, fixed-size `.vhd`, `.chd`) | It **replaces the built-in disk as C:** and the PC boots from it. This is for a DOS PC you've already set up. If C: is already one of your own disks, the image takes the next free position. |
| A floppy image (`.img`, `.ima`, `.vfd`, `.flp`, `.dsk`, `.xdf`, `.mfm`, `.d77`, `.d88`, `.hdm`) | It goes into drive A: right away, with no restart. |
| A ZIP of floppy images | One disk goes into A: and the next into B:. **Drives > Floppies > Drive 1 > Disk in Image** swaps in any other. |
| A CD image (`.cue`, `.iso`, `.chd`) or an `.m3u` list | It goes into the CD drive right away. On a built-in disk it gets the next free letter, D: or E:. |

To use a folder or ZIP as a hard disk rather than a game, choose **Attach Folder...** in one of the positions of
**Drives > Hard Disks**, such as **Primary Slave**.

Where the changes go:

| Disk | What DOS writes |
| --- | --- |
| `FreeDOS.hdd`, `MS-DOS.hdd` and your `.hdd`, `.img` and `.vhd` images | Written into the image itself. |
| `.chd` hard disks | Kept in a working copy in the Save Files folder. The original file stays as it was. |
| Floppy images | Kept in a working copy in the Save Files folder. **Drives > Floppies > Drive 1 > Remember Changes** can write them into the image instead, or insert disks write protected. Use **Save As...** in the drive's menu to write a floppy out. |
| A folder or ZIP attached as a hard disk | Written into the folder when the disk is removed or Geardox quits. A ZIP, or a folder Geardox can't write to, keeps its changes in its own folder in the Save Files folder. |
| A game | Written back when the game closes, as in [Play a game](#play-a-game). |

A folder becomes a FAT16 disk, which holds up to 2015 MB. Files buried too deep for DOS to reach are left out, and
Geardox tells you.

**Save Image As...** in a position of **Drives > Hard Disks** copies the disk there, as it is now, to a new `.hdd` or
`.img` file. It works for any disk, a folder, a ZIP and a game's C: included, and the disk in use stays the same. For
the open game's C:, **Game > Game Files > Save Hard Disk Image...** also saves a `.gdox` next to the image, which plays
it with the game's settings: see [Use a hard disk image](GDOX_README.md#use-a-hard-disk-image). The image holds DOS,
the game and everything it has saved so far, and it boots and starts the game on its own. Geardox has to be able to read
all of the disk's files, and the image can't go inside a folder that is a disk. Its size is fixed: for more free space
in it, set `free_mb` in the game's `.gdox` before you save it.

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
  colours the same way, from the "GD5426/28" entries, or with Cirrus's SETRES utility, which lists every mode the
  card's 2 MB can show.
- **Windows 95** finds the card by itself. Change resolution and colours in the desktop's **Properties > Settings**;
  a colour change needs the restart Windows offers. Windows 95 also runs on the GD5428, with its own Cirrus driver.
  **Shut Down** turns the PC off.
- **DirectX games** install DirectX from their own CD. DirectX 5's setup offers a newer Cirrus 5446 display driver:
  accept it and restart. If Windows then starts in Safe Mode, restart once more from the Start menu.
- **3dfx Voodoo:** turn on **Emulator > Machine > Video Card > 3dfx Voodoo Graphics** and reset. Windows 95 finds a
  "PCI Early non-VGA Device" and asks for its driver: unpack 3dfx's Voodoo Graphics driver kit for Windows 95 (3.01.00
  works) to a folder, choose **Other Locations...** and type that folder, and type it again when Windows asks for the
  "Voodoo Graphics Drivers" disk. After the restart, the desktop's **Properties** has a **3dfx Voodoo Graphics** tab.

## Two Rules That Explain Most Surprises

1. **Hard disks only change at a reset.** If the PC is running, a new hard disk waits, and the notice says "At the
   next reset". Use **Drives > Hard Disks > Reset to Apply Changes**, or **Geardox > Reset** (Ctrl+Shift+R).
   Floppies and CDs can be swapped at any time, like on a real PC.
2. **The PC tries the floppy first, then the hard disk.** A disk left in A: is tried first at a reset. To boot from
   something else, use **Geardox > Reset and Boot From > Floppy 1, Floppy 2, Hard Disk or CD-ROM**. With
   **Emulator > Machine > BIOS Boot Menu** on, pressing Esc in the first seconds of a boot shows a boot menu.

## While a Game Is Open

The game takes over the PC. It has its own C: with its own DOS, FreeDOS or your imported MS-DOS, so the built-in disks
aren't used, and anything you installed there isn't available. Your other hard disks are removed, and they don't come
back on their own when the game ends. Once no disk is attached, the next power on brings the built-in disk back.

The game's `.gdox` can bring its own floppies, extra hard disks and CD. If your CD-ROM drive is set to a position the
game's hard disks use, it moves to the Secondary Master while the game is open.

The game closes, and what it changed is written back, when you choose **Game > Close** followed by its name, open
another game or quit Geardox, or when the game turns the PC off itself. **Geardox > Power Off** only turns the PC off:
the game stays open, and its changes wait until it closes. **Game > Last Save Report...** shows what was written.

**Emulator > Machine** changes made while a game is open are remembered for that game only, and Geardox says so. So
are **Video > CGA Output**, **Input > Emulated Joystick**, the **Input > Host Gamepad** choices,
**Audio > MIDI Out > Game Music**, **Audio > Built-in Synths > MT-32 > Model**, the volumes and mutes in **Audio** and
the DOS chosen in **DOS > Game**. While the game is open, the menus show its settings in colour: orange for your
changes, violet for what its `.gdox` sets, blue for what the game database sets. **Game Default**, first in each list,
undoes one change. **Game > Game Files > Forget This Game's Changes** undoes them all, and drops your edits to the
game's `AUTOEXEC.BAT` and `CONFIG.SYS` from the next time it opens. **DOS > MS-DOS** imports or removes your MS-DOS and
shows the one imported.

## Machine Settings

**Emulator > Machine** chooses the PC. Most changes take effect at the next reset, and the menu offers
**Reset to Apply Changes**. The defaults suit most games:

| Setting | Default | Notes |
| --- | --- | --- |
| Model | 486DX2 at 66 MHz | The 386 models help with old games that run too fast or crash on a 486. The Pentiums, 75 to 200 MHz on a PCI board, are for games like Quake that want more speed. |
| Main RAM | 8 MB, 16 MB on the Pentiums | Some games want 16 MB. |
| Sound Card | Sound Blaster 16, IRQ 5, DMA 1 | Match it in the game's setup. |
| Gravis UltraSound | Off | A second sound card for games, trackers and demos made for it, at port 240 next to the Sound Blaster. The game's setup finds it on its own. Music played with Gravis's patches needs **Audio > Gravis UltraSound > Import Software...** once: see [Use a Gravis UltraSound](GDOX_README.md#use-a-gravis-ultrasound). |
| MPU-401 | Intelligent | The MIDI port at 330 for General MIDI and MT-32 music. Intelligent is the MPU-401's own sequencer, which older MT-32 games need; UART Only is the plain port, for a game that misbehaves with it. The Sound Blaster 16 has its own; with the other cards it's a separate card on IRQ 2 (9). |
| Video Card | Auto | Cirrus Logic SVGA with 2 MB, VESA modes up to 1280x1024 and accelerated Windows drivers: the GD5428, ISA on the 386s and VL-Bus on the 486s, and the PCI GD5446 on the Pentiums. The IBM CGA and EGA are for games written for those cards. |
| 3dfx Voodoo Graphics | Off | A second PCI card with 4 MB for DOS games with 3dfx support, on the Pentiums. The game database turns it on for a game that only runs on one, and **Game > Boot To > 3dfx Voodoo** fits it for a game's 3dfx program. Games that load `GLIDE2X.OVL` need **Video > 3dfx Voodoo Driver > Import Driver...** once: see [Use a 3dfx Voodoo](GDOX_README.md#use-a-3dfx-voodoo). |
| Mouse | PS/2 | |
| CD-ROM Drive | Secondary Master, 4x | |
| Drive Timing | Auto | Fast for games and Real otherwise. Fast takes a sixteenth of the time for seeks, the wait for a sector and a CD's spin-up; Real keeps the drives' mechanics. |

**Turbo** works at once: off runs the CPU at a third of its speed, for games that are too fast. It's Ctrl+Shift+T, and
a "TURBO OFF" badge shows while it's off.

With the CGA, **Video > CGA Output** chooses the monitor: Auto shows composite colour when a game asks for it and RGB
otherwise, and Composite Monitor suits games drawn for a television. It applies at once.

If you change the sound card's IRQ or DMA, the built-in disks don't follow. Edit the `SET BLASTER` line with
`EDIT C:\AUTOEXEC.BAT`. The built-in disks also set up a Gravis UltraSound at IRQ 7 and DMA 3 when it's on, which
suits the Sound Blaster's defaults; the `GDXGUS` and `SET ULTRASND` lines are the ones to edit. Games get the right
`BLASTER` and `ULTRASND` automatically.

## MIDI Music

Games set up for **General MIDI** (including GS and XG) or a **Roland MT-32** send their music to the MPU-401 at port
`330`. Geardox plays it on its own built-in synths, or sends it to a synthesizer outside it: a software synth that
offers a MIDI port, the General MIDI synth Windows includes, or a real module on a USB MIDI interface.

**The built-in synths** need files that Geardox doesn't include. Set them up in **Audio > Built-in Synths**, where
each synth's last line shows what it found:

- **MT-32:** the control and PCM ROMs of an MT-32 or CM-32L, which you have to dump from your own unit. Put them in
  the `mt32` folder inside Geardox's settings folder, or choose any folder with **Custom Location** in
  **MT-32 > ROMs**. The files are known by their contents, so their names don't matter, and ROMs split in two halves
  are joined. **MT-32 > Model** picks the unit: Auto plays a CM-32L when its ROMs are there, which plays everything
  an MT-32 does plus the sound effects of later Sierra games, then an MT-32.
- **General MIDI:** a sound bank (a SoundFont file, `.sf2` or `.sf3`), such as the free GeneralUser GS or FluidR3_GM.
  Put it in the `soundfonts` folder inside Geardox's settings folder, or in the system's usual place
  (`~/Library/Audio/Sounds/Banks` on macOS, `/usr/share/soundfonts`, `/usr/share/sounds/sf2` or
  `/usr/local/share/soundfonts` on Linux, `soundfonts` next to Geardox on Windows), or choose a file with
  **Custom Location** in **General MIDI > Sound Bank**. The first one by name is used.

With the files in place, MT-32 and General MIDI games play with no setup.

**Audio > MIDI Out** chooses where the music goes:

1. **General MIDI** and **MT-32** set the output for games set up for each synth: **Built-in Synth**, **External
   Port**, or **None**. With External Port, **Port** lists the MIDI ports on your computer; the list is read again
   each time it opens, so a synth started after Geardox shows up. A synth with no output, or whose built-in synth
   has no files, plays on the other one's output.
2. **Game Music** is the kind of music the game plays. **Auto**, the default, starts on General MIDI and switches to
   MT-32 when the game sends something only an MT-32 understands, as MT-32 games do when their music starts. So a game
   set up for Roland MT-32 or LAPC-I in its setup program plays on the MT-32 output with nothing to choose here. Pick
   **General MIDI** or **MT-32** for a game that Auto gets wrong. With a game open, the list also has the MIDI ports
   on your computer: one of them sends that game's music there, whatever its kind. With no game open, the choice is
   the default for games.

The last line of the menu shows the kind of music and where it's playing in green, or what's missing in red. If a
game plays MIDI and nothing can play it, Geardox says so. A `.gdox` sets these with `midi`, `midi_out`, `midi_port`
and `mt32_model` under `[sound]`.

The built-in synths play inside the emulated machine's time: they stop when it pauses and go on where they were, and
they're in Geardox's mix, so **Audio > Built-in MT-32 Volume** and **Built-in General MIDI Volume**, their **Mute** and
the recordings include them. They're silent while fast-forwarding. Loading a state brings the game's instruments back,
MT-32 custom sounds included, and the music goes on from the next note.

An outside synth that's closed and opened again gets its port back within two seconds, with the game's instruments
sent again. Pausing, fast-forwarding, rewinding and loading states stop its hanging notes. It plays in its own audio:
Geardox's volume sliders, their mutes and recordings don't include it, and **Mute All Audio** stops its notes.

## Keyboard, Mouse and Joystick

**Keys go to the PC** while Geardox's window has focus, so Ctrl+C and the like reach DOS. Geardox keeps only its
hotkeys, which use Ctrl+Shift, Ctrl with an F-key or a digit, and Alt+Enter. On macOS, Cmd replaces Ctrl in all of
them; on a Mac keyboard the F-keys may need Fn. **Input > Keyboard > Paste as Keystrokes** types the clipboard into
the PC.

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
one to remove it. Removing the last binding you gave a key of yours gives it back its default, while removing a
built-in one leaves that key doing nothing. Right click the PC key to remove all but the built-in ones. Red keys are
ones no key of yours reaches. Press the same hotkey again, or click **Close**, to go back to the game. Changes are kept
for that game only, beside its other files; with no game open, they're kept for the disc, floppy or hard disk image you
opened. A descriptor can set them too, including one key that sends several, such as Ctrl, Alt and Del; see
[Remap keys](GDOX_README.md#remap-keys).

**Ctrl+F2** (**Cmd+F2** on macOS) turns the game's key changes off and on, for instance to type a character's name in
a game that moves on letters. Every game starts with them on.

**The mouse** moves the PC's mouse while the pointer is over the screen. For games that need free movement, Game Focus
locks it inside the window.

**A gamepad** presses the PC's keys and moves its mouse. Out of the box, the D-pad sends the cursor keys, the left
stick moves the mouse, A and B click, X is Space, Y is Left Ctrl, L1 is Left Alt, R1 is Left Shift, Back is Esc and
Start is Enter. **Input > Host Gamepad 1 > Key Mapping** changes them: click a button's row, then press the PC key or
choose a mouse button. **Mouse > Move Mouse With** in the same menu picks what moves the mouse, or nothing. During a
game, these changes are for that game only, and a descriptor can set them too; see
[Use a gamepad](GDOX_README.md#use-a-gamepad). **Host Gamepad 2** works
the same, for games where two players share the keyboard.

**For joystick games,** set **Input > Host Gamepad 1 > Use As** to **Joystick**. That gamepad then drives the PC's
joystick port, and **Input > Emulated Joystick** picks what the game sees: a standard joystick, a Gravis GamePad (the
default) or a CH FlightStick Pro.

All hotkeys can be changed in **Emulator > Hotkeys**, and its **Reset to Defaults** brings these back.

## Where Geardox Keeps Its Files

Settings, the built-in disks and everything Geardox saves live in one folder:

| System | Folder |
| --- | --- |
| Windows | `%APPDATA%\Geardome\Geardox` |
| macOS | `~/Library/Application Support/Geardome/Geardox` |
| Linux | `~/.local/share/Geardome/Geardox` |

To keep everything beside the program instead, start it with `--portable`, or put an empty file named `portable.ini`
next to it. On macOS, it goes next to the `.app`.

In this folder:

- `config.ini` holds your settings.
- `FreeDOS.hdd` is the built-in FreeDOS disk, and `MS-DOS.hdd` the built-in MS-DOS disk, made from your import the
  first time it's needed.
- `msdos.zip` is your imported MS-DOS, `ultrasnd.zip` your imported Gravis UltraSound software and `glide2x.ovl` your
  imported 3dfx Voodoo driver, once you've imported them.
- `mt32` and `soundfonts` are the folders where the built-in synths look for MT-32 ROMs and sound banks.
- `tmp` holds the games Geardox unpacks from ZIPs while they run.

The rest is the **Save Files folder**, this same folder unless **Emulator > Directories > Save Files** moves it:

- `<name>.<id>` folders, one for each game and for each folder or ZIP attached as a hard disk, keep what DOS changed:
  - `Saves` holds a game's saves when they can't go into its folder: a game in a ZIP or a read-only folder, or any
    game when **Game > Save Games To** is **The Save Files Folder**.
  - `C` holds what the game wrote elsewhere on C:, and `C.hdd` a game's hard disk image with its changes, when the
    image itself isn't written.
  - `FreeDOS` and `MS-DOS` hold the changes to each DOS's own files, `AUTOEXEC.BAT` and `CONFIG.SYS` among them, with
    the version they were edited from in `.base`. Your edits win at the next launch; when the `.gdox` or a setting such
    as the sound card's IRQ later changes the same file, Geardox joins both and tells you.
  - `Conflicts` holds changes that couldn't go back, such as those of a session left behind by loading an older save
    state.
  - `Disk` is Geardox's record of the drive, so a save state still loads at the next launch. While the game runs, it
    also holds what DOS has written so far, so if Geardox stops without closing the game, the next launch writes it
    back.
  - `Settings.ini` and `Keys.ini` keep the game's own settings and key map, and `<name>.gdox` is the `.gdox` that
    **Save Descriptor...** suggests when it can't go in the game's folder.
- Working copies of floppy and CHD images, and a `.geardox.cmos` file for each PC model, with its BIOS settings.
- `.keys` files keep the key maps made with no game open, named after the disc, floppy or hard disk image.

Save states, screenshots and recordings go to this folder too, and **Emulator > Directories** moves each kind. There,
**With the Game or Disk** keeps them in the game's `<name>.<id>` folder, or next to the CD, floppy or hard disk image in
use, and **Next to Each Image**, for Save Files, puts only the working copies next to their image. A game's save states
are named after its folder, `<name>.<id>.state1` to `.state5`, and with nothing open they're `PC.state1` to `.state5`.

## Quick Fixes

| Problem | Try |
| --- | --- |
| A hard disk I attached isn't there | Reset. Hard disks only attach at a reset. |
| The PC boots from a floppy I forgot | Eject it from **Drives > Floppies**, or use **Geardox > Reset and Boot From > Hard Disk**. |
| The games I installed are missing | A game has its own C:. Close it and choose **Geardox > Power On** to get the built-in disk back. Games installed on the FreeDOS disk aren't on the MS-DOS one: **Drives > Hard Disks > Default Disk** switches back, or attach both to copy them. |
| Hotkeys don't work | Game Focus is on, as the window title says. Press Ctrl+F10. |
| The mouse stops at the window's edge | Press Ctrl+F10 for Game Focus, which locks it in the window. |
| The game runs too fast | Turn off **Turbo** (Ctrl+Shift+T), or pick a 386 in **Emulator > Machine > Model**. |
| No sound | Run the game's setup and choose Sound Blaster 16, port 220, IRQ 5, DMA 1. |
| No music with General MIDI or MT-32 | Add the MT-32 ROMs or a sound bank in **Audio > Built-in Synths**, or start an outside synth and choose its port in **Audio > MIDI Out**. See [MIDI Music](#midi-music). |
| The game asks for expanded memory (EMS) | Save a `.gdox` for it with **Game > Game Files > Save Descriptor...** and set `ems_kb` in it. See [Memory](GDOX_README.md#memory). |
| A built-in disk is broken | **DOS > Recreate Built-in MS-DOS Disk...** or **Recreate Built-in FreeDOS Disk...**. It erases what was on it. |
