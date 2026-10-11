#!/usr/bin/env bash
# ============================================================================
#  deploy_macos.sh -- finalise a macOS .app bundle for distribution
#  deploy_macos.sh —— 为分发完成 macOS .app 包的收尾处理
#
#  What it does / 它做什么:
#    1. verifies the Qt frameworks were embedded by CMake's install step
#       (qt_generate_deploy_qml_app_script), and re-runs macdeployqt if not;
#    2. rewrites every Mach-O load path so the bundle is relocatable;
#    3. records the pinned version string in Info.plist;
#    4. verifies the bundle's architecture slices and, optionally, signs it.
#    1. 校验 CMake 安装步骤（qt_generate_deploy_qml_app_script）是否已嵌入 Qt 框架，
#       若未嵌入则重新执行 macdeployqt；
#    2. 重写全部 Mach-O 加载路径，使应用包可任意移动；
#    3. 把固定版本号写入 Info.plist；
#    4. 校验应用包的架构切片，并可选择进行签名。
#
#  Usage / 用法:
#    ./deploy_macos.sh --app dist/stage/TCTools-Build_Web.app \
#                      --arch 'arm64' --qt-dir "$QT_ROOT_DIR" --version 0.1.0-rc1
# ============================================================================
set -euo pipefail

# --- Defaults 默认值 ---------------------------------------------------------
APP_PATH=""
ARCH="arm64"
QT_DIR="${QT_ROOT_DIR:-}"
VERSION="0.1.0-rc1"
CODESIGN_IDENTITY="${CODESIGN_IDENTITY:-}"
SKIP_SIGN=0

# --- Argument parsing 参数解析 ----------------------------------------------
usage() {
    cat <<'EOF'
Usage: deploy_macos.sh [options]

  --app <path>        Path to TCTools-Build_Web.app            (required)
  --arch <archs>      arm64 | x86_64 | "arm64;x86_64"          (default: arm64)
  --qt-dir <path>     Qt kit root (default: $QT_ROOT_DIR)      (required-ish)
  --version <ver>     Version string for Info.plist            (default: 0.1.0-rc1)
  --identity <id>     codesign identity; empty means ad-hoc signing
  --skip-sign         Do not sign at all
  -h | --help         Show this help

  --app <路径>        TCTools-Build_Web.app 的路径              （必需）
  --arch <架构>       arm64 | x86_64 | "arm64;x86_64"           （默认 arm64）
  --qt-dir <路径>     Qt 套件根目录（默认 $QT_ROOT_DIR）        （建议提供）
  --version <版本>    写入 Info.plist 的版本号                  （默认 0.1.0-rc1）
  --identity <标识>   codesign 标识；留空则使用临时签名
  --skip-sign         完全跳过签名
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --app)        APP_PATH="${2:?--app needs a value}";   shift 2 ;;
        --arch)       ARCH="${2:?--arch needs a value}";      shift 2 ;;
        --qt-dir)     QT_DIR="${2:?--qt-dir needs a value}";  shift 2 ;;
        --version)    VERSION="${2:?--version needs a value}"; shift 2 ;;
        --identity)   CODESIGN_IDENTITY="${2:?--identity needs a value}"; shift 2 ;;
        --skip-sign)  SKIP_SIGN=1; shift ;;
        -h|--help)    usage; exit 0 ;;
        *) echo "Unknown option 未知参数: $1" >&2; usage >&2; exit 2 ;;
    esac
done

log()  { printf '\033[36m==> %s\033[0m\n' "$*"; }
warn() { printf '\033[33m[warn] %s\033[0m\n' "$*" >&2; }
die()  { printf '\033[31m[error] %s\033[0m\n' "$*" >&2; exit 1; }

# --- Validation 校验 ---------------------------------------------------------
[[ -n "$APP_PATH" ]] || { usage >&2; die "--app is required / --app 为必需参数"; }
[[ -d "$APP_PATH" ]] || die "App bundle not found 未找到应用包: $APP_PATH"
# Normalise to an absolute path; every later command quotes it because the
# developer's checkout path contains non-ASCII characters.
# 规范化为绝对路径；后续命令全部加引号，因为开发机检出路包含非 ASCII 字符。
APP_PATH="$(cd "$(dirname "$APP_PATH")" && pwd)/$(basename "$APP_PATH")"

APP_NAME="$(basename "$APP_PATH" .app)"
CONTENTS="$APP_PATH/Contents"
PLIST="$CONTENTS/Info.plist"
[[ -f "$PLIST" ]] || die "Info.plist missing 缺少 Info.plist: $PLIST"

log "App bundle 应用包 : $APP_PATH"
log "Architectures 架构 : $ARCH"
log "Version 版本       : $VERSION"

# --- Locate macdeployqt 定位 macdeployqt ------------------------------------
MACDEPLOYQT=""
if [[ -n "$QT_DIR" && -x "$QT_DIR/bin/macdeployqt" ]]; then
    MACDEPLOYQT="$QT_DIR/bin/macdeployqt"
elif command -v macdeployqt >/dev/null 2>&1; then
    MACDEPLOYQT="$(command -v macdeployqt)"
fi

# --- 1. Ensure Qt is embedded 确保已嵌入 Qt ---------------------------------
log 'Checking embedded Qt frameworks / 检查已嵌入的 Qt 框架'
if [[ -d "$CONTENTS/Frameworks/QtCore.framework" ]]; then
    log 'Qt frameworks already embedded by cmake --install. / Qt 框架已由 cmake --install 嵌入。'
else
    if [[ -z "$MACDEPLOYQT" ]]; then
        die "Qt frameworks are not embedded and macdeployqt was not found. Pass --qt-dir <qt-kit-root>."
    fi
    warn 'Qt frameworks missing -- running macdeployqt. / 缺少 Qt 框架 —— 执行 macdeployqt。'
    "$MACDEPLOYQT" "$APP_PATH" -verbose=1 -always-overwrite
fi

# --- 2. Rewrite load paths 重写加载路径 -------------------------------------
log 'Rewriting Mach-O load paths / 重写 Mach-O 加载路径'
if [[ -n "$MACDEPLOYQT" ]]; then
    "$MACDEPLOYQT" "$APP_PATH" -verbose=1 -always-overwrite -no-plugins -no-strip
else
    warn 'macdeployqt unavailable -- relying on the install-time rpath fixups. / macdeployqt 不可用 —— 依赖安装期的 rpath 修正。'
fi

# Any binary still pointing into the build machine's Qt install is a bug.
# 任何仍指向构建机 Qt 安装目录的二进制都是缺陷。
log 'Scanning for absolute build-machine paths / 扫描构建机绝对路径'
if [[ -z "$QT_DIR" ]]; then
    warn 'No --qt-dir given -- skipping the stale-reference scan. / 未提供 --qt-dir —— 跳过残留引用扫描。'
else
    stale=0
    while IFS= read -r -d '' binary; do
        # Substring test in bash instead of a regex: the Qt path contains
        # characters that would need escaping in a grep pattern.
        # 在 bash 中做子串判断而非正则：Qt 路径含需要在 grep 模式中转义的字符。
        while IFS= read -r line; do
            case "$line" in
                *"$QT_DIR"*)
                    warn "Stale Qt reference 残留 Qt 引用: $binary"
                    stale=$((stale + 1))
                    break
                    ;;
            esac
        done < <(otool -L "$binary" 2>/dev/null || true)
    done < <(find "$CONTENTS" -type f \( -perm -111 -o -name '*.dylib' \) -print0 2>/dev/null)

    if [[ "$stale" -gt 0 ]]; then
        warn "$stale binary/binaries still reference the build-machine Qt path. Gatekeeper may refuse the bundle."
        warn "有 $stale 个二进制仍引用构建机 Qt 路径，Gatekeeper 可能拒绝该应用包。"
    else
        log 'No stale references found. / 未发现残留引用。'
    fi
fi

# --- 3. Stamp version metadata 写入版本元数据 -------------------------------
log 'Updating Info.plist version keys / 更新 Info.plist 版本键'
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString ${VERSION}" "$PLIST" 2>/dev/null ||
    /usr/libexec/PlistBuddy -c "Add :CFBundleShortVersionString string ${VERSION}" "$PLIST"
/usr/libexec/PlistBuddy -c "Set :TCBWVersionFull ${VERSION}" "$PLIST" 2>/dev/null ||
    /usr/libexec/PlistBuddy -c "Add :TCBWVersionFull string ${VERSION}" "$PLIST"
# CFBundleVersion must stay purely numeric and monotonic.
# CFBundleVersion 必须保持纯数字且单调递增。
NUMERIC_VERSION="$(printf '%s' "$VERSION" | sed -E 's/[^0-9.].*$//; s/\.$//')"
[[ -n "$NUMERIC_VERSION" ]] || NUMERIC_VERSION="0.1.0"
/usr/libexec/PlistBuddy -c "Set :CFBundleVersion ${NUMERIC_VERSION}" "$PLIST" 2>/dev/null ||
    /usr/libexec/PlistBuddy -c "Add :CFBundleVersion string ${NUMERIC_VERSION}" "$PLIST"
plutil -lint "$PLIST" >/dev/null || die "Info.plist failed validation / Info.plist 校验失败"

# --- 4. Verify architectures 校验架构 ---------------------------------------
log 'Verifying architecture slices / 校验架构切片'
MAIN_BINARY="$CONTENTS/MacOS/$APP_NAME"
[[ -f "$MAIN_BINARY" ]] || die "Main executable missing 缺少主可执行文件: $MAIN_BINARY"
FAT_INFO="$(lipo -info "$MAIN_BINARY" 2>/dev/null || true)"
echo "    $FAT_INFO"

# Normalise "arm64;x86_64" into a list and assert each slice exists when the
# build was meant to produce a universal binary.
# 把 "arm64;x86_64" 规范化为列表；若是通用二进制构建，则断言每个切片都存在。
IFS=';' read -r -a WANTED <<< "${ARCH//,/;}"
if [[ "${#WANTED[@]}" -gt 1 ]]; then
    for slice in "${WANTED[@]}"; do
        slice="${slice// /}"
        [[ -n "$slice" ]] || continue
        if ! printf '%s' "$FAT_INFO" | grep -q "$slice"; then
            die "Universal binary is missing the '$slice' slice / 通用二进制缺少 '$slice' 切片"
        fi
    done
    log 'Universal binary contains every requested slice. / 通用二进制包含全部所需切片。'
fi

# --- 5. Codesign 代码签名 ---------------------------------------------------
if [[ "$SKIP_SIGN" -eq 1 ]]; then
    warn 'Signing skipped by request. / 按要求跳过签名。'
elif [[ "$(uname -s)" == 'Darwin' ]]; then
    log 'Code signing bundle / 对应用包进行代码签名'
    # Sign nested helpers first, then the bundle, as codesign requires.
    # 按 codesign 的要求，先签名内部辅助可执行文件，再签名应用包本体。
    while IFS= read -r -d '' nested; do
        if [[ -n "$CODESIGN_IDENTITY" ]]; then
            codesign --force --timestamp --options runtime \
                     --sign "$CODESIGN_IDENTITY" "$nested"
        else
            codesign --force --sign - "$nested"
        fi
    done < <(find "$CONTENTS/Frameworks" "$CONTENTS/PlugIns" -type f \
                  \( -name '*.dylib' -o -perm -111 \) -print0 2>/dev/null || true)

    if [[ -n "$CODESIGN_IDENTITY" ]]; then
        codesign --force --timestamp --options runtime \
                 --entitlements /dev/null \
                 --sign "$CODESIGN_IDENTITY" "$APP_PATH"
    else
        # Ad-hoc signature: enough for local verification, not for distribution.
        # 临时签名：足够本地校验，但不适用于正式分发。
        codesign --force --deep --sign - "$APP_PATH"
    fi
    codesign --verify --deep --strict --verbose=2 "$APP_PATH" ||
        warn 'codesign verification reported problems / codesign 校验报告了问题。'
else
    warn 'Not running on macOS -- skipping codesign. / 非 macOS 环境 —— 跳过 codesign。'
fi

# --- 6. Summary 汇总 --------------------------------------------------------
log 'Deployment summary / 部署汇总'
du -sh "$APP_PATH" || true
echo "Bundle   应用包 : $APP_PATH"
echo "Version  版本   : $VERSION (CFBundleVersion=${NUMERIC_VERSION})"
echo "Arch     架构   : $FAT_INFO"
