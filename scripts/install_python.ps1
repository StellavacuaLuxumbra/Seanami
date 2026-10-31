param(
    [string]$Version = "3.11.9",
    [string]$TargetDir = "",
    [string]$IndexUrl = "",
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $TargetDir) {
    $TargetDir = Join-Path $Root "tools\python311"
}
$TargetDir = [System.IO.Path]::GetFullPath($TargetDir)
$pythonExe = Join-Path $TargetDir "python.exe"

Write-Host "目标目录: $TargetDir"

if ((Test-Path $pythonExe) -and -not $Force) {
    Write-Host "检测到已有 Python: $pythonExe"
} else {
    $installer = Join-Path $env:TEMP "python-$Version-amd64.exe"
    if (-not (Test-Path $installer)) {
        $url = "https://www.python.org/ftp/python/$Version/python-$Version-amd64.exe"
        Write-Host "下载: $url"
        Invoke-WebRequest -Uri $url -OutFile $installer -UseBasicParsing
    } else {
        Write-Host "使用已下载的安装器: $installer"
    }

    New-Item -ItemType Directory -Force -Path $TargetDir | Out-Null

    $argList = @(
        "/quiet",
        "InstallAllUsers=0",
        "TargetDir=`"$TargetDir`"",
        "Include_pip=1",
        "Include_launcher=0",
        "Include_test=0",
        "PrependPath=0",
        "Shortcuts=0"
    )

    Write-Host "安装 Python $Version ..."
    $proc = Start-Process -FilePath $installer -ArgumentList $argList -Wait -PassThru
    if ($proc.ExitCode -ne 0 -and $proc.ExitCode -ne 3010) {
        throw "Python 安装失败，退出码 $($proc.ExitCode)"
    }
}

if (-not (Test-Path $pythonExe)) {
    throw "安装后找不到 python.exe: $pythonExe"
}

Write-Host "Python 版本:"
& $pythonExe --version

$pipArgs = @("-m", "pip", "install", "--upgrade", "pip")
if ($IndexUrl) { $pipArgs += @("-i", $IndexUrl) }
Write-Host "升级 pip ..."
& $pythonExe @pipArgs
if ($LASTEXITCODE -ne 0) { throw "pip 升级失败" }

$installArgs = @("-m", "pip", "install", "basic-pitch")
if ($IndexUrl) { $installArgs += @("-i", $IndexUrl) }
Write-Host "安装 basic-pitch（会下载 TensorFlow，请耐心等待）..."
& $pythonExe @installArgs
if ($LASTEXITCODE -ne 0) { throw "basic-pitch 安装失败" }

$onnxArgs = @("-m", "pip", "install", "onnxruntime")
if ($IndexUrl) { $onnxArgs += @("-i", $IndexUrl) }
Write-Host "安装 onnxruntime（发布包默认推理后端）..."
& $pythonExe @onnxArgs
if ($LASTEXITCODE -ne 0) { throw "onnxruntime 安装失败" }

Write-Host "验证导入 ..."
& $pythonExe -c "import basic_pitch, tensorflow as tf, onnxruntime; print('basic-pitch ok'); print('tensorflow', tf.__version__); print('onnxruntime', onnxruntime.__version__)"
if ($LASTEXITCODE -ne 0) { throw "导入验证失败" }

Write-Host ""
Write-Host "安装完成。"
Write-Host "Python: $pythonExe"
Write-Host "下一步: .\scripts\package.ps1 -PythonHome `"$TargetDir`""
