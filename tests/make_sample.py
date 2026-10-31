#!/usr/bin/env python3
"""生成一段用于冒烟测试的合成旋律（C4 D4 E4 F4 G4）。

Basic Pitch 在真实乐器上效果最好，这里用带泛音的合成音做流程验证。
"""

import math
import os
import struct
import wave


def synth_note(freq: float, duration: float, sample_rate: int, amplitude: float = 0.5):
    samples = []
    count = int(sample_rate * duration)
    for i in range(count):
        t = i / sample_rate
        # 快速起音 + 指数衰减，模拟拨弦/钢琴包络
        envelope = min(1.0, t / 0.01) * math.exp(-2.5 * t)
        value = (
            math.sin(2 * math.pi * freq * t)
            + 0.5 * math.sin(2 * math.pi * 2 * freq * t)
            + 0.25 * math.sin(2 * math.pi * 3 * freq * t)
        ) / 1.75
        samples.append(amplitude * envelope * value)
    return samples


def main() -> int:
    sample_rate = 22050
    notes = [261.63, 293.66, 329.63, 349.23, 392.00]  # C4 D4 E4 F4 G4
    audio = []
    for freq in notes:
        audio.extend(synth_note(freq, 0.5, sample_rate))
        audio.extend([0.0] * int(sample_rate * 0.1))

    out_dir = os.path.join(os.path.dirname(__file__), "samples")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "melody.wav")

    with wave.open(out_path, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(sample_rate)
        for sample in audio:
            clamped = max(-1.0, min(1.0, sample))
            wav.writeframes(struct.pack("<h", int(clamped * 32767)))

    print(out_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
