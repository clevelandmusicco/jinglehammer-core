// ---- commands --------------------------------------------------------------
async function expectOk(label, cmd, payload) {
  const r = await link.send(cmd, payload);
  if (r.status !== 0) throw new Error(`${label}: ${ST_NAME[r.status] || "status " + r.status}`);
  return r;
}

async function doConnect() {
  if (link.connected) {
    if (state.dirty && !confirm("You have unsaved edits. Disconnect and lose them?")) return;
    await link.disconnect();
    state.blob = null; clearDirty();
    $("bankbar").hidden = true; grid.innerHTML = "";
    setControls(false); status("Not connected"); return;
  }
  if (!navigator.serial) { banner("This browser has no WebSerial. Use Chrome or Edge.", true); return; }
  try {
    banner("");
    status("Connecting...", "busy");
    await link.connect();
    const hello = await expectOk("HELLO", CMD.HELLO);
    const protoVer  = hello.payload[0];
    const cfgVer    = hello.payload[1] | (hello.payload[2] << 8);
    const blobSize  = hello.payload[3] | (hello.payload[4] << 8);
    if (blobSize !== BLOB_SIZE || cfgVer !== CONFIG_VER) {
      await link.disconnect();
      status("Version mismatch", "err");
      banner(`Device config layout differs (device: v${cfgVer}/${blobSize}B, app: v${CONFIG_VER}/${BLOB_SIZE}B). Update the app.`, true);
      return;
    }
    void protoVer;
    setControls(true);
    await readFromDevice();
  } catch (e) {
    status("Connection failed", "err");
    banner(String(e.message || e), true);
    try { await link.disconnect(); } catch {}
    setControls(false);
  }
}

async function readFromDevice() {
  status("Reading...", "busy");
  const r = await expectOk("READ", CMD.READ);
  if (r.payload.length !== BLOB_SIZE) throw new Error(`READ returned ${r.payload.length} bytes, expected ${BLOB_SIZE}`);
  state.blob = r.payload;
  readActiveBankHeader(state.blob);
  state.editBank = state.activeBank; // open on the bank the pedal boots into
  parseBank(state.blob, state.editBank);
  clearDirty();
  $("bankbar").hidden = false;
  renderBankBar();
  render();
  status("Connected", "ok");
  banner("");
}

async function writeAndSave() {
  try {
    flushBank(state.blob, state.editBank); // fold the on-screen bank into the blob
    stampCrc(state.blob);
    status("Writing...", "busy");
    await expectOk("WRITE", CMD.WRITE, state.blob);
    status("Saving...", "busy");
    await expectOk("SAVE", CMD.SAVE);
    clearDirty();
    status("Saved to flash", "ok");
    banner(`All ${NUM_BANKS} banks written and saved. Pedal active bank: ${state.activeBank + 1}.`);
  } catch (e) {
    status("Save failed", "err");
    banner(String(e.message || e), true);
  }
}

async function factoryReset() {
  if (!confirm("Reset the controller to factory defaults and overwrite saved config? This cannot be undone.")) return;
  try {
    status("Resetting...", "busy");
    await expectOk("FACTORY", CMD.FACTORY); // loads defaults into device RAM
    await expectOk("SAVE", CMD.SAVE);       // persist them
    await readFromDevice();                 // show what the device now holds
    banner("Controller reset to factory defaults.");
  } catch (e) {
    status("Reset failed", "err");
    banner(String(e.message || e), true);
  }
}

// Per-card "Test": fire one switch's bundle on the device using the values
// currently on screen. The device emits over its normal MIDI path without
// touching stored config or LEDs, so this works on any bank and with unsaved
// edits. No SAVE, no flash write.
async function testSwitch(sw, idx) {
  if (!link.connected) return;
  try {
    status(`Testing footswitch ${idx + 1}...`, "busy");
    await expectOk("TEST", CMD.TEST, presetImage(sw));
    status(`Sent footswitch ${idx + 1}`, "ok");
  } catch (e) {
    status("Test failed", "err");
    banner(String(e.message || e), true);
  }
}
