// ============================================================================
//  AppToast.qml -- transient notification banner
//  AppToast.qml —— 瞬时通知横幅
// ============================================================================
import QtQuick
import TCTools.BuildWeb

Rectangle {
    id: toast

    property string message: ""
    property string tone: "info"        // info | success | warning | danger
    property int duration: 3200
    property bool shown: false

    readonly property color _tone: {
        switch (tone) {
        case "success": return Theme.colour.success
        case "warning": return Theme.colour.warning
        case "danger":  return Theme.colour.danger
        default:        return Theme.colour.info
        }
    }

    implicitWidth: Math.min(row.implicitWidth + Theme.space.xl, 520)
    implicitHeight: Math.max(40, row.implicitHeight + Theme.space.lg)
    radius: Theme.radius.lg
    color: Theme.colour.surfaceRaised
    border.width: 1
    border.color: Theme.alpha(toast._tone, 0.4)

    opacity: shown ? 1 : 0
    visible: opacity > 0.01
    y: shown ? 0 : -12

    Behavior on opacity {
        NumberAnimation { duration: Theme.motion.normal; easing.type: Theme.motion.easingEnter }
    }
    Behavior on y {
        NumberAnimation { duration: Theme.motion.normal; easing.type: Theme.motion.easingEnter }
    }

    function show(text, kind) {
        message = text
        if (kind !== undefined && kind !== "")
            tone = kind
        shown = true
        hideTimer.restart()
    }

    function hide() {
        shown = false
    }

    Timer {
        id: hideTimer
        interval: toast.duration
        repeat: false
        onTriggered: toast.hide()
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.space.sm

        Rectangle {
            width: 8
            height: 8
            radius: 4
            color: toast._tone
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            text: toast.message
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.sizeFootnote
            color: Theme.colour.text
            wrapMode: Text.WordWrap
            width: Math.min(implicitWidth, 440)
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: toast.hide()
    }
}
