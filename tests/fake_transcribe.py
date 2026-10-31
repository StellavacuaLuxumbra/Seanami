#!/usr/bin/env python3
"""测试用假推理脚本：不依赖 Basic Pitch，输出一个固定音符的 MIDI。

用于在没安装 TensorFlow 的机器上验证 C++ 主流程（解码 -> 子进程 -> JSON -> MIDI 校验）。
"""

import argparse
import json
import struct


def vlq(value: int) -> bytes:
    buf = bytearray()
    while True:
        buf.append(value & 0x7F)
        value >>= 7
        if not value:
            break
    buf.reverse()
    for i in range(len(buf) - 1):
        buf[i] |= 0x80
    return bytes(buf)


def write_midi(path: str) -> None:
    track = bytearray()
    # tempo: 120 BPM -> 500000 us per quarter
    track += vlq(0) + b"\xFF\x51\x03" + bytes([0x07, 0xA1, 0x20])
    # note on C4
    track += vlq(0) + bytes([0x90, 60, 100])
    # note off after 480 ticks
    track += vlq(480) + bytes([0x80, 60, 64])
    # end of track
    track += vlq(0) + b"\xFF\x2F\x00"

    with open(path, "wb") as f:
        f.write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, 480))
        f.write(b"MTrk" + struct.pack(">I", len(track)) + track)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--model", default="")
    parser.add_argument("--onset-threshold", type=float, default=0.5)
    parser.add_argument("--frame-threshold", type=float, default=0.3)
    parser.add_argument("--min-note-ms", type=float, default=58.0)
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    write_midi(args.output)
    print(json.dumps({"ok": True, "notes": 1, "output": args.output}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
