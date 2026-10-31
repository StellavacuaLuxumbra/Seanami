#!/usr/bin/env python3
"""用 note onset + pitch 匹配计算 precision / recall / F1。

用法:
  python eval.py --pred output.mid --gt ground_truth.mid
  python eval.py --pred output.mid --gt ground_truth.mid --tolerance 0.05
"""

import argparse
import sys

try:
    import pretty_midi
except ImportError:
    print("需要 pretty_midi: pip install pretty_midi", file=sys.stderr)
    sys.exit(2)


def extract_notes(path: str):
    midi = pretty_midi.PrettyMIDI(path)
    notes = []
    for instrument in midi.instruments:
        if instrument.is_drum:
            continue
        for note in instrument.notes:
            notes.append((note.start, note.pitch))
    notes.sort(key=lambda item: (item[0], item[1]))
    return notes


def match(pred, gt, tolerance: float):
    used = [False] * len(gt)
    true_positive = 0

    for pred_start, pred_pitch in pred:
        best_index = -1
        best_delta = tolerance + 1.0
        for index, (gt_start, gt_pitch) in enumerate(gt):
            if used[index] or gt_pitch != pred_pitch:
                continue
            delta = abs(gt_start - pred_start)
            if delta <= tolerance and delta < best_delta:
                best_index = index
                best_delta = delta
        if best_index >= 0:
            used[best_index] = True
            true_positive += 1

    precision = true_positive / len(pred) if pred else 0.0
    recall = true_positive / len(gt) if gt else 0.0
    f1 = (2 * precision * recall / (precision + recall)) if (precision + recall) else 0.0
    return precision, recall, f1


def main() -> int:
    parser = argparse.ArgumentParser(description="MIDI note F1 评估")
    parser.add_argument("--pred", required=True, help="预测 MIDI")
    parser.add_argument("--gt", required=True, help="标注 MIDI")
    parser.add_argument("--tolerance", type=float, default=0.05, help="onset 容差（秒）")
    args = parser.parse_args()

    pred = extract_notes(args.pred)
    gt = extract_notes(args.gt)
    precision, recall, f1 = match(pred, gt, args.tolerance)

    print(f"pred_notes={len(pred)} gt_notes={len(gt)}")
    print(f"precision={precision:.3f} recall={recall:.3f} f1={f1:.3f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
