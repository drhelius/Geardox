# Mouse, Joystick, Serial and Parallel Ports — What MS-DOS Software Relies On

## 1. Overview

The machine has one mouse, one joystick, two serial ports and one parallel port. The mouse and the joystick are fed by
the host (a mouse and a gamepad) or by the MCP tools `mouse_move`, `mouse_click` and `joystick_set`. Read their state
with `get_input_state`, `get_serial_status` and `get_parallel_status`.

| Device | Where | Choice |
|---|---|---|
| Mouse | PS/2 on the 8042 auxiliary port (IRQ 12), or Microsoft serial on COM1 | Machine setting, applied at reset. None, PS/2 (default) or Serial |
| Joystick | Game port at 201h | Input setting, applied at once. None, Joystick, Gamepad or Flight stick |
| COM1, COM2 | 3F8h IRQ 4, 2F8h IRQ 3 | Two 16550A. Nothing on COM2, the serial mouse on COM1 when chosen |
| LPT1 | 378h IRQ 7 | Output only, nothing attached |

The FreeDOS disk loads CTMOUSE from `FDAUTO.BAT`, which finds either mouse. With no mouse it prints one line at boot.

---

## 2. Mouse

One mouse with left, right and middle buttons. The wheel is ignored. The middle button only reaches a device that has
one, and neither mouse here does. Motion is kept as counts and each device takes what fits in a packet.

### 2.1 PS/2 Mouse

IBM's two-button mouse on the auxiliary port of the 8042, which is the 60h/64h pair `get_keyboard_status` shows.

| Item | Behaviour |
|---|---|
| Interrupt | IRQ 12 (vector 74h) when a mouse byte enters the output buffer; status bit 5 says the byte is from the mouse |
| Sending to it | Command D4h, then the byte at 60h. Without a mouse fitted the byte times out after 15 ms with FEh |
| Enabling | Command byte bit 5 (or command A7h) holds its clock low: the mouse keeps its data until released |
| Priority | A keyboard byte goes before a mouse byte |
| Defaults | Stream mode, 100 reports a second, 4 counts per mm, 1:1 scaling, reporting **disabled** until F4h |

Commands the mouse answers, each with FAh first:

| Command | Meaning |
|---|---|
| FFh | Reset: FAh, AAh, 00h |
| FEh | Resend the last packet (no ACK) |
| F6h | Set defaults |
| F5h, F4h | Stream reporting off, on |
| F3h | Sample rate: 10, 20, 40, 60, 80, 100 or 200 per second; another value gets FEh |
| F2h | Read ID: FAh 00h |
| F0h, EAh | Remote mode, stream mode |
| EEh, ECh | Wrap mode, back from it |
| EBh | Read one packet (remote mode) |
| E9h | Status: three bytes (bit 6 remote, 5 enabled, 4 scaling, 2 left button, 0 right button; resolution code; rate) |
| E8h | Resolution 00h–03h: 1, 2, 4, 8 counts per mm |
| E7h, E6h | 2:1 scaling, 1:1 scaling |

A command byte that isn't valid gets FEh, a second one in a row FCh. Every command but FEh clears the count
accumulators.

Packet, three bytes in stream mode, sent at the end of each sample interval if a button changed or was held, or at
least one count moved:

| Byte | Bits |
|---|---|
| 1 | 7 Y overflow, 6 X overflow, 5 Y sign, 4 X sign, 3 always 1, 2 0 (no middle button), 1 right, 0 left |
| 2 | X, low 8 bits of a 9-bit two's complement number |
| 3 | Y, the same; **Y grows upwards** |

Counts past -256..255 are sent at the limit with the overflow bit and the rest is lost. A still mouse sends nothing.
A button pressed and released within one interval is reported pressed. INT 15h C2h (the BIOS pointing device
services) is the first mouse traffic of a boot: it fails with AH=04h when the mouse doesn't answer.

### 2.2 Serial Mouse

A Microsoft-compatible two-button mouse on COM1, as Logitech's C7-M documents it.

| Item | Behaviour |
|---|---|
| Line | 1200 bit/s, **7 data bits, 2 stop bits** (10 bits a byte, 8.33 ms; 25 ms a packet). The driver sets the UART |
| Power | RTS or DTR at either level. It never loses power |
| Identification | When the host toggles RTS the mouse sends `M` (4Dh) about 14 ms later, and resets to 1200 bit/s |
| Modem lines | Only RXD, TXD, RTS, DTR and ground are wired, so MSR's CTS, DSR, DCD and RI read inactive |
| Rate | Packets only after motion or a button change, at most 40 a second |
| Overflow | Counts past -128..127 are kept for the next packet |

Packet, three bytes, **Y grows downwards**:

| Byte | Bits |
|---|---|
| 1 | 7 is 0, 6 is 1 (the sync bit), 5 left, 4 right, 3-2 Y7-Y6, 1-0 X7-X6 |
| 2 | 6 is 0, 5-0 X5-X0 |
| 3 | 6 is 0, 5-0 Y5-Y0 |

If the UART isn't set to 1200 bit/s and 7 data bits the bytes arrive with framing errors, or with bit 7 set at 8 data
bits.

### 2.3 Driving it from MCP

- `mouse_move` moves to a screenshot pixel (it first finds the pointer by moving 32 counts out and back and comparing
  frames) or by counts. `mouse_click` presses left, right or middle.
- `get_input_state` shows the host's pending counts and buttons, and, for a PS/2 mouse, the mode, reporting, rate,
  resolution, scaling, accumulators and queued bytes. For a serial mouse it shows RTS, readiness and the queue.
- A mouse never read by the software leaves counts pending. `mouse_move` fails after 60 frames of that.
- Trace with `input.changes`, `input.commands` (mouse commands), `input.packets` and, for the serial line,
  `serial.data` and `serial.control`.

---

## 3. Serial Ports

Two 16550A UARTs with the 1.8432 MHz clock of the PC serial adapter. SeaBIOS finds them at 3F8h and 2F8h and stores
their bases at 40:00h. It also probes 3E8h and 2E8h and finds nothing.

| Port | Base | IRQ | Vector | Attached |
|---|---|---|---|---|
| COM1 | 3F8h–3FFh | 4 | 0Ch | The serial mouse when chosen, otherwise nothing |
| COM2 | 2F8h–2FFh | 3 | 0Bh | Nothing |

### 3.1 Registers

| Offset | DLAB 0 read | DLAB 0 write | DLAB 1 |
|---:|---|---|---|
| 0 | RBR, received byte | THR, byte to send | DLL, divisor low |
| 1 | IER | IER | DLM, divisor high |
| 2 | IIR | FCR | |
| 3 | LCR | LCR | |
| 4 | MCR | MCR | |
| 5 | LSR | | |
| 6 | MSR | | |
| 7 | Scratch | Scratch | |

- **Baud rate:** 1,843,200 / (16 × divisor). 1 is 115200, 12 is 9600, 96 is 1200.
- **IER:** bit 0 received data, 1 THR empty, 2 line status, 3 modem status.
- **IIR:** bit 0 clear when an interrupt is pending, bits 3-1 its source (011 line status, 010 received data, 110
  timeout, 001 THR empty, 000 modem status), bits 7-6 set when the FIFO is on (C1h and so on).
- **FCR:** bit 0 FIFO enable, bit 1 clears the receive FIFO, bit 2 the transmit FIFO, bits 7-6 the receive trigger
  (1, 4, 8 or 14 bytes). FIFOs are 16 bytes. A receive timeout fires after four character times.
- **LCR:** bits 1-0 data bits minus 5, bit 2 stop bits, bit 3 parity enable, bit 4 even, bit 5 stick, bit 6 break,
  bit 7 DLAB.
- **MCR:** bit 0 DTR, 1 RTS, 2 OUT1, 3 OUT2, 4 loopback.
- **LSR:** bit 0 data ready, 1 overrun, 2 parity error, 3 framing error, 4 break, 5 THR empty, 6 transmitter empty,
  7 FIFO error.
- **MSR:** bits 7-4 DCD, RI, DSR, CTS; bits 3-0 their changes (the trailing edge for RI).

### 3.2 Behaviour that matters

- **OUT2 gates the IRQ line.** With MCR bit 3 clear no interrupt reaches the PIC. A driver that never sets OUT2 never
  gets an IRQ.
- **Priority:** line status, received data (or timeout), THR empty, modem status. Reading LSR clears line status,
  reading RBR received data, reading IIR or writing THR clears THR empty, reading MSR clears modem status.
- **THR empty** is pending as soon as IER bit 1 is set while THR is empty. Probes rely on it.
- **Loopback** wires DTR to DSR, RTS to CTS, OUT1 to RI and OUT2 to DCD, and TX to RX. The outputs go inactive.
- **Timing:** a character takes (1 start + data + parity + stop bits) × 16 × divisor / 1.8432 MHz. 1200 bit/s 7N1 is
  7.5 ms. THR empty comes back at once, the transmitter is empty one character later.
- **Overrun:** with the FIFO off a byte arriving over an unread one sets OE and replaces it. With the FIFO on, a byte
  arriving at a full FIFO is lost.
- **Nothing attached:** transmitted bytes leave at the line rate and vanish. MSR reads 00h in its input bits.

### 3.3 Debugging

- `get_serial_status` shows both ports with the registers, their bits decoded, the divisor and baud rate, the FIFO
  counts and the IRQ line.
- Trace `serial.writes` (every register write), `serial.data` (bytes received and sent) and `serial.control` (modem
  line and IRQ changes). The default is data plus control.
- Break on the IRQ with `set_breakpoint_on_interrupt` for IRQ 4 or 3, and on the ports with I/O breakpoints at
  03F8h–03FFh and 02F8h–02FFh.

---

## 4. Game Port

One port at 201h, decoded at 200h–207h (a Sound Blaster's game port is the same). IBM's own card decodes only 201h.

| Access | Behaviour |
|---|---|
| Read 201h | Bits 0-3: one-shots A-X, A-Y, B-X, B-Y, **1 while the pulse lasts**. Bits 4-7: buttons A1, A2, B1, B2, **0 when pressed** |
| Write 201h | Any value fires all four one-shots. A write during a pulse restarts all four |

- **Pulse length:** 24.2 µs + 0.011 µs × R, with R from 0 to 100 kΩ: 24.2 µs to 1124.2 µs, 574.2 µs at the centre.
  Software times it by counting loop passes or by reading the timer, so a faster CPU counts more.
- **Before any fire** the axis bits read 0. **An unplugged axis reads 1** until the next fire and never times out.
- **Buttons with nothing attached** read 1.
- **The axis positions are taken at the fire,** as a real stick's resistance is.
- **BIOS:** INT 15h AH=84h reads the stick and sets equipment bit 12 (added to SeaBIOS for this machine). DX=0 reads
  the switches into AL bits 7-4, DX=1 the four axes into AX, BX, CX and DX (A-X, A-Y, B-X, B-Y).

### 4.1 Devices

| Device | Axes | Buttons |
|---|---|---|
| None | All unplugged | All read 1 |
| Joystick | A-X, A-Y, proportional | A1, A2 (IBM's two-button stick) |
| Gamepad | A-X, A-Y at 0, 50 or 100 kΩ: nine positions, not proportional | A1, A2, B1, B2 (Gravis PC GamePad) |
| Flight stick | A-X, A-Y proportional, throttle on B-Y, B-X unplugged | Trigger and three more plus a four-way hat, one at a time as 4-bit codes on bits 7-4 (CH FlightStick Pro) |

- A game that knows two buttons reads the first two on the Joystick and the Gamepad.
- The flight stick's buttons don't act alone: two held together report one only, and a hat position reads as a code no
  single button gives. Games not made for it see odd presses.
- No device has both of B's axes, so a game that looks for a second stick finds none.
- The host gamepad's left stick (and D-pad) drives A-X and A-Y; its buttons drive the joystick buttons. There are no
  controller ports.

### 4.2 Driving it from MCP

- `joystick_set` injects axes (`ax`, `ay`, `bx`, `by`, -32768..32767, 0 is the centre), buttons 1–4 and the hat. Axes
  replace the host's until `reset`; buttons and hat are ORed with the host's. `device` plugs in another joystick at
  once.
- `get_input_state` reports the device, the axes and buttons, what was injected, the game port byte (what a read of
  201h returns now) and each axis pulse in microseconds from the last fire, or `open` for an unplugged axis.
- Trace `input.fires` for each fire with the pulse of each axis, and `input.changes` for host changes.

---

## 5. Parallel Port

LPT1 at 378h–37Ah, IRQ 7 (vector 0Fh), output only, **nothing attached**. SeaBIOS probes 378h and 278h and stores the
base at 40:08h.

| Port | Register | Behaviour |
|---|---|---|
| 378h | Data | A latch that reads back the last write, which is how BIOSes detect the port |
| 379h | Status | **7Fh** with nothing attached: bit 7 (BUSY, inverted) reads 0, the pulled-up lines read 1 |
| 37Ah | Control | Bits 0-3 strobe, autofeed, -init, select-in (all but -init inverted at the connector), bit 4 IRQ enable, bits 6-7 read 1 |

- **No interrupt:** with nothing attached ACK never pulses, so IRQ 7 never fires even with control bit 4 set. A sound
  card on IRQ 7 can't clash.
- **Printing:** INT 17h AH=02h returns 30h (out of paper, selected), which DOS reads as no printer. AH=00h waits for
  status bit 6 and times out after about 1.1 s, so `COPY FILE PRN` gives DOS's "not ready" prompt.
- `get_parallel_status` shows the three registers and their bits. Trace `parallel.data` and `parallel.control`.
