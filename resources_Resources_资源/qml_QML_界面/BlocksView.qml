// ============================================================================
//  BlocksView.qml -- the Scratch-style programming workspace
//  BlocksView.qml —— 类 Scratch 积木编程工作区
// ============================================================================
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import TCTools.BuildWeb

Item {
    id: root

    property string blockFilter: ""

    function appendBlock(blockType) {
        var scriptId = Blocks.selectedScriptId
        if (scriptId === "") {
            scriptId = Blocks.addScript("events.start")
            if (scriptId === "")
                return ""
        }
        return Blocks.insertBlock(blockType, scriptId, "", "", true, -1)
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // -------------------------------------------------------------------
        //  Toolbox / 工具箱
        // -------------------------------------------------------------------
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 250
            color: Theme.colour.surfaceSunken

            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.colour.hairline
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.space.panelHeader + Theme.space.md

                    AppTextField {
                        anchors.fill: parent
                        anchors.margins: Theme.space.sm
                        placeholderText: Tr.t("blocks.searchPlaceholder")
                        onTextChanged: root.blockFilter = text
                    }
                }

                AppSwitch {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.space.md
                    Layout.rightMargin: Theme.space.md
                    Layout.bottomMargin: Theme.space.xs
                    text: Tr.t("blocks.showAdvanced")
                    compact: true
                    checked: Blocks.showAdvanced
                    onToggled: Blocks.showAdvanced = checked
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Theme.colour.hairline
                }

                ListView {
                    id: toolboxList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 3
                    model: ListModel { id: toolboxModel }
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    //  Flattens the category tree into header/entry rows so the
                    //  view needs no proxy model and filtering stays local.
                    //  将分类树扁平化为标题行与条目行，视图因此无需代理模型，
                    //  过滤逻辑也保持在本文件内。
                    function rebuild() {
                        toolboxModel.clear()
                        if (root.blockFilter !== "") {
                            var hits = Blocks.searchBlocks(root.blockFilter)
                            for (var h = 0; h < hits.length; ++h) {
                                toolboxModel.append({
                                    kind: "block",
                                    blockType: hits[h].type,
                                    label: App.chinese ? hits[h].labelZh : hits[h].labelEn,
                                    colour: hits[h].colour !== undefined
                                            ? hits[h].colour
                                            : Theme.blockCategoryColour(hits[h].category)
                                })
                            }
                            return
                        }
                        var cats = Blocks.toolbox
                        for (var c = 0; c < cats.length; ++c) {
                            var cat = cats[c]
                            toolboxModel.append({
                                kind: "header",
                                blockType: "",
                                label: App.chinese ? cat.labelZh : cat.labelEn,
                                colour: cat.colour
                            })
                            for (var i = 0; i < cat.blocks.length; ++i) {
                                var blk = cat.blocks[i]
                                toolboxModel.append({
                                    kind: "block",
                                    blockType: blk.type,
                                    label: App.chinese ? blk.labelZh : blk.labelEn,
                                    colour: blk.colour !== undefined ? blk.colour : cat.colour
                                })
                            }
                        }
                    }

                    Connections {
                        target: Blocks
                        function onToolboxChanged() { toolboxList.rebuild() }
                    }
                    Connections {
                        target: root
                        function onBlockFilterChanged() { toolboxList.rebuild() }
                    }

                    Component.onCompleted: rebuild()

                    delegate: Item {
                        id: toolboxRow
                        required property string kind
                        required property string blockType
                        required property string label
                        required property string colour

                        width: toolboxList.width
                        height: toolboxRow.kind === "header" ? 24 : 30

                        Text {
                            visible: toolboxRow.kind === "header"
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.space.md
                            anchors.verticalCenter: parent.verticalCenter
                            text: toolboxRow.label.toUpperCase()
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.font.sizeCaption2
                            font.weight: Theme.font.weightSemibold
                            font.letterSpacing: 0.7
                            color: toolboxRow.colour
                        }

                        Rectangle {
                            id: chip
                            visible: toolboxRow.kind === "block"
                            anchors.fill: parent
                            anchors.leftMargin: Theme.space.sm
                            anchors.rightMargin: Theme.space.sm
                            radius: Theme.radius.sm
                            color: toolboxRow.colour
                            opacity: chipMouse.drag.active ? 0.9
                                   : (chipHover.hovered ? 0.92 : 0.82)

                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.space.sm
                                anchors.rightMargin: Theme.space.sm
                                text: toolboxRow.label.replace(/%[1-9]/g, "\u25CB")
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.font.sizeCaption
                                font.weight: Theme.font.weightMedium
                                color: "#FFFFFF"
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }

                            HoverHandler { id: chipHover }

                            MouseArea {
                                id: chipMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.OpenHandCursor
                                drag.target: chipDrag
                                drag.threshold: 6
                                onClicked: root.appendBlock(toolboxRow.blockType)
                            }

                            //  Drag source for dropping onto the script canvas.
                            //  拖拽源，用于把积木放到脚本画布上。
                            Item {
                                id: chipDrag
                                width: 1
                                height: 1
                                Drag.active: chipMouse.drag.active
                                Drag.keys: ["tcbw-block"]
                                Drag.supportedActions: Qt.CopyAction
                                Drag.dragData: { "text/plain": toolboxRow.blockType }
                                Drag.hotSpot.x: 0
                                Drag.hotSpot.y: 0
                            }
                        }
                    }
                }
            }
        }

        // -------------------------------------------------------------------
        //  Script canvas / 脚本画布
        // -------------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.colour.canvasBackdrop
            clip: true

            DropArea {
                anchors.fill: parent
                keys: ["tcbw-block"]
                onEntered: function (drag) { drag.accept(Qt.CopyAction) }
                onDropped: function (drop) {
                    var blockType = drop.getDataAsString("text/plain")
                    if (blockType !== "")
                        root.appendBlock(blockType)
                }
            }

            Flickable {
                anchors.fill: parent
                anchors.margins: Theme.space.lg
                contentWidth: Math.max(width, scriptColumn.width + Theme.space.xxl)
                contentHeight: Math.max(height, scriptColumn.height + Theme.space.xxl)
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                Column {
                    id: scriptColumn
                    width: Math.max(360, parent.width - Theme.space.xxl)
                    spacing: Theme.space.lg

                    Repeater {
                        model: Blocks.scripts

                        delegate: Rectangle {
                            id: scriptCard
                            required property var modelData

                            width: scriptColumn.width
                            height: Math.max(90, blockStack.height + 34)
                            radius: Theme.radius.lg
                            color: Theme.colour.surface
                            border.width: scriptCard.modelData.id === Blocks.selectedScriptId ? 2 : 1
                            border.color: scriptCard.modelData.id === Blocks.selectedScriptId
                                          ? Theme.colour.accent : Theme.colour.separator

                            Rectangle {
                                id: scriptHeader
                                width: parent.width
                                height: 30
                                radius: Theme.radius.lg
                                color: scriptCard.modelData.colour !== undefined
                                       ? scriptCard.modelData.colour : Theme.colour.accent
                                opacity: 0.92

                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    width: parent.width
                                    height: parent.radius
                                    color: parent.color
                                }

                                Text {
                                    anchors.left: parent.left
                                    anchors.leftMargin: Theme.space.md
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "when " + scriptCard.modelData.eventType
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.font.sizeCaption
                                    font.weight: Theme.font.weightSemibold
                                    color: "#FFFFFF"
                                }

                                AppIconButton {
                                    anchors.right: parent.right
                                    anchors.rightMargin: Theme.space.xs
                                    anchors.verticalCenter: parent.verticalCenter
                                    glyph: "\u2715"
                                    buttonSize: 20
                                    tooltip: Tr.t("blocks.deleteScript")
                                    onClicked: Blocks.removeScript(scriptCard.modelData.id)
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    anchors.rightMargin: 28
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: Blocks.selectScript(scriptCard.modelData.id)
                                }
                            }

                            Column {
                                id: blockStack
                                anchors.top: scriptHeader.bottom
                                anchors.topMargin: Theme.space.sm
                                anchors.left: parent.left
                                anchors.leftMargin: Theme.space.md
                                anchors.right: parent.right
                                anchors.rightMargin: Theme.space.md
                                spacing: 4

                                Text {
                                    visible: scriptCard.modelData.blockCount === 0
                                    text: Tr.t("blocks.emptyScript")
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.font.sizeCaption
                                    color: Theme.colour.textTertiary
                                }

                                Repeater {
                                    model: scriptCard.modelData.id === Blocks.selectedScriptId
                                           ? Blocks.selectedScriptBlocks : []

                                    delegate: BlockItem {
                                        required property var modelData
                                        width: blockStack.width
                                        blockData: modelData
                                        indentLevel: modelData.depth !== undefined
                                                     ? modelData.depth : 0
                                    }
                                }
                            }
                        }
                    }

                    AppEmptyState {
                        width: parent.width
                        visible: Blocks.scripts.length === 0
                        glyph: "\u25A4"
                        title: Tr.t("blocks.noScripts")
                        detail: Tr.t("blocks.dragHint")
                        actionText: Tr.t("blocks.newScript")
                        onActionTriggered: Blocks.addScript("events.start")
                    }
                }
            }

            //  Hint strip / 提示条
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.space.md
                width: hintText.implicitWidth + Theme.space.xl
                height: 26
                radius: Theme.radius.pill
                color: Theme.colour.chrome
                border.width: 1
                border.color: Theme.colour.separator

                Text {
                    id: hintText
                    anchors.centerIn: parent
                    text: Tr.t("blocks.snapHint")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.sizeCaption
                    color: Theme.colour.textTertiary
                }
            }
        }
    }
}
