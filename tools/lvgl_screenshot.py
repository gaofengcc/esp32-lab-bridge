#!/usr/bin/env python3
"""ESP32 LVGL 截图接收工具。

支持两种请求方式：
1. CDC 帧请求，命令号可配置
2. 文本兜底请求，默认 `!SCN\r\n`

默认接收协议：
  [Magic 4B LE: 0x5CA7E01F][BMP长度 4B LE][BMP 原始数据]
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
import time
from pathlib import Path

import serial

SCREENSHOT_MAGIC = 0x5CA7E01F
CDC_MAGIC = b"\xAB\xCD"
MAX_CDC_PAYLOAD = 4096


def cdc_crc8(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0x07) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc


def build_cdc_frame(cmd: int, payload: bytes = b"") -> bytes:
    body = bytearray(CDC_MAGIC)
    body.append(cmd & 0xFF)
    body.extend(struct.pack("<H", len(payload)))
    body.extend(payload)
    body.append(cdc_crc8(bytes(body)))
    return bytes(body)


def parse_cdc_frame(data: bytes):
    idx = data.find(CDC_MAGIC)
    while idx >= 0:
        if idx + 6 > len(data):
            return None
        cmd = data[idx + 2]
        payload_len = struct.unpack_from("<H", data, idx + 3)[0]
        total = 6 + payload_len
        if payload_len > MAX_CDC_PAYLOAD:
            idx = data.find(CDC_MAGIC, idx + 2)
            continue
        if idx + total > len(data):
            return None
        frame = data[idx : idx + total]
        if cdc_crc8(frame[:-1]) != frame[-1]:
            idx = data.find(CDC_MAGIC, idx + 2)
            continue
        return {
            "cmd": cmd,
            "status": frame[5] if payload_len > 0 else None,
            "payload": frame[6:-1] if payload_len > 1 else b"",
            "total": total,
        }
    return None


def parse_bmp_header(data: bytes, offset: int):
    if len(data) < offset + 54 or data[offset : offset + 2] != b"BM":
        return None
    total_size = struct.unpack_from("<I", data, offset + 2)[0]
    width = struct.unpack_from("<i", data, offset + 18)[0]
    height = struct.unpack_from("<i", data, offset + 22)[0]
    pixel_size = struct.unpack_from("<I", data, offset + 34)[0]
    if pixel_size == 0 and width > 0 and abs(height) > 0:
        row_bytes_raw = abs(width) * 3
        row_pad = (row_bytes_raw + 3) & ~3
        pixel_size = row_pad * abs(height)
    return {
        "total_size": total_size,
        "width": width,
        "height": height,
        "pixel_data_size": pixel_size,
    }


def save_bmp_as_png_or_bmp(bmp_data: bytes, output_path: Path) -> Path:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    if output_path.suffix.lower() != ".png":
        output_path.write_bytes(bmp_data)
        return output_path

    try:
        from PIL import Image
    except ImportError:
        bmp_path = output_path.with_suffix(".bmp")
        bmp_path.write_bytes(bmp_data)
        return bmp_path

    width = struct.unpack_from("<i", bmp_data, 18)[0]
    height = abs(struct.unpack_from("<i", bmp_data, 22)[0])
    row_pad = (width * 3 + 3) & ~3
    pixel_start = 54

    img = Image.new("RGB", (width, height))
    pixels = img.load()
    for y in range(height):
        src_y = height - 1 - y
        row_off = pixel_start + src_y * row_pad
        for x in range(width):
            off = row_off + x * 3
            b, g, r = bmp_data[off : off + 3]
            pixels[x, y] = (r, g, b)
    img.save(output_path)
    return output_path


def capture_screenshot(ser: serial.Serial, args: argparse.Namespace) -> bytes:
    rx = bytearray()
    deadline = time.time() + args.timeout
    fallback_at = time.time() + args.fallback_delay
    fallback_sent = False

    if args.pause_cmd is not None:
        ser.write(build_cdc_frame(args.pause_cmd))
        ser.flush()

    ser.write(build_cdc_frame(args.cmd))
    ser.flush()

    try:
        while time.time() < deadline:
            chunk = ser.read(4096)
            if chunk:
                rx.extend(chunk)

            if not fallback_sent and time.time() >= fallback_at:
                ser.write(args.text_request.encode("ascii"))
                ser.flush()
                fallback_sent = True

            frame = parse_cdc_frame(bytes(rx))
            if frame and frame["cmd"] == args.cmd:
                if frame["status"] not in (None, 0x00):
                    status = frame["status"]
                    payload = frame["payload"].decode("utf-8", "replace").strip()
                    suffix = f": {payload}" if payload else ""
                    raise RuntimeError(f"CDC_0x{args.cmd:02X}_{status:02X}{suffix}")
                if len(rx) >= frame["total"]:
                    del rx[: frame["total"]]
                continue

            magic = struct.pack("<I", SCREENSHOT_MAGIC)
            idx = rx.find(magic)
            if idx >= 0:
                if idx > 0:
                    del rx[:idx]
                    continue
                if len(rx) < 8:
                    continue
                size = struct.unpack_from("<I", rx, 4)[0]
                if size < 54 or size > 10 * 1024 * 1024:
                    del rx[0]
                    continue
                total = 8 + size
                if len(rx) < total:
                    continue
                bmp = bytes(rx[8:total])
                del rx[:total]
                return bmp

            nl = rx.find(b"\n")
            if nl >= 0:
                line = bytes(rx[: nl + 1]).decode("utf-8", "replace").strip()
                del rx[: nl + 1]
                if line.startswith("SCREENSHOT_"):
                    raise RuntimeError(line)
                continue

        raise TimeoutError("screenshot timeout")
    finally:
        if args.resume_cmd is not None:
            ser.write(build_cdc_frame(args.resume_cmd))
            ser.flush()


def main() -> int:
    parser = argparse.ArgumentParser(description="ESP32 LVGL 截图接收工具")
    parser.add_argument("port", nargs="?", help="串口号，例如 COM5")
    parser.add_argument("-o", "--output", default="screenshot.bmp", help="输出文件路径")
    parser.add_argument("--baudrate", type=int, default=921600)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--cmd", type=lambda x: int(x, 0), default=0x02, help="截图 CDC 命令号")
    parser.add_argument("--pause-cmd", type=lambda x: int(x, 0), default=0x0A, help="暂停日志命令号")
    parser.add_argument("--resume-cmd", type=lambda x: int(x, 0), default=0x0B, help="恢复日志命令号")
    parser.add_argument("--text-request", default="!SCN\r\n", help="文本兜底请求")
    parser.add_argument("--fallback-delay", type=float, default=1.0, help="等待多久后发送文本兜底请求")
    parser.add_argument("--no-pause", action="store_true", help="不在截图前后暂停日志")
    args = parser.parse_args()

    if not args.port:
        parser.error("需要提供串口号")

    port = args.port
    output_path = Path(args.output)
    capture_args = args
    if args.no_pause:
        capture_args = argparse.Namespace(**{
            **vars(args),
            "pause_cmd": None,
            "resume_cmd": None,
        })

    with serial.Serial(port, args.baudrate, timeout=0.1) as ser:
        bmp = capture_screenshot(ser, capture_args)
        saved = save_bmp_as_png_or_bmp(bmp, output_path)
        meta = parse_bmp_header(bmp, 0)
        print(f"已保存: {saved}")
        if meta:
            print(f"尺寸: {meta['width']}x{meta['height']} 大小: {meta['total_size']} 字节")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
