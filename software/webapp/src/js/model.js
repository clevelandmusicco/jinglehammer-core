// ---- model <-> blob ---------------------------------------------------------
// state.blob holds the full config_t from the last READ. editBank is the bank
// shown in the editor; activeBank is the device's runtime/boot bank (the header
// field). Paging flips editBank only; "Set as active" moves activeBank. Edits to
// every bank accumulate in state.blob and ship together in one WRITE + SAVE.
// state.switches is a decoded PRESET_T per switch, so a card reads sw.name /
// sw.pc / sw.cc straight out of the struct with no adapter in between.
const state = { blob: null, activeBank: 0, editBank: 0, switches: [], dirty: false };

const dvOf = (blob) => new DataView(blob.buffer, blob.byteOffset, blob.byteLength);
const OFF_ACTIVE_BANK = offsetOf(["active_bank"]);
const OFF_CRC32       = offsetOf(["crc32"]);

// Offset of one switch's preset_t. Addressing the preset rather than the whole
// switch_cfg_t deliberately leaves the leading `mode` byte alone: it is reserved
// for future switch behaviours, this app has no opinion on it, and a
// read-modify-write here must not zero it.
const presetOff = (bank, sw) => offsetOf(["bank", bank, "sw", sw, "preset"]);

function readActiveBankHeader(blob) {
  state.activeBank = dvOf(blob).getUint16(OFF_ACTIVE_BANK, true);
}

// Parse one bank's NUM_SWITCHES presets out of the blob into state.switches.
function parseBank(blob, bankIdx) {
  state.switches = [];
  for (let s = 0; s < NUM_SWITCHES; s++) state.switches.push(decode(PRESET_T, blob, presetOff(bankIdx, s)));
}

// Serialise state.switches back into bankIdx. Leaves the CRC alone - call
// stampCrc once the blob is final (just before WRITE), not per bank.
function flushBank(blob, bankIdx) {
  for (let s = 0; s < NUM_SWITCHES; s++) encode(PRESET_T, state.switches[s], blob, presetOff(bankIdx, s));
}

// A preset_t image (one switch) for CMD.TEST, straight from the on-screen values
// - no blob, no flush, no dirty side effects. name[] ships as zeros; the device
// ignores it on test.
const presetImage = (sw) => encode(PRESET_T, { ...sw, name: "" }, new Uint8Array(PRESET_SIZE));

// Stamp the trailing CRC over every byte before it - the device re-validates this.
function stampCrc(blob) {
  dvOf(blob).setUint32(OFF_CRC32, crc32(blob, OFF_CRC32), true);
}
