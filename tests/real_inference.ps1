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

# basic-pitch 在导入时会向 stderr 打印可选依赖警告；Windows PowerShell 5.1
# 会把原生命令的 stderr 当成 NativeCommandError，这里临时放宽以读取真实退出码。
function Invoke-Native {
    param([scriptblock]$Command)
    $previous = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $Command
        return $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previous
    }
}

function Show-NativeLine {
    process {
        if ($_ -is [System.Management.Automation.ErrorRecord]) {
            Write-Host $_.Exception.Message
        } else {
            Write-Host $_
        }
    }
}

Write-Host "检查 basic-pitch / onnxruntime 导入 ..."
$importExit = Invoke-Native {
    & $python -c "import basic_pitch, onnxruntime; from importlib.metadata import version; print('basic-pitch', version('basic-pitch')); print('onnxruntime', onnxruntime.__version__)" 2>&1 |
        Show-NativeLine
}
if ($importExit -ne 0) { throw "basic-pitch / onnxruntime 导入失败（退出码 $importExit）" }

$outDir = Join-Path $Root "tests\out_real"
if (Test-Path $outDir) { Remove-Item -Recurse -Force $outDir }
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$mid = Join-Path $outDir "result.mid"

Write-Host "运行真实推理: $InputFile"
$runExit = Invoke-Native {
    & $exe $InputFile --python $python --script $script -o $mid -v 2>&1 |
        Show-NativeLine
}
if ($runExit -ne 0) { throw "tab_tool 退出码 $runExit" }
if (-not (Test-Path $mid)) { throw "没有生成 MIDI: $mid" }

& $python (Join-Path $Root "tests\midi_stats.py") $mid
if ($LASTEXITCODE -ne 0) { throw "MIDI 中没有音符" }

Write-Host "真实推理通过: $mid"
if (-not $KeepOutput) { Remove-Item -Recurse -Force $outDir }
