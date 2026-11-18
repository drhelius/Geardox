# Sound Cards, FM, MIDI and the PC Speaker — What MS-DOS Software Relies On

## 1. Overview

The machine has a PC speaker on the board and one optional sound card: AdLib or a Sound Blaster. A card brings FM chips
(Yamaha OPL), a DSP for digitized sound, a mixer and, on the SB16, a MIDI port. An MPU-401 can also be fitted alone. Read
it all with `get_sound_status`, the FM chips in detail with `get_opl_status` and `get_opl_registers`.

| Card (id) | DSP | FM | Mixer | Notes |
|---|---|---|---|---|
| None (`none`) | none | none | none | Only the speaker and the CD drive |
| AdLib (`adlib`) | none | One OPL2 | none | FM only |
| Sound Blaster 1.5 (`sb15`) | 1.05 | One OPL2 | none | No auto-init, no high-speed |
| Sound Blaster 2.0 (`sb20`) | 2.01 | One OPL2 | none | Auto-init, high-speed |
| Sound Blaster Pro (`sbpro`) | 3.00 | Two OPL2, left and right | CT1345 | 8-bit stereo |
| Sound Blaster Pro 2 (`sbpro2`) | 3.02 | One OPL3 | CT1345 | 8-bit stereo |
| Sound Blaster 16 (`sb16`, default) | 4.05 | One OPL3 | CT1745 | 16-bit, MPU-401 on the card |

- **Machine settings, applied at reset:** card, IRQ, DMA and MPU-401. Descriptor keys `card`, `irq`, `dma`, `mpu` in
  `[sound]`; command line `--sound`; libretro `geardox_sound_card`, `_irq` and `_dma`.
- **The base is fixed at 220h.** IRQ is 5 (default) or 7, 8-bit DMA 1 (default) or 3, SB16 16-bit DMA 5. The MPU-401 is on by
  default at 330h. Only the SB16 can move IRQ and DMA at run time, through mixer registers 80h and 81h.
- **BLASTER:** the built-in game disks write `SET BLASTER=A220 I5 D1 H5 P330 T6` from the settings. H is only on the SB16, P
  only with the MPU-401 on, T is the type digit (1 SB 1.5, 2 Pro, 3 SB 2.0, 4 Pro 2, 6 SB16). AdLib and None get no line.
  A, I, D, H, M, P are base, IRQ, 8-bit DMA, 16-bit DMA, mixer base, MPU base. Other disks need it set by hand.
- **Output is 44.1 kHz stereo.** FM, digitized sound, CD audio and the speaker are mixed after their own levels.

---

## 2. Ports

| Port | Access | Meaning | Present on |
|---|---|---|---|
| 388h, 389h | R/W, W | FM status and address, FM data | Every card with FM |
| 38Ah, 38Bh | R/W, W | OPL3 array 1 address, data | OPL3 cards |
| 2x0h, 2x1h | R/W, W | FM status and address, data. Pro: left chip. OPL3: array 0 | Pro, Pro 2, SB16 |
| 2x2h, 2x3h | R/W, W | Pro: right chip. OPL3: array 1 | Pro, Pro 2, SB16 |
| 2x4h, 2x5h | W, R/W | Mixer index, mixer data | Pro, Pro 2, SB16 |
| 2x6h | W | DSP reset | Every DSP |
| 2x8h, 2x9h | R/W, W | FM, the same as 388h and 389h | Every DSP |
| 2xAh | R | DSP read data | Every DSP |
| 2xCh | W, R | DSP command or data; write-buffer status (bit 7) | Every DSP |
| 2xEh | R | DSP read-buffer status (bit 7); acknowledges the 8-bit IRQ | Every DSP |
| 2xFh | R | Acknowledges the 16-bit IRQ | SB16 |
| 330h, 331h | R/W | MPU-401 data, status and command | MPU-401 on |

- 2x is 22h here, as the base never moves. A port a card lacks is open bus, FFh. The data port of an FM chip reads FFh.
- **Pro:** a write to 388h, 389h, 2x8h or 2x9h goes to **both** chips, so old software sounds on both sides. A read of 388h
  gives the left chip. Chip 0 is the left output, chip 1 the right.
- **OPL3 cards:** an address write to 38Ah or 2x2h sets A1 and reaches array 1 (100h-1FFh), once NEW is set (register 105h
  itself is always reachable). 388h, 2x0h, 2x8h and their data ports only reach array 0.

---

## 3. FM: OPL2 and OPL3

The OPL2 (YM3812) has 9 channels of 2 operators, or 6 plus 5 rhythm sounds. The OPL3 (YMF262) has 18 channels, 6 of which
can join in pairs as 4-operator ones, 8 waveforms and a left and right output. Both make 49,716 samples a second. Registers
cannot be read: write the address, then the data.

### 3.1 Registers (the OPL3 repeats the per-channel ones in array 1, at +100h)

| Register | Bits | Meaning |
|---|---|---|
| 01h | D5 | Waveform select enable. An OPL2 plays sine only while 0; an OPL3 always selects |
| 02h, 03h | 7-0 | Timer 1, Timer 2 preset |
| 04h | D7 RST, D6 MASK T1, D5 MASK T2, D1 start T2, D0 start T1 | RST set clears both flags and ignores the rest |
| 08h | D7 CSM, D6 NOTE SEL | CSM on the OPL2 only. NOTE SEL picks the F-number bit for key scaling |
| 20h-35h | D7 AM, D6 VIB, D5 EG-TYP, D4 KSR, D3-0 MULT | Per operator |
| 40h-55h | D7-6 KSL, D5-0 TL | TL in 0.75 dB steps |
| 60h-75h | D7-4 AR, D3-0 DR | 0 never moves, 15 is fastest |
| 80h-95h | D7-4 SL, D3-0 RR | SL in 3 dB steps, 15 is 93 dB |
| A0h-A8h | 7-0 | F-number low, per channel |
| B0h-B8h | D5 KON, D4-2 BLOCK, D1-0 F-number high | Key on and octave |
| BDh | D7 AM depth, D6 VIB depth, D5 RHYTHM, D4 BD, D3 SD, D2 TOM, D1 TC, D0 HH | Array 0 only |
| C0h-C8h | D3-1 FEEDBACK, D0 CONNECTION | OPL3 also D4 CHA (left), D5 CHB (right), D6 CHC, D7 CHD |
| E0h-F5h | D2-0 | Waveform: OPL2 0-3, OPL3 0-7 once NEW is set |
| 104h (OPL3) | D5-0 | Join as 4-operator: D0 channels 1+4, D1 2+5, D2 3+6, D3 10+13, D4 11+14, D5 12+15 |
| 105h (OPL3) | D0 NEW | 1 enables array 1, 8 waveforms, the pan bits and 4-operator |

- **Operator registers** use the slot offsets 00h-05h, 08h-0Dh, 10h-15h. Channels 1-9 have modulators at 00h 01h 02h 08h 09h
  0Ah 10h 11h 12h and carriers 3 higher: channel 1 is 20h and 23h.
- **Pitch:** F-number = Hz × 2^19 / 49716 / 2^(BLOCK-1), so Hz = F-number × 49716 / 2^(20-BLOCK). Printed note tables assume 50
  kHz, so `note_hz` reads about 0.5% flat of them (577 at block 4 is 437.7 Hz, not 440).
- **MULT:** 0 is x1/2; 1-9 are x1-x9; Ah and Bh x10; Ch and Dh x12; Eh and Fh x15. **KSL:** 0 none, 1 3 dB per octave, 2 1.5 dB,
  3 6 dB.
- **Envelope:** attack, decay to SL, then EG-TYP 1 holds SL until key off and EG-TYP 0 keeps falling at RR. Key off releases at
  RR. FB 0-7 is 0, pi/16 ... 4 pi of feedback on operator 1. **Connection** 0: operator 1 modulates operator 2, the only output;
  1: both are summed. Tremolo 3.7 Hz (1 dB, 4.8 dB with BD D7), vibrato about 6 Hz (7 or 14 cents with BD D6).
- **Waveforms:** 0 sine, 1 half sine, 2 absolute sine, 3 quarter pulses; the OPL3 adds 4, 5 (0, 2 at double rate), 6 square, 7.

### 3.2 Channels, 4-operator and rhythm

- **Channels 1-9** are array 0, **10-18** array 1. A 4-operator pair (set in 104h) is channels 1+4, 2+5, 3+6, 10+13, 11+14,
  12+15: the first carries pitch and key, and the CONNECTION bits of both pick the shape: 00 OP1>OP2>OP3>OP4, 01 (OP1>OP2) +
  (OP3>OP4), 10 OP1 + (OP2>OP3>OP4), 11 OP1 + (OP2>OP3) + OP4. Only OP1 feeds back. `get_opl_status` calls them `4op` and
  `4op_pair`; pitch writes to the second are ignored.
- **Rhythm** (BD D5 = 1) turns channels 7-9 into drums and leaves 6 melodic ones. Their key bits must stay 0; BD D4-D0 key the
  drums: the bass drum uses both operators of channel 7, hi-hat is channel 8's first operator, snare its second, tom-tom is
  channel 9's first, top cymbal its second. The slots' own envelope registers still shape each drum.
- **Pan (OPL3):** with NEW = 0 every channel plays to both sides; with NEW = 1 C0h D4 sends it left and D5 right (C, D unused).
- **The Pro's two OPL2s** are chip 0 (left) and chip 1 (right), 9 channels each. `set_audio_mute` numbers them 1-9 and 10-18.

### 3.3 Status, timers and detection

| Status bit | Meaning |
|---|---|
| 7 | IRQ: a timer flag is set |
| 6, 5 | Timer 1 flag, Timer 2 flag |
| 4-0 | Undefined. An idle OPL2 reads 06h there and an OPL3 00h, which is how software tells a Pro from a Pro 2 |

- **Timers** count up from the preset, overflow, reload and keep running: Timer 1 in 80 us steps, Timer 2 in 320 us. The period is
  256 - N steps on an OPL2 and 255 - N on an OPL3. Overflow sets the flag unless it is masked, and it stays until 04h is written
  with D7 set (80h). Geardox raises no interrupt from the timers; software polls the status.
- **AdLib detection:** write 60h then 80h to 04h; read the status, AND E0h, expect 00h; write FFh to 02h and 21h to 04h; wait at
  least 80 us; read the status, AND E0h, expect C0h; write 60h and 80h to 04h again.
- **Write timing:** a real OPL2 wants 3.3 us after an address and 23 us after data, which software meets by reading the status
  6 and 35 times. Geardox accepts writes at any speed.

---

## 4. The DSP

The DSP plays and records digitized sound by DMA and takes commands. Sound is 8-bit unsigned (silence 80h) or 16-bit signed
(silence 0), mono or stereo.

### 4.1 Reset and handshake

1. Write 1 to 2x6h, wait about 3 us, write 0.
2. In about 100 us AAh appears at 2xAh and 2xEh bit 7 reads 1. No AAh means no card at that base.
3. Send E1h and read two bytes: major then minor version (section 1).

- **Writing:** wait for 2xCh bit 7 to read 0, then write. Geardox shows busy for 8 reads in every 16, always while resetting and
  in high-speed mode. **Reading:** wait for 2xEh bit 7 to read 1, then read 2xAh; with nothing to read it repeats the last byte.
  The other status bits read 1.
- **A reset** ends any transfer and turns the speaker off, but one during high-speed mode or MIDI UART keeps rate and block size.

### 4.2 Commands

Lengths and block sizes are **one less** than the count of bytes (8-bit) or samples (16-bit), low byte first. A DSP older than
"From" ignores the command.

| Command | Operands | From | Meaning |
|---|---|---|---|
| 10h, 20h | sample | 1.xx | Direct output of one sample (the program times it); direct input reads 80h |
| 14h, 24h | len lo, hi | 1.xx | 8-bit single-cycle DMA output, input. IRQ at the end |
| 1Ch, 2Ch | | 2.00 | 8-bit auto-init output, input. Block size from 48h, IRQ per block |
| 40h | time constant | 1.xx | Rate = 1,000,000 / (256 - TC) Hz (9Ch is 10000 Hz) |
| 41h, 42h | rate hi, lo | 4.xx | Output, input rate in Hz (5000-45000). High byte first |
| 48h | len lo, hi | 2.00 | Block size for auto-init and high-speed |
| 90h, 98h, 91h, 99h | | 2.01 | High-speed auto-init, single-cycle; output, input. 3.xx only: a 4.xx DSP runs them as normal transfers |
| B0h-BFh, C0h-CFh | mode, len lo, hi | 4.xx | 16-bit, 8-bit DMA. Command bit 3 input, bit 2 auto-init; mode bit 5 stereo, bit 4 signed |
| 16h 17h 74h-77h 1Fh 7Dh 7Fh | len | 1.xx | ADPCM 2, 4, 3-bit output. 17h, 75h, 77h and the auto-init ones carry a reference byte |
| 80h | len lo, hi | 1.xx | Silence for len+1 sample periods, then an IRQ |
| D0h, D4h, D5h, D6h | | 1.xx | Pause, continue 8-bit DMA; D5h, D6h the same for 16-bit (4.xx) |
| D1h, D3h, D8h | | 1.xx | Speaker on, off, status (FFh on, 00h off). D8h from 2.00 |
| D9h, DAh | | 4.xx, 2.00 | End 16-bit, 8-bit auto-init at the end of the current block |
| E1h | | 1.xx | Version: major, minor |
| 34h-37h, 38h | byte | 2.00, 1.xx | MIDI UART mode (every byte written after is MIDI out; only a reset leaves it), one MIDI byte |
| A0h, A8h | | 3.xx | Mono, stereo input |

- **Answered but not in Creative's guide:** E0h (returns the complement of its operand), E2h (a test byte by DMA), E3h (copyright
  string), E4h and E8h (test register), F2h and F3h (raise the 8-bit and, on 4.xx, the 16-bit IRQ), F8h (0, before 4.xx), and on
  4.xx the processor commands 04h, 05h, 08h (version 18h), 0Eh, 0Fh, F9h and FAh. Any other byte is ignored.
- **Speaker:** before 4.xx sound is silent until D1h. On the SB16 it always plays and D1h/D3h only set what D8h returns. On a
  1.xx DSP D1h and D3h also pause the DMA.
- **High-speed mode** takes no commands until a reset, and the busy bit stays set.

### 4.3 Transfers and IRQ

- **DMA first.** Program the 8237 channel (address, count, single or auto-initialize mode, unmasked) before the DSP command. With
  the channel masked the DSP waits: nothing plays and no IRQ comes. 8-bit sound uses the 8-bit channel, 16-bit sound the 16-bit
  one in words (or the 8-bit one in bytes when none is set). Input records silence.
- **IRQ at the end of every block.** The line stays up until the program acknowledges: read 2xEh for 8-bit sound and MIDI, 2xFh
  for 16-bit sound, the MPU data port for the MPU-401. No acknowledge means no second IRQ. The SB16 shares one line among them and
  mixer register 82h says which are pending.
- **Auto-init** loops the block until D9h/DAh or a reset. A single-cycle command of the same kind sent meanwhile takes over at the
  end of the current block.
- **Pro stereo (3.xx):** set mixer 0Eh bit 1. 8-bit data is left, right pairs and the time constant counts each byte, so the
  per-channel rate is half (44100 Hz TC plays 22050 Hz stereo). The SB16 uses the mode byte instead.
- **Filter:** the Pro's output has a 3.2 kHz low-pass unless 0Eh bit 5 is set. Other cards low-pass at 0.45 of the rate.

---

## 5. Mixers

Index to 2x4h, data to 2x5h. Writing any value to index 00h resets the mixer. The SB 1.5, SB 2.0 and AdLib have none and play every
source at full level. **A reset sets full volume,** not Creative's quieter defaults, so games that never write the mixer are heard.

### 5.1 CT1345 (Pro, Pro 2)

- **Volumes** (3 bits a side, left D7-5, right D3-1): 04h voice, 22h master, 26h MIDI (**scales FM**), 28h CD, 2Eh line (kept, not
  mixed). 0 is -46 dB, 7 is 0 dB, about 3.7 dB a step. Voice, FM and CD end at their volume times master. Reads set the unused
  bits 4 and 0.
- **0Eh:** D1 stereo switch, D5 output filter off. **0Ah** microphone and **0Ch** input source, filter and low-pass select are
  kept and do nothing, as nothing records. An unknown index reads FFh.

### 5.2 CT1745 (SB16)

| Index | Meaning |
|---|---|
| 30h-31h, 32h-33h, 34h-35h, 36h-37h | Master, voice, MIDI (**scales FM**) and CD, left then right. 5 bits (7-3), 2 dB a step, -62 dB to 0 dB |
| 38h-3Ah, 3Bh | Line, microphone, PC speaker: kept. The speaker's level does not follow 3Bh |
| 3Ch | Output switches: D2 CD left and D1 CD right cut the CD when 0; the others are kept |
| 3Dh-47h | Input switches, gains, AGC, treble, bass: kept. Output gain 41h, 42h bits 7-6 multiply by 1, 2, 4 or 8 |
| 04h 0Ah 22h 26h 28h 2Eh | The Pro's registers, mapped onto the above at 4 bits a side |
| 80h | IRQ: D0 IRQ 2 (arrives as IRQ 9), D1 5, D2 7, D3 10 |
| 81h | DMA: D0, D1, D3 are 8-bit channels 0, 1, 3; D5, D6, D7 16-bit channels 5, 6, 7 |
| 82h | Interrupt status: D0 8-bit sound and MIDI, D1 16-bit sound, D2 MPU-401 |

- Registers 30h-47h read back what was written. After a reset 30h-37h are full level, 3Ch 1Fh, 3Dh 15h, 3Eh 0Bh, 44h-47h 8. Writing
  80h or 81h moves the card's IRQ or DMA at once.

### 5.3 CD audio

CD-DA is the ATAPI drive's output (page 0Eh volume, see `pc_ide_cdrom`) times the card's CD volume (28h on the Pro, 36h and 37h on
the SB16) times master. It resets to full. Without a mixer the CD plays at the drive's level.

---

## 6. MPU-401

An MPU-401: data at 330h, status (read) and command (write) at 331h. The `mpu` setting picks the mode: `on` (default) is
**intelligent**, the Roland MPU-401's own sequencer and command set that older MT-32 games need, with its tracks, tempo,
timebase, conductor, clock to host and requests for the host; `uart` is the plain MIDI port most later games use; `off` fits none.
Both send MIDI to the host MIDI output. Status bit 7 reads 0 when a byte waits for the host and 1 when none; bit 6 reads 0
(ready) always. Idle reads BFh.

- **UART only mode:** FFh resets, leaves UART mode and returns FEh. 3Fh enters UART mode and returns FEh. Before UART mode any other
  command is acknowledged with FEh and does nothing; in UART mode only FFh is taken. Detection is FFh then a read of FEh.
  Data written outside UART mode is dropped.
- **Interrupt:** asserted while a byte waits (the acknowledge counts), cleared by reading the data port. On the **SB16** it is the
  card's line, shared (mixer 82h bit 2). On any other card, or with None, it is a separate board on IRQ 2, which the AT's second
  PIC delivers as **IRQ 9**.
- The DSP's MIDI commands (38h and the UART ones) send to the same output.

---

## 7. The PC Speaker

- **Path:** PIT channel 2 (port 42h) counts down from 1,193,182 Hz; its OUT is ANDed with port 61h bit 1, and bit 0 is the gate. A
  tone is channel 2 in mode 3, gate on, data on, at 1,193,182 / count Hz.
- **Port 61h:** bit 0 gate, bit 1 speaker data, bits 2-3 as written; bit 4 refresh toggle and bit 5 channel 2 OUT, read only.
- **Samples** play too, by writing counts to channel 2 fast or by toggling bit 1. Geardox averages the speaker over each output
  sample, so both survive. A square wave past 22.05 kHz (count under 54) can't be heard and plays as its 50% average.
- No mixer register changes the speaker's level.

---

## 8. Driving it from MCP

- `get_sound_status`: the `card`; `opl` per chip (`model`, `status`, `new_mode`, `rhythm`, `timer_1` and `timer_2` with reload,
  running, masked and flag, `channels` with `f_number`, `block`, `key`, `connection`, `feedback`, `type` 0 2-op, 1 4-op, 2 4-op
  pair, 3 drum, `envelope`, `output`); `dsp` (`version`, `irq`, `dma_8`, `dma_16`, `speaker`, `rate_hz`, `irq_status` laid out as
  mixer 82h, `filter_hz`, `midi_uart`, `last_command`, and a `transfer` with active, paused, `format`, `auto_init`, `input`,
  `high_speed`, `stereo`, `signed`, `block`, `remaining`); `mixer` (`voice`, `fm` and `cd` gains as left and right fractions);
  `mpu401` (`mode` intelligent or uart_only, `uart`, `pending`, `standalone`, `irq`; when intelligent also `playing`,
  `recording`, `running`, `tempo`, `relative`, `effective_tempo`, `timebase`, `active_tracks`, `requests`, `counters`, `waiting`,
  `conductor` and `clock_to_host`); `midi` (`capture`, `messages`, `bytes`, `dropped`, `synth`, `port`).
- `get_opl_status` takes `chip` (0 or 1, the Pro's second OPL2; default all) and `channel` (1-9 on an OPL2, 1-18 on an OPL3;
  default all). Per channel: `f_number`, `block`, `note_hz`, `key`, `type` (2op, 4op, 4op_pair, drum), `connection`, `feedback`,
  `output`, `muted`, `pan_left` and `pan_right` on the OPL3, `pair_channel` for a 4-op pair, and both operators with their
  `slot` (1-36), `am`, `vib`, `eg_type`, `ksr`, `mult`, `ksl`, `tl`, `ar`, `dr`, `sl`, `rr`, `waveform`, `key`,
  `envelope_state`, `envelope` (attenuation, 1FF silent) and `level_percent`. Per chip: `model`, `status`, `address_latch`,
  `rhythm` (enable, depths and the five drums), `note_select`, `new_mode` on the OPL3, `waveform_enable` and `csm` on an OPL2,
  and `timers` with `reload`, `period_us`, `running`, `masked` and `flag`.
- `get_opl_registers` takes `chip` and `bank` (0 is 000h-0FFh, 1 is 100h-1FFh, OPL3 only) and returns every register as last
  written, with its name, and the address latch: what the game programmed.
- `set_audio_mute` mutes `source` `fm`, `pcm`, `cdda` or `buzzer` (`mute` true or false) in the debugger only, emulation
  unchanged. `channel` mutes one FM channel: 1-18 on the OPL3, 1-9 on an OPL2, and on the Pro 1-9 the left chip and 10-18 the
  right. Mute channels one by one to find the one that makes a sound.
- `write_io_port` takes `port` (hex), `value` (hex) and `size` 8, 16 or 32: an OUT as the CPU does it, so the device reacts and
  the `io.writes` trace sees it. Use it to poke an OPL or DSP register.
- `get_pit_status` shows counter 2 (`mode`, `reload`, `frequency_hz`, `gate`, `out`) and `speaker` (`gate`, `data`, `out2`,
  `output`, `refresh_toggle`). `get_dma_status` shows the DSP's channel (`mask`, `request`, `auto_initialize`, current and base
  address and count, `terminal_count`).
- **Trace filters** for `set_trace_log`: `fm.key`, `fm.frequency`, `fm.channels`, `fm.global`, `fm.timers` and `fm.operators` (the
  last off by default) log each data write with its chip, register and value; `pcm.commands` each DSP command with its operands,
  `pcm.transfers` each started transfer (format, flags, length, rate), `pcm.irqs` each IRQ raised and acknowledged (8 or 16);
  `mixer.volume` and `mixer.control` mixer writes; `midi.data` each MIDI byte (DSP or MPU-401), `midi.commands` MPU-401
  commands and `midi.messages` the bytes it queues for the host; `timer.speaker` changes of port 61h bits 0-1; `dma.registers`, `dma.requests`, `dma.ends`; `io.reads`, `io.writes`.
- **No sound:** is it the card the game expects (`card`, `dsp.version`); does it read AAh after the reset; is a `transfer` active
  with its DMA channel masked; is `irq_status` stuck at 01h (no acknowledge); is the speaker off; is something muted.
