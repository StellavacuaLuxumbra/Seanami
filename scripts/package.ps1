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
$exe = Get-ChildItem -Path $BuildDir -Recurse -Filter "tab_tool.exe" -ErrorAction SilentlyContinue |
       Select-Object -First 1
if (-not $exe) {
    throw "找不到 tab_tool.exe，请先编译：cmake --build $BuildDir --config Release"
}

# 2. 准备输出目录
if (Test-Path $OutDir) {
    # PowerShell 的 Remove-Item 在文件多时非常慢，改用 cmd 的 rd
    & cmd /c "rd /s /q `"$OutDir`"" | Out-Null
    if (Test-Path $OutDir) { throw "无法清理输出目录: $OutDir" }
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# 3. 复制主程序和脚本
Copy-Item $exe.FullName (Join-Path $OutDir "tab_tool.exe")
Copy-Item -Recurse -Force "scripts" (Join-Path $OutDir "scripts")
New-Item -ItemType Directory -Force -Path (Join-Path $OutDir "models\basic_pitch") | Out-Null

# 3.1 如果 exe 是 MinGW 编译的，随带运行时 DLL
foreach ($mingwBin in @("D:\mingw64\bin", "D:\MinGW\bin")) {
    if (Test-Path (Join-Path $mingwBin "libstdc++-6.dll")) {
        foreach ($dll in @("libstdc++-6.dll", "libgcc_s_seh-1.dll", "libwinpthread-1.dll")) {
            $src = Join-Path $mingwBin $dll
            if (Test-Path $src) { Copy-Item $src (Join-Path $OutDir $dll) -Force }
        }
        break
    }
}

# 3.2 onnxruntime 依赖 MSVC 运行时，干净系统上也要能跑
foreach ($dll in @("msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll")) {
    $src = Join-Path $env:WINDIR "System32\$dll"
    if (Test-Path $src) { Copy-Item $src (Join-Path $OutDir $dll) -Force }
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
tab_tool 使用说明
=================

用法：
  tab_tool.exe <input.wav|input.mp3|目录> [选项]

示例：
  tab_tool.exe song.wav
  tab_tool.exe song.mp3 -o song.mid --onset-threshold 0.4 -v
  tab_tool.exe .\music -r -o .\out
  tab_tool.exe .\music -r -o .\out --open

常用选项：
  -o, --output <path>        输出 MIDI 路径；多文件时为输出目录
  -r, --recursive            目录输入时递归子目录
      --open                 完成后用系统默认程序打开第一个 MIDI
  -m, --model <dir>          模型目录
      --onset-threshold <f>  起音阈值 0~1（默认 0.5）
      --frame-threshold <f>  音帧阈值 0~1（默认 0.3）
      --min-note-ms <f>      最短音符毫秒（默认 127.7）
  -v, --verbose              详细日志
  -h, --help                 帮助

运行环境：
  本目录已包含 Python 运行时、basic-pitch 和 onnxruntime，无需额外安装。
  默认使用 Basic Pitch 内置 ONNX 模型，不需要 TensorFlow。

效果与速度：
  独奏/主旋律效果最好，复音和强伴奏效果有限。
  10 秒音频约 15 秒，5 分钟音频约 20 秒（含启动）。
"@
Set-Content -Path (Join-Path $OutDir "README.txt") -Value $readme -Encoding UTF8

Write-Host "打包完成: $OutDir"
Write-Host "测试: $OutDir\tab_tool.exe <音频文件>"

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
