# PACKAGING_SPECS.md — Target Matrix Specification
# 打包规格 —— 目标矩阵说明

TCTools-Build_Web (`TC-BuildWeb`) · v0.1.0-rc1 · publisher **turtlelnc**
TCTools-Build_Web（`TC-BuildWeb`）· v0.1.0-rc1 · 发布者 **turtlelnc**

This file is the single source of truth for **what** we ship and **which runner**
produces it. Implementation lives in `packaging_Packaging_打包/` and
`.github/workflows/`.
本文件是「我们发布什么」与「由哪个运行器产出」的唯一权威说明。具体实现位于
`packaging_Packaging_打包/` 与 `.github/workflows/`。

Target matrix 目标矩阵
======================

## 1. Support matrix 支持矩阵

| # | Platform 平台 | Minimum version 最低版本 | Arch 架构 | Runner 运行器 | Qt kit | Artifact 产物 | WebEngine |
|---|---|---|---|---|---|---|---|
| 1 | Windows | 10 1709 (build 16299) / 11 | x64 | `windows-latest` | `win64_msvc2022_64` | `*-setup.exe` | ON |
| 2 | Windows | 10 1709 / 11 | x86 (Win32) | `windows-latest` | `win32_mingw` | `*-setup.exe` | OFF |
| 3 | Windows | 11 on ARM | ARM64 (native) | `windows-11-arm` | `win64_msvc2022_arm64` | `*-setup.exe` | OFF |
| 4 | Windows | 11 on ARM | ARM64 (cross) | `windows-latest` + `-A ARM64` | `win64_msvc2022_arm64` | build only | OFF |
| 5 | Windows | 10 1709 / 11 | x64 | `windows-latest` | `win64_mingw` | build only | OFF |
| 6 | macOS | 11.0 Big Sur | arm64 (Apple Silicon) | `macos-14`, `macos-latest` | `clang_64` | `*-arm64.dmg` | ON |
| 7 | macOS | 11.0 Big Sur | x86_64 (Intel) | `macos-15-intel` | `clang_64` | build only | OFF |
| 8 | macOS | 11.0 Big Sur | universal `arm64;x86_64` | `macos-14` | `clang_64` | `*-universal.dmg` | OFF |
| 9 | Linux | glibc 2.35 (Ubuntu 22.04 / Debian 12) | x86_64 | `ubuntu-22.04` | `linux_gcc_64` | build only | OFF |
| 10 | Linux | glibc 2.39 (Ubuntu 24.04 / Debian 13) | x86_64 | `ubuntu-24.04` | `linux_gcc_64` | build only | OFF |
| 11 | Linux | Ubuntu 24.04 | aarch64 | `ubuntu-24.04-arm` | `linux_gcc_64` | build only | OFF |
| 12 | Linux | AppImage (portable) | x86_64 | `ubuntu-24.04` | `linux_gcc_64` | `*.AppImage` | OFF |
| 13 | Debian/Ubuntu | any with Qt 6.5+ | amd64 | `ubuntu-24.04` | `linux_gcc_64` | `*.deb` | OFF |

Linux x86 (32-bit) is **not** supported: Qt does not publish pre-built Linux x86
packages, so no kit exists to build against.
Linux x86（32 位）**不予支持**：Qt 不提供预编译的 Linux x86 包，没有可用的套件。

## 2. Qt configuration Qt 配置

| Item 项目 | Value 取值 | Rationale 理由 |
|---|---|---|
| Qt version Qt 版本 | `6.8.*` (LTS) | Pinned spec; ≥ 6.5 as required by `find_package(Qt6 6.5 REQUIRED)` |
| `aqtversion` | `==3.3.*` | Pinned so a new aqtinstall cannot silently change the install |
| Required modules 必需模块 | `Core Gui Qml Quick QuickControls2 QuickLayouts Network Sql Concurrent Svg Test` | Exactly what `CMakeLists.txt` requires — nothing more |
| Addon modules 附加模块 | `qtcharts qtmultimedia qtshadertools qtquick3d` | Cheap, plausibly needed by the AI/backend/UI subsystems |
| WebEngine | optional, `qtwebengine` installed only on Windows x64 and macOS arm64 | `TCBW_ENABLE_WEBENGINE` must degrade gracefully; all other jobs pass `OFF` |

> **Why WebEngine is off almost everywhere.** `TCBW_ENABLE_WEBENGINE` defaults to
> `ON`, but the CMake logic only enables the embedded preview when
> `Qt6WebEngineQuick` is actually found. Passing `-DTCBW_ENABLE_WEBENGINE=OFF`
> on runners without the WebEngine addon keeps CI green while still exercising
> the graceful-degradation path — which is the behaviour we ship to users who
> install Qt without WebEngine.
>
> **为何绝大多数作业关闭 WebEngine。** `TCBW_ENABLE_WEBENGINE` 默认为 `ON`，
> 但 CMake 逻辑只有在真正找到 `Qt6WebEngineQuick` 时才启用内嵌预览。在没有
> WebEngine 附加模块的运行器上传 `-DTCBW_ENABLE_WEBENGINE=OFF`，既能保持 CI 为绿，
> 又能覆盖优雅降级路径 —— 这正是未安装 WebEngine 的用户实际会走的分支。

## 3. Common CMake options 通用 CMake 选项

```text
-DCMAKE_BUILD_TYPE=Release            # single-config generators / 单配置生成器
-DCMAKE_PREFIX_PATH=$QT_ROOT_DIR      # set by install-qt-action / 由 install-qt-action 设置
-DTCBW_BUILD_TESTS=ON                 # project default / 工程默认值
-DTCBW_ENABLE_WEBENGINE=<ON|OFF>      # per-job / 按作业决定
-DTCBW_WARNINGS_AS_ERRORS=OFF         # ON in the dedicated `test` job / 专用 test 作业为 ON
-DCMAKE_C_COMPILER_LAUNCHER=ccache    # and CXX / 以及 CXX
```

Every job runs the full sequence: **configure → build → `ctest --output-on-failure`
→ upload artifacts**. Logs are uploaded on failure, binaries on success.
每个作业均执行完整流程：**配置 → 构建 → `ctest --output-on-failure` → 上传产物**。
失败时上传日志，成功时上传二进制产物。

## 4. Artifact naming 产物命名

```text
TCBuildWeb-<version>-windows-<x64|x86|arm64>-setup.exe
TCBuildWeb-<version>-macos-<arm64|universal>.dmg
TCBuildWeb-<version>-linux-x86_64.AppImage
tctools-buildweb_<version>_<amd64|arm64>.deb
tctools-buildweb_<version>_<amd64|arm64>.tar.gz
```

Every distributable gets a sibling `.sha256` checksum file.
每个可分发产物都会附带同名 `.sha256` 校验和文件。

## 5. Version scheme 版本方案

`PROJECT_VERSION` is `0.1.0` and the release channel is `rc1`, so the full
version is `0.1.0-rc1` (`TCBW_VERSION_FULL` in `CMakeLists.txt`).
`PROJECT_VERSION` 为 `0.1.0`，发布通道为 `rc1`，因此完整版本号为 `0.1.0-rc1`
（`CMakeLists.txt` 中的 `TCBW_VERSION_FULL`）。

| Context 场景 | Representation 表示 |
|---|---|
| Git tag | `v0.1.0-rc1` |
| NSIS `VIProductVersion` | `0.1.0.0` (four integers required / 必须四位整数) |
| macOS `CFBundleShortVersionString` | `0.1.0` |
| macOS `CFBundleVersion` | `0.1.0` (numeric, monotonic / 纯数字且单调递增) |
| `.deb` `Version` | `0.1.0-rc1` |

A version containing `-` is published as a GitHub **pre-release**.
含 `-` 的版本号会以 GitHub **预发布（pre-release）**形式发布。

## 6. Known constraints and assumptions 已知约束与假设

1. **`macos-13` is retired.** GitHub shut the macOS 13 Intel image down in
   September 2025 ([changelog](https://github.blog/changelog/2025-09-19-github-actions-macos-13-runner-image-is-closing-down/)).
   The required `macos-13` row is kept in the matrix but marked
   `experimental: true`; `macos-15-intel` supplies the supported Intel coverage.
   **`macos-13` 已下线。** GitHub 于 2025 年 9 月关闭了 macOS 13 Intel 镜像。
   矩阵保留必需的 `macos-13` 行但标记为 `experimental: true`；Intel 覆盖由
   `macos-15-intel` 提供。
2. **`windows-latest` now means `windows-2025` with Visual Studio 2026**
   ([changelog](https://github.blog/changelog/2026-05-14-github-actions-upcoming-image-migrations/)).
   The MSVC jobs request the `Visual Studio 17 2022` generator by name, which
   remains installed; pin `windows-2022` if a future image drops it.
   **`windows-latest` 现指带 Visual Studio 2026 的 `windows-2025`。** MSVC 作业
   按名称请求 `Visual Studio 17 2022` 生成器，该生成器仍然存在；若将来镜像移除
   它，请改为固定 `windows-2022`。
3. **No 32-bit MSVC Qt 6 desktop kit exists**, so x86 uses `win32_mingw` + Ninja.
   **不存在 32 位 MSVC Qt 6 桌面套件**，因此 x86 使用 `win32_mingw` + Ninja。
4. **ARM64 jobs are experimental where the Qt kit may be absent.** They never
   block the matrix (`continue-on-error`) but always report.
   **ARM64 作业在 Qt 套件可能缺失时视为试验性。** 它们不会阻塞矩阵，但始终上报结果。
5. **Linux ARM64 runners are free only for public repositories.**
   **Linux ARM64 运行器仅在公开仓库免费。**
6. **Deployment relies on `qt_generate_deploy_qml_app_script` first**, with
   `windeployqt` / `macdeployqt` / linuxdeploy as the second pass.
   **部署以 `qt_generate_deploy_qml_app_script` 为主**，`windeployqt` /
   `macdeployqt` / linuxdeploy 作为第二道处理。
7. **Code signing is not configured.** macOS builds are ad-hoc signed only;
   Windows installers are unsigned. Users will see SmartScreen/Gatekeeper
   warnings until certificates are added as repository secrets.
   **未配置代码签名。** macOS 构建仅做临时签名；Windows 安装包未签名。在把证书加入
   仓库 Secrets 之前，用户会看到 SmartScreen/Gatekeeper 警告。
