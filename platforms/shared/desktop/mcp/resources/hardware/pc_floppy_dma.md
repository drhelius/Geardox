# Floppy Controller and DMA — What MS-DOS Software Relies On

## 1. Overview

Every model has two floppy drives (A: is drive 0, B: is drive 1) on an Intel 82077 in AT mode, and two 8237A DMA
controllers. The floppy uses IRQ 6 (vector 0Eh) and DMA channel 2.

- **The BIOS way:** the AT BIOS's INT 13h resets the chip, reads the four poll answers, sends Specify, programs channel
  2 with mode 46h (read), 4Ah (write) or 42h (verify) and waits for IRQ 6.
- **Drive types are fixed at every reset:** CMOS byte 10h holds drive A in its high nibble and B in its low one (1 = 360 KB,
  2 = 1.2 MB, 3 = 720 KB, 4 = 1.44 MB, 5 = 2.88 MB). A game descriptor's `[machine] floppy_a_type` and `floppy_b_type`
  take `360k`, `1200k`, `720k`, `1440k` or `2880k`. On auto the type follows the inserted disk: 360 KB for 40 tracks,
  1.2 MB for 360 rpm high density, 2.88 MB for extra density or over 18 sectors a track, else 1.44 MB.

---

## 2. Ports and Registers

The chip decodes 10 address lines; 3F6h belongs to the IDE channel.

| Port | Read | Write |
|---|---|---|
| 3F0h, 3F1h, 3F3h | FFh (no SRA, SRB or tape register) | Dropped |
| 3F2h | DOR, read back (the real AT register is write-only) | DOR |
| 3F4h | MSR | DSR |
| 3F5h | Result byte, or data in non-DMA mode | Command byte, parameter or data |
| 3F7h | DIR | CCR |

| Register | Bits |
|---|---|
| DOR | 7–4 motor enable for drives 3–0 (only 0 and 1 exist); 3 DMA gate (clear: IRQ 6 and DRQ 2 stay off, the interrupt is held until it opens); 2 reset (0 holds the chip in reset); 1–0 drive select |
| MSR | 7 RQM, 6 DIO (1 = host reads), 5 NDM (non-DMA execution), 4 CB (command busy), 3–0 drive busy (from the start of a seek to that drive's Sense Interrupt Status) |
| DSR, CCR | Bits 1–0 data rate: 00 is 500 kbps, 01 300, 10 250, 11 1 Mbps (FM at half). The latest write of either counts |
| DIR | Bit 7 disk change line of the selected drive; bits 6–0 read 1 |

- **A drive is selected only while its own motor bit is on.** With the motor off no drive answers: DIR bit 7 reads 0,
  Sense Drive Status sees no Track 0 or write protect, steps move nothing, and a read or format waits for index pulses
  until the motor comes on, a disk goes in or the chip is reset. The AT BIOS times out with error 80h.
- **Resets:** DOR bit 2 or DSR bit 7 (self-clearing) keep the DOR, the data rate, Specify's values and, with Lock,
  Configure's. 0.25 ms after the chip leaves reset it raises one interrupt and queues four Sense Interrupt Status
  answers: ST0 C0h–C3h with each drive's PCN. A Configure that disables polling in that time cancels them. Power on
  sets the rate to 250 kbps and the DOR to 00h (chip in reset, gate closed).
- **Disk change:** every drive reads "changed" after a reset, insert, eject or swap. A step pulse on the selected drive
  clears it, but only with a disk inside, so an empty drive stays changed.

---

## 3. Drives and Media

| Type | RPM | Rates (kbps) | Head travel |
|---|---|---|---|
| 360 KB 5.25" | 300 | 250 | Cylinders 0–41, 48 TPI |
| 1.2 MB 5.25" | 360 | 300, 500 | 0–82, 96 TPI |
| 720 KB 3.5" | 300 | 250 | 0–82 |
| 1.44 MB 3.5" | 300 | 250, 500 | 0–82 |
| 2.88 MB 3.5" | 300 | 250, 500, 1000 | 0–82 |

- **A track is readable only at its own rate:** the media's rate (250 for 2D and 2DD, 500 for 2HD, 1000 for ED) scaled
  by the drive's rpm over the disk's must equal the chip's rate, and the drive must support it. A 360 KB disk in a
  1.2 MB drive reads at 300 kbps; a 1.2 MB disk in a 1.44 MB drive cannot be read. A wrong rate looks like a missing
  address mark.
- **Track density:** a 40-track disk in an 80-track drive is seen on even cylinders only (the BIOS double steps); an
  80-track disk in a 40-track drive shows every second track.
- **The head stops at the end of its travel.** `head_cylinder` is where it is; the chip's PCN follows the step pulses
  it sent, so they differ after stepping past the end.
- **Not modelled:** motor spin-up and motor-off timers (index pulses run whenever the selected motor is on and a disk is
  in), step settle, drive faults and not-ready. Write protect (image flag or host setting) shows as ST3 bit 6.

---

## 4. Commands

A command is the bytes written to 3F5h while RQM=1 and DIO=0. It starts 12 µs after the last one.

| Opcode | Command | Bytes in | Result bytes |
|---|---|---|---|
| 06h, 0Ch | Read Data, Read Deleted Data (MT, MFM, SK bits) | 9 | 7 |
| 05h, 09h | Write Data, Write Deleted Data (MT, MFM bits) | 9 | 7 |
| 02h, 42h | Read Track | 9 | 7 |
| 16h | Verify (byte 1 bit 7 is EC: DTL counts sectors) | 9 | 7 |
| 0Ah, 4Ah | Read ID | 2 | 7 |
| 0Dh, 4Dh | Format Track | 6 | 7 |
| 07h, 0Fh | Recalibrate, Seek | 2, 3 | None, interrupt |
| 8Fh, CFh | Relative Seek (CFh steps in) | 3 | None, interrupt |
| 08h | Sense Interrupt Status | 1 | 2: ST0, PCN |
| 04h | Sense Drive Status | 2 | 1: ST3 = 28h + WP 40h + Track 0 10h + head, drive |
| 03h, 13h | Specify, Configure | 3, 4 | None |
| 10h, 0Eh, 12h | Version, Dumpreg, Perpendicular Mode | 1, 1, 2 | 1 (90h), 10, None |
| 14h, 94h | Lock (94h locks) | 1 | 1: 10h locked, 00h unlocked |

- **Anything else is invalid:** one result byte, 80h, no interrupt. That includes the Scan commands and Powerdown.
- **Specify:** SRT in the high nibble of byte 1, HUT in the low one; HLT in bits 7–1 of byte 2 and ND (non-DMA) in bit 0.
  The values survive resets.
- **Configure** byte 2: bit 6 EIS (implied seek), 5 EFIFO (1 disables the FIFO), 4 POLL (1 disables polling), 3–0
  FIFOTHR. After a reset the FIFO is off (one byte per request) unless Lock was set. With EIS=0 the chip never seeks
  for a read or write; at the wrong cylinder it finds no sector.
- **Seek and Recalibrate** step at the Specify rate and raise IRQ 6 at the end; the chip takes other commands meanwhile.
  A Seek to the current cylinder ends at once. Recalibrate steps out until Track 0 shows, at most 79 steps. If it never
  does, ST0 is 70h plus the drive (SE, abnormal, equipment check), which is what happens with no drive selected.
- **Sense Interrupt Status** pops one of up to 8 queued answers: ST0 20h plus the drive for a finished seek, C0h–C3h for
  the reset poll. With none queued it returns the single byte 80h.

---

## 5. Transfers, Interrupts and Status

- **IRQ 6** rises at the start of the result phase of a data command, Read ID or Format, and at the end of a Seek or
  Recalibrate. The first result byte of a data command takes it down; Sense Interrupt Status takes down the others and
  raises it again while more seek answers wait. The other commands raise nothing.
- **DMA mode** (Specify ND=0): the chip raises DRQ 2 per byte, or per FIFO threshold, with the DOR gate open. The 8237
  moves the data on channel 2 and its terminal count ends the command: the chip finishes the sector it is in and goes
  to the result with IC=00. If the transfer reaches EOT with no terminal count, IC=01 and ST1 EN. With ND=1 each byte
  is an interrupt and RQM instead (MSR bit 5) and the program moves it through 3F5h.
- **Result phase:** ST0, ST1, ST2, C, H, R, N. After a normal end C, H and R point at the next sector (R+1; at EOT C+1
  and R=1, or with MT the head flips first). Format returns C=H=R=0.
- **FIFO:** 16 bytes, threshold FIFOTHR+1. A read asks for service at 16 minus the threshold bytes, a write when that
  much room is free. A byte with no room, or none to write, is an overrun: ST1 OR, the command ends.
- **Read Track** starts at the index pulse and reads the sectors in disk order. **Verify** transfers nothing, so a
  terminal count cannot end it. **Format Track** takes 4 bytes per sector (C, H, R, N) from DMA and writes the track at
  the next index pulse, every sector full of the filler byte. The media type is re-evaluated: 36 sectors make 2.88 MB.
- **Search:** the chip matches C, H, R and N in its own density; sectors of the other density, or with no ID in the
  image, are invisible. With no data mark the result is MA and MD.

| Status | Bits |
|---|---|
| ST0 | 7–6 IC: 00 normal, 01 abnormal, 10 invalid command, 11 reset poll. 5 SE seek end. 4 EC equipment check. 2 head. 1–0 drive |
| ST1 80h EN | EOT reached without a terminal count |
| ST1 20h DE | CRC error in the ID or the data (the sector is marked so in the image) |
| ST1 10h OR | Overrun or underrun. Check channel 2: masked, wrong mode or count, or the FIFO threshold |
| ST1 04h ND | Sector not found after two index pulses |
| ST1 02h NW | Write or Format on a write-protected disk |
| ST1 01h MA | No address mark: wrong rate, wrong MFM bit or an unformatted track; with ST2 01h MD, no data mark |
| ST2 40h CM | A deleted mark where a data mark was wanted or the reverse. Without SK the command ends after that sector |
| ST2 20h DD | CRC error in the data field |
| ST2 10h WC, 02h BC | With ND, the IDs on the track have another cylinder (BC too for FFh): the head is not where the program thinks |

---

## 6. Timing

| Item | Model |
|---|---|
| Step | (16 − SRT) ms at 500 kbps, in proportion to 500 / rate: SRT 8 is 8 ms at 500 kbps and 16 ms at 250 |
| Head load | HLT units of 2 ms at 500 kbps (1 ms at 1 Mbps, 4 ms at 250); 0 is 128 units. Paid only if the head unloaded |
| Head unload | HUT units of 16 ms at 500 kbps (8 at 1 Mbps, 32 at 250); 0 is 16 units, counted from the end of a command |
| Rotation | 200 ms a turn at 300 rpm, 166.7 ms at 360. A sector is found when its ID passes under the head; a failed search takes two turns |
| Data | A byte every 8 bit times: 16 µs at 500 kbps in MFM, 32 µs in FM. This is the DMA or FIFO deadline |

Drive timing is a machine setting, Real or Fast; a game opened from a descriptor defaults to Fast, anything else to
Real. Fast divides the step, head load and rotation waits by 16; byte times and head unload keep their value.

---

## 7. Disk Images

- **Formats:** raw dumps (`.img`, `.ima`, `.vfd`, `.flp`, `.dsk`, `.hdm`, `.xdf`, `.bin` when raw), D77 and D88, `.mfm`
  flux images, a `.zip` holding them and an `.m3u` playlist of disks. All become one internal track list: each sector
  keeps its C, H, R, N, density, deleted mark and a status (ok, ID CRC, data CRC, no ID, no data), so copy-protected
  tracks survive. Up to 82 cylinders and 64 sectors a track.
- **Raw geometry** comes from the size (cylinders×heads×sectors): 160 KB 40×1×8, 180 KB 40×1×9, 320 KB 40×2×8,
  360 KB 40×2×9, 720 KB 80×2×9, 1.2 MB 80×2×15, 1.44 MB 80×2×18, 1.68 MB 80×2×21 (DMF), 2.88 MB 80×2×36; 512-byte
  sectors numbered from 1. A known size plus E5h padding takes that size. Otherwise the boot sector decides (media byte
  F0h or more, 8–36 sectors, 1 or 2 heads, at most 82 cylinders).
- **Writes never touch the image file.** They go to a working copy, `name.CRC32.geardox.d77` in the save files folder,
  flushed after a few idle seconds, on eject and on exit. Without floppy persistence the disk is write protected.
  `modified` in `list_floppy_drives` means changes not yet flushed.

---

## 8. The 8237A Controllers

Chip 1 serves channels 0–3 (8-bit), chip 2 channels 4–7 (16-bit, counting words). Channel 4 cascades chip 1 into chip 2.
The ports decode all 16 address lines; 10h–1Fh are not used.

| Chip 1 / chip 2 | Register |
|---|---|
| 00h–07h / C0h–CEh | Current address (even) and count (odd) of each channel, through the flip-flop. Odd ports from C1h mirror the even one below |
| 08h / D0h | Write command, read status |
| 09h / D2h | Write request: bit 2 set or clear, bits 1–0 channel |
| 0Ah / D4h | Write single mask: bit 2 set or clear, bits 1–0 channel |
| 0Bh / D6h | Write mode |
| 0Ch / D8h | Write clears the flip-flop |
| 0Dh / DAh | Write is Master Clear; read gives 00h |
| 0Eh, 0Fh / DCh, DEh | Write clears all mask bits; write all four mask bits |

- **The flip-flop** is shared by all address and count registers of a chip: first access low byte, second high byte.
  Write 0Ch (or D8h) first. Master Clear and reset also clear it and set the mask to Fh.
- **Writes** set the base and current registers together. Reads return the current value; the base can't be read, and
  reads of 09h–0Ch, 0Eh and 0Fh give FFh. Status: bits 3–0 terminal count (cleared by the read), 7–4 requesting.
- **Mode byte:** bits 1–0 channel, 3–2 transfer (00 verify, 01 I/O to memory, 10 memory to I/O, 11 illegal), 4 auto-init,
  5 decrement, 7–6 service (00 demand, 01 single, 10 block, 11 cascade). A floppy read is 46h, a write 4Ah; auto-init
  sound playback is single plus auto-init plus memory to I/O: 58h plus the channel number within its chip.
- **Service:** single mode does one unit per request and gives the bus back; block mode keeps it to terminal count,
  demand mode while DREQ stays up. Priority is fixed, channel 0 first; chip 1 sits at channel 4 of chip 2.
- **Terminal count:** the count holds units minus one and ends on the step from 0 to FFFFh. The channel sets its status
  bit and pulses T/C to the device. Auto-init then reloads current from base and leaves the mask alone; otherwise the
  channel masks itself, with the count at FFFFh.
- **Command register:** only bit 2 (disable) acts. Memory-to-memory, rotating priority and the rest are stored and
  ignored; the first use is logged.
- **Channel 4 has to stay unmasked and in cascade mode** (POST sets mode C0h) or nothing on channels 0–3 gets the bus.
  Master Clear on chip 2 masks it again. A disabled chip also stops its channels.
- **Software requests** (09h) run even through a mask bit and clear at terminal count. A channel whose device has
  nothing to give is marked stalled until DREQ rises again.
- **Timing:** a unit takes about 1.2 µs (5 clocks of 240 ns), 0.94 µs from the second of a block or demand run, plus
  about 0.5 µs to hand the bus back; the CPU is stalled meanwhile.

---

## 9. Channels, Pages and Boundaries

| Channel | Width | Page port | Used by |
|---|---|---|---|
| 0 | 8 | 87h | Free (the SB16 mixer can move the 8-bit channel here) |
| 1 | 8 | 83h | Sound Blaster 8-bit, the default (setting 1 or 3) |
| 2 | 8 | 81h | Floppy |
| 3 | 8 | 82h | Sound Blaster 8-bit, the other setting |
| 4 | — | 8Fh | Cascade of chip 1 (8Fh is the AT's refresh page) |
| 5 | 16 | 8Bh | Sound Blaster 16 16-bit, the default |
| 6, 7 | 16 | 89h, 8Ah | Sound Blaster 16 16-bit, if moved with mixer register 81h |

- **Page registers** (81h–8Fh, read and write) give address bits 23–16; reset clears them. A floppy driver also needs
  the address, the count minus one and channel 2 unmasked (0Ah = 02h), or the chip overruns (ST1 OR).
- **Addresses:** channels 0–3 use page × 10000h + the chip's 16-bit address, in 64 KB blocks. Channels 5–7 use
  (page AND FEh) × 10000h + 2 × the chip's address, count words, 128 KB blocks and never odd addresses.
- **Nothing carries into the page register.** The address wraps inside its 64 KB (or 128 KB) block, so a buffer that
  crosses a boundary wraps its tail onto the start of the same block. The AT BIOS refuses such a transfer with INT 13h
  error 09h.
- **Sound Blaster:** the 8-bit channel carries 8-bit audio and, with no 16-bit channel, 16-bit audio as bytes. Playback
  reads memory a little ahead of the requests; the channel counts the units as they come due. The `device` names in
  `get_dma_status` are fixed labels: channel 1 reads SOUND, 3 SPARE and 5 SOUND 16 whichever channel the card uses.

---

## 10. Driving it from MCP

- `get_fdc_status`: `dor`, `msr`, `dir`, `selected_drive`, `rate` ("500 KBPS"), `phase` (RESET, COMMAND, EXECUTION,
  RESULT), `command` (name), `results` (hex bytes), `pcn[4]`, `sense_queue` (`st0`, `pcn`), `st0`–`st2` of the running
  command, `interrupt` and `dma_request` (the pins, after the DOR gate), `non_dma`, `srt`, `hut`, `hlt`, `implied_seek`,
  the FIFO and polling settings, `lock` and the command's `cylinder`, `head`, `sector`.
- `list_floppy_drives`: per drive `present`, `drive_type`, `inserted`, `head_cylinder`, `disk_changed`, `disk_name`,
  `image`, `media` (2D, 2DD, 2HD, ED), `rpm`, `write_protected`, `modified` and `geometry` (`mixed` for uneven tracks).
- `read_floppy_sector` (`drive`, `cylinder` 0–81, `head`, optional `sector` R) lists the track in disk order without
  `sector` (`c`, `h`, `r`, `n`, `size`, `density`, `deleted`, `status`), or returns that sector with its hex `data`. It
  reads the image: the head, the chip and the change line are untouched.
- `insert_floppy` (`drive` 0–1, `file_path`, optional `write_protected`), `create_blank_floppy` (`type` 360kb, 720kb,
  1200kb, 1440kb or 2880kb; `formatted`; a new `.d77`, `.img` or `.ima` path, never an existing file), `eject_floppy`,
  `swap_floppies` and `set_floppy_write_protect` change disks without a reset. Insert, eject and swap raise the change
  line; write protect shows as ST3 bit 6 and, on a write, ST1 NW. `load_media` opens a game descriptor or a hard disk
  image and powers on; floppies go through `insert_floppy`.
- `get_dma_status`: `controllers` (`chip`, `command`, `status`, `mask`, `software_requests`, `request_levels`,
  `terminal_count`, `flip_flop`), `active_channel`, `stalled` and `channels` (`channel`, `device`, `mode`, `direction`,
  `unit`, `service`, `auto_initialize`, `decrement`, `page`, `physical_address`, current and base address and count,
  `masked`, `request`, `terminal_count`). `physical_address` is the real bus address with the page: compare it with the
  buffer the game thinks it uses.
- `write_io_port` (`port` hex, `value` hex, `size` 8, 16 or 32) is an OUT as the CPU does it: the device reacts and the
  `io.writes` trace sees it. Use it to poke the DOR, a DMA mask or a page register; a stray byte to 3F5h upsets the
  running driver as it would a program.
- Trace `fdc.commands` (opcode, drive, C, H, R, N or the Seek target), `fdc.results` (ST0–ST2 and C, H, R), `fdc.drives`
  (DOR writes), `dma.registers` (every chip or page register write, with the resulting address and count),
  `dma.requests` (each DREQ rise, with address, count, mode and MASKED) and `dma.ends` (terminal count). `io.reads` and
  `io.writes` show the raw 3F2h–3F7h traffic, `pic.requests` and `cpu.irqs` the IRQ 6.
