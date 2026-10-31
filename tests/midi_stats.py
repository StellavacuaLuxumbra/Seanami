#!/usr/bin/env python3
"""打印 MIDI 的音符统计，用于验证推理输出不是空文件。"""

import sys

import pretty_midi


def main() -> int:
    if len(sys.argv) != 2:
        print("用法: python midi_stats.py <file.mid>", file=sys.stderr)
        return 2

    midi = pretty_midi.PrettyMIDI(sys.argv[1])
    notes = [
        (round(note.start, 3), note.pitch)
        for instrument in midi.instruments
        if not instrument.is_drum
        for note in instrument.notes
    ]
    print(f"notes = {len(notes)}")
    print(f"first 20 = {notes[:20]}")
    return 0 if notes else 1


if __name__ == "__main__":
    raise SystemExit(main())
