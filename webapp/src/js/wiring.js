// ---- boot wiring -----------------------------------------------------------
$("btn-connect").addEventListener("click", () => doConnect().catch((e) => banner(String(e), true)));
$("btn-read").addEventListener("click", () => {
  if (state.dirty && !confirm("Discard unsaved edits and re-read from the pedal?")) return;
  readFromDevice().catch((e) => { status("Read failed", "err"); banner(String(e.message || e), true); });
});
$("btn-save").addEventListener("click", writeAndSave);
$("btn-setactive").addEventListener("click", setActiveBank);
$("btn-factory").addEventListener("click", factoryReset);

// Last-ditch guard: browser-native prompt if the tab is closed/reloaded dirty.
window.addEventListener("beforeunload", (e) => { if (state.dirty) { e.preventDefault(); e.returnValue = ""; } });

if (!navigator.serial) banner("This browser has no WebSerial. Use Chrome or Edge to connect.", true);
