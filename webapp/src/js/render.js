// ---- rendering --------------------------------------------------------------
const $ = (id) => document.getElementById(id);
const grid = $("grid");

function el(tag, props = {}, kids = []) {
  const n = document.createElement(tag);
  for (const [k, v] of Object.entries(props)) {
    if (k === "class") n.className = v;
    else if (k === "text") n.textContent = v;
    else if (k.startsWith("on")) n.addEventListener(k.slice(2), v);
    else n.setAttribute(k, v);
  }
  for (const c of [].concat(kids)) if (c) n.appendChild(c);
  return n;
}

function numField(label, value, lo, hi, onChange) {
  const inp = el("input", { type: "number", min: lo, max: hi, value });
  inp.addEventListener("change", () => { inp.value = clamp(+inp.value, lo, hi); onChange(+inp.value); markDirty(); });
  return el("div", { class: "field" }, [el("label", { text: label }), inp]);
}

function render() {
  grid.innerHTML = "";
  if (!state.blob) return;
  state.switches.forEach((sw, idx) => grid.appendChild(switchCard(sw, idx)));
}

// Numbered tabs 1..NUM_BANKS. Current bank is filled; the device's active bank
// wears a green ring + dot so you can tell "editing" from "what the pedal plays".
function renderBankBar() {
  const tabs = $("bank-tabs");
  tabs.innerHTML = "";
  for (let i = 0; i < NUM_BANKS; i++) {
    const cls = "small bank-tab" + (i === state.editBank ? " current" : "") + (i === state.activeBank ? " active" : "");
    tabs.appendChild(el("button", {
      class: cls,
      title: i === state.activeBank ? "Active bank: the pedal recalls this one" : `Edit bank ${i + 1}`,
      onclick: () => selectBank(i),
    }, [document.createTextNode(String(i + 1)), i === state.activeBank ? el("span", { class: "adot" }) : null]));
  }
  $("btn-setactive").disabled = state.editBank === state.activeBank;
  $("active-note").textContent = `Editing bank ${state.editBank + 1} · pedal active bank ${state.activeBank + 1}`;
}

// PC and CC are both 0..N lists of messages; one section builder renders either.
// `rowFn(item, i, items)` builds the per-message editor; `blank()` makes a new one.
function listSection(label, items, max, rowFn, blank) {
  const list = el("div", { class: "cc-list" });
  if (items.length === 0) {
    list.appendChild(el("div", { class: "cc-empty", text: `No ${label} messages.` }));
  } else {
    items.forEach((item, i) => list.appendChild(rowFn(item, i, items)));
  }

  const addBtn = el("button", {
    class: "small", text: `+ Add ${label}`,
    onclick: () => { items.push(blank()); markDirty(); render(); },
  });
  if (items.length >= max) { addBtn.disabled = true; addBtn.textContent = `Max ${max}`; }

  return el("div", {}, [
    el("div", { class: "row section-head" }, [el("span", { class: "section-label", text: `${label} (${items.length})` }), el("span", { class: "spacer" }), addBtn]),
    list,
  ]);
}

function pcRow(pc, i, items) {
  return el("div", { class: "cc-row" }, [
    el("span", { class: "idx", text: String(i + 1) }),
    numField("Channel (1-16)", pc.channel + 1, 1, 16, (v) => { pc.channel = v - 1; }),
    numField("Program (0-127)", pc.program, 0, 127, (v) => { pc.program = v; }),
    el("span", { class: "spacer" }),
    el("button", { class: "small danger", text: "Remove", onclick: () => { items.splice(i, 1); markDirty(); render(); } }),
  ]);
}

function ccRow(cc, i, items) {
  return el("div", { class: "cc-row" }, [
    el("span", { class: "idx", text: String(i + 1) }),
    numField("Channel (1-16)", cc.channel + 1, 1, 16, (v) => { cc.channel = v - 1; }),
    numField("CC # (0-127)", cc.controller, 0, 127, (v) => { cc.controller = v; }),
    numField("Value (0-127)", cc.value, 0, 127, (v) => { cc.value = v; }),
    el("span", { class: "spacer" }),
    el("button", { class: "small danger", text: "Remove", onclick: () => { items.splice(i, 1); markDirty(); render(); } }),
  ]);
}

// Free-text label shown on the controller's screen. maxlength caps visible
// chars at MAX_NAME-1; writeStr drops anything non-ASCII before it hits the blob.
function nameField(sw) {
  const inp = el("input", { type: "text", maxlength: MAX_NAME - 1, placeholder: "Unnamed", value: sw.name || "" });
  const count = el("span", { class: "count", text: `${(sw.name || "").length}/${MAX_NAME - 1}` });
  inp.addEventListener("input", () => { sw.name = inp.value; count.textContent = `${inp.value.length}/${MAX_NAME - 1}`; markDirty(); });
  return el("div", { class: "field name-field" }, [
    el("div", { class: "row" }, [el("label", { text: "Name (shown on controller)" }), el("span", { class: "spacer" }), count]),
    inp,
  ]);
}

function switchCard(sw, idx) {
  const pcSection = listSection("Program Change", sw.pc, MAX_PC, pcRow, () => ({ channel: 0, program: 0 }));
  const ccSection = listSection("Control Change", sw.cc, MAX_CC, ccRow, () => ({ channel: 0, controller: 0, value: 0 }));

  const testBtn = el("button", {
    class: "small test", text: "Test",
    title: "Play this bundle now over MIDI - on-screen values, no save, no LED change",
    onclick: () => testSwitch(sw, idx),
  });
  testBtn.disabled = !link.connected;

  return el("div", { class: "card" }, [
    el("div", { class: "head" }, [
      el("span", { class: "swnum", text: String(idx + 1) }),
      el("h2", { text: `Footswitch ${idx + 1}` }),
      el("span", { class: "spacer" }),
      testBtn,
    ]),
    el("div", { class: "body" }, [nameField(sw), pcSection, ccSection]),
  ]);
}
