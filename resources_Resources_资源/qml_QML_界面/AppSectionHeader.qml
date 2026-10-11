// ============================================================================
//  AppSectionHeader.qml -- titled section divider used in panels and dialogs
//  AppSectionHeader.qml —— 面板与对话框中带标题的分节标题
// ============================================================================
import QtQuick
import TCTools.BuildWeb

Item {
    id: root

    property string title: ""
    property string subtitle: ""
    property string trailingText: ""
    property bool uppercase: false
    property bool collapsible: false
    property bool expanded: true

    signal trailingClicked()
    signal toggled()

    implicitHeight: column.implicitHeight

    Column {
        id: column
        width: parent.width
        spacing: 2

        Item {
            width: parent.width
            height: Math.max(titleLabel.implicitHeight, trailingLabel.implicitHeight)

            Text {
                id: titleLabel
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: (root.collapsible ? (root.expanded ? "\u25BE  " : "\u25B8  ") : "")
                      + (root.uppercase ? root.title.toUpperCase() : root.title)
                font.family: Theme.fontFamily
                font.pixelSize: root.uppercase ? Theme.font.sizeCaption2
                                               : Theme.font.sizeFootnote
                font.weight: Theme.font.weightSemibold
                font.letterSpacing: root.uppercase ? 0.8 : 0
                color: Theme.colour.textTertiary
                elide: Text.ElideRight
                width: parent.width - trailingLabel.width - Theme.space.sm
            }

            Text {
                id: trailingLabel
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: root.trailingText !== ""
                text: root.trailingText
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.sizeCaption
                color: Theme.colour.accent

                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -6
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.trailingClicked()
                }
            }
        }

        Text {
            visible: root.subtitle !== "" && (!root.collapsible || root.expanded)
            width: parent.width
            text: root.subtitle
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.sizeCaption
            color: Theme.colour.textTertiary
            wrapMode: Text.WordWrap
            lineHeight: 1.35
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.collapsible
        cursorShape: root.collapsible ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: root.toggled()
    }
}
