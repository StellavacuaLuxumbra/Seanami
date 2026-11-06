# 评估工具

| 文件 | 用途 |
| --- | --- |
| `eval.py` | 对比一个预测 MIDI 和一个标注（MIDI 或 vocadito CSV） |
| `note_eval.py` | 共享的读取与 mir_eval 评分逻辑 |
| `fetch_vocadito.py` | 按 HTTP Range 下载 vocadito 测试片段和人工标注 |
| `vocadito_bench.py` | 批量跑 tab_tool，计算每段及平均 note F1 |

指标口径：mir_eval 的 note onset + pitch F1，onset 容差 50 ms，音高容差
50 cents，默认忽略 offset。每段用 A1、A2 两份人工标注各算一次，取较高值
作为该段的 A-max，再对片段求平均。

```powershell
# 下载 10 段测试数据（约 18 MB）
.\tools\python311\python.exe tests\eval\fetch_vocadito.py

# 批量评估
.\tools\python311\python.exe tests\eval\vocadito_bench.py --data-dir tests\real\vocadito

# 换最短音符阈值
.\tools\python311\python.exe tests\eval\vocadito_bench.py --data-dir tests\real\vocadito --min-note-ms 100

# 单文件
.\tools\python311\python.exe tests\eval\eval.py --pred output.mid --gt ground_truth.mid
```

vocadito 数据由 Bittner et al. 发布，采用 CC BY 4.0 许可：
<https://zenodo.org/records/5557945>。下载的音频和标注位于 `tests/real/`，
已被 `.gitignore` 忽略，不进入仓库。
