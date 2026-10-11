// ============================================================================
//  BlockItem.qml -- one rendered block instance
//  BlockItem.qml —— 单个积木实例的渲染
//
//  Slot order matters. BlockRegistry numbers the `%1`, `%2`, ... placeholders in
//  its labels in *declaration order*: every field first, then every input socket,
//  then every statement branch. This renderer therefore builds its segment list
//  from exactly that order, so a placeholder can never point at the wrong slot.
//  槽位顺序很重要。BlockRegistry 按*声明顺序*为标签中的 `%1`、`%2`……占位符编号：
//  先是全部字段，然后是全部输入槽，最后是全部语句分支。因此本渲染器严格按该顺序
//  构建片段列表，使占位符绝不会指向错误的槽位。
// ============================================================================
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import TCTools.BuildWeb

Rectangle {
    id: root

    //  `blockData` is the QVariantMap produced by BlocksController.
    //  `blockData` 是 BlocksController 产生的 QVariantMap。
    property var blockData: ({})
    property int indentLevel: 0
    property bool selected: false

    readonly property string shape: blockData.shape !== undefined ? blockData.shape : "Stack"
    readonly property bool isReporter: shape === "Reporter" || shape === "Boolean"
                                       || shape === "Round"
    readonly property bool isContainer: shape === "CShape" || shape === "EShape"
    readonly property string bodyColour: blockData.colour !== undefined
                                         ? blockData.colour
                                         : Theme.blockCategoryColour("ui")
    readonly property string blockId: blockData.id !== undefined ? blockData.id : ""

    signal fieldEdited(string blockId, string fieldName, string value)
    signal blockClicked(string blockId)

    width: parent !== null ? parent.width : 240
    height: contentColumn.implicitHeight + (isContainer ? 22 : 12)
    radius: isReporter ? height / 2 : Theme.radius.sm
    color: bodyColour
    opacity: blockData.enabled === false ? 0.5 : 1
    border.width: selected || blockData.selected === true ? 2 : 0
    border.color: "#FFFFFF"

    ColumnLayout {
        id: contentColumn
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        anchors.topMargin: 6
        spacing: 4

        // --- Header line: label text interleaved with slots -----------------
        // --- 标题行：标签文本与槽位交错排列 ---------------------------------
        Flow {
            Layout.fillWidth: true
            spacing: 4

            Repeater {
                model: root.buildSegments()

                delegate: Item {
                    id: segmentItem
                    required property var modelData

                    width: modelData.kind === "label" ? segmentLabel.implicitWidth
                         : modelData.kind === "field" ? 92
                         : socketRow.visible ? socketRow.implicitWidth : 96
                    height: 22

                    // --- Literal label text ------------------------------------
                    Text {
                        id: segmentLabel
                        visible: segmentItem.modelData.kind === "label"
                        anchors.verticalCenter: parent.verticalCenter
                        text: segmentItem.modelData.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.sizeCaption
                        font.weight: Theme.font.weightMedium
                        color: "#FFFFFF"
                    }

                    // --- Inline field (dropdown or free text) -------------------
                    AppComboBox {
                        visible: segmentItem.modelData.kind === "field"
                                 && segmentItem.modelData.options !== undefined
                                 && segmentItem.modelData.options.length > 0
                        anchors.fill: parent
                        compact: true
                        model: segmentItem.modelData.options
                        Component.onCompleted: selectValue(segmentItem.modelData.value)
                        onActivated: root.fieldEdited(root.blockId, segmentItem.modelData.name,
                                                      currentValue())
                    }

                    AppTextField {
                        visible: segmentItem.modelData.kind === "field"
                                 && (segmentItem.modelData.options === undefined
                                     || segmentItem.modelData.options.length === 0)
                        anchors.fill: parent
                        text: segmentItem.modelData.value !== undefined
                              ? segmentItem.modelData.value : ""
                        prefixText: segmentItem.modelData.prefix !== undefined
                                    ? segmentItem.modelData.prefix : ""
                        onEditingFinished: root.fieldEdited(root.blockId,
                                                            segmentItem.modelData.name, text)
                    }

                    // --- Input socket ------------------------------------------
                    //  A connected socket renders the connected block; an empty
                    //  socket renders its shadow value, which is what the
                    //  compiler uses when the socket is left alone.
                    //  已连接的槽渲染所连接的积木；空槽渲染其影子值，也就是编译器在
                    //  槽未被改动时所使用的值。
                    RowLayout {
                        id: socketRow
                        visible: segmentItem.modelData.kind === "input"
                        anchors.fill: parent
                        spacing: 3

                        Rectangle {
                            Layout.alignment: Qt.AlignVCenter
                            width: 14
                            height: 14
                            radius: 7
                            color: Qt.rgba(1, 1, 1, 0.22)
                            border.width: 1
                            border.color: Qt.rgba(1, 1, 1, 0.55)

                            Text {
                                anchors.centerIn: parent
                                text: segmentItem.modelData.hasBlocks ? "\u25CF" : "\u25CB"
                                font.pixelSize: 7
                                color: "#FFFFFF"
                            }
                        }

                        AppTextField {
                            Layout.fillWidth: true
                            visible: !segmentItem.modelData.hasBlocks
                            text: segmentItem.modelData.value !== undefined
                                  ? segmentItem.modelData.value : ""
                            placeholderText: segmentItem.modelData.name
                            onEditingFinished: root.fieldEdited(root.blockId,
                                                                segmentItem.modelData.name,
                                                                text)
                        }
                    }
                }
            }
        }

        // --- Nested statement branches --------------------------------------
        // --- 嵌套语句分支 ---------------------------------------------------
        Repeater {
            model: root.isContainer && root.blockData.statements !== undefined
                   ? root.blockData.statements : []

            delegate: ColumnLayout {
                required property var modelData
                Layout.fillWidth: true
                Layout.leftMargin: 8
                spacing: 4

                Text {
                    text: App.chinese ? modelData.labelZh : modelData.labelEn
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.sizeCaption2
                    color: "#FFFFFF"
                    opacity: 0.85
                }

                Repeater {
                    model: modelData.blocks !== undefined ? modelData.blocks : []

                    delegate: BlockItem {
                        required property var modelData
                        Layout.fillWidth: true
                        blockData: modelData
                        indentLevel: root.indentLevel + 1
                        onFieldEdited: function (blockId, fieldName, value) {
                            root.fieldEdited(blockId, fieldName, value)
                        }
                        onBlockClicked: function (blockId) {
                            root.blockClicked(blockId)
                        }
                    }
                }

                Text {
                    visible: modelData.blocks === undefined || modelData.blocks.length === 0
                    text: "\u22EF"
                    font.pixelSize: 10
                    color: "#FFFFFF"
                    opacity: 0.6
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        //  Only the left button selects; a right click is reserved for a future
        //  context menu, so it must not fall through to selection.
        //  仅左键用于选中；右键保留给未来的上下文菜单，因此不应触发选中。
        acceptedButtons: Qt.LeftButton
        onClicked: root.blockClicked(root.blockId)
    }

    //  Splits the block label into literal text and slot placeholders using the
    //  registry's declaration order: fields, then inputs, then branches.
    //  按注册表的声明顺序（字段、输入、分支）把积木标签拆分为字面文本与槽位占位符。
    function buildSegments() {
        const segments = []
        const label = App.chinese ? (blockData.labelZh || "") : (blockData.labelEn || "")
        const fields = blockData.fields !== undefined ? blockData.fields : []
        const inputs = blockData.inputs !== undefined ? blockData.inputs : []
        const statements = blockData.statements !== undefined ? blockData.statements : []

        //  One slot entry per declaration, in registry order.
        //  每个声明对应一个槽位条目，按注册表顺序。
        const slots = []
        for (let i = 0; i < fields.length; ++i) {
            const field = fields[i]
            slots.push({
                kind: "field",
                name: field.name,
                value: field.value !== undefined ? field.value : field.defaultValue,
                options: field.options,
                optionLabelsZh: field.optionLabelsZh,
                prefix: field.prefix !== undefined ? field.prefix : ""
            })
        }
        for (let i = 0; i < inputs.length; ++i) {
            const input = inputs[i]
            slots.push({
                kind: "input",
                name: input.name,
                value: input.value !== undefined ? input.value : input.shadowValue,
                hasBlocks: input.connected === true
                           || (input.blocks !== undefined && input.blocks.length > 0)
            })
        }
        for (let i = 0; i < statements.length; ++i) {
            const branch = statements[i]
            slots.push({
                kind: "branch",
                name: branch.name,
                //  Branch names are already rendered by the statement repeater,
                //  so the header only contributes their label text.
                //  分支名称已由语句重复器渲染，因此标题只贡献其标签文本。
                text: App.chinese ? branch.labelZh : branch.labelEn
            })
        }

        const parts = label.split(/(%[1-9])/)
        for (let i = 0; i < parts.length; ++i) {
            const part = parts[i]
            if (part === "")
                continue
            const match = /^%([1-9])$/.exec(part)
            if (match === null) {
                segments.push({ kind: "label", text: part })
                continue
            }
            const index = parseInt(match[1], 10) - 1
            if (index < 0 || index >= slots.length) {
                segments.push({ kind: "label", text: "\u25CB" })
                continue
            }
            const slot = slots[index]
            if (slot.kind === "branch") {
                //  The branch body is drawn by the statement repeater below.
                //  分支主体由下方的语句重复器绘制。
                segments.push({ kind: "label", text: slot.text })
            } else {
                segments.push(slot)
            }
        }
        return segments
    }
}
