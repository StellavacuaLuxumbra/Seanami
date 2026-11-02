"""共享的 note 评估逻辑：读取 MIDI / vocadito CSV，按 mir_eval 口径算 P/R/F1。"""

from __future__ import annotations

import csv

import mir_eval.transcription as mir_transcription
import numpy as np
import pretty_midi


def midi_to_hz(pitch: float) -> float:
    return 440.0 * (2.0 ** ((pitch - 69.0) / 12.0))


def load_midi_notes(path: str):
    midi = pretty_midi.PrettyMIDI(path)
    notes = []
    for instrument in midi.instruments:
        if instrument.is_drum:
            continue
        for note in instrument.notes:
            if note.end <= note.start:
                continue
            notes.append((note.start, note.end, midi_to_hz(note.pitch)))
    notes.sort(key=lambda item: (item[0], item[2]))
    return notes


def load_csv_notes(path: str):
    notes = []
    with open(path, newline="", encoding="utf-8-sig") as handle:
        for row in csv.reader(handle):
            if len(row) < 2 or not row[0].strip():
                continue
            try:
                onset = float(row[0])
                frequency = float(row[1])
                duration = float(row[2]) if len(row) > 2 and row[2].strip() else 0.0
            except ValueError:
                continue  # 跳过表头或注释行
            if frequency <= 0.0:
                continue
            notes.append((onset, onset + max(duration, 0.0), frequency))
    notes.sort(key=lambda item: (item[0], item[2]))
    return notes


def load_notes(path: str):
    if path.lower().endswith(".csv"):
        return load_csv_notes(path)
    return load_midi_notes(path)


def to_arrays(notes):
    if not notes:
        return np.zeros((0, 2)), np.zeros(0)
    intervals = np.array([[onset, offset] for onset, offset, _ in notes], dtype=float)
    pitches = np.array([pitch for _, _, pitch in notes], dtype=float)
    return intervals, pitches


def score(pred_notes, gt_notes, onset_tolerance=0.05, pitch_tolerance=50.0, offset_ratio=None):
    ref_intervals, ref_pitches = to_arrays(gt_notes)
    est_intervals, est_pitches = to_arrays(pred_notes)
    return mir_transcription.precision_recall_f1_overlap(
        ref_intervals,
        ref_pitches,
        est_intervals,
        est_pitches,
        onset_tolerance=onset_tolerance,
        pitch_tolerance=pitch_tolerance,
        offset_ratio=offset_ratio,
        offset_min_tolerance=onset_tolerance,
    )[:3]
