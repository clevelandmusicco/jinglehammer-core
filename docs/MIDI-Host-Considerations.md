# MIDI Host Considerations

Status: design notes, no decision made yet. Captured 2026-06-02.

Parking-lot document for a question that keeps coming up: how does this
controller talk to pedals, and what changes if it has to talk to USB-only
pedals as well as the 5-pin DIN ones it already supports? Nothing here is
built. This is the context you need to pick up the thread later without
re-deriving it.

## What prompted this

The controller is a MIDI *source*. The original worry was "I can't plug a USB
cable from the Pico straight into a pedal's USB and have them talk." That is
correct, and the reason is worth stating precisely because it drives every
option below.

New data point (the reason this doc exists rather than a one-line answer): the
rig will likely need to drive **5-pin DIN pedals and USB-only pedals at the same
time**. That is the interesting case. The two transports are independent and the
combined design has real config-model and timing consequences.

## The core constraint: USB is host/device, not peer-to-peer

USB has exactly one **host** per bus (it powers the bus, enumerates devices,
and schedules all traffic) and one or more **devices**. Today the Pico
enumerates as a USB *device*: the composite CDC + USB-MIDI in
`usb_descriptors.c`, set up by the `CFG_TUD_*` block in `tusb_config.h`. A
pedal's USB port is almost always *also* a device port (it expects to be
plugged into a computer). Two devices, no host between them, nothing happens.

A USB *hub* does not fix this. A hub is a passive fan-out that still needs a
host upstream. To drive a USB pedal the controller has to **become a USB host**.
That is what commercial "USB MIDI Host" boxes are (Kenton, Disaster Area,
iConnectivity). DIN MIDI sidesteps the whole issue: it is a one-way 5 mA current
loop, point to point, no host and no enumeration.

## Transport A: 5-pin DIN (already supported in firmware)

`midi.c` already emits real serial MIDI on `MIDI_UART` / `MIDI_TX_PIN` (GP0,
UART0 TX) at 31250 8N1. `board.h` was written with this in mind. For a DIN pedal
there is **no firmware work**; the gap is a small output hardware stage between
GP0 and the DIN socket:

- DIN pins 4 and 5 are the signal pair (the loop); pin 2 is cable shield/ground.
- Two series resistors set loop current to ~5 mA. The pedal supplies the
  opto-isolator that closes the loop, so the controller side is just resistors
  plus the connector.

One gotcha: the textbook MIDI-OUT circuit (220 ohm / 220 ohm) assumes a **5 V**
logic driver. GP0 swings **3.3 V**. Two clean fixes:

1. Drive direct from 3.3 V GP0 with resistor values rescaled for the lower
   voltage (fewest parts; needs the correct 3.3 V values to hold ~5 mA).
2. Buffer GP0 through a 5 V `74HCT04` / `74HCT14` gate, then use the standard
   220 ohm / 220 ohm circuit unchanged. The 'HCT family reads 3.3 V as a valid
   logic high and outputs a full 5 V swing. One extra chip, stronger drive,
   more margin on cheap or long cables. This is what the MIDI 1.0 spec did.

Before building either, pull the current MIDI Association electrical spec for
the exact resistor values rather than trusting a remembered number; an
underdriven opto gives garbled or missed MIDI.

## Transport B: USB-only pedals (needs a USB host; not built)

If a pedal has no DIN/TRS and only USB, the controller must host it. The clean
pattern on RP2350:

- **Keep native USB as the device** so the `webapp` CDC config link keeps
  working unchanged.
- **Add a second USB port via `pico-pio-usb`**, a Full-Speed USB port bit-banged
  on two GPIOs through PIO, configured as **host**.
- Enable the TinyUSB host stack (`CFG_TUH_*` in `tusb_config.h`), enumerate the
  pedal as a USB-MIDI device, and forward the bytes `midi.c` already builds.

This is a known-good setup (Adafruit's "USB MIDI Host" examples do exactly
this), but it is a real chunk of work, not a flag flip. Hard caveat: the pedal's
USB port **must be class-compliant USB-MIDI**. Some pedals expose USB only for a
proprietary editor/firmware protocol and ignore raw MIDI. If a given pedal is
one of those, host mode does not help it and DIN is the only route. Verify this
per pedal model before committing.

## The combined case: DIN + USB-only at the same time

The transports are independent, so "both at once" is additive, not a conflict:

```text
UART0 TX (GP0) --> DIN output stage --------> DIN pedal(s)      [done in fw]
Native USB     --> device  --------> webapp config (CDC)        [done in fw]
                          \--------> cable 0 MIDI to a DAW/host [done, best-effort]
PIO-USB (2 GPIOs) --> host --------> USB-MIDI pedal(s)          [NOT built]
```

The DIN side and the native-USB device side are unchanged. All the new work is
the PIO-USB host port plus how a preset decides *which* of these sinks a given
message goes to.

### Firmware impact

- **Send fan-out.** `emit()` in `midi.c` is the single choke point: today it does
  `uart_write_blocking` (authoritative) then a best-effort `tud_midi_stream_write`.
  A USB-host sink (`tuh_midi_...`) slots in here as a third destination. The
  "UART authoritative, USB best-effort" split already in that function is the
  right model; the host sink is also best-effort (a pedal that unplugs or NAKs
  must never stall the footswitch poll).
- **tusb_config.** Add the `CFG_TUH_*` host block (host MIDI, and `CFG_TUH_HUB`
  if more than one USB pedal, see hardware below). Device config stays.
- **Super-loop.** `main.c` would also service `tuh_task()`. PIO-USB host has to
  service bus timing (start-of-frame every 1 ms); the common pattern is to run
  the PIO-USB host on **core1** so the device stack, footswitch poll, and flash
  ops on core0 cannot starve it.

### Config-model impact (the part that actually needs a decision)

This is the real reason the combined case is not free. Today every PC/CC targets
a MIDI **channel** (0..15) and nothing else (see `config.h`, `preset_t`). On a
single DIN wire that is enough: every pedal sees every byte and filters by
channel. Once there are multiple physical buses (one DIN wire plus N separate
USB pedals), channel alone is ambiguous: a DIN pedal and a USB pedal both
listening on channel 1 would both need "channel 1 PC 5," but those are different
wires. The data model needs a notion of **destination port**. Options, roughly
in increasing cost:

1. **Unique-channel rig (no model change).** Assign every pedal a unique channel
   across the whole rig and broadcast every message to every sink. Works for up
   to 16 pedals, zero firmware/data change, but the user must never reuse a
   channel. Simplest, and genuinely fine for small rigs.
2. **Destination per preset.** One `dest` byte per preset: a whole footswitch
   press targets one port. Small blob change, limited flexibility.
3. **Destination per message.** A `dest` byte on each PC and CC entry. Most
   flexible, largest blob change.
4. **Routing table.** A channel-to-port map held alongside the banks; message
   structs stay as they are and channel becomes the routing key. Keeps presets
   unchanged at the cost of a side table.

Any of options 2-4 changes the persisted layout, which means bumping
`CONFIG_VERSION`. Per `CLAUDE.md` and the `project-config-model` memory, that
invalidates every stored blob and falls back to factory defaults, so it needs
the migration step once units are in the field. The webapp would also grow a
per-message (or per-preset) port picker. See `project-config-model` for the blob
layout and migration notes.

Related open problem: **how config names a USB pedal**. USB devices enumerate by
VID/PID and connection order, neither of which is a stable user-facing label.
Identifying "USB pedal A" reliably (by VID/PID, or by physical hub port) is its
own small design question.

### Hardware impact

- **Pins.** GP0 is MIDI TX; GP2-5 are footswitches; GP6-9 are LEDs (`board.h`).
  PIO-USB needs two consecutive GPIOs for D+/D- (D- = D+ + 1). GP10/GP11 are
  free and would do. Plenty of headroom.
- **Connector and power.** A USB-A receptacle wired to the PIO pins, plus 5 V
  VBUS to power the pedal. The Pico can pass 5 V from VSYS/VBUS but downstream
  current is limited by the upstream supply.
- **Multiple USB pedals.** One PIO-USB root port drives one device. More than
  one USB pedal means an external USB **hub** downstream (likely powered) and
  `CFG_TUH_HUB` in the host stack. TinyUSB host hub support exists but adds
  enumeration complexity and is worth a bring-up test before relying on it.

## Sharp edges to remember

- **Flash erase versus USB host timing.** Config save erases a flash sector and
  masks interrupts for tens of ms (`CLAUDE.md`, `project-config-model`). Today
  that only pauses the USB *device* feed, which is best-effort and harmless. A
  USB *host* that loses start-of-frame timing for tens of ms can make the
  downstream pedal drop off and re-enumerate. Mitigations: only save when idle,
  keep the host stack on core1 away from the core0 flash op, or just accept a
  re-enumeration after a save. Decide this if/when the host path is built.
- **Class-compliance is per pedal.** Confirm each USB-only pedal actually speaks
  USB-MIDI class before assuming the host path covers it.
- **One native USB controller.** RP2350 has a single native USB port; it can be
  host or device but not both usefully at once. Keeping native = device (for the
  webapp) and PIO-USB = host (for pedals) is why the second port exists. The
  inverse (native host, PIO device) is possible but costs the clean webapp link.

## Quick decision guide

- All target pedals have DIN/TRS: build the DIN output stage, no firmware work.
  Done.
- A pedal is USB-only and class-compliant: add the PIO-USB host path.
- Mixed DIN + USB at once: DIN path as above, plus the host path, plus a
  config-model destination decision (start at option 1 if the rig is small).
- A pedal is USB-only and not class-compliant: it cannot be driven; there is no
  software fix.

## To verify before building (do not trust memory)

- Exact MIDI-OUT resistor values for a 3.3 V vs 5 V driver: current MIDI
  Association electrical spec.
- `pico-pio-usb` current RP2350 support and pin constraints:
  [Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB)
- TinyUSB host MIDI and hub examples (`CFG_TUH_MIDI`, `CFG_TUH_HUB`) for the
  forwarding and enumeration code.
