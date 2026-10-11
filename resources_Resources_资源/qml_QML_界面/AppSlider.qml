// ============================================================================
//  AppSlider.qml -- Apple-style continuous slider
//  AppSlider.qml —— Apple 风格连续滑块
// ============================================================================
import QtQuick
import QtQuick.Controls.Basic
import TCTools.BuildWeb

Slider {
    id: control

    implicitHeight: 22

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: (control.height - height) / 2
        width: 16
        height: 16
        radius: width / 2
        color: "#FFFFFF"
        border.width: 1
        border.color: Theme.colour.separatorStrong
    }

    background: Rectangle {
        x: control.leftPadding
        y: (control.height - height) / 2
        width: control.availableWidth
        height: 4
        radius: 2
        color: Theme.colour.fillStrong

        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: parent.radius
            color: Theme.colour.accent
        }
    }
}
