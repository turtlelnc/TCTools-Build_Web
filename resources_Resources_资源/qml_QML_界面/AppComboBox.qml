// ============================================================================
//  AppComboBox.qml -- Apple-style pop-up selection control
//  AppComboBox.qml —— Apple 风格下拉选择控件
// ============================================================================
import QtQuick
import QtQuick.Controls.Basic
import TCTools.BuildWeb

ComboBox {
    id: control

    property string label: ""
    property bool compact: false
    property string leadingGlyph: ""

    implicitHeight: compact ? 24 : Theme.size.controlHeight
    implicitWidth: 180
    leftPadding: Theme.space.sm + (leadingGlyph !== "" ? 18 : 0)
    rightPadding: Theme.space.xl
    font.family: Theme.fontFamily
    font.pixelSize: compact ? Theme.font.sizeCaption : Theme.font.sizeBody

    //  Accepts a plain string list or a list of { text, value, description }
    //  objects, so call sites do not need a separate model wrapper.
    //  接受普通字符串列表或 { text, value, description } 对象列表，因此调用处
    //  无需额外的模型包装。
    function currentValue() {
        if (model === undefined || model === null)
            return undefined
        if (typeof model === "object" && model.length !== undefined) {
            var item = model[currentIndex]
            if (item !== undefined && item !== null && typeof item === "object"
                    && item.value !== undefined)
                return item.value
            return item
        }
        return currentIndex
    }

    function selectValue(value) {
        if (model === undefined || model === null)
            return false
        if (typeof model === "object" && model.length !== undefined) {
            for (var i = 0; i < model.length; ++i) {
                var item = model[i]
                var candidate = (item !== null && typeof item === "object")
                              ? item.value : item
                if (candidate === value) {
                    currentIndex = i
                    return true
                }
            }
        }
        return false
    }

    contentItem: Text {
        leftPadding: control.leftPadding
        rightPadding: control.rightPadding
        text: control.displayText
        font: control.font
        color: control.enabled ? Theme.colour.text : Theme.colour.textTertiary
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Text {
        x: control.width - width - Theme.space.sm
        y: (control.height - height) / 2
        text: "\u25BE"
        font.pixelSize: 10
        color: Theme.colour.textTertiary
    }

    background: Rectangle {
        radius: Theme.radius.md
        color: !control.enabled ? Theme.colour.surfaceSunken
             : control.pressed ? Theme.colour.fillSecondary
             : control.hovered ? Theme.colour.fillSecondary
             : Theme.colour.fill
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? Theme.colour.accent : Theme.colour.separator
    }

    popup: Popup {
        y: control.height + 4
        width: Math.max(control.width, 160)
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 320)
        padding: 4

        background: Rectangle {
            radius: Theme.radius.lg
            color: Theme.colour.surfaceRaised
            border.width: 1
            border.color: Theme.colour.separator
        }

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator { }
        }
    }

    delegate: ItemDelegate {
        id: delegateItem
        required property int index
        required property var modelData

        width: control.popup.width - 8
        height: Math.max(Theme.size.controlHeight,
                         (control.font.pixelSize + 6)
                         + (descriptionText.visible ? control.font.pixelSize + 2 : 0))
        highlighted: control.highlightedIndex === index
        hoverEnabled: true

        readonly property string labelText: (modelData !== null && typeof modelData === "object")
                                          ? (modelData.text !== undefined ? modelData.text : "") : modelData
        readonly property string detailText: (modelData !== null && typeof modelData === "object"
                                              && modelData.description !== undefined)
                                             ? modelData.description : ""

        background: Rectangle {
            radius: Theme.radius.sm
            color: delegateItem.highlighted || delegateItem.hovered
                   ? Theme.colour.accentSoft : "transparent"
        }

        contentItem: Column {
            spacing: 0
            anchors.verticalCenter: parent.verticalCenter
            leftPadding: Theme.space.sm

            Text {
                text: delegateItem.labelText
                font: control.font
                color: Theme.colour.text
                elide: Text.ElideRight
                width: delegateItem.width - Theme.space.lg
            }

            Text {
                id: descriptionText
                visible: delegateItem.detailText !== ""
                text: delegateItem.detailText
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.sizeCaption
                color: Theme.colour.textTertiary
                elide: Text.ElideRight
                width: delegateItem.width - Theme.space.lg
            }
        }

        onClicked: {
            control.currentIndex = index
            control.popup.close()
        }
    }
}
