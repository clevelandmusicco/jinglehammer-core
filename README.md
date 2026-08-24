# Jinglehammer Core - Programmable MIDI Controller

The Jinglehammer Core is a 4 footswitch x 8 bank MIDI controller initially
designed for use with guitar effect pedals, but it can be used to control
anything that understands MIDI messages. Press a footswitch and it sends a
configurable bundle of Program Change and Control Change messages, with each
message free to target its own channel. Configuration lives in onboard flash
and survives power cycles; you edit it from a browser over USB, so there's no
separate app to install.

This repo holds the whole project: `software/` (RP2350 firmware plus the
browser-based config editor), `hardware/` (enclosure CAD and PCB design), and
`docs/` (design notes not tied to a single build artifact).

- Firmware is C + CMake with a VSCode workspace file provided
- Configuration webapp is simple HTML + Javascript with Python used for
  "building"
- PCB sources are in KiCad format and the Gerbers are tested at JLCPCB
- The enclosure source is provided in FreeCAD format, with STEP and 3MF files
  for use in your slicer of choice
  
> [!NOTE]
> Pictures of the hardware and the webapp coming soon.
