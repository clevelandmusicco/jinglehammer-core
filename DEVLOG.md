# DEVLOG: bank-nav branch

Working log for the bank-navigation feature, branched off `main`. Purpose: let
future sessions (or future me) resume without re-deriving decisions already
made. Update this file each commit; keep it current, not historical - prune
resolved "open questions" rather than leaving them stale.

## Goal

Add bank up/down navigation on the footswitches, plus a temporary LED readout
of which bank is now active. Scope for this branch is mechanics + indicator
only; no webapp changes, no new switch modes beyond what's needed for this.

First cut used double-tap on SW1/SW4. Superseded (see below) by a two-switch
chord: double-tap fired each tap's own preset in addition to stepping the
bank, so a bank change always came with an unwanted MIDI fire from whichever
preset lived on that switch.

## Status

- `21667e1` - double-tap detection + bank step (`controller.c`) - **superseded**
- `4c79de1` - LED bank-display on top of that (`controller.c`, `controller.h`, `main.c`) - display logic carried forward as-is
- `71eb2a9` - replaced double-tap with a chord gesture; see Behaviour below.
- Current (uncommitted) - preset-wait after bank-display timeout, replacing
  the old "LED 0 = preset 1" placeholder; see Preset-wait section below.
- Not yet done: flashed/tested on real hardware. Everything below is
  desk-reviewed and builds clean, not yet felt with a boot.

## Behaviour, as built

- SW1+SW2 pressed together -> `active_bank -= 1`; SW3+SW4 together ->
  `active_bank += 1`. Clamped at 0 / `NUM_BANKS-1` (no wrap).
- A recognised chord suppresses both switches' normal preset fire entirely -
  only the bank step and LED display happen. This is the whole point of the
  switch from double-tap: no more MIDI collision between "I meant to change
  bank" and "that switch also has a preset."
- Chord partner pairing is `i ^ 1`: switches (0,1) are the down pair, (2,3)
  the up pair.
- Detection uses a tolerance window, not exact same-tick simultaneity. Each
  switch debounces independently (8ms shift register, see `io.c`), all four
  sampled in the same `io_poll()` call - but two real footswitch presses
  aimed at "the same moment" can still complete a poll tick or two apart. So
  every edge on SW1-4 is held pending for `CHORD_WINDOW_MS` (20ms: 8ms
  debounce + slack for press skew): if the partner edge shows up before the
  deadline (same tick or a later one), it's a chord - bank step fires,
  neither preset does, and the first-arriving switch's pending entry is
  cancelled without ever firing. If the deadline passes with no partner, it
  was a solo press - the queued preset fires then, late by at most
  `CHORD_WINDOW_MS`.
  - Consequence: **every** press on SW1-4 now carries up to 20ms of added
    latency, even a plain single-switch press, since the firmware can't know
    in advance whether a chord partner is coming. This is a deliberate
    tradeoff (user's call, see chat history) - 20ms is inaudible/imperceptible
    for PC/CC messages, traded for chords being reliably hittable by foot
    instead of requiring exact-tick luck.
- `active_bank` change is RAM-only - no `config_save()` call. Cheap to spam,
  survives until a webapp Save or reverts to the last-Saved bank on reboot.
- On a recognised chord (even one that clamps to the same bank - that's the
  "you're already at the end" feedback), LEDs switch to bank-display mode for
  800ms: LED `(bank % NUM_SWITCHES)` lights, solid for banks 1-4, blinking for
  banks 5-8. Unchanged from the double-tap version.
- Blink = 4 on/off cycles at 100ms/state, fills the 800ms window exactly.
- A later chord while the display is showing restarts the window/blink
  against the new bank.
- Any normal switch press (chord or solo-fire-after-timeout) preempts the
  display immediately and shows that press's radio LED instead - a fired
  preset always wins over a stale bank indicator.
- On bank-display timeout, the unit does **not** default to preset 1 - see
  "preset-wait" below, added after this was flagged as misleading (stepping a
  bank shouldn't silently imply picking a preset on it).

## Preset-wait (uncommitted, on top of the above)

Resolves the open question below about what happens after the bank-display
window: previously LED 0 lit as a "preset 1 of the new bank" placeholder with
no MIDI sent, which read as if a preset had actually been selected. Now:

- When `BANK_DISPLAY_WINDOW_MS` elapses, the unit enters a new LED mode
  (`LED_MODE_AWAIT_PRESET`) instead of returning straight to radio mode: all 4
  LEDs breathe together for `PRESET_WAIT_WINDOW_MS` (2000ms) - fade low-\>high
  over `PRESET_WAIT_STATE_MS` (500ms), then high-\>low over the next 500ms,
  repeat - 2 up/down cycles fill the window exactly, same "N units fill the
  window" pattern as the bank blink, just continuous brightness instead of a
  hard on/off toggle.
- First cut used a hard on/off blink (250ms/state, 4 cycles), same mechanism
  as bank-display's blink. Replaced with an actual fade after the ask for a
  smoother "breathing" look instead of a blink - see the `io.c` change below.
- Any normal press during the fade (chord or solo) preempts it immediately,
  same rule as bank-display preemption - the newly fired preset's radio LED
  wins and becomes the active preset going forward.
- Another chord during the fade restarts bank-display again against the
  further-stepped bank, then re-enters preset-wait - chaining chords keeps
  extending the sequence rather than committing anything.
- If the 2000ms window lapses with nothing picked, the whole navigation
  sequence is abandoned: `active_bank` reverts (RAM-only) to whatever it was
  *before the first chord in the sequence*. This is the literal "keep the old
  bank and preset" ask - a bank step is now provisional until a preset
  confirms it.
- First cut of the revert set the reverted-to preset's LED directly, with no
  indication a revert had even happened - visually indistinguishable from
  just picking that preset normally. Fixed per feedback: the revert now lands
  `active_bank` in RAM, then calls `start_bank_display(true)` to replay the
  *normal* bank-display readout (same position/blink code) for the bank
  being reverted to, and only after that window ends does it settle on the
  LED of the preset that was actually active before all this started (or off,
  pre-first-press). Worked example: bank 5 preset 3 active, SW3+4 steps to
  bank 6, nothing picked in time -> LED1 blinks (bank 5's display code),
  then LED3 lights solid (preset 3) - reads as "back to bank 5, preset 3,"
  not a silent snap.
- `start_bank_display()` takes an `is_revert` bool, stashed in
  `bank_display_revert`, so `controller_poll()` knows what to do once that
  display's own window ends: forward nav (`is_revert == false`) chains into
  `start_preset_wait()` as before; a revert (`is_revert == true`) settles
  straight into `LED_MODE_RADIO` showing `active_switch`. Same struct/timer/
  blink fields serve both cases - it's the same display, just two different
  reasons to be showing it and two different next steps.
- `controller_on_edges()` snapshots `g_config.active_bank` before its
  `bank_step()` call, but only commits it to `pre_nav_bank` (the revert
  target) when the sequence starts from `LED_MODE_RADIO` - a chord that lands
  mid-display or mid-wait (including during a revert's own display) must not
  overwrite the revert target with an intermediate bank. A fresh chord
  landing during a revert's display works out correctly anyway: by then
  `active_bank` has already been set to `pre_nav_bank` in RAM, so stepping
  from "current" and stepping from "the bank being reverted to" are the same
  number - the new chord just starts a normal forward nav from there,
  `bank_display_revert` gets reset to `false` for it, and `pre_nav_bank`
  isn't touched because `led_mode` isn't `LED_MODE_RADIO` at that instant.
- `controller_poll()` now does three jobs: fires any preset whose chord window
  expired with no partner, drives the bank-display deadline/blink, and drives
  the preset-wait deadline/fade (including the revert). All need to progress
  with no switch presses arriving, hence still polled every ~1ms from
  `main.c` independent of edges. The fade level is recomputed every poll tick
  (not just at the 500ms state boundary) by linearly interpolating elapsed
  time within the current ramp, so it reads as continuous motion rather than
  256 possible steps landing only every 500ms.
- `io.c` LEDs moved from plain `gpio_put` to hardware PWM
  (`gpio_set_function(..., GPIO_FUNC_PWM)`, 8-bit duty, `hardware_pwm` added
  to `target_link_libraries` in `CMakeLists.txt`) so brightness is controllable
  at all - `io_set_leds(mask)` keeps the old on/off call shape (duty 0 or 255),
  new `io_set_leds_level(level)` sets all 4 LEDs to the same 0-255 duty for the
  fade. LED1/LED2 (GP6/7) and LED3/LED4 (GP8/9) each share a PWM slice, but
  channels A/B have independent compare registers, so per-LED brightness
  still works independently - just don't call `pwm_init()` again after
  `io_init()`, since it resets both channels' compare registers on that slice.

## Known limits / assumptions

- Bank-display encoding (`position = bank % NUM_SWITCHES`, `blink = bank >=
  NUM_SWITCHES`) is only unambiguous up to `2 * NUM_SWITCHES` banks.
  `NUM_BANKS` is exactly that today (8 = 2*4). If `NUM_BANKS` ever grows past
  that, the display silently aliases - no guard exists for it, out of scope
  for this branch.
- Chord pairing (`i ^ 1`, delta by `i < 2`) is hardcoded for exactly 4
  switches split into two fixed pairs. Fine at `NUM_SWITCHES == 4`; would need
  rework if the switch count or pairing ever changes.
- Timing (chord window, display window, blink) is wall-clock (`absolute_time_t`
  / `time_reached`), not tied to poll count, so it degrades gracefully if
  `controller_poll()` call cadence ever drifts from 1ms - just coarser, not
  broken.
- No flash write ever happens from a chord. Same deliberate tradeoff as the
  double-tap version - flash write is blocking + wears the sector, bad fit for
  something tapped often mid-set.

## Open questions / next steps

- Hardware feel: `CHORD_WINDOW_MS` (20ms), 800ms display window, 100ms blink
  cadence - all reasonable guesses, untested on a real footswitch. The chord
  window in particular may need tuning: too short and intentional chords miss
  each other's tick, too long and fast sequential solo presses on SW1-4 risk
  being misread as a chord.
- Webapp is untouched. `active_bank` can now drift from what the webapp last
  saw without a Save - probably fine (Read already re-syncs), but no explicit
  decision was made about surfacing this in the UI.
- Non-preset switch modes (latching, hold for something other than bank nav)
  still not started - unrelated to this branch, just noting so it's not
  confused with in-scope work.

## How to resume in a new session

- Read `controller.c`'s top-of-file comment block first - kept in sync with
  behaviour, describes the policy inline. This file is the "why we chose
  this," that one is the "what it does."
- Build: `cmake --build build` (build dir already configured, see root
  `CLAUDE.md`).
- Flash: no BOOTSEL-less USB path exists for this firmware - confirmed
  `picotool load -f` won't work here (raw TinyUSB composite device, no
  `pico_stdio_usb`/vendor reset interface, `CFG_TUD_VENDOR 0`). Use the SWD
  path instead: VS Code task `Flash` (`openocd` + CMSIS-DAP probe), already
  wired up in `.vscode/tasks.json`. Requires a physical debug probe on the
  board's SWD pads.
