#!/usr/bin/env python3
"""测试用假推理脚本：不依赖 Basic Pitch，输出一个固定音符的 MIDI。

用于验证 C++ 主流程（解码 -> 子进程/worker -> JSON -> MIDI 校验）。
"""

import argparse
import json
import os
import struct
import sys


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
    parent = os.path.dirname(os.path.abspath(path))
    if parent:
        os.makedirs(parent, exist_ok=True)

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


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    parser.add_argument("--worker", action="store_true")
    parser.add_argument("--input", default="")
    parser.add_argument("--output", default="")
    parser.add_argument("--model", default="")
    parser.add_argument("--preset", default="default")
    parser.add_argument("--onset-threshold", type=float, default=0.5)
    parser.add_argument("--frame-threshold", type=float, default=0.3)
    parser.add_argument("--min-note-ms", type=float, default=127.7)
    parser.add_argument("--min-freq", type=float, default=0)
    parser.add_argument("--max-freq", type=float, default=0)
    parser.add_argument("--mono", dest="mono", action="store_true", default=False)
    parser.add_argument("--poly", dest="mono", action="store_false", default=False)
    parser.add_argument("--quantize", dest="quantize", action="store_true", default=False)
    parser.add_argument("--no-quantize", dest="quantize", action="store_false", default=False)
    parser.add_argument("--quantize-grid", default="1/16")
    parser.add_argument("--bpm", type=float, default=0)
    parser.add_argument("--separate", default="none")
    parser.add_argument("--postprocess", dest="postprocess", action="store_true", default=True)
    parser.add_argument("--no-postprocess", dest="postprocess", action="store_false", default=True)
    parser.add_argument("--merge-gap-ms", type=float, default=0)
    parser.add_argument("--tempo-map", dest="tempo_map", action="store_true", default=True)
    parser.add_argument("--no-tempo-map", dest="tempo_map", action="store_false", default=True)
    parser.add_argument("--track-name", default="")
    parser.add_argument("--program", type=int, default=-1)
    parser.add_argument("--hpss-margin", type=float, default=1.0)
    parser.add_argument("--demucs-model", default="htdemucs")
    parser.add_argument("-v", "--verbose", action="store_true")
    return parser


def summary(args, ok: bool = True, error: str = "") -> dict:
    payload = {
        "ok": ok,
        "notes": 1 if ok else 0,
        "output": args.output,
        "bpm": 120.0,
        "quantized": bool(getattr(args, "quantize", False)),
        "preset": getattr(args, "preset", "default"),
        "separate": getattr(args, "separate", "none"),
    }
    if error:
        payload["error"] = error
    return payload


def dump_params(args) -> None:
    path = os.environ.get("SEANAMI_FAKE_PARAMS")
    if not path:
        return
    try:
        with open(path, "w", encoding="utf-8") as f:
            json.dump(vars(args), f, ensure_ascii=False, indent=2)
    except Exception:
        pass


def log_pid(args) -> None:
    path = os.environ.get("SEANAMI_FAKE_PID_LOG")
    if not path:
        return
    try:
        with open(path, "a", encoding="utf-8") as f:
            f.write(f"{os.getpid()}\t{args.input}\n")
    except Exception:
        pass


def run_one(args) -> int:
    dump_params(args)
    log_pid(args)
    write_midi(args.output)
    print(json.dumps(summary(args), ensure_ascii=False))
    return 0


def run_worker(args) -> int:
    try:
        sys.stdin.reconfigure(encoding="utf-8")
        sys.stdout.reconfigure(encoding="utf-8", line_buffering=True)
    except Exception:
        pass
    print(json.dumps({"ready": True}), flush=True)
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            job = json.loads(line)
            merged = vars(args).copy()
            merged.update(job)
            merged["worker"] = False
            job_args = argparse.Namespace(**merged)
            dump_params(job_args)
            log_pid(job_args)
            write_midi(job_args.output)
            print(json.dumps(summary(job_args), ensure_ascii=False), flush=True)
        except Exception as exc:  # noqa: BLE001
            print(json.dumps({"ok": False, "error": str(exc)}, ensure_ascii=False), flush=True)
    return 0


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    if args.worker:
        return run_worker(args)
    if not args.input or not args.output:
        print(json.dumps({"ok": False, "error": "缺少 --input/--output"}), flush=True)
        return 2
    return run_one(args)


if __name__ == "__main__":
    raise SystemExit(main())
