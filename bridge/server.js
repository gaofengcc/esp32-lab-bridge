#!/usr/bin/env node
import http from "node:http";
import https from "node:https";
import fs from "node:fs";
import fsp from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const DEFAULT_CONFIG = {
  projectName: "ESP32 Lab Bridge",
  host: "0.0.0.0",
  port: 3000,
  defaultPort: "COM5",
  defaultBaudrate: 921600,
  defaultChip: "esp32s3",
  screenshotTimeoutMs: 30000,
  serialBridgeScript: "serial_bridge.ps1",
  publicDir: "public",
  logsDir: "logs",
  capturesDir: "captures",
  esptoolExe: "",
  esptoolExeName: "esptool.exe",
};

const CDC_MAGIC_0 = 0xAB;
const CDC_MAGIC_1 = 0xCD;
const SCREEN_CMD_ID = 0x02;
const REBOOT_CMD_ID = 0x08;
const LOG_PAUSE_CMD_ID = 0x0A;
const LOG_RESUME_CMD_ID = 0x0B;
const MAX_CDC_PAYLOAD_BYTES = 4096;
const MAX_SCREENSHOT_BYTES = 10 * 1024 * 1024;
const SCREENSHOT_MAGIC = Buffer.from([0x1f, 0xe0, 0xa7, 0x5c]);
const SCREEN_CMD = Buffer.from("!SCN\r\n", "ascii");

function crc8Ccitt(bytes) {
  let crc = 0;
  for (const byte of bytes) {
    crc ^= byte;
    for (let i = 0; i < 8; i += 1) {
      crc = (crc & 0x80) ? ((crc << 1) ^ 0x07) : (crc << 1);
      crc &= 0xff;
    }
  }
  return crc;
}

function buildCdcFrame(cmd, payload = Buffer.alloc(0)) {
  const body = Buffer.concat([
    Buffer.from([CDC_MAGIC_0, CDC_MAGIC_1, cmd & 0xff, payload.length & 0xff, (payload.length >> 8) & 0xff]),
    Buffer.from(payload),
  ]);
  return Buffer.concat([body, Buffer.from([crc8Ccitt(body)])]);
}

function decodeStatus(status) {
  switch (status) {
    case 0x00: return "OK";
    case 0x01: return "INVALID";
    case 0x02: return "BUSY";
    case 0x03: return "NOT_FOUND";
    case 0x04: return "UNSUPPORTED";
    case 0x05: return "INTERNAL";
    default: return `0x${status.toString(16).padStart(2, "0")}`;
  }
}

function stripAnsi(text) {
  return String(text).replace(/\x1b\[[0-9;]*[a-zA-Z]/g, "");
}

function json(res, statusCode, data) {
  const body = JSON.stringify(data);
  res.writeHead(statusCode, {
    "Content-Type": "application/json; charset=utf-8",
    "Content-Length": Buffer.byteLength(body),
    "Cache-Control": "no-store",
  });
  res.end(body);
}

function text(res, statusCode, body, contentType = "text/plain; charset=utf-8") {
  res.writeHead(statusCode, {
    "Content-Type": contentType,
    "Content-Length": Buffer.byteLength(body),
    "Cache-Control": "no-store",
  });
  res.end(body);
}

function readConfig() {
  const configPath = path.join(__dirname, "config.json");
  const raw = fs.existsSync(configPath)
    ? fs.readFileSync(configPath, "utf8")
    : "{}";
  const parsed = JSON.parse(raw || "{}");
  return { ...DEFAULT_CONFIG, ...parsed };
}

function resolvePath(baseDir, value) {
  if (!value) {
    return "";
  }
  return path.isAbsolute(value) ? value : path.join(baseDir, value);
}

function serveStatic(publicDir, req, res) {
  let rel = req.url === "/" ? "/index.html" : req.url;
  rel = decodeURIComponent(rel.split("?")[0]);
  const filePath = path.normalize(path.join(publicDir, rel));
  const relative = path.relative(publicDir, filePath);
  if (relative.startsWith("..") || path.isAbsolute(relative)) {
    text(res, 403, "Forbidden");
    return;
  }
  fs.readFile(filePath, (err, data) => {
    if (err) {
      text(res, 404, "Not found");
      return;
    }
    const ext = path.extname(filePath).toLowerCase();
    const type =
      ext === ".html" ? "text/html; charset=utf-8" :
      ext === ".js" ? "application/javascript; charset=utf-8" :
      ext === ".css" ? "text/css; charset=utf-8" :
      ext === ".bmp" ? "image/bmp" :
      "application/octet-stream";
    res.writeHead(200, {
      "Content-Type": type,
      "Content-Length": data.length,
      "Cache-Control": "no-store",
    });
    res.end(data);
  });
}

function parseJsonBody(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    req.on("data", (chunk) => chunks.push(chunk));
    req.on("end", () => {
      if (chunks.length === 0) {
        resolve({});
        return;
      }
      try {
        resolve(JSON.parse(Buffer.concat(chunks).toString("utf8")));
      } catch (err) {
        reject(err);
      }
    });
    req.on("error", reject);
  });
}

function parseNumber(value, fallback) {
  const n = Number(value);
  return Number.isFinite(n) ? n : fallback;
}

async function readBody(req) {
  const chunks = [];
  for await (const chunk of req) {
    chunks.push(chunk);
  }
  return Buffer.concat(chunks);
}

class EventHub {
  constructor() {
    this.clients = new Set();
    this.history = [];
    this.maxHistory = 300;
  }

  emit(type, payload) {
    const event = { ts: new Date().toISOString(), type, payload };
    this.history.push(event);
    if (this.history.length > this.maxHistory) {
      this.history.shift();
    }
    const line = `event: ${type}\ndata: ${JSON.stringify(event)}\n\n`;
    for (const res of this.clients) {
      res.write(line);
    }
  }

  attach(res) {
    res.writeHead(200, {
      "Content-Type": "text/event-stream",
      "Cache-Control": "no-cache",
      Connection: "keep-alive",
    });
    res.write("\n");
    for (const event of this.history) {
      res.write(`event: ${event.type}\n`);
      res.write(`data: ${JSON.stringify(event)}\n\n`);
    }
    this.clients.add(res);
    res.on("close", () => this.clients.delete(res));
  }
}

class SerialBridge {
  constructor(hub, scriptPath, onData) {
    this.hub = hub;
    this.scriptPath = scriptPath;
    this.onData = onData;
    this.proc = null;
    this.stdoutBuffer = "";
    this.readyResolve = null;
    this.readyReject = null;
    this.opening = null;
    this.closing = false;
    this.portPath = "";
    this.baudrate = 0;
    this.startupError = "";
  }

  get isOpen() {
    return !!this.proc;
  }

  async open(portPath, baudrate) {
    if (!portPath) {
      throw new Error("port is required");
    }
    if (this.proc) {
      await this.close();
    }

    const args = [
      "-NoLogo",
      "-NoProfile",
      "-ExecutionPolicy",
      "Bypass",
      "-File",
      this.scriptPath,
      "-Port",
      portPath,
      "-BaudRate",
      String(baudrate),
    ];

    return new Promise((resolve, reject) => {
      const child = spawn("powershell.exe", args, {
        stdio: ["pipe", "pipe", "pipe"],
        windowsHide: true,
      });
      this.proc = child;
      this.portPath = portPath;
      this.baudrate = baudrate;
      this.stdoutBuffer = "";
      this.closing = false;
      this.startupError = "";

      const readyPromise = new Promise((readyResolve, readyReject) => {
        this.readyResolve = readyResolve;
        this.readyReject = readyReject;
      });
      this.opening = readyPromise;
      let settled = false;
      let ready = false;
      let timer = null;

      const fail = (err) => {
        if (settled) {
          return;
        }
        settled = true;
        if (timer) {
          clearTimeout(timer);
          timer = null;
        }
        if (this.readyReject) {
          const rejectReady = this.readyReject;
          this.readyResolve = null;
          this.readyReject = null;
          rejectReady(err);
        }
        this._terminate(child);
        if (this.proc === child) {
          this._cleanup();
        }
        reject(err);
      };

      child.stdout.on("data", (chunk) => this._onStdout(chunk));
      child.stderr.on("data", (chunk) => {
        const text = stripAnsi(chunk.toString("utf8")).trim();
        if (text) {
          if (!ready) {
            this.startupError = text;
          }
          this.hub.emit("log", { line: `[bridge] ${text}` });
        }
      });
      child.on("error", fail);
      child.on("exit", (code, signal) => {
        if (settled) {
          return;
        }
        if (!ready) {
          const reason = this.startupError ||
            (signal
              ? `serial bridge terminated by ${signal} before READY`
              : `serial bridge exited before READY (code ${code ?? "unknown"})`);
          fail(new Error(reason));
          return;
        }
        if (this.proc === child) {
          this.proc = null;
          this.opening = null;
          this.stdoutBuffer = "";
        }
        if (code !== 0 && !this.closing) {
          this.hub.emit("status", {
            state: "idle",
            port: this.portPath,
            baudrate: this.baudrate,
          });
        }
      });

      timer = setTimeout(() => {
        fail(new Error("serial bridge start timeout"));
      }, 8000);

      readyPromise.then(() => {
        if (settled) {
          return;
        }
        ready = true;
        clearTimeout(timer);
        timer = null;
        settled = true;
        this.opening = null;
        this.readyResolve = null;
        this.readyReject = null;
        this.hub.emit("log", { line: `[bridge] opened ${portPath} @ ${baudrate}` });
        resolve();
      }).catch((err) => {
        fail(err);
      });
    });
  }

  async close() {
    if (!this.proc) {
      return;
    }
    this.closing = true;
    const child = this.proc;
    try {
      if (child.stdin.writable) {
        child.stdin.write("CLOSE\n");
      }
    } catch {}
    await new Promise((resolve) => {
      let done = false;
      const finish = () => {
        if (done) {
          return;
        }
        done = true;
        clearTimeout(timer);
        resolve();
      };
      const timer = setTimeout(() => {
        this._terminate(child);
        finish();
      }, 1500);
      child.once("exit", finish);
      child.once("error", finish);
    });
    if (this.proc === child) {
      this._cleanup();
    }
  }

  write(buffer) {
    if (!this.proc) {
      throw new Error("serial bridge is not open");
    }
    this.proc.stdin.write(`WRITE ${Buffer.from(buffer).toString("base64")}\n`);
  }

  reset() {
    if (!this.proc) {
      throw new Error("serial bridge is not open");
    }
    this.proc.stdin.write("RESET\n");
  }

  _cleanup() {
    this.proc = null;
    this.stdoutBuffer = "";
    this.opening = null;
    this.readyResolve = null;
    this.readyReject = null;
    this.portPath = "";
    this.baudrate = 0;
    this.closing = false;
    this.startupError = "";
  }

  _terminate(child) {
    if (!child || child.exitCode !== null || child.signalCode !== null || child.killed) {
      return;
    }
    try {
      child.kill();
    } catch {}
  }

  _onStdout(chunk) {
    this.stdoutBuffer += chunk.toString("utf8");
    let idx = this.stdoutBuffer.indexOf("\n");
    while (idx >= 0) {
      const raw = this.stdoutBuffer.slice(0, idx).replace(/\r$/, "");
      this.stdoutBuffer = this.stdoutBuffer.slice(idx + 1);
      this._handleLine(raw);
      idx = this.stdoutBuffer.indexOf("\n");
    }
  }

  _handleLine(line) {
    if (!line) {
      return;
    }
    if (line === "READY" || line.startsWith("READY ")) {
      if (this.readyResolve) {
        this.readyResolve();
        this.readyResolve = null;
        this.readyReject = null;
      }
      return;
    }
    if (line.startsWith("ERROR")) {
      if (this.opening) {
        this.startupError = line.slice(5).trim() || line;
        if (this.readyReject) {
          const rejectReady = this.readyReject;
          this.readyResolve = null;
          this.readyReject = null;
          rejectReady(new Error(this.startupError));
        }
      }
      this.hub.emit("log", { line: `[bridge] ${line}` });
      return;
    }
    if (line.startsWith("DATA ")) {
      const payload = line.slice(5).trim();
      if (payload) {
        this.onData(Buffer.from(payload, "base64"));
      }
      return;
    }
    this.hub.emit("log", { line: `[bridge] ${line}` });
  }
}

class DeviceSession {
  constructor(hub, config) {
    this.hub = hub;
    this.config = config;
    this.portPath = config.defaultPort;
    this.baudrate = config.defaultBaudrate;
    this.state = "idle";
    this.rxBuffer = Buffer.alloc(0);
    this.capture = null;
    this.pendingCdcResponses = new Map();
    this.logPauseDepth = 0;
    this.latestShotPath = "";
    this.latestScreenshot = null;
    this.captureSeq = 0;
    this.logPath = "";
    this.bridge = new SerialBridge(hub, resolvePath(__dirname, config.serialBridgeScript), (chunk) => this._onData(chunk));
  }

  get snapshot() {
    return {
      projectName: this.config.projectName,
      state: this.state,
      port: this.portPath,
      baudrate: this.baudrate,
      logPath: this.logPath,
      latestShotPath: this.latestShotPath,
      latestScreenshot: this.latestScreenshot,
      logPauseDepth: this.logPauseDepth,
    };
  }

  async start({ port, baudrate = this.config.defaultBaudrate }) {
    if (!port) {
      throw new Error("port is required");
    }
    if (this.bridge.isOpen && this.portPath === port && this.baudrate === baudrate) {
      return this.snapshot;
    }
    if (this.bridge.isOpen) {
      await this.stop();
    }
    this.portPath = port;
    this.baudrate = baudrate;
    this.state = "opening";
    this.logPath = path.join(resolvePath(__dirname, this.config.logsDir), `serial_${Date.now()}.log`);
    try {
      await fsp.mkdir(path.dirname(this.logPath), { recursive: true });
      this.hub.emit("status", this.snapshot);
      await this.bridge.open(port, baudrate);
    } catch (err) {
      await this.bridge.close().catch(() => {});
      this.state = "idle";
      this.portPath = this.config.defaultPort;
      this.logPath = "";
      this.hub.emit("status", this.snapshot);
      const reason = err instanceof Error ? err.message : String(err);
      throw new Error(`failed to open port ${port}: ${reason}`);
    }
    this.state = "running";
    this.hub.emit("status", this.snapshot);
    this._log(`[lab] session open port=${this.portPath} baud=${this.baudrate}`);
    this._log(`[lab] log=${this.logPath}`);
    return this.snapshot;
  }

  async stop() {
    this.capture?.reject?.(new Error("session stopped"));
    this.capture = null;
    this.pendingCdcResponses.clear();
    this.rxBuffer = Buffer.alloc(0);
    this.logPauseDepth = 0;
    this.state = "stopping";
    this.hub.emit("status", this.snapshot);
    await this.bridge.close().catch(() => {});
    this.state = "idle";
    this.portPath = this.config.defaultPort;
    this.logPath = "";
    this.latestShotPath = "";
    this.hub.emit("status", this.snapshot);
    return this.snapshot;
  }

  async flash(binBuffer, { port, baudrate = this.config.defaultBaudrate }) {
    const targetPort = port || this.portPath;
    if (!targetPort) {
      throw new Error("port is required for flash");
    }

    const wasRunning = this.bridge.isOpen;
    if (wasRunning) {
      await this.stop();
    }

    const tmpDir = await fsp.mkdtemp(path.join(os.tmpdir(), "esp32-lab-"));
    const firmwarePath = path.join(tmpDir, "firmware.bin");
    await fsp.writeFile(firmwarePath, Buffer.from(binBuffer));
    this.state = "flashing";
    this.hub.emit("status", this.snapshot);

    try {
      await this._runEsptool(targetPort, baudrate, firmwarePath);
      this.hub.emit("flash_done", { port: targetPort, baudrate, size: binBuffer.length });
    } finally {
      await fsp.rm(tmpDir, { recursive: true, force: true }).catch(() => {});
      if (wasRunning) {
        await this.start({ port: targetPort, baudrate });
      } else {
        this.state = "idle";
        this.hub.emit("status", this.snapshot);
      }
    }
    return this.snapshot;
  }

  async screenshot() {
    if (!this.bridge.isOpen) {
      throw new Error("device session is not started");
    }
    if (this.capture) {
      throw new Error("screenshot already in progress");
    }

    await this.pauseLogs();
    const seq = String(++this.captureSeq).padStart(4, "0");
    const captureDir = resolvePath(__dirname, this.config.capturesDir);
    await fsp.mkdir(captureDir, { recursive: true });
    const targetBase = path.join(captureDir, `shot_${seq}`);

    const capture = {
      targetBase,
      resolve: null,
      reject: null,
      promise: null,
      fallbackTimer: null,
    };
    capture.promise = new Promise((resolve, reject) => {
      capture.resolve = resolve;
      capture.reject = reject;
    });
    this.capture = capture;
    this.rxBuffer = Buffer.alloc(0);
    this._log("[shot] send framed screenshot request");
    this.bridge.write(buildCdcFrame(SCREEN_CMD_ID));
    capture.fallbackTimer = setTimeout(() => {
      if (this.capture === capture) {
        this._log("[shot] fallback !SCN");
        this.bridge.write(SCREEN_CMD);
      }
    }, 1000);

    let timeoutTimer = null;
    try {
      const meta = await Promise.race([
        capture.promise,
        new Promise((_, reject) => {
          timeoutTimer = setTimeout(() => {
            if (capture.fallbackTimer) {
              clearTimeout(capture.fallbackTimer);
            }
            if (this.capture === capture) {
              this.capture = null;
            }
            reject(new Error("screenshot timeout"));
          }, this.config.screenshotTimeoutMs);
        }),
      ]);
      return meta;
    } finally {
      if (timeoutTimer) {
        clearTimeout(timeoutTimer);
      }
      if (capture.fallbackTimer) {
        clearTimeout(capture.fallbackTimer);
      }
      await this.resumeLogs().catch(() => {});
    }
  }

  async pauseLogs() {
    if (this.logPauseDepth > 0) {
      this.logPauseDepth += 1;
      return this.snapshot;
    }
    const result = await this._sendCdcCommand(LOG_PAUSE_CMD_ID);
    if (result.status !== 0x00) {
      throw new Error(`log pause rejected: ${result.statusText}`);
    }
    this.logPauseDepth = 1;
    this._log("[lab] log output paused");
    this.hub.emit("status", this.snapshot);
    return this.snapshot;
  }

  async resumeLogs() {
    if (this.logPauseDepth === 0) {
      return this.snapshot;
    }
    this.logPauseDepth -= 1;
    if (this.logPauseDepth > 0) {
      return this.snapshot;
    }
    const result = await this._sendCdcCommand(LOG_RESUME_CMD_ID);
    if (result.status !== 0x00) {
      this.logPauseDepth = 1;
      throw new Error(`log resume rejected: ${result.statusText}`);
    }
    this._log("[lab] log output resumed");
    this.hub.emit("status", this.snapshot);
    return this.snapshot;
  }

  async reboot() {
    if (!this.bridge.isOpen) {
      throw new Error("device session is not started");
    }
    this.state = "rebooting";
    this.hub.emit("status", this.snapshot);
    this._log("[lab] reboot requested via CDC command");
    this.bridge.write(buildCdcFrame(REBOOT_CMD_ID));
    await new Promise((resolve) => setTimeout(resolve, 700));
    this.state = "running";
    this.hub.emit("status", this.snapshot);
    return this.snapshot;
  }

  async _sendCdcCommand(cmd, payload = Buffer.alloc(0), timeoutMs = 3000) {
    if (!this.bridge.isOpen) {
      throw new Error("device session is not started");
    }
    if (this.pendingCdcResponses.has(cmd)) {
      throw new Error(`command 0x${cmd.toString(16)} is already pending`);
    }
    const frame = buildCdcFrame(cmd, payload);
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pendingCdcResponses.delete(cmd);
        reject(new Error(`CDC command 0x${cmd.toString(16)} timeout`));
      }, timeoutMs);
      this.pendingCdcResponses.set(cmd, {
        resolve: (resp) => {
          clearTimeout(timer);
          resolve(resp);
        },
        reject: (err) => {
          clearTimeout(timer);
          reject(err);
        },
      });
      try {
        this.bridge.write(frame);
      } catch (err) {
        clearTimeout(timer);
        this.pendingCdcResponses.delete(cmd);
        reject(err);
      }
    });
  }

  _onData(chunk) {
    if (!chunk || chunk.length === 0) {
      return;
    }
    this.rxBuffer = Buffer.concat([this.rxBuffer, chunk]);

    while (this.rxBuffer.length > 0) {
      if (this.pendingCdcResponses.size > 0 &&
          this.rxBuffer.length >= 6 &&
          this.rxBuffer[0] === CDC_MAGIC_0 &&
          this.rxBuffer[1] === CDC_MAGIC_1) {
        const payloadLen = this.rxBuffer.readUInt16LE(3);
        const total = 6 + payloadLen;
        if (payloadLen <= MAX_CDC_PAYLOAD_BYTES && this.rxBuffer.length >= total) {
          const frame = this.rxBuffer.subarray(0, total);
          this.rxBuffer = this.rxBuffer.subarray(total);
          this._handleCdcFrame(frame);
          continue;
        }
      }

      if (this.capture) {
        const idx = this.rxBuffer.indexOf(SCREENSHOT_MAGIC);
        if (idx < 0) {
          if (this.rxBuffer.length >= 7 &&
              this.rxBuffer[0] === CDC_MAGIC_0 &&
              this.rxBuffer[1] === CDC_MAGIC_1) {
            const cmd = this.rxBuffer[2];
            const payloadLen = this.rxBuffer.readUInt16LE(3);
            const total = 6 + payloadLen;
            if (payloadLen <= MAX_CDC_PAYLOAD_BYTES && this.rxBuffer.length >= total) {
              const frame = this.rxBuffer.subarray(0, total);
              this.rxBuffer = this.rxBuffer.subarray(total);
              if (cmd === SCREEN_CMD_ID && payloadLen >= 1) {
                const status = frame[5];
                const extra = frame.subarray(6, total - 1);
                const message = extra.length > 0 ? extra.toString("utf8").trim() : "";
                const suffix = message ? `: ${message}` : "";
                const err = new Error(`SCREENSHOT_${decodeStatus(status)}${suffix}`);
                this.capture?.reject?.(err);
                this.capture = null;
                continue;
              }
              this._flushText(frame);
              continue;
            }
          }
          const nl = this.rxBuffer.indexOf(0x0a);
          if (nl >= 0) {
            this._flushText(this.rxBuffer.subarray(0, nl + 1));
            this.rxBuffer = this.rxBuffer.subarray(nl + 1);
            continue;
          }
          if (this.rxBuffer.length > 64 * 1024) {
            this._flushText(this.rxBuffer);
            this.rxBuffer = Buffer.alloc(0);
          }
          return;
        }
        if (idx > 0) {
          this._flushText(this.rxBuffer.subarray(0, idx));
          this.rxBuffer = this.rxBuffer.subarray(idx);
          continue;
        }
        if (this.rxBuffer.length < 8) {
          return;
        }
        const size = this.rxBuffer.readUInt32LE(4);
        if (size < 54 || size > MAX_SCREENSHOT_BYTES) {
          this.rxBuffer = this.rxBuffer.subarray(1);
          continue;
        }
        const total = 8 + size;
        if (this.rxBuffer.length < total) {
          return;
        }
        const bmp = this.rxBuffer.subarray(8, total);
        this.rxBuffer = this.rxBuffer.subarray(total);
        this._finishScreenshot(bmp).catch((err) => {
          this.capture?.reject?.(err);
          this.capture = null;
        });
        continue;
      }

      const nl = this.rxBuffer.indexOf(0x0a);
      if (nl < 0) {
        if (this.rxBuffer.length > 8192) {
          this._flushText(this.rxBuffer);
          this.rxBuffer = Buffer.alloc(0);
        }
        return;
      }
      const line = this.rxBuffer.subarray(0, nl + 1);
      this.rxBuffer = this.rxBuffer.subarray(nl + 1);
      this._flushText(line);
    }
  }

  _handleCdcFrame(frame) {
    if (!frame || frame.length < 7) {
      return;
    }
    const cmd = frame[2];
    const payloadLen = frame.readUInt16LE(3);
    const crc = frame[frame.length - 1];
    if (crc8Ccitt(frame.subarray(0, frame.length - 1)) !== crc) {
      this._log(`[lab] CDC CRC mismatch for cmd=0x${cmd.toString(16)}`);
      return;
    }
    const pending = this.pendingCdcResponses.get(cmd);
    if (!pending) {
      this._flushText(frame);
      return;
    }
    this.pendingCdcResponses.delete(cmd);
    const status = frame[5];
    const payload = frame.subarray(6, 6 + Math.max(0, payloadLen - 1));
    const statusText = decodeStatus(status);
    if (status !== 0x00) {
      pending.reject(new Error(`CDC_0x${cmd.toString(16)}_${statusText}`));
      return;
    }
    pending.resolve({ cmd, status, statusText, payload });
  }

  _flushText(buf) {
    const textLine = stripAnsi(buf.toString("utf8")).trimEnd();
    if (textLine) {
      this._log(textLine);
      if (this.capture && /SCREENSHOT_(BUSY|ERROR|NOT_FOUND)/.test(textLine)) {
        this.capture.reject?.(new Error(textLine));
        this.capture = null;
      }
    }
  }

  async _finishScreenshot(bmp) {
    const base = this.capture?.targetBase;
    if (!base) {
      return;
    }
    const bmpPath = `${base}.bmp`;
    await fsp.writeFile(bmpPath, bmp);
    const meta = {
      ok: true,
      bytes: bmp.length,
      bmpPath,
      capturedAt: new Date().toISOString(),
    };
    this.latestScreenshot = meta;
    this.latestShotPath = bmpPath;
    this.hub.emit("screenshot", meta);
    this._log(`[screenshot] saved ${bmpPath} (${bmp.length} bytes)`);
    this.capture?.resolve?.(meta);
    this.capture = null;
  }

  async _runEsptool(port, baudrate, firmwarePath) {
    const tool = await this._resolveEsptoolCommand();
    const args = [
      "--chip", this.config.defaultChip,
      "-p", port,
      "-b", String(baudrate),
      "--before", "default-reset",
      "--after", "hard-reset",
      "write-flash",
      "0x0", firmwarePath,
    ];
    await new Promise((resolve, reject) => {
      const child = spawn(tool.cmd, [...tool.args, ...args], {
        stdio: ["ignore", "pipe", "pipe"],
        windowsHide: true,
      });
      child.stdout.on("data", (d) => process.stdout.write(d));
      child.stderr.on("data", (d) => process.stderr.write(d));
      child.on("error", reject);
      child.on("exit", (code) => {
        if (code === 0) {
          resolve();
        } else {
          reject(new Error(`esptool exited with code ${code}`));
        }
      });
    });
  }

  async _resolveEsptoolCommand() {
    if (this.config.esptoolExe) {
      const stat = await fsp.stat(this.config.esptoolExe).catch(() => null);
      if (stat?.isFile()) {
        return { cmd: this.config.esptoolExe, args: [] };
      }
      if (stat?.isDirectory()) {
        const found = await this._findEsptoolExecutableInDir(this.config.esptoolExe);
        if (found) {
          return { cmd: found, args: [] };
        }
      }
      throw new Error(`未找到烧录工具: ${this.config.esptoolExe}`);
    }

    for (const [cmd, ...baseArgs] of [["py", "-3"], ["python"], ["python3"]]) {
      const probe = await this._runCommand(cmd, [...baseArgs, "-m", "esptool", "--help"]);
      if (probe.code === 0) {
        return { cmd, args: [...baseArgs, "-m", "esptool"] };
      }
    }
    throw new Error("未找到可用的 esptool");
  }

  async _findEsptoolExecutableInDir(dirPath) {
    const wanted = new Set([this.config.esptoolExeName.toLowerCase(), "esptool.exe"]);
    const queue = [dirPath];
    while (queue.length) {
      const dir = queue.shift();
      let entries = [];
      try {
        entries = await fsp.readdir(dir, { withFileTypes: true });
      } catch {
        continue;
      }
      for (const entry of entries) {
        const entryPath = path.join(dir, entry.name);
        if (entry.isFile() && wanted.has(entry.name.toLowerCase())) {
          return entryPath;
        }
        if (entry.isDirectory()) {
          queue.push(entryPath);
        }
      }
    }
    return null;
  }

  _runCommand(cmd, args) {
    return new Promise((resolve) => {
      const child = spawn(cmd, args, { stdio: ["ignore", "pipe", "pipe"], windowsHide: true });
      let stdout = "";
      let stderr = "";
      child.stdout.on("data", (d) => { stdout += d.toString("utf8"); });
      child.stderr.on("data", (d) => { stderr += d.toString("utf8"); });
      child.on("error", (err) => resolve({ code: -1, stdout, stderr: `${stderr}\n${err.message}`.trim() }));
      child.on("exit", (code) => resolve({ code: code ?? -1, stdout, stderr }));
    });
  }

  _log(line) {
    const clean = stripAnsi(line);
    if (!clean) {
      return;
    }
    const out = clean.endsWith("\n") ? clean : `${clean}\n`;
    process.stdout.write(out);
    if (this.logPath) {
      fsp.appendFile(this.logPath, out).catch(() => {});
    }
    this.hub.emit("log", { line: clean });
  }
}

async function main() {
  const config = readConfig();
  const publicDir = resolvePath(__dirname, config.publicDir);
  const logsDir = resolvePath(__dirname, config.logsDir);
  const capturesDir = resolvePath(__dirname, config.capturesDir);
  await fsp.mkdir(logsDir, { recursive: true });
  await fsp.mkdir(capturesDir, { recursive: true });

  const hub = new EventHub();
  const session = new DeviceSession(hub, config);

  const server = http.createServer(async (req, res) => {
    try {
      const url = new URL(req.url, `http://${req.headers.host}`);

      if (req.method === "GET" && url.pathname === "/api/state") {
        json(res, 200, session.snapshot);
        return;
      }
      if (req.method === "GET" && url.pathname === "/api/tools") {
        json(res, 200, {
          ok: true,
          projectName: config.projectName,
          defaultChip: config.defaultChip,
          esptoolExe: config.esptoolExe,
          screenshotTimeoutMs: config.screenshotTimeoutMs,
        });
        return;
      }
      if (req.method === "GET" && url.pathname === "/api/events") {
        hub.attach(res);
        return;
      }
      if (req.method === "POST" && url.pathname === "/api/session/start") {
        const body = await parseJsonBody(req);
        json(res, 200, await session.start({
          port: body.port,
          baudrate: parseNumber(body.baudrate, config.defaultBaudrate),
        }));
        return;
      }
      if (req.method === "POST" && url.pathname === "/api/session/stop") {
        json(res, 200, await session.stop());
        return;
      }
      if (req.method === "POST" && url.pathname === "/api/flash") {
        const body = await readBody(req);
        json(res, 200, await session.flash(body, {
          port: url.searchParams.get("port"),
          baudrate: parseNumber(url.searchParams.get("baudrate"), config.defaultBaudrate),
        }));
        return;
      }
      if (req.method === "POST" && url.pathname === "/api/screenshot") {
        json(res, 200, await session.screenshot());
        return;
      }
      if (req.method === "POST" && url.pathname === "/api/log/pause") {
        json(res, 200, await session.pauseLogs());
        return;
      }
      if (req.method === "POST" && url.pathname === "/api/log/resume") {
        json(res, 200, await session.resumeLogs());
        return;
      }
      if (req.method === "POST" && url.pathname === "/api/reboot") {
        json(res, 200, await session.reboot());
        return;
      }
      if (req.method === "GET" && url.pathname === "/api/screenshot/latest") {
        if (!session.latestShotPath || !fs.existsSync(session.latestShotPath)) {
          text(res, 404, "no screenshot");
          return;
        }
        res.writeHead(200, {
          "Content-Type": "image/bmp",
          "Cache-Control": "no-store",
        });
        fs.createReadStream(session.latestShotPath).pipe(res);
        return;
      }
      if (req.method === "GET") {
        serveStatic(publicDir, req, res);
        return;
      }

      text(res, 404, "Not found");
    } catch (err) {
      json(res, 500, {
        ok: false,
        error: err instanceof Error ? err.message : String(err),
      });
    }
  });

  server.listen(config.port, config.host, () => {
    console.log(`${config.projectName} listening on http://${config.host}:${config.port}`);
  });
}

main().catch((err) => {
  console.error(err instanceof Error ? err.stack || err.message : String(err));
  process.exit(1);
});
