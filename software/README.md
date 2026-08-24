# Jinglehammer Core

A four-switch MIDI foot controller built on a Raspberry Pi Pico 2 (RP2350). Each
footswitch sends a configurable bundle of Program Change and Control Change
messages over hardware MIDI (UART) and USB-MIDI. Presets, banks, and channels
live in flash and are edited from a browser over USB-CDC. No drivers, no install.

Two pieces, each with its own README:

- [`firmware/`](firmware/README.md) - the C firmware and its Pico SDK build.
- [`webapp/`](webapp/README.md) - a single-file config editor that talks to the
  controller over WebSerial.

This directory is the whole software side. One level up, the repo also holds
`docs/` and `hardware/`, the latter for a 3D-printed enclosure and PCB design
files. Neither is part of any build here.

## Overall UX

The whole controller can effectively be taught in three sentences:

- Press 1–4 to select a preset in the active bank.
- Press the two left- or right-most switches simultaneously to move down or up a bank.
- Solid LEDs indicate Banks 1–4; blinking LEDs indicate Banks 5–8.

**Presets.** Also called "bundles". Selecting a preset sends that switch's message
bundle: Program Changes first, then Control Changes, each message on its own MIDI
channel.

Switches are momentary presets in "radio" mode. The switch you press becomes the
active one, its LED lights, the other three go dark.

**Banks.** Press SW1+SW2 together to step the active bank down, SW3+SW4 to step
it up. Two switches together is a "chord". A chord suppresses both switches'
presets, so changing bank spews no stray MIDI. After a step the LEDs show the
new bank for a little less than a second: banks 1-4 light LED 1-4 solid, banks
5-8 light the same four, but blinking.

Here's a visual of the LED feedback when navigating the banks:

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

After one of the above is displayed, all four LEDs slowly pulse for about 2s while the
controller waits for you to pick a preset on the new bank. Pick one and it fires
normally. Pick nothing and the bank navigation is abandoned; the controller returns
to where you started.

Bank changes are RAM-only. Nothing reaches flash until you Save from the web
app, so the bank the controller boots into is whichever one was last written.

## Hardware summary

Four momentary footswitches (normally open to GND, internal pull-ups), four
active-high PWM-driven LEDs, and UART0 TX at 31250 baud into a standard
UART-to-MIDI output stage. MIDI is output only. USB is native and enumerates as
a composite CDC + USB-MIDI device, so the same cable carries MIDI and the config
link.

Pins and counts are in [`firmware/board.h`](firmware/board.h). Full table in the
[firmware README](firmware/README.md).

## Getting started

```sh
git clone https://github.com/clevelandmusicco/jinglehammer-core
cd jinglehammer-core/software
cmake --build firmware/build     # after a one-time configure, see firmware/README.md
./webapp/build.py                # generates webapp/index.html
```

In VSCode, open `jinglehammer.code-workspace` rather than the `software/` folder.
It is a multi-root workspace over `software/`, `firmware/` and `webapp/`, and it
is what makes VSCode pick up each half's `.vscode/` directory: opening `software/`
as a plain folder means `firmware/.vscode/` is never read, so the Pico extension
config, IntelliSense and all the tasks are silently ignored.

Tasks are namespaced by half and independent of each other:

| task | does |
| --- | --- |
| `firmware: build` | incremental `cmake --build build` |
| `firmware: configure` | fresh CMake configure with the SDK and toolchain paths |
| `firmware: clean rebuild` | wipe `build/`, reconfigure, build |
| `firmware: size` | flash/RAM footprint of the ELF |
| `firmware: deploy` | build, then `picotool load -fx` over USB |
| `webapp: build` | inline `src/` into `index.html` |
| `webapp: check` | fail if `index.html` is stale |
| `webapp: test` | rebuild, then run the gjs self-test |
| `webapp: open` | rebuild and open `index.html` in a browser |
| `all: build` | both halves, in parallel |
| `all: verify` | firmware build plus webapp self-test |
| `all: deploy` | firmware build, load over USB, rebuild the editor |

`firmware: deploy` uses picotool's `-f`, which reboots a running board into
BOOTSEL on its own, so you do not need to hold the button. That relies on the
reset interface the firmware exposes (`firmware/usb_reset_iface.c`), so a board
running some older build has to be flashed the manual way once before the task
starts working. The Pico extension's own generated tasks (`Compile Project`,
`Flash`, the reset ones) are still there and still work.

## Work in progress

This is NOT a finished project. Expect a lot of change (improvements?) in the coming
weeks.
