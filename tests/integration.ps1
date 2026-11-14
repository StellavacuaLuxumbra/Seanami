param(
    [switch]$KeepOutput
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $Root

$exe = Join-Path $Root "build\seanami.exe"
$python = Join-Path $Root "tools\python311\python.exe"
$fakeScript = Join-Path $Root "tests\fake_transcribe.py"
$sample = Join-Path $Root "tests\samples\melody.wav"

if (-not (Test-Path $exe)) {
    throw "找不到 build\seanami.exe，请先运行 .\build.ps1"
}
if (-not (Test-Path $python)) {
    throw "找不到 tools\python311\python.exe，请先运行 .\scripts\install_python.ps1"
}
if (-not (Test-Path $sample)) {
    & $python (Join-Path $Root "tests\make_sample.py")
}

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) {
        throw $Message
    }
}

function Invoke-Seanami {
    param([string[]]$Arguments)
    & $exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Seanami 退出码 $LASTEXITCODE：$($Arguments -join ' ')"
    }
}

function Assert-Midi {
    param([string]$Path, [string]$Label)
    if (-not (Test-Path $Path)) {
        throw "$Label 没有生成 MIDI: $Path"
    }
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 14) {
        throw "$Label MIDI 文件过小: $Path"
    }
    $header = [System.Text.Encoding]::ASCII.GetString($bytes, 0, 4)
    if ($header -ne "MThd") {
        throw "$Label MIDI 文件头不正确: $header"
    }
}

$outDir = Join-Path $Root "tests\out_integration"
if (Test-Path $outDir) { Remove-Item -Recurse -Force $outDir }
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$paramsPath = Join-Path $outDir "params.json"
$pidLog = Join-Path $outDir "pids.log"
$env:SEANAMI_FAKE_PARAMS = $paramsPath
$env:SEANAMI_FAKE_PID_LOG = $pidLog

Write-Host "运行集成测试（使用假推理脚本，不依赖 TensorFlow）..."
Write-Host "--- 1. 单文件 worker ---"
$singleMid = Join-Path $outDir "single.mid"
Invoke-Seanami @($sample, "--python", $python, "--script", $fakeScript, "-o", $singleMid, "-v")
Assert-Midi $singleMid "单文件"

Write-Host "--- 2. 单文件 --no-worker ---"
$noWorkerMid = Join-Path $outDir "no_worker.mid"
Invoke-Seanami @($sample, "--python", $python, "--script", $fakeScript, "-o", $noWorkerMid, "--no-worker")
Assert-Midi $noWorkerMid "no-worker"

Write-Host "--- 3. 预设与显式参数 ---"
$presetMid = Join-Path $outDir "preset.mid"
Invoke-Seanami @($sample, "--python", $python, "--script", $fakeScript, "-o", $presetMid,
    "--preset", "vocal", "--quantize", "--quantize-grid", "1/8")
$p = Get-Content $paramsPath -Raw -Encoding UTF8 | ConvertFrom-Json
Assert-True ($p.preset -eq "vocal") "预设名未传递: $($p.preset)"
Assert-True ([math]::Abs($p.min_freq - 82.41) -lt 0.001) "vocal min_freq 不正确: $($p.min_freq)"
Assert-True ([math]::Abs($p.max_freq - 1046.5) -lt 0.001) "vocal max_freq 不正确: $($p.max_freq)"
Assert-True ([math]::Abs($p.min_note_ms - 100.0) -lt 0.001) "vocal min_note_ms 不正确: $($p.min_note_ms)"
Assert-True ($p.mono -eq $true) "vocal 应为单音"
Assert-True ($p.quantize -eq $true) "显式 --quantize 未生效"
Assert-True ($p.quantize_grid -eq "1/8") "量化网格未传递: $($p.quantize_grid)"
Assert-True ($p.program -eq 53) "vocal GM program 不正确: $($p.program)"

$overrideMid = Join-Path $outDir "override.mid"
Invoke-Seanami @($sample, "--python", $python, "--script", $fakeScript, "-o", $overrideMid,
    "--preset", "vocal", "--poly", "--no-quantize", "--program", "40")
$p = Get-Content $paramsPath -Raw -Encoding UTF8 | ConvertFrom-Json
Assert-True ($p.mono -eq $false) "--poly 未覆盖预设"
Assert-True ($p.quantize -eq $false) "--no-quantize 未覆盖预设"
Assert-True ($p.program -eq 40) "显式 --program 未覆盖预设"

Write-Host "--- 4. 批量 --jobs 3 / CSV 报告 ---"
$inDir = Join-Path $outDir "in"
New-Item -ItemType Directory -Force -Path (Join-Path $inDir "sub") | Out-Null
Copy-Item $sample (Join-Path $inDir "a.wav")
Copy-Item $sample (Join-Path $inDir "b b.wav")
Copy-Item $sample (Join-Path $inDir "中文 名称.wav")
Copy-Item $sample (Join-Path $inDir "sub\d.wav")
Set-Content -Path (Join-Path $inDir "readme.txt") -Value "not audio"

$batchOut = Join-Path $outDir "batch"
$csvReport = Join-Path $outDir "report.csv"
Invoke-Seanami @($inDir, "-o", $batchOut, "--python", $python, "--script", $fakeScript,
    "--jobs", "3", "--report", $csvReport)
Assert-Midi (Join-Path $batchOut "a.mid") "批量 a"
Assert-Midi (Join-Path $batchOut "b b.mid") "批量 空格名"
Assert-Midi (Join-Path $batchOut "中文 名称.mid") "批量 中文名"
Assert-True (-not (Test-Path (Join-Path $batchOut "sub\d.mid"))) "非递归模式不应处理子目录"
$rows = Import-Csv $csvReport -Encoding UTF8
Assert-True ($rows.Count -eq 3) "CSV 报告行数应为 3，实际 $($rows.Count)"
foreach ($row in $rows) {
    Assert-True ($row.status -eq "success") "CSV 状态不是 success: $($row.status)"
}

Write-Host "--- 5. 递归 / JSON 报告 ---"
$recursiveOut = Join-Path $outDir "recursive"
$jsonReport = Join-Path $outDir "report.json"
Invoke-Seanami @($inDir, "-r", "-o", $recursiveOut, "--python", $python, "--script", $fakeScript,
    "--jobs", "2", "--report", $jsonReport)
$doc = Get-Content $jsonReport -Raw -Encoding UTF8 | ConvertFrom-Json
Assert-True ($doc.total -eq 4) "递归任务数应为 4，实际 $($doc.total)"
Assert-True ($doc.succeeded -eq 4) "递归成功数应为 4，实际 $($doc.succeeded)"
Assert-True ($doc.failed -eq 0) "递归不应有失败"
Assert-Midi (Join-Path $recursiveOut "sub\d.mid") "递归子目录"

Write-Host "--- 6. --skip-existing ---"
$skipReport = Join-Path $outDir "skip.json"
Invoke-Seanami @($inDir, "-r", "-o", $recursiveOut, "--python", $python, "--script", $fakeScript,
    "--jobs", "2", "--skip-existing", "--report", $skipReport)
$skipDoc = Get-Content $skipReport -Raw -Encoding UTF8 | ConvertFrom-Json
Assert-True ($skipDoc.total -eq 4) "跳过任务数应为 4，实际 $($skipDoc.total)"
Assert-True ($skipDoc.skipped -eq 4) "跳过数应为 4，实际 $($skipDoc.skipped)"
Assert-True ($skipDoc.succeeded -eq 0) "跳过后成功数应为 0"
Assert-True ($skipDoc.failed -eq 0) "跳过后不应有失败"

Write-Host "--- 7. worker 复用（--jobs 1） ---"
if (Test-Path $pidLog) { Remove-Item -Force $pidLog }
$pidOut = Join-Path $outDir "pidbatch"
Invoke-Seanami @($inDir, "-o", $pidOut, "--python", $python, "--script", $fakeScript, "--jobs", "1")
$pids = Get-Content $pidLog -Encoding UTF8 |
    ForEach-Object { ($_ -split "`t")[0] } |
    Select-Object -Unique
Assert-True ($pids.Count -eq 1) "顺序批处理应复用同一个 worker，实际 PID 数: $($pids.Count)"

Write-Host "--- 8. FLAC 输入 ---"
$flac = Join-Path $outDir "melody test.flac"
& $python (Join-Path $Root "tests\make_flac.py") $sample $flac | Out-Null
if ($LASTEXITCODE -ne 0) { throw "生成 FLAC 失败" }
$flacMid = Join-Path $outDir "flac.mid"
Invoke-Seanami @($flac, "--python", $python, "--script", $fakeScript, "-o", $flacMid)
Assert-Midi $flacMid "FLAC"

Write-Host "集成测试通过（worker、批量、报告、跳过、预设、FLAC）"
if (-not $KeepOutput) {
    Remove-Item -Recurse -Force $outDir
    Remove-Item Env:SEANAMI_FAKE_PARAMS -ErrorAction SilentlyContinue
    Remove-Item Env:SEANAMI_FAKE_PID_LOG -ErrorAction SilentlyContinue
}
