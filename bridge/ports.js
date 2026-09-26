import path from "node:path";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const DEFAULT_SCRIPT_PATH = path.join(__dirname, "list_ports.ps1");

function normalizePortName(value) {
  const name = String(value ?? "").trim().toUpperCase();
  return /^COM\d+$/.test(name) ? name : "";
}

function parsePortList(raw) {
  const parsed = JSON.parse(String(raw || "").replace(/^\uFEFF/, "").trim() || "[]");
  const items = Array.isArray(parsed) ? parsed : [parsed];
  const unique = new Map();
  for (const item of items) {
    const port = normalizePortName(item?.port ?? item?.name);
    if (!port) {
      continue;
    }
    const description = String(item?.description ?? "").trim() || port;
    unique.set(port, { port, description });
  }
  return [...unique.values()].sort((a, b) => {
    const aNumber = Number(a.port.slice(3));
    const bNumber = Number(b.port.slice(3));
    return aNumber - bNumber;
  });
}

function decodePowerShellOutput(buffer) {
  return Buffer.concat(buffer).toString("utf8");
}

/**
 * 通过独立 PowerShell 脚本枚举串口。
 *
 * `occupiedPort` 仅用于方便调用方直接得到占用状态；不传时返回 false。
 * `command`/`scriptPath` 可注入，便于 Linux 主机上的 mock 测试。
 */
export function listSerialPorts({
  command = "powershell.exe",
  scriptPath = DEFAULT_SCRIPT_PATH,
  occupiedPort = "",
  timeoutMs = 8000,
} = {}) {
  const occupied = normalizePortName(occupiedPort);
  const args = [
    "-NoLogo",
    "-NoProfile",
    "-ExecutionPolicy",
    "Bypass",
    "-File",
    scriptPath,
  ];

  return new Promise((resolve, reject) => {
    let settled = false;
    const stdout = [];
    const stderr = [];
    const child = spawn(command, args, {
      stdio: ["ignore", "pipe", "pipe"],
      windowsHide: true,
    });

    const finish = (callback, value) => {
      if (settled) {
        return;
      }
      settled = true;
      clearTimeout(timer);
      callback(value);
    };

    const timer = setTimeout(() => {
      try {
        child.kill();
      } catch {}
      finish(reject, new Error(`serial port enumeration timeout after ${timeoutMs}ms`));
    }, timeoutMs);

    child.stdout.on("data", (chunk) => stdout.push(Buffer.from(chunk)));
    child.stderr.on("data", (chunk) => stderr.push(Buffer.from(chunk)));
    child.on("error", (err) => {
      finish(reject, new Error(`serial port enumeration failed: ${err.message}`));
    });
    child.on("exit", (code, signal) => {
      if (code !== 0) {
        const errorText = decodePowerShellOutput(stderr).trim();
        const suffix = errorText ? `: ${errorText}` : "";
        finish(reject, new Error(`serial port enumeration exited with code ${code ?? "unknown"}${signal ? ` (${signal})` : ""}${suffix}`));
        return;
      }
      try {
        const ports = parsePortList(decodePowerShellOutput(stdout)).map((item) => ({
          ...item,
          inUse: Boolean(occupied && item.port === occupied),
        }));
        finish(resolve, ports);
      } catch (err) {
        finish(reject, new Error(`invalid serial port enumeration output: ${err instanceof Error ? err.message : String(err)}`));
      }
    });
  });
}

export function resolveFirstSerialPort(ports) {
  if (!Array.isArray(ports)) {
    return "";
  }
  const selected = ports.find((item) => item && !item.inUse && item.port);
  return selected?.port || "";
}

export { DEFAULT_SCRIPT_PATH, normalizePortName, parsePortList };
