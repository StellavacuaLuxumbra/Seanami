param(
    [string]$PythonHome = "",
    [string]$BuildDir = "build",
    [string]$OutDir = "dist",
    [switch]$SkipInstall
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
if (Test-Path $OutDir) { Remove-Item -Recurse -Force $OutDir }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# 3. 复制主程序和脚本
Copy-Item $exe.FullName (Join-Path $OutDir "tab_tool.exe")
Copy-Item -Recurse -Force "scripts" (Join-Path $OutDir "scripts")
New-Item -ItemType Directory -Force -Path (Join-Path $OutDir "models\basic_pitch") | Out-Null

# 4. 复制 Python 运行时（完整安装目录，保证自包含）
if ($PythonHome) {
    if (-not (Test-Path (Join-Path $PythonHome "python.exe"))) {
        throw "PythonHome 下找不到 python.exe: $PythonHome"
    }
    Write-Host "复制 Python 运行时: $PythonHome"
    Copy-Item -Recurse -Force $PythonHome (Join-Path $OutDir "python")
} else {
    Write-Warning "未指定 -PythonHome，发布包将缺少 Python 运行时。"
    Write-Warning "示例: .\scripts\package.ps1 -PythonHome 'C:\Python311'"
}

# 5. 安装 basic-pitch 到发布包内的 Python
$pythonExe = Join-Path $OutDir "python\python.exe"
if ((Test-Path $pythonExe) -and -not $SkipInstall) {
    Write-Host "安装 basic-pitch（可能需要几分钟）..."
    & $pythonExe -m pip install --upgrade pip
    & $pythonExe -m pip install --no-warn-script-location basic-pitch
}

# 6. 生成使用说明
$readme = @"
tab_tool 使用说明
=================

用法：
  tab_tool.exe <input.wav|input.mp3> [选项]

示例：
  tab_tool.exe song.wav
  tab_tool.exe song.mp3 -o song.mid --onset-threshold 0.4 -v

常用选项：
  -o, --output <path>        输出 MIDI 路径
  -m, --model <dir>          模型目录
      --onset-threshold <f>  起音阈值 0~1（默认 0.5）
      --frame-threshold <f>  音帧阈值 0~1（默认 0.3）
      --min-note-ms <f>      最短音符毫秒（默认 58）
  -v, --verbose              详细日志
  -h, --help                 帮助

运行环境：
  本目录已包含 Python 运行时和 basic-pitch，无需额外安装。
"@
Set-Content -Path (Join-Path $OutDir "README.txt") -Value $readme -Encoding UTF8

Write-Host "打包完成: $OutDir"
Write-Host "测试: $OutDir\tab_tool.exe <音频文件>"
