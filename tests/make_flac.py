#!/usr/bin/env python3
"""把 WAV 转成 FLAC，用于集成测试验证 FLAC 解码路径。"""

import sys

import soundfile as sf


def main() -> int:
    if len(sys.argv) < 3:
        print("用法: make_flac.py <input.wav> <output.flac>")
        return 2
    src, dst = sys.argv[1], sys.argv[2]
    data, sample_rate = sf.read(src, always_2d=False)
    sf.write(dst, data, sample_rate, format="FLAC")
    print(dst)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
