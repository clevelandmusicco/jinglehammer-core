# Config editor

`index.html` is the editor for the pedal's presets, banks, and channels. It
talks to the unit over USB-CDC using WebSerial, so it opens straight off disk
with nothing installed. See the [software README](../README.md) for the pedal
itself and [`../firmware/`](../firmware/README.md) for the device side.

## Building

`index.html` is generated from `src/` and is not checked in, so build it once
after cloning:

```sh
./build.py
```

That inlines the template, CSS, and JS modules into one self-contained file. No
npm and no bundler; it is plain Python 3 stdlib, so it runs the same on
Linux, macOS, and Windows. The app has to ship as a single file because
WebSerial works from `file://` but ES modules do not.

- `./build.py --check` exits 1 if `index.html` is stale.
- `./test.py` rebuilds, then self-tests the byte codec and page load. Needs
  `gjs` (Debian/Ubuntu: `apt install gjs`).

Both scripts resolve their own directory from the script path, so they run
from any working directory.

Edit `src/`. Anything written to `index.html` is lost on the next build.

## Source layout

```
src/index.template.html   page skeleton plus <!-- include: ... --> lines
src/style.css
src/js/config-schema.js   field table mirroring config_t, plus decode/encode
src/js/crc32.js           CRC-32 matching the firmware's
src/js/model.js           in-RAM blob and edit folding
src/js/protocol.js        CDC frame codec
src/js/commands.js        HELLO / READ / WRITE / SAVE / FACTORY / TEST
src/js/monitor.js         raw-frame monitor, taps Link.send and Link._parse
src/js/paging.js          bank tabs, edit vs active bank
src/js/render.js          DOM building
src/js/status.js          status pill and banner
src/js/wiring.js          button listeners and startup
test/selftest.js          codec offsets and page-load test, run by test.py
```

`config-schema.js` mirrors the firmware's `config_t` name-for-name, and every
offset in the app derives from that one table. The constants in
`test/selftest.js` are hand-computed on purpose, so the schema cannot silently
agree with itself. A format change means editing both, plus `CONFIG_VER`.

## Editing presets

Open `index.html` in a Chromium-based browser (Chrome or Edge; Firefox and
Safari do not ship WebSerial). Plug the controller in over USB, then:

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
