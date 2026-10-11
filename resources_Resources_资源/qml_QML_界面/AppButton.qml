// ============================================================================
//  AppButton.qml -- the Apple-style push button
//  AppButton.qml —— Apple 风格按钮
// ============================================================================
import QtQuick
import QtQuick.Controls.Basic
import TCTools.BuildWeb

AbstractButton {
    id: control

    //  Visual variants / 视觉变体
    //  "primary"   filled accent button, one per screen at most
    //  "secondary" neutral filled button
    //  "ghost"     borderless, for toolbars and inline actions
    //  "danger"    destructive action
    //  "link"      inline text action
    //  "primary"   强调色填充按钮，每屏最多一个
    //  "secondary" 中性填充按钮
    //  "ghost"     无边框，用于工具栏与行内操作
    //  "danger"    破坏性操作
    //  "link"      行内文本操作
    property string variant: "secondary"
    property string size: "medium"        // "small" | "medium" | "large"
    property string iconName: ""
    property bool loading: false
    property string tooltip: ""

    implicitHeight: size === "small" ? 24
                  : size === "large" ? Theme.size.controlHeightLarge
                  : Theme.size.controlHeight
    implicitWidth: Math.max(implicitHeight,
                            label.implicitWidth + leftPadding + rightPadding)
    padding: size === "small" ? Theme.space.sm : Theme.space.md
    leftPadding: iconName !== "" ? Theme.space.sm + 18 : padding
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    readonly property color _fill: {
        if (!enabled)
            return variant === "ghost" || variant === "link" ? "transparent" : Theme.colour.fill
        if (variant === "primary")
            return control.pressed ? Theme.colour.accentPressed
                 : control.hovered ? Theme.colour.accentHover
                 : Theme.colour.accent
        if (variant === "danger")
            return control.pressed ? Theme.mix(Theme.colour.danger, "#000000", 0.22)
                 : control.hovered ? Theme.mix(Theme.colour.danger, "#FFFFFF", 0.12)
                 : Theme.colour.danger
        if (variant === "ghost" || variant === "link")
            return control.pressed ? Theme.colour.fillSecondary
                 : control.hovered ? Theme.colour.fill
                 : "transparent"
        return control.pressed ? Theme.colour.fillStrong
             : control.hovered ? Theme.colour.fillSecondary
             : Theme.colour.fill
    }

    readonly property color _text: {
        if (!enabled)
            return Theme.colour.textTertiary
        if (variant === "primary" || variant === "danger")
            return Theme.colour.textOnAccent
        if (variant === "link")
            return Theme.colour.accent
        return Theme.colour.text
    }

    background: Rectangle {
        radius: control.size === "small" ? Theme.radius.sm
              : control.size === "large" ? Theme.radius.lg
              : Theme.radius.md
        color: control._fill
        border.width: control.visualFocus && control.variant !== "primary"
                      && control.variant !== "danger" ? 2 : 0
        border.color: Theme.colour.accent

        Behavior on color {
            ColorAnimation { duration: Theme.motion.fast; easing.type: Theme.motion.easingStandard }
        }
    }

    contentItem: Item {
        implicitWidth: label.implicitWidth + (control.iconName !== "" ? 20 : 0)
        implicitHeight: label.implicitHeight

        Text {
            id: icon
            visible: control.iconName !== ""
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: control.iconName
            font.family: Theme.fontFamily
            font.pixelSize: control.size === "small" ? 12 : 14
            color: control._text
            opacity: control.loading ? 0 : 1
        }

        BusyIndicator {
            visible: control.loading
            running: control.loading
            anchors.centerIn: parent
            implicitWidth: 16
            implicitHeight: 16
        }

        Text {
            id: label
            anchors.left: control.iconName !== "" ? icon.right : parent.left
            anchors.leftMargin: control.iconName !== "" ? 6 : 0
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            font.family: Theme.fontFamily
            font.pixelSize: control.size === "small" ? Theme.font.sizeCaption
                          : control.size === "large" ? Theme.font.sizeSubhead
                          : Theme.font.sizeBody
            font.weight: control.variant === "primary" ? Theme.font.weightMedium
                                                       : Theme.font.weightRegular
            color: control._text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            opacity: control.loading ? 0.35 : 1
        }
    }

    ToolTip.visible: control.tooltip !== "" && control.hovered
    ToolTip.text: control.tooltip
    ToolTip.delay: 600

    Keys.onSpacePressed: if (control.enabled) control.clicked()
    Keys.onReturnPressed: if (control.enabled) control.clicked()
}
