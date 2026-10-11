// ============================================================================
//  AppBadge.qml -- small status pill
//  AppBadge.qml —— 小型状态标签
// ============================================================================
import QtQuick
import TCTools.BuildWeb

Rectangle {
    id: badge

    property string text: ""
    property string tone: "neutral"    // neutral | accent | success | warning | danger | info
    property bool outline: false

    implicitWidth: label.implicitWidth + Theme.space.md
    implicitHeight: label.implicitHeight + 6
    radius: Theme.radius.pill

    readonly property color _tone: {
        switch (tone) {
        case "accent":  return Theme.colour.accent
        case "success": return Theme.colour.success
        case "warning": return Theme.colour.warning
        case "danger":  return Theme.colour.danger
        case "info":    return Theme.colour.info
        default:        return Theme.colour.textSecondary
        }
    }

    color: badge.outline ? "transparent" : Theme.alpha(badge._tone, 0.14)
    border.width: badge.outline ? 1 : 0
    border.color: Theme.alpha(badge._tone, 0.45)

    Text {
        id: label
        anchors.centerIn: parent
        text: badge.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.sizeCaption
        font.weight: Theme.font.weightMedium
        color: badge._tone
    }
}
