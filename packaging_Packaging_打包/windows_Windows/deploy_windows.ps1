# ============================================================================
#  deploy_windows.ps1 -- stage the Qt + MinGW runtime next to the built .exe
#  deploy_windows.ps1 —— 把 Qt 与 MinGW 运行时部署到已构建的 .exe 旁边
#
#  Why / 为什么需要它:
#    CMake's `qt_generate_deploy_qml_app_script` copies Qt libraries, QML modules
#    and plugins when you run `cmake --install`; windeployqt is the belt-and-
#    braces pass that also pulls in anything the CMake scan missed and adds the
#    MinGW runtime DLLs that the Qt kit does not ship in the app directory.
#    CMake 的 `qt_generate_deploy_qml_app_script` 会在 `cmake --install` 时复制
#    Qt 库、QML 模块与插件；windeployqt 是第二道保险：补齐 CMake 扫描遗漏的
#    内容，并加入 Qt 套件不会放进程序目录的 MinGW 运行时 DLL。
#
#  Usage / 用法:
#    pwsh -File deploy_windows.ps1 -BinaryDir 'C:\repo\dist\stage'
#    pwsh -File deploy_windows.ps1 -BinaryDir 'build\Release' -QtDir 'C:\Qt\6.11.1\mingw_64'
#    pwsh -File deploy_windows.ps1 -BinaryDir 'build\Release' -Verify
# ============================================================================
[CmdletBinding()]
param(
    # Directory that holds TCTools-Build_Web.exe (or its subdirectory).
    # 存放 TCTools-Build_Web.exe 的目录（或其上级目录）。
    [Parameter(Mandatory = $true)]
    [string] $BinaryDir,

    # Qt kit root, e.g. C:\Qt\6.11.1\mingw_64
    # Qt 套件根目录，例如 C:\Qt\6.11.1\mingw_64
    [string] $QtDir = 'C:\Qt\6.11.1\mingw_64',

    # MinGW runtime source directory.
    # MinGW 运行时来源目录。
    [string] $MingwBin = 'C:\Mingw-w64\mingw64\bin',

    [string] $ExeName = 'TCTools-Build_Web.exe',

    # Fail if a required runtime DLL is still missing afterwards.
    # 若之后仍缺少必需的运行时 DLL，则报错退出。
    [switch] $Verify,

    # Do not copy the C++ runtime (use when the target already ships it).
    # 不复制 C++ 运行时（目标机已自带时使用）。
    [switch] $NoCompilerRuntime
)

$ErrorActionPreference = 'Stop'

# MinGW runtime DLLs required by any MinGW-built Qt application.
# MinGW 构建的 Qt 应用所必需的 MinGW 运行时 DLL。
$script:MingwRuntimeDlls = @(
    'libgcc_s_seh-1.dll'
    'libstdc++-6.dll'
    'libwinpthread-1.dll'
)

# DLLs that MUST be present next to the executable after deployment.
# 部署完成后必须与可执行文件同目录存在的 DLL。
$script:RequiredDlls = @(
    'Qt6Core.dll'
    'Qt6Gui.dll'
    'Qt6Qml.dll'
    'Qt6Quick.dll'
    'Qt6QuickControls2.dll'
    'Qt6Network.dll'
    'Qt6Sql.dll'
    'Qt6Concurrent.dll'
    'Qt6Svg.dll'
)

function Write-Step {
    param([string] $Message)
    Write-Host ''
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Resolve-QtBinDir {
    param([string] $Root)

    # Prefer the Qt bin/ directory; fall back to a windeployqt found on PATH.
    # 优先使用 Qt 的 bin/ 目录；找不到时回退到 PATH 上的 windeployqt。
    $candidates = @(
        (Join-Path $Root 'bin')
        $Root
    )
    foreach ($dir in $candidates) {
        if (Test-Path -LiteralPath $dir) {
            $tool = Join-Path $dir 'windeployqt.exe'
            if (Test-Path -LiteralPath $tool) { return $dir }
        }
    }
    $onPath = Get-Command 'windeployqt.exe' -ErrorAction SilentlyContinue
    if ($onPath) { return (Split-Path -Parent $onPath.Source) }
    throw "windeployqt.exe not found. Looked under '$Root' and on PATH. Pass -QtDir <qt-kit-root>."
}

# ----------------------------------------------------------------------------
#  1. Validate inputs 校验输入
# ----------------------------------------------------------------------------
Write-Step 'Validating inputs / 校验输入'

if (-not (Test-Path -LiteralPath $BinaryDir)) {
    throw "BinaryDir does not exist: '$BinaryDir'"
}
$BinaryDir = (Resolve-Path -LiteralPath $BinaryDir).Path

if (-not (Test-Path -LiteralPath (Join-Path $QtDir 'include'))) {
    Write-Warning "Qt include directory not found under '$QtDir' -- continuing, windeployqt search may still succeed."
}

$exePath = Join-Path $BinaryDir $ExeName
if (-not (Test-Path -LiteralPath $exePath)) {
    # Multi-config generators put the binary in a per-config subdirectory.
    # 多配置生成器会把可执行文件放在按配置命名的子目录中。
    $found = Get-ChildItem -LiteralPath $BinaryDir -Filter $ExeName -Recurse -File -ErrorAction SilentlyContinue |
        Sort-Object -Property @{ Expression = { if ($_.FullName -match '\\Release\\') { 0 } else { 1 } } }, FullName |
        Select-Object -First 1
    if (-not $found) {
        throw "Executable '$ExeName' not found anywhere under '$BinaryDir'. Build first."
    }
    $exePath = $found.FullName
}
$targetDir = Split-Path -Parent $exePath
Write-Host "Executable 可执行文件 : $exePath"
Write-Host "Target dir 目标目录   : $targetDir"

# ----------------------------------------------------------------------------
#  2. Run windeployqt 执行 windeployqt
# ----------------------------------------------------------------------------
Write-Step 'Running windeployqt / 执行 windeployqt'

$qtBin = Resolve-QtBinDir -Root $QtDir
$windeployqt = Join-Path $qtBin 'windeployqt.exe'
Write-Host "windeployqt : $windeployqt"
Write-Host "Qt version  : $(& $windeployqt --version 2>&1 | Select-Object -First 1)"

$deployArgs = @(
    '--release'
    '--verbose', '1'
    '--no-system-d3d-compiler'
    '--no-opengl-sw'
    '--compiler-runtime'
    '--dir', $targetDir
)

if ($NoCompilerRuntime) {
    # Replace the runtime flag rather than passing both.
    # 用“不带运行时”替换相应参数，而不是同时传入两者。
    $deployArgs = $deployArgs | Where-Object { $_ -ne '--compiler-runtime' }
    $deployArgs += '--no-compiler-runtime'
}

# Point windeployqt at the Qt QML directory so dynamic QML plugins are found.
# 指向 Qt 的 QML 目录，以便找到动态加载的 QML 插件。
$qmlDir = Join-Path $QtDir 'qml'
if (Test-Path -LiteralPath $qmlDir) {
    $deployArgs += @('--qmlimport', $qmlDir)
}

# Ship Qt's own translations (qtbase_zh_CN.qm etc.).
# 附带 Qt 自带翻译（qtbase_zh_CN.qm 等）。
$translationsDir = Join-Path $QtDir 'translations'
if (Test-Path -LiteralPath $translationsDir) {
    $deployArgs += @('--translations', 'en,zh_CN')
}

$deployArgs += $exePath

# On Windows PowerShell 5.1 the native command's stderr is written as an error
# record when redirected; temporarily relax the preference so it is captured
# rather than thrown.
# 在 Windows PowerShell 5.1 中，重定向时原生命令的 stderr 会写成错误记录；
# 临时放宽设置，以便捕获而不是抛出。
$previousEap = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    & $windeployqt @deployArgs 2>&1 | ForEach-Object { Write-Host "    $_" }
    $deployExit = $LASTEXITCODE
}
finally {
    $ErrorActionPreference = $previousEap
}
if ($deployExit -ne 0) {
    throw "windeployqt failed with exit code $deployExit."
}

# ----------------------------------------------------------------------------
#  3. Copy the MinGW runtime DLLs 复制 MinGW 运行时 DLL
# ----------------------------------------------------------------------------
Write-Step 'Copying MinGW runtime / 复制 MinGW 运行时'

$mingwSources = @($MingwBin, $qtBin) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -Unique
$copiedMingw = 0
foreach ($dll in $script:MingwRuntimeDlls) {
    $source = $null
    foreach ($dir in $mingwSources) {
        $candidate = Join-Path $dir $dll
        if (Test-Path -LiteralPath $candidate) { $source = $candidate; break }
    }
    if ($null -eq $source) {
        # Expected on MSVC builds: MSVC links the UCRT and needs no MinGW DLLs.
        # MSVC 构建下这是预期情况：MSVC 链接 UCRT，不需要 MinGW DLL。
        Write-Host "    skip (not found) 跳过（未找到）: $dll" -ForegroundColor DarkYellow
        continue
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $targetDir $dll) -Force
    Write-Host "    copied 已复制: $dll" -ForegroundColor Green
    $copiedMingw++
}
if ($copiedMingw -eq 0) {
    Write-Host '    No MinGW runtime copied -- expected for MSVC kits.' -ForegroundColor DarkYellow
}

# ----------------------------------------------------------------------------
#  4. Verify 校验
# ----------------------------------------------------------------------------
Write-Step 'Verifying deployment / 校验部署结果'

$missing = @()
foreach ($dll in $script:RequiredDlls) {
    if (-not (Test-Path -LiteralPath (Join-Path $targetDir $dll))) { $missing += $dll }
}

# Platform plugins are mandatory for a GUI app to start at all.
# 平台插件是 GUI 程序能启动的必要条件。
$platformPlugin = Join-Path $targetDir 'plugins\platforms\qwindows.dll'
if (-not (Test-Path -LiteralPath $platformPlugin)) {
    $alt = Get-ChildItem -LiteralPath $targetDir -Filter 'qwindows.dll' -Recurse -File -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $alt) { $missing += 'plugins\platforms\qwindows.dll' }
}

$dllCount = (Get-ChildItem -LiteralPath $targetDir -Filter '*.dll' -File -ErrorAction SilentlyContinue |
    Measure-Object).Count
$totalMb = [math]::Round(
    ((Get-ChildItem -LiteralPath $targetDir -Recurse -File -ErrorAction SilentlyContinue |
        Measure-Object -Property Length -Sum).Sum / 1MB), 2)

Write-Host "    DLLs at top level 顶层 DLL 数 : $dllCount"
Write-Host "    Staged size 分阶段大小       : $totalMb MB"

if ($missing.Count -gt 0) {
    $message = "Missing runtime after deployment 部署后仍缺少运行时: $($missing -join ', ')"
    if ($Verify) { throw $message }
    Write-Warning $message
}
else {
    Write-Host '    All required runtime DLLs present. 全部必需运行时 DLL 均已就位。' -ForegroundColor Green
}

Write-Step 'Deployment complete / 部署完成'
Write-Host "Staged directory 分阶段目录: $targetDir"
