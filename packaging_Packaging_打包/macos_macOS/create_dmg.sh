#!/usr/bin/env bash
# ============================================================================
#  create_dmg.sh -- build a distributable macOS .dmg from a finished .app bundle
#  create_dmg.sh —— 由已完成的 .app 包生成可分发的 macOS .dmg
#
#  Behaviour / 行为:
#    * uses `create-dmg` when it is installed (nicer layout, window geometry),
#      otherwise falls back to plain `hdiutil`, which is always present on macOS;
#    * always ships an /Applications symlink so users can drag-install;
#    * outputs TCBuildWeb-<version>-macos-<arch>.dmg.
#    * 若已安装 `create-dmg` 则使用它（布局与窗口尺寸更美观），否则回退到
#      macOS 必然自带的 `hdiutil`；
#    * 始终附带 /Applications 符号链接，便于用户拖拽安装；
#    * 输出 TCBuildWeb-<version>-macos-<arch>.dmg。
#
#  Usage / 用法:
#    ./create_dmg.sh --app dist/stage/TCTools-Build_Web.app \
#                    --version 0.1.0-rc1 --arch arm64 --output-dir dist
# ============================================================================
set -euo pipefail

# --- Defaults 默认值 ---------------------------------------------------------
APP_PATH=""
VERSION="0.1.0-rc1"   # overridden by --version / 由 --version 覆盖
ARCH="arm64"
OUTPUT_DIR="dist"
VOLUME_NAME="TCTools-Build_Web"
KEEP_STAGING=0

# --- Argument parsing 参数解析 ----------------------------------------------
usage() {
    cat <<'EOF'
Usage: create_dmg.sh [options]

  --app <path>         Path to TCTools-Build_Web.app        (required)
  --version <ver>      Version string                       (default: 0.1.0-rc1)
  --arch <arch>        arm64 | x86_64 | universal           (default: arm64)
  --output-dir <dir>   Where to write the .dmg              (default: dist)
  --volume-name <name> Mounted volume name                  (default: TCTools-Build_Web)
  --keep-staging       Keep the staging directory for debugging
  -h | --help          Show this help

  --app <路径>         TCTools-Build_Web.app 的路径          （必需）
  --version <版本>     版本号                                （默认 0.1.0-rc1）
  --arch <架构>        arm64 | x86_64 | universal            （默认 arm64）
  --output-dir <目录>  .dmg 输出目录                         （默认 dist）
  --volume-name <名>   挂载卷名称                            （默认 TCTools-Build_Web）
  --keep-staging       保留暂存目录以便调试
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --app)          APP_PATH="${2:?--app needs a value}";          shift 2 ;;
        --version)      VERSION="${2:?--version needs a value}";      shift 2 ;;
        --arch)         ARCH="${2:?--arch needs a value}";            shift 2 ;;
        --output-dir)   OUTPUT_DIR="${2:?--output-dir needs a value}"; shift 2 ;;
        --volume-name)  VOLUME_NAME="${2:?--volume-name needs a value}"; shift 2 ;;
        --keep-staging) KEEP_STAGING=1; shift ;;
        -h|--help)      usage; exit 0 ;;
        *) echo "Unknown option 未知参数: $1" >&2; usage >&2; exit 2 ;;
    esac
done

log()  { printf '\033[36m==> %s\033[0m\n' "$*"; }
warn() { printf '\033[33m[warn] %s\033[0m\n' "$*" >&2; }
die()  { printf '\033[31m[error] %s\033[0m\n' "$*" >&2; exit 1; }

# --- Validation 校验 ---------------------------------------------------------
[[ "$(uname -s)" == 'Darwin' ]] || die "create_dmg.sh requires macOS / create_dmg.sh 需要 macOS 环境"
[[ -n "$APP_PATH" ]] || { usage >&2; die "--app is required / --app 为必需参数"; }
[[ -d "$APP_PATH" ]] || die "App bundle not found 未找到应用包: $APP_PATH"
command -v hdiutil >/dev/null 2>&1 || die 'hdiutil not found / 未找到 hdiutil'

APP_PATH="$(cd "$(dirname "$APP_PATH")" && pwd)/$(basename "$APP_PATH")"
mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"

DMG_NAME="TCBuildWeb-${VERSION}-macos-${ARCH}.dmg"
DMG_PATH="$OUTPUT_DIR/$DMG_NAME"
STAGING="$(mktemp -d "${TMPDIR:-/tmp}/tcbw-dmg.XXXXXX")"

cleanup() {
    if [[ "$KEEP_STAGING" -eq 1 ]]; then
        warn "Staging kept / 已保留暂存目录: $STAGING"
    else
        rm -rf "$STAGING"
    fi
    # Detach anything still mounted from a previous failed run.
    # 卸载上次失败运行可能仍挂载的卷。
    if [[ -d "/Volumes/$VOLUME_NAME" ]]; then
        hdiutil detach "/Volumes/$VOLUME_NAME" -force >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

log "App bundle 应用包 : $APP_PATH"
log "Version 版本      : $VERSION"
log "Arch 架构         : $ARCH"
log "Output 输出       : $DMG_PATH"

# --- Stage payload 准备载荷 -------------------------------------------------
log 'Staging DMG contents / 准备 DMG 内容'
cp -R "$APP_PATH" "$STAGING/"
ln -s /Applications "$STAGING/Applications"

# Any leftover quarantine attribute would make the app look "damaged" to users.
# 残留的 quarantine 属性会让用户看到“应用已损坏”的提示。
xattr -cr "$STAGING/$(basename "$APP_PATH")" 2>/dev/null || true

# --- Build the DMG 生成 DMG -------------------------------------------------
rm -f "$DMG_PATH"

if command -v create-dmg >/dev/null 2>&1; then
    log 'Using create-dmg / 使用 create-dmg'
    # create-dmg is notoriously picky about cosmetic options (missing .icns,
    # AppleScript timeouts under CI). Treat any failure as a signal to fall back
    # to hdiutil instead of failing the whole release.
    # create-dmg 对外观选项非常挑剔（缺少 .icns、CI 下 AppleScript 超时）。
    # 任何失败都视为回退到 hdiutil 的信号，而不是让整个发布失败。
    if ! create-dmg \
            --volname "$VOLUME_NAME" \
            --window-pos 200 120 \
            --window-size 640 400 \
            --icon-size 112 \
            --icon "$(basename "$APP_PATH")" 160 180 \
            --hide-extension "$(basename "$APP_PATH")" \
            --app-drop-link 480 180 \
            --no-internet-enable \
            "$DMG_PATH" \
            "$STAGING"; then
        warn 'create-dmg failed -- falling back to hdiutil. / create-dmg 失败 —— 回退到 hdiutil。'
        rm -f "$DMG_PATH"
    fi
fi

if [[ ! -f "$DMG_PATH" ]]; then
    log 'Using hdiutil / 使用 hdiutil'
    # Two-step: build a read/write image from the folder, then convert it to a
    # compressed read-only image -- the layout hdiutil recommends for releases.
    # 两步式：先由目录生成可读写镜像，再转换为压缩只读镜像 —— hdiutil 推荐的
    # 发行版布局。
    TEMP_DMG="$STAGING/tcbw-temp.dmg"
    hdiutil create \
        -volname "$VOLUME_NAME" \
        -srcfolder "$STAGING" \
        -ov -format UDRW \
        "$TEMP_DMG" >/dev/null
    hdiutil convert "$TEMP_DMG" \
        -format UDZO \
        -imagekey zlib-level=9 \
        -o "$DMG_PATH" >/dev/null
fi

[[ -f "$DMG_PATH" ]] || die "DMG was not produced / 未生成 DMG: $DMG_PATH"

# --- Verify 校验 ------------------------------------------------------------
log 'Verifying DMG / 校验 DMG'
hdiutil verify "$DMG_PATH" >/dev/null || die "hdiutil verify failed / hdiutil 校验失败"
# Record a checksum so release consumers can verify the download.
# 记录校验和，便于发布使用者校验下载文件。
( cd "$OUTPUT_DIR" && shasum -a 256 "$DMG_NAME" > "$DMG_NAME.sha256" )

log 'DMG ready / DMG 已就绪'
ls -lh "$DMG_PATH" "$DMG_PATH.sha256"
echo "DMG     安装镜像 : $DMG_PATH"
echo "SHA256  校验和   : $(awk '{print $1}' "$DMG_PATH.sha256")"
