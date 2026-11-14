# tab_tool

基于 C++ 命令行的音频自动扒谱工具。C++ 负责音频解码、批处理编排、MIDI 校验和常驻 worker 管理，
Python 侧用 [Basic Pitch](https://github.com/spotify/basic-pitch) 做音频转 MIDI，通过子进程解耦。
默认使用 Basic Pitch 自带的 ONNX 模型和 onnxruntime，发布包不需要 TensorFlow。

预编译的 Windows 发布包见 [GitHub Releases](https://github.com/StellavacuaLuxumbra/Seanami/releases)，
解压后可直接运行，无需安装 Python。

## v0.2.0 新功能

- FLAC 输入（WAV / MP3 / FLAC）
- 节拍检测与量化（opt-in，支持 1/4 ~ 1/32、三连音网格）
- 乐器预设：`vocal`、`guitar`、`piano`、`bass`
- 后处理：音域过滤、单音化、碎片合并、精确去重
- 常驻 Python worker：批量处理只加载一次模型
- 并行批处理 `--jobs N`、跳过已存在输出 `--skip-existing`、CSV/JSON 汇总报告
- HPSS 谐波分离（内置，无需额外依赖）；可选 Demucs 人声分离
- DAW 友好 MIDI：tempo map、4/4 拍号、轨道名、GM program、力度

## 功能

- 单文件或目录批量处理，目录可递归
- 批量默认复用常驻 worker；`--jobs N` 可并行多个 worker
- 失败不中断整批，退出码和报告可区分成功/失败/跳过
- 输出标准 MIDI，默认写入检测到的速度地图和 4/4 拍号
- 可指定轨道名、GM 乐器编号和量化网格
- 内置小型 vocadito 真实音频基准，用 note F1 评估

## 架构

```
CLI (C++)
  └── BatchRunner              顺序 / 并行 / 跳过 / 汇总报告
        └── TranscribeJob
              ├── AudioDecoder       WAV/MP3/FLAC -> 标准 PCM WAV（dr_wav / dr_mp3 / dr_flac）
              ├── PythonTranscriber  常驻 worker 或单次子进程调用 python/scripts/transcribe.py
              └── MidiValidator      校验输出 MIDI
```

- `src/domain/`：`ITranscriber` 接口、任务参数、统计信息
- `src/app/`：单任务流水线、批处理与报告
- `src/infra/`：音频解码、Python worker/子进程、MIDI 校验
- `src/cli/`：参数解析、预设、日志

## 构建

一键构建，按以下顺序选择工具链：

1. `tools\ziglang` 中的 Zig（推荐，生成 64 位静态 exe，发布包不需要编译器 DLL）
2. Visual Studio 2022 / MSVC
3. MinGW g++

```powershell
.\build.ps1
```

> 仓库自带的发布包使用 Zig 构建一次完成，产物可直接拷到没有装编译器的机器运行。
> 如需在新机器上复现静态构建，可先安装 Zig 工具链（仅构建需要，不进发布包）：

```powershell
tools\python311\python.exe -m pip install --target tools\ziglang ziglang
```

手动 CMake 构建（MSVC 动态运行时）：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

产物：`build/Release/tab_tool.exe`（MSVC 生成器）或 `build/tab_tool.exe`（`build.ps1` / MinGW）。

> 如果项目路径包含中文，MinGW Makefiles 可能无法正确生成构建规则，建议使用 MSVC，
> 或把项目放到纯英文路径下。直接用 `build.ps1` 的 g++ 分支可以绕过 CMake。
> 动态链接的 MinGW 构建需要 `libstdc++-6.dll` 等同目录运行时；`build.ps1`
> 的 Zig 分支为静态链接，没有这个依赖。

## 安装运行时（一次）

```powershell
# 自动下载 Python 3.11，并安装 basic-pitch + onnxruntime 到 tools\python311
.\scripts\install_python.ps1
```

> 开发环境会安装 TensorFlow（basic-pitch 的默认依赖），但发布包默认使用 ONNX，
> 打包时会移除 TensorFlow 和 scikit-learn 等非必需文件。发布目录约 515 MB。

## 开发模式运行

```powershell
# 构建
.\build.ps1

# 生成一段测试旋律（可选）
python tests\make_sample.py

# 用内置 Python 跑完整流程
.\scripts\run_dev.ps1 tests\samples\melody.wav
```

也可以手动指定解释器：

```powershell
.\build\tab_tool.exe song.wav `
  --python .\tools\python311\python.exe `
  --script .\scripts\transcribe.py `
  -v
```

## 批量处理与 worker

输入可以是目录，默认只处理该目录第一层，`-r` 递归子目录：

```powershell
# 处理整个目录，MIDI 输出到 out\
.\build\tab_tool.exe .\music -o .\out

# 递归子目录，并保留相对目录结构
.\build\tab_tool.exe .\music -r -o .\out

# 4 个 worker 并行，跳过已有输出，写 CSV 报告
.\build\tab_tool.exe .\music -r -o .\out --jobs 4 --skip-existing --report .\report.csv

# 完成后用系统默认程序打开第一个 MIDI
.\build\tab_tool.exe .\music -r -o .\out --open
```

规则：

- 单文件且 `-o` 不存在时，`-o` 按文件路径处理。
- 输入是目录，或 `-o` 已存在且是目录时，`-o` 按输出目录处理。
- 输出目录会自动创建；同名文件会被覆盖。
- 某个文件失败不会中断整批，最后汇总成功/失败/跳过数量，有失败时退出码为 4。
- 报告扩展名为 `.csv` 时写 CSV，其它扩展名写 JSON。
- 批处理默认启用常驻 Python worker，模型只加载一次；`--jobs N` 会启动 N 个独立 worker
  并行处理，内存占用也随 worker 数量增加。
- worker 崩溃或协议异常时，该任务会自动回退到单次子进程；`--no-worker` 可完全禁用 worker。

进程与退出码：

| 退出码 | 含义 |
| --- | --- |
| 0 | 全部成功（或被 `--skip-existing` 跳过） |
| 1 | 参数错误 |
| 2 | 输入不存在或没有可处理的音频 |
| 4 | 部分文件失败 |
| 5 | 报告写入失败 |

## 节拍、量化与预设

节拍检测默认开启，用于写 tempo map；量化默认关闭。需要把音符吸附到节拍网格时显式加
`--quantize`：

```powershell
# 自动检测 BPM，并吸附到 1/16 网格
.\build\tab_tool.exe song.wav --quantize --quantize-grid 1/16

# 固定 96 BPM，只写单一速度
.\build\tab_tool.exe song.wav --bpm 96 --quantize --quantize-grid 1/8
```

支持的网格：`1/4`、`1/8`、`1/16`、`1/32`、`1/8T`、`1/16T`。

> 量化会让 MIDI 更贴合 DAW 网格，但对严格按原始演奏时间标注的 note F1 有影响：
> 在 vocadito 10 段上，默认 F1 0.523，开启量化后为 0.454。因此量化保持 opt-in。

预设只调整适合该乐器的检测参数，不会自动打开量化或碎片合并；需要时再显式加
`--quantize` / `--merge-gap-ms`。命令行显式参数始终覆盖预设。

| 预设 | 音域 | 单音 | 最短音符 | onset 阈值 | GM program |
| --- | --- | --- | --- | --- | --- |
| `default` | 不限 | 否 | 127.7 ms | 0.50 | 不写入 |
| `vocal` | E2 ~ C6 | 是 | 100 ms | 0.45 | 53 Voice Oohs |
| `guitar` | E2 ~ E6 | 是 | 100 ms | 0.50 | 25 Acoustic Guitar |
| `piano` | A0 ~ C8 | 否 | 90 ms | 0.50 | 0 Acoustic Grand Piano |
| `bass` | B0 ~ G4 | 是 | 90 ms | 0.50 | 33 Electric Bass |

```powershell
.\build\tab_tool.exe song.wav --preset vocal
.\build\tab_tool.exe song.wav --preset vocal --quantize
.\build\tab_tool.exe song.wav --preset guitar --min-freq 100
```

## 人声 / 伴奏分离

```powershell
# 内置 HPSS，只保留谐波分量，不需要额外依赖
.\build\tab_tool.exe song.wav --separate hpss

# 可选 Demucs（发布包不包含，需要自行安装 demucs 和 torch）
.\build\tab_tool.exe song.wav --separate demucs --demucs-model htdemucs
```

- `hpss` 使用 STFT 中值滤波提取谐波分量，适合削弱鼓点等打击性伴奏；比不分离慢约 60%。
- `demucs` 首次运行会下载模型；未安装时会给出明确错误，并建议改用 `hpss`。
- 在 vocadito 10 段清唱数据上，HPSS 默认 F1 从 0.523 提升到 0.538。

## DAW 友好 MIDI

- 默认写入 tempo map（随节拍变化的 `set_tempo`）和 4/4 拍号；`--no-tempo-map` 只写单一速度。
- 显式 `--bpm` 时只写单一速度，不再生成 tempo map。
- 默认轨道名取输入文件名，可用 `--track-name` 覆盖。
- 预设会自动写入对应的 GM program；`--program 0~127` 可覆盖，`--program -1` 表示不写入。
- note on/off 按 tick 排序，同 tick 时 note off 优先，避免相邻同音高音符粘连。

## 打包发布（开箱即用）

```powershell
# 先编译
.\build.ps1

# 打包：自动使用 tools\python311，复制进 dist 并生成 README
.\scripts\package.ps1

# 同时生成 dist.zip，方便分发
.\scripts\package.ps1 -Zip
```

如果 Python 装在别处，可以显式指定：

```powershell
.\scripts\package.ps1 -PythonHome "C:\Python311"
```

发布目录结构：

```
dist/
├── tab_tool.exe           # 64 位静态链接（Zig 构建），无需编译器 DLL
├── msvcp140*.dll          # onnxruntime 需要的 MSVC 运行时
├── vcruntime140*.dll
├── python/                # 精简后的 Python 运行时 + basic-pitch + onnxruntime
├── scripts/
│   └── transcribe.py
├── models/
│   └── basic_pitch/       # 预留目录，空时回退到包内置 ONNX 模型
├── LICENSE
├── THIRD_PARTY_NOTICES.md
└── README.txt
```

> 如果改用动态链接的 MinGW 构建，`package.ps1` 会自动从匹配的 64 位 MinGW
> 目录复制 `libstdc++-6.dll`、`libgcc_s_seh-1.dll`、`libwinpthread-1.dll`。

用户解压后直接运行，无需安装 Python：

```powershell
.\tab_tool.exe song.wav
```

> 发布包约 515 MB（已移除 TensorFlow、keras、sklearn 等）。压缩后的 `dist.zip` 约 155 MB。
> 如果显式传入 `-m` 指向 TensorFlow SavedModel，仍需要自行准备 TensorFlow 运行时。
>
> 打包时 Python 运行时用硬链接镜像（同一磁盘上近乎瞬时，且不重复占用空间）；
> 跨盘或不支持硬链接时自动回退为普通复制。

## 效果与性能

在 vocadito 的 10 段独唱人声上评估（每段有两位音乐人的独立标注）。指标为 mir_eval 的
note onset + pitch F1：onset 容差 50 ms，音高容差 50 cents，忽略 offset。每段分别对
A1/A2 计算，取较高值作为该段的 A-max，再对 10 段求平均。

| 最短音符 | 平均 F1 | 精确率 | 召回率 | 平均音符数/段 |
| --- | --- | --- | --- | --- |
| 58 ms | 0.523 | 0.438 | 0.655 | 86.4 |
| 80 ms | 0.535 | 0.474 | 0.620 | 73.3 |
| 100 ms | 0.526 | 0.500 | 0.562 | 62.9 |
| 127.7 ms（默认） | 0.523 | 0.526 | 0.526 | 55.1 |

v0.2.0 功能对比（均为默认 127.7 ms 网格）：

| 配置 | 平均 F1 | 精确率 | 召回率 | 平均音符数/段 |
| --- | --- | --- | --- | --- |
| 默认 | 0.523 | 0.526 | 0.526 | 55.1 |
| `--preset vocal` | 0.521 | 0.494 | 0.557 | 62.7 |
| `--separate hpss` | 0.538 | 0.540 | 0.541 | 55.0 |
| `--quantize` | 0.454 | 0.485 | 0.436 | 49.7 |

- 两位人工标注者之间的平均 F1 是 0.726，可作为这组数据的一致性参考。
- 单段差异较大：最好 0.694（vocadito_8），最难 0.327（vocadito_10，快速装饰音较多）。
- 默认 127.7 ms 与 Basic Pitch 0.4.0 官方默认一致，精确率/召回最均衡，碎片也最少。
- `vocal` 预设把音域下限从“不限”收紧到 E2，覆盖男声；在 vocadito_10 上从默认 28 个音符
  提升到 38 个，F1 从 0.327 提升到 0.411，同时整体 F1 与默认基本持平。
- HPSS 对清唱数据有小幅提升；对强伴奏、鼓点明显的混音，分离收益通常更明显。
- 与 Basic Pitch 官方参考输出对比：vocadito_10 的默认输出与上游模型逐音一致，
  说明 C++ 解码、参数传递和 Python 推理链路与上游模型一致；上面的 0.52 是模型相对
  人工标注的真实水平。
- 典型表现：旋律轮廓和主要音高稳定；快速经过音、装饰音、气声/滑音容易漏检或合并。
  复音和强伴奏仍不是强项。

性能（Windows，CPU，发布包内 Python，热启动）：

| 场景 | 总耗时 |
| --- | --- |
| 单个 3 秒文件（含模型加载） | 约 7~8 秒 |
| 10 段 vocadito 批量（worker 复用模型） | 约 13.6 秒 |
| 10 段 + `--separate hpss` | 约 22.7 秒 |
| 3 个文件 + `--jobs 2` | 约 11.3 秒 |

首次从 zip 解压后运行可能触发 Windows Defender 扫描和 numba 首次编译，冷启动可能达到
1~3 分钟；之后会恢复正常。批量目录默认复用一个 worker，模型只加载一次；`--jobs N` 会并行
加载 N 份模型，适合文件很多且内存充足的场景。

## 命令行

```
tab_tool <input.wav|input.mp3|input.flac|目录> [选项]

输出:
  -o, --output <path>        输出 MIDI 路径；多文件时为输出目录
  -r, --recursive            目录输入时递归子目录
      --open                 完成后用系统默认程序打开第一个 MIDI
      --track-name <name>    MIDI 轨道名（默认使用输入文件名）
      --program <0-127>      GM 乐器编号（-1 表示不写入）
      --tempo-map            写入检测到的速度地图（默认开启）
      --no-tempo-map         只写单一速度

分析:
      --preset <name>        default / vocal / guitar / piano / bass
      --onset-threshold <f>  起音阈值，0~1
      --frame-threshold <f>  音帧阈值，0~1
      --min-note-ms <f>      最短音符毫秒数
      --min-freq <hz>        最低音高频率，0 表示不限制
      --max-freq <hz>        最高音高频率，0 表示不限制
      --mono / --poly        单音 / 复音输出
      --quantize             吸附到节拍网格
      --no-quantize          不量化（默认）
      --quantize-grid <g>    1/4、1/8、1/16、1/32、1/8T、1/16T
      --bpm <f>              固定 BPM；0 表示自动检测
      --merge-gap-ms <f>     合并同音高碎片的最大间隔
      --postprocess          启用后处理（默认开启）
      --no-postprocess       关闭后处理
      --separate <mode>      none / hpss / demucs
      --hpss-margin <f>      HPSS 分离强度，>=1.0（默认 1.0）
      --demucs-model <name>  Demucs 模型（默认 htdemucs）

批量:
      --jobs <n>             并行任务数，1~16（默认 1）
      --skip-existing        跳过已存在的输出 MIDI
      --report <path>        写入 JSON 或 CSV 汇总报告
      --no-worker            禁用常驻 Python worker

运行环境:
  -m, --model <dir>          模型目录（默认使用 basic-pitch 内置 ONNX 模型）
      --python <path>        Python 解释器（默认 <exe>/python/python.exe）
      --script <path>        推理脚本（默认 <exe>/scripts/transcribe.py）
  -v, --verbose              输出详细日志
      --keep-temp            保留临时文件（调试用）
      --version              显示版本
  -h, --help                 显示帮助
```

## 测试

不依赖 TensorFlow 的集成测试（使用假推理脚本验证解码、worker、批量、报告、跳过、预设和 FLAC）：

```powershell
.\tests\integration.ps1
```

保留测试输出：

```powershell
.\tests\integration.ps1 -KeepOutput
```

真实推理冒烟测试（需要 basic-pitch / onnxruntime）：

```powershell
.\tests\real_inference.ps1
.\tests\real_inference.ps1 -InputFile .\tests\real\vocadito\vocadito_10.wav
```

## 评估

单文件对比预测 MIDI 和人工标注。标注可以是 MIDI，也可以是 vocadito 风格的 CSV：

```powershell
.\tools\python311\python.exe tests\eval\eval.py --pred output.mid --gt ground_truth.mid
.\tools\python311\python.exe tests\eval\eval.py --pred output.mid --gt vocadito_10_notesA1.csv
```

批量跑 vocadito 基准（首次会按需下载 10 段音频和标注，约 18 MB）：

```powershell
.\tools\python311\python.exe tests\eval\fetch_vocadito.py
.\tools\python311\python.exe tests\eval\vocadito_bench.py --data-dir tests\real\vocadito

# 对比其它最短音符阈值
.\tools\python311\python.exe tests\eval\vocadito_bench.py --data-dir tests\real\vocadito --min-note-ms 100
```

数据来自 [vocadito](https://zenodo.org/records/5557945)（CC BY 4.0，Bittner et al.）。
下载脚本只按 HTTP Range 取需要的片段，不下载完整的 58 MB 压缩包。`tests/real/`
已在 `.gitignore` 中，不会进入仓库。

## 已知限制

- 聚焦独奏/主旋律（钢琴、吉他、清唱、贝斯），强复音和重伴奏仍有限。
- 节拍检测对自由速度、弱起和无明显鼓点的素材可能不稳定，可用 `--bpm` 指定固定速度。
- 量化按 note F1 评估会降低分数，是否开启取决于更看重“严格演奏时间”还是“DAW 网格”。
- 发布包不包含 Demucs / Torch；需要更好的分离效果时请自行安装。
- 不输出乐谱（MusicXML），只输出 MIDI。
- 不支持实时输入。

## 许可证

本项目源码采用 MIT 许可证，见 [LICENSE](LICENSE)。发布包内含的第三方组件按各自许可证分发，
详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
