const controls = ["pause", "resume", "stop"];
let socket;
let pollTimer;
let reconnectTimer;
let closed = false;

function disableControls() {
  for (const command of controls) document.getElementById(command).disabled = true;
}

function send(command) {
  if (socket?.readyState === WebSocket.OPEN) {
    socket.send(JSON.stringify({ command }));
  }
}

function showStatus(status) {
  if (status.error && !status.state) {
    document.getElementById("error").textContent = status.error;
    return;
  }
  const names = {
    idle: "Idle", preparing: "Preparing speech", playing: "Playing",
    paused: "Paused", stopping: "Stopping", error: "Playback failed"
  };
  if (!Object.hasOwn(names, status.state)) throw new Error("Invalid playback state");
  document.getElementById("state").textContent = names[status.state];
  document.getElementById("position").textContent =
    `${Number(status.positionSeconds).toFixed(1)} s played · ${Number(status.generatedSeconds).toFixed(1)} s generated`;
  document.getElementById("error").textContent = status.error || "";
  document.getElementById("pause").disabled = !["preparing", "playing"].includes(status.state);
  document.getElementById("resume").disabled = status.state !== "paused";
  document.getElementById("stop").disabled = !["preparing", "playing", "paused"].includes(status.state);
}

async function connect() {
  clearTimeout(reconnectTimer);
  clearInterval(pollTimer);
  disableControls();
  const { port = 7861 } = await chrome.storage.sync.get({ port: 7861 });
  if (closed) return;
  const connection = new WebSocket(`ws://127.0.0.1:${port}/v1/playback`);
  socket = connection;
  connection.onopen = () => {
    if (socket !== connection) return;
    send("status");
    pollTimer = setInterval(() => send("status"), 500);
  };
  connection.onmessage = (event) => {
    if (socket !== connection) return;
    try { showStatus(JSON.parse(event.data)); }
    catch { document.getElementById("error").textContent = "Invalid response from TTS Host."; }
  };
  connection.onerror = () => connection.close();
  connection.onclose = () => {
    if (socket !== connection || closed) return;
    clearInterval(pollTimer);
    disableControls();
    document.getElementById("state").textContent = "Disconnected";
    document.getElementById("position").textContent = "";
    document.getElementById("error").textContent =
      `Could not connect on port ${port}. Start TTS Host and allow this extension's origin in Host settings. Retrying…`;
    reconnectTimer = setTimeout(connect, 2000);
  };
}

for (const command of controls) {
  document.getElementById(command).addEventListener("click", () => send(command));
}
window.addEventListener("pagehide", () => {
  closed = true;
  clearInterval(pollTimer);
  clearTimeout(reconnectTimer);
  socket?.close();
});
void connect();
