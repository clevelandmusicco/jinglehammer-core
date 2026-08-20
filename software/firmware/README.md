# Jinglehammer firmware

C firmware for the RP2350 (Pico 2). Targets Pico SDK 2.2.0 and board `pico2`.
For what the pedal actually does, see the [software README](../README.md); for
the config editor, [`../webapp/`](../webapp/README.md).

## Hardware

Pins, counts, and the MIDI baud rate all live in [`board.h`](board.h), which is
the only file to edit when re-pinning.

| Function | GPIO | Notes |
| --- | --- | --- |
| Footswitch 1-4 | GP2, GP3, GP4, GP5 | Momentary, normally open to GND. Internal pull-ups, so a press reads 0. |
| LED 1-4 | GP6, GP7, GP8, GP9 | Active high, PWM-driven so brightness can be faded. LED N pairs with switch N. |
| MIDI out | GP0 (UART0 TX) | 31250 baud 8N1, into a standard UART-to-MIDI output stage (5-pin DIN or TRS-A). Output only. |

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

**Recommended:** install [VS Code](https://code.visualstudio.com/) and the
[Raspberry Pi Pico extension](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico).
Open **this** folder (`software/firmware`, not the repo root) and run the
extension's "New Project" / "Import Project" flow once. That downloads the SDK,
ARM toolchain, CMake, Ninja, OpenOCD, and picotool into `~/.pico-sdk/`, which is
where the checked-in [`.vscode/`](.vscode) config points. After that the
extension's Configure / Build / Flash buttons work directly.

The extension resolves its paths against the folder you opened, so opening
`software/` or the repo root instead leaves those buttons pointed at a build
directory that does not exist.

**Manual:** install the same set yourself under `~/.pico-sdk/`:

- Pico SDK 2.2.0 (`~/.pico-sdk/sdk/2.2.0`)
- ARM toolchain 14_2_Rel1 (`~/.pico-sdk/toolchain/14_2_Rel1`)
- CMake, Ninja, and picotool (any recent version works if you're not using the VS Code extension)

## Building and flashing

Run these from this directory. `CMakeLists.txt` lists bare source filenames and
includes `pico_sdk_import.cmake` from beside itself, so `-S` has to be this
directory rather than the repo root.

Configure once:

```sh
cmake -S . -B build -G Ninja \
  -DPICO_SDK_PATH=$HOME/.pico-sdk/sdk/2.2.0 \
  -DPICO_TOOLCHAIN_PATH=$HOME/.pico-sdk/toolchain/14_2_Rel1 \
  -DPICO_BOARD=pico2
```

Then build:

```sh
cmake --build build
```

Output: `build/midi_ctrl.uf2`.

Flash it to the Pico 2:

- **CLI, board already running this firmware:** `picotool load -fx build/midi_ctrl.uf2`.
  `-f` reboots it into BOOTSEL over USB and `-x` runs the new image, so the
  button and the USB cable stay untouched. Needs the reset interface
  (`usb_reset_iface.c`), which means the board must already be running a build
  that has it. No driver setup on any host: Linux and macOS hand the interface
  to libusb unasked, and Windows 8.1+ binds WinUSB from the MS OS 2.0
  descriptors in `usb_descriptors.c`.
- **Drag-and-drop:** hold BOOTSEL while plugging in USB, then copy `build/midi_ctrl.uf2` to the `RP2350` drive that appears.
  This is the one-time path onto a board running anything older.
- **SWD:** VS Code task `Flash` (openocd + CMSIS-DAP probe), for when USB is not
  an option.

A fresh unit comes up on factory defaults: bank 1, four presets, switch 1
sending a PC on channel 1 plus three CCs on channels 2 and 3.

## Debugging

stdio is off on both USB and UART0 by default: USB belongs to TinyUSB (CDC +
MIDI) and UART0 is the MIDI port. To get `printf` out, route stdio to a spare
UART, add `pico_enable_stdio_uart(midi_ctrl 1)` to `CMakeLists.txt`, and call
`stdio_init_all()` in `main`.
