// ============================================================================
//  SidebarPanel.qml -- contextual left sidebar (pages, layers, block scripts)
//  SidebarPanel.qml —— 上下文左侧栏（页面、图层、积木脚本）
// ============================================================================
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import TCTools.BuildWeb

Rectangle {
    id: root

    property string mode: "design"

    color: Theme.colour.sidebar
    border.width: 0

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.colour.hairline
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // -------------------------------------------------------------------
        //  Header switches with the active mode
        //  标题随当前模式变化
        // -------------------------------------------------------------------
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.space.panelHeader + Theme.space.sm

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.space.md
                anchors.rightMargin: Theme.space.sm
                spacing: Theme.space.sm

                Text {
                    Layout.fillWidth: true
                    text: {
                        switch (root.mode) {
                        case "blocks":  return Tr.t("blocks.scripts")
                        case "ai":      return Tr.t("panel.pages")
                        case "backend": return Tr.t("panel.endpoints")
                        default:        return Tr.t("panel.pages")
                        }
                    }
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.sizeFootnote
                    font.weight: Theme.font.weightSemibold
                    color: Theme.colour.textTertiary
                    elide: Text.ElideRight
                }

                AppIconButton {
                    visible: root.mode !== "blocks" && root.mode !== "backend"
                    glyph: "\uFF0B"
                    tooltip: Tr.t("canvas.addPage")
                    buttonSize: 22
                    onClicked: {
                        var id = App.addPage("")
                        if (id !== "")
                            Builder.selectNode("")
                    }
                }

                AppIconButton {
                    visible: root.mode === "blocks"
                    glyph: "\uFF0B"
                    tooltip: Tr.t("blocks.newScript")
                    buttonSize: 22
                    onClicked: Blocks.addScript("events.start")
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.colour.hairline
        }

        // -------------------------------------------------------------------
        //  Pages + layers
        //  页面 + 图层
        // -------------------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: Theme.space.sm
            spacing: Theme.space.md
            visible: root.mode !== "blocks"

            ListView {
                id: pageList
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 160)
                Layout.leftMargin: Theme.space.sm
                Layout.rightMargin: Theme.space.sm
                clip: true
                model: App.pages
                spacing: 2
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Rectangle {
                    id: pageRow
                    required property var modelData
                    width: pageList.width
                    height: 30
                    radius: Theme.radius.sm
                    color: pageRow.modelData.id === App.currentPageId
                           ? Theme.colour.accentSoft
                           : (pageHover.hovered ? Theme.colour.fill : "transparent")

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.space.sm
                        anchors.rightMargin: Theme.space.xs
                        spacing: Theme.space.xs

                        Text {
                            text: pageRow.modelData.isHome ? "\u2302" : "\u25A1"
                            font.pixelSize: 12
                            color: pageRow.modelData.id === App.currentPageId
                                   ? Theme.colour.accent : Theme.colour.textTertiary
                        }

                        Text {
                            Layout.fillWidth: true
                            text: pageRow.modelData.title !== ""
                                  ? pageRow.modelData.title : pageRow.modelData.name
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.font.sizeFootnote
                            color: Theme.colour.text
                            elide: Text.ElideRight
                        }

                        Text {
                            text: String(pageRow.modelData.nodeCount)
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.font.sizeCaption2
                            color: Theme.colour.textTertiary
                        }
                    }

                    HoverHandler { id: pageHover }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        cursorShape: Qt.PointingHandCursor
                        onClicked: App.currentPageId = pageRow.modelData.id
                        onPressAndHold: renamePageDialog.openFor(pageRow.modelData.id,
                                                                 pageRow.modelData.name)
                    }
                }
            }

            AppSectionHeader {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space.md
                Layout.rightMargin: Theme.space.md
                title: Tr.t("panel.layers")
                uppercase: true
                trailingText: Builder.hasSelection ? Tr.t("action.delete") : ""
                onTrailingClicked: Builder.deleteSelected()
            }

            ListView {
                id: layerList
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: Theme.space.xs
                Layout.rightMargin: Theme.space.xs
                clip: true
                model: Builder.layerTree
                spacing: 0
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Item {
                    id: layerRow
                    required property var modelData
                    width: layerList.width
                    height: 26

                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: layerRow.modelData.depth * 12
                        radius: Theme.radius.xs
                        color: layerRow.modelData.selected
                               ? Theme.colour.accentSoft
                               : (layerHover.hovered ? Theme.colour.fill : "transparent")
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: layerRow.modelData.depth * 12 + Theme.space.sm
                        anchors.rightMargin: Theme.space.xs
                        spacing: Theme.space.xs

                        Text {
                            text: layerRow.modelData.selected ? "\u25B8" : ""
                            font.pixelSize: 9
                            color: Theme.colour.accent
                        }

                        Text {
                            Layout.fillWidth: true
                            text: App.chinese ? layerRow.modelData.labelZh
                                             : layerRow.modelData.labelEn
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.font.sizeFootnote
                            color: layerRow.modelData.hidden ? Theme.colour.textTertiary
                                                             : Theme.colour.text
                            elide: Text.ElideRight
                            opacity: layerRow.modelData.hidden ? 0.5 : 1
                        }

                        Text {
                            visible: layerRow.modelData.locked
                            text: "\u26BF"
                            font.pixelSize: 10
                            color: Theme.colour.textTertiary
                        }
                    }

                    HoverHandler { id: layerHover }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Builder.selectNode(layerRow.modelData.id)
                    }
                }
            }
        }

        // -------------------------------------------------------------------
        //  Block scripts
        //  积木脚本
        // -------------------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: Theme.space.sm
            spacing: Theme.space.xs
            visible: root.mode === "blocks"

            ListView {
                id: scriptList
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: Theme.space.sm
                Layout.rightMargin: Theme.space.sm
                clip: true
                model: Blocks.scripts
                spacing: 3
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Rectangle {
                    id: scriptRow
                    required property var modelData
                    width: scriptList.width
                    height: 46
                    radius: Theme.radius.md
                    color: scriptRow.modelData.selected
                           ? Theme.colour.accentWash
                           : (scriptHover.hovered ? Theme.colour.fill : Theme.colour.surface)
                    border.width: 1
                    border.color: scriptRow.modelData.selected
                                  ? Theme.colour.accent : Theme.colour.hairline

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.space.sm
                        spacing: Theme.space.sm

                        Rectangle {
                            width: 4
                            height: parent.height
                            radius: 2
                            color: scriptRow.modelData.colour !== undefined
                                   ? scriptRow.modelData.colour : Theme.colour.accent
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            Text {
                                Layout.fillWidth: true
                                text: scriptRow.modelData.name
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.font.sizeFootnote
                                font.weight: Theme.font.weightMedium
                                color: Theme.colour.text
                                elide: Text.ElideRight
                            }

                            Text {
                                Layout.fillWidth: true
                                text: App.chinese ? scriptRow.modelData.previewZh
                                                  : scriptRow.modelData.previewEn
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.font.sizeCaption2
                                color: Theme.colour.textTertiary
                                elide: Text.ElideRight
                            }
                        }

                        AppIconButton {
                            glyph: "\u2715"
                            buttonSize: 20
                            tooltip: Tr.t("blocks.deleteScript")
                            danger: true
                            onClicked: Blocks.removeScript(scriptRow.modelData.id)
                        }
                    }

                    HoverHandler { id: scriptHover }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Blocks.selectScript(scriptRow.modelData.id)
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                Layout.margins: Theme.space.md
                visible: Blocks.scripts.length === 0
                text: Tr.t("blocks.noScripts")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.sizeCaption
                color: Theme.colour.textTertiary
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }

    // -----------------------------------------------------------------------
    //  Inline rename dialog for pages
    //  页面重命名的内联对话框
    // -----------------------------------------------------------------------
    Dialog {
        id: renamePageDialog
        anchors.centerIn: parent
        modal: true
        width: 320
        padding: Theme.space.lg
        title: Tr.t("common.rename")
        property string pageId: ""

        function openFor(id, currentName) {
            pageId = id
            nameField.text = currentName
            open()
        }

        background: Rectangle {
            radius: Theme.radius.xl
            color: Theme.colour.surfaceRaised
            border.width: 1
            border.color: Theme.colour.separator
        }

        contentItem: ColumnLayout {
            spacing: Theme.space.md

            AppTextField {
                id: nameField
                Layout.fillWidth: true
                label: Tr.t("blocks.scriptName")
                onAccepted: renamePageDialog.accept()
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Theme.space.sm

                AppButton {
                    text: Tr.t("common.cancel")
                    variant: "ghost"
                    onClicked: renamePageDialog.close()
                }

                AppButton {
                    text: Tr.t("common.rename")
                    variant: "primary"
                    onClicked: {
                        App.renamePage(renamePageDialog.pageId, nameField.text)
                        renamePageDialog.close()
                    }
                }
            }
        }
    }
}
