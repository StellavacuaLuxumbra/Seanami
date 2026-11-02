#!/usr/bin/env python3
"""在 vocadito 片段上批量跑 tab_tool，并用人工标注计算 note F1。

数据目录里每个片段需要：
  vocadito_N.wav
  vocadito_N_notesA1.csv
  vocadito_N_notesA2.csv

CSV 为 vocadito 格式：onset_seconds, frequency_hz, duration_seconds（无表头）。

用法:
  python tests/eval/vocadito_bench.py --data-dir tests/real/vocadito
  python tests/eval/vocadito_bench.py --data-dir tests/real/vocadito --min-note-ms 100
  python tests/eval/vocadito_bench.py --data-dir tests/real/vocadito --reuse

每段音频用 A1、A2 两份标注各算一次 F1，再取两者较高值作为该段的 A-max
（与 vocadito 论文的 A max 口径一致）。
"""

import argparse
import subprocess
import sys
from pathlib import Path

from note_eval import load_csv_notes, load_midi_notes, score


def parse_args():
    parser = argparse.ArgumentParser(description="vocadito note F1 benchmark")
    parser.add_argument("--data-dir", default="tests/real/vocadito")
    parser.add_argument("--exe", default="build/tab_tool.exe")
    parser.add_argument("--python", default="tools/python311/python.exe")
    parser.add_argument("--script", default="scripts/transcribe.py")
    parser.add_argument("--midi-dir", default="tests/out_eval/vocadito")
    parser.add_argument("--min-note-ms", type=float, default=None)
    parser.add_argument("--onset-tolerance", type=float, default=0.05)
    parser.add_argument("--reuse", action="store_true", help="复用已存在的 MIDI")
    parser.add_argument("--limit", type=int, default=0, help="只跑前 N 段，0 表示全部")
    return parser.parse_args()


def run_tool(args, wav: Path, mid: Path) -> bool:
    if args.reuse and mid.exists():
        return True
    mid.parent.mkdir(parents=True, exist_ok=True)
    command = [
        args.exe,
        str(wav),
        "-o",
        str(mid),
        "--python",
        args.python,
        "--script",
        args.script,
    ]
    if args.min_note_ms is not None:
        command += ["--min-note-ms", str(args.min_note_ms)]
    result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if result.returncode != 0:
        print(f"  失败: {wav.name} exit={result.returncode}", file=sys.stderr)
        if result.stderr.strip():
            print(result.stderr.strip(), file=sys.stderr)
        return False
    return True


def main() -> int:
    args = parse_args()
    data_dir = Path(args.data_dir)
    if not data_dir.is_dir():
        print(f"找不到数据目录: {data_dir}", file=sys.stderr)
        return 2
    if not Path(args.exe).exists():
        print(f"找不到可执行文件: {args.exe}，请先运行 .\\build.ps1", file=sys.stderr)
        return 2

    clips = sorted(data_dir.glob("vocadito_*.wav"))
    if args.limit > 0:
        clips = clips[: args.limit]
    if not clips:
        print(f"{data_dir} 下没有 vocadito_*.wav", file=sys.stderr)
        return 2

    rows = []
    skipped = 0
    print(f"数据: {data_dir}  片段: {len(clips)}  最短音符: {args.min_note_ms or '默认'}")
    for wav in clips:
        name = wav.stem
        a1_path = data_dir / f"{name}_notesA1.csv"
        a2_path = data_dir / f"{name}_notesA2.csv"
        if not a1_path.exists() or not a2_path.exists():
            print(f"  跳过 {name}: 缺少人工标注 CSV")
            skipped += 1
            continue
        mid = Path(args.midi_dir) / f"{name}.mid"
        if not run_tool(args, wav, mid):
            skipped += 1
            continue

        pred = load_midi_notes(str(mid))
        if not pred:
            print(f"  跳过 {name}: 没有生成音符")
            skipped += 1
            continue

        a1 = load_csv_notes(str(a1_path))
        a2 = load_csv_notes(str(a2_path))
        p1, r1, f1 = score(pred, a1, args.onset_tolerance)
        p2, r2, f2 = score(pred, a2, args.onset_tolerance)
        _, _, human = score(a1, a2, args.onset_tolerance)
        best = max((p1, r1, f1), (p2, r2, f2), key=lambda item: item[2])
        rows.append((name, len(pred), (p1, r1, f1), (p2, r2, f2), best, human))
        print(
            f"  {name:<14} notes={len(pred):>3}  "
            f"A1={f1:.3f}  A2={f2:.3f}  A-max={best[2]:.3f}"
        )

    if not rows:
        print("没有可评估的片段", file=sys.stderr)
        return 1

    print()
    print(f"{'clip':<16}{'notes':>5}  {'A1 P/R/F1':<20}{'A2 P/R/F1':<20}{'A-max':>7}{'human':>8}")
    for name, count, one, two, best, human in rows:
        print(
            f"{name:<16}{count:>5}  "
            f"{one[0]:.2f}/{one[1]:.2f}/{one[2]:.3f}     "
            f"{two[0]:.2f}/{two[1]:.2f}/{two[2]:.3f}     "
            f"{best[2]:>7.3f}{human:>8.3f}"
        )

    n = len(rows)
    mean = lambda values: sum(values) / n
    print()
    print(f"片段数: {n}  跳过: {skipped}")
    print(f"平均 F1  A1={mean([row[2][2] for row in rows]):.3f}  "
          f"A2={mean([row[3][2] for row in rows]):.3f}  "
          f"A-max={mean([row[4][2] for row in rows]):.3f}")
    print(f"平均精度/召回（A-max 对应标注）  "
          f"P={mean([row[4][0] for row in rows]):.3f}  R={mean([row[4][1] for row in rows]):.3f}")
    print(f"两位标注者之间平均 F1（人类一致性）: {mean([row[5] for row in rows]):.3f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
