#!/usr/bin/env python3
"""用 note onset + pitch 匹配计算 precision / recall / F1。

预测和标注都可以是 MIDI；标注也可以是 vocadito 风格的 CSV：
每行 `onset_seconds, frequency_hz, duration_seconds`，无表头。

匹配使用 mir_eval 的标准实现：onset 容差默认 50 ms，音高容差默认 50 cents。
默认忽略 note offset（offset_ratio=None），与常见 note transcription 口径一致。

用法:
  python eval.py --pred output.mid --gt ground_truth.mid
  python eval.py --pred output.mid --gt vocadito_10_notesA1.csv
  python eval.py --pred output.mid --gt ground_truth.mid --onset-tolerance 0.1
"""

import argparse
import sys

try:
    from note_eval import load_midi_notes, load_notes, score
except ImportError as exc:  # pragma: no cover - 开发脚本
    print(f"缺少依赖: {exc}", file=sys.stderr)
    print("请先安装 basic-pitch（会带上 mir_eval / numpy / pretty_midi）", file=sys.stderr)
    sys.exit(2)


def main() -> int:
    parser = argparse.ArgumentParser(description="MIDI / CSV note F1 评估")
    parser.add_argument("--pred", required=True, help="预测 MIDI")
    parser.add_argument("--gt", required=True, help="标注 MIDI 或 vocadito CSV")
    parser.add_argument("--onset-tolerance", type=float, default=0.05, help="onset 容差（秒）")
    parser.add_argument("--pitch-tolerance", type=float, default=50.0, help="音高容差（cents）")
    parser.add_argument(
        "--offset-ratio",
        type=float,
        default=None,
        help="offset 容差比例；默认 None 表示忽略 offset",
    )
    args = parser.parse_args()

    pred = load_midi_notes(args.pred)
    gt = load_notes(args.gt)
    if not pred:
        print("预测 MIDI 中没有音符", file=sys.stderr)
        return 1
    if not gt:
        print("标注中没有音符", file=sys.stderr)
        return 1

    precision, recall, f1 = score(
        pred,
        gt,
        onset_tolerance=args.onset_tolerance,
        pitch_tolerance=args.pitch_tolerance,
        offset_ratio=args.offset_ratio,
    )

    print(f"pred_notes={len(pred)} gt_notes={len(gt)}")
    print(f"precision={precision:.3f} recall={recall:.3f} f1={f1:.3f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
