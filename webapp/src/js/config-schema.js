// ---- config_t byte layout (mirrors config.h / board.h, little-endian) -------
// CONFIG_T below is the only description of the wire format: every size, offset
// and codec path derives from it, so adding a field means editing one table
// instead of hunting offset arithmetic in three places. Field names mirror
// config.h so a diff against the header reads name-for-name.
const NUM_SWITCHES = 4;
const NUM_BANKS    = 8;
const MAX_PC       = 16;                        // MAX_PC_PER_SWITCH
const MAX_CC       = 16;                        // MAX_CC_PER_SWITCH
const MAX_NAME     = 16;                        // MAX_NAME_LEN; buffer bytes incl. NUL, <=15 visible chars
const CONFIG_MAGIC = 0x4D494443;                // "MIDC"
const CONFIG_VER   = 3;

const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v | 0));

function sizeOf(fields) { let n = 0; for (const f of fields) n += f.size; return n; }

// Field kinds. `lo`/`hi` on a scalar are applied on encode - the device
// range-checks the same values and rejects the whole blob if one is out, so
// clamping here is what stops a stray UI value from failing a Save.
const u8  = (name, lo = 0, hi = 255) => ({ name, kind: "u8", size: 1, lo, hi });
const u16 = (name) => ({ name, kind: "u16", size: 2 });
const u32 = (name) => ({ name, kind: "u32", size: 4 });
const str = (name, size) => ({ name, kind: "str", size });                                    // fixed, NUL-terminated ASCII
const sub = (name, of) => ({ name, kind: "sub", of, size: sizeOf(of) });                      // nested struct
const arr = (name, len, of) => ({ name, kind: "arr", len, of, size: len * sizeOf(of) });      // C: T x[len]
const vec = (name, cap, of) => ({ name, kind: "vec", cap, of, size: 1 + cap * sizeOf(of) });  // C: uint8_t x_count; T x[cap]

const CH = (name) => u8(name, 0, 15);           // MIDI channel, stored 0..15
const D7 = (name) => u8(name, 0, 127);          // 7-bit MIDI data byte

const PC_MSG_T     = [CH("channel"), D7("program")];
const CC_MSG_T     = [CH("channel"), D7("controller"), D7("value")];
const PRESET_T     = [str("name", MAX_NAME), vec("pc", MAX_PC, PC_MSG_T), vec("cc", MAX_CC, CC_MSG_T)];
const SWITCH_CFG_T = [u8("mode"), sub("preset", PRESET_T)];
const BANK_CFG_T   = [arr("sw", NUM_SWITCHES, SWITCH_CFG_T)];
const CONFIG_T     = [
  u32("magic"), u16("version"), u16("active_bank"),
  arr("bank", NUM_BANKS, BANK_CFG_T),
  u32("crc32"),                                 // over every byte preceding it
];

const BLOB_SIZE   = sizeOf(CONFIG_T);           // 3180
const PRESET_SIZE = sizeOf(PRESET_T);           // 98

// Byte offset of a field path: strings name a field, numbers index an arr/vec.
// e.g. offsetOf(["bank", 3, "sw", 1, "preset"]). The only layout arithmetic in
// the app - everything else asks for a path.
function offsetOf(path, fields = CONFIG_T, off = 0) {
  let i = 0;
  while (i < path.length) {
    const key = path[i++];
    let f = null;
    for (const g of fields) { if (g.name === key) { f = g; break; } off += g.size; }
    if (!f) throw new Error(`offsetOf: no field "${key}"`);
    if (f.kind === "sub") { fields = f.of; continue; }
    if (f.kind === "arr" || f.kind === "vec") {
      if (i >= path.length) break;              // caller wants the array itself
      off += (f.kind === "vec" ? 1 : 0) + path[i++] * sizeOf(f.of);
      fields = f.of;
      continue;
    }
    break;                                      // scalar or str: the path ends here
  }
  return off;
}

// Read stops at the first NUL and keeps only printable ASCII, so a stray byte
// off the device can't smear the UI.
function readStr(buf, off, size) {
  let s = "";
  for (let i = 0; i < size; i++) {
    const b = buf[off + i];
    if (b === 0) break;
    s += (b >= 0x20 && b <= 0x7e) ? String.fromCharCode(b) : "";
  }
  return s;
}

// Write up to size-1 printable-ASCII chars, then NUL-pad the rest so the last
// byte is always 0 - the device rejects a name without an in-buffer NUL.
function writeStr(buf, off, size, s) {
  let n = 0;
  for (let i = 0; i < s.length && n < size - 1; i++) {
    const c = s.charCodeAt(i);
    if (c >= 0x20 && c <= 0x7e) buf[off + n++] = c;
  }
  for (; n < size; n++) buf[off + n] = 0;
}

// Decode `fields` at `off` into a plain object. A vec stops at its declared
// capacity, so a corrupt count byte can't walk off the end of the struct.
function decode(fields, buf, off = 0) {
  const out = {};
  for (const f of fields) {
    const o = off;
    off += f.size;
    switch (f.kind) {
      case "u8":  out[f.name] = buf[o]; break;
      case "u16": out[f.name] = buf[o] | (buf[o + 1] << 8); break;
      case "u32": out[f.name] = (buf[o] | (buf[o + 1] << 8) | (buf[o + 2] << 16) | (buf[o + 3] << 24)) >>> 0; break;
      case "str": out[f.name] = readStr(buf, o, f.size); break;
      case "sub": out[f.name] = decode(f.of, buf, o); break;
      case "arr": {
        const w = sizeOf(f.of), a = [];
        for (let i = 0; i < f.len; i++) a.push(decode(f.of, buf, o + i * w));
        out[f.name] = a;
        break;
      }
      case "vec": {
        const w = sizeOf(f.of), n = Math.min(buf[o], f.cap), a = [];
        for (let i = 0; i < n; i++) a.push(decode(f.of, buf, o + 1 + i * w));
        out[f.name] = a;
        break;
      }
    }
  }
  return out;
}

// Encode `obj` into `buf` at `off`, returning `buf`. Scalars clamp to their
// declared range and vec tails are zero-filled, so the same model always
// produces the same bytes - a shrunk list never leaves stale entries behind.
function encode(fields, obj, buf, off = 0) {
  for (const f of fields) {
    const o = off;
    off += f.size;
    const v = obj ? obj[f.name] : undefined;
    switch (f.kind) {
      case "u8":  buf[o] = clamp(v, f.lo, f.hi); break;
      case "u16": buf[o] = v & 0xff; buf[o + 1] = (v >>> 8) & 0xff; break;
      case "u32": buf[o] = v & 0xff; buf[o + 1] = (v >>> 8) & 0xff; buf[o + 2] = (v >>> 16) & 0xff; buf[o + 3] = (v >>> 24) & 0xff; break;
      case "str": writeStr(buf, o, f.size, v || ""); break;
      case "sub": encode(f.of, v, buf, o); break;
      case "arr": {
        const w = sizeOf(f.of);
        for (let i = 0; i < f.len; i++) encode(f.of, v && v[i], buf, o + i * w);
        break;
      }
      case "vec": {
        const w = sizeOf(f.of), n = Math.min((v && v.length) || 0, f.cap);
        buf[o] = n;
        for (let i = 0; i < f.cap; i++) encode(f.of, i < n ? v[i] : null, buf, o + 1 + i * w);
        break;
      }
    }
  }
  return buf;
}
