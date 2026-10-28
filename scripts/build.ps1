<#
.SYNOPSIS
    构建 ESP-Mosaico 磁吸手柄（CH592F）固件（使用 MounRiver Studio 3 自带工具链）。

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

$mrsBase = Join-Path $MrsRoot 'resources\app\resources\win32'
$gcc = Find-Tool 'riscv-none-embed-gcc.exe' (Join-Path $mrsBase 'components\WCH\Toolchain')
$make = Find-Tool 'make.exe' (Join-Path $mrsBase 'others\Build_Tools\Make')

if (-not $gcc -or -not $make) {
    Write-Host "未检测到 riscv-none-embed-gcc / make。" -ForegroundColor Yellow
    Write-Host "  方式 A：用 MounRiver Studio 打开本目录后 Build（Ctrl+B）" -ForegroundColor Cyan
    Write-Host "  方式 B：用参数指定 MRS 安装路径： -MrsRoot D:\MounRiver\MounRiver_Studio3" -ForegroundColor Cyan
    if ($Require) { exit 1 }
    exit 0
}

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
