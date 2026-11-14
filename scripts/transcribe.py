#!/usr/bin/env python3
"""Basic Pitch 推理入口。

C++ 通过子进程调用本脚本，支持两种模式：

1. 单次运行（默认）：
   --input <wav> --output <mid> [选项]
   stdout 最后一行是 JSON 摘要，例如 {"ok": true, "notes": 123}

2. 常驻 worker（批量时由 C++ 自动启用）：
   --worker
   启动后先输出一行 {"ready": true}，然后从 stdin 逐行读取 JSON 任务，
   每个任务输出一行 JSON 结果。模型只加载一次。

失败时返回非 0 退出码，并在 JSON 中给出 error。
"""

from __future__ import annotations

import argparse
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
import traceback
import unicodedata
from typing import Any, Dict, List, Optional, Sequence, Tuple

SAMPLE_RATE = 22050

# 量化网格：单位为“拍”（四分音符）。
GRID_STEPS = {
    "1/4": 1.0,
    "1/8": 0.5,
    "1/16": 0.25,
    "1/32": 0.125,
    "1/8T": 1.0 / 3.0,
    "1/16T": 1.0 / 6.0,
}

# 预设会先应用，再被命令行显式参数覆盖。
# 量化默认不开启：在 vocadito 基准上量化会让 note F1 下降约 0.07，
# 需要吸附节拍网格时请显式使用 --quantize。合并间隔同理，默认 0。
PRESETS: Dict[str, Dict[str, Any]] = {
    "default": {},
    "vocal": {
        "onset_threshold": 0.45,
        "frame_threshold": 0.30,
        "min_note_ms": 100.0,
        "min_freq": 82.41,      # E2，覆盖男声
        "max_freq": 1046.50,    # C6
        "mono": True,
        "quantize": False,
        "merge_gap_ms": 0.0,
        "program": 53,          # GM Voice Oohs
    },
    "guitar": {
        "onset_threshold": 0.50,
        "frame_threshold": 0.30,
        "min_note_ms": 100.0,
        "min_freq": 82.41,      # E2
        "max_freq": 1318.51,    # E6
        "mono": True,
        "quantize": False,
        "merge_gap_ms": 0.0,
        "program": 25,          # GM Acoustic Guitar (steel)
    },
    "piano": {
        "onset_threshold": 0.50,
        "frame_threshold": 0.30,
        "min_note_ms": 90.0,
        "min_freq": 27.50,      # A0
        "max_freq": 4186.01,    # C8
        "mono": False,
        "quantize": False,
        "merge_gap_ms": 0.0,
        "program": 0,           # GM Acoustic Grand Piano
    },
    "bass": {
        "onset_threshold": 0.50,
        "frame_threshold": 0.30,
        "min_note_ms": 90.0,
        "min_freq": 30.87,      # B0
        "max_freq": 392.00,     # G4
        "mono": True,
        "quantize": False,
        "merge_gap_ms": 0.0,
        "program": 33,          # GM Electric Bass (finger)
    },
}

BASE_DEFAULTS: Dict[str, Any] = {
    "onset_threshold": 0.5,
    "frame_threshold": 0.3,
    "min_note_ms": 127.7,
    "min_freq": None,
    "max_freq": None,
    "mono": False,
    "quantize": False,
    "quantize_grid": "1/16",
    "bpm": None,
    "separate": "none",
    "postprocess": True,
    "merge_gap_ms": 0.0,
    "tempo_map": True,
    "track_name": "",
    "program": -1,
    "hpss_margin": 1.0,
    "demucs_model": "htdemucs",
}

TUNABLE_KEYS = (
    "onset_threshold",
    "frame_threshold",
    "min_note_ms",
    "min_freq",
    "max_freq",
    "mono",
    "quantize",
    "quantize_grid",
    "bpm",
    "separate",
    "postprocess",
    "merge_gap_ms",
    "tempo_map",
    "track_name",
    "program",
    "hpss_margin",
    "demucs_model",
)


def configure_stdio() -> None:
    """统一 UTF-8，避免 Windows 控制台代码页导致中文乱码。"""
    try:
        sys.stdin.reconfigure(encoding="utf-8")
        sys.stdout.reconfigure(encoding="utf-8", line_buffering=True)
        sys.stderr.reconfigure(encoding="utf-8")
    except Exception:
        pass


def emit(payload: dict, stream=None) -> None:
    target = stream or sys.stdout
    target.write(json.dumps(payload, ensure_ascii=False) + "\n")
    target.flush()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Basic Pitch 推理脚本（支持常驻 worker）")
    parser.add_argument("--worker", action="store_true", help="常驻 worker 模式")
    parser.add_argument("--input", default="", help="输入 WAV 路径")
    parser.add_argument("--output", default="", help="输出 MIDI 路径")
    parser.add_argument("--model", default="", help="模型目录或文件，空则用包内置 ONNX 模型")
    parser.add_argument("--preset", default="default", choices=sorted(PRESETS))
    parser.add_argument("--onset-threshold", type=float, default=None)
    parser.add_argument("--frame-threshold", type=float, default=None)
    parser.add_argument("--min-note-ms", type=float, default=None)
    parser.add_argument("--min-freq", type=float, default=None)
    parser.add_argument("--max-freq", type=float, default=None)
    parser.add_argument("--mono", dest="mono", action="store_true", default=None)
    parser.add_argument("--poly", dest="mono", action="store_false", default=None)
    parser.add_argument("--quantize", dest="quantize", action="store_true", default=None)
    parser.add_argument("--no-quantize", dest="quantize", action="store_false", default=None)
    parser.add_argument("--quantize-grid", default=None, choices=sorted(GRID_STEPS))
    parser.add_argument("--bpm", type=float, default=None)
    parser.add_argument("--separate", default=None, choices=("none", "hpss", "demucs"))
    parser.add_argument("--postprocess", dest="postprocess", action="store_true", default=None)
    parser.add_argument("--no-postprocess", dest="postprocess", action="store_false", default=None)
    parser.add_argument("--merge-gap-ms", type=float, default=None)
    parser.add_argument("--tempo-map", dest="tempo_map", action="store_true", default=None)
    parser.add_argument("--no-tempo-map", dest="tempo_map", action="store_false", default=None)
    parser.add_argument("--track-name", default=None)
    parser.add_argument("--program", type=int, default=None)
    parser.add_argument("--hpss-margin", type=float, default=None)
    parser.add_argument("--demucs-model", default=None)
    parser.add_argument("-v", "--verbose", action="store_true")
    return parser


def resolve_config(args: argparse.Namespace) -> Dict[str, Any]:
    preset_name = args.preset or "default"
    if preset_name not in PRESETS:
        raise ValueError(f"未知预设: {preset_name}")

    config = dict(BASE_DEFAULTS)
    config.update(PRESETS[preset_name])
    for key in TUNABLE_KEYS:
        value = getattr(args, key, None)
        if value is not None:
            config[key] = value

    config["preset"] = preset_name
    if not config.get("track_name"):
        stem = os.path.splitext(os.path.basename(args.input))[0] if args.input else ""
        config["track_name"] = stem or "Melody"

    if config["min_note_ms"] is None or config["min_note_ms"] <= 0:
        raise ValueError("--min-note-ms 必须大于 0")
    if config["quantize_grid"] not in GRID_STEPS:
        raise ValueError(f"不支持的量化网格: {config['quantize_grid']}")
    if config["min_freq"] is not None and config["min_freq"] <= 0:
        config["min_freq"] = None
    if config["max_freq"] is not None and config["max_freq"] <= 0:
        config["max_freq"] = None
    if (
        config["min_freq"] is not None
        and config["max_freq"] is not None
        and config["min_freq"] >= config["max_freq"]
    ):
        raise ValueError("--min-freq 必须小于 --max-freq")
    if config["bpm"] is not None and config["bpm"] <= 0:
        config["bpm"] = None
    if config["bpm"] is not None and not (20.0 <= config["bpm"] <= 400.0):
        raise ValueError("--bpm 必须在 20~400 之间")
    if config["merge_gap_ms"] < 0:
        raise ValueError("--merge-gap-ms 不能为负数")
    if config["hpss_margin"] < 1.0:
        raise ValueError("--hpss-margin 必须 >= 1.0")
    if config["program"] is not None and not (-1 <= config["program"] <= 127):
        raise ValueError("--program 必须在 0~127 之间（-1 表示不指定）")
    return config


def resolve_model_path(model_arg: str) -> str:
    import basic_pitch

    if model_arg and os.path.exists(model_arg):
        if not (os.path.isdir(model_arg) and not os.listdir(model_arg)):
            return model_arg

    if getattr(basic_pitch, "ONNX_PRESENT", False):
        return str(
            basic_pitch.build_icassp_2022_model_path(
                basic_pitch.FilenameSuffix.onnx
            )
        )

    from basic_pitch import ICASSP_2022_MODEL_PATH

    return str(ICASSP_2022_MODEL_PATH)


def note_name_from_freq(freq: Optional[float], ceil: bool = False) -> Optional[int]:
    if not freq or freq <= 0:
        return None
    value = 69.0 + 12.0 * math.log2(freq / 440.0)
    return int(math.ceil(value)) if ceil else int(math.floor(value))


def load_audio(path: str):
    import librosa

    y, sr = librosa.load(path, sr=SAMPLE_RATE, mono=True)
    if y is None or len(y) == 0:
        raise RuntimeError(f"音频为空: {path}")
    return y, sr


def harmonic_component(y, margin: float):
    """提取 HPSS 谐波分量。

    不调用 librosa.effects.hpss：该模块会导入 sklearn.decomposition，
    而发布包不包含 scikit-learn。这里用与 librosa 相同的
    “时间/频率方向中值滤波 + 软掩码”算法，只依赖 numpy/scipy/librosa.stft。
    """
    import numpy as np
    import librosa
    from scipy.ndimage import median_filter

    margin = max(1.0, float(margin))
    stft = librosa.stft(y, n_fft=2048, hop_length=512)
    magnitude = np.abs(stft)
    harmonic = median_filter(magnitude, size=(1, 31), mode="reflect")
    percussive = median_filter(magnitude, size=(31, 1), mode="reflect")

    power = 2.0
    h = harmonic ** power
    p = (percussive * margin) ** power
    denom = h + p
    mask = np.divide(h, denom, out=np.full_like(h, 0.5), where=denom > 0)
    return librosa.istft(stft * mask, hop_length=512, length=len(y))


def separate_audio(
    y,
    sr: int,
    input_path: str,
    config: Dict[str, Any],
    temp_dir: str,
) -> Tuple[Any, str]:
    """返回 (用于节拍分析的音频, 用于推理的音频路径)。"""
    method = config["separate"]
    if method == "none":
        return y, input_path

    import soundfile as sf

    if method == "hpss":
        harmonic = harmonic_component(y, float(config["hpss_margin"]))
        out_path = os.path.join(temp_dir, "harmonic.wav")
        sf.write(out_path, harmonic, sr, subtype="PCM_16")
        return harmonic, out_path

    if method == "demucs":
        out_root = os.path.join(temp_dir, "demucs")
        os.makedirs(out_root, exist_ok=True)
        command = [
            sys.executable,
            "-m",
            "demucs",
            "--two-stems",
            "vocals",
            "-n",
            str(config["demucs_model"]),
            "-o",
            out_root,
            input_path,
        ]
        try:
            completed = subprocess.run(
                command,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                errors="replace",
            )
        except FileNotFoundError as exc:
            raise RuntimeError(
                "无法启动 Demucs。发布包不包含 Demucs/Torch；"
                "如需人声分离，请先在 Python 环境安装 demucs，或改用 --separate hpss"
            ) from exc

        if completed.returncode != 0:
            detail = (completed.stderr or completed.stdout or "").strip()
            if len(detail) > 1200:
                detail = detail[-1200:]
            raise RuntimeError(
                "Demucs 分离失败。请确认已安装 demucs 和 torch，"
                "首次运行还需要下载模型。\n" + detail
            )

        candidates = []
        for root, _, files in os.walk(out_root):
            for name in files:
                if name.lower() == "vocals.wav":
                    candidates.append(os.path.join(root, name))
        if not candidates:
            raise RuntimeError("Demucs 运行完成，但没有找到 vocals.wav")
        vocals_path = sorted(candidates)[0]
        vocal_y, vocal_sr = load_audio(vocals_path)
        if vocal_sr != sr:
            import librosa

            vocal_y = librosa.resample(vocal_y, orig_sr=vocal_sr, target_sr=sr)
        return vocal_y, vocals_path

    raise ValueError(f"未知分离方式: {method}")


def detect_beats(y, sr: int, config: Dict[str, Any]) -> Tuple[float, Any]:
    """返回 (BPM, 拍点时间数组)。失败时回退到 120 BPM 空网格。"""
    import numpy as np
    import librosa

    explicit_bpm = config.get("bpm")
    try:
        onset_env = librosa.onset.onset_strength(y=y, sr=sr)
        if explicit_bpm:
            tempo, beats = librosa.beat.beat_track(
                onset_envelope=onset_env, sr=sr, bpm=float(explicit_bpm), units="time"
            )
        else:
            tempo, beats = librosa.beat.beat_track(
                onset_envelope=onset_env, sr=sr, units="time"
            )
        tempo_value = float(np.atleast_1d(tempo)[0])
        beats = np.asarray(beats, dtype=float).reshape(-1)
        beats = beats[np.isfinite(beats)]
        beats = np.unique(beats)
        if len(beats) >= 2:
            intervals = np.diff(beats)
            beats = beats[np.concatenate(([True], intervals > 1e-3))]
        if explicit_bpm:
            return float(explicit_bpm), beats
        if not math.isfinite(tempo_value) or tempo_value <= 0:
            return 120.0, beats
        return tempo_value, beats
    except Exception:
        if config.get("verbose"):
            traceback.print_exc()
        return float(explicit_bpm or 120.0), np.empty(0, dtype=float)


def time_to_beat(t: float, beats, bpm: float) -> float:
    import numpy as np

    if beats is not None and len(beats) >= 2:
        beats = np.asarray(beats, dtype=float)
        if t <= beats[0]:
            interval = max(beats[1] - beats[0], 1e-3)
            return (t - beats[0]) / interval
        if t >= beats[-1]:
            interval = max(beats[-1] - beats[-2], 1e-3)
            return (len(beats) - 1) + (t - beats[-1]) / interval
        idx = int(np.searchsorted(beats, t, side="right") - 1)
        idx = max(0, min(idx, len(beats) - 2))
        interval = max(beats[idx + 1] - beats[idx], 1e-3)
        return idx + (t - beats[idx]) / interval
    return t * float(bpm or 120.0) / 60.0


def beat_to_time(pos: float, beats, bpm: float) -> float:
    import numpy as np

    if beats is not None and len(beats) >= 2:
        beats = np.asarray(beats, dtype=float)
        if pos <= 0:
            interval = max(beats[1] - beats[0], 1e-3)
            return beats[0] + pos * interval
        if pos >= len(beats) - 1:
            interval = max(beats[-1] - beats[-2], 1e-3)
            return beats[-1] + (pos - (len(beats) - 1)) * interval
        idx = int(math.floor(pos))
        idx = max(0, min(idx, len(beats) - 2))
        frac = pos - idx
        return beats[idx] + frac * (beats[idx + 1] - beats[idx])
    return pos * 60.0 / float(bpm or 120.0)


def quantize_notes(
    notes: List[Dict[str, Any]],
    beats,
    bpm: float,
    grid_step: float,
    min_duration: float,
) -> int:
    """把音符起止吸附到节拍网格，返回被移动的音符数。"""
    moved = 0
    min_beats = max(grid_step, min_duration * float(bpm or 120.0) / 60.0)
    for note in notes:
        old_start = note["start"]
        old_end = note["end"]
        start_pos = time_to_beat(old_start, beats, bpm)
        end_pos = time_to_beat(old_end, beats, bpm)
        q_start = round(start_pos / grid_step) * grid_step
        q_end = round(end_pos / grid_step) * grid_step
        if q_end <= q_start:
            q_end = q_start + min_beats
        new_start = max(0.0, beat_to_time(q_start, beats, bpm))
        new_end = beat_to_time(q_end, beats, bpm)
        if new_end <= new_start:
            new_end = new_start + min_duration
        if abs(new_start - old_start) > 1e-4 or abs(new_end - old_end) > 1e-4:
            moved += 1
        note["start"] = new_start
        note["end"] = new_end
    return moved


def postprocess_notes(
    note_events: Sequence[Sequence[Any]],
    config: Dict[str, Any],
    min_duration: float,
) -> Tuple[List[Dict[str, Any]], Dict[str, int]]:
    stats = {"input": len(note_events), "range": 0, "merged": 0, "mono": 0, "short": 0}
    min_pitch = note_name_from_freq(config.get("min_freq"))
    max_pitch = note_name_from_freq(config.get("max_freq"), ceil=True)

    notes: List[Dict[str, Any]] = []
    seen = set()
    for event in note_events:
        if isinstance(event, dict):
            start = float(event["start"])
            end = float(event["end"])
            pitch = int(event["pitch"])
            amplitude = float(event["amplitude"])
        else:
            start, end, pitch, amplitude = (
                float(event[0]),
                float(event[1]),
                int(event[2]),
                float(event[3]),
            )
        if end <= start:
            continue
        if min_pitch is not None and pitch < min_pitch:
            stats["range"] += 1
            continue
        if max_pitch is not None and pitch > max_pitch:
            stats["range"] += 1
            continue
        key = (round(start, 5), round(end, 5), pitch)
        if key in seen:
            continue
        seen.add(key)
        notes.append(
            {
                "start": start,
                "end": end,
                "pitch": pitch,
                "amplitude": amplitude,
            }
        )

    if not config.get("postprocess", True):
        notes.sort(key=lambda n: (n["start"], n["pitch"]))
        stats["output"] = len(notes)
        return notes, stats

    # 合并同音高、间隔很小的碎片。默认 gap=0 时只做精确去重，
    # 不改变 Basic Pitch 的原始切分，避免影响默认基线。
    gap = max(0.0, float(config["merge_gap_ms"])) / 1000.0
    if gap > 0:
        by_pitch: Dict[int, List[Dict[str, Any]]] = {}
        for note in notes:
            by_pitch.setdefault(note["pitch"], []).append(note)
        notes = []
        for pitch_notes in by_pitch.values():
            pitch_notes.sort(key=lambda n: n["start"])
            merged: List[Dict[str, Any]] = []
            for note in pitch_notes:
                if merged and note["start"] - merged[-1]["end"] <= gap:
                    merged[-1]["end"] = max(merged[-1]["end"], note["end"])
                    merged[-1]["amplitude"] = max(merged[-1]["amplitude"], note["amplitude"])
                    stats["merged"] += 1
                else:
                    merged.append(note)
            notes.extend(merged)

    # 单音模式：后音优先保留高幅度，必要时截断前音。
    if config.get("mono"):
        notes.sort(key=lambda n: (n["start"], -n["amplitude"], n["pitch"]))
        mono_notes: List[Dict[str, Any]] = []
        for note in notes:
            if not mono_notes:
                mono_notes.append(note)
                continue
            prev = mono_notes[-1]
            if note["start"] >= prev["end"] - 1e-6:
                mono_notes.append(note)
                continue
            if note["amplitude"] > prev["amplitude"] * 1.05:
                trimmed_end = min(prev["end"], note["start"])
                if trimmed_end - prev["start"] >= min_duration:
                    prev["end"] = trimmed_end
                    mono_notes.append(note)
                else:
                    mono_notes[-1] = note
                stats["mono"] += 1
            else:
                trimmed_start = max(note["start"], prev["end"])
                if note["end"] - trimmed_start >= min_duration:
                    note["start"] = trimmed_start
                    mono_notes.append(note)
                stats["mono"] += 1
        notes = mono_notes

    notes.sort(key=lambda n: (n["start"], n["pitch"]))
    strict = bool(
        config.get("quantize")
        or config.get("mono")
        or float(config.get("merge_gap_ms", 0.0)) > 0
    )
    cleaned: List[Dict[str, Any]] = []
    for note in notes:
        if strict and note["end"] - note["start"] < min_duration - 1e-6:
            stats["short"] += 1
            continue
        cleaned.append(note)
    stats["output"] = len(cleaned)
    return cleaned, stats


def velocity_from_amplitude(amplitude: float) -> int:
    amplitude = max(0.0, min(1.0, float(amplitude)))
    return max(1, min(127, int(round(amplitude * 127.0))))


def safe_midi_text(text: str, fallback: str = "Melody") -> str:
    text = (text or "").strip()
    if not text:
        return fallback
    try:
        text.encode("latin-1")
        return text
    except UnicodeEncodeError:
        folded = unicodedata.normalize("NFKD", text).encode("ascii", "ignore").decode("ascii")
        folded = " ".join(folded.split()).strip()
        return folded or fallback


def build_tempo_segments(
    beats, bpm: float, end_time: float
) -> List[Tuple[float, float, float]]:
    """返回 [(start_seconds, end_seconds, bpm)]，覆盖 0..end_time。"""
    import numpy as np

    end_time = max(float(end_time), 1e-3)
    if beats is not None and len(beats) >= 2:
        beats = np.asarray(beats, dtype=float)
        intervals = np.diff(beats)
        valid = intervals > 1e-3
        if valid.any():
            segments: List[Tuple[float, float, float]] = []
            first_interval = float(intervals[0])
            if beats[0] > 0.02:
                segments.append((0.0, float(beats[0]), 60.0 / first_interval))
            for idx, interval in enumerate(intervals):
                if interval <= 1e-3:
                    continue
                start = float(beats[idx])
                end = float(beats[idx + 1])
                segments.append((start, end, 60.0 / float(interval)))
            last_end = segments[-1][1] if segments else 0.0
            last_bpm = segments[-1][2] if segments else 120.0
            if end_time > last_end:
                segments.append((last_end, max(end_time, last_end + 1e-3), last_bpm))
            if segments:
                return segments
    return [(0.0, end_time, float(bpm or 120.0))]


def seconds_to_tick(
    t: float, segments: Sequence[Tuple[float, float, float]], ppq: int
) -> int:
    """在给定 tempo map 下，把绝对秒数换算成 MIDI tick。"""
    t = max(0.0, float(t))
    beats_before = 0.0
    for start, end, bpm in segments:
        if t <= start:
            break
        span = min(t, end) - start
        if span > 0:
            beats_before += span * bpm / 60.0
        if t <= end:
            break
    return int(round(beats_before * ppq))


def write_midi(
    path: str,
    notes: Sequence[Dict[str, Any]],
    bpm: float,
    beats,
    tempo_map: bool,
    ppq: int,
    track_name: str,
    program: int,
) -> None:
    import mido

    end_time = max((n["end"] for n in notes), default=0.0)
    if tempo_map:
        segments = build_tempo_segments(beats, bpm, end_time)
    else:
        segments = [(0.0, max(end_time, 1e-3), float(bpm or 120.0))]

    midi = mido.MidiFile(ticks_per_beat=ppq, type=1)
    conductor = mido.MidiTrack()
    midi.tracks.append(conductor)
    conductor.append(mido.MetaMessage("track_name", name="Tempo", time=0))
    conductor.append(mido.MetaMessage("time_signature", numerator=4, denominator=4, time=0))

    last_tick = 0
    for start, _end, segment_bpm in segments:
        tick = seconds_to_tick(start, segments, ppq)
        delta = max(0, tick - last_tick)
        conductor.append(
            mido.MetaMessage(
                "set_tempo",
                tempo=mido.bpm2tempo(max(20.0, min(400.0, float(segment_bpm)))),
                time=delta,
            )
        )
        last_tick = tick
    conductor.append(mido.MetaMessage("end_of_track", time=0))

    track = mido.MidiTrack()
    midi.tracks.append(track)
    track.append(
        mido.MetaMessage("track_name", name=safe_midi_text(track_name), time=0)
    )
    track.append(mido.MetaMessage("text", text="Generated by tab_tool", time=0))
    if 0 <= int(program) <= 127:
        track.append(mido.Message("program_change", program=int(program), channel=0, time=0))

    events: List[Tuple[int, int, Any]] = []
    for note in notes:
        start_tick = seconds_to_tick(note["start"], segments, ppq)
        end_tick = seconds_to_tick(note["end"], segments, ppq)
        if end_tick <= start_tick:
            end_tick = start_tick + 1
        velocity = velocity_from_amplitude(note["amplitude"])
        pitch = max(0, min(127, int(note["pitch"])))
        events.append(
            (
                start_tick,
                1,
                mido.Message("note_on", note=pitch, velocity=velocity, channel=0, time=0),
            )
        )
        events.append(
            (
                end_tick,
                0,
                mido.Message("note_off", note=pitch, velocity=0, channel=0, time=0),
            )
        )

    events.sort(key=lambda item: (item[0], item[1]))
    last_tick = 0
    for tick, _order, message in events:
        message.time = max(0, tick - last_tick)
        track.append(message)
        last_tick = tick
    track.append(mido.MetaMessage("end_of_track", time=0))

    out_dir = os.path.dirname(os.path.abspath(path))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    midi.save(path)


def run_job(
    config: Dict[str, Any],
    input_path: str,
    output_path: str,
    model_path: str,
    model_cache: Dict[str, Any],
    verbose: bool = False,
) -> Dict[str, Any]:
    import numpy as np
    from basic_pitch import inference as bp_inference

    if not os.path.isfile(input_path):
        raise RuntimeError(f"输入文件不存在: {input_path}")

    temp_dir = tempfile.mkdtemp(prefix="tab_tool_py_")
    cleanup_paths: List[str] = []
    try:
        y, sr = load_audio(input_path)
        audio_path = input_path
        if config["separate"] != "none":
            y, audio_path = separate_audio(y, sr, input_path, config, temp_dir)
            if audio_path != input_path:
                cleanup_paths.append(audio_path)

        bpm, beats = detect_beats(y, sr, config)
        if verbose:
            print(
                f"[transcribe] input={input_path} preset={config['preset']} "
                f"bpm={bpm:.2f} beats={len(beats)} separate={config['separate']} "
                f"quantize={config['quantize']} grid={config['quantize_grid']}",
                file=sys.stderr,
            )

        resolved_model = resolve_model_path(model_path)
        model = model_cache.get(resolved_model)
        if model is None:
            if verbose:
                print(f"[transcribe] model={resolved_model}", file=sys.stderr)
            model = bp_inference.Model(resolved_model)
            model_cache[resolved_model] = model

        output = bp_inference.run_inference(audio_path, model)

        fft_hop = getattr(bp_inference, "FFT_HOP", 256)
        audio_rate = getattr(bp_inference, "AUDIO_SAMPLE_RATE", SAMPLE_RATE)
        min_note_frames = max(1, int(round(config["min_note_ms"] / 1000.0 * audio_rate / fft_hop)))
        _, note_events = bp_inference.infer.model_output_to_notes(
            output,
            onset_thresh=float(config["onset_threshold"]),
            frame_thresh=float(config["frame_threshold"]),
            min_note_len=min_note_frames,
            min_freq=config["min_freq"],
            max_freq=config["max_freq"],
            include_pitch_bends=False,
            melodia_trick=True,
            midi_tempo=float(bpm),
        )

        min_duration = float(config["min_note_ms"]) / 1000.0
        notes, stats = postprocess_notes(note_events, config, min_duration)

        moved = 0
        if config["quantize"] and notes:
            moved = quantize_notes(
                notes,
                beats,
                bpm,
                GRID_STEPS[config["quantize_grid"]],
                min_duration,
            )
            notes, quant_stats = postprocess_notes(notes, config, min_duration)
            stats["short"] += quant_stats.get("short", 0)
            stats["mono"] += quant_stats.get("mono", 0)
            stats["merged"] += quant_stats.get("merged", 0)

        write_midi(
            output_path,
            notes,
            bpm,
            beats,
            bool(config["tempo_map"]) and not config.get("bpm"),
            480,
            config.get("track_name") or "Melody",
            int(config.get("program", -1)),
        )

        return {
            "ok": True,
            "notes": len(notes),
            "output": output_path,
            "bpm": round(float(bpm), 3),
            "beats": int(len(beats)),
            "quantized": bool(config["quantize"]),
            "quantize_moved": int(moved),
            "preset": config["preset"],
            "separate": config["separate"],
            "postprocess": {
                "input": int(stats.get("input", 0)),
                "output": int(stats.get("output", len(notes))),
                "range": int(stats.get("range", 0)),
                "merged": int(stats.get("merged", 0)),
                "mono": int(stats.get("mono", 0)),
                "short": int(stats.get("short", 0)),
            },
        }
    finally:
        for path in cleanup_paths:
            try:
                if os.path.isdir(path):
                    shutil.rmtree(path, ignore_errors=True)
                elif os.path.exists(path):
                    os.remove(path)
            except Exception:
                pass
        shutil.rmtree(temp_dir, ignore_errors=True)


def run_worker(args: argparse.Namespace) -> int:
    # worker 启动时不立即加载模型，避免启动阻塞；首个任务会加载并缓存。
    emit({"ready": True})
    model_cache: Dict[str, Any] = {}
    for raw_line in sys.stdin:
        line = raw_line.strip()
        if not line:
            continue
        try:
            job = json.loads(line)
        except Exception as exc:  # noqa: BLE001
            emit({"ok": False, "error": f"无法解析任务 JSON: {exc}"})
            continue

        try:
            merged = vars(args).copy()
            merged.update(job)
            merged["worker"] = False
            job_args = argparse.Namespace(**merged)
            config = resolve_config(job_args)
            result = run_job(
                config,
                job_args.input,
                job_args.output,
                job_args.model,
                model_cache,
                verbose=bool(job_args.verbose),
            )
            emit(result)
        except Exception as exc:  # noqa: BLE001
            traceback.print_exc()
            emit({"ok": False, "error": str(exc)})
    return 0


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    configure_stdio()

    if args.worker:
        return run_worker(args)

    if not args.input:
        emit({"ok": False, "error": "缺少 --input"})
        return 2
    if not args.output:
        emit({"ok": False, "error": "缺少 --output"})
        return 2

    try:
        config = resolve_config(args)
        result = run_job(
            config,
            args.input,
            args.output,
            args.model,
            {},
            verbose=args.verbose,
        )
        emit(result)
        return 0
    except Exception as exc:  # noqa: BLE001
        traceback.print_exc()
        emit({"ok": False, "error": str(exc)})
        return 1


if __name__ == "__main__":
    sys.exit(main())
