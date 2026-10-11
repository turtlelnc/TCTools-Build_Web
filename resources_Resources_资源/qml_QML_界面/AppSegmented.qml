// ============================================================================
//  AppSegmented.qml -- Apple-style segmented control
//  AppSegmented.qml —— Apple 风格分段控件
//
//  Segments share equal width, which is both Apple's default appearance for a
//  fixed set of short labels and far more robust than measuring each delegate
//  during layout.
//  各分段等宽：这既是 Apple 对固定短标签集合的默认外观，也比在布局期间逐个
//  测量委托项稳健得多。
// ============================================================================
import QtQuick
import QtQuick.Controls.Basic
import TCTools.BuildWeb

Item {
    id: control

    //  List of { text, value, glyph } entries.
    //  { text, value, glyph } 条目列表。
    property var items: []
    property var currentValue: undefined
    property bool fullWidth: false
    property int itemHeight: 26

    signal activated(var value)

    implicitHeight: itemHeight
    implicitWidth: fullWidth ? parent.width : Math.max(120, track.implicitWidth)

    readonly property int segmentCount: Math.max(1, items.length)
    readonly property int selectedIndex: indexOfValue(currentValue)

    function indexOfValue(value) {
        for (var i = 0; i < items.length; ++i) {
            if (items[i].value === value)
                return i
        }
        return -1
    }

    Rectangle {
        id: track
        anchors.fill: parent
        radius: Theme.radius.md
        color: Theme.colour.fill

        //  Natural width is driven by the widest label plus padding; when
        //  fullWidth is requested the control simply fills its parent.
        //  自然宽度由最宽标签加内边距决定；当要求 fullWidth 时控件直接填满父级。
        implicitWidth: widestLabel.width * control.segmentCount + 4

        Text {
            id: widestLabel
            visible: false
            text: {
                var widest = ""
                for (var i = 0; i < control.items.length; ++i) {
                    var entry = control.items[i]
                    var glyph = (entry.glyph !== undefined && entry.glyph !== "") ? entry.glyph + "  " : ""
                    var candidate = glyph + (entry.text !== undefined ? entry.text : "")
                    if (candidate.length > widest.length)
                        widest = candidate
                }
                return widest
            }
            font.family: Theme.fontFamily
            font.pixelSize: Theme.font.sizeFootnote
            font.weight: Theme.font.weightMedium
        }

        readonly property real segmentWidth: (width - 4) / control.segmentCount

        //  Sliding selection pill / 可滑动选中指示块
        Rectangle {
            id: pill
            visible: control.selectedIndex >= 0
            x: 2 + Math.max(0, control.selectedIndex) * track.segmentWidth
            y: 2
            width: track.segmentWidth
            height: track.height - 4
            radius: Theme.radius.sm - 1
            color: Theme.colour.surface
            border.width: 1
            border.color: Theme.colour.hairline

            Behavior on x {
                NumberAnimation { duration: Theme.motion.fast; easing.type: Theme.motion.easingStandard }
            }
        }

        Row {
            id: segmentRow
            anchors.fill: parent
            anchors.margins: 2
            spacing: 0

            Repeater {
                model: control.items

                delegate: AbstractButton {
                    id: segment
                    required property var modelData
                    required property int index

                    width: track.segmentWidth
                    height: segmentRow.height
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus
                    padding: 0

                    readonly property bool selected: control.currentValue === segment.modelData.value

                    background: Rectangle {
                        radius: Theme.radius.sm - 1
                        color: segment.hovered && !segment.selected && !segment.pressed
                               ? Theme.colour.fillSecondary
                               : "transparent"
                    }

                    contentItem: Text {
                        text: {
                            var glyph = (segment.modelData.glyph !== undefined
                                         && segment.modelData.glyph !== "")
                                      ? segment.modelData.glyph + "  " : ""
                            return glyph + (segment.modelData.text !== undefined
                                            ? segment.modelData.text : "")
                        }
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.sizeFootnote
                        font.weight: segment.selected ? Theme.font.weightMedium
                                                      : Theme.font.weightRegular
                        color: segment.selected ? Theme.colour.text : Theme.colour.textSecondary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        opacity: segment.pressed ? 0.6 : 1
                    }

                    onClicked: {
                        control.currentValue = segment.modelData.value
                        control.activated(segment.modelData.value)
                    }
                }
            }
        }
    }
}
