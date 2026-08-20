// ---- protocol + WebSerial link: one request, one framed response ----------
const SOF0 = 0x4D, SOF1 = 0x43;
const CMD = { HELLO: 0x01, READ: 0x02, WRITE: 0x03, SAVE: 0x04, FACTORY: 0x05, TEST: 0x06 };
const ST_NAME = { 0:"ok", 1:"bad length", 2:"rejected (validation failed)", 3:"flash write failed", 4:"unknown command" };

class Link {
  constructor() { this.port = null; this.reader = null; this.writer = null; this.rx = new Uint8Array(0); this.pending = null; this.onFrame = null; }

  get connected() { return !!this.port; }

  async connect() {
    this.port = await navigator.serial.requestPort();
    await this.port.open({ baudRate: 115200 }); // USB-CDC ignores the rate; any value opens the port
    this.reader = this.port.readable.getReader();
    this.writer = this.port.writable.getWriter();
    this.rx = new Uint8Array(0);
    this._loop();
  }

  async disconnect() {
    try { await this.reader?.cancel(); } catch {}
    try { this.reader?.releaseLock(); } catch {}
    try { await this.writer?.close(); } catch {}
    try { await this.port?.close(); } catch {}
    this.port = this.reader = this.writer = null;
  }

  async _loop() {
    try {
      for (;;) {
        const { value, done } = await this.reader.read();
        if (done) break;
        if (value && value.length) {
          const merged = new Uint8Array(this.rx.length + value.length);
          merged.set(this.rx); merged.set(value, this.rx.length);
          this.rx = merged;
          this._parse();
        }
      }
    } catch { /* port closed/cancelled */ }
  }

  _parse() {
    // resync to frame start
    while (this.rx.length >= 1 && this.rx[0] !== SOF0) this.rx = this.rx.slice(1);
    if (this.rx.length >= 2 && this.rx[1] !== SOF1) { this.rx = this.rx.slice(1); return this._parse(); }
    if (this.rx.length < 6) return;
    const cmd = this.rx[2], status = this.rx[3];
    const len = this.rx[4] | (this.rx[5] << 8);
    if (this.rx.length < 6 + len) return;
    const payload = this.rx.slice(6, 6 + len);
    this.onFrame?.("rx", this.rx.slice(0, 6 + len));
    this.rx = this.rx.slice(6 + len);
    const p = this.pending; this.pending = null;
    if (p) p({ cmd, status, payload });
    if (this.rx.length) this._parse();
  }

  send(cmd, payload = new Uint8Array(0)) {
    const frame = new Uint8Array(5 + payload.length);
    frame[0] = SOF0; frame[1] = SOF1; frame[2] = cmd;
    frame[3] = payload.length & 0xFF; frame[4] = (payload.length >> 8) & 0xFF;
    frame.set(payload, 5);
    this.onFrame?.("tx", frame);
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => { this.pending = null; reject(new Error("timeout waiting for device")); }, 4000);
      this.pending = (resp) => { clearTimeout(timer); resolve(resp); };
      this.writer.write(frame).catch((e) => { clearTimeout(timer); this.pending = null; reject(e); });
    });
  }
}

const link = new Link();
