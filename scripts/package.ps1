param(
    [string]$PythonHome = "",
    [string]$BuildDir = "build",
    [string]$OutDir = "dist",
    [switch]$SkipInstall,
    [switch]$Zip
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $Root

# 1. 定位编译产物
$exe = Get-ChildItem -Path $BuildDir -Recurse -Filter "seanami.exe" -ErrorAction SilentlyContinue |
       Select-Object -First 1
if (-not $exe) {
    throw "找不到 seanami.exe，请先编译：cmake --build $BuildDir --config Release"
}

# 2. 准备输出目录
if (Test-Path $OutDir) {
    # PowerShell 的 Remove-Item 在文件多时非常慢，改用 cmd 的 rd
    & cmd /c "rd /s /q `"$OutDir`"" | Out-Null
    if (Test-Path $OutDir) { throw "无法清理输出目录: $OutDir" }
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# 3. 复制主程序、脚本和许可证
Copy-Item $exe.FullName (Join-Path $OutDir "seanami.exe")
Copy-Item -Recurse -Force "scripts" (Join-Path $OutDir "scripts")
Copy-Item (Join-Path $Root "LICENSE") (Join-Path $OutDir "LICENSE")
Copy-Item (Join-Path $Root "THIRD_PARTY_NOTICES.md") (Join-Path $OutDir "THIRD_PARTY_NOTICES.md")
New-Item -ItemType Directory -Force -Path (Join-Path $OutDir "models\basic_pitch") | Out-Null

# 3.1 判断 exe 是否动态依赖 MinGW 运行时：静态链接（Zig / -static）不需要任何 DLL。
#     过去无条件从 D:\mingw64 / D:\MinGW 复制，会把 32 位 DLL 配给 64 位 exe，
#     导致 0xC000007B。这里先读 PE 头确认架构，并检查 exe 的导入表字符串。
$exeBytes = [System.IO.File]::ReadAllBytes($exe.FullName)
$peOffset = [BitConverter]::ToInt32($exeBytes, 0x3C)
$machine = [BitConverter]::ToUInt16($exeBytes, $peOffset + 4)
$is64 = ($machine -eq 0x8664)
$exeText = [System.Text.Encoding]::ASCII.GetString($exeBytes)

if ($exeText.Contains("libstdc++-6.dll") -or $exeText.Contains("libgcc_s_")) {
    $mingwDirs = @()
    $gxxCmd = Get-Command g++.exe -ErrorAction SilentlyContinue
    if ($gxxCmd) { $mingwDirs += (Split-Path -Parent $gxxCmd.Source) }
    $mingwDirs += @("D:\mingw64\bin", "D:\MinGW\bin")

    $archDll = if ($is64) { "libgcc_s_seh-1.dll" } else { "libgcc_s_dw2-1.dll" }
    $copied = $false
    foreach ($mingwBin in ($mingwDirs | Select-Object -Unique)) {
        if (-not (Test-Path (Join-Path $mingwBin "libstdc++-6.dll"))) { continue }
        if (-not (Test-Path (Join-Path $mingwBin $archDll))) { continue }
        foreach ($dll in @("libstdc++-6.dll", $archDll, "libwinpthread-1.dll")) {
            $src = Join-Path $mingwBin $dll
            if (Test-Path $src) { Copy-Item $src (Join-Path $OutDir $dll) -Force }
        }
        $copied = $true
        break
    }
    if (-not $copied) {
        throw "exe 动态依赖 MinGW 运行时，但没有找到与架构匹配的 MinGW 目录（需要 $archDll）"
    }
} else {
    Write-Host "exe 为静态链接，无需 MinGW 运行时 DLL"
}

# 3.2 onnxruntime 依赖 MSVC 运行时，干净系统上也要能跑。
#     放到 python.exe 同目录和 onnxruntime\capi 目录，确保 DLL 搜索路径能命中。
$msvcDlls = @("msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll", "vcruntime140.dll", "vcruntime140_1.dll")
$msvcTargets = @($OutDir, (Join-Path $OutDir "python"), (Join-Path $OutDir "python\Lib\site-packages\onnxruntime\capi"))
foreach ($dll in $msvcDlls) {
    $src = Join-Path $env:WINDIR "System32\$dll"
    if (-not (Test-Path $src)) { continue }
    foreach ($target in $msvcTargets) {
        if (Test-Path $target) { Copy-Item $src (Join-Path $target $dll) -Force }
    }
}

# 4. 复制 Python 运行时（完整安装目录，保证自包含）
if (-not $PythonHome) {
    $defaultPython = Join-Path $Root "tools\python311"
    if (Test-Path (Join-Path $defaultPython "python.exe")) {
        $PythonHome = $defaultPython
        Write-Host "使用项目内置 Python: $PythonHome"
    }
}

if ($PythonHome) {
    $PythonHome = [System.IO.Path]::GetFullPath($PythonHome)
    if (-not (Test-Path (Join-Path $PythonHome "python.exe"))) {
        throw "PythonHome 下找不到 python.exe: $PythonHome"
    }
    Write-Host "复制 Python 运行时: $PythonHome"
    # 复制时就排除 TensorFlow 等运行时不需要的目录，避免先复制 1 GB 再删除。
    $excludeRelative = @(
        "Lib\test",
        "Lib\idlelib",
        "Lib\tkinter",
        "Lib\turtledemo",
        "Lib\ensurepip",
        "Lib\lib2to3",
        "Lib\distutils",
        "Lib\site-packages\tensorflow",
        "Lib\site-packages\keras",
        "Lib\site-packages\tensorboard",
        "Lib\site-packages\tensorboard_data_server",
        "Lib\site-packages\tensorflow_estimator",
        "Lib\site-packages\tensorflow_io_gcs_filesystem",
        "Lib\site-packages\clang",
        "Lib\site-packages\grpc",
        "Lib\site-packages\h5py",
        "Lib\site-packages\sklearn",
        "Lib\site-packages\scipy\tests",
        "Lib\site-packages\numpy\tests",
        "Lib\site-packages\librosa\tests"
    )
    $helperArgs = @(
        (Join-Path $Root "scripts\hardlink_tree.py"),
        $PythonHome,
        (Join-Path $OutDir "python"),
        "--exclude-name", "__pycache__",
        "--exclude-ext", ".lib"
    )
    foreach ($relative in $excludeRelative) {
        $helperArgs += @("--exclude", $relative)
    }
    & (Join-Path $PythonHome "python.exe") @helperArgs
    if ($LASTEXITCODE -ne 0) { throw "复制 Python 运行时失败（exit $LASTEXITCODE）" }
    Write-Host "Python 运行时复制完成"
} else {
    Write-Warning "未指定 -PythonHome，发布包将缺少 Python 运行时。"
    Write-Warning "先运行 .\scripts\install_python.ps1，或手动指定 -PythonHome"
}

# 5. 确认发布包内已包含 basic-pitch
$pythonExe = Join-Path $OutDir "python\python.exe"
$basicPitchDir = Join-Path $OutDir "python\Lib\site-packages\basic_pitch"
if ((Test-Path $pythonExe) -and -not $SkipInstall) {
    if (Test-Path $basicPitchDir) {
        Write-Host "发布包内已包含 basic-pitch，跳过安装"
    } else {
        Write-Host "安装 basic-pitch（可能需要几分钟）..."
        & $pythonExe -m pip install --upgrade pip
        if ($LASTEXITCODE -ne 0) { throw "pip 升级失败" }
        & $pythonExe -m pip install --no-warn-script-location basic-pitch
        if ($LASTEXITCODE -ne 0) { throw "basic-pitch 安装失败" }
    }
}

# 5.1 精简运行时（删除测试、文档和编译头等非必需内容）
if (Test-Path $pythonExe) {
    $trimDirs = @(
        "Lib\test",
        "Lib\idlelib",
        "Lib\tkinter",
        "Lib\turtledemo",
        "Lib\ensurepip",
        "Lib\lib2to3",
        "Lib\distutils",
        # 发布包默认使用 ONNX 后端，TensorFlow 相关运行时全部移除
        "Lib\site-packages\tensorflow",
        "Lib\site-packages\keras",
        "Lib\site-packages\tensorboard",
        "Lib\site-packages\tensorboard_data_server",
        "Lib\site-packages\tensorflow_estimator",
        "Lib\site-packages\tensorflow_io_gcs_filesystem",
        "Lib\site-packages\clang",
        "Lib\site-packages\grpc",
        "Lib\site-packages\h5py",
        "Lib\site-packages\sklearn",
        # 训练/可视化才会用到的测试数据
        "Lib\site-packages\scipy\tests",
        "Lib\site-packages\numpy\tests",
        "Lib\site-packages\librosa\tests"
    )
    foreach ($dir in $trimDirs) {
        $target = Join-Path $OutDir "python\$dir"
        if (Test-Path $target) { Remove-Item -Recurse -Force $target }
    }

    # 运行时不需要 .lib 导入库
    Get-ChildItem -Path (Join-Path $OutDir "python\Lib\site-packages") -Recurse -Filter "*.lib" -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue

    Get-ChildItem -Path (Join-Path $OutDir "python") -Recurse -Directory -Filter "__pycache__" -ErrorAction SilentlyContinue |
        Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
}

# 6. 生成使用说明
$readme = @"
Seanami 0.2.1 使用说明
=======================

用法：
  seanami.exe <input.wav|input.mp3|input.flac|目录> [选项]

快速开始：
  seanami.exe song.wav
  seanami.exe song.mp3 -o song.mid --preset vocal
  seanami.exe song.flac --preset guitar --quantize --quantize-grid 1/16
  seanami.exe .\music -r -o .\out
  seanami.exe .\music -r -o .\out --jobs 4 --skip-existing --report .\report.csv
  seanami.exe song.wav --separate hpss

常用选项：
  -o, --output <path>        输出 MIDI 路径；多文件时为输出目录
  -r, --recursive            目录输入时递归子目录
      --open                 完成后用系统默认程序打开第一个 MIDI
      --track-name <name>    MIDI 轨道名（默认使用输入文件名）
      --program <0-127>      GM 乐器编号（-1 表示不写入）
      --tempo-map            写入速度地图（默认开启）
      --no-tempo-map         只写单一速度

  --preset <name>            default / vocal / guitar / piano / bass
  --onset-threshold <f>      起音阈值 0~1（默认 0.5）
  --frame-threshold <f>      音帧阈值 0~1（默认 0.3）
  --min-note-ms <f>          最短音符毫秒（默认 127.7）
  --min-freq / --max-freq    音域限制（Hz，0 表示不限制）
  --mono / --poly            单音 / 复音
  --quantize                 吸附到节拍网格（默认关闭）
  --quantize-grid <g>        1/4、1/8、1/16、1/32、1/8T、1/16T
  --bpm <f>                  固定 BPM；0 表示自动检测
  --merge-gap-ms <f>         合并同音高碎片的最大间隔
  --separate <mode>          none / hpss / demucs
  --hpss-margin <f>          HPSS 分离强度，>=1.0（默认 1.0）

  --jobs <n>                 并行任务数 1~16（默认 1）
  --skip-existing            跳过已存在的输出 MIDI
  --report <path>            CSV 或 JSON 汇总报告
  --no-worker                禁用常驻 Python worker
  -v, --verbose              详细日志
  -h, --help                 帮助

预设：
  default  不限音域，复音，127.7 ms
  vocal    E2~C6，单音，100 ms，GM 53 Voice Oohs
  guitar   E2~E6，单音，100 ms，GM 25 Acoustic Guitar
  piano    A0~C8，复音，90 ms，GM 0 Acoustic Grand Piano
  bass     B0~G4，单音，90 ms，GM 33 Electric Bass
  预设不会自动开启量化或碎片合并，需要时请显式加 --quantize / --merge-gap-ms。

批量与 worker：
  目录默认只处理第一层，-r 递归。批量时模型只加载一次；--jobs N 会启动
  N 个独立 worker 并行处理，内存占用也会增加。某个文件失败不会中断整批，
  有失败时退出码为 4；--skip-existing 会跳过已有输出。

人声/伴奏分离：
  hpss   内置，无额外依赖，适合削弱鼓点等打击性伴奏。
  demucs 发布包不包含，需要自行安装 demucs 和 torch；未安装时会给出提示。

运行环境：
  本目录已包含 Python 运行时、basic-pitch 和 onnxruntime，无需额外安装。
  默认使用 Basic Pitch 内置 ONNX 模型，不需要 TensorFlow。

效果与速度：
  独奏/主旋律效果最好，强复音和重伴奏效果有限。
  默认 note F1 约 0.52（vocadito 10 段清唱），HPSS 约 0.54，vocal 预设约 0.52。
  热启动单文件约 7~15 秒；10 段批量约 14 秒；HPSS 约 23 秒。
  首次运行可能触发杀毒软件扫描和 numba 首次编译，冷启动可能 1~3 分钟。

许可证：
  本项目源码采用 MIT 许可证，见 LICENSE。
  发布包内的第三方组件按各自许可证分发，见 THIRD_PARTY_NOTICES.md。
"@
Set-Content -Path (Join-Path $OutDir "README.txt") -Value $readme -Encoding UTF8

Write-Host "打包完成: $OutDir"
Write-Host "测试: $OutDir\seanami.exe <音频文件>"

# 7. 可选：生成 zip
if ($Zip) {
    $zipPath = "$OutDir.zip"
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::CreateFromDirectory(
        (Resolve-Path $OutDir).Path,
        $zipPath,
        [System.IO.Compression.CompressionLevel]::Optimal,
        $false)
    Write-Host "压缩包: $zipPath"
}
