# ============================================================================
#  build.ps1 -- local Windows build helper for TCTools-Build_Web
#  build.ps1 —— TCTools-Build_Web 的 Windows 本地构建助手
#
#  Wraps the three commands you would otherwise retype: CMake configure, build
#  and ctest -- plus the optional install + windeployqt staging step.
#  封装了你本来需要反复手敲的三条命令：CMake 配置、构建与 ctest —— 以及可选的
#  安装与 windeployqt 分阶段部署步骤。
#
#  Usage / 用法:
#    pwsh -File scripts_Scripts_脚本/build.ps1
#    pwsh -File scripts_Scripts_脚本/build.ps1 -BuildType Debug -Tests
#    pwsh -File scripts_Scripts_脚本/build.ps1 -Clean -WebEngine -Deploy
#    pwsh -File scripts_Scripts_脚本/build.ps1 -Install -InstallPrefix 'C:\TCBW'
#
#  Author's note / 作者说明:
#    The checkout path on the primary development machine contains non-ASCII
#    characters, so EVERY path is quoted and passed as a single argument.
#    主开发机上的检出路包含非 ASCII 字符，因此**所有**路径都被引号包裹并作为
#    单个参数传递。
# ============================================================================
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string] $BuildType = 'Release',

    [string] $BuildDir = 'build',

    # Qt kit root. Default matches the developers' local MinGW kit.
    # Qt 套件根目录。默认值与开发者本机的 MinGW 套件一致。
    [string] $QtDir = 'C:\Qt\6.11.1\mingw_64',

    [string] $MingwBin = 'C:\Mingw-w64\mingw64\bin',

    # CMake generator. 'auto' prefers Ninja + the MinGW toolchain when both are
    # present, and otherwise falls back to the Visual Studio generator.
    # CMake 生成器。'auto' 在二者都存在时优先使用 Ninja + MinGW 工具链，
    # 否则回退到 Visual Studio 生成器。
    [ValidateSet('auto', 'ninja', 'vs', 'mingw-makefiles')]
    [string] $Generator = 'auto',

    [string] $Arch = 'x64',

    [bool] $Tests = $true,
    [bool] $WebEngine = $true,
    [bool] $WarningsAsErrors = $false,

    [switch] $Clean,
    [switch] $Install,
    [string] $InstallPrefix = '',
    [switch] $Deploy,
    [switch] $NoConfigure,
    [int] $Parallel = 0
)

$ErrorActionPreference = 'Stop'

# ----------------------------------------------------------------------------
#  Helpers 辅助函数
# ----------------------------------------------------------------------------
function Write-Step {
    param([string] $Message)
    Write-Host ''
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Write-Info {
    param([string] $Message)
    Write-Host "    $Message"
}

function Invoke-Native {
    <#
      Runs a native command and throws on a non-zero exit code. Windows
      PowerShell 5.1 turns native stderr into error records, so the caller's
      ErrorActionPreference is relaxed for the duration of the call and the exit
      code is inspected explicitly.
      运行原生命令，非零退出码即抛出异常。Windows PowerShell 5.1 会把原生命令的
      stderr 变成错误记录，因此在调用期间放宽 ErrorActionPreference，并显式检查
      退出码。
    #>
    param(
        [Parameter(Mandatory = $true)][string] $Command,
        [Parameter(Mandatory = $true)][string[]] $Arguments
    )

    Write-Host "    > $Command $($Arguments -join ' ')" -ForegroundColor DarkGray
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $Command @Arguments
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previous
    }

    if ($exitCode -ne 0) {
        throw "'$Command' failed with exit code $exitCode."
    }
}

function Test-CommandExists {
    param([string] $Name)
    return [bool] (Get-Command $Name -ErrorAction SilentlyContinue)
}

# ----------------------------------------------------------------------------
#  0. Locate the repository root 定位仓库根目录
# ----------------------------------------------------------------------------
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path -LiteralPath (Join-Path $repoRoot 'CMakeLists.txt'))) {
    throw "CMakeLists.txt not found under '$repoRoot'. Is scripts_Scripts_脚本/ in the repository root?"
}
Set-Location -LiteralPath $repoRoot

$buildDirFull = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repoRoot $BuildDir }

Write-Step "TCTools-Build_Web local build / 本地构建"
Write-Info "Repo 仓库     : $repoRoot"
Write-Info "Build dir     : $buildDirFull"
Write-Info "Build type    : $BuildType"
Write-Info "Qt dir        : $QtDir"
Write-Info "Arch          : $Arch"
Write-Info "Tests         : $Tests"
Write-Info "WebEngine     : $WebEngine"

# ----------------------------------------------------------------------------
#  1. Validate the toolchain 校验工具链
# ----------------------------------------------------------------------------
Write-Step 'Validating toolchain / 校验工具链'

if (-not (Test-CommandExists 'cmake')) {
    throw 'cmake was not found on PATH. Install CMake 3.24+ or add it to PATH.'
}
Write-Info "cmake  : $((Get-Command cmake).Source)"
Write-Info "version: $(& cmake --version | Select-Object -First 1)"

if (-not (Test-Path -LiteralPath (Join-Path $QtDir 'include'))) {
    throw "Qt include directory not found under '$QtDir'. Pass -QtDir <path>."
}
Write-Info "Qt     : $QtDir"

# Prepend the MinGW bin directory so Ninja finds g++/gcc.
# 把 MinGW 的 bin 目录前置，使 Ninja 能找到 g++/gcc。
if (Test-Path -LiteralPath $MingwBin) {
    $env:PATH = "$MingwBin;$env:PATH"
    Write-Info "MinGW  : $MingwBin (prepended to PATH / 已前置到 PATH)"
}
else {
    Write-Warning "MinGW bin directory not found: '$MingwBin'"
}

# ----------------------------------------------------------------------------
#  2. Choose a generator 选择生成器
# ----------------------------------------------------------------------------
$useNinja = $false
$selectedGenerator = ''
$generatorArgs = @()

#  Ninja on Windows writes build.ninja as UTF-8 and hands the command line to
#  the compiler through a byte-oriented path. Every project folder name here is
#  bilingual ("src_Source_源代码"), so the object paths are non-ASCII and Ninja
#  cannot create them: the build dies with
#      Fatal error: can't create .../IdGenerator.cpp.obj: No such file or directory
#  GNU Make does not have that limitation, so 'auto' prefers "MinGW Makefiles"
#  whenever the source tree contains non-ASCII characters. Pass -Generator ninja
#  explicitly to override (for an ASCII checkout this is not needed).
#  Windows 上的 Ninja 以 UTF-8 写出 build.ninja，并以面向字节的方式把命令行交给
#  编译器。本项目所有文件夹名都是双语的（"src_Source_源代码"），因此目标文件路径
#  含非 ASCII 字符，Ninja 无法创建它们，构建会以
#      Fatal error: can't create .../IdGenerator.cpp.obj: No such file or directory
#  失败。GNU Make 没有该限制，因此只要源码树含非 ASCII 字符，'auto' 就优先选择
#  "MinGW Makefiles"。可显式传入 -Generator ninja 覆盖（ASCII 检出路径下无需如此）。
$repoHasNonAsciiPath = $false
foreach ($ch in (Split-Path -Parent $PSScriptRoot).ToCharArray()) {
    if ([int][char]$ch -gt 127) { $repoHasNonAsciiPath = $true; break }
}

switch ($Generator) {
    'ninja' {
        if (-not (Test-CommandExists 'ninja')) { throw "Generator 'ninja' requested but ninja is not on PATH." }
        $useNinja = $true
        $selectedGenerator = 'Ninja'
    }
    'mingw-makefiles' {
        $selectedGenerator = 'MinGW Makefiles'
        $useNinja = $false
    }
    'vs' {
        $selectedGenerator = 'Visual Studio 17 2022'
    }
    default {
        # 'auto' 自动
        if ($repoHasNonAsciiPath -and (Test-CommandExists 'mingw32-make')) {
            $selectedGenerator = 'MinGW Makefiles'
            Write-Info 'Non-ASCII path detected 检测到非 ASCII 路径: using MinGW Makefiles (Ninja cannot handle bilingual folder names).'
        }
        elseif ((Test-CommandExists 'ninja') -and (Test-CommandExists 'g++')) {
            $useNinja = $true
            $selectedGenerator = 'Ninja'
        }
        elseif (Test-CommandExists 'ninja') {
            $useNinja = $true
            $selectedGenerator = 'Ninja'
        }
        else {
            $selectedGenerator = 'Visual Studio 17 2022'
        }
    }
}
Write-Info "Generator 生成器: $selectedGenerator"

# ----------------------------------------------------------------------------
#  3. Configure 配置
# ----------------------------------------------------------------------------
$configureArgs = @(
    '-S', '.',
    '-B', $buildDirFull,
    '-G', $selectedGenerator,
    "-DCMAKE_PREFIX_PATH=$QtDir",
    "-DTCBW_BUILD_TESTS=$(if ($Tests) { 'ON' } else { 'OFF' })",
    "-DTCBW_ENABLE_WEBENGINE=$(if ($WebEngine) { 'ON' } else { 'OFF' })",
    "-DTCBW_WARNINGS_AS_ERRORS=$(if ($WarningsAsErrors) { 'ON' } else { 'OFF' })"
)

if ($useNinja) {
    # Ninja is single-config, so the build type is a configure-time variable.
    # Ninja 是单配置生成器，因此构建类型是配置期变量。
    $configureArgs += "-DCMAKE_BUILD_TYPE=$BuildType"
}
else {
    # Multi-config generators ignore CMAKE_BUILD_TYPE; -A selects the target
    # architecture for Visual Studio.
    # 多配置生成器忽略 CMAKE_BUILD_TYPE；Visual Studio 用 -A 选择目标架构。
    $archFlag = @{ 'x64' = 'x64'; 'x86' = 'Win32'; 'arm64' = 'ARM64' }[$Arch.ToLower()]
    if (-not $archFlag) { throw "Unsupported -Arch '$Arch'. Use x64, x86 or arm64." }
    $configureArgs += @('-A', $archFlag)
}

if ($Install -and $InstallPrefix) {
    $configureArgs += "-DCMAKE_INSTALL_PREFIX=$InstallPrefix"
}

if ($Clean -and (Test-Path -LiteralPath $buildDirFull)) {
    Write-Step 'Removing the previous build tree / 删除旧的构建目录'
    Write-Info "rm -rf $buildDirFull"
    Remove-Item -LiteralPath $buildDirFull -Recurse -Force
}

if ($NoConfigure) {
    Write-Step 'Skipping configure (-NoConfigure) / 跳过配置（-NoConfigure）'
}
else {
    Write-Step 'Configuring / 配置'
    Invoke-Native -Command 'cmake' -Arguments $configureArgs
}

# ----------------------------------------------------------------------------
#  4. Build 构建
# ----------------------------------------------------------------------------
Write-Step 'Building / 构建'
$buildArgs = @('--build', $buildDirFull)
if (-not $useNinja) { $buildArgs += @('--config', $BuildType) }
$buildArgs += '--parallel'
if ($Parallel -gt 0) { $buildArgs += $Parallel.ToString() }
Invoke-Native -Command 'cmake' -Arguments $buildArgs

# ----------------------------------------------------------------------------
#  5. Test 测试
# ----------------------------------------------------------------------------
if ($Tests) {
    Write-Step 'Running tests / 运行测试'
    $ctestArgs = @('--test-dir', $buildDirFull)
    if (-not $useNinja) { $ctestArgs += @('-C', $BuildType) }
    $ctestArgs += '--output-on-failure'
    Invoke-Native -Command 'ctest' -Arguments $ctestArgs
}
else {
    Write-Step 'Tests disabled (-Tests $false) / 测试已禁用'
}

# ----------------------------------------------------------------------------
#  6. Install and deploy 安装与部署
# ----------------------------------------------------------------------------
if ($Install) {
    Write-Step 'Installing / 安装'
    $installArgs = @('--install', $buildDirFull)
    if (-not $useNinja) { $installArgs += @('--config', $BuildType) }
    if ($InstallPrefix) { $installArgs += @('--prefix', $InstallPrefix) }
    Invoke-Native -Command 'cmake' -Arguments $installArgs
}

if ($Deploy) {
    Write-Step 'Deploying the Qt runtime / 部署 Qt 运行时'

    $deployScript = Join-Path $repoRoot 'packaging_Packaging_打包/windows_Windows/deploy_windows.ps1'
    if (-not (Test-Path -LiteralPath $deployScript)) {
        throw "Deploy script not found: '$deployScript'"
    }

    $targetDir = if ($Install -and $InstallPrefix) { $InstallPrefix } else { $buildDirFull }
    & $deployScript -BinaryDir $targetDir -QtDir $QtDir -MingwBin $MingwBin -Verify
    if ($LASTEXITCODE -ne 0) { throw "deployment failed with exit code $LASTEXITCODE" }
}

# ----------------------------------------------------------------------------
#  7. Summary 汇总
# ----------------------------------------------------------------------------
Write-Step 'Done / 完成'
$artifacts = Get-ChildItem -LiteralPath $buildDirFull -Filter 'TCTools-Build_Web.exe' -Recurse -File -ErrorAction SilentlyContinue
if ($artifacts) {
    foreach ($artifact in $artifacts) {
        Write-Host ("    {0}  ({1:N1} MB)" -f $artifact.FullName, ($artifact.Length / 1MB)) -ForegroundColor Green
    }
}
else {
    Write-Info "No executable found under '$buildDirFull' yet."
}
Write-Host ''
Write-Host 'Next steps / 后续步骤:' -ForegroundColor Cyan
Write-Host "    Run  运行   : `"$buildDirFull\TCTools-Build_Web.exe`""
Write-Host "    Deploy 部署 : pwsh -File `"packaging_Packaging_打包/windows_Windows/deploy_windows.ps1`" -BinaryDir `"$buildDirFull`" -QtDir `"$QtDir`""
Write-Host "    Installer   : makensis /DTCBW_VERSION=0.1.0-rc1 packaging_Packaging_打包/windows_Windows/windows_installer.nsi"
