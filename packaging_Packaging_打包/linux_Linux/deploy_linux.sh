#!/usr/bin/env bash
# ============================================================================
#  deploy_linux.sh -- stage a Linux install tree and build a .deb package
#  deploy_linux.sh —— 生成 Linux 安装目录树并构建 .deb 包
#
#  Behaviour / 行为:
#    * `cmake --install` into a staging prefix that mirrors the real filesystem;
#    * drops the .desktop entry, the AppStream metainfo and the icons in place;
#    * builds a Debian package with dpkg-deb using debian_control/control as the
#      template, rewriting Version/Architecture/Installed-Size at build time.
#    * 使用 `cmake --install` 安装到镜像真实文件系统的分阶段前缀；
#    * 就位 .desktop 条目、AppStream 元信息与图标；
#    * 以 debian_control/control 为模板，用 dpkg-deb 构建 Debian 包，并在构建时
#      重写 Version/Architecture/Installed-Size。
#
#  Usage / 用法:
#    ./deploy_linux.sh --build-dir build --output-dir dist --version 0.1.0-rc1
#    ./deploy_linux.sh --build-dir build --output-dir dist --no-deb
# ============================================================================
set -euo pipefail

# --- Defaults 默认值 ---------------------------------------------------------
BUILD_DIR="build"
OUTPUT_DIR="dist"
VERSION="${TCBW_VERSION:-0.1.0-rc1}"
ARCH="amd64"
STAGE_DIR=""
BUILD_DEB=1
BUILD_TARBALL=1
SKIP_INSTALL=0
MAINTAINER="turtlelnc <turtlelnc@users.noreply.github.com>"

# --- Argument parsing 参数解析 ----------------------------------------------
usage() {
    cat <<'EOF'
Usage: deploy_linux.sh [options]

  --build-dir <dir>    Existing CMake build directory        (default: build)
  --output-dir <dir>   Where to write packages               (default: dist)
  --stage-dir <dir>    Where to stage the install tree       (default: <output>/stage)
  --version <ver>      Version string                        (default: $TCBW_VERSION)
  --arch <arch>        amd64 | arm64                         (default: amd64)
  --maintainer <who>   Debian Maintainer field               (default: turtlelnc)
  --no-deb             Skip .deb generation
  --no-tarball         Skip .tar.gz generation
  --skip-install       Reuse an existing --stage-dir without reinstalling
  -h | --help          Show this help

  --build-dir <目录>   已存在的 CMake 构建目录               （默认 build）
  --output-dir <目录>  包输出目录                            （默认 dist）
  --stage-dir <目录>   安装树暂存目录                        （默认 <输出>/stage）
  --version <版本>     版本号                                （默认 $TCBW_VERSION）
  --arch <架构>        amd64 | arm64                         （默认 amd64）
  --no-deb             跳过 .deb 生成
  --no-tarball         跳过 .tar.gz 生成
  --skip-install       复用已有 --stage-dir，不重新安装
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)   BUILD_DIR="${2:?--build-dir needs a value}";    shift 2 ;;
        --output-dir)  OUTPUT_DIR="${2:?--output-dir needs a value}";  shift 2 ;;
        --stage-dir)   STAGE_DIR="${2:?--stage-dir needs a value}";    shift 2 ;;
        --version)     VERSION="${2:?--version needs a value}";        shift 2 ;;
        --arch)        ARCH="${2:?--arch needs a value}";              shift 2 ;;
        --maintainer)  MAINTAINER="${2:?--maintainer needs a value}";  shift 2 ;;
        --no-deb)      BUILD_DEB=0; shift ;;
        --no-tarball)  BUILD_TARBALL=0; shift ;;
        --skip-install) SKIP_INSTALL=1; shift ;;
        -h|--help)     usage; exit 0 ;;
        *) echo "Unknown option 未知参数: $1" >&2; usage >&2; exit 2 ;;
    esac
done

log()  { printf '\033[36m==> %s\033[0m\n' "$*"; }
warn() { printf '\033[33m[warn] %s\033[0m\n' "$*" >&2; }
die()  { printf '\033[31m[error] %s\033[0m\n' "$*" >&2; exit 1; }

# Paths are made absolute up front: the staging tree is referenced after several
# `cd` operations, and the developer's checkout path contains non-ASCII bytes.
# 路径先转为绝对路径：暂存目录会在多次 `cd` 之后被引用，且开发机检出路含非 ASCII。
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
if [[ ! -d "$BUILD_DIR" ]]; then
    [[ -d "$REPO_ROOT/$BUILD_DIR" ]] && BUILD_DIR="$REPO_ROOT/$BUILD_DIR" || die "Build dir not found 未找到构建目录: $BUILD_DIR"
fi
BUILD_DIR="$(cd "$BUILD_DIR" && pwd)"
mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"
[[ -n "$STAGE_DIR" ]] || STAGE_DIR="$OUTPUT_DIR/stage"
mkdir -p "$STAGE_DIR"
STAGE_DIR="$(cd "$STAGE_DIR" && pwd)"

PKG_NAME="tctools-buildweb"
PKG_DIR="$STAGE_DIR/${PKG_NAME}_${VERSION}_${ARCH}"

log "Repo 仓库        : $REPO_ROOT"
log "Build dir 构建目录: $BUILD_DIR"
log "Output 输出      : $OUTPUT_DIR"
log "Stage 暂存       : $STAGE_DIR"
log "Version 版本     : $VERSION ($ARCH)"

# ----------------------------------------------------------------------------
#  1. cmake --install into a clean staging prefix
#     使用 cmake --install 安装到干净的分阶段前缀
# ----------------------------------------------------------------------------
if [[ "$SKIP_INSTALL" -eq 0 ]]; then
    log 'Installing into the staging prefix / 安装到分阶段前缀'
    rm -rf "$PKG_DIR"
    mkdir -p "$PKG_DIR/DEBIAN"
    # /usr is the Debian-correct prefix; the AppImage path uses a separate tree.
    # /usr 是 Debian 规范的前缀；AppImage 使用另一棵独立目录树。
    cmake --install "$BUILD_DIR" --prefix "$PKG_DIR/usr" --config Release
else
    log 'Reusing the existing staging tree / 复用已有暂存目录'
    [[ -d "$PKG_DIR/usr" ]] || die "--skip-install given but $PKG_DIR/usr does not exist."
fi

# ----------------------------------------------------------------------------
#  2. Desktop entry, AppStream metainfo and icons
#     .desktop 条目、AppStream 元信息与图标
# ----------------------------------------------------------------------------
log 'Installing .desktop entry and icons / 安装 .desktop 条目与图标'

DESKTOP_SRC="$REPO_ROOT/packaging_Packaging_打包/linux_Linux/tctools-buildweb.desktop"
[[ -f "$DESKTOP_SRC" ]] || die "Desktop file not found / 未找到 .desktop 文件: $DESKTOP_SRC"

install -Dm644 "$DESKTOP_SRC" \
    "$PKG_DIR/usr/share/applications/tctools-buildweb.desktop"

# Rewrite the AppImage version hint to the actual build version.
# 把 AppImage 版本提示改写为实际构建版本。
sed -i "s/^X-AppImage-Version=.*/X-AppImage-Version=${VERSION}/" \
    "$PKG_DIR/usr/share/applications/tctools-buildweb.desktop"

# Icons: accept a PNG/SVG shipped anywhere in the packaging tree so the packager
# is not blocked by a missing asset. Fall back to a generated placeholder only if
# nothing at all is available.
# 图标：接受打包目录中任意位置的 PNG/SVG，避免因缺少素材而阻塞打包流程。
# 只有在完全没有素材时才生成占位图标。
ICON_SRC=""
for candidate in \
    "$REPO_ROOT/resources_Resources_资源/icons_Icons_图标/tctools-buildweb.png" \
    "$REPO_ROOT/resources_Resources_资源/icons_Icons_图标/tctools-buildweb.svg" \
    "$REPO_ROOT/packaging_Packaging_打包/linux_Linux/tctools-buildweb.png" \
    "$REPO_ROOT/packaging_Packaging_打包/linux_Linux/tctools-buildweb.svg"; do
    [[ -f "$candidate" ]] && { ICON_SRC="$candidate"; break; }
done

if [[ -n "$ICON_SRC" ]]; then
    ext="${ICON_SRC##*.}"
    # PNG and SVG belong in different hicolor subdirectories; putting a PNG in
    # `scalable/` makes some icon caches drop it entirely.
    # PNG 与 SVG 分属不同的 hicolor 子目录；把 PNG 放进 `scalable/` 会导致部分
    # 图标缓存直接忽略它。
    case "$ext" in
        svg) icon_dir="$PKG_DIR/usr/share/icons/hicolor/scalable/apps" ;;
        png) icon_dir="$PKG_DIR/usr/share/icons/hicolor/256x256/apps" ;;
        *)   icon_dir="$PKG_DIR/usr/share/icons/hicolor/scalable/apps" ;;
    esac
    install -Dm644 "$ICON_SRC" "$icon_dir/tctools-buildweb.$ext"
    log "Icon installed 图标已安装: $ICON_SRC -> $icon_dir"
else
    warn 'No application icon found; shipping without an icon.'
    warn '未找到应用图标；将不带图标发布。'
fi

# AppStream metainfo improves visibility in software centres.
# AppStream 元信息可提升在软件中心中的可见度。
METAINFO_DIR="$PKG_DIR/usr/share/metainfo"
install -d "$METAINFO_DIR"
cat > "$METAINFO_DIR/com.turtlelnc.tctools.buildweb.metainfo.xml" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!-- Generated by deploy_linux.sh / 由 deploy_linux.sh 生成 -->
<component type="desktop-application">
  <id>com.turtlelnc.tctools.buildweb</id>
  <name>TCTools-Build_Web</name>
  <summary>Visual web page builder with Scratch-style block programming</summary>
  <summary xml:lang="zh_CN">带 Scratch 风格积木编程的可视化网页构建器</summary>
  <developer_name>turtlelnc</developer_name>
  <metadata_license>CC0-1.0</metadata_license>
  <project_license>MIT</project_license>
  <url type="homepage">https://github.com/turtlelnc/TCTools-Build_Web</url>
  <launchable type="desktop-id">tctools-buildweb.desktop</launchable>
  <releases>
    <release version="${VERSION}" date="$(date -u +%Y-%m-%d)"/>
  </releases>
  <content_rating type="oars-1.1"/>
</component>
EOF

# Validate the desktop entry when the tool is available.
# 若工具可用则校验 .desktop 条目。
if command -v desktop-file-validate >/dev/null 2>&1; then
    desktop-file-validate "$PKG_DIR/usr/share/applications/tctools-buildweb.desktop" ||
        warn 'desktop-file-validate reported problems / desktop-file-validate 报告了问题。'
fi

# ----------------------------------------------------------------------------
#  3. .deb control file -> dpkg-deb
#     .deb control 文件 -> dpkg-deb
# ----------------------------------------------------------------------------
DEB_PATH="$OUTPUT_DIR/${PKG_NAME}_${VERSION}_${ARCH}.deb"

if [[ "$BUILD_DEB" -eq 1 ]] && ! command -v dpkg-deb >/dev/null 2>&1; then
    warn 'dpkg-deb not found; skipping the .deb. / 未找到 dpkg-deb；跳过 .deb。'
    BUILD_DEB=0
fi

if [[ "$BUILD_DEB" -eq 1 ]]; then
    CONTROL_TEMPLATE="$REPO_ROOT/packaging_Packaging_打包/linux_Linux/debian_control/control"
    [[ -f "$CONTROL_TEMPLATE" ]] || die "control template not found / 未找到 control 模板: $CONTROL_TEMPLATE"

    # Installed-Size must be in KiB and has to be accurate before dpkg-deb runs.
    # Installed-Size 以 KiB 为单位，且必须在 dpkg-deb 运行前算准。
    installed_kib="$(du -sk --exclude=DEBIAN "$PKG_DIR" | awk '{print $1}')"

    sed \
        -e "s|^Version:.*|Version: ${VERSION}|" \
        -e "s|^Architecture:.*|Architecture: ${ARCH}|" \
        -e "s|^Installed-Size:.*|Installed-Size: ${installed_kib}|" \
        -e "s|^Maintainer:.*|Maintainer: ${MAINTAINER}|" \
        "$CONTROL_TEMPLATE" > "$PKG_DIR/DEBIAN/control"

    # Ship the project licence as the Debian copyright file.
    # 把项目许可证作为 Debian 版权文件一并提供。
    if [[ -f "$REPO_ROOT/LICENSE" ]]; then
        install -Dm644 "$REPO_ROOT/LICENSE" "$PKG_DIR/usr/share/doc/$PKG_NAME/copyright"
    fi

    dpkg-deb --root-owner-group --build "$PKG_DIR" "$DEB_PATH"
    log "Debian package built 已构建 Debian 包: $DEB_PATH"
    dpkg-deb --info "$DEB_PATH" || true
fi

# ----------------------------------------------------------------------------
#  4. Portable tarball 可移植压缩包
# ----------------------------------------------------------------------------
if [[ "$BUILD_TARBALL" -eq 1 ]]; then
    log 'Building portable tarball / 构建可移植压缩包'
    TARBALL="$OUTPUT_DIR/${PKG_NAME}_${VERSION}_${ARCH}.tar.gz"
    tar -C "$PKG_DIR/usr" -czf "$TARBALL" .
    log "Tarball built 已构建压缩包: $TARBALL"
fi

# ----------------------------------------------------------------------------
#  5. Summary 汇总
# ----------------------------------------------------------------------------
log 'Packaging summary / 打包汇总'
find "$OUTPUT_DIR" -maxdepth 1 -type f \( -name '*.deb' -o -name '*.tar.gz' \) \
    -printf '%f  %s bytes\n' 2>/dev/null || ls -l "$OUTPUT_DIR"
