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

test("listSerialPorts marks the occupied port and keeps UTF-8 descriptions", async () => {
  const mock = await makeMockCommand(
    "printf '%s\\n' '[{\"port\":\"COM10\",\"description\":\"USB-SERIAL COM10\"},{\"port\":\"COM6\",\"description\":\"USB-SERIAL CH340 (COM6)\"},{\"port\":\"COM2\",\"description\":\"开发板串口 COM2\"}]'",
  );
  try {
    const ports = await listSerialPorts({
      command: mock.command,
      scriptPath: "ignored-by-mock",
      occupiedPort: "com6",
      timeoutMs: 1000,
    });

    assert.deepEqual(ports, [
      { port: "COM2", description: "开发板串口 COM2", inUse: false },
      { port: "COM6", description: "USB-SERIAL CH340 (COM6)", inUse: true },
      { port: "COM10", description: "USB-SERIAL COM10", inUse: false },
    ]);
  } finally {
    await rm(mock.dir, { recursive: true, force: true });
  }
});

test("listSerialPorts rejects when the mock command exceeds the timeout", async () => {
  const mock = await makeMockCommand(
    "sleep 1\nprintf '%s\\n' '[]'",
  );
  try {
    await assert.rejects(
      listSerialPorts({
        command: mock.command,
        scriptPath: "ignored-by-mock",
        timeoutMs: 20,
      }),
      /serial port enumeration timeout after 20ms/,
    );
  } finally {
    await rm(mock.dir, { recursive: true, force: true });
  }
});

