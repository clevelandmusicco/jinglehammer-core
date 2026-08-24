# Jinglehammer Core Firmware

C firmware for the RP2350 (Pico 2). Targets Pico SDK 2.2.0 and board `pico2`.
For what the controller actually does, see the [software README](../README.md); for
the config editor, [`../webapp/`](../webapp/README.md).

## Hardware

Pins, counts, and the MIDI baud rate all live in [`board.h`](board.h).

| Function | GPIO | Notes |
| --- | --- | --- |
| Footswitch 1-4 | GP2, GP3, GP4, GP5 | Momentary, normally open to GND. Internal pull-ups, so a press reads 0. |
| LED 1-4 | GP6, GP7, GP8, GP9 | Active high, PWM-driven so brightness can be faded. LED N pairs with switch N. |
| MIDI out | GP0 (UART0 TX) | 31250 baud 8N1, into a standard UART-to-MIDI output stage (5-pin DIN or TRS-A). Output only. |

> [!WARNING]
> These pins currently don't match the PCB layout! That will be resolved very soon.

USB is native and enumerates as a composite CDC + USB-MIDI device, so the same
cable carries MIDI and the config link.

## Source layout

One responsibility per module:

| File | Responsibility |
| --- | --- |
| `board.h` | Pins, counts, MIDI UART baud. |
| `config.[ch]` | Persisted data model, CRC-32, flash load/save, serialize seams. |
| `midi.[ch]` | Builds PC/CC bytes, emits a preset bundle to UART + USB. |
| `io.[ch]` | Polled switch debounce and PWM LED output. |
| `controller.[ch]` | Radio policy and bank navigation: press edges to MIDI + LEDs. |
| `cdc_proto.[ch]` | Framed read/write/save command set on USB-CDC. Non-blocking. |
| `main.c` | Init plus the super-loop. |
| `usb_descriptors.c`, `tusb_config.h` | TinyUSB composite CDC + MIDI device. |

Dependency direction: `main` wires everything; `controller` uses `config`,
`midi`, `io`; `cdc_proto` uses `config`; `midi` and `config` use `board`; `io`
uses `board`. No cycles.

## Setting up the Pico toolchain

For a fresh Pico 2 with nothing installed yet, use VS Code — it sets up
everything in one step and needs no manual PATH wrangling.

1. Install [VS Code](https://code.visualstudio.com/) and the
   [Raspberry Pi Pico extension](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico).
2. Open [`software/jinglehammer.code-workspace`](../jinglehammer.code-workspace)
   in VS Code (not a plain folder). It's a multi-root workspace with this
   folder as one of the roots, which is what the checked-in
   [`.vscode/`](.vscode) config and the extension's path resolution need — VS
   Code only reads a folder's `.vscode/` when that folder is itself a
   workspace-folder root. Opening `software/` or the repo root as a plain
   folder skips it, and the Build/Flash buttons end up pointed at a build
   directory that doesn't exist.
3. Run the extension's "Import Project" flow once (it detects the existing
   `CMakeLists.txt`). This downloads the SDK, ARM toolchain, CMake, Ninja,
   OpenOCD, and picotool into `~/.pico-sdk/` — nothing is installed system-wide.

That's it. The extension's Configure / Build / Flash buttons work from here on.

**Manual setup** (skip the extension, use the CLI below): put the same tools
under `~/.pico-sdk/` yourself — Pico SDK 2.2.0 at `~/.pico-sdk/sdk/2.2.0`, ARM
toolchain 14_2_Rel1 at `~/.pico-sdk/toolchain/14_2_Rel1`, plus CMake, Ninja, and
picotool (any recent version of these three is fine).

## Building and flashing

### First flash, out of the box

A brand new Pico 2 has no firmware with the custom reset interface, so its
*only* way in is BOOTSEL mode:

1. Hold the **BOOTSEL** button, plug in USB, then let go. The board mounts as
   a mass-storage drive named `RP2350`.
2. Build the firmware (from this directory):

   ```sh
   cmake -S . -B build -G Ninja \
     -DPICO_SDK_PATH=$HOME/.pico-sdk/sdk/2.2.0 \
     -DPICO_TOOLCHAIN_PATH=$HOME/.pico-sdk/toolchain/14_2_Rel1 \
     -DPICO_BOARD=pico2
   cmake --build build
   ```

   Output: `build/midi_ctrl.uf2`.
3. Copy `build/midi_ctrl.uf2` onto the `RP2350` drive. The board flashes
   itself and reboots into the new firmware.

It now comes up on factory defaults: bank 1, four presets, switch 1 sending a
PC on channel 1 plus three CCs on channels 2 and 3.

### Every flash after that

Once the board is running a build that includes `usb_reset_iface.c` (true
after the first flash above), you don't need BOOTSEL again — the board can
reset itself over the USB cable:

```sh
cmake --build build
picotool load -fx build/midi_ctrl.uf2
```

`-f` reboots the board into BOOTSEL and `-x` runs the new image immediately
after.

**SWD** (CMSIS-DAP probe, VS Code task `Flash`) works too, for when USB isn't
an option. But most folks find `picotool load -fx` to be simpler.

## Debugging

stdio is off on both USB and UART0 by default: USB belongs to TinyUSB (CDC +
MIDI) and UART0 is the MIDI port. To get `printf` out, route stdio to a spare
UART, add `pico_enable_stdio_uart(midi_ctrl 1)` to `CMakeLists.txt`, and call
`stdio_init_all()` in `main`.
