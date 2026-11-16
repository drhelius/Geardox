# Pentium on a 430FX PCI Board — What MS-DOS Software Relies On

## 1. Overview

Five models put a Pentium (P54C) on a PCI board. The CPU is the 486 core with a few new instructions and its own timing
model, so the `i386_cpu` resource still applies. The board is the AT board of `pc_at_board` (PICs, PIT, DMA, CMOS, 8042,
floppy, ISA cards, A20) with a chipset added: an Intel 430FX (82437FX host bridge) and a PIIX (82371FB). The video card
is a PCI Cirrus GD5446, a 2 MB sibling of the GD5428 in `pc_video`.

| Model key | CPU clock | Bus clock | Ratio | EDX at reset, CPUID signature |
|---|---:|---:|---:|---|
| `pentium_75` | 75 MHz | 50 MHz | 1.5 | 0521h (stepping B1) |
| `pentium_90` | 90 MHz | 60 MHz | 1.5 | 0521h |
| `pentium_133` | 133.33 MHz | 66.67 MHz | 2 | 0525h (C2) |
| `pentium_166` | 166.67 MHz | 66.67 MHz | 2.5 | 052Ch (cC0) |
| `pentium_200` | 200 MHz | 66.67 MHz | 3 | 052Ch |

- **The board is "Generic Pentium 430FX".** RAM is 8, 16, 32, 64 or 128 MB, 16 MB by default (the AT boards: 8). The PCI
  clock is half the bus clock (25, 30 or 33.33 MHz); ISA stays at 8.33 MHz.
- **Selecting it:** `--model pentium_133`, `--video gd5446`, or `model =` and `video =` in a `.gdox` descriptor.
  `--cpu-mhz` changes the CPU clock only; the bus clock and the signature follow the model.
- **Video fallbacks:** Auto picks the GD5446. The VL-Bus card falls back to the ISA GD5428 here ("This machine has no
  VL-Bus slot"), and the GD5446 on an AT model does too ("This machine has no PCI slot"). ISA cards work in both. On the
  AT models CF8h–CFFh, 4D0h–4D1h and CF9h read FFh and drop writes.
- **The PCI board runs the CPU looser:** a compiled block may run past the end of a slice, software interrupts don't end
  a batch, and a slice is 40,000 machine clocks, not 20,000, so device events can come a few hundred clocks late.

---

## 2. The Pentium Core

The 486's instructions and behaviour stay (BSWAP, XADD, CMPXCHG, INVD, WBINVD, INVLPG, EFLAGS.AC, CR0.NE/WP/AM/NW/CD, #AC,
the exact on-chip x87 without the FDIV flaw). CR0 resets to 60000010h. Not implemented: MMX, SMM and RSM, the APIC.

### 2.1 CPUID

CPUID exists on all five models and EFLAGS.ID (bit 21) toggles. The DX4 has it too; the 386s and other 486s raise #UD.

| EAX | Returns |
|---:|---|
| 0 | EAX 1, EBX 756E6547h, EDX 49656E69h, ECX 6C65746Eh ("GenuineIntel") |
| 1 | EAX the model's signature (family 5, model 2), EBX and ECX 0, EDX 000001B1h |
| other | All registers 0 |

EDX 1B1h reports FPU (bit 0), TSC (4), MSR (5), MCE (7) and CX8 (8). A real P54C reports 1BFh, adding VME (1), DE (2) and
PSE (3); Geardox leaves those out because it doesn't implement them.

### 2.2 CR4

MOV CR4 exists on the Pentium (#UD on a 386 or 486). Bits 0–4 and 6 (5Fh) are writable; a 1 in any other bit raises
#GP(0). CR4 is 0 at power on and after every CPU reset. Only TSD (bit 2) does anything: RDTSC raises #GP(0) above CPL 0,
so always in virtual-8086 mode. VME, PVI, DE, PSE and MCE (bits 0, 1, 3, 4, 6) are stored with no effect: no virtual
interrupts, I/O breakpoints, 4 MB pages (PDE bit 7) or exception 18.

### 2.3 Time Stamp Counter

- **RDTSC (0F 31)** returns EDX:EAX, the CPU clocks since the last CPU reset, in the model's CPU clock with the Pentium
  stalls and bus waits included. It starts at 0 and costs 6 clocks at CPL 0 and 11 above.
- **Any CPU reset starts it again from 0** (port 92h bit 0, CF9h, a triple fault); WRMSR to MSR 10h sets it. Real mode
  always allows RDTSC. SeaBIOS calibrates its delays with it.

### 2.4 Model-specific registers and new instructions

RDMSR (0F 32) and WRMSR (0F 30) take the index in ECX and the value in EDX:EAX. Real mode allows them; protected and
virtual-8086 mode need CPL 0 (else #GP(0)). An index outside the table is #GP(0): 03h, 0Ah, 0Fh and 14h and up.

| Index | Register | Behaviour |
|---:|---|---|
| 00h, 01h | Machine check address and type | Stored |
| 02h, 04h–09h, 0Bh–0Eh | Test registers TR1 to TR12 (parity, caches, TLB, BTB, feature control) | Stored, test nothing |
| 10h | Time Stamp Counter | Live |
| 11h | CESR, performance event select | Stored |
| 12h, 13h | Performance counters 0 and 1 | Stored, count nothing |

All are 64 bits and 0 after power on or a CPU reset. MOV to or from TR3–TR7 is #UD on a Pentium (the MSRs replace them).
CMPXCHG8B (0F C7 /1) compares EDX:EAX with an 8-byte operand: equal stores ECX:EBX and sets ZF, else clears ZF and loads
EDX:EAX; a register operand or another /r is #UD. These four opcodes are #UD on the 386 and 486; the exceptions are the 486's.

---

## 3. Pentium Timing

Clocks are counted the same in the interpreter and the dynarec, with no bus model (no prefetch queue, DRAM rows or cache
lines). Each instruction costs the clocks of Intel's Pentium table, with these rules:

- **Pairing:** two instructions in a row issue together when the first is UV or PU and the second UV or PV, and the second
  neither reads nor writes a register the first writes (the flags count; a compare and its Jcc, and two PUSHes or two POPs,
  are exceptions). A prefixed instruction goes alone in the U pipe, instructions over 7 bytes (prefixes not counted)
  don't pair, a branch pairs only as the second. A pair costs its longer half; two paired read-modify-write add 2.
- **Address generation interlock:** 1 clock when an address uses a register the previous instruction or pair wrote.
  **Prefixes:** 1 clock each (LOCK, segment, 66h, 67h, the 0Fh of two-byte opcodes except near Jcc), so 16-bit code in a
  32-bit segment pays it on nearly every instruction.
- **Branches:** static, no branch target buffer. A backward taken or forward not-taken branch is free; any other outcome
  costs 3 clocks (4 for a paired Jcc). **x87:** throughput clocks, FXCH free after an FX instruction, FMUL and FDIV busy
  time by precision, no latency. **Memory:** one fitted average per operand stands for misses and DRAM, misaligned accesses
  cost nothing extra, and devices keep a flat wait per access.

---

## 4. PCI Configuration

### 4.1 Mechanism #1

| Port | Name | Behaviour |
|---|---|---|
| CF8h | CONFADD | 32-bit access only (byte and word accesses are not decoded). Bit 31 enable, 23–16 bus, 15–11 device, 10–8 function, 7–2 register. Only bits 80FFFFFCh are kept |
| CFCh–CFFh | CONFDATA | Any width. A byte at CFC+n is register byte (CONFADD register + n); a dword is four byte accesses in order |

- **A cycle goes nowhere** (reads FFh a byte, writes dropped) when bit 31 is clear, the bus isn't 0, the device is above 20
  or empty, or the function isn't one the device has.
- **Writes are byte by byte and masked per register.** Read-only and reserved bits drop writes; unlisted registers read 00h.
  Mechanism #2 isn't implemented.

### 4.2 Bus 0

Device numbers are the board's IDSEL wiring: host bridge 0, PIIX 7, slots 8 to 11.

| Device | Function | Dword at 00h | Class | What |
|---:|---:|---|---|---|
| 0 | 0 | 122D8086h | 060000h | 82437FX host bridge |
| 7 | 0 | 122E8086h | 060100h | PIIX ISA bridge, header type 80h (multifunction) |
| 7 | 1 | 12308086h | 010180h | PIIX IDE |
| 8 | 0 | 00B81013h | 030000h | Cirrus GD5446, only with that card |
| 9–11 | — | FFFFFFFFh | — | Empty slots |

---

## 5. The 82437FX Host Bridge (device 0)

| Offset | Register | Reset | Notes |
|---|---|---|---|
| 04h, 07h | PCICMD, PCISTS high byte | 06h, 02h | PCICMD bit 1 (memory access) is the only writable bit; PCISTS bits 5, 4 (aborts) clear on a write of 1 |
| 0Dh, 50h, 58h, 68h | MLT, PCON, DRAMT, DRT | 00h | Stored |
| 52h | CC (cache control) | 42h | Stored; the strap reads as a 256 KB L2. No cache is modelled |
| 57h | DRAMC | 01h | Bits 7–6 hole enable, below. Others stored |
| 59h–5Fh | PAM0–PAM6 | 00h | A view of the upper memory map, §5.1 |
| 60h–64h | DRB0–DRB4 | RAM size | All five read the RAM in 4 MB units, up to 20h (128 MB). Bits 5–0 writable, change nothing |
| 72h | SMRAM | 02h | Stored. SMM isn't emulated; A0000h–BFFFFh is never remapped |

- **The hole (DRAMC bits 7–6 = 10b)** opens 15–16 MB to the bus. It resets set only with an ISA Cirrus card and 16 MB or more,
  for that card's aperture at 15 MB; with the GD5446 DRAMC reads 01h and all RAM is usable. Software can change it, and the
  aperture moves over or under RAM. 01b (512–640 KB) is stored and does nothing.

### 5.1 Shadowing and PAM

- **SeaBIOS locks the upper memory with the shadow register at 404h–407h** (it reads "GDOX"), as on the AT boards, not
  through PAM. After POST the F segment and the option ROMs are read-only RAM and the free space between them is open
  (reads FFh, drops writes).
- **PAM is a view of that map,** kept in 4 KB pages. A PAM read gives each 16 KB segment's state from its first page: bit 0
  read, bit 1 write (0 open, 1 read-only, 2 write-only, 3 read/write), bit 2 cacheable (kept as written). After POST PAM0
  reads 10h, segments holding ROMs 11h, free ones 00h.
- **A PAM write that changes a nibble sets all four pages of that segment,** so a UMB manager that makes a free segment RAM
  with a nibble of 3 works (UMBPCI does this on 430FX boards). The last write wins, from PAM or from 404h. PAM0 holds
  F0000h–FFFFFh in its upper nibble only; PAM1–PAM4 hold C0000h–DFFFFh, PAM5 and PAM6 E0000h–EFFFFh. A write-only page reads
  its RAM, and a disabled F segment stays readable.

---

## 6. The PIIX (device 7)

### 6.1 Function 0: ISA bridge

| Offset | Register | Reset | Notes |
|---|---|---|---|
| 04h, 07h | PCICMD, PCISTS high byte | 07h, 02h | PCICMD bit 3 is the only writable bit; PCISTS bits 5–3 clear on a write of 1 |
| 4Ch, 4Eh | IORT, XBCS | 4Dh, 03h | Stored |
| 60h–63h | PIRQRC A–D | 80h | Routing enable (bit 7) and IRQ (bits 3–0), stored |
| 69h, 6Ah, 70h–71h, 76h–79h | TOM, MSTAT, MBIRQ, MBDMA, PCSC | 02h, 04h, 80h, 0Ch, 0002h | Stored; memory and DMA aren't affected |
| A0h–AEh | SMICNTL, SMIEN, SEE, FTMR, SMIREQ, timers | 08h, 0Fh at A8h | Stored; SMIREQ clears bits written 0. No SMI is raised |

No PCI device raises an interrupt, so the PIRQ routing only has to read back and the PICs stay edge triggered.

| Port | Register | Behaviour |
|---|---|---|
| 4D0h, 4D1h | ELCR1, ELCR2 | Bits 7–3 and bits 7, 6, 4–1 stored. Changes nothing: no IRQ is level triggered |
| CF9h | Reset control | A rising bit 2 resets the CPU as port 92h bit 0 does. Bit 1 is stored and read back, bit 2 reads 0 |

A CF9h reset restarts only the CPU (§2.3). RAM, A20, the devices and every PCI register keep their values, even with bit 1
(hard reset) set. `get_system_status` reports `board.last_reset_cause` as `port_92`. Bit 2 stays stored, so a second reset
needs a write of 00h first.

### 6.2 Function 1: IDE

| Offset | Register | Reset | Notes |
|---|---|---|---|
| 04h, 06h–07h | PCICMD, PCISTS | 00h, 0280h | PCICMD bits 0 (I/O) and 2 writable; PCISTS bits 13–11 clear on a write of 1 |
| 09h | Class interface | 80h | Legacy-mode channels, which SeaBIOS needs |
| 20h | BMIBA | 0 | Not writable: bus master IDE isn't emulated, DOS drivers stay on PIO |
| 40h–41h, 42h–43h | IDETIM primary, secondary | 0000h | Stored except bit 14. Bit 15 decodes the channel; timing bits do nothing |

- **A channel answers only when PCICMD bit 0 and its IDETIM bit 15 are set.** SeaBIOS sets both; before that the ports read
  as open bus (always decoded on the AT boards). The drives, IRQ14 and IRQ15 are those of `pc_ide_cdrom`; SeaBIOS finds
  them through this function, not by probing the ISA ports.

---

## 7. The PCI GD5446

1013:00B8h at device 8, class 030000h, CR27 reads B8h, 2 MB of video memory, interrupt pin 0.

| Offset | Register | Behaviour |
|---|---|---|
| 04h | PCICMD | Bits 0 (I/O), 1 (memory) and 5 (palette snoop) writable. The VGA ports and A0000h window answer regardless |
| 10h | BAR0 | Bits 31–25 writable: 32 MB, memory, not prefetchable. Written FFFFFFFFh it reads FE000000h |
| 14h, 18h, 30h | MMIO and BitBLT window, GPIO, expansion ROM | Read 0, unused. 3Ch (interrupt line) is read and write |

- **The linear aperture** maps the 2 MB at BAR0 when PCICMD bit 1 and any of SR07 bits 7–4 are set. The debugger shows it as
  "Linear VRAM"; it moves when BAR0 is written. SeaBIOS put BAR0 at FC000000h in the tested setups, so read it. There is no
  15 MB aperture and no memory hole with this card.
- **Beyond the GD5428 (`pc_video`):** video memory reads cost 16 bus clocks and writes 5 (the VL-Bus card's figures). SR0F
  reads 18h (2 MB), SR17 00h. VCLK0–3 (SR0B–SR0E, SR1B–SR1E) reset to 66h/3Bh,
  5Bh/2Fh, 45h/30h and 7Eh/33h. CR17 bit 2 counts the vertical CRTC registers in line pairs. The rest is the 5428's: unlock,
  banking, hidden DAC, formats. BitBLT, the hardware cursor and the MMIO window stay register-only.
- **VBE:** the VGA BIOS is the Cirrus PCI build of SeaVGABIOS ("SeaVGABIOS CL-GD5446"), served as this card's PCI option ROM
  so SeaBIOS runs it with the card's bus, device and function, and VBE takes PhysBasePtr from BAR0. It lists the 5428's
  modes plus 1024x768 at 16 bpp and 1280x1024 at 8 bpp (107h, 59.94 Hz, a 107.9 MHz clock). 1024x768 at 24 bpp, 1280x1024
  at 16 bpp and 24 bpp above 640x480 are not offered. A return to a VGA mode restores VCLK0 and VCLK1.

---

## 8. SeaBIOS on the PCI Board

- **A second system BIOS image,** "SeaBIOS 430FX" (128 KB), with the PCI BIOS (INT 1Ah function B1h) and a PCI IRQ routing
  table ($PIR). Small patches make it recognise the 430FX (PCI memory is placed as for an i440FX), the PIIX functions and
  this board's $PIR. The 386 and 486 models keep the AT image and the ISA VGA BIOS.
- **POST scans bus 0,** assigns the BARs, picks the primary VGA and starts the IDE function. `get_bios_log` shows the devices,
  the BARs ("BAR0 mapped at FC000000h, size 32 MB"), "Using 00:08.0 for primary VGA", the PIIX init, both ATA channels at
  00:07.1 and the $PIR copy.
- **$PIR:** the router is the PIIX (00:07.0). Devices 8–11 are slots 1–4, and device d's INTA# goes to PIRQ (d − 1) mod 4,
  so the GD5446's is PIRQD, though it never fires.

---

## 9. Driving it from MCP

| Tool | What it shows here |
|---|---|
| `get_system_status` | `machine`: `model`, `key`, `cpu` (`Pentium`), `cpu_clock_hz`, `ram_kb`, `fpu` (`Integrated`), `video` (`card`, `id` `gd5446`, `vram_kb` 2048), `settings`, `warnings` (the video fallbacks). `board`: `cpu_resets`, `last_reset_cause`, `post_code`. `bios`: `system_bios` ("SeaBIOS 430FX ..."), `vga_bios` |
| `get_i386_status`, `write_i386_register` | On a Pentium: `cr4` (hex), `tsc` (16 hex digits, high dword first), `msrs` (an object keyed by index, `"10"`, with 16-digit values for the implemented ones); `test_registers` is null. `CR4` can be written, masked to its writable bits |
| `get_video_output_status`, `get_vga_registers` | The `cirrus` objects: `unlocked`, `extended_mode`, `format`, `aperture_base` (BAR0 once PCICMD and SR07 enable it, else null), `bank_0`, `bank_1`, `pitch_bytes`, `vclk_hz`; `sequencer` (SR00–SR1F), `graphics`, `crtc`, `hidden_dac` |
| `get_bios_log`, `get_frame_buffer` | The POST log of §8 (`text`; `max_bytes` takes the tail). Buffer `linear` decodes the 2 MB with `format`, `pitch`, `start` |
| `list_memory_areas`, `read_memory` | "I/O PORTS" (side-effect free), "Linear VRAM" at BAR0, "Shadow ROMs", "Shadow BIOS", "Upper Memory" |

No tool decodes PCI configuration space (the System Control window of the desktop does). Drive the ports instead:

1. `write_io_port` `port` `CF8`, `value` `80004010`, `size` 32: an OUT as the CPU does it, selecting bus 0, device 8,
   function 0, register 10h (80000000h + device × 800h + function × 100h + register).
2. `read_memory` `area` `I/O PORTS`, `offset` `CFC`, `size` 4: BAR0 as bytes, lowest first. The area shows CFCh–CFFh, CF9h,
   4D0h and 4D1h, but not CF8h itself.
3. To write a register, `write_io_port` to `CFC` (`size` 8, 16 or 32) after the CF8h write. To size a BAR, write FFFFFFFFh,
   read it back and restore the old value. `write_io_port` also reaches CF9h: 04h resets the CPU.

**Trace filters** (`set_trace_log`):

- `io.reads`, `io.writes`: every port access with size and value, so 32-bit CF8h and CFCh accesses show whole. A
  `set_breakpoint` with `space` `io` on CF8h–CFFh, 4D0h, 4D1h or CF9h stops on them.
- `system.reset`: CPU resets (CF9h, port 92h, triple fault) with the cause. `system.control`: system control port writes,
  including the shadow lock at 404h–407h. `system.post`: POST codes.
- `cpu.exceptions`: the #GP of a bad MSR index, of reserved CR4 bits and of RDTSC under TSD, and the #UD of MOV TRn and of
  a Pentium opcode on a 386 or 486.
- `video.crtc`, `video.output`: the VGA and Cirrus register writes, SR07 included.
