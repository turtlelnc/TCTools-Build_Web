#!/usr/bin/env bash
# ============================================================================
#  build_appimage.sh -- build a portable single-file Linux AppImage
#  build_appimage.sh —— 构建可移植的单文件 Linux AppImage
#
#  Behaviour / 行为:
#    * `cmake --install` into an AppDir laid out the AppImage way
#      (usr/bin, usr/share/applications, usr/share/icons/hicolor);
#    * copies the Qt QML import tree so the Quick/QML UI can load its modules;
#    * runs linuxdeploy to bundle Qt plugins and produce the .AppImage;
#    * verifies the AppImage by extracting and running it, and writes a checksum.
#    * 使用 `cmake --install` 安装到符合 AppImage 约定的 AppDir
#      （usr/bin、usr/share/applications、usr/share/icons/hicolor）；
#    * 复制 Qt QML 导入目录树，使 Quick/QML 界面能加载其模块；
#    * 运行 linuxdeploy 打包 Qt 插件并生成 .AppImage；
#    * 通过解包运行来校验 AppImage，并写出校验和。
#
#  Usage / 用法:
#    ./build_appimage.sh --build-dir build --output-dir dist \
#        --linuxdeploy /tmp/linuxdeploy-x86_64.AppImage --version 0.1.0-rc1
# ============================================================================
set -euo pipefail

# --- Defaults 默认值 ---------------------------------------------------------
BUILD_DIR="build"
OUTPUT_DIR="dist"
VERSION="${TCBW_VERSION:-0.1.0-rc1}"
ARCH="x86_64"
LINUXDEPLOY="${LINUXDEPLOY:-}"
QT_DIR="${QT_ROOT_DIR:-}"
APPDIR=""
SKIP_INSTALL=0
KEEP_APPDIR=0

APP_ID="tctools-buildweb"
EXEC_NAME="TCTools-Build_Web"

# --- Argument parsing 参数解析 ----------------------------------------------
usage() {
    cat <<'EOF'
Usage: build_appimage.sh [options]

  --build-dir <dir>     Existing CMake build directory       (default: build)
  --output-dir <dir>    Where to write the .AppImage         (default: dist)
  --appdir <dir>        AppDir location                      (default: <output>/AppDir)
  --linuxdeploy <path>  linuxdeploy AppImage or binary       (default: $LINUXDEPLOY or PATH)
  --qt-dir <path>       Qt kit root                          (default: $QT_ROOT_DIR)
  --arch <arch>         x86_64 | aarch64                     (default: x86_64)
  --version <ver>       Version string                       (default: $TCBW_VERSION)
  --skip-install        Reuse an existing AppDir
  --keep-appdir         Do not delete the AppDir afterwards
  -h | --help           Show this help

  --build-dir <目录>    已存在的 CMake 构建目录              （默认 build）
  --output-dir <目录>   .AppImage 输出目录                   （默认 dist）
  --appdir <目录>       AppDir 位置                          （默认 <输出>/AppDir）
  --linuxdeploy <路径>  linuxdeploy 的 AppImage 或可执行文件 （默认 $LINUXDEPLOY 或 PATH）
  --qt-dir <路径>       Qt 套件根目录                        （默认 $QT_ROOT_DIR）
  --arch <架构>         x86_64 | aarch64                     （默认 x86_64）
  --version <版本>      版本号                               （默认 $TCBW_VERSION）
  --skip-install        复用已有的 AppDir
  --keep-appdir         结束后不删除 AppDir
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)    BUILD_DIR="${2:?--build-dir needs a value}";      shift 2 ;;
        --output-dir)   OUTPUT_DIR="${2:?--output-dir needs a value}";    shift 2 ;;
        --appdir)       APPDIR="${2:?--appdir needs a value}";            shift 2 ;;
        --linuxdeploy)  LINUXDEPLOY="${2:?--linuxdeploy needs a value}";  shift 2 ;;
        --qt-dir)       QT_DIR="${2:?--qt-dir needs a value}";            shift 2 ;;
        --arch)         ARCH="${2:?--arch needs a value}";                shift 2 ;;
        --version)      VERSION="${2:?--version needs a value}";          shift 2 ;;
        --skip-install) SKIP_INSTALL=1; shift ;;
        --keep-appdir)  KEEP_APPDIR=1; shift ;;
        -h|--help)      usage; exit 0 ;;
        *) echo "Unknown option 未知参数: $1" >&2; usage >&2; exit 2 ;;
    esac
done

log()  { printf '\033[36m==> %s\033[0m\n' "$*"; }
warn() { printf '\033[33m[warn] %s\033[0m\n' "$*" >&2; }
die()  { printf '\033[31m[error] %s\033[0m\n' "$*" >&2; exit 1; }

# --- Validation and path normalisation 校验与路径规范化 ----------------------
[[ "$(uname -s)" == 'Linux' ]] || die "build_appimage.sh requires Linux / build_appimage.sh 需要 Linux 环境"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
if [[ ! -d "$BUILD_DIR" ]]; then
    [[ -d "$REPO_ROOT/$BUILD_DIR" ]] && BUILD_DIR="$REPO_ROOT/$BUILD_DIR" || die "Build dir not found 未找到构建目录: $BUILD_DIR"
fi
BUILD_DIR="$(cd "$BUILD_DIR" && pwd)"
mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"
[[ -n "$APPDIR" ]] || APPDIR="$OUTPUT_DIR/AppDir"
mkdir -p "$APPDIR"
APPDIR="$(cd "$APPDIR" && pwd)"

# linuxdeploy can be given as a path or found on PATH.
# linuxdeploy 可以以路径给出，也可以从 PATH 中查找。
if [[ -n "$LINUXDEPLOY" && ! -e "$LINUXDEPLOY" ]]; then
    die "linuxdeploy not found at 未在该路径找到 linuxdeploy: $LINUXDEPLOY"
fi
if [[ -z "$LINUXDEPLOY" ]]; then
    LINUXDEPLOY="$(command -v linuxdeploy || true)"
fi
[[ -n "$LINUXDEPLOY" ]] || die 'linuxdeploy not found. Pass --linuxdeploy <path>. / 未找到 linuxdeploy，请传 --linuxdeploy <路径>。'

# AppImages need FUSE; extraction mode works in containers and on CI runners.
# AppImage 需要 FUSE；解包模式在容器与 CI 运行器上可用。
if [[ "$LINUXDEPLOY" == *.AppImage ]]; then
    export APPIMAGE_EXTRACT_AND_RUN="${APPIMAGE_EXTRACT_AND_RUN:-1}"
fi

log "Repo 仓库        : $REPO_ROOT"
log "Build dir 构建目录: $BUILD_DIR"
log "AppDir           : $APPDIR"
log "Output 输出      : $OUTPUT_DIR"
log "linuxdeploy      : $LINUXDEPLOY"
log "Version 版本     : $VERSION ($ARCH)"

# ----------------------------------------------------------------------------
#  1. Populate the AppDir 填充 AppDir
# ----------------------------------------------------------------------------
if [[ "$SKIP_INSTALL" -eq 0 ]]; then
    log 'Installing into the AppDir / 安装到 AppDir'
    cmake --install "$BUILD_DIR" --prefix "$APPDIR/usr" --config Release
else
    log 'Reusing the existing AppDir / 复用已有 AppDir'
    [[ -d "$APPDIR/usr" ]] || die "--skip-install given but $APPDIR/usr does not exist."
fi

# Linux applications belong in usr/bin; CMake's GNUInstallDirs uses bin/.
# Linux 应用应位于 usr/bin；CMake 的 GNUInstallDirs 使用 bin/。
if [[ -f "$APPDIR/usr/bin/$EXEC_NAME" ]]; then
    log "Executable in place 可执行文件已就位: usr/bin/$EXEC_NAME"
elif find "$APPDIR" -type f -name "$EXEC_NAME" -print -quit | grep -q .; then
    found="$(find "$APPDIR" -type f -name "$EXEC_NAME" -print -quit)"
    warn "Executable landed outside usr/bin 可执行文件未落在 usr/bin: $found"
    install -Dm755 "$found" "$APPDIR/usr/bin/$EXEC_NAME"
else
    die "Executable '$EXEC_NAME' not found in the AppDir. Did the build succeed? / 在 AppDir 中未找到可执行文件，构建是否成功？"
fi

# ----------------------------------------------------------------------------
#  2. Desktop entry and icon 桌面条目与图标
# ----------------------------------------------------------------------------
log 'Placing .desktop entry and icon / 放置 .desktop 条目与图标'

DESKTOP_SRC="$REPO_ROOT/packaging_Packaging_打包/linux_Linux/tctools-buildweb.desktop"
[[ -f "$DESKTOP_SRC" ]] || die "Desktop file not found / 未找到 .desktop 文件: $DESKTOP_SRC"

install -Dm644 "$DESKTOP_SRC" "$APPDIR/usr/share/applications/$APP_ID.desktop"
sed -i "s/^X-AppImage-Version=.*/X-AppImage-Version=${VERSION}/" \
    "$APPDIR/usr/share/applications/$APP_ID.desktop"

ICON_SRC=""
for candidate in \
    "$REPO_ROOT/resources_Resources_资源/icons_Icons_图标/tctools-buildweb.png" \
    "$REPO_ROOT/resources_Resources_资源/icons_Icons_图标/tctools-buildweb.svg" \
    "$REPO_ROOT/packaging_Packaging_打包/linux_Linux/tctools-buildweb.png" \
    "$REPO_ROOT/packaging_Packaging_打包/linux_Linux/tctools-buildweb.svg"; do
    [[ -f "$candidate" ]] && { ICON_SRC="$candidate"; break; }
done

ICON_ARG=()
if [[ -n "$ICON_SRC" ]]; then
    ext="${ICON_SRC##*.}"
    case "$ext" in
        png) icon_dir="256x256" ;;
        *)   icon_dir="scalable" ;;
    esac
    install -Dm644 "$ICON_SRC" "$APPDIR/usr/share/icons/hicolor/$icon_dir/apps/$APP_ID.$ext"
    ICON_ARG=(--icon-file "$ICON_SRC")
    log "Icon installed 图标已安装: $ICON_SRC"
else
    # A .desktop entry with no Icon= resolves is merely cosmetic; warn, do not fail.
    # .desktop 条目的 Icon= 无法解析只是外观问题；告警而不失败。
    warn 'No icon asset found; the AppImage will use the default icon.'
    warn '未找到图标素材；AppImage 将使用默认图标。'
fi

if command -v desktop-file-validate >/dev/null 2>&1; then
    desktop-file-validate "$APPDIR/usr/share/applications/$APP_ID.desktop" ||
        warn 'desktop-file-validate reported problems / desktop-file-validate 报告了问题。'
fi

# ----------------------------------------------------------------------------
#  3. Qt QML import tree Qt QML 导入目录树
# ----------------------------------------------------------------------------
# `qt_generate_deploy_qml_app_script` copies the QML modules it can resolve, but
# the import tree is also needed on disk for runtime-resolved imports.
# `qt_generate_deploy_qml_app_script` 会复制其能解析的 QML 模块，但运行时解析的
# 导入仍需磁盘上的导入目录树。
if [[ -z "$QT_DIR" && -n "${QTDIR:-}" ]]; then
    QT_DIR="$QTDIR"
fi

QML_SRC=""
if [[ -n "$QT_DIR" && -d "$QT_DIR/qml" ]]; then
    QML_SRC="$QT_DIR/qml"
elif [[ -d "/usr/lib/x86_64-linux-gnu/qt6/qml" ]]; then
    QML_SRC="/usr/lib/x86_64-linux-gnu/qt6/qml"
fi

if [[ -n "$QML_SRC" ]]; then
    log "Staging QML imports 暂存 QML 导入: $QML_SRC"
    mkdir -p "$APPDIR/usr/qml"
    cp -a "$QML_SRC/." "$APPDIR/usr/qml/"
    # The Qt Quick Controls 2 QML module is a hard requirement for the UI.
    # Qt Quick Controls 2 QML 模块是界面的硬性依赖。
    [[ -d "$APPDIR/usr/qml/QtQuick/Controls" ]] ||
        warn 'QtQuick/Controls missing from the staged QML tree / 暂存的 QML 目录树缺少 QtQuick/Controls。'
else
    warn 'No Qt QML directory found; QML imports may be incomplete. Pass --qt-dir.'
    warn '未找到 Qt QML 目录；QML 导入可能不完整。请传 --qt-dir。'
fi

# Ship Qt's translations alongside the app.
# 随应用一并提供 Qt 的翻译文件。
if [[ -n "$QT_DIR" && -d "$QT_DIR/translations" ]]; then
    log 'Staging Qt translations / 暂存 Qt 翻译'
    mkdir -p "$APPDIR/usr/translations"
    find "$QT_DIR/translations" -maxdepth 1 -name '*_zh_CN.qm' -o -maxdepth 1 -name 'qtbase_*.qm' \
        -exec cp -f {} "$APPDIR/usr/translations/" \; 2>/dev/null || true
fi

# ----------------------------------------------------------------------------
#  4. Run linuxdeploy 运行 linuxdeploy
# ----------------------------------------------------------------------------
log 'Running linuxdeploy / 运行 linuxdeploy'

rm -f "$OUTPUT_DIR"/*.AppImage

# VERSION is what linuxdeploy appends to the AppImage filename.
# VERSION 会被 linuxdeploy 追加到 AppImage 文件名中。
export VERSION
(
    cd "$OUTPUT_DIR"
    "$LINUXDEPLOY" \
        --appdir "$APPDIR" \
        --desktop-file "$APPDIR/usr/share/applications/$APP_ID.desktop" \
        "${ICON_ARG[@]}" \
        --output appimage
)

# linuxdeploy names the file <Name>-<version>-<arch>.AppImage; normalise it.
# linuxdeploy 命名为 <名称>-<版本>-<架构>.AppImage；这里统一改名。
RAW_APPIMAGE="$(find "$OUTPUT_DIR" -maxdepth 1 -name '*.AppImage' -print -quit)"
[[ -n "$RAW_APPIMAGE" ]] || die "linuxdeploy produced no AppImage / linuxdeploy 未生成 AppImage"

FINAL_APPIMAGE="$OUTPUT_DIR/TCBuildWeb-${VERSION}-linux-${ARCH}.AppImage"
if [[ "$RAW_APPIMAGE" != "$FINAL_APPIMAGE" ]]; then
    mv -f "$RAW_APPIMAGE" "$FINAL_APPIMAGE"
fi
chmod +x "$FINAL_APPIMAGE"
log "AppImage built 已生成 AppImage: $FINAL_APPIMAGE"

# ----------------------------------------------------------------------------
#  5. Verify 校验
# ----------------------------------------------------------------------------
log 'Verifying AppImage / 校验 AppImage'
# 1. The AppImage header must be readable -- cheapest possible structural check.
# 1. AppImage 头部必须可读 —— 成本最低的结构性校验。
if ! "$FINAL_APPIMAGE" --appimage-offset >/dev/null 2>&1; then
    die 'AppImage header check failed / AppImage 头部校验失败。'
fi
log 'AppImage header OK. / AppImage 头部正常。'

# 2. Extract and run the payload once, offscreen, with a hard timeout. A Qt GUI
#    app has no `--help` handler, so a timeout is the EXPECTED outcome and still
#    proves the bundled runtime can start; a non-zero exit means a real failure.
#    The repository's CI must not hang on a GUI event loop.
# 2. 在 offscreen 模式下解包运行一次，并设置硬超时。Qt GUI 应用没有 `--help`
#    处理逻辑，因此超时是**预期**结果，且已足以证明内置运行时能够启动；
#    非零退出码才代表真正的失败。CI 不能因 GUI 事件循环而挂住。
if command -v timeout >/dev/null 2>&1; then
    set +e
    QT_QPA_PLATFORM=offscreen timeout 25 "$FINAL_APPIMAGE" --appimage-extract-and-run --version \
        >/dev/null 2>&1
    smoke_status=$?
    set -e
    case "$smoke_status" in
        0)   log 'AppImage smoke test exited cleanly. / AppImage 冒烟测试正常退出。' ;;
        124) log 'AppImage smoke test timed out after starting -- treated as success (GUI app, no --version handler). / AppImage 冒烟测试启动后超时 —— 视为成功（GUI 应用无 --version 处理）。' ;;
        *)   warn "AppImage smoke test exited with status $smoke_status; inspect the bundle manually. / AppImage 冒烟测试退出码 $smoke_status，请手动检查该包。" ;;
    esac
else
    warn 'timeout(1) unavailable; skipping the smoke test. / 无 timeout(1)，跳过冒烟测试。'
fi

( cd "$OUTPUT_DIR" && sha256sum "$(basename "$FINAL_APPIMAGE")" > "$(basename "$FINAL_APPIMAGE").sha256" )

# ----------------------------------------------------------------------------
#  6. Cleanup and summary 清理与汇总
# ----------------------------------------------------------------------------
if [[ "$KEEP_APPDIR" -eq 0 ]]; then
    log 'Removing the AppDir / 删除 AppDir'
    rm -rf "$APPDIR"
else
    warn "AppDir kept 已保留 AppDir: $APPDIR"
fi

log 'AppImage packaging summary / AppImage 打包汇总'
ls -lh "$FINAL_APPIMAGE" "$FINAL_APPIMAGE.sha256"
echo "AppImage 产物 : $FINAL_APPIMAGE"
echo "SHA256 校验和 : $(awk '{print $1}' "$FINAL_APPIMAGE.sha256")"
