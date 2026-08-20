// ---- status / banner --------------------------------------------------------
function status(text, kind = "") { $("status").className = "pill " + kind; $("status-text").textContent = text; }
function banner(msg, isErr = false) {
  const b = $("banner");
  if (!msg) { b.className = "banner"; b.textContent = ""; return; }
  b.className = "banner show" + (isErr ? " err" : "");
  b.textContent = msg;
}

function setControls(connected) {
  $("btn-read").disabled = !connected;
  $("btn-save").disabled = !connected;
  $("btn-factory").disabled = !connected;
  $("btn-connect").textContent = connected ? "Disconnect" : "Connect";
}
