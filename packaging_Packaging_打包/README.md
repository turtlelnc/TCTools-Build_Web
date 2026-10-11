# Packaging Guide / 打包指南

TCTools-Build_Web (`TC-BuildWeb`) · v0.1.0-rc1 · publisher **turtlelnc**
TCTools-Build_Web（`TC-BuildWeb`）· v0.1.0-rc1 · 发布者 **turtlelnc**

Everything needed to turn a successful build into a distributable installer.
本目录包含把一次成功构建变成可分发安装包所需的全部内容。
For the *what/where* matrix, see [PACKAGING_SPECS.md](./PACKAGING_SPECS.md).
关于“发布什么、在哪里发布”的矩阵，见 [PACKAGING_SPECS.md](./PACKAGING_SPECS.md)。

---

## Directory layout 目录结构

```text
packaging_Packaging_打包/
├── PACKAGING_SPECS.md              # target matrix / 目标矩阵
├── README.md                       # this file / 本文件
├── windows_Windows/
│   ├── windows_installer.nsi       # NSIS 3.x installer / NSIS 3.x 安装包脚本
│   └── deploy_windows.ps1          # windeployqt + MinGW runtime / 部署脚本
├── macos_macOS/
│   ├── Info.plist.in               # bundle metadata template / 包元数据模板
│   ├── deploy_macos.sh             # frameworks + rpath + codesign / 部署脚本
│   └── create_dmg.sh               # .dmg builder / DMG 生成脚本
└── linux_Linux/
    ├── tctools-buildweb.desktop    # freedesktop launcher / 启动器条目
    ├── deploy_linux.sh             # staging + .deb / 分阶段与 .deb
    ├── build_appimage.sh           # .AppImage builder / AppImage 生成脚本
    └── debian_control/control      # dpkg control template / control 模板
```

---

## Golden rule: install first, deploy second 黄金法则：先安装，再部署

`CMakeLists.txt` calls `qt_generate_deploy_qml_app_script`, so **`cmake --install`
already copies the Qt libraries, QML modules and plugins** into the install tree.
The platform scripts in this directory run *after* the install and only top up
what CMake could not resolve (MinGW runtime DLLs, rpaths, DMG packaging, ...).
Running them against a raw build directory works, but the result is incomplete.
`CMakeLists.txt` 调用了 `qt_generate_deploy_qml_app_script`，因此
**`cmake --install` 本身就会把 Qt 库、QML 模块与插件**复制到安装目录树中。
本目录的平台脚本在安装**之后**运行，只补齐 CMake 无法解析的部分（MinGW 运行时
DLL、rpath、DMG 打包等）。直接对原始构建目录运行也能工作，但产物是不完整的。

```bash
# Correct order / 正确顺序
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$PWD/dist/stage"     # <- Qt lands here / Qt 在此就位
```

---

## Windows

**Prerequisites 前置条件**

| Tool | Notes 说明 |
|---|---|
| NSIS 3.x | `makensis` on `PATH`, or `C:\Program Files (x86)\NSIS\makensis.exe` |
| Qt kit | MinGW kit (default `C:\Qt\6.11.1\mingw_64`) or an MSVC kit |
| MinGW-w64 | `C:\Mingw-w64\mingw64\bin` — source of the MinGW runtime DLLs |

**1. Deploy the runtime 部署运行时**

```powershell
pwsh -File packaging_Packaging_打包/windows_Windows/deploy_windows.ps1 `
     -BinaryDir 'dist\stage' `
     -QtDir 'C:\Qt\6.11.1\mingw_64' `
     -MingwBin 'C:\Mingw-w64\mingw64\bin' `
     -Verify
```

`-Verify` turns a missing `Qt6Qml.dll` or `plugins\platforms\qwindows.dll` into a
hard failure instead of a silently broken installer.
`-Verify` 会把缺少 `Qt6Qml.dll` 或 `plugins\platforms\qwindows.dll` 变成硬失败，
而不是一个静默损坏的安装包。

**2. Build the installer 生成安装包**

```powershell
makensis /V2 `
  /DTCBW_VERSION=0.1.0-rc1 `
  /DTCBW_STAGE="$PWD\dist\stage" `
  /DTCBW_OUTFILE="$PWD\dist\TCBuildWeb-0.1.0-rc1-windows-x64-setup.exe" `
  /DTCBW_ARCH=x64 `
  'packaging_Packaging_打包/windows_Windows/windows_installer.nsi'
```

**Installer behaviour 安装包行为**

| Feature 功能 | Behaviour 行为 |
|---|---|
| Install scope 安装范围 | **Per-user by default** (no UAC). Pass `/ALLUSERS` for a machine-wide install with elevation / **默认当前用户**（不弹 UAC）。传 `/ALLUSERS` 则全机安装并提权 |
| Silent install 静默安装 | `setup.exe /S` (add `/ALLUSERS` for per-machine) / `setup.exe /S`（全机安装再加 `/ALLUSERS`） |
| Shortcuts 快捷方式 | Start Menu (optional component) + Desktop (optional component) / 开始菜单（可选组件）+ 桌面（可选组件） |
| Uninstaller 卸载程序 | `Uninstall.exe` + an entry in *Apps & features* / `Uninstall.exe` + “应用和功能”条目 |
| File association 文件关联 | `.tcbw` projects → `TCTools-Build_Web` (optional component) / `.tcbw` 工程（可选组件） |
| Minimum OS 最低系统 | Windows 10 1709 (build 16299); enforced in `.onInit` / 在 `.onInit` 中强制校验 |

> **Note / 注意:** the `windows_installer.nsi` default `TCBW_STAGE` resolves
> relative to the script file, so `makensis` works from any working directory.
> `windows_installer.nsi` 的 `TCBW_STAGE` 默认值相对于脚本文件解析，因此从任意
> 工作目录运行 `makensis` 都可以。

---

## macOS

**1. Deploy and finalise the bundle 部署并收尾应用包**

```bash
QT_ROOT_DIR=/opt/Qt/6.8.3/macos \
bash packaging_Packaging_打包/macos_macOS/deploy_macos.sh \
  --app  dist/stage/TCTools-Build_Web.app \
  --arch arm64 \
  --qt-dir "$QT_ROOT_DIR" \
  --version 0.1.0-rc1
```

For a universal binary pass `--arch 'arm64;x86_64'`; the script then **asserts**
that both slices are present in the main executable and fails if one is missing.
通用二进制传 `--arch 'arm64;x86_64'`；脚本会**断言**主可执行文件含两种切片，
缺失即失败。

Set `CODESIGN_IDENTITY='Developer ID Application: ...'` to produce a real
signature; without it the bundle is ad-hoc signed and is suitable for local
testing only.
设置 `CODESIGN_IDENTITY='Developer ID Application: ...'` 可生成正式签名；
不设置时为临时签名，仅适合本地测试。

**2. Build the DMG 生成 DMG**

```bash
bash packaging_Packaging_打包/macos_macOS/create_dmg.sh \
  --app  dist/stage/TCTools-Build_Web.app \
  --version 0.1.0-rc1 \
  --arch arm64 \
  --output-dir dist
```

Uses `create-dmg` when installed (drag-to-install layout) and falls back to
`hdiutil` otherwise; both paths produce a compressed `.dmg` plus a `.sha256`.
装有 `create-dmg` 时使用它（拖拽安装布局），否则回退到 `hdiutil`；两条路径都会
生成压缩 `.dmg` 及其 `.sha256`。

**3. `Info.plist.in` 元数据模板**

CMake generates a working `Info.plist` from the `MACOSX_BUNDLE_*` target
properties. This template adds what CMake does not manage — document types,
category, ATS policy, usage strings. Wire it up with `configure_file(... @ONLY)`
and `MACOSX_BUNDLE_INFO_PLIST`; see the header of that file for the exact snippet
and the substitution placeholders.
CMake 会依据 `MACOSX_BUNDLE_*` 目标属性生成可用的 `Info.plist`。本模板补充 CMake
不管理的内容 —— 文档类型、分类、ATS 策略、用途说明。用 `configure_file(... @ONLY)`
与 `MACOSX_BUNDLE_INFO_PLIST` 接入；精确代码片段与替换占位符见该文件头部。

---

## Linux

**1. `.deb` and portable tarball .deb 与可移植压缩包**

```bash
bash packaging_Packaging_打包/linux_Linux/deploy_linux.sh \
  --build-dir build \
  --output-dir dist \
  --version 0.1.0-rc1 \
  --arch amd64
```

Stages `cmake --install` output, installs the `.desktop` entry, icons and
AppStream metainfo, rewrites `debian_control/control` (Version, Architecture,
Installed-Size, Maintainer) and calls `dpkg-deb`. Validate with:
对 `cmake --install` 的产物进行分阶段，安装 `.desktop` 条目、图标与 AppStream
元信息，重写 `debian_control/control`（Version、Architecture、Installed-Size、
Maintainer）并调用 `dpkg-deb`。可用以下命令校验：

```bash
desktop-file-validate packaging_Packaging_打包/linux_Linux/tctools-buildweb.desktop
sudo apt install ./dist/tctools-buildweb_0.1.0-rc1_amd64.deb
```

**2. AppImage**

```bash
bash packaging_Packaging_打包/linux_Linux/build_appimage.sh \
  --build-dir build \
  --output-dir dist \
  --qt-dir "$QT_ROOT_DIR" \
  --version 0.1.0-rc1
```

Downloads-free: pass `--linuxdeploy /path/to/linuxdeploy-x86_64.AppImage`.
The script copies the Qt QML import tree, stages translations, runs linuxdeploy,
then smoke-tests the result (offscreen, 25 s timeout — a timeout counts as
success because the GUI app has no `--version` handler).
无需联网下载：传 `--linuxdeploy /path/to/linuxdeploy-x86_64.AppImage`。脚本会复制
Qt QML 导入目录树、暂存翻译、运行 linuxdeploy，然后对结果做冒烟测试（offscreen，
25 秒超时 —— 超时视为成功，因为 GUI 应用没有 `--version` 处理逻辑）。

**Runtime dependencies 运行时依赖** are declared in
`debian_control/control` and use `|` alternatives because Ubuntu 22.04 and 24.04
name the QML modules differently. Qt WebEngine is a `Recommends:`, never a
`Depends:` — matching the optional `TCBW_ENABLE_WEBENGINE` CMake option.
运行时依赖声明在 `debian_control/control` 中，并使用 `|` 备选项，因为
Ubuntu 22.04 与 24.04 的 QML 模块命名不同。Qt WebEngine 位于 `Recommends:`，
绝不在 `Depends:` —— 与可选的 `TCBW_ENABLE_WEBENGINE` CMake 选项保持一致。

---

## Local build helper 本地构建助手

```powershell
# Windows
pwsh -File scripts_Scripts_脚本/build.ps1 -BuildType Release -Tests
pwsh -File scripts_Scripts_脚本/build.ps1 -Clean -Install -InstallPrefix 'C:\TCBW' -Deploy
```

```bash
# Linux / macOS
./scripts_Scripts_脚本/build.sh --build-type Release
./scripts_Scripts_脚本/build.sh --clean --package deb
QT_ROOT_DIR=/opt/Qt/6.8.3/macos ./scripts_Scripts_脚本/build.sh --arch 'arm64;x86_64' --package dmg
```

Both helpers quote **every** path, because the primary development checkout
lives under a directory containing non-ASCII characters.
两个助手脚本对**每一个**路径都加引号，因为主开发检出路位于含非 ASCII 字符的目录下。

---

## CI and releases CI 与发布

| Workflow 工作流 | Trigger 触发 | Purpose 用途 |
|---|---|---|
| `.github/workflows/build_构建.yml` | push / PR / weekly cron / manual | Full matrix: configure, build, test, artifacts / 全矩阵：配置、构建、测试、产物 |
| `.github/workflows/release_发布.yml` | tag `v*` / manual | Build installers, publish a GitHub Release / 构建安装包并发布 GitHub Release |
| `.github/dependabot.yml` | weekly | Keep Actions and the Qt toolchain current / 保持 Actions 与 Qt 工具链更新 |

To cut a release 发布一个版本:

```bash
git tag v0.1.0-rc1
git push origin v0.1.0-rc1        # the Lead owns git / git 操作由负责人执行
```

A tag containing `-` is published as a pre-release automatically.
含 `-` 的标签会自动作为预发布版本发布。

---

## Troubleshooting 故障排查

| Symptom 现象 | Cause 原因 | Fix 解决 |
|---|---|---|
| AppImage/`.deb` builds but the app shows a blank window | QML modules not staged / QML 模块未暂存 | Pass `--qt-dir` so the QML import tree is copied / 传 `--qt-dir` 以复制 QML 导入树 |
| `windeployqt` reports "cannot find Qt6Core.dll" | `-QtDir` points at the wrong kit / `-QtDir` 指向错误的套件 | Point `-QtDir` at the kit root (the directory containing `bin/`), not at `bin/` itself / 指向含 `bin/` 的套件根目录，而非 `bin/` 本身 |
| `.exe` fails with `libgcc_s_seh-1.dll is missing` | MinGW runtime not copied / 未复制 MinGW 运行时 | Pass `-MingwBin 'C:\Mingw-w64\mingw64\bin'` / 传入该参数 |
| macOS: "app is damaged and can't be opened" | Quarantine attribute / 隔离属性 | `xattr -cr /Applications/TCTools-Build_Web.app` |
| macOS: universal build fails at link time | Host SDK cannot emit x86_64 / 宿主 SDK 无法生成 x86_64 | Build the Intel slice on an Intel runner / 在 Intel 运行器上构建 Intel 切片 |
| macOS: stale Qt path still in a binary | `macdeployqt` skipped a helper / `macdeployqt` 漏掉了某个组件 | Re-run `deploy_macos.sh` with `--qt-dir` set; check the scan output / 带 `--qt-dir` 重跑并查看扫描输出 |
| CI: "Incomplete source tree" | `CMakeLists.txt` references sources that do not exist yet / `CMakeLists.txt` 引用了尚不存在的源文件 | Complete the source tree; the job lists every missing path / 补齐源码树，作业会列出所有缺失路径 |
