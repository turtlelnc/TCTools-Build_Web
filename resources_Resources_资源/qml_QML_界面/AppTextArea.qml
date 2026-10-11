// ============================================================================
//  AppTextArea.qml -- Apple-style multi-line text area
//  AppTextArea.qml —— Apple 风格多行文本框
// ============================================================================
import QtQuick
import QtQuick.Controls.Basic
import TCTools.BuildWeb

ScrollView {
    id: control

    property alias text: area.text
    property alias placeholderText: area.placeholderText
    property bool monospaced: false
    property int areaHeight: 120
    property bool wrapText: true

    implicitHeight: areaHeight + 2
    clip: true

    TextArea {
        id: area
        placeholderTextColor: Theme.colour.textTertiary
        color: Theme.colour.text
        font.family: control.monospaced ? Theme.fontFamilyMono : Theme.fontFamily
        font.pixelSize: control.monospaced ? Theme.font.sizeFootnote : Theme.font.sizeBody
        selectByMouse: true
        persistentSelection: true
        wrapMode: control.wrapText ? TextEdit.Wrap : TextEdit.NoWrap
        leftPadding: Theme.space.sm
        rightPadding: Theme.space.sm
        topPadding: Theme.space.sm
        bottomPadding: Theme.space.sm

        background: Rectangle {
            radius: Theme.radius.md
            color: control.monospaced ? Theme.colour.codeBackground : Theme.colour.fill
            border.width: area.activeFocus ? 2 : 1
            border.color: area.activeFocus ? Theme.colour.accent : Theme.colour.separator
        }
    }
}
