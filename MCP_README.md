# Geardox MCP Server

A [Model Context Protocol](https://modelcontextprotocol.io/introduction) server for the Geardox emulator, enabling AI-assisted debugging and development of MS-DOS PC software and games.

This server provides tools for game development, rom hacking, translation, reverse engineering, and debugging through standardized MCP protocols compatible with AI agents like Claude Code, Codex and others.

## Downloads

These MCP Bundles (`.mcpb`) package Geardox with its MCP server configuration for MCP clients that install bundles.
Claude Code and Codex use a regular Geardox build instead, see [Quick Start](#quick-start).

<table>
  <thead>
    <tr>
      <th>Platform</th>
      <th>Architecture</th>
      <th>Download Link</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td rowspan="2"><strong>Windows</strong></td>
      <td>x64</td>
      <td><a href="https://github.com/drhelius/Geardox/releases/download/0.0.2/Geardox-0.0.2-mcpb-windows-x64.mcpb">Geardox-0.0.2-mcpb-windows-x64.mcpb</a></td>
    </tr>
    <tr>
      <td>ARM64</td>
      <td><a href="https://github.com/drhelius/Geardox/releases/download/0.0.2/Geardox-0.0.2-mcpb-windows-arm64.mcpb">Geardox-0.0.2-mcpb-windows-arm64.mcpb</a></td>
    </tr>
    <tr>
      <td rowspan="2"><strong>macOS</strong></td>
      <td>x64</td>
      <td><a href="https://github.com/drhelius/Geardox/releases/download/0.0.2/Geardox-0.0.2-mcpb-macos-x64.mcpb">Geardox-0.0.2-mcpb-macos-x64.mcpb</a></td>
    </tr>
    <tr>
      <td>ARM64</td>
      <td><a href="https://github.com/drhelius/Geardox/releases/download/0.0.2/Geardox-0.0.2-mcpb-macos-arm64.mcpb">Geardox-0.0.2-mcpb-macos-arm64.mcpb</a></td>
    </tr>
    <tr>
      <td rowspan="2"><strong>Linux</strong></td>
      <td>x64</td>
      <td><a href="https://github.com/drhelius/Geardox/releases/download/0.0.2/Geardox-0.0.2-mcpb-linux-x64.mcpb">Geardox-0.0.2-mcpb-linux-x64.mcpb</a></td>
    </tr>
    <tr>
      <td>ARM64</td>
      <td><a href="https://github.com/drhelius/Geardox/releases/download/0.0.2/Geardox-0.0.2-mcpb-linux-arm64.mcpb">Geardox-0.0.2-mcpb-linux-arm64.mcpb</a></td>
    </tr>
  </tbody>
</table>

## Features

- **Full Debugger Access**: Intel 80386, 486 and Pentium registers with the x87 FPU, descriptor tables, paging, memory inspection, execute, data, I/O and interrupt breakpoints, and execution control
- **i386 Addressing**: Every address takes linear, `SR:offset` or `selector:offset` forms, translated through segmentation and paging in real, protected and VM86 mode
- **Multiple Memory Areas**: Linear and physical spaces, side-effect free I/O port reads, Main RAM, the System BIOS, CMOS RAM, VGA planes, the CPU's video window, the Cirrus linear aperture, shadow RAM and upper memory, the Gravis UltraSound's DRAM, media images, floppy images and IDE sector buffers, and I/O port writes with `write_io_port`
- **Disassembly**: Intel-syntax 80386 disassembly from current memory in 16 or 32-bit code, with control flow, targets, I/O port and interrupt vector names, and the BIOS or DOS function an `INT` is about to call
- **Hardware Inspection**: 8259A interrupt controller pair, 8254 timer, 8237 DMA pair, MC146818 RTC and CMOS, 8042 keyboard controller, 16550 serial ports, parallel port, VGA, Cirrus GD5428 and GD5446, CGA and EGA video, OPL2 and OPL3 FM chips, Sound Blaster DSP and mixers, Gravis UltraSound, MPU-401 and MIDI out, ATAPI CD-ROM, 82077 floppy controller and IDE hard disks. PCI on the Pentium models is covered by a documentation resource and the I/O breakpoints, I/O trace and BIOS log; there is no PCI-specific tool
- **Machine and Game Info**: The machine in use and where each setting came from, the built-in SeaBIOS log, the Turbo button, and the open game's `.gdox` descriptor with its overrides, its DOS, its disk and where saves go
- **Audio Inspection**: OPL FM channels, operators and registers, the Sound Blaster DSP transfer, IRQ and DMA, its mixer levels including CD, the Gravis UltraSound's registers and voices, the MPU-401, MIDI out and the built-in MT-32 and General MIDI synths, the volume settings, debugger mutes per source and FM channel
- **Storage Inspection**: CD-ROM TOC, CD audio and disc sectors; floppy drives, track sector lists and sector data; IDE channels and hard disks with their sectors
- **Text and Graphics Screens**: Read a text mode screen as characters and attributes, decode video memory as PNG planes, planar, 256 colors, text or Cirrus linear modes, and read the VGA, CGA and EGA palettes and registers
- **Symbol Support**: Add, remove, load, list, and look up user symbols, plus automatic labels
- **Program Development**: Run a program you are building through GDXDBG, follow where DOS loaded it, its console output and exit code, load its map symbols where DOS put it, and read DOS's memory chain
- **Input**: Inspect the PC keyboard, PS/2 or serial mouse and game port joystick, press keys and type text, move and click the mouse to screen pixels, drive the joystick, and run frame-timed input macros
- **Key Map**: Read and change the host key map and its per-game bindings, press host keys through it, and open or close the keyboard mapper
- **Game Descriptors**: Load a `.gdox` descriptor, a folder or ZIP with one or that the game database recognises, or a game's DOS program, to open its game with its machine and DOS settings, choose whether it runs the game, its setup or the DOS prompt, and read what was applied
- **Bookmarks**: Memory and disassembler bookmarks for navigation
- **Call Stack**: View calls, interrupts and exceptions with their vectors
- **Trace Logger**: CPU instruction trace with interleaved hardware events (interrupts and exceptions, mode, paging and task switches, I/O ports, PIC, timers, DMA, video, FM, PCM, mixer, MIDI, Gravis UltraSound, CD-ROM, FDC, IDE with the file a sector belongs to, keyboard, mouse and joystick, serial, parallel, system, PCI, debug console), each with the address of the code behind it, in memory or streamed to disk. Only the instruction stream steps the CPU; device and I/O streams record while the dynarec runs
- **Profiler**: Per-function and per-interrupt-vector call counts, calls per frame, and inclusive/exclusive cycle statistics
- **Screenshot Capture**: Get current frame as PNG image
- **Video Recording**: Record emulated video and audio to AVI files on disk
- **Rewind**: Time-travel debugging with snapshot status and seek tools
- **Documentation Resources**: Built-in hardware and programming documentation for AI context
- **GUI Integration**: MCP server runs alongside the emulator GUI, sharing the same state

## Transport Modes

The Geardox MCP server supports two transport modes:

### STDIO Transport (Recommended)

The default mode uses standard input/output for communication. The emulator is launched by the AI client and communicates through stdin/stdout pipes.

### HTTP Transport

The HTTP transport mode runs the emulator with an embedded web server on `127.0.0.1:7777/mcp` by default. The emulator stays running independently while the AI client connects via HTTP. Each request's `Host` and browser `Origin` must match the address on which its connection reached the server. Loopback mode can run without authentication; wildcard and other non-loopback bind addresses require `GEARDOX_MCP_HTTP_TOKEN`, and the server refuses to start without it.

### Headless Mode

Add `--headless` to run without a GUI window. This is useful for servers, CLI agents, or any machine without a display. All MCP tools work identically in headless mode. Requires `--mcp-stdio` or `--mcp-http`, or a development run with `--run`, `--target debug` or `--gdb` ([DEV_README.md](DEV_README.md)).

Media and machine can be set at launch too, for example `--cd game.cue --fd1 disk.img --model pentium_90 --ram 32 --sound sb16 --load-state 1`, or a game descriptor, folder or ZIP as the media argument with `--target game`, `voodoo` (its 3dfx program), `setup` or `prompt`. The machine options (`--model`, `--cpu-mhz`, `--ram`, `--fpu`, `--sound`, `--gus`, `--video`, `--mouse`, `--cdrom`, `--joystick`) apply to that run only; run `geardox --help` for the full list.

### Pause When Inactive

While the MCP server is running, the emulator keeps running when its window loses focus, even with **Pause When Inactive** enabled in the **Emulator** menu. The setting keeps its value and applies again once the server stops. Tools that wait for frames to run (`debug_step_frame`, `controller_macro`, `mouse_move`, `mouse_click`, `keyboard_type`) end with an error if emulation is paused from the GUI while they wait. Losing focus releases the keys the host keyboard holds, but the keys an MCP tool pressed stay held until a tool releases them, `debug_reset` runs or the server stops.

### Concurrent Clients

The HTTP server accepts repeated valid MCP initialization requests. All connected clients control the same Geardox instance. Individual HTTP requests are serialized, but multi-request debugging workflows are not atomic. Concurrent agents can interfere with each other through pauses, resets, breakpoints, memory writes, media loads, and save states.

For independent agent tasks, run one Geardox instance per agent on a unique HTTP port. Use `--headless` and give each instance its own portable application directory so its configuration and runtime files are isolated:

```bash
./geardox --mcp-http --headless --portable --mcp-http-port 7778
```

The `--portable` option stores configuration and user data beside the application. Alternatively, create an empty `portable.ini` beside the executable in each application directory. On macOS, place it next to each `.app` bundle.

## MCP Tool Router

By default, Geardox exposes every MCP tool directly. This avoids nested tool discovery in clients that already defer MCP schemas, including Claude Code.

Add `--mcp-router` to expose a compact set of high-frequency tools directly and route advanced debugger tools through lightweight discovery tools. This reduces MCP context while preserving access to the full debugger surface.

Direct tools in routed mode: `load_media`, `get_media_info`, `debug_pause`, `debug_continue`, `debug_step_into`, `debug_step_over`, `debug_step_out`, `get_i386_status`, `read_memory`, `write_memory`, `get_disassembly`, `set_breakpoint`, `get_screenshot`, `get_text_screen`, and `joystick_set`.

Router tools:

- `list_tool_categories` lists routed tool categories with descriptions and tool counts.
- `get_category_tools` lists routed tools in a category with compact descriptions.
- `search_tools` searches direct and routed tools and returns compact category/tool/description matches.
- `get_tool_info` returns one tool's real input schema and metadata.
- `execute_tool` executes a routed tool by name. First use `search_tools` or `get_category_tools` to discover the tool, then call `get_tool_info` to obtain its exact input schema.

Example routed call:

```json
{
  "name": "translate_address",
  "arguments": {
    "address": "CS:1234"
  }
}
```

Without `--mcp-router`, call every MCP tool directly.

## Quick Start

Geardox has its BIOS built in (SeaBIOS and SeaVGABIOS), so there is no firmware to set and nothing to load before starting. With no media attached it boots a built-in DOS hard disk: FreeDOS, or your own MS-DOS once you import it.

Get a desktop build of Geardox from the [downloads](README.md#downloads) or
[build it from source](README.md#build-instructions). In the examples below, replace `/path/to/geardox` with the full
path to the executable:

- **macOS:** `/Applications/Geardox.app/Contents/MacOS/geardox`
- **Linux:** `/path/to/geardox`
- **Windows:** `C:/path/to/geardox.exe`

### STDIO Mode with Claude Code

1. **Add the Geardox MCP server** using the CLI:

   ```bash
   claude mcp add --transport stdio --scope user geardox -- /path/to/geardox --mcp-stdio
   ```

   Everything after `--` is passed to Geardox untouched. `--scope user` makes the server available in all your projects. Use `--scope local` (the default) to add it only to the current project, or `--scope project` to share it through a `.mcp.json` file in the project root:

   ```json
   {
     "mcpServers": {
       "geardox": {
         "command": "/path/to/geardox",
         "args": ["--mcp-stdio"]
       }
     }
   }
   ```

   Claude Code asks for approval the first time it uses a server from `.mcp.json`.

2. **Verify the server was added**:

   ```bash
   claude mcp list
   ```

3. **Start debugging**: Start Claude Code and ask about the program state. Geardox launches when the session starts. Run `/mcp` inside Claude Code to check the server status or reconnect it.

### STDIO Mode with Codex

The Codex CLI, IDE extension, and desktop app share the same MCP configuration, so you only need to add the server once.

1. **Add the Geardox MCP server** using the CLI:

   ```bash
   codex mcp add geardox -- /path/to/geardox --mcp-stdio
   ```

   Or add it to `~/.codex/config.toml`:

   ```toml
   [mcp_servers.geardox]
   command = "/path/to/geardox"
   args = ["--mcp-stdio"]
   ```

   To use the server only in one project, add the same table to `.codex/config.toml` in that project. Codex loads project configuration only for trusted projects. In TOML, write Windows paths with forward slashes or in single quotes.

2. **Verify the server was added**:

   ```bash
   codex mcp list
   ```

3. **Start debugging**: Start Codex and ask about the program state. Geardox launches when the session starts. Type `/mcp` in the Codex CLI to see the active servers.

### HTTP Mode

1. **Start the emulator manually** with HTTP transport:

   ```bash
   ./geardox --mcp-http
   ```

   The default endpoint is `http://127.0.0.1:7777/mcp`.

   To use a custom port:

   ```bash
   ./geardox --mcp-http --mcp-http-port 3000
   ```

   To bind to a custom address, set a bearer token first:

   ```bash
   GEARDOX_MCP_HTTP_TOKEN="change-this-token" ./geardox --mcp-http --mcp-http-address 0.0.0.0 --mcp-http-port 3000
   ```

   Clients must connect using the server's actual interface address, such as `http://192.168.1.50:3000/mcp`, not `0.0.0.0` or a spoofed loopback address.

   You can also start the server using the "MCP Server" item of the "Debug" menu in the GUI, while the debugger is enabled.

2. **Configure bearer-token authentication**:

   Set `GEARDOX_MCP_HTTP_TOKEN` before starting HTTP mode. Authentication is optional for loopback binds and required for wildcard or other non-loopback binds.

   macOS and Linux:

   ```bash
   GEARDOX_MCP_HTTP_TOKEN="change-this-token" ./geardox --mcp-http
   ```

   Windows PowerShell:

   ```powershell
   $env:GEARDOX_MCP_HTTP_TOKEN = "change-this-token"
   .\geardox.exe --mcp-http
   ```

   Windows Command Prompt:

   ```cmd
   set GEARDOX_MCP_HTTP_TOKEN=change-this-token
   geardox.exe --mcp-http
   ```

3. **Configure Claude Code**:

   ```bash
   claude mcp add --transport http --scope user geardox http://127.0.0.1:7777/mcp
   ```

   If the server uses a bearer token, add the header:

   ```bash
   claude mcp add --transport http --scope user geardox http://127.0.0.1:7777/mcp \
     --header "Authorization: Bearer change-this-token"
   ```

   In a shared `.mcp.json`, read the token from the environment instead of committing it:

   ```json
   {
     "mcpServers": {
       "geardox": {
         "type": "http",
         "url": "http://127.0.0.1:7777/mcp",
         "headers": {
           "Authorization": "Bearer ${GEARDOX_MCP_HTTP_TOKEN}"
         }
       }
     }
   }
   ```

4. **Or configure Codex**:

   ```bash
   codex mcp add geardox --url http://127.0.0.1:7777/mcp
   ```

   If the server uses a bearer token, Codex reads it from an environment variable, so set `GEARDOX_MCP_HTTP_TOKEN` in the environment where Codex runs too:

   ```bash
   codex mcp add geardox --url http://127.0.0.1:7777/mcp --bearer-token-env-var GEARDOX_MCP_HTTP_TOKEN
   ```

   The equivalent `~/.codex/config.toml` entry:

   ```toml
   [mcp_servers.geardox]
   url = "http://127.0.0.1:7777/mcp"
   bearer_token_env_var = "GEARDOX_MCP_HTTP_TOKEN"
   ```

5. **Start a new Claude Code or Codex session** and start debugging

> **Note:** The MCP HTTP Server must be running standalone before connecting the AI client.
>
> **Security:** Without `GEARDOX_MCP_HTTP_TOKEN`, HTTP mode starts only on a loopback address. Wildcard and other non-loopback binds are refused. `Host` and browser `Origin` values are matched to the connection's actual destination address to prevent DNS rebinding and address spoofing.

## Usage Examples

Once configured, you can ask your AI assistant:

### Basic Commands

- "What is running, and which machine model, CPU speed and sound card is it using?"
- "Load the game at /path/to/game.gdox"
- "Open the DOS prompt of /path/to/game.gdox and type DIR"
- "Show me the 80386 registers and the current mode"
- "Read 64 bytes at DS:0100"
- "Translate 0010:00402000 to a physical address"
- "Set a breakpoint at CS:2140"
- "Step through the next 5 instructions"
- "Capture a screenshot of the current frame"
- "What does the text screen say?"
- "Show me the VGA mode, the palette and decode the video memory as a 256 color image"
- "Which OPL channels are playing and what notes?"
- "Mute FM channel 3 and the PC speaker so I can hear the digital audio alone"
- "Show me the disc TOC and read the volume descriptor at LBA 16"
- "Insert /path/to/disk.img in drive 0 and list the sectors of cylinder 0"
- "Record a video of the next 600 frames to /path/to/clip.avi"
- "Hold joystick button 1 and push the stick left"
- "Type DIR and Return at the DOS prompt"
- "Press F1, wait 30 frames, then click the OK button"

### Advanced Debugging Workflows

- "Break on the keyboard interrupt (IRQ 1), analyze what the handler does, and bookmark the routines it calls"
- "Find the INT 21h open of the game's data file: break on software INT 21h and show me the file name at DS:DX each time AH is 3Dh"
- "Find where this game keeps the player's lives in RAM using value-change searches, then add a watch on it and freeze it"
- "Break when the game writes to the Sound Blaster DSP write port and show me the code that writes it"
- "Trace the I/O writes to ports 388 and 389 for 2 frames and tell me which OPL registers the music routine sets"
- "The game switches to protected mode through its DOS extender. Stop right after it loads the GDT and decode every descriptor"
- "Follow a floppy read: break on writes to the 82077 data port, inspect DMA channel 2 and the sectors that arrive"
- "Disassemble the routine at 0028:00012345 in 32-bit mode with control flow and port names, and explain it"
- "Trace 2 frames of instructions and I/O, then tell me what the timer interrupt handler does"
- "Profile the game for 10 seconds and list the 10 most expensive functions"

## Available MCP Tools

This is the full tool catalog. All tools are exposed directly by default. With `--mcp-router`, discover advanced tools through `search_tools` or `get_category_tools`, inspect their schemas with `get_tool_info`, then invoke them with `execute_tool`.

The server exposes tools organized in the following categories, which are the categories of the tool router (the name of each is in parentheses):

### Addresses

Every tool that takes an Intel 80386 address accepts:

- `1234ABCD`, `0x1234ABCD` or `$1234ABCD`: a linear address
- `CS:1234`: a segment register (CS, DS, ES, FS, GS, SS) and an offset, using its cached base
- `0008:00001234`: a selector in protected mode (read from the GDT or LDT) or a segment in real and VM86 mode, and an offset

Results report the `linear` address, and `logical` and `physical` addresses where meaningful. Breakpoints in the `physical` and `io` spaces take a plain hex address or port. The memory tools take an `area`, the `id` from `list_memory_areas`, and hex offsets in it, and the LINEAR area also takes `SR:offset` or `SSSS:offset` in `read_memory`, `write_memory`, `memory_search_capture` and `memory_find`; bookmarks, watches and `select_memory_range` take plain hex.

### Execution Control (`execution`)
- `debug_pause` - Pause at the current instruction and enter the debugger. The pause, step, run and frame tools turn the debugger on
- `debug_continue` - Resume execution from a pause or breakpoint
- `debug_step_into` - Step one instruction, entering calls and interrupts. A `REP` string instruction runs all its elements
- `debug_step_over` - Step one instruction, running through `CALL`s, `INT n` handlers and IRQs that come in during the step
- `debug_step_out` - Run until the current subroutine or interrupt handler returns; with an empty call stack it steps one instruction and adds a `note`
- `debug_step_frame` - Run complete frames with breakpoints active, then pause; `frames` sets how many (default 1). By default it answers when they finish or a breakpoint stops them, with the `debug_get_status` fields plus `frames` run, `requested_frames` and `breakpoint_hit`, for up to 1800 frames. `wait: false` answers at once and takes up to 216000 frames; poll `debug_get_status` or stop them with `debug_pause`. A reset, state load or rewind during a wait ends it with an error, and so does powering the emulator off
- `debug_run_to_cursor` - Continue execution until reaching specified address; the stop shows as `run_to` in `debug_get_status`
- `debug_reset` - Reset the PC; optional `boot` (`floppy1`, `floppy2`, `hard_disk`, `cdrom`) picks the boot device for this reset, and fails with `cdrom` when no CD-ROM drive is fitted. Injected joystick axes, buttons, mouse buttons and the keys MCP tools hold are cleared
- `debug_get_status` - Get debug status: `paused`, `at_breakpoint`, `pc` (linear), `logical_pc`, `mode` (`real`, `protected`, `vm86`), `halted`, `breakpoint`, `media_loading`, `media_ready`, `powered_on`, `frame`
- `toggle_fast_forward` - Enable or disable fast forward mode with `enabled`; optional `speed` (0: 1.5x, 1: 2x, 2: 2.5x, 3: 3x, 4: Unlimited)

### Breakpoints (`breakpoints`)
- `set_breakpoint` - Add or update a breakpoint at an address, or over an inclusive range up to `end_address`; `enabled` false keeps it in the list disabled. `type`: `execute` (stops before the instruction; default for linear), `read` (default for physical and io), `write` or `access` (stop after the instruction that made the access); `space`: `linear` (default), `physical` or `io` (IN/OUT/INS/OUTS ports). Execute breakpoints are linear or physical, a physical one follows the code through the current page mapping. Physical read and write breakpoints also see DMA transfers, and linear ones while paging is off. CPU accesses to descriptor tables, the TSS and page tables don't trigger breakpoints
- `remove_breakpoint` - Remove a breakpoint matching address (and `end_address` for ranges), `type` and `space`
- `set_breakpoints_active` - Turn every breakpoint on or off at once with `active`, like Disable All in the debugger; each keeps its own enabled state
- `list_breakpoints` - List all breakpoints with type, space, range, enabled state and `port_name` or `symbol`, and `all_disabled`, plus `interrupt_breakpoints` (with `vector_name`, `vector_description`, source and enabled state) and `irq_breakpoints` (with `irq_name`)
- `set_breakpoint_on_interrupt` - Add or update a break on entry to an interrupt `vector` (0-255), with `source` `any` (default), `exception`, `hardware` or `software`, or on IRQ line `irq` (0-15) when the PIC delivers it, before the handler's first instruction; give `vector` or `irq`, and `source` only with `vector`. An IRQ breakpoint follows the line, not the vector, so it keeps working if the program moves the PIC vector bases (0 timer, 1 keyboard, 4 COM1, 5 or 7 sound card, 6 floppy, 12 PS/2 mouse, 13 FPU, 14 and 15 IDE). `enabled` false keeps it disabled
- `clear_breakpoint_on_interrupt` - Remove an interrupt breakpoint by `vector` and `source`, or an IRQ breakpoint by `irq`

`debug_get_status` reports what stopped execution in `breakpoint`: `kind` (`execute`, `read`, `write`, `access`, `interrupt`, `run_to`, `magic` for the magic breakpoint `XCHG BX,BX`, `program` for a program's break request through the development port), with the space, address and size of the access (`dma` when a DMA transfer made it, `port_name` for an I/O port), or the vector, vector name and source of the interrupt, plus `irq` and `irq_name` for a hardware interrupt and `function` when a software interrupt calls a known BIOS or DOS service.

### Memory (`memory`)
- `list_memory_areas` - List memory areas: LINEAR, PHYSICAL, I/O PORTS, and every memory region the machine has (Main RAM, System BIOS, BIOS Copy, CMOS RAM, VGA Plane 0-3, VGA Window or CGA Memory, CGA VRAM, Linear VRAM on a Cirrus card, No Video Memory on a CGA, Shadow ROMs, Shadow BIOS, Upper Memory, GUS DRAM with the Gravis UltraSound on, and the Media Image, Floppy N Image and IDE Disk N Buffer of what is inserted), with `id`, `name`, `size`, `unit_size`, `flags` (`readable`, `writable`, `executable`, `rom`, `mapped`, `mmio`, `video`, `audio`, `overlay`), `physical_base`, `group` (`address_space`, `memory`, `rom`, `media`, `cpu_window`) and `description`. An area keeps its `id` when disks are inserted or ejected
- `read_memory` - Read `size` bytes (1-65536) at `offset` from a memory area; unmapped or unreadable bytes read as `??`. The LINEAR area takes logical addresses too
- `write_memory` - Write hex `bytes` at `offset` in a memory area; optional `repeat` writes them back to back that many times to fill a range, up to 1 MB in total. ROMs and I/O PORTS are read-only; `write_io_port` writes a port
- `write_io_port` - Write an I/O port as an `OUT` does, between instructions: `port` and `value` in hex, `size` 8 (default), 16 or 32 bits. The device acts on it and the `io.writes` trace logs it; breakpoints don't stop it. Reading the I/O PORTS area doesn't touch devices
- `translate_address` - Translate a logical or linear address: logical, linear, PDE and PTE with their indexes, page flags, physical, bus, region and offset, or the reason it fails
- `select_memory_range` - Select the range from `start_address` to `end_address` (inclusive) in the Memory Workspace
- `get_memory_selection` - Get current memory selection range
- `add_memory_bookmark` - Add bookmark in memory area
- `remove_memory_bookmark` - Remove memory bookmark
- `list_memory_bookmarks` - List all bookmarks in memory area
- `add_memory_watch` - Add watch (tracked memory location), 8, 16, 32 or 64 bits little endian, with optional `notes`; `freeze` holds the value it reads now, written back every frame like a cheat, until `remove_memory_watch`, and turns the debugger on
- `remove_memory_watch` - Remove memory watch
- `list_memory_watches` - List all watches in memory area with their values and whether they are frozen
- `memory_search_capture` - Capture memory snapshot for search comparison; optional `start` and `size` (hex, `size` at most 4000000; both are needed for areas over 64 MB) and `width` (8, 16, 32)
- `memory_search` - Search memory with `operator` (<, >, ==, !=, <=, >=), `compare_type` (`previous`, `value` or `address`, with `compare_value` the value or the area offset) and `data_type` (`unsigned` default, `signed`, `hex`); returns up to 1000 `[address, value, previous]` rows and `total_matches`
- `memory_find` - Find hex byte sequences (`hex_bytes`) or text (`text`, up to 512 bytes, optional `case_sensitive`) in memory; optional `start` and `size`; returns up to 100 offsets and `total_matches`

### CPU (`cpu`)
- `get_i386_status` - Get the complete CPU status: general registers, EIP, EFLAGS bits, CS:EIP with linear and physical PC, CR0/CR2/CR3, mode, CPL, IOPL, code and stack size, the last exception the CPU raised with its error code and return address (interrupts and `INT n` don't replace it), `halted` and `shutdown`, segment descriptor caches, GDTR/IDTR/LDTR/TR and the debug and test registers. On a 486 or Pentium it adds the AC flag and the CR0 bits CD, NW, AM, WP and NE, and on a 486 TR3-TR5. On a Pentium it also adds `cr4`, `tsc` (16 hex digits, high dword first) and `msrs` (an object keyed by MSR index), and `test_registers` is null. `fpu` holds the x87 state when an FPU is fitted (null otherwise): control word with exception masks, precision and rounding, status word with TOP, condition codes and exception flags, tag word, the eight stack registers with raw 80-bit value, decoded value and tag, and the last instruction, opcode and operand pointers
- `write_i386_register` - Write the register `name`: EAX-EDI, EIP, EFLAGS, CR0, CR2, CR3, CR4 (Pentium), DR0-DR7, TR3-TR5 (486), TR6, TR7, CS-GS, LDTR, TR, GDTR_BASE, GDTR_LIMIT, IDTR_BASE, IDTR_LIMIT; `value` is hex. Segment writes load the descriptor like the CPU does and fail on an invalid one; LDTR and TR load their LDT or TSS descriptor from the GDT
- `get_i386_descriptors` - Decode `table` (`gdt`, `ldt`, `idt`) entries from `start`, up to `count` (default 64, max 256): selector, base, limit, type, DPL, flags, or gate targets with symbols; in real mode the IDT is the IVT, VM86 mode still uses the protected-mode gates
- `get_page_directory` - List the present page-directory entries, or with `index` (0-1023) the present pages of that page table, with linear and physical addresses and flags

### Disassembly (`disassembly`)
- `get_disassembly` - Decode Intel 80386 instructions from current memory, without changing the debugger's disassembly or labels: `start_address` and `end_address` (up to 0x10000 bytes) or `count` (up to 1000); `code_size` (`auto`, `16`, `32`); `resolve_symbols`; `detailed` adds `flow` (call, jump, conditional, return, int, iret), `target`, I/O `port` and `port_name` for IN/OUT, `vector`, `vector_name` and `vector_description` for INT, and `function` (the BIOS or DOS service picked by AX) when the INT is the current instruction. A range outside CS is decoded as flat code with the selected size and a `note`
- `get_call_stack` - View the call stack: entries with `kind` (call, interrupt, exception), `source` (software, hardware, exception), `vector`, `vector_name`, `vector_description`, `from`, `to` and `return` addresses (logical and linear) and symbols
- `add_disassembler_bookmark` - Add bookmark in disassembler
- `remove_disassembler_bookmark` - Remove disassembler bookmark
- `list_disassembler_bookmarks` - List all disassembler bookmarks

### Symbols (`symbols`)
- `add_symbol` - Add or rename a user symbol at an address; user symbols take precedence over automatic labels
- `remove_symbol` - Remove the user symbol at an address
- `load_symbols` - Load user symbols from a file: `ADDRESS NAME`, `NAME = ADDRESS` (or `EQU`) or GNU `nm` output (`ADDRESS TYPE NAME`) per line, `;` or `#` comments, linear hex or `SSSS:OOOOOOOO` addresses; GNU ld and NASM map files load too; with `program` the file belongs to the running program: `SSSS:OOOO` addresses are relative to its load segment (Borland TLINK, Microsoft LINK and Watcom WLINK maps load too), plain ones to its base, and they go when it exits
- `list_symbols` - List user symbols and automatic labels a page at a time: optional `filter` (case-insensitive part of the name), `start` and `count` (default 200, max 1000), with `total` and `has_more`. With `name` it returns every symbol with that exact name instead, and with `address` the symbol at that address

### System Hardware (`hardware_system`)
- `get_pic_status` - Get both 8259A PICs: per-IRQ source, input level, request, in-service, mask and vector; ICW1-ICW4 decoded, init state and modes
- `get_pit_status` - Get the 8254: the three counters with gate, mode, reload, count, OUT, null count and period, and the speaker and refresh toggle bits of port 0061
- `get_dma_status` - Get the two 8237 DMA controllers: command, status, mask, requests and flip-flop per chip, and the eight channels' device, mode, page, physical address, base and current registers and counts
- `get_system_status` - Get the machine (model, CPU, clock, effective clock and Turbo, RAM, FPU, video card, floppy drives) and where each setting came from (`user`, `game`, `model` or `media`) with warnings, the board state (A20, port 0092 and 0061, NMI mask, POST code, coprocessor IRQ 13 and CPU resets with the last reset cause, memory map) and the built-in BIOS (images with CRCs, firmware config and debug console)
- `set_turbo` - Press the board's Turbo button with `turbo`: false runs the CPU at a third of its clock, at once and without a reset. Returns the effective clock
- `get_rtc_status` - Get the MC146818 clock: date, time, modes, alarm, periodic rate, interrupt enables and flags, and the registers
- `get_keyboard_status` - Get the 8042 keyboard controller and the MF2 keyboard: status, command byte, input and output ports, output buffer and its source, A20 sources, scan code set, translation, LEDs, typematic, queued bytes and held keys
- `get_serial_status` - Get both 16550A serial ports (COM1 at 03F8 IRQ 4, COM2 at 02F8 IRQ 3): what is attached, divisor and baud, IER, IIR, FCR, LCR, MCR, LSR, MSR and scratch registers with their bits decoded, FIFO counts and the IRQ line
- `get_parallel_status` - Get LPT1 (0378 IRQ 7, nothing attached): data latch, status and control, with the bits decoded
- `get_bios_log` - Get what the BIOS wrote to the debug console at port 0402: SeaBIOS's POST messages since power on; `max_bytes` (1-65536) returns the last bytes only

### Video Hardware (`hardware_video`)
- `get_crtc_status` - Get the CRTC: mode, timing, frame size, beam position, scanout counters, cursor and vertical interrupt. With a CGA it reads the 6845, which has no vertical interrupt
- `get_crtc_registers` - Get the 25 CRTC registers with their names, the index and the CR00-CR07 write protection. With a CGA, the 6845's 18 registers
- `write_crtc_register` - Write a CRTC register (`register` 0-24, CR00-CR18, or R0-R17 with a CGA) with an 8-bit hex `value` like the CPU does, so the write protection, timing and trace logger see it
- `get_video_output_status` - Get the VGA paths around the CRTC: memory window, chain-4, odd/even, direct writes, write pipeline, attribute controller and DAC state. It names the video card and, with a Cirrus card, adds the extended mode, pixel format, video clocks, banks, linear aperture, pitch and start; an `ega` object adds the monitor's lines and colors, the switches and the status mux, and a `cga` object the mode and color registers and the output
- `get_vga_registers` - Get the miscellaneous, feature, sequencer, graphics, CRTC and attribute registers as hex arrays. With a Cirrus card a `cirrus` object adds the lock, SR00-SR1F, GR00-GR3F, CR19-CR27 and the hidden DAC, a `blt` object the BitBLT engine (state, mode, ROP, size, addresses, pitches, bytes fed and needed, blits started and per frame by kind) and a `cursor` object the hardware cursor (enabled, size, position, pattern address, colors). With a CGA, `crtc6845` and the mode and color registers instead
- `get_palettes` - Get the palettes with RGB888 colors, with `palette` `attribute` (the 16 attribute colors as they reach the screen), `dac` (the 256 entries with their 6-bit components) or `all` (default). An EGA has the monitor's 64 colors instead of a DAC; a CGA its 16 RGB colors
- `get_frame_buffer` - Decode the whole video memory as PNG from CRTC address 0, to find off-screen graphics: `buffer` `plane0`-`plane3`, `planar`, `256`, `text` or `linear`, with optional `pitch`. A CGA's 16 KB decode in its current mode whatever the buffer. With a Cirrus card, `linear` decodes byte by byte from `start` in 8, 15 (5:5:5), 16 (5:6:5) or 24 (8:8:8) bits per pixel (`format`); format, pitch and start default to the current mode
- `get_text_screen` - Read a text mode screen as characters per row, with the cursor position and optionally `attributes` as one hex string per row. Use it instead of a screenshot to know what a DOS screen says; it fails when the VGA is not in a text mode
- `get_voodoo_status` - Get the 3dfx Voodoo Graphics (the card must be on): PCI window, status register, FIFO depth and busy time, pending swaps and the front and back buffers, video timing and refresh, the graphics and video clocks, the frame buffer layout, the init and video registers, the pixel pipeline registers, the texture unit's registers, the five statistics counters and whether the render thread is on
- `get_voodoo_buffer` - Image of a 3dfx Voodoo buffer as PNG: `buffer` `front` (what the monitor shows, the default), `back`, or `aux` (the depth or alpha buffer as grey levels)

### Audio Hardware (`hardware_audio`)
- `get_sound_status` - Get the sound card: the OPL chips with status, timers and channels, the Sound Blaster DSP (version, IRQ, DMA, speaker, sample rate, IRQ status, filter, MIDI UART and the transfer in progress with its format, auto-init, high-speed, stereo, block and remaining bytes), the mixer levels of `voice`, `fm` and `cd` as left and right gains from 0 to 1, the MPU-401 (mode, UART, pending bytes, standalone or on the card, IRQ, and in intelligent mode its sequencer state: playing, recording, tempo, timebase, tracks and conductor), the Gravis UltraSound when it's on (`gus`: running, output, active voices, frame rate, the reset, mix control, IRQ status and latch registers, the IRQ and DMA channels the latches select, the 8Fh voice IRQ latch, DRAM DMA and sampling control, both timers and the MIDI port), and MIDI out: capture mode, messages sent, dropped bytes, the SysEx log, the kind of music (`synth`: `gm` or `mt32`; `synth_detected` is true when it came from what the game sent, not from a setting), the output in use (`port`) and the one wanted (`wanted_port`), and the built-in synth (`midi_synth`: `destination` `builtin`, `external` or `none`, `engine` `mt32` or `soundfont`, the MT-32 ROM set or sound bank, active and maximum voices, the last frame's render time, messages dropped and the debugger's mute), and the volume settings (`levels`: for `master`, `fm`, `pcm`, `cdda`, `buzzer`, `gus`, `mt32` and `gm`, the volume in percent and, except `master`, whether it's muted, each with where it comes from: `user`, `descriptor`, `database` or `change`)
- `get_opl_status` - Get the OPL FM chips (YM3812 OPL2 or YMF262 OPL3): status, address latch, rhythm, note select, mode flags, both timers with their period, and each channel's frequency, note, key, type (2op, 4op, 4op_pair, drum), connection, feedback, pan, output and mute with both operators' parameters, envelope state and level; optional `chip` (0, or 1 for the Sound Blaster Pro's right OPL2) and `channel` (1-9 on an OPL2, 1-18 on the OPL3)
- `get_opl_registers` - Get one OPL register bank as last written with register names, and the address latch; `chip` (0, or 1 for the Sound Blaster Pro's right OPL2) and `bank` (0 for 000-0FF, channels 1-9; 1 for 100-1FF, channels 10-18, OPL3 only)
- `get_gus_voices` - Get the Gravis UltraSound's voices (the card must be on): the active voices, the frame rate and, for each voice, its current, start and end address in sample memory (hex, with the fraction in 1/512ths), `frequency` and `rate_hz`, `volume`, `volume_level` and `volume_db` (null when silent), `pan`, `output`, and the `wave` and `ramp` controls with stopped, loop, bidirectional, IRQ enabled and pending, direction and, for the wave, 8 or 16 bits. `voice` (0-31) reads one voice, `all` true lists all 32, and the default is the active voices
- `set_audio_mute` - Mute (`mute` true) or unmute `source` (`fm`, `pcm` for the Sound Blaster DSP's digital audio, `cdda`, `buzzer` for the PC speaker, `gus` for the Gravis UltraSound, `midi` for the built-in synths) or, for `fm`, one `channel` of it (1-18 on the OPL3, 1-9 on an OPL2; on the Sound Blaster Pro 1-9 the left OPL2 and 10-18 the right) in the debugger; emulated state is unchanged

### CD-ROM Hardware (`hardware_cdrom`)
- `get_cdrom_status` - Get the ATAPI CD-ROM drive (fitted, position, speed, tray, disc, ready or spinning up, motor, unit attention, prevent, head, read-ahead window, last packet, sense, audio status, page 0Eh channels and volumes), the disc, `audio` with CD audio playback and `playlist` with the discs of the loaded `.m3u` (`index`, current disc) and whether the drive is empty
- `list_cdrom_tracks` - Get the disc TOC: track type, absolute MSF and LBA ranges, length, pregap, and the track under the head
- `read_cdrom_sector` - Read one sector of a CD image as hex by `lba` (from the program area start, MSF minus 00:02:00); `mode` `user` (2048 bytes, default) or `raw` (2352). Does not move the drive head. Not available for physical CD drives

### Floppy Hardware (`hardware_floppy`)
- `get_fdc_status` - Get the 82077: DOR, MSR, the change line, data rate, phase, command and result bytes, PCN per drive, the Sense Interrupt Status queue, Specify and Configure values, and the INT and DRQ lines
- `list_floppy_drives` - Get both drives: image, media, geometry, RPM, write protect, modified, head cylinder, motor, ready and selected
- `read_floppy_sector` - Read the first sector with ID R (`sector`) on a track by `drive` (0 or 1), `cylinder`, `head`: IDs, density, deleted mark, status and data as hex. Without `sector` it lists the track's sectors in disk order

### Hard Disk Hardware (`hardware_hard_disk`)
- `get_ide_status` - Get both IDE channels: task file registers, device control, command, phase, transfer position, and per device status, error, pending interrupt, geometry, translation, head position and read-ahead; the CD-ROM's slot shows its ATAPI command and packet
- `list_hard_disks` - Get hard disk slots 0-3 (primary master, primary slave, secondary master, secondary slave): image, size, geometry, BIOS translation, write protect, blocks the machine wrote that are not in the image file yet, where the disk came from (`game`, `msdos`, `freedos`, `image`, `folder` or `zip`), and `at_next_reset` when a change waits for a reset
- `read_hard_disk_sector` - Read the 512-byte sector `lba` of the disk in slot `id` as hex, as the machine sees it, unwritten changes included, without bus traffic

### Media (`media`)
- `load_media` - Load a game descriptor, a CD image, a floppy image or a hard disk image from `file_path`. A `.gdox` descriptor, or a folder or ZIP with one in its root, opens its game: C: made from DOS and the game's folder, read in place, and the machine powered on, with `target` choosing what runs at boot (`game` by default, the program the game's choice names; `voodoo` for its 3dfx program with the 3dfx Voodoo fitted, for that session; `setup`, `prompt`, or `debug` to run the game's program through GDXDBG for the program tools); the answer is `get_game_info`'s. A `.exe`, `.com` or `.bat`, or a folder or ZIP the game database recognises, opens as a game too, and with several games in a folder or ZIP the first by name opens; any other folder or ZIP opens as a game at the DOS prompt in its folder, named after it. A CD is `.cue`, `.chd`, `.iso`; a floppy (`.img`, `.ima`, `.vfd`, `.flp`, `.dsk`, `.d77`, `.d88`, `.hdm`, `.xdf`, `.mfm`, an `.m3u` playlist or a `.zip`) goes in drive 0; a hard disk image (`.hdd`, `.h0`-`.h3`, `.img` or `.hdm` over 8 MB, a fixed `.vhd`, a hard disk `.chd`) attaches to its slot or the first free one and powers on an empty machine. It answers when the load has finished
- `eject_cdrom` - Open the CD-ROM tray and eject the CD, even when software prevented removal; the machine keeps running and `load_media` inserts another
- `close_cdrom_tray` - Close the CD-ROM drive's tray, as the drive's own button does. With a disc inside it spins up and reports a disc change; the machine keeps running
- `insert_playlist_disc` - Insert playlist disc `index` (from the `get_cdrom_status` playlist) into the empty drive, keeping the machine running; `eject_cdrom` first
- `get_media_info` - Get loaded media info, the built-in BIOS images, the floppy drives (image, disk name, media, write protect, modified) and the hard disks (as `list_hard_disks`)
- `list_recent_media` - List the most recent CD images, floppies and hard disks opened by Geardox, with `file_path` values for `load_media`
- `insert_floppy` - Insert a floppy image (`.img`, `.ima`, `.vfd`, `.flp`, `.dsk`, `.d77`, `.d88`, `.hdm`, `.xdf`, `.mfm`, an `.m3u` list or a `.zip`) in `drive` (0 or 1); optional `write_protected`. The previous disk's changes are saved first
- `create_blank_floppy` - Create a blank floppy image file at `file_path` (`.d77`, `.img` or `.ima`) and insert it in `drive`, like New Blank Disk; `type` (`360kb`, `720kb`, `1200kb`, `1440kb` default, `2880kb`), `formatted` true (default) for an empty FAT12 disk or false for an unformatted one (`.d77` only). An existing file is never replaced
- `eject_floppy` - Eject the disk in `drive`, saving its changes first
- `swap_floppies` - Swap the disks in drives 0 and 1
- `set_floppy_write_protect` - Set the write protect tab of the disk in `drive` with `write_protected`
- `attach_hard_disk` - Attach a raw image of 512-byte sectors (`.hdd`, `.h0`-`.h3`, `.img` over 8 MB, a fixed `.vhd`, a hard disk `.chd`), a folder or a `.zip` to slot `id` (0-3); a folder or ZIP becomes a FAT16 disk read in place, DOS's changes written back into the folder when it's detached or Geardox quits, a ZIP's or a read-only folder's into its own save folder, and the result has `left_out`. Optional `write_protected`, by default the slot's Write Protected setting. A running machine takes it at the next reset, one that is off at once. `create_size_mb` (1-2047) first creates an image at `file_path`, never replacing an existing file: an empty FAT16 disk (33 MB or more), unless `formatted` is false for a blank disk that needs FDISK and FORMAT
- `detach_hard_disk` - Detach the disk in slot `id` after writing its changes; on a running machine at the next reset
- `flush_hard_disks` - Write the changes to the image files now; otherwise they are written about 2 seconds after the machine stops writing
- `save_hard_disk` - Save the disk in slot `id`, as it is now and with its unwritten changes, to a new raw image at `file_path` (`.hdd` or `.img`), never replacing an existing file or writing inside a folder that is a disk: a game's live C:, a folder, a ZIP or any image. The disk in use stays the same. `descriptor` (with `id` 0 and a game open) also saves a `.gdox` next to the image that plays it with the game's settings, never replacing one: the result has `descriptor_path`, or `descriptor_error` with the reason. The result has `blocks`, and `warning` when DOS was in the middle of writing and the image's files don't add up
- `get_game_info` - Describe the open game: its `name`, the descriptor and its path, the overrides file (`settings_path`), each key set and its layer (`descriptor` or `override`), the machine in use (model, CPU clock, RAM and FPU, each with where the value came from: `user`, `game`, `model` or `media`; then the Turbo button, sound card, IRQ, DMA, MPU-401, Gravis UltraSound, video, mouse, joystick, MIDI music kind, MIDI output, MIDI port and MT-32 model), the target (`game`, `setup` or `prompt`), the `program` (`runs` and `chosen`, `game` or `voodoo`; `voodoo_program`, whether the game has a 3dfx program; `needs_driver`, whether that needs the imported 3dfx driver), the keyboard layout, the DOS it boots (FreeDOS or the user's imported MS-DOS) and its layer, the `source` folder, the game's own `save_folder`, whether the last session's changes hadn't been written and were `recovered` or `refused` (`kept` when the disk goes on as it was left, otherwise in `Disk/Changes.refused`), `left_out`, where saves go (`saves_to`: `game_folder`, `save_folder`, or `image` for a game whose C: is a hard disk image, with the `saves` path), `state_mismatch` (a loaded state didn't match one of the disks, so the rest of the session goes to `Conflicts` in the save folder), and the last write-back (`done`, `refused`, `kept`, `written`, `deleted`, `folders`, `conflicts`, `failed` and `notes`). `open` is false with no game

### Capture (`capture`)
- `get_screenshot` - Capture current screen frame as base64 PNG with its `width` and `height`. Right after a state load it fails until a frame runs when the state has no saved frame or the screen size changed
- `start_video_recording` - Start recording video and audio to an AVI file (MJPEG or uncompressed video, PCM audio). Only the resulting `file_path` is returned; the video stays on disk. Optional `file_path` (absolute `.avi`; if omitted, an automatic name in the configured recordings directory), `scale` (1-20), `aspect_ratio` (`screen`, `square`, `4:3`, `16:9`, `16:10`), and `quality` (`low`, `medium`, `high`, `lossless`, `lossless_compressed`). `low` and `medium` halve the color resolution; `lossless` is uncompressed; `lossless_compressed` stores PNG frames, much smaller but playable only with FFmpeg based players. Given options update the recording settings, same as the GUI menu. Frames are recorded only while the emulator runs, so continue or step execution before stopping
- `stop_video_recording` - Stop the active recording and finalize the AVI file. Returns `file_path` and the number of recorded `frames`

### Save States (`state`)
- `list_save_state_slots` - List all 5 save state slots with information (media name, timestamp, validity, whether they have a screenshot), the empty slots and the current slot
- `save_state` - Save emulator state to the selected slot, to `slot` (1-5, which becomes the selected one) or to an absolute `file_path`
- `load_state` - Load emulator state from the selected slot, from `slot` (1-5, which becomes the selected one) or from an absolute `file_path`. It puts the saved frame back on screen without running the machine; `frame_restored` is false when the state has no frame or the screen size changed. Hard disk writes made since the state are undone; `hard_disks_changed` lists the slots where that wasn't possible

### Rewind (`rewind`)
- `get_rewind_status` - Get rewind buffer status: `enabled`, `debug_mode`, `snapshot_count`, `frames_per_snapshot` (1 in debug mode, which takes a snapshot every frame and every debugger stop), `recorded_seconds`, and `memory_usage` and `memory_budget` in bytes
- `rewind_seek` - Seek to a specific rewind snapshot (`snapshot` 1 is the oldest, `snapshot_count` the newest) while paused; returns how many seconds before the newest it was taken (`age_seconds`) and the new `cs`, `eip` and linear PC

### Input (`input`)
- `joystick_set` - Drive the emulated game port joystick, which the host gamepad also drives. `ax`, `ay`, `bx`, `by` are axis positions from -32768 to 32767 (0 is centre, `ay` negative is up, `bx` and `by` only on the devices that wire them, `by` is the flightstick's throttle) that replace the host's axes until `reset` or `debug_reset`; `buttons` (1-4; the joystick has 1-2, the gamepad and the flightstick 1-4) and `hat` (`none`, `up`, `down`, `left`, `right` and the four diagonals, on the flightstick) add to the host's, with `action` `press` (default), `release` or `press_and_release`, which releases after some frames. `device` plugs in another joystick, taking effect at once: `none`, `standard` (IBM, 2 buttons), `gamepad` (Gravis, 4 buttons, nine positions) or `flightstick` (CH FlightStick Pro). `reset` clears everything injected first, giving the host's input back
- `get_input_state` - Get the mouse (`type`, host pending counts and buttons, injected buttons, the PS/2 mouse's mode, reporting, rate, resolution, scaling and queued bytes, or the serial mouse's port, RTS and queue) and the joystick (`type`, axes, buttons, hat, injected state, the game port byte at 0201 and each axis pulse in microseconds or `open`), and the pending tap releases
- `keyboard_key` - Press, release or tap a key of the PC keyboard by name (RETURN, SPACE, A, 1, F1, LEFT SHIFT, LEFT CTRL, ESCAPE, UP, KP ENTER...; ENTER and ESC work too, CTRL, ALT and SHIFT are the left ones). `press_and_release` releases after 10 frames. `modifiers`, up to 4 key names, makes a combination: `{"key":"RETURN","action":"press_and_release","modifiers":["ALT"]}` is Alt+Enter and `["CTRL","ALT"]` with DELETE is Ctrl+Alt+Del. They are pressed in order before the key and released in reverse after it, and a `release` releases them too. A press answers `dropped`, true with a `reason` when the keyboard didn't take it: it isn't scanning, or its buffer is full and it sent an overrun. A pressed key stays held until it is released or the server stops
- `keyboard_type` - Type ASCII `text` (1 to 256 characters) on the US keyboard layout, with Shift where needed and a newline as RETURN, for DOS prompts; optional `frames_per_key` (1-60, default 3) is how many frames each key is held and how many it waits after. It runs emulated frames, kept in rewind like any input, and answers with the number of `characters`, `frames_waited` and `dropped` (the key presses the keyboard didn't take) when the last one is typed. It fails when the emulator is powered off, when the GUI is typing pasted text, or on a character the keyboard can't type. A breakpoint, a pause from the GUI or a reset stops it and releases its keys
- `controller_macro` - Run a frame-timed input script on the PC keyboard (key names as `keyboard_key`), the mouse buttons (`left`, `right`, `middle`) and the joystick (buttons `1`-`4`, `up`, `down`, `left`, `right` on stick A, `hat_up`, `hat_down`, `hat_left`, `hat_right`). `commands` is an ordered array of objects with exactly one of `tap`, `press`, `release` (a key, button or direction name) or `wait` (1-1000 frames), and optionally `device`; `device` is the default device of the commands (`keyboard` by default, `mouse` or `joystick`) and `tap_frames` (1-60, default 3) is how long a tap holds and the gap after it. For example `[{"press":"LEFT CTRL"},{"tap":"F5"},{"release":"LEFT CTRL"},{"wait":30},{"tap":"left","device":"mouse"}]`. The mouse and joystick commands fail when the machine has no mouse or no joystick is plugged in. It runs emulated frames, kept in rewind, and answers with `commands`, `steps`, `frames_waited`, `held`, the number of keys, buttons and directions a `press` left held at the end, and `dropped`, the key presses the keyboard didn't take. A breakpoint, a pause from the GUI, a reset, state load or rewind ends it with an error and releases what it holds
- `mouse_get_cursor` - Read where the last `mouse_move` or `mouse_click` measured the pointer: position, box, pixels per mouse count, how many frames ago it was measured, and whether the current frame still draws it there. `estimated: true` marks a position moved by counts or with a button held since the last measurement. Not known (`known: false`) before the first move, and after a reset, state load, rewind, screen size change or failed mouse tool. Read-only, it never runs frames
- `mouse_move` - Move the mouse pointer to screen pixel `x`, `y` (the coordinates of `get_screenshot`; the top-left of the pointer image lands there), or by `dx`, `dy` mouse counts (-4096 to 4096). A screen move finds the pointer by moving it 32 counts out and back and comparing frames, which also keeps its image (up to 48x48); after that it finds the pointer by that image wherever all of it is drawn, once, with no move out and back, and measures the pointer's speed again after every move. It moves about 4 pixels per frame (4 counts per frame for a pointer near a pixel per count, up to 64 for software that turns many counts into a pixel) and checks again until it is there, up to 16 corrections, stopping after 3 in a row that get no closer. Both axes share the steps, so diagonal moves are straight lines. A `dx`, `dy` move uses the same pace once a screen move has measured the pointer, and 4 counts per frame before. Measurements that don't look like a pointer are retried up to 6 times with a larger (doubling up to 1024 counts) or flipped out-and-back move, after 30 frames when the pointer wasn't seen moving at all (software that hides it while busy), and the error says why the last one failed. It fails when the software isn't reading the mouse for 60 frames, and when the pointer stops short of the target. With a button held by `mouse_click` `press`, a screen move goes straight from the last known position instead, since moving out and back would drag; the result has `estimated: true`. It runs emulated frames, kept in rewind like any input; a breakpoint, pause from the GUI, reset, state load or rewind stops it. It fails when the machine has no mouse (set Mouse in the machine configuration)
- `mouse_click` - Click the `button` `left` (default), `right` or `middle` with `action` `click` (default), `double_click`, `press`, `release` or `drag`, at screen pixel `x`, `y` when given (moved there like `mouse_move`) or at the current position. `press_frames` and `release_frames` set how long the button stays down and up (1-120, default 4). `drag` moves to `x`, `y` with the button up, presses, moves straight to `to_x`, `to_y` and releases, also when something fails, returning `from` (measured) and `to` (estimated); `press`, `mouse_move` and `release` make the same drag over several calls. A pointer that snaps to a grid and can't land on the exact pixel still clicks when it covers the target
- `get_key_map` - Get the key map of the running content: `enabled` (key mapping on), `mapper_open`, the per-game `.keys` `file`, the `game` bindings of the descriptor's `[keys]` and the `user` bindings, `remapped` host keys with what they drive now, and the `unreachable` PC keys no host key reaches
- `set_key_binding` - Bind a `host` key to `keys` for this game and save its `.keys` file, as the keyboard mapper does: one PC key, up to 4 joined with `+`, `none`, or `default` to drop the user's binding
- `set_key_mapping` - Turn the game's key mapping on or off with `enabled`; off, every host key types its built-in key
- `host_key` - Press, release or tap a host key through the key map, as the host keyboard would; while the keyboard mapper is open it goes to the mapper instead. Host keys are USB HID names (`a`, `up`, `kp_8`, `left_ctrl`, `f13`...); `press_and_release` releases after 10 frames. The answer has `drives`, the PC keys it presses now
- `open_key_mapper` - Open the full screen keyboard mapper; the machine stops until it closes, and `get_screenshot` shows it
- `close_key_mapper` - Close the keyboard mapper, saving the game's bindings

### Trace Logger (`trace`)
- `get_trace_log` - Read trace logger entries (CPU + hardware events) using absolute sequence pagination, with `enabled` and `stopped_by` when something stopped it. Optional `types` (filter groups, plus `mark`), `contains` (text), `pc_range` (linear hex `start-end` of the code behind each entry) and `compact` (CPU lines without registers, segments, flags, bytes and linear address). Use `set_trace_log` to start or stop the logger
- `set_trace_log` - Start or stop trace logging and configure exact event filters, capture filters (`code`, `ports`, `interrupts`), `keep_across_resets` and memory or disk storage

#### Trace pagination

`get_trace_log` returns:

- `total_entries`: entries currently retained in the memory ring.
- `total_logged`: the next absolute sequence number. It is monotonic for the process lifetime and is not reset by clear, resize, media changes, or resets.
- `oldest_sequence`: absolute sequence of the oldest retained entry (`total_logged - total_entries`).
- `start`: actual absolute sequence used for this page.
- `next_sequence`: absolute sequence to pass as the next `start`.
- `count`: number of returned entries.
- `overrun`: `true` when a requested `start` had expired and was clamped to `oldest_sequence`.
- `overwritten`: entries the memory ring dropped since it was cleared.
- `lines`: formatted trace entries in sequence order, each starting with the machine clock and the signed clocks since the previous entry (`@000000012345 +42`). An event stamped at its own time (a timer edge found during a catch-up, a palette load, an interrupt inside a batch) can come before the line above it, with a negative delta. Instruction lines carry the linear address, CS:EIP, general and segment registers, EFLAGS, Intel syntax and bytes.
- `scanned` and `sequences`: with `types`, `contains` or `pc_range`, up to 500000 entries are scanned from `start`; `sequences` gives the sequence of each returned line and `next_sequence` where to go on.

When `start` is omitted, the latest 100 retained entries are returned. A negative `start` requests that many entries from the retained tail. Positive starts are absolute sequences. `count` defaults to 100 and is capped at 1000. A requested sequence older than `oldest_sequence` starts at the oldest retained entry and sets `overrun` to `true`. A sequence at or beyond `total_logged` returns an empty page with `start` and `next_sequence` equal to the requested value and `overrun` set to `false`.

Memory-mode counters shown in the GUI and written by manual export use the same absolute sequence values as MCP. Disk trace files use a separate zero-based entry counter local to each file.

#### Trace filters

Omitting `filters` enables the default of CPU instructions, IRQs and exceptions. When supplied, `filters` must be non-empty and unique. Each entry is one of these streams, or a group name, the part before the dot (`cpu`, `io`, `fdc`...), which enables all of its streams:

```text
cpu.instructions      cpu.irqs              cpu.exceptions        cpu.software_ints
cpu.modes             cpu.returns           cpu.msrs
io.reads              io.writes             io.polls              io.denied
pic.requests          pic.mask              pic.commands          pic.init
pic.spurious          pic.line_low
timer.timeouts        timer.counters        timer.speaker         timer.reads
dma.registers         dma.requests          dma.ends              dma.unsupported
video.mode            video.crtc            video.output          video.datapath
video.dac             video.palette_load    video.palette         video.vsync
video.frame           video.status          video.missed_vblank   video.blt
video.blt_registers
fm.key                fm.frequency          fm.operators          fm.channels
fm.global             fm.timers
pcm.commands          pcm.transfers         pcm.irqs              pcm.resets
pcm.direct
mixer.volume          mixer.control         mixer.reads
midi.out              midi.data             midi.commands         midi.messages
cdrom.packets         cdrom.status          cdrom.data            cdrom.media
cdrom.cdda            cdrom.resets          cdrom.polls
fdc.commands          fdc.results           fdc.drives            fdc.sectors
ide.commands          ide.results           ide.resets
keyboard.keys         keyboard.reads        keyboard.commands     keyboard.bytes
keyboard.empty_reads
input.changes         input.commands        input.packets         input.fires
serial.writes         serial.data           serial.control        serial.reads
parallel.data         parallel.control      parallel.strobes
system.reset          system.a20            system.control        system.post
system.rtc            system.rtc_reads      system.pci            system.console
gus.registers         gus.irqs              gus.dma               gus.latches
voodoo.commands       voodoo.culled         voodoo.overflows      voodoo.lfb_reads
```

`cpu.instructions` is the only stream that runs the machine one instruction at a time. Every other stream records while the frame runs batched with the dynarec, with times as exact as the stepped run: I/O and the events it causes are stamped at the access, scheduler events at their time, interrupt and exception entries with the CPU's time inside the batch. `io.polls` writes one `POLL` line per collapsed polling loop (port, value, count, cycles) instead of one line per read.

Hardware events are decoded as the guest sees them: register names per adapter (VGA, EGA, Cirrus, 6845), the value the chip kept next to the byte written when they differ, IRQ lines and vectors with the line an EOI cleared, DMA blocks with base, length and auto-init, ATAPI packets decoded by command, FDC commands and results per command, IDE commands per command with the DOS file a sector belongs to on a FAT disk, keyboard bytes with the scan set, mouse packets, MIDI messages as the synth gets them, DSP commands by name. Video register writes carry the beam line and dot. `[MARK]` lines mark frames, debugger stops and continues, state loads, resets, power and media changes and the debugger turning off.

Capture filters cut what is recorded, not only what is shown. `code` takes up to 4 linear hex ranges of the instructions recorded, like `F0000-FFFFF`, and up to 4 excluded ones with `!`. `ports` takes up to 4 port ranges and 4 excluded ones (`388-389 !3DA`), with `:r` or `:w` for one direction. `interrupts` takes up to 8 software INT vectors, each with `:AH` (two hex digits) or `:AX` (four), like `21:3D 31 2F:1687`. Giving any of the three replaces all three; empty strings record everything. They persist in the configuration and the Trace Logger window's Capture menu shows them.

`video.missed_vblank` detects frames where the game missed VBlank. It watches one linear address, set with `vblank_watch_address` (hex, e.g. `1234ABCD`, `0x1234ABCD`, or `$1234ABCD`), for the access set with `vblank_watch_operation` (`read`, `write`, or `read_write`). CPU data accesses count, instruction fetches do not. At each VSYNC the frame that just ended is checked, and if the watched access did not happen during it, a `VBLANK MISSED` entry is logged with the watched address, operation, and the number of consecutive missed frames. The first VSYNC after starting the logger, changing the watch, resetting, or loading a state only arms the check. Both parameters persist in the configuration and keep their current values when omitted; the response echoes them when `video.missed_vblank` is active.

The trace logger records while the debugger runs the machine, so `set_trace_log` with `enabled` true turns the debugger on, opens the Trace Logger window and starts recording. Memory output allocates its buffer when recording starts. Entry times are machine clocks (the 28.636 MHz timebase, twice the 14.318 MHz master clock, not CPU cycles), and an event inside an instruction carries the instruction's start time. Each entry is 96 bytes.

#### Trace storage

`output` is `memory` or `disk`; starting a stopped logger without it selects `memory`. `memory_size`, the entries kept in memory, is `100K`, `500K`, `1M`, `2M` or `5M`; the persisted default is `100K`. `disk_size`, the disk file's limit, is `10MB`, `50MB`, `100MB`, `250MB`, `500MB`, `1GB` or `unbounded`; the persisted default is `100MB`.

Storage changes while tracing is active cleanly stop and restart the logger. Repeating the active storage configuration is idempotent and only updates filters. Stopping preserves retained memory entries; changing memory capacity or starting disk output resets the ring used by that recording. Resets, power cycles and media changes write a `[MARK]` line and recording goes on; with `keep_across_resets` false they stop the logger and `stopped_by` says which. Disk output stops when its staging buffer overflows and the window shows how many entries were lost.

`output_path` is a directory only, not a filename. Geardox creates a unique timestamped trace filename in that directory. When omitted, the configured default, media, or custom directory policy remains in effect.

### Profiler (`profiler`)
- `set_profiler` - Start, stop, or reset the function profiler with `action` (`start`, `stop`, `reset`). `start` turns the debugger on and opens the Profiler debugger window and `stop` closes it. Statistics are only collected while the window is visible (in headless mode, while started) and the debugger runs the machine
- `get_profiler_data` - Read profiler results: `collecting`, `window_open`, `total_cycles`, `frames`, `function_count`, and per-function `name`, `symbol` (`manual`, `auto` or `none`), `address`, `vector`, `vector_name` and `vector_description` (interrupt handlers), `type` (`call`, `interrupt`, or `root` for code running outside any call), `calls`, `calls_per_frame`, `inclusive_cycles`, `inclusive_percent`, `exclusive_cycles`, `exclusive_percent`, `average_cycles`, `min_cycles`, and `max_cycles`. Average, min and max count completed outermost calls, so a recursive function's inner returns don't skew them. Optional `sort` (`inclusive`, `exclusive`, `calls`, `average`, `max`; highest first), `count` (default 50, max 1000), and `filter` (name or hex address substring)

### Program (`program`)
- `run_program` - Rebuild the open game's disk from its folder, boot, and run a program through GDXDBG, which reports where DOS loaded it, its console output and its exit code: `command` (program and arguments, relative to the game's folder; default the descriptor's `exe` and `args`) and `stop_at_entry`, which stops at the program's first instruction, or for a DJGPP program with `gdxdbg.c` when it reports its base. Returns at once; poll `get_program_status`
- `relaunch_game` - Rebuild the disk and run the same program again, keeping the breakpoints, after a host rebuild
- `get_program_status` - `state` (`none`, `running`, `exited`), `runs`, `path`, `host_path`, `psp`, `entry`, `stack`, `load_segment`, `base` and `base_reported`, `code_size`, `coff`, `symbol_file` and `symbols`, and once it exits `exit_code` and `termination` (`normal`, `ctrl_c`, `critical_error`, `resident`, `not_loaded`)
- `get_program_output` - Read the program console (DOS console output mirrored by GDXDBG, port E9h and `gdx_printf`): `since` (a character count, the previous answer's `next`) and `max_bytes`; answers `text`, `next`, `written` and `has_more`
- `get_dos_status` - Read DOS without calling it, once GDXDBG has run: `dos_version`, `indos`, `list_of_lists`, `memory_blocks` (MCB segment, owner and its name, size, free), `chain_valid`, `free_bytes`, `largest_free_bytes` and `program_psp`

## Available MCP Resources

In addition to tools, the MCP server provides documentation resources that AI assistants can access to better understand the MS-DOS PC hardware and programming.

The server reads them from the `mcp/resources/hardware` folder next to the executable (`Contents/Resources` inside `Geardox.app`). `make dist` and `make bundle` put them in the macOS app; with other builds, copy `platforms/shared/desktop/mcp/resources` beside the executable as `mcp/resources`.

In Claude Code, type `@` in your prompt to pick a resource, or reference one directly, for example `@geardox:geardox://hardware/pc_at_board`. Codex reads resources through its built-in MCP resource tools, so ask it to read the ones that are relevant.

### Hardware Documentation Resources

Complete technical reference documentation for the MS-DOS PC hardware components:

- **Intel 80386 — What MS-DOS PC Software Relies On** (`geardox://hardware/i386_cpu`)
- **Intel x87 FPU — What MS-DOS Software Relies On** (`geardox://hardware/x87_fpu`)
- **Pentium on a 430FX PCI Board — What MS-DOS Software Relies On** (`geardox://hardware/pentium_pci`)
- **PC/AT Board — Memory, I/O, Interrupts, Timers, BIOS and CMOS** (`geardox://hardware/pc_at_board`)
- **BIOS and DOS Services — What MS-DOS Software Relies On** (`geardox://hardware/dos_bios_services`)
- **CGA, EGA, VGA and the Cirrus Logic Cards — What MS-DOS Software Relies On** (`geardox://hardware/pc_video`)
- **Mouse, Joystick, Serial and Parallel Ports — What MS-DOS Software Relies On** (`geardox://hardware/pc_input_serial_parallel`)
- **IDE and the ATAPI CD-ROM Drive — What MS-DOS Software Relies On** (`geardox://hardware/pc_ide_cdrom`)
- **Sound Cards, FM, MIDI and the PC Speaker — What MS-DOS Software Relies On** (`geardox://hardware/pc_sound`)
- **Floppy Controller and DMA — What MS-DOS Software Relies On** (`geardox://hardware/pc_floppy_dma`)
- **MS-DOS Development — Program Tracking, Ports and gdb** (`geardox://hardware/dos_development`)

## How MCP Works in Geardox

- The MCP server runs **alongside** the GUI in a background thread
- The emulator GUI remains fully functional (you can play/debug normally while using MCP)
- Commands from the AI are queued and executed on the GUI thread
- Both GUI and MCP share the same emulator state
- Changes made through MCP are instantly reflected in the GUI and vice versa

## Architecture

### STDIO Transport
```
┌─────────────────┐                    ┌──────────────────┐
│  Claude Code /  │       stdio        │     Geardox      │
│      Codex      │◄──────────────────►│    MCP Server    │
│   (AI Client)   │       pipes        │   (background)   │
└─────────────────┘                    └──────────────────┘
        │                                       │
        └───► Launches ►────────────────────────┘
                                                │
                                                │ Shared State
                                                ▼
                                       ┌──────────────────┐
                                       │   Emulator Core  │
                                       │   + GUI Window   │
                                       └──────────────────┘
```

### HTTP Transport
```
┌─────────────────┐                    ┌──────────────────┐
│  Claude Code /  │  HTTP (port 7777)  │     Geardox      │
│      Codex      │◄──────────────────►│ MCP HTTP Server  │
│   (AI Client)   │                    │    (listener)    │
└─────────────────┘                    └──────────────────┘
                                                │
                                                │ Shared State
                                                ▼
                                       ┌──────────────────┐
                                       │   Emulator Core  │
                                       │   + GUI Window   │
                                       └──────────────────┘
```
