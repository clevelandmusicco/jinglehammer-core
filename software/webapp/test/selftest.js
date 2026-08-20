// Self-test for the config editor. Run via ../test.py (needs gjs; see below).
//
// There is no browser in the build environment and `node` is a broken snap on
// this box, so gjs (SpiderMonkey) is what we have. It gives a real JS engine
// plus Uint8Array/DataView, which is everything the codec touches.
//
// Two halves:
//   1. codec  - loads src/js/{config-schema,crc32,model}.js (no DOM in any of
//               them) and checks the schema table against byte offsets worked
//               out by hand, so a wrong CONFIG_T can't quietly agree with itself.
//   2. load   - runs the *built* index.html top to bottom against a stub DOM,
//               catching reference errors and top-level throws.

const GLib = imports.gi.GLib;
const read = (p) => imports.byteArray.toString(GLib.file_get_contents(p)[1]);

let fails = [];
const ok = (cond, msg) => { if (!cond) fails.push(msg); };
const eq = (got, want, msg) => ok(got === want, `${msg}: got ${got}, want ${want}`);
const bytesEq = (a, b) => {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
};

// ---- layout, computed by hand from config.h - deliberately NOT derived ------
// from the schema, or the test would just be asking the code to agree with
// itself. Update these when config_t changes; that is the point of them.
const NUM_SWITCHES = 4, NUM_BANKS = 8, MAX_PC = 16, MAX_CC = 16, MAX_NAME = 16;
const PRESET_SIZE = MAX_NAME + 1 + 2 * MAX_PC + 1 + 3 * MAX_CC;   // 98
const SWITCH_SIZE = 1 + PRESET_SIZE;                              // 99, leading `mode`
const BANK_SIZE   = SWITCH_SIZE * NUM_SWITCHES;                   // 396
const HDR         = 4 + 2 + 2;                                    // magic, version, active_bank
const BLOB_SIZE   = HDR + BANK_SIZE * NUM_BANKS + 4;              // 3180

// ---- half 1: codec ---------------------------------------------------------
const SRC = ["src/js/config-schema.js", "src/js/crc32.js", "src/js/model.js"];
const EXPORTS = ["state", "parseBank", "flushBank", "presetImage", "stampCrc", "readActiveBankHeader",
                 "crc32", "offsetOf", "presetOff", "decode", "encode", "PRESET_T", "CONFIG_T",
                 "BLOB_SIZE", "PRESET_SIZE"];
const C = new Function(SRC.map(read).join("\n") + "\nreturn {" + EXPORTS.join(",") + "};")();

eq(C.BLOB_SIZE, BLOB_SIZE, "BLOB_SIZE");
eq(C.PRESET_SIZE, PRESET_SIZE, "PRESET_SIZE");
eq(C.offsetOf(["magic"]), 0, "magic offset");
eq(C.offsetOf(["version"]), 4, "version offset");
eq(C.offsetOf(["active_bank"]), 6, "active_bank offset");
eq(C.offsetOf(["bank"]), HDR, "bank[] offset");
eq(C.offsetOf(["crc32"]), BLOB_SIZE - 4, "crc32 offset");
for (let b = 0; b < NUM_BANKS; b++)
  for (let s = 0; s < NUM_SWITCHES; s++)
    eq(C.presetOff(b, s), HDR + b * BANK_SIZE + s * SWITCH_SIZE + 1, `presetOff(${b},${s})`);
// the cc count byte, reached through a nested path
eq(C.offsetOf(["bank", 2, "sw", 3, "preset", "cc"]),
   HDR + 2 * BANK_SIZE + 3 * SWITCH_SIZE + 1 + MAX_NAME + 1 + MAX_PC * 2, "nested cc path");

// a typo'd path must throw, not silently resolve to offset 0
let threw = false;
try { C.offsetOf(["bnak", 0]); } catch (e) { threw = true; }
ok(threw, "offsetOf swallowed an unknown field name");

// round trip: model -> bytes -> model
{
  const blob = new Uint8Array(BLOB_SIZE);
  const model = [];
  for (let s = 0; s < NUM_SWITCHES; s++)
    model.push({ name: "sw" + s,
                 pc: [{ channel: s, program: 10 + s }],
                 cc: [{ channel: 15, controller: 7, value: 100 }, { channel: 0, controller: 1, value: 2 }] });
  C.state.switches = model;
  C.flushBank(blob, 5);
  C.parseBank(blob, 5);
  ok(JSON.stringify(C.state.switches) === JSON.stringify(model),
     "round trip lost data:\n  " + JSON.stringify(C.state.switches));
}

// over-capacity lists truncate to the cap; out-of-range values clamp
{
  const blob = new Uint8Array(BLOB_SIZE);
  const pc = [], cc = [];
  for (let i = 0; i < MAX_PC + 7; i++) pc.push({ channel: 99, program: 999 });
  for (let i = 0; i < MAX_CC + 7; i++) cc.push({ channel: -5, controller: -1, value: 300 });
  C.state.switches = [{ name: "x", pc, cc }, { name: "", pc: [], cc: [] },
                      { name: "", pc: [], cc: [] }, { name: "", pc: [], cc: [] }];
  C.flushBank(blob, 0);
  const o = C.presetOff(0, 0);
  eq(blob[o + MAX_NAME], MAX_PC, "pc_count clamped to capacity");
  eq(blob[o + MAX_NAME + 1], 15, "channel 99 clamped to 15");
  eq(blob[o + MAX_NAME + 2], 127, "program 999 clamped to 127");
  const cco = o + MAX_NAME + 1 + MAX_PC * 2;
  eq(blob[cco], MAX_CC, "cc_count clamped to capacity");
  eq(blob[cco + 1], 0, "channel -5 clamped to 0");
  eq(blob[cco + 3], 127, "value 300 clamped to 127");
}

// names: truncated to MAX_NAME-1 visible chars, always NUL-terminated in buffer,
// non-printable bytes dropped. The device rejects a blob whose name has no NUL.
{
  const blob = new Uint8Array(BLOB_SIZE);
  C.state.switches = [{ name: "0123456789abcdefghij", pc: [], cc: [] },
                      { name: "a\x01b\x7fc", pc: [], cc: [] },
                      { name: "", pc: [], cc: [] }, { name: "", pc: [], cc: [] }];
  C.flushBank(blob, 1);
  const o = C.presetOff(1, 0);
  eq(blob[o + MAX_NAME - 1], 0, "name buffer not NUL-terminated");
  C.parseBank(blob, 1);
  eq(C.state.switches[0].name, "0123456789abcde", "long name truncated to 15");
  eq(C.state.switches[1].name, "abc", "non-printable bytes dropped");
}

// a shrunk list must zero its tail, so identical models give identical bytes
{
  const a = new Uint8Array(BLOB_SIZE), b = new Uint8Array(BLOB_SIZE);
  for (let i = 0; i < a.length; i++) a[i] = 0xAA;               // stale garbage
  const blank = () => [0, 1, 2, 3].map(() => ({ name: "n", pc: [{ channel: 1, program: 2 }], cc: [] }));
  C.state.switches = [0, 1, 2, 3].map(() => ({ name: "n",
    pc: [{ channel: 1, program: 2 }, { channel: 3, program: 4 }], cc: [] }));
  C.flushBank(a, 3);
  C.state.switches = blank();
  C.flushBank(a, 3);                                            // shrink 2 -> 1
  C.state.switches = blank();
  C.flushBank(b, 3);                                            // fresh, same model
  // compare the preset_t regions only: the `mode` bytes between them are
  // untouched by flushBank by design, so they still hold each buffer's fill.
  for (let s = 0; s < NUM_SWITCHES; s++) {
    const o = C.presetOff(3, s);
    ok(bytesEq(a.slice(o, o + PRESET_SIZE), b.slice(o, o + PRESET_SIZE)),
       `sw${s}: shrinking a list left stale bytes behind`);
  }
}

// flushBank must not touch the reserved `mode` byte, nor any other bank
{
  const blob = new Uint8Array(BLOB_SIZE);
  for (let i = 0; i < blob.length; i++) blob[i] = 0x5A;
  const before = blob.slice();
  C.state.switches = [0, 1, 2, 3].map(() => ({ name: "z", pc: [], cc: [] }));
  C.flushBank(blob, 4);
  const lo = HDR + 4 * BANK_SIZE, hi = lo + BANK_SIZE;
  ok(bytesEq(blob.slice(0, lo), before.slice(0, lo)) && bytesEq(blob.slice(hi), before.slice(hi)),
     "flushBank wrote outside its bank");
  for (let s = 0; s < NUM_SWITCHES; s++)
    eq(blob[lo + s * SWITCH_SIZE], 0x5A, `sw${s} reserved mode byte clobbered`);
}

// CRC lands in the last four bytes and covers everything before it
{
  const blob = new Uint8Array(BLOB_SIZE);
  C.stampCrc(blob);
  const first = blob.slice(BLOB_SIZE - 4);
  ok(!bytesEq(first, new Uint8Array(4)), "CRC of a zero blob is suspiciously zero");
  blob[HDR] ^= 0xFF;                                            // touch a body byte
  C.stampCrc(blob);
  ok(!bytesEq(first, blob.slice(BLOB_SIZE - 4)), "CRC did not change after a body edit");
}

// active_bank reads as little-endian u16 at offset 6
{
  const blob = new Uint8Array(BLOB_SIZE);
  blob[6] = 6; blob[7] = 0;
  C.readActiveBankHeader(blob);
  eq(C.state.activeBank, 6, "active_bank read");
}

// presetImage is a bare preset_t with a zeroed name
{
  const img = C.presetImage({ name: "ignored", pc: [{ channel: 2, program: 3 }], cc: [] });
  eq(img.length, PRESET_SIZE, "presetImage length");
  for (let i = 0; i < MAX_NAME; i++) eq(img[i], 0, `presetImage name byte ${i} not zero`);
  eq(img[MAX_NAME], 1, "presetImage pc_count");
  eq(img[MAX_NAME + 1], 2, "presetImage pc channel");
}

// ---- half 2: the built page loads ------------------------------------------
const node = () => ({
  style: {}, classList: { add() {}, remove() {}, toggle() {} }, dataset: {},
  children: [], attributes: {},
  appendChild(c) { this.children.push(c); return c; },
  append(...c) { this.children.push(...c); },
  remove() {}, addEventListener() {}, removeEventListener() {},
  setAttribute(k, v) { this.attributes[k] = v; }, getAttribute(k) { return this.attributes[k]; },
  querySelector: () => node(), querySelectorAll: () => [],
  insertAdjacentHTML() {}, focus() {}, click() {}, scrollIntoView() {},
  textContent: "", innerHTML: "", className: "", value: "", disabled: false, hidden: false,
});
const stub = {
  document: { getElementById: () => node(), createElement: () => node(), createTextNode: () => node(),
              querySelector: () => node(), querySelectorAll: () => [], addEventListener() {}, body: node() },
  window: { addEventListener() {} },
  navigator: { serial: { requestPort: () => {}, addEventListener() {} } },
  alert: () => {}, confirm: () => true,
  setTimeout: () => 0, clearTimeout: () => {}, setInterval: () => 0,
  TextEncoder: function () { this.encode = (s) => new Uint8Array(s.length); },
  TextDecoder: function () { this.decode = () => ""; },
};
{
  const html = read("index.html");
  const m = html.match(/<script>\n([\s\S]*)\n<\/script>/);
  if (!m) fails.push("index.html has no <script> block - did build.py run?");
  else {
    const names = Object.keys(stub);
    try {
      new Function(...names, m[1])(...names.map((n) => stub[n]));
    } catch (e) {
      fails.push("index.html failed to load: " + e + "\n" + e.stack);
    }
  }
}

if (fails.length) {
  print(fails.length + " FAILURE" + (fails.length === 1 ? "" : "S"));
  fails.forEach((f) => print("  - " + f));
  imports.system.exit(1);
}
print("PASS  codec offsets, clamping, round trip, CRC; index.html loads clean");
