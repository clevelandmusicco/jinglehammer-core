# jinglehammer

Firmware for a four-switch MIDI foot controller built on a Raspberry Pi Pico 2 (RP2350). Each footswitch sends a configurable bundle of Program Change and Control Change messages over hardware MIDI (UART) and USB-MIDI. Configuration (presets, banks, channels) is stored in flash and edited from a browser via a single-file web app (`webapp/index.html`) over USB-CDC — no drivers or install needed.

## Getting the code

```sh
git clone <repo-url>
cd jinglehammer
```

## Setting up the Pico toolchain

This project targets Pico SDK 2.2.0 and board `pico2`.

**Recommended:** install [VS Code](https://code.visualstudio.com/) and the [Raspberry Pi Pico extension](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico). Open this folder and use the extension's "New Project" / "Import Project" flow once — it downloads the SDK, ARM toolchain, CMake, Ninja, OpenOCD, and picotool into `~/.pico-sdk/`, and the checked-in `.vscode/` config points at those paths. After that, the extension's Configure / Build / Flash buttons work directly.

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
