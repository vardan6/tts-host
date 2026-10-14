const assert = require("node:assert/strict");
const { readFileSync } = require("node:fs");
const { resolve } = require("node:path");
const vm = require("node:vm");
const { test } = require("node:test");

async function browser() {
  const elements = Object.fromEntries(["pause", "resume", "stop", "state", "position", "error"].map(id =>
    [id, { disabled: true, textContent: "", addEventListener(event, fn) { this[event] = fn; } }]));
  const sockets = [];
  const intervals = new Set();
  const timeouts = new Set();
  const events = {};
  class Socket {
    static OPEN = 1;
    readyState = 0;
    sent = [];
    constructor(url) { this.url = url; sockets.push(this); }
    send(message) { this.sent.push(JSON.parse(message)); }
    close() { this.readyState = 3; this.onclose?.(); }
    open() { this.readyState = 1; this.onopen(); }
    status(state, error = "") {
      this.onmessage({ data: JSON.stringify({ state, error, positionSeconds: 3, generatedSeconds: 12 }) });
    }
  }
  const sandbox = {
    document: { getElementById: id => elements[id] },
    window: { addEventListener: (name, fn) => { events[name] = fn; } },
    chrome: { storage: { sync: { get: async () => ({ port: 8123 }) } } },
    WebSocket: Socket,
    setInterval: fn => { intervals.add(fn); return fn; },
    clearInterval: fn => intervals.delete(fn),
    setTimeout: fn => { timeouts.add(fn); return fn; },
    clearTimeout: fn => timeouts.delete(fn)
  };
  vm.runInNewContext(readFileSync(resolve(__dirname, "../extension/playback.js"), "utf8"), sandbox);
  await new Promise(resolve => setImmediate(resolve));
  return { elements, sockets, intervals, timeouts, events };
}

test("controls use configured port, reflect Host state, and send commands", async () => {
  const { elements, sockets, intervals } = await browser();
  const socket = sockets[0];
  assert.equal(socket.url, "ws://127.0.0.1:8123/v1/playback");
  socket.open();
  assert.equal(socket.sent[0].command, "status");
  socket.status("playing");
  assert.equal(elements.pause.disabled, false);
  assert.equal(elements.resume.disabled, true);
  assert.equal(elements.stop.disabled, false);
  assert.match(elements.position.textContent, /3.0 s played.*12.0 s generated/);
  elements.pause.click();
  assert.equal(socket.sent.at(-1).command, "pause");
  socket.status("paused");
  assert.equal(elements.pause.disabled, true);
  assert.equal(elements.resume.disabled, false);
  elements.resume.click();
  assert.equal(socket.sent.at(-1).command, "resume");
  elements.stop.click();
  assert.equal(socket.sent.at(-1).command, "stop");
  for (const poll of intervals) poll();
  assert.equal(socket.sent.at(-1).command, "status");
  for (const state of ["idle", "stopping", "error"]) {
    socket.status(state, state === "error" ? "No output device" : "");
    assert.ok(elements.pause.disabled && elements.resume.disabled && elements.stop.disabled);
  }
  assert.equal(elements.error.textContent, "No output device");
});

test("disconnect disables controls and retries; closing page does not stop speech", async () => {
  const { elements, sockets, intervals, timeouts, events } = await browser();
  const first = sockets[0];
  first.open();
  first.status("playing");
  first.close();
  assert.equal(elements.state.textContent, "Disconnected");
  assert.ok(elements.pause.disabled && elements.resume.disabled && elements.stop.disabled);
  assert.equal(intervals.size, 0);
  assert.equal(timeouts.size, 1);
  await [...timeouts][0]();
  const second = sockets[1];
  second.open();
  second.status("playing");
  events.pagehide();
  assert.equal(second.readyState, 3);
  assert.equal(intervals.size, 0);
  assert.equal(timeouts.size, 0);
  assert.ok(sockets.every(socket => socket.sent.every(message => message.command !== "stop")));
});
