# PC/AT Board — Memory, I/O, Interrupts, Timers, BIOS and CMOS

## 1. Overview

Geardox emulates an ISA-only IBM PC/AT compatible: no PCI and no chipset registers. Models are a 386SX-20 (24 address
lines, 16-bit bus), a 386DX-33 and a 486DX2-66. Board timing comes from the 14.31818 MHz oscillator: the PIT counts at
a twelfth of it (1,193,182 Hz) and ISA cycles run at about 8.33 MHz. The video card is an ISA VGA or a Cirrus Logic
CL-GD5428 (a VL-Bus one on the 486), described in the `pc_video` resource.

---

## 2. Memory Map

| Range | Contents | Notes |
|---|---|---|
| 00000h–9FFFFh | Conventional RAM | 640 KB |
| A0000h–BFFFFh | Video aperture | The video card's window, see the `pc_video` resource. Open bus (FFh) with no card |
| C0000h–DFFFFh | Upper memory | SeaBIOS: board RAM, writable, with the built-in VGA BIOS of the card copied to C0000h. Other BIOSes: the VGA BIOS ROM at C0000h, open bus above |
| E0000h–FFFFFh | BIOS copy | Board RAM filled from the BIOS image at power on. Writable for SeaBIOS, read-only otherwise |
| 100000h–top of RAM | Extended RAM | Absent with 1 MB of RAM. With a Cirrus card and 16 MB or more, F00000h–FFFFFFh is a hole for the card's linear frame buffer and that RAM is lost |
| Top of address space | System BIOS ROM | The pristine image, 64, 128 or 256 KB, ending at FFFFFFFFh (FFFFFFh on a 386SX) |

- The CPU starts at FFFFFFF0h, in the ROM image at the top, and jumps into the copy below 1 MB.
- A CPU reset (port 92h, triple fault) keeps RAM, the BIOS copy and A20 as they are. Only power on and the
  emulator's reset refill the copy.
- The debugger shows main RAM as one region, the ROM image as "System BIOS" and the copy as "BIOS Copy".

### 2.1 A20 Gate

With A20 off, physical address bit 20 reads as 0, so FFFF:0010h–FFFF:FFFFh wrap to 0000:0000h–0000:FFEFh as on an
8086. Port 92h bit 1 drives it; the 8042 output port joins in when the keyboard controller is emulated. A20 is on at
power on.

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
| 402h | Debug console | E9h | Appends a byte to the BIOS log (SeaBIOS only) |
| 510h–511h | Firmware config | 511h: next data byte | 510h: 16-bit selector (SeaBIOS only) |

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
| Triple fault (shutdown cycle) | CPU only |

After a CPU reset the BIOS reads the CMOS shutdown code to decide whether to run POST again or to return to a
program through 40:67h.

---

## 7. BIOS

The built-in BIOS is SeaBIOS with SeaVGABIOS, built for this ISA board. The VGA BIOS is one per video card: the plain
SeaVGABIOS for the `vga` card, or its Cirrus build for the CL-GD5428 cards, which is larger and leaves less upper memory.
A BIOS image the user supplies replaces the system BIOS; an image with the SeaBIOS banner gets the SeaBIOS mapping, any
other image the AT mapping.

| | SeaBIOS mapping | AT mapping |
|---|---|---|
| Upper memory and BIOS copy | Writable RAM | Read-only, open bus between the VGA BIOS and the copy |
| VGA BIOS | Firmware config file `vgaroms/vgabios.bin` (the built-in image of the card, or the user's), copied to C0000h by SeaBIOS | ROM at C0000h, run by the BIOS's option ROM scan |
| Firmware config (510h–511h) | The fw_cfg interface SeaBIOS reads, a subset: signature, ID 1 (no DMA), file directory, VGA BIOS, boot menu settings, and `etc/e820` with the RAM around the 15–16 MB hole when there is one | Absent |
| Debug console (402h) | SeaBIOS messages, read with `get_bios_log` | Absent |
| CMOS | Generated at every power on | Kept |

SeaBIOS boots in the order floppy, hard disk, CD-ROM unless the boot device setting puts another first. Without a
keyboard controller its keyboard init times out; that is expected until the 8042 is emulated.

## 8. CMOS

A Motorola MC146818 at 70h/71h: 14 clock and status registers and 114 bytes of battery-backed RAM. The clock starts
from the host's local time (or a fixed date) and counts machine time; status A's update-in-progress bit is set for the
last 244 µs of each second. Alarm, periodic and update-ended interrupts are not emulated yet.

SeaBIOS has no setup screen, so with it the board writes these bytes at power on:

| Bytes | Content |
|---|---|
| 0Fh | Shutdown status 0 |
| 10h | Floppy drive types (none until the floppy controller is emulated) |
| 14h | Equipment: bit 1 coprocessor, VGA display |
| 15h–16h | Base memory, 640 KB |
| 17h–18h, 30h–31h | Extended memory in KB, up to 63 MB, or 14 MB below the 15–16 MB hole |
| 2Eh–2Fh | Checksum of 10h–2Dh, high byte first |
| 32h | Century, BCD |
| 34h–35h | Memory above 16 MB in 64 KB units |
| 38h | Bit 0 skips the floppy boot signature check; bits 4–7 third boot device |
| 3Dh | Bits 0–3 first, bits 4–7 second boot device (1 floppy, 2 hard disk, 3 CD-ROM) |
