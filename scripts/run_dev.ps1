param(
    [Parameter(Mandatory = $true)]
    [string]$InputFile,
    [string]$Output = "",
    [switch]$KeepTemp
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $Root

$python = Join-Path $Root "tools\python311\python.exe"
$exe = Join-Path $Root "build\tab_tool.exe"
$script = Join-Path $Root "scripts\transcribe.py"

if (-not (Test-Path $python)) {
    throw "找不到内置 Python，请先运行 .\scripts\install_python.ps1"
}
if (-not (Test-Path $exe)) {
    throw "找不到 tab_tool.exe，请先运行 .\build.ps1"
}

$toolArgs = @(
    $InputFile,
    "--python", $python,
    "--script", $script,
    "-v"
)
if ($Output) { $toolArgs += @("-o", $Output) }
if ($KeepTemp) { $toolArgs += "--keep-temp" }

& $exe @toolArgs
exit $LASTEXITCODE
