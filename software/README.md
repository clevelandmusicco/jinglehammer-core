# Jinglehammer software

A four-switch MIDI foot controller built on a Raspberry Pi Pico 2 (RP2350). Each
footswitch sends a configurable bundle of Program Change and Control Change
messages over hardware MIDI (UART) and USB-MIDI. Presets, banks, and channels
live in flash and are edited from a browser over USB-CDC. No drivers, no install.

Two pieces, each with its own README:

- [`firmware/`](firmware/README.md) - the C firmware and its Pico SDK build.
- [`webapp/`](webapp/README.md) - the single-file config editor that talks to the
  unit over WebSerial.

This directory is the whole software side. One level up, the repo also holds
`docs/` and `hardware/`, the latter for enclosure and PCB design files. Neither
is part of any build here.

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

## Hardware summary

Four momentary footswitches (normally open to GND, internal pull-ups), four
active-high PWM-driven LEDs, and UART0 TX at 31250 baud into a standard
UART-to-MIDI output stage. MIDI is output only. USB is native and enumerates as
a composite CDC + USB-MIDI device, so the same cable carries MIDI and the config
link.

Pins and counts are in [`firmware/board.h`](firmware/board.h), the only file to
edit when re-pinning. Full table in the [firmware README](firmware/README.md).

## Getting started

```sh
git clone <repo-url>
cd jinglehammer/software
cmake --build firmware/build     # after a one-time configure, see firmware/README.md
./webapp/build.sh                # generates webapp/index.html
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

## Further reading

- [`../docs/MIDI-Host-Considerations.md`](../docs/MIDI-Host-Considerations.md):
  parking-lot design notes on driving 5-pin DIN and USB-only pedals from the same
  rig. Nothing there is built yet.
- [`DEVLOG.md`](DEVLOG.md): design decisions and the reasoning behind them,
  newest last.
