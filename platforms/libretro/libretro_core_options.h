/*
 * Geardox - MS-DOS PC/AT Emulator
 * Copyright (C) 2026  Ignacio Sanchez

 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/
 *
 */

#ifndef LIBRETRO_CORE_OPTIONS_H__
#define LIBRETRO_CORE_OPTIONS_H__

#include <stdlib.h>
#include <string.h>

#include "libretro.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 ********************************
 * Core Option Definitions
 ********************************
 */

struct retro_core_option_v2_category option_cats_us[] = {
    {
        "video",
        "Video",
        "Configure video output settings."
    },
    {
        "audio",
        "Audio",
        "Configure the volume of each sound source."
    },
    {
        "input",
        "Input",
        "Configure the joystick and the mouse."
    },
    {
        "cdrom",
        "CD-ROM",
        "Configure CD-ROM settings."
    },
    {
        "floppy",
        "Floppy",
        "Configure floppy drives and save disks."
    },
    {
        "system",
        "System",
        "Configure the emulated machine."
    },
    { NULL, NULL, NULL },
};

struct retro_core_option_v2_definition option_defs_us[] = {

    /* Video */
    {
        "geardox_aspect_ratio",
        "Aspect Ratio",
        NULL,
        "Select which aspect ratio will be presented by the core. '1:1 PAR' produces square pixels at monitor resolution, also when Native Resolution outputs fewer pixels.",
        NULL,
        "video",
        {
            { "1:1 PAR",   NULL },
            { "4:3 DAR",   NULL },
            { "16:9 DAR",  NULL },
            { "16:10 DAR", NULL },
            { NULL, NULL },
        },
        "4:3 DAR"
    },
    {
        "geardox_native_resolution",
        "Native Resolution",
        NULL,
        "Output one pixel per game pixel, as shaders expect. When disabled, zoomed modes are output at monitor resolution.",
        NULL,
        "video",
        {
            { "Enabled",  NULL },
            { "Disabled", NULL },
            { NULL, NULL },
        },
        "Enabled"
    },
    {
        "geardox_cga_output",
        "CGA Output",
        NULL,
        "The monitor on the IBM CGA's output. Auto shows composite colour where a game turns on 640x200 with the colour burst, RGB elsewhere.",
        NULL,
        "video",
        {
            { "auto",      "Auto" },
            { "rgb",       "RGB Monitor" },
            { "composite", "Composite Monitor" },
            { NULL, NULL },
        },
        "auto"
    },
    {
        "geardox_frameskip",
        "Frameskip",
        NULL,
        "Skip frames to avoid audio crackling when the device is too slow. 'Auto' skips when the frontend reports that the audio buffer is about to run out, 'Manual' uses the threshold below.",
        NULL,
        "video",
        {
            { "Disabled", NULL },
            { "Auto",     NULL },
            { "Manual",   NULL },
            { NULL, NULL },
        },
        "Disabled"
    },
    {
        "geardox_frameskip_threshold",
        "Frameskip Threshold (%)",
        NULL,
        "With 'Manual' frameskip, skip frames while the audio buffer is below this percentage. Higher values reduce crackling but skip more frames.",
        NULL,
        "video",
        {
            { "15", NULL }, { "18", NULL }, { "21", NULL }, { "24", NULL }, { "27", NULL }, { "30", NULL },
            { "33", NULL }, { "36", NULL }, { "39", NULL }, { "42", NULL }, { "45", NULL }, { "48", NULL },
            { "51", NULL }, { "54", NULL }, { "57", NULL }, { "60", NULL }, { NULL, NULL }
        },
        "33"
    },

    /* Audio */

    {
        "geardox_fm_volume",
        "FM Volume",
        NULL,
        "Volume of the FM synthesizer, as a percentage where 100 is the default.",
        NULL,
        "audio",
        {
            { "0",   NULL }, { "10",  NULL }, { "20",  NULL }, { "30",  NULL }, { "40",  NULL },
            { "50",  NULL }, { "60",  NULL }, { "70",  NULL }, { "80",  NULL }, { "90",  NULL },
            { "100", NULL }, { "110", NULL }, { "120", NULL }, { "130", NULL }, { "140", NULL },
            { "150", NULL }, { "160", NULL }, { "170", NULL }, { "180", NULL }, { "190", NULL },
            { "200", NULL }, { NULL, NULL }
        },
        "100"
    },
    {
        "geardox_pcm_volume",
        "PCM Volume",
        NULL,
        "Volume of the PCM sound source, as a percentage where 100 is the default.",
        NULL,
        "audio",
        {
            { "0",   NULL }, { "10",  NULL }, { "20",  NULL }, { "30",  NULL }, { "40",  NULL },
            { "50",  NULL }, { "60",  NULL }, { "70",  NULL }, { "80",  NULL }, { "90",  NULL },
            { "100", NULL }, { "110", NULL }, { "120", NULL }, { "130", NULL }, { "140", NULL },
            { "150", NULL }, { "160", NULL }, { "170", NULL }, { "180", NULL }, { "190", NULL },
            { "200", NULL }, { NULL, NULL }
        },
        "100"
    },
    {
        "geardox_cdda_volume",
        "CD-DA Volume",
        NULL,
        "Volume of the CD-ROM audio tracks, as a percentage where 100 is the default.",
        NULL,
        "audio",
        {
            { "0",   NULL }, { "10",  NULL }, { "20",  NULL }, { "30",  NULL }, { "40",  NULL },
            { "50",  NULL }, { "60",  NULL }, { "70",  NULL }, { "80",  NULL }, { "90",  NULL },
            { "100", NULL }, { "110", NULL }, { "120", NULL }, { "130", NULL }, { "140", NULL },
            { "150", NULL }, { "160", NULL }, { "170", NULL }, { "180", NULL }, { "190", NULL },
            { "200", NULL }, { NULL, NULL }
        },
        "100"
    },
    {
        "geardox_buzzer_volume",
        "Buzzer Volume",
        NULL,
        "Volume of the buzzer that plays system beeps, as a percentage where 100 is the default.",
        NULL,
        "audio",
        {
            { "0",   NULL }, { "10",  NULL }, { "20",  NULL }, { "30",  NULL }, { "40",  NULL },
            { "50",  NULL }, { "60",  NULL }, { "70",  NULL }, { "80",  NULL }, { "90",  NULL },
            { "100", NULL }, { "110", NULL }, { "120", NULL }, { "130", NULL }, { "140", NULL },
            { "150", NULL }, { "160", NULL }, { "170", NULL }, { "180", NULL }, { "190", NULL },
            { "200", NULL }, { NULL, NULL }
        },
        "100"
    },
    {
        "geardox_mt32_volume",
        "Built-in MT-32 Volume",
        NULL,
        "Volume of Geardox's own MT-32, as a percentage where 100 is the default. An external MIDI port keeps its own volume.",
        NULL,
        "audio",
        {
            { "0",   NULL }, { "10",  NULL }, { "20",  NULL }, { "30",  NULL }, { "40",  NULL },
            { "50",  NULL }, { "60",  NULL }, { "70",  NULL }, { "80",  NULL }, { "90",  NULL },
            { "100", NULL }, { "110", NULL }, { "120", NULL }, { "130", NULL }, { "140", NULL },
            { "150", NULL }, { "160", NULL }, { "170", NULL }, { "180", NULL }, { "190", NULL },
            { "200", NULL }, { NULL, NULL }
        },
        "100"
    },
    {
        "geardox_gm_volume",
        "Built-in General MIDI Volume",
        NULL,
        "Volume of Geardox's own General MIDI synth, as a percentage where 100 is the default. An external MIDI port keeps its own volume.",
        NULL,
        "audio",
        {
            { "0",   NULL }, { "10",  NULL }, { "20",  NULL }, { "30",  NULL }, { "40",  NULL },
            { "50",  NULL }, { "60",  NULL }, { "70",  NULL }, { "80",  NULL }, { "90",  NULL },
            { "100", NULL }, { "110", NULL }, { "120", NULL }, { "130", NULL }, { "140", NULL },
            { "150", NULL }, { "160", NULL }, { "170", NULL }, { "180", NULL }, { "190", NULL },
            { "200", NULL }, { NULL, NULL }
        },
        "100"
    },

    /* Input */

    {
        "geardox_joystick",
        "Joystick",
        NULL,
        "The joystick on the game port that the first controller drives when its device type is PC Joystick. Games choose it by what they support. The Gravis GamePad suits most, flight simulators want the Joystick or the CH FlightStick Pro.",
        NULL,
        "input",
        {
            { "none",        "None" },
            { "standard",    "Joystick" },
            { "gamepad",     "Gravis GamePad" },
            { "flightstick", "CH FlightStick Pro" },
            { NULL, NULL },
        },
        "gamepad"
    },
    {
        "geardox_mouse_sensitivity",
        "Mouse Sensitivity",
        NULL,
        "Adjust the sensitivity of the mouse. Higher values result in faster cursor movement.",
        NULL,
        "input",
        {
            { "1",  NULL }, { "2",  NULL }, { "3",  NULL }, { "4",  NULL },
            { "5",  NULL }, { "6",  NULL }, { "7",  NULL }, { "8",  NULL },
            { "9",  NULL }, { "10", NULL }, { "11", NULL }, { "12", NULL },
            { "13", NULL }, { "14", NULL }, { "15", NULL }, { NULL, NULL }
        },
        "7"
    },
    {
        "geardox_pad1_mouse",
        "Gamepad 1 Moves Mouse With",
        NULL,
        "With the port's device type set to Keyboard and Mouse or RetroPad, this stick or the D-pad moves the PC's mouse. Its buttons press the keys set in the port's controls when the device type is Keyboard and Mouse.",
        NULL,
        "input",
        {
            { "left_stick",  "Left Stick" },
            { "right_stick", "Right Stick" },
            { "dpad",        "D-Pad" },
            { "off",         "Off" },
            { NULL, NULL },
        },
        "left_stick"
    },
    {
        "geardox_pad1_mouse_buttons",
        "Gamepad 1 Mouse Buttons",
        NULL,
        "The two buttons that click the PC mouse's left and right buttons. Leave them without keys in the port's controls.",
        NULL,
        "input",
        {
            { "b_a",   "B and A" },
            { "y_x",   "Y and X" },
            { "l_r",   "L and R" },
            { "l2_r2", "L2 and R2" },
            { "off",   "Off" },
            { NULL, NULL },
        },
        "b_a"
    },
    {
        "geardox_pad2_mouse",
        "Gamepad 2 Moves Mouse With",
        NULL,
        "With the port's device type set to Keyboard and Mouse or RetroPad, this stick or the D-pad moves the PC's mouse. Its buttons press the keys set in the port's controls when the device type is Keyboard and Mouse.",
        NULL,
        "input",
        {
            { "left_stick",  "Left Stick" },
            { "right_stick", "Right Stick" },
            { "dpad",        "D-Pad" },
            { "off",         "Off" },
            { NULL, NULL },
        },
        "left_stick"
    },
    {
        "geardox_pad2_mouse_buttons",
        "Gamepad 2 Mouse Buttons",
        NULL,
        "The two buttons that click the PC mouse's left and right buttons. Leave them without keys in the port's controls.",
        NULL,
        "input",
        {
            { "b_a",   "B and A" },
            { "y_x",   "Y and X" },
            { "l_r",   "L and R" },
            { "l2_r2", "L2 and R2" },
            { "off",   "Off" },
            { NULL, NULL },
        },
        "b_a"
    },
    {
        "geardox_pad_mouse_speed",
        "Gamepad Mouse Speed",
        NULL,
        "How fast a stick or the D-pad moves the PC's mouse.",
        NULL,
        "input",
        {
            { "1",  NULL }, { "2",  NULL }, { "3",  NULL }, { "4",  NULL },
            { "5",  NULL }, { "6",  NULL }, { "7",  NULL }, { "8",  NULL },
            { "9",  NULL }, { "10", NULL }, { "11", NULL }, { "12", NULL },
            { "13", NULL }, { "14", NULL }, { "15", NULL }, { NULL, NULL }
        },
        "5"
    },

    /* CD-ROM */

    {
        "geardox_cdrom_preload",
        "Preload CD-ROM (restart)",
        NULL,
        "Preload all CD-ROM tracks into RAM. This increases memory usage but may improve performance.",
        NULL,
        "cdrom",
        {
            { "Disabled", NULL },
            { "Enabled",  NULL },
            { NULL, NULL },
        },
        "Disabled"
    },

    /* Floppy */

    {
        "geardox_floppy_drive",
        "Disk Control Floppy Drive",
        NULL,
        "Select the floppy drive that the frontend's disk control ejects and inserts floppy disks into.",
        NULL,
        "floppy",
        {
            { "Floppy 1", NULL },
            { "Floppy 2", NULL },
            { NULL, NULL },
        },
        "Floppy 1"
    },
    {
        "geardox_floppy_multidrive",
        "Floppy MultiDrive (restart)",
        NULL,
        "Also insert the floppy that follows the boot floppy into drive 2, for software that reads from both drives.",
        NULL,
        "floppy",
        {
            { "Disabled", NULL },
            { "Enabled",  NULL },
            { NULL, NULL },
        },
        "Disabled"
    },
    {
        "geardox_save_disk",
        "Save Disk (restart)",
        NULL,
        "Add a blank 2HD floppy disk to the disk control list for software that asks for a user or save disk. It is stored in the save directory.",
        NULL,
        "floppy",
        {
            { "Disabled", NULL },
            { "Enabled",  NULL },
            { NULL, NULL },
        },
        "Disabled"
    },

    /* System */

    {
        "geardox_model",
        "Model (restart)",
        NULL,
        "The PC/AT machine to emulate. The 486DX2-66 suits most games of 1990 to 1995; a game that runs too fast wants a 386, or Turbo off. RetroArch's 'Save Game Options' keeps a choice for one game.",
        NULL,
        "system",
        {
            { "386sx_20",   "386SX-20 (80386SX 20 MHz)" },
            { "386dx_33",   "386DX-33 (80386DX 33 MHz)" },
            { "486sx_33",   "486SX-33 (80486SX 33 MHz)" },
            { "486dx2_66",  "486DX2-66 (80486DX2 66 MHz)" },
            { "486dx4_100", "486DX4-100 (80486DX4 100 MHz)" },
            { "pentium_75", "Pentium 75 (P54C 75 MHz)" },
            { "pentium_90", "Pentium 90 (P54C 90 MHz)" },
            { "pentium_133", "Pentium 133 (P54C 133 MHz)" },
            { "pentium_166", "Pentium 166 (P54C 166 MHz)" },
            { "pentium_200", "Pentium 200 (P54C 200 MHz)" },
            { NULL, NULL },
        },
        "486dx2_66"
    },
    {
        "geardox_cpu_clock",
        "CPU Speed (restart)",
        NULL,
        "Model Default runs the CPU at the model's original clock. Devices keep their real timing at any speed. Some software may not expect a faster CPU.",
        NULL,
        "system",
        {
            { "auto", "Model Default" },
            { "16",   "16 MHz" },
            { "20",   "20 MHz" },
            { "25",   "25 MHz" },
            { "33",   "33 MHz" },
            { "40",   "40 MHz" },
            { "50",   "50 MHz" },
            { "66",   "66 MHz" },
            { "75",   "75 MHz" },
            { "90",   "90 MHz" },
            { "100",  "100 MHz" },
            { "133",  "133 MHz" },
            { "166",  "166 MHz" },
            { "200",  "200 MHz" },
            { NULL, NULL },
        },
        "auto"
    },
    {
        "geardox_main_ram",
        "Main RAM (restart)",
        NULL,
        "8 MB runs nearly every DOS game. A size the model doesn't offer uses its next size up, or its largest. Save states grow with it.",
        NULL,
        "system",
        {
            { "1",    "1 MB" },
            { "2",    "2 MB" },
            { "4",    "4 MB" },
            { "8",    "8 MB" },
            { "16",   "16 MB" },
            { "32",   "32 MB" },
            { "64",   "64 MB" },
            { "128",  "128 MB" },
            { NULL, NULL },
        },
        "8"
    },
    {
        "geardox_fpu",
        "FPU (restart)",
        NULL,
        "The math coprocessor: an 80387 in the socket of the 386 models; the 486DX2 has one built in. A few games need one, and some run different code with one fitted.",
        NULL,
        "system",
        {
            { "80387", "80387" },
            { "none",  "None" },
            { NULL, NULL },
        },
        "80387"
    },
    {
        "geardox_floppy_a",
        "Floppy A Drive (restart)",
        NULL,
        "The drive type of floppy A. Auto fits the smallest drive that holds the disk at power on.",
        NULL,
        "system",
        {
            { "auto",  "Auto" },
            { "360k",  "360 KB 5.25" },
            { "1200k", "1.2 MB 5.25" },
            { "720k",  "720 KB 3.5" },
            { "1440k", "1.44 MB 3.5" },
            { "2880k", "2.88 MB 3.5" },
            { NULL, NULL },
        },
        "auto"
    },
    {
        "geardox_floppy_b",
        "Floppy B Drive (restart)",
        NULL,
        "The drive type of floppy B. Auto fits the smallest drive that holds the disk at power on.",
        NULL,
        "system",
        {
            { "auto",  "Auto" },
            { "360k",  "360 KB 5.25" },
            { "1200k", "1.2 MB 5.25" },
            { "720k",  "720 KB 3.5" },
            { "1440k", "1.44 MB 3.5" },
            { "2880k", "2.88 MB 3.5" },
            { NULL, NULL },
        },
        "auto"
    },
    {
        "geardox_video_card",
        "Video Card (restart)",
        NULL,
        "The video card in the machine. Model Default fits a Cirrus Logic GD5428, on the VL-Bus in the 486, and a PCI GD5446 in the Pentiums. The VL-Bus and PCI cards fall back to the ISA one on models without their slot. The IBM CGA and EGA are 8-bit cards for older games.",
        NULL,
        "system",
        {
            { "auto",       "Model Default" },
            { "cga",        "IBM CGA" },
            { "ega",        "IBM EGA" },
            { "vga",        "VGA" },
            { "gd5428",     "SVGA Cirrus GD5428 ISA" },
            { "gd5428_vlb", "SVGA Cirrus GD5428 VL-Bus" },
            { "gd5446",     "SVGA Cirrus GD5446 PCI" },
            { NULL, NULL },
        },
        "auto"
    },
    {
        "geardox_voodoo",
        "3dfx Voodoo Graphics (restart)",
        NULL,
        "A 3dfx Voodoo Graphics with 4 MB as a second PCI card, on the Pentiums only, for DOS games with 3dfx support. Games that load GLIDE2X.OVL need it from your own 3dfx DOS driver: put its ZIP, or the GLIDE2X.OVL file, in <system>/geardox/3dfx.",
        NULL,
        "system",
        {
            { "disabled", "Off" },
            { "enabled",  "On" },
            { NULL, NULL },
        },
        "disabled"
    },
    {
        "geardox_mouse",
        "Mouse (restart)",
        NULL,
        "The mouse in the machine. PS/2 is the usual one, Serial is a Microsoft mouse on COM1. The frontend's mouse drives it.",
        NULL,
        "system",
        {
            { "none",   "None" },
            { "ps2",    "PS/2" },
            { "serial", "Serial (COM1)" },
            { NULL, NULL },
        },
        "ps2"
    },
    {
        "geardox_cdrom_drive",
        "CD-ROM Drive (restart)",
        NULL,
        "Where the ATAPI CD-ROM drive sits on the IDE cables. The position it takes is closed to hard disks.",
        NULL,
        "system",
        {
            { "none", "None" },
            { "pm",   "Primary Master" },
            { "ps",   "Primary Slave" },
            { "sm",   "Secondary Master" },
            { "ss",   "Secondary Slave" },
            { NULL, NULL },
        },
        "sm"
    },
    {
        "geardox_cdrom_speed",
        "CD-ROM Speed (restart)",
        NULL,
        "How fast the CD-ROM drive spins, so how long a seek or a read takes. Games that stream from the disc may want 4x or more.",
        NULL,
        "system",
        {
            { "1x", NULL },
            { "2x", NULL },
            { "4x", NULL },
            { "8x", NULL },
            { NULL, NULL },
        },
        "4x"
    },
    {
        "geardox_drive_timing",
        "Drive Timing (restart)",
        NULL,
        "How long disk, floppy and CD-ROM seeks, rotation and spin-up take. Auto is fast for games and real otherwise; transfers keep their rate either way.",
        NULL,
        "system",
        {
            { "auto", "Auto" },
            { "real", "Real" },
            { "fast", "Fast" },
            { NULL, NULL },
        },
        "auto"
    },
    {
        "geardox_sound_card",
        "Sound Card (restart)",
        NULL,
        "The card in the machine. Games ask it what it is and choose, so a Sound Blaster 16 suits nearly all of them.",
        NULL,
        "system",
        {
            { "none",   "None" },
            { "adlib",  "AdLib" },
            { "sb15",   "Sound Blaster 1.5" },
            { "sb20",   "Sound Blaster 2.0" },
            { "sbpro",  "Sound Blaster Pro" },
            { "sbpro2", "Sound Blaster Pro 2" },
            { "sb16",   "Sound Blaster 16" },
            { NULL, NULL },
        },
        "sb16"
    },
    {
        "geardox_sound_irq",
        "Sound Card IRQ (restart)",
        NULL,
        "The card's interrupt. Some old games only work with a fixed one.",
        NULL,
        "system",
        {
            { "5", NULL },
            { "7", NULL },
            { NULL, NULL },
        },
        "5"
    },
    {
        "geardox_sound_dma",
        "Sound Card DMA (restart)",
        NULL,
        "The card's 8-bit DMA channel. Some old games only work with a fixed one.",
        NULL,
        "system",
        {
            { "1", NULL },
            { "3", NULL },
            { NULL, NULL },
        },
        "1"
    },
    {
        "geardox_mpu",
        "MPU-401 (restart)",
        NULL,
        "The MIDI port at 330. The Sound Blaster 16 has its own; other cards get a separate MPU-401 on IRQ 2 (9). Intelligent has the MPU-401's own sequencer, which older MT-32 games need; UART Only is the plain port most later games use. The music plays as MIDI sets.",
        NULL,
        "system",
        {
            { "enabled",  "Intelligent" },
            { "uart",     "UART Only" },
            { "disabled", "Off" },
            { NULL, NULL },
        },
        "enabled"
    },
    {
        "geardox_midi",
        "MIDI Out",
        NULL,
        "Where MIDI music plays. Built-in Synth plays MT-32 music, told from what the game sends, on an MT-32 from your own ROMs in <system>/geardox/mt32 (or <system>), and General MIDI games on a sound bank (.sf2 or .sf3) in <system>/geardox/soundfonts; a game plays on the other one when its own is missing. External Port sends it to the port in Settings > Audio > MIDI. A .gdox picks one for its game with midi_out in [sound].",
        NULL,
        "system",
        {
            { "builtin",   "Built-in Synth" },
            { "retroarch", "External Port" },
            { NULL, NULL },
        },
        "builtin"
    },
    {
        "geardox_mt32_model",
        "Built-in MT-32 Model",
        NULL,
        "The unit the built-in MT-32 plays, when its ROMs are there. Auto plays a CM-32L first, then an MT-32. A .gdox picks one for its game with mt32_model in [sound].",
        NULL,
        "system",
        {
            { "auto",     "Auto" },
            { "mt32_old", "MT-32 (old)" },
            { "mt32_new", "MT-32 (new)" },
            { "cm32l",    "CM-32L" },
            { NULL, NULL },
        },
        "auto"
    },
    {
        "geardox_gus",
        "Gravis UltraSound (restart)",
        NULL,
        "A Gravis UltraSound at 240 next to the sound card, for trackers, demos and games that support it. Games that play their music through it need the patches: put your own Gravis 4.11 install disks, or a folder with the software installed, in <system>/geardox/ultrasnd.",
        NULL,
        "system",
        {
            { "disabled", "Off" },
            { "enabled",  "On" },
            { NULL, NULL },
        },
        "disabled"
    },
    {
        "geardox_descriptor",
        "Game Descriptors (restart)",
        NULL,
        "A .gdox file, or a folder or ZIP with one in its root, opens its game: the machine it asks for, DOS set up for it and the game running, with its saves kept. The machine options it sets are hidden while it runs. Disabled, the folder or ZIP is a plain disk as before.",
        NULL,
        "system",
        {
            { "enabled",  NULL },
            { "disabled", NULL },
            { NULL, NULL },
        },
        "enabled"
    },
    {
        "geardox_game_database",
        "Game Database (restart)",
        NULL,
        "A game folder or ZIP without a .gdox is recognised from Geardox's built-in game database and opens like one with its .gdox. A .gdox in the folder always wins.",
        NULL,
        "system",
        {
            { "enabled",  NULL },
            { "disabled", NULL },
            { NULL, NULL },
        },
        "enabled"
    },
    {
        "geardox_game_pick",
        "Game In Folder (restart)",
        NULL,
        "When a folder or ZIP holds more than one game, which one opens, in the order of their names.",
        NULL,
        "system",
        {
            { "1", NULL },
            { "2", NULL },
            { "3", NULL },
            { "4", NULL },
            { "5", NULL },
            { "6", NULL },
            { "7", NULL },
            { "8", NULL },
            { NULL, NULL },
        },
        "1"
    },
    {
        "geardox_game_target",
        "Game Runs (restart)",
        NULL,
        "What a game descriptor runs at each boot: the game, its 3dfx program with the 3dfx Voodoo fitted, its setup program, or the DOS prompt on its drive.",
        NULL,
        "system",
        {
            { "game",   "Game" },
            { "voodoo", "3dfx Voodoo" },
            { "setup",  "Setup" },
            { "prompt", "DOS Prompt" },
            { NULL, NULL },
        },
        "game"
    },
    {
        "geardox_write_back",
        "Game Saves (restart)",
        NULL,
        "Where what a game changes goes when its session ends: a folder in the save directory, or the game's own folder. A ZIP always uses the save directory.",
        NULL,
        "system",
        {
            { "save_folder", "Save Directory" },
            { "game_folder", "Game Folder" },
            { NULL, NULL },
        },
        "save_folder"
    },
    {
        "geardox_game_dos",
        "Game DOS (restart)",
        NULL,
        "Your own MS-DOS: put its Setup disk images, or a ZIP of them, in the system folder's geardox/msdos. Geardox imports them once, then games boot it unless their descriptor asks for FreeDOS. FreeDOS here runs every game on FreeDOS; the built-in disk follows Built-in DOS Disk. MS-DOS 5.0 to 6.22, in any language.",
        NULL,
        "system",
        {
            { "auto",    "MS-DOS When Found" },
            { "freedos", "FreeDOS" },
            { NULL, NULL },
        },
        "auto"
    },
    {
        "geardox_keyboard_layout",
        "DOS Keyboard Layout (restart)",
        NULL,
        "The layout of your keyboard, for DOS in games: Geardox loads KEYB with it in the AUTOEXEC.BAT it makes for each game, on FreeDOS or MS-DOS, and in a built-in disk when it makes one. A game's descriptor can set its own with keyboard = under [dos].",
        NULL,
        "system",
        {
            { "us", "United States" },
            { "be", "Belgium" },
            { "br", "Brazil" },
            { "su", "Finland" },
            { "fr", "France" },
            { "gr", "Germany" },
            { "it", "Italy" },
            { "la", "Latin America" },
            { "nl", "Netherlands" },
            { "sp", "Spain" },
            { "sv", "Sweden" },
            { "sf", "Switzerland (French)" },
            { "sg", "Switzerland (German)" },
            { "uk", "United Kingdom" },
            { NULL, NULL },
        },
        "us"
    },
    {
        "geardox_dos_disk",
        "Built-in DOS Disk (restart)",
        NULL,
        "With no hard disk in the content, a built-in DOS disk goes on Primary Master: your MS-DOS when its Setup disks are in the system folder's geardox/msdos, or FreeDOS. They live in the save folder as MS-DOS.hdd and FreeDOS.hdd, and they're yours: Geardox never changes them again. Delete one for a new one.",
        NULL,
        "system",
        {
            { "auto",     "MS-DOS When Found" },
            { "freedos",  "FreeDOS" },
            { "disabled", NULL },
            { NULL, NULL },
        },
        "auto"
    },
    {
        "geardox_boot_device",
        "Boot Device (restart)",
        NULL,
        "The device the machine boots from at power-on and on every restart. Auto follows the BIOS boot order.",
        NULL,
        "system",
        {
            { "auto",    "Auto" },
            { "floppy1", "Floppy 1" },
            { "floppy2", "Floppy 2" },
            { "hdd",     "Hard Disk" },
            { "cdrom",   "CD-ROM" },
            { NULL, NULL },
        },
        "auto"
    },
    {
        "geardox_cmos",
        "CMOS (restart)",
        NULL,
        "Where the battery-backed CMOS with the BIOS settings is kept. 'Shared' keeps one CMOS for all content in the save directory, like the real machine. 'Per Game' keeps a separate CMOS in each content's save file, a better choice for netplay.",
        NULL,
        "system",
        {
            { "Shared",   NULL },
            { "Per Game", NULL },
            { NULL, NULL },
        },
        "Shared"
    },
    {
        "geardox_turbo",
        "Turbo",
        NULL,
        "The board's Turbo button. Off runs the CPU at a third of its clock, for games too fast for this machine. Takes effect at once.",
        NULL,
        "system",
        {
            { "On",  NULL },
            { "Off", NULL },
            { NULL, NULL },
        },
        "On"
    },
    {
        "geardox_boot_menu",
        "BIOS Boot Menu (restart)",
        NULL,
        "SeaBIOS offers its boot menu with Esc for 2.5 seconds at each boot.",
        NULL,
        "system",
        {
            { "Disabled", NULL },
            { "Enabled",  NULL },
            { NULL, NULL },
        },
        "Disabled"
    },
    {
        "geardox_rtc",
        "Clock Start (restart)",
        NULL,
        "Start the PC clock from the host time or from a fixed date (January 1, 1990). A fixed date makes netplay and replays independent of each computer's clock.",
        NULL,
        "system",
        {
            { "Host time",  NULL },
            { "Fixed date", NULL },
            { NULL, NULL },
        },
        "Host time"
    },
#if !defined(GDOX_DISABLE_DYNAREC)
    {
        "geardox_dynarec",
        "CPU Dynarec",
        NULL,
        "Use dynamic recompilation. Most games will run faster but some games may run slower.",
        NULL,
        "system",
        {
            { "Enabled",  NULL },
            { "Disabled", NULL },
            { NULL, NULL },
        },
        "Enabled"
    },
#endif
#if defined(GDOX_ENABLE_VOODOO_THREAD)
    {
        "geardox_voodoo_thread",
        "3dfx Voodoo Render Thread",
        NULL,
        "Draw the 3dfx Voodoo's pixels on other host threads, two on hosts with four cores or more. The picture is the same either way; turn it off only to diagnose a problem.",
        NULL,
        "system",
        {
            { "enabled",  "On" },
            { "disabled", "Off" },
            { NULL, NULL },
        },
        "enabled"
    },
#endif

    { NULL, NULL, NULL, NULL, NULL, NULL, {{0, 0}}, NULL },
};

struct retro_core_options_v2 options_us = {
    option_cats_us,
    option_defs_us
};

/*
 ********************************
 * Functions
 ********************************
 */

static void libretro_set_core_options(retro_environment_t environ_cb, bool *categories_supported)
{
    unsigned version = 0;

    if (!environ_cb || !categories_supported)
        return;

    *categories_supported = false;

    if (!environ_cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version))
        version = 0;

    if (version >= 2)
    {
        *categories_supported = environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2,
                &options_us);
    }
    else
    {
        size_t i, j;
        size_t option_index  = 0;
        size_t num_options   = 0;
        struct retro_core_option_definition *option_v1_defs_us = NULL;
        struct retro_variable *variables   = NULL;
        char **values_buf                  = NULL;

        while (true)
        {
            if (option_defs_us[num_options].key)
                num_options++;
            else
                break;
        }

        if (version >= 1)
        {
            option_v1_defs_us = (struct retro_core_option_definition *)
                    calloc(num_options + 1, sizeof(struct retro_core_option_definition));

            for (i = 0; i < num_options; i++)
            {
                struct retro_core_option_v2_definition *option_def_us = &option_defs_us[i];
                struct retro_core_option_value *option_values         = option_def_us->values;
                struct retro_core_option_definition *option_v1_def_us = &option_v1_defs_us[i];
                struct retro_core_option_value *option_v1_values      = option_v1_def_us->values;

                option_v1_def_us->key           = option_def_us->key;
                option_v1_def_us->desc          = option_def_us->desc;
                option_v1_def_us->info          = option_def_us->info;
                option_v1_def_us->default_value = option_def_us->default_value;

                while (option_values->value)
                {
                    option_v1_values->value = option_values->value;
                    option_v1_values->label = option_values->label;

                    option_values++;
                    option_v1_values++;
                }
            }

            environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS, option_v1_defs_us);
        }
        else
        {
            variables  = (struct retro_variable *)calloc(num_options + 1, sizeof(struct retro_variable));
            values_buf = (char **)calloc(num_options, sizeof(char *));

            if (!variables || !values_buf)
                goto error;

            for (i = 0; i < num_options; i++)
            {
                const char *key                        = option_defs_us[i].key;
                const char *desc                       = option_defs_us[i].desc;
                const char *default_value              = option_defs_us[i].default_value;
                struct retro_core_option_value *values  = option_defs_us[i].values;
                size_t buf_len                         = 3;
                size_t default_index                   = 0;

                values_buf[i] = NULL;

                if (desc)
                {
                    size_t num_values = 0;

                    while (true)
                    {
                        if (values[num_values].value)
                        {
                            if (default_value)
                                if (strcmp(values[num_values].value, default_value) == 0)
                                    default_index = num_values;

                            buf_len += strlen(values[num_values].value);
                            num_values++;
                        }
                        else
                            break;
                    }

                    if (num_values > 0)
                    {
                        buf_len += num_values - 1;
                        buf_len += strlen(desc);

                        values_buf[i] = (char *)calloc(buf_len, sizeof(char));
                        if (!values_buf[i])
                            goto error;

                        strcpy(values_buf[i], desc);
                        strcat(values_buf[i], "; ");

                        strcat(values_buf[i], values[default_index].value);

                        for (j = 0; j < num_values; j++)
                        {
                            if (j != default_index)
                            {
                                strcat(values_buf[i], "|");
                                strcat(values_buf[i], values[j].value);
                            }
                        }
                    }
                }

                variables[option_index].key   = key;
                variables[option_index].value = values_buf[i];
                option_index++;
            }

            environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
        }

error:
        if (option_v1_defs_us)
        {
            free(option_v1_defs_us);
            option_v1_defs_us = NULL;
        }

        if (values_buf)
        {
            for (i = 0; i < num_options; i++)
            {
                if (values_buf[i])
                {
                    free(values_buf[i]);
                    values_buf[i] = NULL;
                }
            }

            free(values_buf);
            values_buf = NULL;
        }

        if (variables)
        {
            free(variables);
            variables = NULL;
        }
    }
}

#ifdef __cplusplus
}
#endif

#endif
