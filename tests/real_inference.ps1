param(
    [string]$InputFile = "",
    [switch]$KeepOutput
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $Root

$exe = Join-Path $Root "build\tab_tool.exe"
$python = Join-Path $Root "tools\python311\python.exe"
$script = Join-Path $Root "scripts\transcribe.py"
if (-not $InputFile) {
    $InputFile = Join-Path $Root "tests\samples\melody.wav"
}

if (-not (Test-Path $exe)) { throw "找不到 build\tab_tool.exe，请先运行 .\build.ps1" }
if (-not (Test-Path $python)) { throw "找不到内置 Python，请先运行 .\scripts\install_python.ps1" }
if (-not (Test-Path $InputFile)) {
    & $python (Join-Path $Root "tests\make_sample.py") | Out-Null
}

Write-Host "检查 basic-pitch 导入 ..."
& $python -c "import basic_pitch, tensorflow as tf; from importlib.metadata import version; print('basic-pitch', version('basic-pitch')); print('tensorflow', tf.__version__)"
if ($LASTEXITCODE -ne 0) { throw "basic-pitch / tensorflow 导入失败" }

$outDir = Join-Path $Root "tests\out_real"
if (Test-Path $outDir) { Remove-Item -Recurse -Force $outDir }
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$mid = Join-Path $outDir "result.mid"

Write-Host "运行真实推理: $InputFile"
& $exe $InputFile --python $python --script $script -o $mid -v
if ($LASTEXITCODE -ne 0) { throw "tab_tool 退出码 $LASTEXITCODE" }
if (-not (Test-Path $mid)) { throw "没有生成 MIDI: $mid" }

& $python (Join-Path $Root "tests\midi_stats.py") $mid
if ($LASTEXITCODE -ne 0) { throw "MIDI 中没有音符" }

Write-Host "真实推理通过: $mid"
if (-not $KeepOutput) { Remove-Item -Recurse -Force $outDir }
