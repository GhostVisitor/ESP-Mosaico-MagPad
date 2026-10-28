<#
.SYNOPSIS
    构建 ESP-Mosaico 磁吸手柄（CH592F）固件（使用 MounRiver Studio 自带工具链）。

.PARAMETER MrsRoot
    MounRiver Studio 安装目录（如 C:\MounRiver\MounRiver_Studio2）。
    默认会自动探测 C:\MounRiver\MounRiver_Studio2 / ...Studio3 等常见位置。

.DESCRIPTION
    1. 自动查找 make 与 riscv-none-embed-gcc（PATH 或 MRS 3 安装目录）；
    2. 调用根目录 Makefile 编译，产物在 build/；
    3. 打印 Flash/RAM 占用。

.PARAMETER Diag
    编译 USB CDC 诊断固件（PAD_DIAG.hex，仅用于排查启动/按键/电池）。

.EXAMPLE
    pwsh -File scripts/build.ps1
    pwsh -File scripts/build.ps1 -Clean
    pwsh -File scripts/build.ps1 -Diag
#>
[CmdletBinding()]
param(
    [switch]$Clean,
    [switch]$Diag,
    [switch]$Require,
    [string]$MrsRoot = 'C:\MounRiver\MounRiver_Studio3'
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$repo = Split-Path -Parent $PSScriptRoot
$proj = $repo

if (-not (Test-Path (Join-Path $proj 'Makefile'))) {
    throw "未找到 Makefile：$proj"
}

function Find-Tool([string]$ExeName, [string]$SearchRoot) {
    $cmd = Get-Command $ExeName -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    if ($SearchRoot -and (Test-Path $SearchRoot)) {
        return Get-ChildItem $SearchRoot -Recurse -Filter $ExeName -ErrorAction SilentlyContinue |
            Select-Object -First 1 -ExpandProperty FullName
    }
    return $null
}

function Resolve-Toolchain([string]$Root) {
    if (-not $Root) { return $null }
    $base = Join-Path $Root 'resources\app\resources\win32'
    if (-not (Test-Path $base)) { return $null }
    $g = Find-Tool 'riscv-none-embed-gcc.exe' (Join-Path $base 'components\WCH\Toolchain')
    $m = Find-Tool 'make.exe' (Join-Path $base 'others\Build_Tools\Make')
    if ($g -and $m) { return [pscustomobject]@{ Gcc = $g; Make = $m } }
    return $null
}

# 1) 先用 -MrsRoot 指定的（或默认路径）
$tc = Resolve-Toolchain $MrsRoot

# 2) 没找到就自动探测常见安装位置（MRS 2 / 3 均可）
if (-not $tc) {
    $probe = @()
    foreach ($base in @('C:\MounRiver', 'D:\MounRiver', $env:ProgramFiles, ${env:ProgramFiles(x86)})) {
        if ($base -and (Test-Path $base)) {
            $probe += Get-ChildItem $base -Directory -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -like 'MounRiver*' } | ForEach-Object { $_.FullName }
        }
    }
    foreach ($c in $probe) {
        $tc = Resolve-Toolchain $c
        if ($tc) {
            Write-Host "自动检测到工具链：$c" -ForegroundColor DarkGray
            break
        }
    }
}

if (-not $tc) {
    Write-Host "未检测到 riscv-none-embed-gcc / make。" -ForegroundColor Yellow
    Write-Host "  方式 A：用 MounRiver Studio 打开本目录后 Build（Ctrl+B）" -ForegroundColor Cyan
    Write-Host "  方式 B：用参数指定 MRS 安装路径： -MrsRoot C:\MounRiver\MounRiver_Studio2" -ForegroundColor Cyan
    if ($Require) { exit 1 }
    exit 0
}

$gcc = $tc.Gcc
$make = $tc.Make

$toolchainPrefix = Join-Path (Split-Path $gcc -Parent) 'riscv-none-embed-'
Write-Host "工具链: $gcc" -ForegroundColor DarkGray
Write-Host "make  : $make" -ForegroundColor DarkGray

$buildArgs = @('-C', $proj, '-j4', "TOOLCHAIN=$($toolchainPrefix.Replace('\', '/'))")
if ($Diag) {
    $buildArgs += @('TARGET=build/PAD_DIAG', 'OBJ_DIR=build/obj_diag', 'EXTRA_CFLAGS=-DPAD_DIAG_CDC')
}

Push-Location $repo
try {
    if ($Clean) { & $make @('-C', $proj) clean | Out-Null }
    & $make @buildArgs
    $code = $LASTEXITCODE
}
finally { Pop-Location }

if ($code -ne 0) {
    Write-Host "编译失败（exit $code）" -ForegroundColor Red
    exit $code
}

Write-Host ""
Write-Host "构建成功：" -ForegroundColor Green
Get-ChildItem (Join-Path $proj 'build') -File | ForEach-Object {
    Write-Host ("  {0,-24} {1,10:N0} B" -f $_.Name, $_.Length)
}
$siz = Join-Path $proj ($(if ($Diag) { 'build\PAD_DIAG.siz' } else { 'build\PAD_CH592.siz' }))
if (Test-Path $siz) {
    Write-Host ""
    Get-Content $siz | ForEach-Object { Write-Host "  $_" -ForegroundColor DarkGray }
}
