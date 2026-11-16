# PC/AT Board — Memory, I/O, Interrupts, Timers, BIOS and CMOS

## 1. Overview

Geardox emulates an IBM PC/AT compatible. The 386 and 486 models are ISA boards with no chipset registers: a 386SX-20
(24 address lines, 16-bit bus), a 386DX-33, a 486SX-33, a 486DX2-66 (the default) and a 486DX4-100, the 486s with a
VL-Bus. The Pentium 75, 90, 133, 166 and 200 sit on a 430FX PCI board, described in the `pentium_pci` resource. Board
timing comes from the 14.31818 MHz oscillator: the PIT counts at a twelfth of it (1,193,182 Hz) and ISA cycles run at
about 8.33 MHz. The video card is an IBM CGA or EGA, a VGA, a Cirrus Logic CL-GD5428 (ISA, or VL-Bus on a 486) or a
PCI CL-GD5446 on a Pentium, described in the `pc_video` resource. Auto picks the GD5428 ISA on a 386, the VL-Bus one on
a 486 and the GD5446 on a Pentium.

---

## 2. Memory Map

| Range | Contents | Notes |
|---|---|---|
| 00000h–9FFFFh | Conventional RAM | 640 KB |
| A0000h–BFFFFh | Video aperture | The video card's window, see the `pc_video` resource. Open bus (FFh) with no card |
| C0000h–DFFFFh | Upper memory | Board RAM, with the card's VGA BIOS copied to C0000h (a CGA's BIOS goes to C8000h and nothing answers at C0000h). Locked at the end of POST, see §7 |
| E0000h–FFFFFh | BIOS copy | Board RAM filled from the BIOS image at power on. Writable during POST, the F segment read-only once locked |
| 100000h–top of RAM | Extended RAM | Absent with 1 MB of RAM. With a GD5428 (ISA or VL-Bus) and 16 MB or more, F00000h–FFFFFFh is a hole for the card's linear frame buffer and that RAM is lost. The PCI GD5446 maps its buffer at BAR0 instead |
| Top of address space | System BIOS ROM | The pristine image, 64, 128 or 256 KB, ending at FFFFFFFFh (FFFFFFh on a 386SX) |

- The CPU starts at FFFFFFF0h, in the ROM image at the top, and jumps into the copy below 1 MB.
- A CPU reset (port 92h, triple fault) keeps RAM, the BIOS copy and A20 as they are. Only power on and the
  emulator's reset refill the copy.
- The debugger shows main RAM as one region, the ROM image as "System BIOS" and the copy as "BIOS Copy".

### 2.1 A20 Gate

With A20 off, physical address bit 20 reads as 0, so FFFF:0010h–FFFF:FFFFh wrap to 0000:0000h–0000:FFEFh as on an
8086. The gate is open when port 92h bit 1 or the 8042's output port bit 1 asks for it. A CPU reset opens it, so the
CPU can fetch its reset vector, until one of the two is written again. A20 is on at power on.

---

## 3. I/O Map

Board ports decode all 16 address lines. Unused ports read FFh and drop writes. Every access takes ISA I/O time.
`read_memory` on the I/O PORTS area reads ports without side effects; `write_io_port` writes one as an OUT does.

| Port | Device | Read | Write |
|---|---|---|---|
| 20h–21h | Master 8259A | IRR/ISR, IMR | ICW1–ICW4, OCW1–OCW3 |
| 40h–42h | 8254 counters 0–2 | Count or status | Count |
| 43h | 8254 control | FFh | Control word, counter latch, read-back |
| 61h | System control B | Bit 0 speaker gate, bit 1 speaker data, bits 2–3 as written, bit 4 refresh toggle, bit 5 OUT2 | Bits 0–3 |
| 70h | CMOS index | — | Bits 0–6 index, bit 7 masks NMI |
| 80h | POST code | Last value | Latches a POST code |
| 92h | System control A | Bit 1 A20, bit 0 as written | Bit 1 A20, rising bit 0 resets the CPU |
| A0h–A1h | Slave 8259A | IRR/ISR, IMR | ICW1–ICW4, OCW1–OCW3 |
| 71h | CMOS data | MC146818 register or RAM byte | Register or RAM byte |
| F0h | Coprocessor | — | Clears the coprocessor error and IRQ13 |
| F1h | Coprocessor | — | Resets the coprocessor |
| 402h | Debug console | E9h | Appends a byte to the BIOS log |
| 404h–407h | Shadow register | `GDOX` | The upper memory map SeaBIOS locks at the end of POST |
| 408h | APM power off | — | Powers the machine off |
| 510h–511h | Firmware config | 511h: next data byte | 510h: 16-bit selector |
| 4D0h–4D1h, CF8h–CFFh, CF9h | Pentium models | ELCR, PCI configuration, reset control | See the `pentium_pci` resource |

---

## 4. Interrupts

Two 8259A PICs, the slave cascaded on the master's IR2, edge triggered. The BIOS programs the master base to 08h and
the slave to 70h; in protected mode the master's vectors overlap exceptions 8–15, so DOS extenders move them. An ISA
card on IRQ2 reaches the slave's IR1, IRQ9.

| Vector | IRQ | Source |
|---:|---:|---|
| 08h | 0 | PIT counter 0 |
| 09h | 1 | Keyboard |
| 0Ah | 2 | Slave PIC (cascade) |
| 0Bh | 3 | COM2 |
| 0Ch | 4 | COM1 |
| 0Dh | 5 | LPT2, often a sound card |
| 0Eh | 6 | Floppy disk |
| 0Fh | 7 | LPT1 |
| 70h | 8 | Real-time clock |
| 71h | 9 | Redirected IRQ2 |
| 72h–73h | 10–11 | Free |
| 74h | 12 | PS/2 mouse |
| 75h | 13 | Math coprocessor |
| 76h | 14 | Primary IDE |
| 77h | 15 | Secondary IDE |

The PICs run without automatic EOI: a handler sends an EOI to the PIC (to the slave and to the master for IRQ8–15)
before that line, or one of lower priority, can interrupt again.

An unmasked coprocessor error with CR0.NE clear raises IRQ13 instead of exception 16 and stays latched until a write
to port F0h. A 486 with CR0.NE set takes exception 16.

---

## 5. Timers

One 8254 at 40h–43h, clocked at 1,193,182 Hz:

| Counter | Gate | Output | BIOS setting |
|---:|---|---|---|
| 0 | Tied high | IRQ0, one request per rising edge | Mode 3, count 0: 18.2 Hz |
| 1 | Tied high | Refresh request, toggles port 61h bit 4 | Mode 2, count 18: 15 µs |
| 2 | Port 61h bit 0 | Port 61h bit 5, and the speaker through port 61h bit 1 | Set by programs for tones and delays |

Read-back (control word 11xxxxxxb) latches the count and the status byte of the selected counters. Before counter 1 is
programmed, the refresh toggle still flips every 15 µs.

---

## 6. Resets

| Source | Effect |
|---|---|
| Power on, emulator reset | Everything resets and the BIOS copy is refilled |
| Port 92h, rising bit 0 | CPU only |
| 8042 command FEh, or a write of the output port with bit 0 clear | CPU only |
| Triple fault (shutdown cycle) | CPU only |
| CF9h on the Pentium models | CPU only, see the `pentium_pci` resource |

After a CPU reset the BIOS reads the CMOS shutdown code to decide whether to run POST again or to return to a
program through 40:67h.

---

## 7. BIOS

The BIOS is built in, SeaBIOS with SeaVGABIOS; there is no other. The 386 and 486 boards run its ISA build, the
Pentium models a second image with the PCI BIOS (INT 1Ah B1h) and a $PIR table. The VGA BIOS is one per video card:
the plain SeaVGABIOS for the VGA, its Cirrus build for the CL-GD5428 cards (larger, so less upper memory is left), a
PCI Cirrus build for the CL-GD5446, an EGA build, and for the CGA an INT 10h that SeaBIOS places at C8000h and enters
from F000:F065, where an AT keeps its video BIOS.

| Item | What the board gives SeaBIOS |
|---|---|
| Upper memory and BIOS copy | Writable RAM during POST. At the end of POST SeaBIOS locks them through the shadow register at 404h–407h: the option ROMs and the F segment become read-only and the free space between the option ROMs and SeaBIOS's low zone has nothing there, so UMB managers find it free |
| VGA BIOS | Firmware config file `vgaroms/vgabios.bin`, or `pci1013,00b8.rom` for the GD5446, copied to C0000h by SeaBIOS |
| Firmware config (510h–511h) | The fw_cfg interface SeaBIOS reads, a subset: signature, ID 1 (no DMA), file directory, VGA BIOS, boot menu settings, the game target, and `etc/e820` with the RAM around the 15–16 MB hole when there is one |
| Debug console (402h) | SeaBIOS messages, read with `get_bios_log` |
| CMOS | Generated at every power on, see §8 |

SeaBIOS boots in the order floppy, hard disk, CD-ROM unless the boot device setting puts another first.

## 8. CMOS

A Motorola MC146818 at 70h/71h: 14 clock and status registers and 114 bytes of battery-backed RAM. The clock starts
from the host's local time (or a fixed date) and counts machine time; status A's update-in-progress bit is set for the
last 244 µs of each second. The periodic, alarm and update-ended interrupts raise IRQ8 through status C.

SeaBIOS has no setup screen, so with it the board writes these bytes at power on:

| Bytes | Content |
|---|---|
| 0Fh | Shutdown status 0 |
| 10h | Floppy drive types, drive A in bits 7–4 and B in bits 3–0: 0 none, 1 360 KB, 2 1.2 MB, 3 720 KB, 4 1.44 MB, 5 2.88 MB |
| 12h, 19h–1Ah | Hard disks 0 and 1: Fh in their nibble of 12h and type 47 in 19h and 1Ah |
| 14h | Equipment: bit 0 floppy drives fitted, bits 7–6 their count less one, bit 1 coprocessor, bits 5–4 display (00b EGA or VGA, 10b CGA) |
| 15h–16h | Base memory, 640 KB |
| 17h–18h, 30h–31h | Extended memory in KB, up to 63 MB, or 14 MB below the 15–16 MB hole |
| 2Eh–2Fh | Checksum of 10h–2Dh, high byte first |
| 32h | Century, BCD |
| 34h–35h | Memory above 16 MB in 64 KB units |
| 38h | Bit 0 skips the floppy boot signature check; bits 4–7 third boot device |
| 39h | BIOS translation of hard disks 0–3, two bits each |
| 3Dh | Bits 0–3 first, bits 4–7 second boot device (1 floppy, 2 hard disk, 3 CD-ROM) |
