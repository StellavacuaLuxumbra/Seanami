param(
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

function Get-VsInstallPath {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { return $null }
    return (& $vswhere -latest -property installationPath)
}

$vsPath = Get-VsInstallPath
if ($vsPath) {
    Write-Host "使用 Visual Studio: $vsPath"
    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    if ($LASTEXITCODE -eq 0) {
        cmake --build build --config $Config
        if ($LASTEXITCODE -eq 0) {
            $exe = Get-ChildItem -Path build -Recurse -Filter "tab_tool.exe" | Select-Object -First 1
            Write-Host "产物: $($exe.FullName)"
            exit 0
        }
    }
    Write-Warning "MSVC 构建失败，尝试 MinGW..."
}

$gxx = $null
foreach ($candidate in @("D:\mingw64\bin\g++.exe", "D:\MinGW\bin\g++.exe")) {
    if (Test-Path $candidate) { $gxx = $candidate; break }
}
if (-not $gxx) {
    $gxx = (Get-Command g++ -ErrorAction SilentlyContinue).Source
}
if (-not $gxx) {
    throw "未找到 Visual Studio 或 g++，请先安装其一"
}

Write-Host "使用 MinGW: $gxx"
New-Item -ItemType Directory -Force -Path build | Out-Null
& $gxx -std=c++17 -O2 -Isrc -Ithird_party `
    src/main.cpp `
    src/cli/args.cpp `
    src/cli/progress.cpp `
    src/app/transcribe_job.cpp `
    src/infra/audio_decoder.cpp `
    src/infra/python_transcriber.cpp `
    src/infra/midi_validator.cpp `
    -o build/tab_tool.exe -lshell32

if ($LASTEXITCODE -ne 0) { throw "编译失败" }

# 方便本地直接运行：把脚本放到 exe 旁边
Copy-Item -Recurse -Force "scripts" "build/scripts"
Write-Host "产物: build\tab_tool.exe"
