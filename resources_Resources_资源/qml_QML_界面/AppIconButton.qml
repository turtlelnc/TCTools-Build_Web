// ============================================================================
//  AppIconButton.qml -- square, borderless icon button
//  AppIconButton.qml —— 方形无边框图标按钮
// ============================================================================
import QtQuick
import QtQuick.Controls.Basic
import TCTools.BuildWeb

AbstractButton {
    id: control

    property string glyph: ""
    property string tooltip: ""
    property int buttonSize: Theme.size.hitTarget
    property bool active: false
    property bool danger: false

    implicitWidth: buttonSize
    implicitHeight: buttonSize
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    background: Rectangle {
        radius: Theme.radius.sm
        color: !control.enabled ? "transparent"
             : control.pressed ? Theme.colour.fillStrong
             : control.hovered || control.active ? Theme.colour.fill
             : "transparent"

        Behavior on color {
            ColorAnimation { duration: Theme.motion.instant; easing.type: Theme.motion.easingStandard }
        }
    }

    contentItem: Text {
        text: control.glyph
        font.family: Theme.fontFamily
        font.pixelSize: Theme.size.icon
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: !control.enabled ? Theme.colour.textTertiary
             : control.danger ? Theme.colour.danger
             : control.active ? Theme.colour.accent
             : Theme.colour.textSecondary
        opacity: control.down ? 0.6 : 1
    }

    ToolTip.visible: control.tooltip !== "" && control.hovered
    ToolTip.text: control.tooltip
    ToolTip.delay: 500
}
