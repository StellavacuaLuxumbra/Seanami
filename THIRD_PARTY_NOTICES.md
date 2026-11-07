# 第三方组件声明

本项目源码采用 MIT 许可证（见 [LICENSE](LICENSE)）。发布包 `dist.zip` 内含以下第三方运行时和库，
它们分别遵循各自的许可证。完整许可证文本随发布包提供，位于 `python/LICENSE.txt` 和
`python/Lib/site-packages/*.dist-info/` 目录。

| 组件 | 版本 | 许可证 | 说明 |
| --- | --- | --- | --- |
| Python | 3.11.9 | PSF-2.0 | `python/LICENSE.txt` |
| Basic Pitch | 0.4.0 | Apache-2.0 | Spotify AB；含 ONNX 模型，见 `basic_pitch-0.4.0.dist-info/LICENSE` 和 `NOTICE` |
| onnxruntime | 1.31.1 | MIT | 见 `onnxruntime-1.31.1.dist-info/METADATA` |
| NumPy | 1.26.4 | BSD-3-Clause | 见 `numpy-1.26.4.dist-info` |
| SciPy | 1.17.1 | BSD-3-Clause | 见 `scipy-1.17.1.dist-info` |
| librosa | 0.11.0 | ISC | 见 `librosa-0.11.0.dist-info` |
| numba | 0.68.1 | BSD-2-Clause | 见 `numba-0.68.1.dist-info` |
| llvmlite | 0.50.0 | BSD-2-Clause | 见 `llvmlite-0.50.0.dist-info/LICENSE` |
| pretty_midi | 0.2.11.post0 | MIT | 见 `pretty_midi-0.2.11.post0.dist-info` |
| mido | 1.3.3 | MIT | MIDI 读写 |
| soundfile | 0.14.0 | BSD-3-Clause | 见 `soundfile-0.14.0.dist-info` |
| python-soxr | 1.1.0 | LGPL-2.1-or-later | 动态扩展；源码 <https://github.com/dofuuz/python-soxr> |
| dr_wav / dr_mp3 | — | public domain / MIT-0 | David Reid；见 `third_party/` |
| nlohmann/json | — | MIT | 见 `third_party/json.hpp` |
| GCC 运行库（libstdc++ / libgcc） | — | GPL-3.0-or-later WITH GCC-exception-3.1 | MinGW-w64 运行库 DLL |
| libwinpthread | — | MIT | MinGW-w64 运行库 DLL |
| Microsoft Visual C++ 运行库 | 14.x | Microsoft 许可 | `msvcp140*.dll` |

说明：

- 上表覆盖发布包中的主要组件，其余依赖的许可证文本同样保留在对应的 `*.dist-info` 目录中。
- `python-soxr` 以未修改的动态扩展形式随 Python 运行时分发；其 LGPL-2.1 许可证文本见
  `python/Lib/site-packages/soxr-1.1.0.dist-info/licenses/`。
- Basic Pitch 的代码和模型权重由 Spotify AB 以 Apache-2.0 许可证发布。
- 本项目的 MIT 许可证仅适用于本项目原创代码，不改变任何第三方组件的许可证。
