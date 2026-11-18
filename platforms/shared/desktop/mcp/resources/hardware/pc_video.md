# CGA, EGA, VGA and the Cirrus Logic Cards — What MS-DOS Software Relies On

## 1. Overview

The machine has one video card, picked by the Video Card machine setting and applied at reset:

| Setting | Card | Bus | Memory | Used by Auto |
|---|---|---|---|---|
| `cga` | IBM Color/Graphics Monitor Adapter | 8-bit ISA | 16 KB | — |
| `ega` | IBM Enhanced Graphics Adapter, Enhanced Color Display | 8-bit ISA | 256 KB | — |
| `vga` | IBM-compatible VGA | ISA | 256 KB | — |
| `gd5428` | Cirrus Logic CL-GD5428 | ISA | 2 MB | The 386s |
| `gd5428_vlb` | Cirrus Logic CL-GD5428 | VL-Bus | 2 MB | The 486s |
| `gd5446` | Cirrus Logic CL-GD5446 | PCI | 2 MB | The Pentiums |

- **Command line:** `--video auto|cga|ega|vga|gd5428|gd5428_vlb|gd5446`. **Libretro:** `geardox_video_card`. **Menu:**
  Emulator, Machine, Video Card.
- **The VL-Bus card needs a 486 and the PCI card a Pentium.** On other models the machine falls back to the ISA
  GD5428 and `get_system_status` lists a warning.
- **The CGA and the EGA** are covered in §4. The GD5446 is the GD5428 of §3 with 2 MB on the PCI bus; its
  configuration space, aperture and extra VBE modes are in the `pentium_pci` resource.
- **The VGA card is today's VGA bit for bit.** The 5428 is a VGA superset: every standard mode works the same, plus the
  extended registers, 2 MB of memory, banking, a linear aperture and 8, 15, 16 and 24 bpp modes.
- `get_system_status` reports the card under `machine.video` (`card`, `id`, `vram_kb`) and the setting under
  `machine.settings.video`.

---

## 2. VGA Ports

Video ports decode ten address lines, so each repeats every 400h. Miscellaneous output bit 0 picks the 3Bxh or the 3Dxh
CRTC and status block, and the other block reads FFh.

| Port | Read | Write |
|---|---|---|
| 3C0h | Attribute index | Attribute index or data, by a flip-flop that 3DAh resets |
| 3C1h | Attribute data | — |
| 3C2h | Input status 0: bit 4 switch sense (color monitor), bit 7 vertical interrupt | Miscellaneous output |
| 3C3h | VGA enable | VGA enable, bit 0 |
| 3C4h–3C5h | Sequencer index, data | Same |
| 3C6h | Pel mask | Pel mask |
| 3C7h | DAC state | DAC read address |
| 3C8h–3C9h | DAC address, data | DAC write address, data |
| 3CAh, 3CCh | Feature control, miscellaneous output | — |
| 3CEh–3CFh | Graphics index, data | Same |
| 3B4h–3B5h, 3D4h–3D5h | CRTC index, data | Same |
| 3BAh, 3DAh | Input status 1: display enable, vertical retrace, computed from the beam | Feature control |

- **Timing is the real one.** 25.175 and 28.322 MHz dot clocks give 31.47 kHz lines, about 70.09 Hz in the 400-line
  modes and 59.94 Hz at 480 lines. Status bits come from the machine clock, so retrace loops see real edges.
- **The standard modes:** text 00h–03h and 07h, CGA 04h–06h, EGA and VGA planar 0Dh–12h, and 13h with its unchained
  variants. The whole write pipeline is modelled: latches, write modes 0–3, read modes 0–1, rotate, logic ops,
  set/reset, bit mask, odd/even, chain-4, byte, word and doubleword CRTC addressing, split screen and panning.
- **Memory layout:** 256 KB held as four 64 KB banks by the low two bits of the plane offset, repeated for each 256 KB on
  the 5428. A packed-pixel mode is then linear in host memory in order. The debugger's plane views and `get_frame_buffer`
  show the first 256 KB of each plane.

---

## 3. The Cirrus Extensions

Sequencer indexes run SR00–SR1F, graphics GR00–GR3F and the CRTC CR00–CR27. With the `vga` card the indexes stay the
VGA's.

### 3.1 Lock and identification

- **SR06 unlocks the extensions.** Writing 12h (bit 7 is not part of the key, so 92h works) unlocks and reads back 12h.
  Anything else locks and reads back 0Fh. While locked, SR07–SR1F, GR09–GR3F, CR19–CR27 and the hidden DAC drop writes
  and read FFh.
- **CR27 reads 98h,** the CL-GD5428 chip ID. SeaVGABIOS checks it, SR06 and SR0F at setup.
- **SR0F** reads as a 2 MB, 32-bit DRAM bus with a 50.11 MHz memory clock. SR17 bits 5–3 read 7.

### 3.2 What is implemented

| Register | Meaning |
|---|---|
| SR07 | Bit 0 extended mode, bits 3–1 pixel path (0 for 8 bpp, 3 for 15 and 16 bpp, 2 for 24 bpp, 1 for clock/2), bits 7–4 linear aperture in MB. The GD5428 runs 1 and 2 at twice and three times the pixel clock; the GD5446 runs every path at the pixel clock |
| SR0B–SR0E, SR1B–SR1E | VCLK0–3. Rate is 14.31818 MHz x N / D, halved when D's bit 0 is set. N is bits 6–0 of SR0B–SR0E, D is bits 5–1 of SR1B–SR1E. Miscellaneous output bits 3–2 pick one |
| GR09, GR0A | Bank offsets, in 4 KB units, or 16 KB with GR0B bit 5 |
| GR0B | Bit 0 dual page (A0000h–A7FFFh uses GR09, A8000h–AFFFFh uses GR0A), bit 5 16 KB units |
| CR1A | Blank end bits and bit 1, start address latched at vertical sync |
| CR1B | Bit 0 and bits 3–2 start address bits 16–18, bit 4 offset bit 8, bit 1 wide address counter (19 bits), bits 5 and 7 blanking |
| CR1D | Kept and read back (a 5429 register, which SeaVGABIOS reads) |
| CR25, CR27 | Part status and chip ID, read only |
| Hidden DAC | Four reads of 3C6h, then the next access of 3C6h reaches it. Reads of 3C7h–3C9h relock it. It picks the pixel format |

- **Banking:** a single page is `GR09 x 4 KB + offset in the window`. Extended modes use a 64 KB window at A0000h
  (GR06 = 05h). A bank past the installed 2 MB reaches nothing: reads give FFh and writes are lost.
- **Extended mode** (SR07 bit 0) reaches all of video memory. The display reads bytes in order from 4 x the start
  address, `offset x 8` bytes per row, pixels little endian.
- **The linear aperture** is a 2 MB block at SR07's bits 7–4 in MB (0 is off), on an even MB when GR0B bit 5 is set,
  through the same layout as the window. RAM wins where it covers the block, except in the board's memory hole at 14–16
  MB, which a Cirrus card with 16 MB of RAM or more always has (§4). The built-in VGA BIOS puts the aperture at 14 MB
  (E00000h) and reports it through VBE 4F01h. The debugger shows it as the memory region "Linear VRAM".
- **Hidden DAC formats:**

| Hidden DAC | Format | Bytes per pixel |
|---|---|---|
| 00h | 8 bpp through the palette | 1 |
| F0h | 5:5:5 | 2 |
| E1h | 5:6:5 | 2 |
| E5h | 8:8:8 (B, G, R byte order) | 3 |
| bit 7 set, bit 6 clear | 5:5:5, with bit 4 mixing it with the palette | 2 |
| bit 7 and 6 set, low nibble 8, 9, 6 or 7 | Grey, 3:3:2, blank (6 and 7) | 1 |

- **Direct colour bypasses the palette,** and the pel mask applies to the palette only.

### 3.3 BitBLT, extended writes and the hardware cursor

- **BitBLT (GR20–GR39):** GR31 bit 1 starts a blit and bit 2 resets the engine. Screen to screen copies, forwards or
  backwards (GR30 bit 0), the 16 Cirrus ROPs (GR32), 8x8 patterns (GR30 bit 6) and colour expansion (bit 7) with the
  foreground in GR01, GR11, GR13, GR15 and the background in GR00, GR10, GR12, GR14. GR30 bits 5–4 give 1 to 4 bytes per
  pixel (bit 4 only, 1 or 2, on the 5428). Transparency (GR30 bit 3) compares the result with GR34–GR35 under the
  GR38–GR39 mask on the 5428. On the 5446 it skips background pixels of an expansion, or compares GR34–GR35 otherwise.
- **System memory blits:** a source from the CPU (GR30 bit 2) takes its data through any write to the A0000h window or
  the aperture, and a destination in system memory (5428, GR30 bit 1) gives it through reads. Plain data pads each line
  to a dword. Colour expanded data runs on across lines, each line dword aligned only with GR33 bit 0 on the 5446, and
  the whole blit pads to a dword. Every other blit finishes when it starts. GR31 bit 0 reads 1 only while a system
  memory blit waits for its data, and the start registers then hold where it ended.
- **5446 additions:** GR33 bit 2 solid fill, bit 1 inverted expansion, bit 0 dword-aligned expanded system data. GR2F
  bits 2–0 skip the first pixels of each line of an expansion or a pattern (bits 4–0, in bytes, at 24 bpp), and bits 6–5
  the first bytes of system data. A pattern sits on a boundary of its own size, and the source's low three bits pick its
  first line. With GR31 bit 7, writing GR2A starts the blit. The registers are also memory mapped, at B8000h with SR17
  bit 2 (and GR06's map 1) or in the last 256 bytes of the aperture with bit 6: 00h–07h colours, 08h–1Bh GR20–GR33,
  1Ch–1Dh GR34–GR35 and 40h GR31. Only 40h reads back.
- **Extended writes (GR0B):** bit 2 makes GR00, GR01, GR05 and SR02 8 bits wide and turns on write modes 4 and 5, which
  expand each CPU bit to a foreground (GR01) or, in mode 5, background (GR00) pixel under SR02's pixel mask, one or two
  bytes per pixel (bit 4). Bit 3 gives write mode 1 and its reads 8 latches, for 8 pixels a byte. Bits 1 and 4 make the
  window address count in units of 8 or 16 bytes. On the 5446, Set/Reset, rotate, the logical function, write modes 2
  and 3 and read mode 1 are off while chain-4 and extended mode are both on.
- **Hardware cursor (SR10–SR13):** SR12 bit 0 shows it, bit 2 picks 64x64 over 32x32. The position is SR10 and SR11
  shifted left by 3, with the low three bits from the index bits 7–5 of the write. SR13 picks the pattern in the last
  16 KB of video memory. Two bit planes per pixel: 0, 0 transparent, 0, 1 the extended DAC entry 0 (written with SR12
  bit 1 set), 1, 1 entry 15, 1, 0 inverts the screen.

Stored only: the colour key (GR0C, GR0D), GR0E, interlace (CR19, CR1A bit 0), the scratch registers (SR09, SR0A, SR14,
SR15), the EEPROM, FIFO and signature registers (SR08, SR16, SR18, SR19) and the memory clock (SR1F). The sleep ports
46E8h, 102h and 94h are not decoded.

### 3.4 Timing

The VGA and ISA cards use the model's fitted VGA wait states. The VL-Bus card has its own, shorter ones. Both pace
the CPU's accesses to video memory, so a framebuffer-heavy game runs slower on an ISA card than on a VL-Bus one.

---

## 4. The CGA and the EGA

- **CGA:** 16 KB at B8000h, a Motorola 6845 CRTC at 3D4h/3D5h (R0–R17), the mode register at 3D8h, the colour
  register at 3D9h and the status at 3DAh. Modes 0–6 only: 40 and 80 column text, 320x200 in four colours and 640x200
  in two. Its BIOS is SeaVGABIOS's CGA INT 10h, which SeaBIOS places at C8000h; nothing answers at C000:0000h.
- **CGA output:** Video, CGA Output picks Auto (the default), RGB Monitor or Composite Monitor, at once. Auto shows a
  composite monitor's artifact colours in 640x200 graphics with the colour burst on, and RGB otherwise. Libretro has
  `geardox_cga_output`.
- **EGA:** 256 KB in four planes at A0000h, the EGA's registers at the VGA's ports, with an Enhanced Color Display:
  350-line modes in 64 colours, 200-line modes in 16. There is no DAC; the attribute palette picks monitor colours.
  **Its registers are write only:** only CR0C–CR0F, the input status registers and the light pen read back, so the BIOS
  and drivers keep their own copies. The switch setting reads through input status 0.

---

## 5. The VGA BIOS and VBE

The built-in firmware carries a VGA BIOS per card: SeaVGABIOS for a plain VGA, its EGA and CGA builds, its Cirrus
build for the 5428, which sits at C0000h and ends lower in upper memory, and a PCI Cirrus build for the GD5446.
`get_system_status` (`bios`) tells which one is loaded.

- **Plain VGA build:** VBE 3.0 code with the VGA's own modes only, no banks and no linear buffer.
- **Cirrus build:** reports VBE 3.0 and a 6-bit DAC. 4F0Ah returns VBE 2.0's protected mode interface (set window,
  display start and palette as 32-bit code over ports), and 4F01h's window function is a far call that only touches
  ports. The modes that fit 2 MB are:

| VESA | Mode |
|---|---|
| 150h, 152h | 320x200 and 320x400, 8 bpp, 70 Hz, the dot clock halved (no Cirrus number) |
| 100h | 640x400, 8 bpp, 70 Hz (Cirrus 5Eh) |
| 101h | 640x480, 8 bpp |
| 103h | 800x600, 8 bpp |
| 105h | 1024x768, 8 bpp |
| 110h, 111h | 640x480, 15 and 16 bpp |
| 113h, 114h | 800x600, 15 and 16 bpp |
| 107h | 1280x1024, 8 bpp |
| 116h, 117h | 1024x768, 15 and 16 bpp |
| 112h | 640x480, 24 bpp, clock/3 at 75.5 MHz on the GD5428, 25.2 MHz on the GD5446 |

  Nothing that needs more than 2 MB, and no other 24-bit mode. The VGA modes, including the planar 6Ah, stay.
- **Banked access:** a 64 KB window at A0000h in 4 KB steps through 4F05h. **Linear access:** the aperture of §3.2,
  with PhysBasePtr in 4F01h. With 16 MB or more the AT boards leave a memory hole at 14-16 MB for it, as 486 board
  setups did: RAM there is lost, extended memory stops at 14 MB, and the E820 map has RAM below 14 MB and from 16 MB up.
  The Pentium board's 430FX can only leave 15-16 MB, so there an ISA GD5428 with 16 MB or more runs banked only.
- **UniVBE** and similar drivers identify the chip from its registers and use the aperture through SR07.

---

## 6. Debugging It

- **VGA window:** display and beam state for every card. With a Cirrus card it adds a CIRRUS section: the card, locked or
  unlocked, extended mode, SR07 decoded, the four VCLKs in MHz with the selected one highlighted, the bank offsets and
  dual page, the aperture base or off, the hidden DAC and its format, the extended start, offset and pitch, the wide
  counter, and a mode line with resolution, depth and refresh. A BITBLT section shows the engine's state, the decoded
  registers, the bytes fed to a system memory blit, and the blits and remaps of the last frame. A HARDWARE CURSOR
  section shows its position, size, pattern address and colours, and draws the pattern (magenta for inverted pixels).
- **VGA Registers window:** the VGA registers, plus SR05–SR1F, GR09–GR11 and GR20–GR3A, and CR19–CR1D, CR25–CR27 with a
  Cirrus card. All of them are editable.
- **VRAM Viewer:** the plane, planar, 256 color and text tabs read the first 256 KB. The Linear tab decodes all the
  card's memory from a start byte with a pitch in bytes, at 8 bpp (DAC palette), 15 bpp 5:5:5, 16 bpp 5:6:5 or 24 bpp
  8:8:8. Auto follows the current extended mode.
- **Memory editor:** the "Linear VRAM" region when the aperture is on.
- **MCP:**
  - `get_system_status`: the card and its memory.
  - `get_video_output_status`: `card`, and with a Cirrus card a `cirrus` object: `unlocked`, `extended_mode`, `format`
    (`palette`, `grey`, `332`, `555`, `555_mixed`, `565`, `888`, `blank`), `bytes_per_pixel`, `hidden_dac`,
    `pixel_path`, `vclk_hz`, `vclk_selected`, `dot_clock_hz`, `bank_0`, `bank_1`, `dual_page`, `aperture_base` (null when
    off), `wide_counter`, `offset`, `pitch_bytes`, `start_address` and `start_byte`.
  - `get_vga_registers`: with a Cirrus card a `cirrus` object with `unlocked`, `sequencer` (SR00–SR1F), `graphics`
    (GR00–GR3F), `crtc` (CR19–CR27, from `crtc_first`) and `hidden_dac`. A `blt` object decodes the engine: `state`
    (`idle`, `busy`, `waiting_cpu`), `status`, `mode`, `rop`, `width_bytes`, `height`, `bytes_per_pixel`, `source`,
    `destination`, both pitches, `line`, `bytes_fed`, `bytes_needed`, `started`, `handlers` (the CPU's writes go through
    the engine) and `last_frame` (blits by kind and remaps in the frame before). A `cursor` object gives `enabled`,
    `size`, `x`, `y`, `pattern_address` and its two `colors`.
  - `get_frame_buffer`: buffer `linear` takes `format` (8, 15, 16 or 24), `pitch` in bytes and `start` as a byte offset,
    and each defaults to the current mode.
  - `get_crtc_status`, `get_crtc_registers`, `get_palettes` and `get_text_screen` are the VGA's.
  - On a CGA, `get_crtc_status`, `get_crtc_registers` and `write_crtc_register` take the 6845 (R0–R17);
    `get_video_output_status` drops the VGA's memory, pipeline, attribute and DAC parts for a `cga` object (`mode`,
    `color`, the decoded mode bits, `color_index`, `intense_palette`, `palette`, `output` and `output_setting`);
    `get_vga_registers` returns `card`, `crtc6845`, `mode` and `color`; `get_palettes` returns the 16 `cga` colours;
    `get_text_screen` reads its text.
  - On an EGA, `get_video_output_status` adds an `ega` object (`monitor_lines`, `monitor_colors`, `switches`,
    `sensed_switch`, `status_mux`, `graphics_position`, `dot_clock_hz`), `get_vga_registers` an `ega` object with what
    reads back, and `get_palettes` a `monitor` list (64 or 16 colours) with `monitor_lines`.
  - `list_memory_areas` and the memory tools see "Linear VRAM".
- **Trace:** the Video group's Sequencer, Graphics, Attribute events record register writes with their index, the
  extended ones included. Its BitBLT event records each blit's start: size, source, destination, mode, ROP and the 5446's
  GR33.
