# tab_tool

基于 C++ 命令行的音频自动扒谱工具。C++ 负责音频解码、流程编排和 MIDI 校验，Python 侧用
[Basic Pitch](https://github.com/spotify/basic-pitch) 做音频转 MIDI，通过子进程解耦。

## 架构

```
CLI (C++)
  └── TranscribeJob
        ├── AudioDecoder       WAV/MP3 -> 标准 PCM WAV（dr_wav / dr_mp3）
        ├── PythonTranscriber  子进程调用 python/scripts/transcribe.py
        └── MidiValidator      校验输出 MIDI
```

- `src/domain/`：`ITranscriber` 接口、任务参数
- `src/app/`：任务编排
- `src/infra/`：音频解码、Python 子进程、MIDI 校验
- `src/cli/`：参数解析与日志

## 构建

推荐 Visual Studio 2022（MSVC）。也支持 MinGW g++。

```powershell
# 一键构建（优先 MSVC，找不到则回退 g++）
.\build.ps1
```

手动 CMake 构建：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

产物：`build/Release/tab_tool.exe`（或 `build/tab_tool.exe`，取决于生成器）。

> 如果项目路径包含中文，MinGW Makefiles 可能无法正确生成构建规则，建议使用 MSVC，
> 或把项目放到纯英文路径下。直接用 `build.ps1` 的 g++ 分支可以绕过 CMake。

## 开发模式运行

开发时可以不打包，直接用系统 Python：

```powershell
# 安装依赖
pip install basic-pitch

# 运行（--python 指向系统 Python，--script 指向仓库里的脚本）
.\build\Release\tab_tool.exe song.wav `
  --python "$(Get-Command python).Source" `
  --script .\scripts\transcribe.py `
  -v
```

## 打包发布（开箱即用）

```powershell
# 先编译
cmake --build build --config Release

# 打包：把 Python 安装目录复制进 dist，并安装 basic-pitch
.\scripts\package.ps1 -PythonHome "C:\Python311"
```

发布目录结构：

```
dist/
├── tab_tool.exe
├── python/            # 完整 Python 运行时
├── scripts/
│   └── transcribe.py
├── models/
│   └── basic_pitch/
└── README.txt
```

用户解压后直接运行，无需安装 Python：

```powershell
.\tab_tool.exe song.wav
```

> 注意：Basic Pitch 依赖 TensorFlow，发布包约 500~600 MB。

## 命令行

```
tab_tool <input.wav|input.mp3> [选项]

  -o, --output <path>        输出 MIDI 路径（默认与输入同名 .mid）
  -m, --model <dir>          模型目录（默认 <exe>/models/basic_pitch）
      --python <path>        Python 解释器（默认 <exe>/python/python.exe）
      --script <path>        推理脚本（默认 <exe>/scripts/transcribe.py）
      --onset-threshold <f>  起音阈值，0~1，默认 0.5
      --frame-threshold <f>  音帧阈值，0~1，默认 0.3
      --min-note-ms <f>      最短音符毫秒数，默认 58
  -v, --verbose              输出详细日志
      --keep-temp            保留临时文件（调试用）
      --version              显示版本
  -h, --help                 显示帮助
```

## 评估

准备预测 MIDI 和人工标注 MIDI，计算 note F1：

```powershell
python tests/eval/eval.py --pred output.mid --gt ground_truth.mid
```

建议维护 3~5 首带标注的短片段作为回归测试集，每次调参后对比 F1。

## 已知限制

- 第一版聚焦独奏/主旋律（钢琴、吉他、清唱），复音和强伴奏效果有限。
- 不输出乐谱（MusicXML），只输出 MIDI。
- 不支持实时输入。
