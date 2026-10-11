// ============================================================================
//  AppSwitch.qml -- Apple-style toggle switch
//  AppSwitch.qml —— Apple 风格开关
// ============================================================================
import QtQuick
import QtQuick.Controls.Basic
import TCTools.BuildWeb

Switch {
    id: control

    property string label: ""
    property string description: ""
    property bool compact: false

    implicitHeight: compact ? 20 : 22
    implicitWidth: compact ? 36 : 42
    hoverEnabled: true

    indicator: Rectangle {
        id: track
        implicitWidth: control.compact ? 36 : 42
        implicitHeight: control.compact ? 20 : 22
        x: 0
        y: (control.height - height) / 2
        radius: height / 2
        color: !control.enabled ? Theme.colour.fill
             : control.checked ? Theme.colour.accent
             : Theme.colour.fillStrong
        border.width: control.visualFocus ? 2 : 0
        border.color: Theme.colour.accent

        Behavior on color {
            ColorAnimation { duration: Theme.motion.fast; easing.type: Theme.motion.easingStandard }
        }

        Rectangle {
            id: knob
            width: control.checked ? parent.height - 4 : parent.height - 4
            height: width
            radius: width / 2
            y: 2
            x: control.checked ? parent.width - width - 2 : 2
            color: control.enabled ? "#FFFFFF" : Theme.colour.surfaceRaised

            Behavior on x {
                NumberAnimation {
                    duration: Theme.motion.normal
                    easing.type: Theme.motion.easingSpring
                }
            }
        }
    }

    //  When a label is supplied the switch grows into a full setting row.
    //  提供标签时，开关会扩展为完整的设置行。
    contentItem: Item {
        implicitWidth: control.label !== "" ? labelColumn.implicitWidth + Theme.space.lg : 0
        implicitHeight: control.label !== "" ? labelColumn.implicitHeight : 0
        visible: control.label !== ""

        Column {
            id: labelColumn
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1

            Text {
                text: control.label
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.sizeBody
                color: control.enabled ? Theme.colour.text : Theme.colour.textTertiary
            }

            Text {
                visible: control.description !== ""
                text: control.description
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.sizeCaption
                color: Theme.colour.textTertiary
            }
        }
    }
}
