const $ = (id) => document.getElementById(id);
const logEl = $("log");
const preview = $("preview");
const stateEl = $("state");
const dot = $("dot");
const metaEl = $("meta");
const titleEl = $("title");

function setState(state) {
  stateEl.textContent = `${state.state || "idle"}${state.port ? ` @ ${state.port}` : ""}`;
  dot.className = "dot";
  if (state.state === "running") dot.classList.add("ok");
  else if (state.state === "flashing" || state.state === "opening" || state.state === "rebooting") dot.classList.add("warn");
  else if (state.state === "idle") dot.classList.add("err");
  metaEl.textContent = JSON.stringify(state, null, 2);
  if (state.projectName) {
    titleEl.textContent = state.projectName;
  }
}

function appendLog(text) {
  logEl.textContent += `${text}\n`;
  logEl.scrollTop = logEl.scrollHeight;
}

async function requestJson(path, init = {}) {
  const res = await fetch(path, init);
  return await res.json();
}

async function refreshState() {
  setState(await requestJson("/api/state"));
}

async function startSession() {
  const body = {
    port: $("port").value.trim(),
    baudrate: Number($("baudrate").value || 921600),
  };
  setState(await requestJson("/api/session/start", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  }));
}

async function stopSession() {
  setState(await requestJson("/api/session/stop", { method: "POST" }));
}

async function flashFirmware() {
  const file = $("firmware")?.files?.[0];
  if (!file) {
    appendLog("[ui] 请先选择 bin 文件");
    return;
  }
  const buf = await file.arrayBuffer();
  setState(await requestJson(`/api/flash?port=${encodeURIComponent($("port").value.trim())}&baudrate=${encodeURIComponent($("baudrate").value || 921600)}`, {
    method: "POST",
    headers: { "Content-Type": "application/octet-stream" },
    body: buf,
  }));
}

async function screenshot() {
  const meta = await requestJson("/api/screenshot", { method: "POST" });
  appendLog(`[shot] ${JSON.stringify(meta)}`);
  preview.src = `/api/screenshot/latest?t=${Date.now()}`;
}

async function reboot() {
  setState(await requestJson("/api/reboot", { method: "POST" }));
}

async function pauseLogs() {
  setState(await requestJson("/api/log/pause", { method: "POST" }));
}

async function resumeLogs() {
  setState(await requestJson("/api/log/resume", { method: "POST" }));
}

function connectEvents() {
  const es = new EventSource("/api/events");
  es.addEventListener("status", (ev) => setState(JSON.parse(ev.data).payload));
  es.addEventListener("log", (ev) => appendLog(JSON.parse(ev.data).payload.line));
  es.addEventListener("screenshot", (ev) => {
    const payload = JSON.parse(ev.data).payload;
    appendLog(`[screenshot] ${payload.bmpPath}`);
    preview.src = `/api/screenshot/latest?t=${Date.now()}`;
  });
  es.addEventListener("flash_done", (ev) => {
    appendLog(`[flash] ${JSON.stringify(JSON.parse(ev.data).payload)}`);
  });
}

$("startBtn").addEventListener("click", startSession);
$("stopBtn").addEventListener("click", stopSession);
$("flashBtn").addEventListener("click", flashFirmware);
$("shotBtn").addEventListener("click", screenshot);
$("rebootBtn").addEventListener("click", reboot);
$("pauseBtn").addEventListener("click", pauseLogs);
$("resumeBtn").addEventListener("click", resumeLogs);
$("clearBtn").addEventListener("click", () => { logEl.textContent = ""; });

refreshState().catch(() => {});
connectEvents();

