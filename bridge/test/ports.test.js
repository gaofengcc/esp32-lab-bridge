import assert from "node:assert/strict";
import { chmod, mkdtemp, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import test from "node:test";

import {
  listSerialPorts,
  parsePortList,
  resolveFirstSerialPort,
} from "../ports.js";

async function makeMockCommand(body) {
  const dir = await mkdtemp(path.join(os.tmpdir(), "esp32-lab-ports-test-"));
  const command = path.join(dir, "mock-powershell.sh");
  await writeFile(command, `#!/bin/sh\n${body}\n`, "utf8");
  await chmod(command, 0o755);
  return { dir, command };
}

test("parsePortList normalizes, deduplicates and sorts COM names", () => {
  const ports = parsePortList(JSON.stringify([
    { port: "com10", description: "USB-SERIAL COM10" },
    { name: "COM6", description: "" },
    { port: "COM2", description: "板卡 COM2" },
    { port: "not-a-port", description: "ignored" },
    { port: "COM6", description: "duplicate" },
  ]));

  assert.deepEqual(ports, [
    { port: "COM2", description: "板卡 COM2" },
    { port: "COM6", description: "duplicate" },
    { port: "COM10", description: "USB-SERIAL COM10" },
  ]);
  assert.equal(resolveFirstSerialPort(ports), "COM2");
});

test("listSerialPorts parses multiple ports, keeps UTF-8 descriptions and falls back when missing", async () => {
  const mock = await makeMockCommand(
    "printf '%s\\n' '[{\"port\":\"COM10\",\"description\":\"USB-SERIAL COM10\"},{\"port\":\"COM6\",\"description\":\"USB-SERIAL CH340 (COM6)\"},{\"port\":\"COM2\",\"description\":\"\"}]'",
  );
  try {
    const ports = await listSerialPorts({
      command: mock.command,
      scriptPath: "ignored-by-mock",
      occupiedPort: "com6",
      timeoutMs: 1000,
    });

    assert.deepEqual(ports, [
      { port: "COM2", description: "COM2", inUse: false },
      { port: "COM6", description: "USB-SERIAL CH340 (COM6)", inUse: true },
      { port: "COM10", description: "USB-SERIAL COM10", inUse: false },
    ]);
  } finally {
    await rm(mock.dir, { recursive: true, force: true });
  }
});

test("resolveFirstSerialPort returns empty for an empty list and skips occupied ports", () => {
  assert.equal(resolveFirstSerialPort([]), "");
  assert.equal(resolveFirstSerialPort(undefined), "");
  assert.equal(resolveFirstSerialPort([
    { port: "COM2", description: "COM2", inUse: true },
    { port: "COM6", description: "COM6", inUse: true },
  ]), "");
  assert.equal(resolveFirstSerialPort([
    { port: "COM2", description: "COM2", inUse: true },
    { port: "COM6", description: "COM6", inUse: false },
  ]), "COM6");
});

test("listSerialPorts rejects a non-zero mock command with stderr", async () => {
  const mock = await makeMockCommand(
    "printf '%s\\n' 'Get-CimInstance failed' >&2\nexit 7",
  );
  try {
    await assert.rejects(
      listSerialPorts({
        command: mock.command,
        scriptPath: "ignored-by-mock",
        timeoutMs: 1000,
      }),
      /serial port enumeration exited with code 7: Get-CimInstance failed/,
    );
  } finally {
    await rm(mock.dir, { recursive: true, force: true });
  }
});

test("listSerialPorts rejects non-JSON output with a readable error", async () => {
  const mock = await makeMockCommand(
    "printf '%s\\n' 'not-json-output'",
  );
  try {
    await assert.rejects(
      listSerialPorts({
        command: mock.command,
        scriptPath: "ignored-by-mock",
        timeoutMs: 1000,
      }),
      /invalid serial port enumeration output:/,
    );
  } finally {
    await rm(mock.dir, { recursive: true, force: true });
  }
});

test("listSerialPorts rejects when the command is unavailable", async () => {
  const mock = await makeMockCommand("");
  const missingCommand = path.join(mock.dir, "does-not-exist");
  try {
    await assert.rejects(
      listSerialPorts({
        command: missingCommand,
        scriptPath: "ignored-by-mock",
        timeoutMs: 1000,
      }),
      /serial port enumeration failed:/,
    );
  } finally {
    await rm(mock.dir, { recursive: true, force: true });
  }
});

test("listSerialPorts rejects when the mock command exceeds the timeout", async () => {
  const mock = await makeMockCommand(
    "sleep 1\nprintf '%s\\n' '[]'",
  );
  try {
    const startedAt = Date.now();
    await assert.rejects(
      listSerialPorts({
        command: mock.command,
        scriptPath: "ignored-by-mock",
        timeoutMs: 20,
      }),
      /serial port enumeration timeout after 20ms/,
    );
    assert.ok(Date.now() - startedAt < 500, "timeout should settle promptly");
  } finally {
    await rm(mock.dir, { recursive: true, force: true });
  }
});
