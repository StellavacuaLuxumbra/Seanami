param(
    [switch]$KeepOutput
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $Root

$exe = Join-Path $Root "build\tab_tool.exe"
$python = Join-Path $Root "tools\python311\python.exe"
$fakeScript = Join-Path $Root "tests\fake_transcribe.py"
$sample = Join-Path $Root "tests\samples\melody.wav"

if (-not (Test-Path $exe)) {
    throw "找不到 build\tab_tool.exe，请先运行 .\build.ps1"
}
if (-not (Test-Path $python)) {
    throw "找不到 tools\python311\python.exe，请先运行 .\scripts\install_python.ps1"
}
if (-not (Test-Path $sample)) {
    & $python (Join-Path $Root "tests\make_sample.py")
}

$outDir = Join-Path $Root "tests\out_integration"
if (Test-Path $outDir) { Remove-Item -Recurse -Force $outDir }
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$mid = Join-Path $outDir "melody.mid"
Write-Host "运行集成测试（使用假推理脚本，不依赖 TensorFlow）..."
& $exe $sample --python $python --script $fakeScript -o $mid -v
if ($LASTEXITCODE -ne 0) {
    throw "tab_tool 退出码 $LASTEXITCODE"
}

if (-not (Test-Path $mid)) {
    throw "没有生成 MIDI: $mid"
}

$bytes = [System.IO.File]::ReadAllBytes($mid)
if ($bytes.Length -lt 14) {
    throw "MIDI 文件过小"
}
$header = [System.Text.Encoding]::ASCII.GetString($bytes, 0, 4)
if ($header -ne "MThd") {
    throw "MIDI 文件头不正确: $header"
}

Write-Host "集成测试通过: $mid ($($bytes.Length) bytes)"
if (-not $KeepOutput) {
    Remove-Item -Recurse -Force $outDir
}
