# tab_tool

基于 C++ 命令行的音频自动扒谱工具。C++ 负责音频解码、流程编排和 MIDI 校验，Python 侧用
[Basic Pitch](https://github.com/spotify/basic-pitch) 做音频转 MIDI，通过子进程解耦。
默认使用 Basic Pitch 自带的 ONNX 模型和 onnxruntime，发布包不需要 TensorFlow。

预编译的 Windows 发布包见 [GitHub Releases](https://github.com/StellavacuaLuxumbra/Seanami/releases)，
解压后可直接运行，无需安装 Python。

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

## 安装运行时（一次）

```powershell
# 自动下载 Python 3.11，并安装 basic-pitch + onnxruntime 到 tools\python311
.\scripts\install_python.ps1
```

> 开发环境会安装 TensorFlow（basic-pitch 的默认依赖），但发布包默认使用 ONNX，
> 打包时会移除 TensorFlow 相关文件，体积从约 1.5 GB 降到约 480 MB。

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

## 批量处理

输入可以是目录，默认只处理该目录第一层，`-r` 递归子目录：

```powershell
# 处理整个目录，MIDI 输出到 out\
.\build\tab_tool.exe .\music -o .\out

# 递归子目录，并保留相对目录结构
.\build\tab_tool.exe .\music -r -o .\out

# 完成后用系统默认程序打开第一个 MIDI
.\build\tab_tool.exe .\music -r -o .\out --open
```

规则：

- 单文件且 `-o` 不存在时，`-o` 按文件路径处理。
- 输入是目录，或 `-o` 已存在且是目录时，`-o` 按输出目录处理。
- 输出目录会自动创建；同名文件会被覆盖。
- 某个文件失败不会中断整批，最后汇总成功/失败数量，有失败时退出码为 4。

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
├── tab_tool.exe
├── libstdc++-6.dll        # MinGW 构建时自动带上
├── libgcc_s_seh-1.dll
├── libwinpthread-1.dll
├── msvcp140*.dll          # onnxruntime 需要的 MSVC 运行时
├── python/                # 精简后的 Python 运行时 + basic-pitch + onnxruntime
├── scripts/
│   └── transcribe.py
├── models/
│   └── basic_pitch/       # 预留目录，空时回退到包内置 ONNX 模型
└── README.txt
```

用户解压后直接运行，无需安装 Python：

```powershell
.\tab_tool.exe song.wav
```

> 发布包约 480 MB（已移除 TensorFlow），压缩后的 `dist.zip` 约 155 MB。如果显式传入 `-m` 指向 TensorFlow SavedModel，
> 仍需要自行准备 TensorFlow 运行时。
>
> 打包时 Python 运行时用硬链接镜像（同一磁盘上近乎瞬时，且不重复占用空间）；
> 跨盘或不支持硬链接时自动回退为普通复制。

## 效果与性能

在 vocadito 的 10 段独唱人声上评估（每段有两位音乐人的独立标注）。指标为 mir_eval 的
note onset + pitch F1：onset 容差 50 ms，音高容差 50 cents，忽略 offset。每段分别对
A1/A2 计算，取较高值作为该段的 A-max，再对 10 段求平均。

| 最短音符 | 平均 F1 | 精确率 | 召回率 | 平均音符数/段 |
| --- | --- | --- | --- | --- |
| 58 ms | 0.525 | 0.439 | 0.657 | 86.4 |
| 80 ms | 0.536 | 0.474 | 0.620 | 73.3 |
| 100 ms | 0.526 | 0.501 | 0.562 | 62.9 |
| 127.7 ms（默认） | 0.520 | 0.523 | 0.523 | 55.1 |

- 两位人工标注者之间的平均 F1 是 0.726，可作为这组数据的一致性参考。
- 单段差异较大：最好 0.694（vocadito_8），最难 0.327（vocadito_10，快速装饰音较多）。
- 默认 127.7 ms 与 Basic Pitch 0.4.0 官方默认一致，精确率/召回最均衡，碎片也最少
  （两位标注者的平均音符数分别为 58.0 和 47.8）。
- 如果更看重召回，可以降到 80~100 ms，代价是更多短碎片和误检。
- 与 Basic Pitch 官方参考输出对比：vocadito_10 的 28 个音符逐音一致（F1 = 1.0），
  说明 C++ 解码、参数传递和 Python 推理链路与上游模型一致；上面的 0.52 是模型相对
  人工标注的真实水平。
- 典型表现：旋律轮廓和主要音高稳定；快速经过音、装饰音、气声/滑音容易漏检或合并。
  复音和强伴奏不在第一版的目标范围内。

性能（Windows，CPU，含 Python 启动和模型加载）：

| 音频长度 | 总耗时 |
| --- | --- |
| 9.1 s | 15.3 s |
| 63.7 s | 15.2 s |
| 298.8 s | 20.0 s |

热启动约 6~8 秒，之后长音频端到端约为实时的 15~20 倍（5 分钟音频实测 12~20 秒）。
首次从 zip 解压后运行可能触发 Windows Defender 等杀毒软件扫描，冷启动实测约 55 秒；
之后会恢复正常。批量目录会为每个文件启动一次 Python，文件很多时启动开销会累加。

## 命令行

```
tab_tool <input.wav|input.mp3|目录> [选项]

输入可以是单个音频文件，也可以是包含 WAV/MP3 的目录。

  -o, --output <path>        输出 MIDI 路径；多文件时为输出目录
  -r, --recursive            目录输入时递归子目录
      --open                 完成后用系统默认程序打开第一个 MIDI
  -m, --model <dir>          模型目录（默认使用 basic-pitch 内置模型）
      --python <path>        Python 解释器（默认 <exe>/python/python.exe）
      --script <path>        推理脚本（默认 <exe>/scripts/transcribe.py）
      --onset-threshold <f>  起音阈值，0~1，默认 0.5
      --frame-threshold <f>  音帧阈值，0~1，默认 0.3
      --min-note-ms <f>      最短音符毫秒数，默认 127.7
  -v, --verbose              输出详细日志
      --keep-temp            保留临时文件（调试用）
      --version              显示版本
  -h, --help                 显示帮助
```

## 测试

不依赖 TensorFlow 的集成测试（用假推理脚本验证解码、子进程、JSON 和 MIDI 校验）：

```powershell
.\tests\integration.ps1
```

保留测试输出：

```powershell
.\tests\integration.ps1 -KeepOutput
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

- 第一版聚焦独奏/主旋律（钢琴、吉他、清唱），复音和强伴奏效果有限。
- 不输出乐谱（MusicXML），只输出 MIDI。
- 不支持实时输入。

## 许可证

本项目源码采用 MIT 许可证，见 [LICENSE](LICENSE)。发布包内含的第三方组件按各自许可证分发，
详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
