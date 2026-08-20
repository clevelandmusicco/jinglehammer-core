# jinglehammer-core

Firmware for a four-switch MIDI foot controller built on a Raspberry Pi Pico 2
(RP2350). Each footswitch sends a configurable bundle of Program Change and
Control Change messages over hardware MIDI (UART) and USB-MIDI. Presets, banks,
and channels live in flash and are edited from a browser over USB-CDC with the
single-file web app in `webapp/`. No drivers, no install; see
[Config editor](#config-editor-web-app) below.

## What it does

One press sends that switch's whole bundle: Program Changes first, in array
order, then Control Changes, each message on its own MIDI channel. PC before CC
is the safe order for patch recall, since the CCs then land on the patch the PC
just selected.

Switches are momentary presets in "radio" mode. The switch you press becomes the
active one, its LED lights, the other three go dark. Nothing is active at boot,
so the unit stays silent until the first press.

**Banks.** Press SW1+SW2 together to step the active bank down, SW3+SW4 to step
it up. Both ends clamp; there is no wrap. A chord suppresses both switches'
presets, so changing bank emits no stray MIDI. After a step the LEDs show the
new bank for about 0.8 s: banks 1-4 light LED 1-4 solid, banks 5-8 light the
same four blinking. Then all four LEDs breathe for about 2 s while the unit
waits for you to pick a preset on the new bank. Pick one and it fires normally.
Pick nothing and the whole move is abandoned: the bank returns to where you
started, replays that bank's readout, and settles back on the preset that was
lit before you began.

During bank navigation, the LEDs provide simple visual feedback:

```text
Bank 1 = ● ○ ○ ○
Bank 2 = ○ ● ○ ○
Bank 3 = ○ ○ ● ○
Bank 4 = ○ ○ ○ ●


Bank 5 = ◉ ○ ○ ○
Bank 6 = ○ ◉ ○ ○
Bank 7 = ○ ○ ◉ ○
Bank 8 = ○ ○ ○ ◉
```

where ◉ means blinking.

Bank changes are RAM-only. Nothing reaches flash until you Save from the web
app, so the bank the unit boots into is whichever one was last written.

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

## Getting the code

```sh
git clone <repo-url>
cd jinglehammer
```

## Setting up the Pico toolchain

This project targets Pico SDK 2.2.0 and board `pico2`.

**Recommended:** install [VS Code](https://code.visualstudio.com/) and the
[Raspberry Pi Pico extension](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico).
Open this folder and run the extension's "New Project" / "Import Project" flow
once. That downloads the SDK, ARM toolchain, CMake, Ninja, OpenOCD, and picotool
into `~/.pico-sdk/`, which is where the checked-in `.vscode/` config points.
After that the extension's Configure / Build / Flash buttons work directly.

**Manual:** install the same set yourself under `~/.pico-sdk/`:

- Pico SDK 2.2.0 (`~/.pico-sdk/sdk/2.2.0`)
- ARM toolchain 14_2_Rel1 (`~/.pico-sdk/toolchain/14_2_Rel1`)
- CMake, Ninja, and picotool (any recent version works if you're not using the VS Code extension)

## Building and flashing

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

- **Drag-and-drop:** hold BOOTSEL while plugging in USB, then copy `build/midi_ctrl.uf2` to the `RP2350` drive that appears.
- **CLI:** `picotool load -f build/midi_ctrl.uf2`

A fresh unit comes up on factory defaults: bank 1, four presets, switch 1
sending a PC on channel 1 plus three CCs on channels 2 and 3.

## Config editor (web app)

`webapp/index.html` is the editor. It is generated from `webapp/src/`, not
checked in, so build it once after cloning:

```sh
./webapp/build.sh
```

That inlines the template, CSS, and JS modules into one self-contained file.
No npm and no bundler; it is `sh` plus `awk`. The app has to ship as a single
file because WebSerial works from `file://` but ES modules do not.

- `./webapp/build.sh --check` exits 1 if `index.html` is stale.
- `./webapp/test.sh` rebuilds, then self-tests the byte codec and page load.
  Needs `gjs` (Debian/Ubuntu: `apt install gjs`).

Edit `webapp/src/`. Anything written to `index.html` is lost on the next build.

### Editing presets

Open `webapp/index.html` in a Chromium-based browser (Chrome or Edge; Firefox
and Safari do not ship WebSerial). Plug the controller in over USB, then:

1. **Connect**, and pick the controller from the browser's port chooser.
2. **Read** to pull the current config off the device.
3. Edit each footswitch: name, plus its Program Change and Control Change
   lists (up to 16 of each, every message with its own 1-16 MIDI channel).
   **Test** plays a switch's bundle immediately from the on-screen values,
   without saving. Numbered tabs page through the 8 banks, and **Set as
   active** marks the bank the controller runs and boots on.
4. **Write + Save** sends all 8 banks and commits them to flash. The device
   range-checks the whole blob, so one bad value rejects the entire write.

**Factory reset** overwrites the saved config with the built-in defaults.
**Frames** opens a raw-frame monitor for debugging the CDC link.

## Further reading

- [`doc/MIDI-Host-Considerations.md`](doc/MIDI-Host-Considerations.md): parking-lot
  design notes on driving 5-pin DIN and USB-only pedals from the same rig. Nothing
  there is built yet.
- [`DEVLOG.md`](DEVLOG.md): design decisions and the reasoning behind them, newest last.
