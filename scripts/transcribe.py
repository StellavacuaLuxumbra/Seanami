#!/usr/bin/env python3
"""Basic Pitch 推理入口。

C++ 通过子进程调用本脚本，约定：
- 输入：--input <wav> --output <mid> ...
- 输出：stdout 最后一行是 JSON 摘要，例如 {"ok": true, "notes": 123}
- 失败：返回非 0 退出码，并在 JSON 中给出 error
"""

import argparse
import json
import os
import sys
import traceback


def configure_stdio() -> None:
    """统一 UTF-8，避免 Windows 控制台代码页导致中文乱码。"""
    try:
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")
    except Exception:
        pass


def emit(payload: dict) -> None:
    print(json.dumps(payload, ensure_ascii=False))


def main() -> int:
    parser = argparse.ArgumentParser(description="Basic Pitch 推理脚本")
    parser.add_argument("--input", required=True, help="输入 WAV 路径")
    parser.add_argument("--output", required=True, help="输出 MIDI 路径")
    parser.add_argument("--model", default="", help="模型目录，空则用包内置模型")
    parser.add_argument("--onset-threshold", type=float, default=0.5)
    parser.add_argument("--frame-threshold", type=float, default=0.3)
    parser.add_argument("--min-note-ms", type=float, default=58.0)
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    configure_stdio()

    if not os.path.isfile(args.input):
        emit({"ok": False, "error": f"输入文件不存在: {args.input}"})
        return 2

    try:
        import basic_pitch
        from basic_pitch import ICASSP_2022_MODEL_PATH
        from basic_pitch.inference import predict
    except Exception as exc:  # noqa: BLE001
        emit(
            {
                "ok": False,
                "error": (
                    "无法导入 basic_pitch，请先安装: pip install basic-pitch；"
                    f"原始错误: {exc}"
                ),
            }
        )
        return 2

    model_path = args.model
    if not model_path or not os.path.exists(model_path) or (
        os.path.isdir(model_path) and not os.listdir(model_path)
    ):
        # 优先使用 Basic Pitch 自带的 ONNX 模型，避免依赖体积巨大的 TensorFlow；
        # 没装 onnxruntime 时回退到包默认模型（可能是 TF SavedModel）。
        if getattr(basic_pitch, "ONNX_PRESENT", False):
            model_path = str(
                basic_pitch.build_icassp_2022_model_path(
                    basic_pitch.FilenameSuffix.onnx
                )
            )
        else:
            model_path = str(ICASSP_2022_MODEL_PATH)

    if args.verbose:
        print(f"[transcribe] input={args.input}", file=sys.stderr)
        print(f"[transcribe] model={model_path}", file=sys.stderr)
        print(f"[transcribe] onset={args.onset_threshold}", file=sys.stderr)
        print(f"[transcribe] frame={args.frame_threshold}", file=sys.stderr)
        print(f"[transcribe] min_note_ms={args.min_note_ms}", file=sys.stderr)

    try:
        _, midi_data, note_events = predict(
            args.input,
            model_path,
            onset_threshold=args.onset_threshold,
            frame_threshold=args.frame_threshold,
            minimum_note_length=args.min_note_ms,
        )
        midi_data.write(args.output)
        emit({"ok": True, "notes": len(note_events), "output": args.output})
        return 0
    except Exception as exc:  # noqa: BLE001
        traceback.print_exc()
        emit({"ok": False, "error": str(exc)})
        return 1


if __name__ == "__main__":
    sys.exit(main())
