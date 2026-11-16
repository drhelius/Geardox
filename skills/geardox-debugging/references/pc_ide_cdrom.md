# IDE and the ATAPI CD-ROM Drive — What MS-DOS Software Relies On

## 1. Overview

The machine has two IDE channels with two positions each. Hard disks and one ATAPI CD-ROM drive share them. Read the
channels with `get_ide_status` and `list_hard_disks`, the drive with `get_cdrom_status`, and put discs in and out with
`load_media`, `eject_cdrom`, `close_cdrom_tray` and `insert_playlist_disc`.

| Channel | Registers | Control | IRQ (vector) | Positions |
|---|---|---|---|---|
| Primary | 1F0h–1F7h | 3F6h | 14 (76h) | Master (id 0), Slave (id 1) |
| Secondary | 170h–177h | 376h | 15 (77h) | Master (id 2), Slave (id 3) |

- **The CD-ROM drive is a machine setting:** None, Primary Master, Primary Slave, Secondary Master (default) or
  Secondary Slave, and a speed of 1x, 2x, 4x (default) or 8x. Both apply at reset. On the command line, `--cdrom
  none|pm|ps|sm|ss`. Libretro has `geardox_cdrom_drive` and `geardox_cdrom_speed`.
- **The position it takes is closed to hard disks.** The Hard Disks menu shows it as taken and attaching a disk there
  fails. `list_hard_disks` ids 0-3 are the same positions.
- **An empty position reads 00h** from Status, which is what SeaBIOS needs to skip it. A channel with no device does
  not decode its ports.
- **Boot:** SeaBIOS lists the drive in its Esc menu as "DVD/CD [ata1-0: GEARDOX CD-ROM ATAPI-0 DVD/CD]". `debug_reset`
  with `boot: cdrom` boots from it, and fails when no drive is fitted. A boot disc is an El Torito one: boot record at
  LBA 11h, catalog, validation entry (01h, 55h, AAh) and an initial entry of 88h.

---

## 2. Registers and Signature

An ATAPI device reads the ATA task file differently:

| Offset | Read | Write |
|---|---|---|
| 1 | Error: sense key in bits 7-4, ABRT bit 2, EOM bit 1, ILI bit 0 | Features: bit 0 DMA, bit 1 overlap. Both must be 0 |
| 2 | Interrupt Reason: CoD bit 0, IO bit 1 | Unused |
| 3 | Sector Number, reads back | Reserved |
| 4, 5 | Byte Count | Byte Count limit |
| 6 | Device, DRV in bit 4 | Device |
| 7 | Status: BSY, DRDY, DSC, DRQ, CHK | Command |

- **The signature** follows power-on, hardware reset, SRST, DEVICE RESET (08h) and an aborted ECh or 20h: Sector Count 01h,
  Sector Number 01h, Cylinder 14h EBh, Device 00h, Error 01h, Status 00h. DRDY stays 0 until a PACKET or IDENTIFY PACKET
  DEVICE command.
- **SRST doesn't really reset an ATAPI device:** it keeps its modes and a running play, aborts only a command waiting
  at DRQ, and puts the signature back. It takes about 1 ms.
- **ATA commands:** 08h DEVICE RESET, 90h EXECUTE DEVICE DIAGNOSTIC, A0h PACKET, A1h IDENTIFY PACKET DEVICE, E0h, E1h,
  E5h and E6h (accepted with no effect, E5h returns FFh), EFh SET FEATURES (03h, 66h and CCh only). ECh and 20h abort
  after writing the signature. Anything else aborts, and stops a play.
- **IDENTIFY PACKET DEVICE:** word 0 is **85C0h** (ATAPI, CD-ROM, removable, accelerated DRQ, 12-byte packets). Model
  "GEARDOX CD-ROM", firmware "1.00", word 49 = 0200h (LBA, no DMA), word 51 = 0200h (PIO mode 2), word 53 = 0, so
  drivers run it in PIO. DOS CD drivers such as UDVD2 identify the drive from word 0. The Byte Count registers read
  0200h for its data block, which some drivers use to size the transfer.

---

## 3. The Packet Protocol

1. **A0h** sets BSY at once. Features with DMA or overlap set abort. An odd limit counts as one less, and a limit of 0
   means the largest, FFFEh, which a 1994 Lite-On driver relies on.
2. **Packet DRQ:** 10 µs later BSY drops and DRQ rises with CoD=1, IO=0 and no interrupt.
3. **The sixth word** sets BSY at once and runs the command. SeaBIOS reads Alternate Status once, straight after it.
4. **Data in:** each block sets Byte Count, CoD=0, IO=1, DRQ and an interrupt. A block is as many whole sectors as fit
   the buffer and the limit, at least one. Reading a block's last word sets BSY at once, then comes the next block or the
   status phase.
5. **Data out** is MODE SELECT only, one block of the parameter list length with CoD=0 and IO=0.
6. **Status:** BSY and DRQ drop, CoD=1, IO=1, DRDY and DSC set, CHK on an error with its sense key in Error, and an
   interrupt.
- INTRQ is cleared by a Status read, a Command write or a reset; nIEN holds it back without losing it. SeaBIOS and the
  DOS drivers poll with nIEN set.
- A command written during BSY is ignored. Reading past a block gives FFFFh. A new command while DRQ is up for data
  aborts the old one.
- PLAY and SEEK return status at once. DSC rises when the seek ends, with no interrupt.

---

## 4. Packet Commands

| Op | Command | Behaviour |
|---|---|---|
| 00h | TEST UNIT READY | Ready, or the not-ready sense of section 5 |
| 03h | REQUEST SENSE | 18-byte fixed sense; audio status goes in ASCQ with key NO SENSE |
| 12h | INQUIRY | Type 05h, removable, version byte 21h, 36 bytes: "GEARDOX", "CD-ROM", "1.00" |
| 1Bh | START STOP UNIT | Eject (LoEj=1 Start=0), close (LoEj=1 Start=1), start and stop the motor; Immed accepted |
| 1Eh | PREVENT/ALLOW MEDIUM REMOVAL | Locks software eject only |
| 25h | READ CD-ROM CAPACITY | Last LBA, block size 2048 |
| 28h, A8h | READ(10), READ(12) | 2048-byte Mode 1 reads. An audio sector gives 05/64/00 with ILI |
| 2Bh | SEEK(10) | Moves the head, stops a play |
| 42h | READ SUB-CHANNEL | Formats 01h, 02h, 03h. MCN and ISRC report not valid |
| 43h | READ TOC | Formats 0, 1 and 2, MSF or LBA |
| 44h | READ HEADER | Mode and address of a data sector |
| 45h, 47h, A5h | PLAY AUDIO(10), MSF, (12) | See section 7 |
| 48h | PLAY AUDIO TRACK INDEX | Index 1 only |
| 4Bh | PAUSE/RESUME | See section 7 |
| 4Eh | STOP PLAY/SCAN | See section 7 |
| 55h, 5Ah | MODE SELECT(10), MODE SENSE(10) | Pages 01h, 0Dh, 0Eh, 2Ah. Only page 0Eh's SOTC, channels and volumes change |
| B9h, BEh | READ CD MSF, READ CD | Data and audio sectors with any field the image can give; sub-channel is Q only |
| BBh | SET CD SPEED | Accepted with no effect: the speed is a machine setting |
| BDh | MECHANISM STATUS | No changer, the tray state, the current LBA |
| 4Ah, 49h, A9h, BAh, BCh, A6h and others | | 05/20/00 invalid command |

- **Page 2Ah** reports audio play, CD-DA commands, an accurate stream, lock, eject, a tray, separate volume and mute, 256
  volume levels, a 256 KB buffer and speeds in kB/s (176 per 1x). No Mode 2, multisession, R-W, UPC or ISRC.
- **Page 0Dh** gives 60 seconds a minute and 75 frames a second.
- **Page 0Eh** starts with Immed 1, SOTC 0, port 0 on the left channel and port 1 on the right, both at FFh.
- A READ of an audio sector returns 05/64/00; READ CD of one returns 2352 bytes, 588 little-endian left and right pairs.

---

## 5. Sense and Unit Attention

The drive keeps one fixed-format sense, replaced by each command that ends and kept by REQUEST SENSE. Key/ASC/ASCQ:

| Condition | Sense |
|---|---|
| Power-on or reset | 06/29/00, once |
| Disc changed | 06/28/00, once, when it is ready |
| No disc, tray open or closed | 02/3A/00 |
| Spinning up | 02/04/01 |
| Unknown opcode | 05/20/00 |
| Bad field in the packet | 05/24/00 |
| LBA past the end | 05/21/00 |
| Data command on audio, or audio command on data | 05/64/00, with ILI on READ |
| Software eject while removal is prevented | 02/53/02 |
| PAUSE or RESUME with nothing to act on | 0B/B9/00 |

**A unit attention is cleared by the command that reports it.** REQUEST SENSE returns it as its sense data, INQUIRY
runs without clearing it, and every other command gets CHECK CONDITION with it first.

---

## 6. Tray and Disc

Three states: tray open, closed with no disc, closed with a disc. MODE SENSE's medium type is 71h, 70h or the disc type
(01h data, 02h audio, 03h mixed). Everything else sees 02/3A/00 for the first two.

- **Insert** (menu, playlist, `load_media`, `insert_playlist_disc`, libretro or `--cd`): the tray closes on the disc, the
  drive spins up with 02/04/01 for 0.9 s, then reports 06/28/00 once.
- **Eject from the host** (menu or `eject_cdrom`) opens the tray at once, stops audio and reports no disc. It works even
  when software has prevented removal, as the emergency eject hole does.
- **Eject from software** (START STOP UNIT with LoEj) obeys the prevent bit and gets 02/53/02.
- **Close** is `close_cdrom_tray`, or START STOP UNIT with LoEj=1 and Start=1. With a disc inside the drive spins up.
- **Power-on with a disc** (`--cd` or a disc already in the drive) is ready at once, with 06/29/00 pending.
- **A disc change is never a reset.** The machine keeps running; the OS sees the unit attention.
- `get_cdrom_status` shows `tray_open`, `disc`, `ready`, `spinning_up`, `motor_on`, `unit_attention`, `prevent_removal`.
  The drive's "ready" is its own: the media can be loaded while the drive is still spinning up.

---

## 7. CD Audio

- **PLAY** starts at once and returns status. A length of 0 plays nothing and is no error. Start equal to end does
  nothing; start past end is 05/24/00. FFFFFFFFh or FFh:FFh:FFh starts from the current position. The start must be in an
  audio track, or 05/64/00.
- **SOTC 0** plays across tracks. **SOTC 1** stops at the next track. Running into a data track or the lead-out ends the
  play at the last audio sector, with status 13h.
- **A new PLAY, READ, READ CD, READ HEADER, SEEK, START STOP or any ATA command stops a play.** TEST UNIT READY, REQUEST
  SENSE, INQUIRY, MODE SENSE, READ SUB-CHANNEL, READ TOC and PREVENT don't.
- **PAUSE** while playing pauses, **RESUME** while paused carries on, **STOP** ends a play or a pause. PAUSE and RESUME with
  nothing to act on give 0B/B9/00. STOP with nothing playing is no error.

| Audio status | When |
|---|---|
| 11h | Playing |
| 12h | Paused |
| 13h | The play ended by itself; reported once, then 15h |
| 14h | The play ended on an error; once, then 15h |
| 15h | Nothing to report: power-on, after STOP, after a play stopped by another command |

- **The Q channel** is made from the TOC (no image stores sub-channels): ADR 1, the track's control bits, track, index
  (0 in a pregap, 1 after), relative and absolute time. The position is what is heard; with no play it is the head.
- **Volume:** page 0Eh's two ports each have a channel select (0 mute, 1 left, 2 right, 3 both mixed) and a volume;
  gain is volume/256. The sound card's mixer (SB Pro CT1345 28h, SB16 CT1745 36h/37h) is the second stage, and resets to
  full. With no mixer (None, AdLib, SB 1.5, SB 2.0) CD audio plays at the drive's level.
- `get_cdrom_status` returns `drive.audio_status`, `drive.page_0e` (SOTC, per port channel and volume) and `audio` (the
  play state, range and position). `get_sound_status` shows the CD-DA level.

---

## 8. Timing

| Item | Model |
|---|---|
| Sector rate | 75 a second at 1x; 3.33 ms a sector at 4x |
| Seek | A power-law curve over the head's radial position: about 150 ms for a third of the stroke, 450 ms end to end |
| Rotation | The head waits for the sector to come round; 30 ms a turn inside, 70 ms outside at 4x |
| Read-ahead | After a read the drive keeps reading into its 256 KB buffer (128 sectors). A read inside it costs only the command, one just past waits for it, any other seeks (`read_ahead_start_lba`, `read_ahead_end_lba`) |
| Command time | A command that touches no disc ends 50 µs after its packet |
| Spin-up | 0.9 s after the tray closes on a disc; the tray itself opens and closes at once |
| Motor | START STOP with Start=0 stops it, and the next access pays the 0.9 s. No automatic spin-down |
| Audio | Plays at 1x whatever the data speed, after its seek and rotation |

UDVD2 allows 0.5 s to the packet DRQ, 7 s to the first data and 3 s between blocks; SeaBIOS waits up to 30 s for
a spinning-up disc.

---

## 9. Driving it from MCP

- `get_cdrom_status`: the drive (`fitted`, `position`, `speed`, `tray_open`, `disc`, `ready`, `spinning_up`, `motor_on`,
  `unit_attention`, `prevent_removal`, `head_lba`/`head_msf`, read-ahead window, `last_packet`, `sense`,
  `audio_status`, `page_0e`), the media, `audio` playback and the playlist.
- `get_ide_status` shows the drive in its position with the task file: status, error, interrupt reason, byte count,
  device, command, last packet, phase.
- `eject_cdrom`, `close_cdrom_tray`, `load_media` (a `.cue`, `.chd`, `.iso` or `.m3u`) and `insert_playlist_disc` move
  the tray without a reset. `debug_reset` with `boot: cdrom` is the way to boot a disc.
- `list_cdrom_tracks` and `read_cdrom_sector` read the image and do not move the head.
- Trace `cdrom.packets` (each packet with its 12 bytes and name), `cdrom.status` (completion, sense), `cdrom.data`
  (LBA and count), `cdrom.media` (tray and ready events) and `cdrom.cdda` (audio play events). The drive's ATA commands
  (A1h, 08h and the rest) show as `cdrom.packets` COMMAND events, not in the `ide` trace, which is the hard disks'.
