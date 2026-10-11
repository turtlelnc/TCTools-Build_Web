// ============================================================================
//  Theme.qml -- the Apple-inspired design system
//  Theme.qml —— Apple 风格设计系统
//
//  A single source of truth for colour, type, spacing, radius, shadow and
//  motion. Modelled on apple.com.cn: generous whitespace, a restrained
//  palette, translucent layered surfaces, generous corner radii and motion
//  that never draws attention to itself.
//  颜色、字体、间距、圆角、阴影与动效的唯一真实来源。参照 apple.com.cn：
//  宽裕的留白、克制的配色、半透明的分层表面、宽大的圆角，以及绝不喧宾夺主的动效。
//
//  This file is a QML singleton: use `Theme.colour.accent`, `Theme.font.title`, ...
//  本文件是 QML 单例：使用 `Theme.colour.accent`、`Theme.font.title` 等。
// ============================================================================
pragma Singleton

import QtQuick
import TCTools.BuildWeb

QtObject {
    id: theme

    // -----------------------------------------------------------------------
    //  Colour
    //  颜色
    // -----------------------------------------------------------------------
    readonly property bool dark: App.darkTheme

    //  Apple's system palette, in the web-facing sRGB values used by
    //  apple.com.cn rather than the native NSColor values.
    //  Apple 系统色板，采用 apple.com.cn 使用的网页 sRGB 值，而非原生 NSColor 值。
    readonly property QtObject colour: QtObject {
        // Canvas and surfaces / 画布与表面
        readonly property color canvas: theme.dark ? "#000000" : "#FFFFFF"
        readonly property color surface: theme.dark ? "#1C1C1E" : "#FFFFFF"
        readonly property color surfaceRaised: theme.dark ? "#2C2C2E" : "#FBFBFD"
        readonly property color surfaceSunken: theme.dark ? "#141416" : "#F5F5F7"
        readonly property color surfaceGlass: theme.dark ? Qt.rgba(0.17, 0.17, 0.18, 0.72)
                                                         : Qt.rgba(1.00, 1.00, 1.00, 0.72)
        readonly property color sidebar: theme.dark ? Qt.rgba(0.11, 0.11, 0.12, 0.86)
                                                    : Qt.rgba(0.96, 0.96, 0.97, 0.86)
        readonly property color chrome: theme.dark ? Qt.rgba(0.09, 0.09, 0.10, 0.82)
                                                   : Qt.rgba(1.00, 1.00, 1.00, 0.82)

        // Text / 文本
        readonly property color text: theme.dark ? "#F5F5F7" : "#1D1D1F"
        readonly property color textSecondary: theme.dark ? "#A1A1A6" : "#6E6E73"
        readonly property color textTertiary: theme.dark ? "#6E6E73" : "#86868B"
        readonly property color textInverse: theme.dark ? "#1D1D1F" : "#FFFFFF"
        readonly property color textOnAccent: "#FFFFFF"

        // Accents / 强调色
        readonly property color accent: theme.dark ? "#0A84FF" : "#0071E3"
        readonly property color accentHover: theme.dark ? "#3D9BFF" : "#0077ED"
        readonly property color accentPressed: theme.dark ? "#0060DF" : "#006EDB"
        readonly property color accentSoft: theme.dark ? Qt.rgba(0.04, 0.52, 1.0, 0.18)
                                                       : Qt.rgba(0.0, 0.44, 0.89, 0.10)
        readonly property color accentWash: theme.dark ? Qt.rgba(0.04, 0.52, 1.0, 0.10)
                                                       : Qt.rgba(0.0, 0.44, 0.89, 0.06)

        // Semantic / 语义色
        readonly property color success: theme.dark ? "#30D158" : "#1D8348"
        readonly property color warning: theme.dark ? "#FFD60A" : "#B25000"
        readonly property color danger: theme.dark ? "#FF453A" : "#D70015"
        readonly property color info: theme.dark ? "#64D2FF" : "#0071E3"

        // Fills and separators / 填充与分隔线
        readonly property color fill: theme.dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.05)
        readonly property color fillSecondary: theme.dark ? Qt.rgba(1, 1, 1, 0.16)
                                                          : Qt.rgba(0, 0, 0, 0.08)
        readonly property color fillStrong: theme.dark ? Qt.rgba(1, 1, 1, 0.22)
                                                       : Qt.rgba(0, 0, 0, 0.12)
        readonly property color separator: theme.dark ? Qt.rgba(1, 1, 1, 0.13)
                                                      : Qt.rgba(0, 0, 0, 0.10)
        readonly property color separatorStrong: theme.dark ? Qt.rgba(1, 1, 1, 0.22)
                                                            : Qt.rgba(0, 0, 0, 0.18)
        readonly property color hairline: theme.dark ? Qt.rgba(1, 1, 1, 0.08)
                                                     : Qt.rgba(0, 0, 0, 0.06)

        // Canvas-specific / 画布专用
        readonly property color canvasBackdrop: theme.dark ? "#0B0B0C" : "#EDEDF0"
        readonly property color selection: theme.dark ? "#0A84FF" : "#0071E3"
        readonly property color selectionSoft: Qt.rgba(0.04, 0.52, 1.0, 0.16)
        readonly property color guide: "#FF375F"
        readonly property color gridLine: theme.dark ? Qt.rgba(1, 1, 1, 0.05)
                                                     : Qt.rgba(0, 0, 0, 0.04)
        readonly property color dropTarget: Qt.rgba(0.04, 0.52, 1.0, 0.22)
        readonly property color dropIndicator: "#0A84FF"
        readonly property color codeBackground: theme.dark ? "#15161A" : "#F6F7F9"
        readonly property color codeText: theme.dark ? "#E6E6EB" : "#1D1D1F"
        readonly property color codeKeyword: theme.dark ? "#FF7AB2" : "#AD3DA4"
        readonly property color codeString: theme.dark ? "#FF8170" : "#C41A16"
        readonly property color codeComment: theme.dark ? "#7F8C98" : "#707F8C"
        readonly property color codeNumber: theme.dark ? "#D9C97C" : "#1C00CF"
    }

    // -----------------------------------------------------------------------
    //  Typography
    //  字体排印
    // -----------------------------------------------------------------------
    //  The platform system font stack, matching what apple.com.cn resolves to
    //  on each operating system.
    //  各操作系统上 apple.com.cn 实际解析到的平台系统字体栈。
    readonly property string fontFamily: {
        if (Qt.platform.os === "osx" || Qt.platform.os === "macos")
            return "SF Pro Text"
        if (Qt.platform.os === "windows")
            return "Segoe UI Variable Text"
        return "Inter"
    }

    readonly property string fontFamilyDisplay: {
        if (Qt.platform.os === "osx" || Qt.platform.os === "macos")
            return "SF Pro Display"
        if (Qt.platform.os === "windows")
            return "Segoe UI Variable Display"
        return "Inter"
    }

    //  Monospaced stack for the code viewer and the block-generated source.
    //  代码查看器与积木生成源码使用的等宽字体栈。
    readonly property string fontFamilyMono: {
        if (Qt.platform.os === "osx" || Qt.platform.os === "macos")
            return "SF Mono"
        if (Qt.platform.os === "windows")
            return "Cascadia Mono"
        return "JetBrains Mono"
    }

    readonly property QtObject font: QtObject {
        readonly property int sizeCaption2: 10
        readonly property int sizeCaption: 11
        readonly property int sizeFootnote: 12
        readonly property int sizeBody: 13
        readonly property int sizeCallout: 14
        readonly property int sizeSubhead: 15
        readonly property int sizeTitle3: 17
        readonly property int sizeTitle2: 20
        readonly property int sizeTitle1: 24
        readonly property int sizeLargeTitle: 30
        readonly property int sizeHero: 40

        readonly property int weightRegular: Font.Normal
        readonly property int weightMedium: Font.Medium
        readonly property int weightSemibold: Font.DemiBold
        readonly property int weightBold: Font.Bold

        //  Pre-composed text styles so call sites stay terse and consistent.
        //  预先组合好的文本样式，使调用处保持简洁一致。
        readonly property var hero: ({
            family: theme.fontFamilyDisplay,
            pixelSize: theme.font.sizeHero,
            weight: theme.font.weightBold,
            letterSpacing: -0.6,
            lineHeight: 1.08
        })
        readonly property var largeTitle: ({
            family: theme.fontFamilyDisplay,
            pixelSize: theme.font.sizeLargeTitle,
            weight: theme.font.weightBold,
            letterSpacing: -0.4,
            lineHeight: 1.14
        })
        readonly property var title1: ({
            family: theme.fontFamilyDisplay,
            pixelSize: theme.font.sizeTitle1,
            weight: theme.font.weightSemibold,
            letterSpacing: -0.3,
            lineHeight: 1.2
        })
        readonly property var title2: ({
            family: theme.fontFamilyDisplay,
            pixelSize: theme.font.sizeTitle2,
            weight: theme.font.weightSemibold,
            letterSpacing: -0.2,
            lineHeight: 1.22
        })
        readonly property var title3: ({
            family: theme.fontFamily,
            pixelSize: theme.font.sizeTitle3,
            weight: theme.font.weightSemibold,
            letterSpacing: -0.1,
            lineHeight: 1.28
        })
        readonly property var subhead: ({
            family: theme.fontFamily,
            pixelSize: theme.font.sizeSubhead,
            weight: theme.font.weightMedium,
            letterSpacing: -0.06,
            lineHeight: 1.35
        })
        readonly property var callout: ({
            family: theme.fontFamily,
            pixelSize: theme.font.sizeCallout,
            weight: theme.font.weightRegular,
            letterSpacing: -0.04,
            lineHeight: 1.4
        })
        readonly property var body: ({
            family: theme.fontFamily,
            pixelSize: theme.font.sizeBody,
            weight: theme.font.weightRegular,
            letterSpacing: -0.02,
            lineHeight: 1.45
        })
        readonly property var footnote: ({
            family: theme.fontFamily,
            pixelSize: theme.font.sizeFootnote,
            weight: theme.font.weightRegular,
            letterSpacing: 0.0,
            lineHeight: 1.4
        })
        readonly property var caption: ({
            family: theme.fontFamily,
            pixelSize: theme.font.sizeCaption,
            weight: theme.font.weightRegular,
            letterSpacing: 0.06,
            lineHeight: 1.35
        })
        readonly property var captionCaps: ({
            family: theme.fontFamily,
            pixelSize: theme.font.sizeCaption2,
            weight: theme.font.weightSemibold,
            letterSpacing: 0.8,
            lineHeight: 1.3
        })
        readonly property var mono: ({
            family: theme.fontFamilyMono,
            pixelSize: theme.font.sizeFootnote,
            weight: theme.font.weightRegular,
            letterSpacing: 0.0,
            lineHeight: 1.5
        })
    }

    // -----------------------------------------------------------------------
    //  Spacing, radius and sizing
    //  间距、圆角与尺寸
    // -----------------------------------------------------------------------
    readonly property QtObject space: QtObject {
        readonly property int xxs: 2
        readonly property int xs: 4
        readonly property int sm: 8
        readonly property int md: 12
        readonly property int lg: 16
        readonly property int xl: 24
        readonly property int xxl: 32
        readonly property int xxxl: 48
        readonly property int huge: 64

        //  Layout metrics / 布局尺寸
        readonly property int sidebarWidth: 248
        readonly property int sidebarCollapsedWidth: 56
        readonly property int inspectorWidth: 292
        readonly property int topBarHeight: 52
        readonly property int toolbarHeight: 44
        readonly property int statusBarHeight: 26
        readonly property int titleBarHeight: 30
    }

    readonly property QtObject radius: QtObject {
        readonly property int xs: 4
        readonly property int sm: 6
        readonly property int md: 10
        readonly property int lg: 14
        readonly property int xl: 18
        readonly property int xxl: 24
        readonly property int pill: 999
    }

    readonly property QtObject size: QtObject {
        readonly property int controlHeight: 30
        readonly property int controlHeightLarge: 38
        readonly property int iconSm: 14
        readonly property int icon: 17
        readonly property int iconLg: 22
        readonly property int handleSize: 8
        readonly property int hitTarget: 28
        readonly property int swatch: 22
        readonly property int panelHeader: 34
    }

    // -----------------------------------------------------------------------
    //  Elevation
    //  层级阴影
    // -----------------------------------------------------------------------
    readonly property QtObject elevation: QtObject {
        readonly property var none: ({ blur: 0, spread: 0, dy: 0, opacity: 0.0 })
        readonly property var low: theme.dark
            ? ({ blur: 12, spread: 0, dy: 2, opacity: 0.34 })
            : ({ blur: 12, spread: 0, dy: 2, opacity: 0.08 })
        readonly property var medium: theme.dark
            ? ({ blur: 24, spread: 0, dy: 6, opacity: 0.42 })
            : ({ blur: 24, spread: 0, dy: 6, opacity: 0.11 })
        readonly property var high: theme.dark
            ? ({ blur: 40, spread: 0, dy: 12, opacity: 0.52 })
            : ({ blur: 40, spread: 0, dy: 12, opacity: 0.16 })
        readonly property var overlay: theme.dark
            ? ({ blur: 56, spread: 0, dy: 20, opacity: 0.60 })
            : ({ blur: 56, spread: 0, dy: 20, opacity: 0.22 })
    }

    // -----------------------------------------------------------------------
    //  Motion
    //  动效
    // -----------------------------------------------------------------------
    //  Honours the operating system's "reduce motion" preference, which the
    //  user can also force from the settings screen.
    //  遵循操作系统的"减弱动态效果"偏好，用户也可在设置界面强制开启。
    readonly property bool reduceMotion: App.editorSettings.reduceMotion === true

    readonly property QtObject motion: QtObject {
        readonly property int instant: theme.reduceMotion ? 0 : 90
        readonly property int fast: theme.reduceMotion ? 0 : 160
        readonly property int normal: theme.reduceMotion ? 0 : 240
        readonly property int slow: theme.reduceMotion ? 0 : 380
        readonly property int spring: theme.reduceMotion ? 0 : 420

        readonly property int easingStandard: Easing.OutCubic
        readonly property int easingEnter: Easing.OutQuint
        readonly property int easingExit: Easing.InCubic
        readonly property int easingSpring: Easing.OutBack
    }

    // -----------------------------------------------------------------------
    //  Shared gradients and brushes
    //  共享渐变与笔刷
    // -----------------------------------------------------------------------
    readonly property QtObject brush: QtObject {
        readonly property var accentGradient: theme.dark
            ? { start: "#0A84FF", end: "#5E5CE6" }
            : { start: "#0071E3", end: "#5E5CE6" }
        readonly property var heroGradient: theme.dark
            ? { start: "#1C1C1E", end: "#000000" }
            : { start: "#FBFBFD", end: "#F5F5F7" }
    }

    // -----------------------------------------------------------------------
    //  Helpers
    //  辅助函数
    // -----------------------------------------------------------------------
    //  Mixes two colours; `amount` 0 returns `a`, 1 returns `b`.
    //  混合两种颜色；`amount` 为 0 返回 a，为 1 返回 b。
    function mix(a, b, amount) {
        var t = Math.max(0, Math.min(1, amount))
        return Qt.rgba(a.r + (b.r - a.r) * t,
                       a.g + (b.g - a.g) * t,
                       a.b + (b.b - a.b) * t,
                       a.a + (b.a - a.a) * t)
    }

    //  A translucent variant of a colour, used for hover and selection states.
    //  颜色的半透明变体，用于悬停与选中状态。
    function alpha(colour, a) {
        return Qt.rgba(colour.r, colour.g, colour.b, a)
    }

    //  Produces a soft, Apple-like elevation effect as a list of parameters
    //  ready to be spread onto a DropShadow.
    //  生成柔和的 Apple 风格层级阴影参数，可直接展开到 DropShadow 上。
    function shadowFor(level) {
        var e = level === "high" ? elevation.high
              : level === "medium" ? elevation.medium
              : level === "overlay" ? elevation.overlay
              : elevation.low
        return e
    }

    //  Colour used for a block-programming category swatch.
    //  积木编程分类色块使用的颜色。
    function blockCategoryColour(key) {
        var map = {
            events: "#FF9F0A",
            ui: "#0A84FF",
            text: "#5E5CE6",
            control: "#FFD60A",
            logic: "#30D158",
            math: "#64D2FF",
            variables: "#FF6482",
            storage: "#AC8E68",
            http: "#40C8E0",
            backend: "#5AC8FA",
            data: "#BF5AF2",
            ai: "#FF375F",
            mcp: "#FF9F0A",
            timing: "#8E8E93",
            console: "#48484A"
        }
        return map[key] !== undefined ? map[key] : "#8E8E93"
    }
}
