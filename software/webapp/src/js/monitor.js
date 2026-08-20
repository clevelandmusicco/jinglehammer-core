// ---- frame monitor ----------------------------------------------------------
// Taps Link.send (tx) and Link._parse (rx) so every wire frame lands here
// verbatim. Header bytes are color-grouped to make the framing legible; large
// payloads (a full READ blob is ~3.1 KB) are truncated until you expand them.
const CMD_NAME = Object.fromEntries(Object.entries(CMD).map(([k, v]) => [v, k]));
const monLog = $("mon-log");
let monCount = 0;
const PREVIEW = 64; // payload bytes shown before the "+N more" expander

const hb = (b) => b.toString(16).toUpperCase().padStart(2, "0");
const hexSpan = (cls, bytes) => `<span class="${cls}">${Array.from(bytes, hb).join(" ")}</span>`;

function logFrame(dir, bytes) {
  const tx = dir === "tx";
  const cmd = bytes[2];
  const status = tx ? null : bytes[3];
  const hdrLen = tx ? 5 : 6;                 // M C cmd [status] len_lo len_hi
  const len = bytes[hdrLen - 2] | (bytes[hdrLen - 1] << 8);
  const payload = bytes.slice(hdrLen);

  const hdrSpans = [hexSpan("sof", bytes.slice(0, 2)), hexSpan("h-cmd", bytes.slice(2, 3))];
  if (!tx) hdrSpans.push(hexSpan("h-status", bytes.slice(3, 4)));
  hdrSpans.push(hexSpan("h-len", bytes.slice(hdrLen - 2, hdrLen)));
  const hdrHex = hdrSpans.join(" ");

  const now = new Date();
  const time = now.toTimeString().slice(0, 8) + "." + String(now.getMilliseconds()).padStart(3, "0");

  const row = document.createElement("div");
  row.className = "mon-row " + dir + (status ? " bad" : "");
  row.innerHTML =
    `<div class="mon-head">` +
      `<span class="mon-time">${time}</span>` +
      `<span class="mon-dir">${tx ? "TX&rarr;" : "&larr;RX"}</span>` +
      `<span class="mon-cmd">${CMD_NAME[cmd] || "0x" + hb(cmd)}</span>` +
      (tx ? "" : `<span class="mon-meta">${ST_NAME[status] ?? "status " + status}</span>`) +
      `<span class="mon-meta">${len} byte${len === 1 ? "" : "s"} payload</span>` +
    `</div>` +
    `<div class="mon-hex">${hdrHex}${payload.length <= PREVIEW
      ? (payload.length ? " " + hexSpan("pay", payload) : "")
      : ` ${hexSpan("pay", payload.slice(0, PREVIEW))} <span class="mon-more">+${payload.length - PREVIEW} more</span>`}</div>`;

  const more = row.querySelector(".mon-more");
  if (more) more.addEventListener("click", () => {
    row.querySelector(".mon-hex").innerHTML = hdrHex + " " + hexSpan("pay", payload);
  });

  if (monCount === 0) monLog.innerHTML = "";
  monLog.appendChild(row);
  monCount++;
  while (monLog.childElementCount > 500) monLog.removeChild(monLog.firstChild); // bound memory
  $("mon-count").textContent = `${monCount} frame${monCount === 1 ? "" : "s"}`;
  if ($("mon-autoscroll").checked) monLog.scrollTop = monLog.scrollHeight;
}
link.onFrame = logFrame;

$("btn-frames").addEventListener("click", () => {
  const panel = $("frames");
  panel.hidden = !panel.hidden;
  $("btn-frames").classList.toggle("primary", !panel.hidden);
  if (!panel.hidden && $("mon-autoscroll").checked) monLog.scrollTop = monLog.scrollHeight;
});
$("mon-clear").addEventListener("click", () => {
  monLog.innerHTML = `<div class="mon-empty">No frames yet. Connect and read.</div>`;
  monCount = 0;
  $("mon-count").textContent = "0 frames";
});
