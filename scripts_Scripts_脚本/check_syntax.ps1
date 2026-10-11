# ============================================================================
#  check_syntax.ps1 -- fast syntax/compile check for individual translation units
#  check_syntax.ps1 —— 针对单个编译单元的快速语法/编译检查
#
#  Why this exists / 为什么需要它:
#    Several authors work on this repository at the same time. A full `cmake
#    --build` fails whenever ANY file is mid-edit, which makes concurrent work
#    painful. This script compiles only the files you name, so each author can
#    verify their own work independently of everybody else.
#    本仓库由多位作者同时编辑。只要有任意一个文件处于未完成状态，完整的
#    `cmake --build` 就会失败，使并行工作变得困难。本脚本只编译你指定的文件，
#    因此每位作者都能独立验证自己的工作。
#
#  Usage / 用法:
#    Windows PowerShell 5.1 (this machine has no `pwsh` on PATH):
#      powershell -NoProfile -File scripts_Scripts_脚本/check_syntax.ps1 -Files src_Source_源代码/core_Core_核心/Types.cpp
#    PowerShell 7+:
#      pwsh -NoProfile -File scripts_Scripts_脚本/check_syntax.ps1 -Files src_Source_源代码/core_Core_核心/Types.cpp
#    Every file in every module / 检查各模块的全部文件:
#      powershell -NoProfile -File scripts_Scripts_脚本/check_syntax.ps1 -All
#
#  Always pass `-Files` explicitly: under Windows PowerShell 5.1 a bare
#  positional list does not bind.
#  请始终显式传入 `-Files`：在 Windows PowerShell 5.1 下裸位置参数列表不会绑定。
# ============================================================================
[CmdletBinding()]
param(
    [Parameter(Position = 0, ValueFromRemainingArguments = $true)]
    [string[]] $Files,

    [switch] $All,
    [string] $QtDir    = 'C:\Qt\6.11.1\mingw_64',
    [string] $MingwBin = 'C:\Mingw-w64\mingw64\bin'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$srcRoots = @(
    'src_Source_源代码/core_Core_核心',
    'src_Source_源代码/codegen_CodeGen_代码生成',
    'src_Source_源代码/blocks_Blocks_积木',
    'src_Source_源代码/ai_AI_智能',
    'src_Source_源代码/backend_Backend_后端',
    'src_Source_源代码/app_应用'
) | ForEach-Object { Join-Path $root $_ }

if ($All) {
    $Files = @()
    foreach ($dir in $srcRoots) {
        if (Test-Path $dir) {
            $Files += Get-ChildItem -Path $dir -Filter *.cpp -Recurse |
                      Select-Object -ExpandProperty FullName
        }
    }
}

if (-not $Files -or $Files.Count -eq 0) {
    Write-Host 'No files given. Pass -Files <paths>, or -All.' -ForegroundColor Yellow
    exit 2
}

$gxx = Join-Path $MingwBin 'g++.exe'
if (-not (Test-Path $gxx)) { throw "g++ not found at $gxx" }
if (-not (Test-Path (Join-Path $QtDir 'include'))) { throw "Qt include dir not found under $QtDir" }

$incArgs = @()
foreach ($d in $srcRoots) { $incArgs += "-I$d" }
$incArgs += "-I$(Join-Path $QtDir 'include')"

#  Qt module include directories / Qt 模块包含目录
$qtModules = @('QtCore', 'QtGui', 'QtNetwork', 'QtSql', 'QtConcurrent', 'QtSvg',
               'QtQml', 'QtQuick', 'QtQuickControls2', 'QtQuickLayouts', 'QtTest')
foreach ($m in $qtModules) {
    $p = Join-Path $QtDir "include/$m"
    if (Test-Path $p) { $incArgs += "-I$p" }
}

#  MSVC-compatibility shims are not needed; MinGW is the only compiler here.
$compileArgs = @('-fsyntax-only', '-std=c++20', '-fPIC', '-DQT_NO_DEBUG') + $incArgs

$failed = @()
$passed = 0

#  A compiler diagnostic on stderr is NOT a terminating PowerShell error. In
#  Windows PowerShell 5.1 there is no $PSNativeCommandUseErrorActionPreference,
#  so the preference is relaxed for the duration of the loop and failures are
#  detected from $LASTEXITCODE instead. Without this the first failing file
#  would abort the run and hide every later result.
#  编译器写入 stderr 的诊断信息并不是终止性 PowerShell 错误。Windows PowerShell
#  5.1 没有 $PSNativeCommandUseErrorActionPreference，因此在循环期间放宽该偏好，
#  改为依据 $LASTEXITCODE 判断失败。若不如此，第一个失败的文件就会中止整次运行，
#  从而隐藏后续所有结果。
$savedPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'

foreach ($f in $Files) {
    $full = if ([System.IO.Path]::IsPathRooted($f)) { $f } else { Join-Path $root $f }
    $relative = $full.Replace($root, '').TrimStart('\')

    if (-not (Test-Path $full)) {
        Write-Host "MISSING  $relative" -ForegroundColor Red
        $failed += $relative
        continue
    }

    $diagnostics = & $gxx @compileArgs $full 2>&1 | ForEach-Object { $_.ToString() }
    $exitCode = $LASTEXITCODE

    if ($exitCode -eq 0) {
        Write-Host "OK       $relative" -ForegroundColor Green
        $passed++
    } else {
        Write-Host "FAIL     $relative" -ForegroundColor Red
        $diagnostics | Select-Object -First 40 | ForEach-Object {
            Write-Host "         $_" -ForegroundColor DarkRed
        }
        $failed += $relative
    }
}

$ErrorActionPreference = $savedPreference

Write-Host ''
if ($failed.Count -gt 0) {
    Write-Host "passed=$passed failed=$($failed.Count)" -ForegroundColor Red
    Write-Host "failing files: $($failed -join ', ')" -ForegroundColor Red
    exit 1
}
Write-Host "passed=$passed failed=0" -ForegroundColor Green
exit 0
