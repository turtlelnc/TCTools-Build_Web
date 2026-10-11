#!/usr/bin/env bash
# ============================================================================
#  build.sh -- local POSIX (Linux/macOS) build helper for TCTools-Build_Web
#  build.sh —— TCTools-Build_Web 的 POSIX（Linux/macOS）本地构建助手
#
#  Wraps CMake configure + build + ctest, plus optional install and the
#  platform packaging scripts (macOS .app/.dmg, Linux .deb/AppImage).
#  封装 CMake 配置 + 构建 + ctest，并可选执行安装与平台打包脚本
#  （macOS 的 .app/.dmg、Linux 的 .deb/AppImage）。
#
#  Usage / 用法:
#    ./scripts_Scripts_脚本/build.sh
#    ./scripts_Scripts_脚本/build.sh --build-type Debug --clean
#    ./scripts_Scripts_脚本/build.sh --install --prefix "$HOME/tcbw"
#    QT_ROOT_DIR=/opt/Qt/6.8.3/gcc_64 ./scripts_Scripts_脚本/build.sh --package deb
# ============================================================================
set -euo pipefail

# --- Defaults 默认值 ---------------------------------------------------------
BUILD_TYPE="Release"
BUILD_DIR="build"
QT_DIR="${QT_ROOT_DIR:-}"
INSTALL_PREFIX=""
PACKAGE=""           # "" | deb | dmg | appimage / 空 | deb | dmg | appimage
DO_CLEAN=0
DO_INSTALL=0
DO_CONFIGURE=1
DO_TEST=1
WEBENGINE=ON
TESTS=ON
WARNINGS_AS_ERRORS=OFF
JOBS=""
GENERATOR="Ninja"
ARCH=""              # macOS only / 仅 macOS

# --- Argument parsing 参数解析 ----------------------------------------------
usage() {
    cat <<'EOF'
Usage: build.sh [options]

  --build-type <t>      Debug | Release | RelWithDebInfo | MinSizeRel  (Release)
  --build-dir <dir>     Build directory                                (build)
  --qt-dir <path>       Qt kit root                     (default: $QT_ROOT_DIR)
  --generator <gen>     CMake generator                                (Ninja)
  --arch <archs>        macOS architectures, e.g. 'arm64' or 'arm64;x86_64'
  --jobs <n>            Parallel build jobs                 (default: nproc)
  --clean               Delete the build directory first
  --no-configure        Skip the configure step
  --no-tests            Configure with -DTCBW_BUILD_TESTS=OFF and skip ctest
  --webengine <on|off>  Toggle -DTCBW_ENABLE_WEBENGINE                 (on)
  --werror              Pass -DTCBW_WARNINGS_AS_ERRORS=ON
  --install             Run `cmake --install`
  --prefix <dir>        Install prefix for --install
  --package <kind>      deb | dmg | appimage -- run the packaging script
  -h | --help           Show this help

  --build-type <类型>   构建类型                                       (Release)
  --build-dir <目录>    构建目录                                       (build)
  --qt-dir <路径>       Qt 套件根目录                     （默认 $QT_ROOT_DIR）
  --generator <生成器>  CMake 生成器                                   (Ninja)
  --arch <架构>         macOS 架构，例如 'arm64' 或 'arm64;x86_64'
  --jobs <n>            并行构建任务数                        （默认 nproc）
  --clean               先删除构建目录
  --no-configure        跳过配置步骤
  --no-tests            以 -DTCBW_BUILD_TESTS=OFF 配置并跳过 ctest
  --webengine <on|off>  切换 -DTCBW_ENABLE_WEBENGINE                   (on)
  --werror              传入 -DTCBW_WARNINGS_AS_ERRORS=ON
  --install             执行 `cmake --install`
  --prefix <目录>       --install 使用的安装前缀
  --package <类型>      deb | dmg | appimage —— 运行对应打包脚本
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-type)     BUILD_TYPE="${2:?--build-type needs a value}";    shift 2 ;;
        --build-dir)      BUILD_DIR="${2:?--build-dir needs a value}";      shift 2 ;;
        --qt-dir)         QT_DIR="${2:?--qt-dir needs a value}";            shift 2 ;;
        --generator)      GENERATOR="${2:?--generator needs a value}";      shift 2 ;;
        --arch)           ARCH="${2:?--arch needs a value}";                shift 2 ;;
        --jobs)           JOBS="${2:?--jobs needs a value}";                shift 2 ;;
        --clean)          DO_CLEAN=1; shift ;;
        --no-configure)   DO_CONFIGURE=0; shift ;;
        --no-tests)       DO_TEST=0; TESTS=OFF; shift ;;
        --webengine)      WEBENGINE="${2:?--webengine needs on|off}";       shift 2 ;;
        --werror)         WARNINGS_AS_ERRORS=ON; shift ;;
        --install)        DO_INSTALL=1; shift ;;
        --prefix)         INSTALL_PREFIX="${2:?--prefix needs a value}";    shift 2 ;;
        --package)        PACKAGE="${2:?--package needs a value}";          shift 2 ;;
        -h|--help)        usage; exit 0 ;;
        *) echo "Unknown option 未知参数: $1" >&2; usage >&2; exit 2 ;;
    esac
done

log()  { printf '\033[36m==> %s\033[0m\n' "$*"; }
info() { printf '    %s\n' "$*"; }
warn() { printf '\033[33m[warn] %s\033[0m\n' "$*" >&2; }
die()  { printf '\033[31m[error] %s\033[0m\n' "$*" >&2; exit 1; }

# ----------------------------------------------------------------------------
#  0. Repository root 仓库根目录
# ----------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
[[ -f "$REPO_ROOT/CMakeLists.txt" ]] ||
    die "CMakeLists.txt not found under '$REPO_ROOT'. Is scripts_Scripts_脚本/ in the repository root?"
cd "$REPO_ROOT"

[[ "$BUILD_DIR" == /* ]] || BUILD_DIR="$REPO_ROOT/$BUILD_DIR"

OS_NAME="$(uname -s)"
if [[ -z "$JOBS" ]]; then
    # Prefer nproc (Linux/coreutils), then sysctl (macOS), then a safe default.
    # 优先 nproc（Linux/coreutils），其次 sysctl（macOS），最后使用安全默认值。
    if command -v nproc >/dev/null 2>&1; then
        JOBS="$(nproc)"
    elif command -v sysctl >/dev/null 2>&1; then
        JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
    else
        JOBS=4
    fi
fi

log "TCTools-Build_Web local build / 本地构建"
info "Repo 仓库      : $REPO_ROOT"
info "Build dir      : $BUILD_DIR"
info "Build type     : $BUILD_TYPE"
info "Generator      : $GENERATOR"
info "Jobs           : $JOBS"
info "Qt dir         : ${QT_DIR:-<unset 未设置>}"
info "Tests          : $TESTS"
info "WebEngine      : $WEBENGINE"

# ----------------------------------------------------------------------------
#  1. Validate the toolchain 校验工具链
# ----------------------------------------------------------------------------
log 'Validating toolchain / 校验工具链'
command -v cmake >/dev/null 2>&1 || die 'cmake was not found on PATH. Install CMake 3.24+ .'
info "cmake   : $(command -v cmake)"
info "version : $(cmake --version | head -n 1)"

if [[ -z "$QT_DIR" ]]; then
    # Fall back to a Qt that is already discoverable by CMake, and only warn:
    # a system Qt install needs no explicit prefix.
    # 回退到 CMake 已能发现的 Qt，且仅告警：系统级 Qt 安装无需显式前缀。
    warn 'QT_ROOT_DIR / --qt-dir not set; relying on CMake to find Qt 6.'
elif [[ ! -d "$QT_DIR/lib/cmake" ]]; then
    warn "No lib/cmake under QT_DIR='$QT_DIR'; CMake may fail to find Qt 6."
fi

if [[ "$GENERATOR" == 'Ninja' ]] && ! command -v ninja >/dev/null 2>&1; then
    die 'ninja was not found on PATH. Install ninja-build or pass --generator.'
fi

# ----------------------------------------------------------------------------
#  2. Configure 配置
# ----------------------------------------------------------------------------
CONFIGURE_ARGS=(
    -S .
    -B "$BUILD_DIR"
    -G "$GENERATOR"
    "-DTCBW_BUILD_TESTS=$TESTS"
    "-DTCBW_ENABLE_WEBENGINE=$WEBENGINE"
    "-DTCBW_WARNINGS_AS_ERRORS=$WARNINGS_AS_ERRORS"
)

if [[ -n "$QT_DIR" ]]; then
    CONFIGURE_ARGS+=("-DCMAKE_PREFIX_PATH=$QT_DIR")
fi
if [[ -n "$INSTALL_PREFIX" ]]; then
    CONFIGURE_ARGS+=("-DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX")
fi

# Ninja is single-config; Visual Studio/Xcode ignore CMAKE_BUILD_TYPE.
# Ninja 是单配置生成器；Visual Studio/Xcode 会忽略 CMAKE_BUILD_TYPE。
case "$GENERATOR" in
    Ninja*|Unix\ Makefiles*|"MinGW Makefiles")
        CONFIGURE_ARGS+=("-DCMAKE_BUILD_TYPE=$BUILD_TYPE")
        ;;
esac

if [[ "$OS_NAME" == 'Darwin' && -n "$ARCH" ]]; then
    CONFIGURE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=$ARCH")
    CONFIGURE_ARGS+=("-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0")
fi

if [[ "$DO_CLEAN" -eq 1 && -d "$BUILD_DIR" ]]; then
    log 'Removing the previous build tree / 删除旧的构建目录'
    info "rm -rf $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

if [[ "$DO_CONFIGURE" -eq 1 ]]; then
    log 'Configuring / 配置'
    cmake "${CONFIGURE_ARGS[@]}"
else
    log 'Skipping configure (--no-configure) / 跳过配置（--no-configure）'
fi

# ----------------------------------------------------------------------------
#  3. Build 构建
# ----------------------------------------------------------------------------
log 'Building / 构建'
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel "$JOBS"

# ----------------------------------------------------------------------------
#  4. Test 测试
# ----------------------------------------------------------------------------
if [[ "$DO_TEST" -eq 1 ]]; then
    log 'Running tests / 运行测试'
    # `-C <config>` is required by multi-config generators and harmless otherwise.
    # 多配置生成器需要 `-C <配置>`，其余情况无害。
    #
    # A headless Linux CI box has no display; offscreen keeps Qt GUI tests happy.
    # 无显示器的 Linux CI 环境需要 offscreen 平台，Qt GUI 测试才能运行。
    if [[ "$OS_NAME" == 'Linux' && -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
        info 'No display detected; using QT_QPA_PLATFORM=offscreen / 未检测到显示器，使用 offscreen'
        QT_QPA_PLATFORM=offscreen ctest --test-dir "$BUILD_DIR" -C "$BUILD_TYPE" --output-on-failure
    else
        ctest --test-dir "$BUILD_DIR" -C "$BUILD_TYPE" --output-on-failure
    fi
else
    log 'Tests disabled (--no-tests) / 测试已禁用（--no-tests）'
fi

# ----------------------------------------------------------------------------
#  5. Install 安装
# ----------------------------------------------------------------------------
if [[ "$DO_INSTALL" -eq 1 ]]; then
    log 'Installing / 安装'
    INSTALL_ARGS=(--install "$BUILD_DIR" --config "$BUILD_TYPE")
    if [[ -n "$INSTALL_PREFIX" ]]; then
        INSTALL_ARGS+=(--prefix "$INSTALL_PREFIX")
        # AppImage/deb layouts are staged separately; tell the user where things went.
        # AppImage/deb 布局另行暂存；这里告知用户产物位置。
        INSTALL_DIR="$INSTALL_PREFIX"
    else
        INSTALL_DIR="$BUILD_DIR/install"
    fi
    cmake "${INSTALL_ARGS[@]}"
    info "Installed to 已安装到: $INSTALL_DIR"
fi

# ----------------------------------------------------------------------------
#  6. Package 打包
# ----------------------------------------------------------------------------
if [[ -n "$PACKAGE" ]]; then
    log "Packaging ($PACKAGE) / 打包（$PACKAGE）"
    VERSION_STRING="${TCBW_VERSION:-0.1.0-rc1}"
    OUTPUT_DIR="$REPO_ROOT/dist"

    case "$PACKAGE" in
        deb)
            [[ "$OS_NAME" == 'Linux' ]] || die '--package deb is Linux-only / --package deb 仅限 Linux'
            bash "$REPO_ROOT/packaging_Packaging_打包/linux_Linux/deploy_linux.sh" \
                --build-dir "$BUILD_DIR" \
                --output-dir "$OUTPUT_DIR" \
                --version "$VERSION_STRING"
            ;;
        appimage)
            [[ "$OS_NAME" == 'Linux' ]] || die '--package appimage is Linux-only / --package appimage 仅限 Linux'
            bash "$REPO_ROOT/packaging_Packaging_打包/linux_Linux/build_appimage.sh" \
                --build-dir "$BUILD_DIR" \
                --output-dir "$OUTPUT_DIR" \
                --qt-dir "$QT_DIR" \
                --version "$VERSION_STRING"
            ;;
        dmg)
            [[ "$OS_NAME" == 'Darwin' ]] || die '--package dmg is macOS-only / --package dmg 仅限 macOS'
            APP_BUNDLE="$(find "$BUILD_DIR" -maxdepth 3 -name 'TCTools-Build_Web.app' -print -quit)"
            [[ -n "$APP_BUNDLE" ]] || die "No TCTools-Build_Web.app found under '$BUILD_DIR'."
            bash "$REPO_ROOT/packaging_Packaging_打包/macos_macOS/deploy_macos.sh" \
                --app "$APP_BUNDLE" \
                --arch "${ARCH:-$(uname -m)}" \
                --qt-dir "$QT_DIR" \
                --version "$VERSION_STRING"
            bash "$REPO_ROOT/packaging_Packaging_打包/macos_macOS/create_dmg.sh" \
                --app "$APP_BUNDLE" \
                --version "$VERSION_STRING" \
                --arch "${ARCH:-$(uname -m)}" \
                --output-dir "$OUTPUT_DIR"
            ;;
        *)
            die "Unknown --package '$PACKAGE'. Use deb, dmg or appimage. / 未知的 --package 取值。"
            ;;
    esac
fi

# ----------------------------------------------------------------------------
#  7. Summary 汇总
# ----------------------------------------------------------------------------
log 'Done / 完成'
EXECUTABLE="$(find "$BUILD_DIR" -maxdepth 3 -type f -name 'TCTools-Build_Web' -print -quit 2>/dev/null || true)"
if [[ -n "$EXECUTABLE" ]]; then
    info "Executable 可执行文件: $EXECUTABLE"
else
    info "No executable found under '$BUILD_DIR' yet."
fi
echo ''
echo 'Next steps / 后续步骤:'
echo "    Run  运行   : \"$BUILD_DIR/TCTools-Build_Web\""
echo "    Test 测试   : ctest --test-dir \"$BUILD_DIR\" --output-on-failure"
echo "    Package 打包: $SCRIPT_DIR/build.sh --package deb|dmg|appimage"
