// ============================================================================
//  AppTextField.qml -- Apple-style single-line text field
//  AppTextField.qml —— Apple 风格单行文本输入框
// ============================================================================
import QtQuick
import QtQuick.Controls.Basic
import TCTools.BuildWeb

TextField {
    id: control

    property string label: ""
    property string helpText: ""
    property bool monospaced: false
    property bool invalid: false
    property string prefixText: ""
    property string suffixText: ""

    implicitHeight: Theme.size.controlHeight
    leftPadding: Theme.space.sm + (prefixText !== "" ? prefixTextWidth : 0)
    rightPadding: Theme.space.sm + (suffixText !== "" ? suffixTextWidth : 0)
    topPadding: 0
    bottomPadding: 0
    selectByMouse: true
    persistentSelection: true
    placeholderTextColor: Theme.colour.textTertiary
    color: Theme.colour.text
    font.family: monospaced ? Theme.fontFamilyMono : Theme.fontFamily
    font.pixelSize: Theme.font.sizeBody
    passwordCharacter: "\u2022"
    passwordMaskDelay: 0

    readonly property real prefixTextWidth: prefixLabel.implicitWidth
    readonly property real suffixTextWidth: suffixLabel.implicitWidth

    background: Rectangle {
        radius: Theme.radius.md
        color: !control.enabled ? Theme.colour.surfaceSunken
             : control.activeFocus ? Theme.colour.surface
             : Theme.colour.fill
        border.width: control.activeFocus || control.invalid ? 2 : 1
        border.color: control.invalid ? Theme.colour.danger
                    : control.activeFocus ? Theme.colour.accent
                    : Theme.colour.separator

        Behavior on border.color {
            ColorAnimation { duration: Theme.motion.fast; easing.type: Theme.motion.easingStandard }
        }
    }

    //  Leading and trailing fixed text (units, "https://", ...).
    //  前置与后置固定文本（单位、"https://" 等）。
    Text {
        id: prefixLabel
        visible: control.prefixText !== ""
        anchors.left: parent.left
        anchors.leftMargin: Theme.space.sm
        anchors.verticalCenter: parent.verticalCenter
        text: control.prefixText
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.sizeFootnote
        color: Theme.colour.textTertiary
    }

    Text {
        id: suffixLabel
        visible: control.suffixText !== ""
        anchors.right: parent.right
        anchors.rightMargin: Theme.space.sm
        anchors.verticalCenter: parent.verticalCenter
        text: control.suffixText
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.sizeFootnote
        color: Theme.colour.textTertiary
    }

    //  Optional caption above and hint below, rendered by the parent when it
    //  needs a full form row. Kept as properties so AppTextField stays a
    //  drop-in TextField.
    //  可选的标题与提示文字，由父级在需要完整表单行时渲染。以属性形式保留，
    //  使 AppTextField 仍然可以直接替换 TextField。
    ToolTip.visible: control.helpText !== "" && control.hovered && !control.activeFocus
    ToolTip.text: control.helpText
    ToolTip.delay: 800
}
