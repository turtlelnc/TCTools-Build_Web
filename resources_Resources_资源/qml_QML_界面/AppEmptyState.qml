// ============================================================================
//  AppEmptyState.qml -- friendly placeholder for empty panels
//  AppEmptyState.qml —— 空面板的友好占位
// ============================================================================
import QtQuick
import TCTools.BuildWeb

Item {
    id: root

    property string glyph: "\u25CB"
    property string title: ""
    property string detail: ""
    property string actionText: ""
    property bool compact: false

    signal actionTriggered()

    implicitHeight: column.implicitHeight

    Column {
        id: column
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.space.xl, 320)
        spacing: Theme.space.sm

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.glyph
            font.pixelSize: root.compact ? 22 : 32
            color: Theme.colour.textTertiary
            opacity: 0.55
        }

        Text {
            width: parent.width
            text: root.title
            visible: root.title !== ""
            font.family: Theme.fontFamily
            font.pixelSize: root.compact ? Theme.font.sizeFootnote : Theme.font.sizeCallout
            font.weight: Theme.font.weightMedium
            color: Theme.colour.textSecondary
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Text {
            width: parent.width
            text: root.detail
            visible: root.detail !== ""
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.sizeCaption
            color: Theme.colour.textTertiary
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            lineHeight: 1.35
        }

        Item {
            width: 1
            height: root.actionText !== "" ? Theme.space.sm : 0
            visible: root.actionText !== ""
        }

        AppButton {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.actionText !== ""
            text: root.actionText
            variant: "secondary"
            size: "small"
            onClicked: root.actionTriggered()
        }
    }
}
