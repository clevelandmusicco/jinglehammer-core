// ---- bank paging ------------------------------------------------------------
// Switching banks folds the on-screen edits into the in-RAM blob first, so
// nothing is lost when you come back. The blob is the source of truth between
// page flips; the device only sees it on WRITE + SAVE.
function selectBank(idx) {
  if (idx === state.editBank) return;
  flushBank(state.blob, state.editBank);
  state.editBank = idx;
  parseBank(state.blob, idx);
  renderBankBar();
  render();
}

// Point the device's runtime/boot bank at whatever bank is on screen. This is
// just another pending edit to the header - it commits on the next Save.
function setActiveBank() {
  if (state.activeBank === state.editBank) return;
  state.activeBank = state.editBank;
  dvOf(state.blob).setUint16(OFF_ACTIVE_BANK, state.activeBank, true);
  markDirty();
  renderBankBar();
}

function markDirty() { if (!state.dirty) { state.dirty = true; reflectDirty(); } }
function clearDirty() { state.dirty = false; reflectDirty(); }
function reflectDirty() { $("btn-save").textContent = state.dirty ? "Write + Save *" : "Write + Save"; }
