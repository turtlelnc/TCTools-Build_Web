// ============================================================================
//  DesignView.qml -- palette, canvas and drop handling
//  DesignView.qml —— 素材面板、画布与拖放处理
// ============================================================================
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import TCTools.BuildWeb

Item {
    id: root

    property string elementFilter: ""
    property bool componentsExpanded: true
    property real zoom: 1.0

    //  Inserts a palette element. When something that can hold children is
    //  selected the new element goes inside it; otherwise it is appended to the
    //  page root.
    //  插入素材元素。若当前选中的元素可以容纳子元素，则插入其内部；否则追加到
    //  页面根节点。
    function appendElement(tag) {
        var parentId = ""
        if (Builder.hasSelection) {
            var node = Builder.selectedNode
            if (node !== undefined && node !== null && node.allowsChildren === true)
                parentId = node.id
        }
        return Builder.insertElement(tag, parentId, -1)
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ===================================================================
        //  Element palette / 元素素材面板
        // ===================================================================
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 236
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
                        placeholderText: Tr.t("common.searchPlaceholder")
                        onTextChanged: root.elementFilter = text
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Theme.colour.hairline
                }

                ListView {
                    id: paletteList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 2
                    model: ListModel { id: paletteModel }
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    //  The model mirrors the catalogue, flattened, with header
                    //  rows only when no filter is active. Rebuilding on every
                    //  palette or filter change keeps filtering a pure view
                    //  concern and avoids a proxy model.
                    //  模型是目录的扁平镜像；仅在未过滤时插入分类标题行。在素材或
                    //  过滤条件变化时重建，使过滤保持为纯视图职责，且无需代理模型。
                    function rebuild() {
                        paletteModel.clear()
                        if (root.elementFilter !== "") {
                            var hits = Builder.searchElements(root.elementFilter)
                            for (var h = 0; h < hits.length; ++h) {
                                paletteModel.append({
                                    kind: "item",
                                    tag: hits[h].tag,
                                    label: App.chinese ? hits[h].labelZh : hits[h].labelEn,
                                    description: App.chinese ? hits[h].descriptionZh
                                                             : hits[h].descriptionEn
                                })
                            }
                            return
                        }
                        var groups = Builder.palette
                        for (var g = 0; g < groups.length; ++g) {
                            var group = groups[g]
                            paletteModel.append({
                                kind: "header",
                                tag: "",
                                label: App.chinese ? group.labelZh : group.labelEn,
                                description: ""
                            })
                            for (var i = 0; i < group.items.length; ++i) {
                                var item = group.items[i]
                                paletteModel.append({
                                    kind: "item",
                                    tag: item.tag,
                                    label: App.chinese ? item.labelZh : item.labelEn,
                                    description: App.chinese ? item.descriptionZh
                                                             : item.descriptionEn
                                })
                            }
                        }
                    }

                    Connections {
                        target: Builder
                        function onPaletteChanged() { paletteList.rebuild() }
                    }

                    Connections {
                        target: root
                        function onElementFilterChanged() { paletteList.rebuild() }
                    }

                    Component.onCompleted: rebuild()

                    delegate: Item {
                        id: paletteRow
                        required property string kind
                        required property string tag
                        required property string label
                        required property string description

                        width: paletteList.width
                        height: paletteRow.kind === "header" ? 26 : 30

                        Text {
                            visible: paletteRow.kind === "header"
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.space.md
                            anchors.verticalCenter: parent.verticalCenter
                            text: paletteRow.label.toUpperCase()
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.font.sizeCaption2
                            font.weight: Theme.font.weightSemibold
                            font.letterSpacing: 0.8
                            color: Theme.colour.textTertiary
                        }

                        Rectangle {
                            id: paletteCell
                            visible: paletteRow.kind === "item"
                            anchors.fill: parent
                            anchors.leftMargin: Theme.space.sm
                            anchors.rightMargin: Theme.space.sm
                            radius: Theme.radius.sm
                            color: paletteMouse.drag.active ? Theme.colour.accentSoft
                                   : (paletteHover.hovered ? Theme.colour.fill : "transparent")

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.space.sm
                                anchors.rightMargin: Theme.space.sm
                                spacing: Theme.space.sm

                                Text {
                                    text: "\u25A2"
                                    font.pixelSize: 12
                                    color: Theme.colour.accent
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: paletteRow.label
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.font.sizeFootnote
                                    color: Theme.colour.text
                                    elide: Text.ElideRight
                                }
                            }

                            HoverHandler { id: paletteHover }

                            MouseArea {
                                id: paletteMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.OpenHandCursor
                                drag.target: ghost
                                drag.threshold: 6

                                onClicked: root.appendElement(paletteRow.tag)
                            }
                        }

                        //  Drag ghost. The ParentChange state is the pattern
                        //  used by Qt's own drag-and-drop example: the ghost is
                        //  reparented to the view root while a drag is active
                        //  so it is not clipped by the list.
                        //  拖拽虚影。ParentChange 状态取自 Qt 官方拖放示例：拖动期间
                        //  虚影被重新挂到视图根节点，从而不被列表裁剪。
                        Rectangle {
                            id: ghost
                            visible: paletteMouse.drag.active
                            width: Math.max(120, ghostLabel.implicitWidth + 24)
                            height: 26
                            radius: Theme.radius.sm
                            color: Theme.colour.accent
                            opacity: 0.92
                            z: 999
                            Drag.active: paletteMouse.drag.active
                            Drag.keys: ["tcbw-element"]
                            Drag.supportedActions: Qt.CopyAction
                            Drag.dragData: { "text/plain": paletteRow.tag }
                            Drag.hotSpot.x: 12
                            Drag.hotSpot.y: 13

                            Text {
                                id: ghostLabel
                                anchors.centerIn: parent
                                text: paletteRow.label
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.font.sizeCaption
                                color: Theme.colour.textOnAccent
                            }

                            states: State {
                                when: paletteMouse.drag.active
                                ParentChange { target: ghost; parent: root }
                            }
                        }
                    }
                }

                // -----------------------------------------------------------
                //  Ready-made sections / 现成版式
                // -----------------------------------------------------------
                AppSectionHeader {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.space.md
                    Layout.rightMargin: Theme.space.md
                    Layout.topMargin: Theme.space.sm
                    title: Tr.t("elements.components")
                    uppercase: true
                    collapsible: true
                    expanded: root.componentsExpanded
                    onToggled: root.componentsExpanded = !root.componentsExpanded
                }

                ListView {
                    id: compositeList
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.componentsExpanded
                                           ? Math.min(contentHeight + 4, 210) : 0
                    Layout.leftMargin: Theme.space.sm
                    Layout.rightMargin: Theme.space.sm
                    Layout.bottomMargin: Theme.space.sm
                    visible: root.componentsExpanded
                    clip: true
                    model: Builder.composites
                    spacing: 2
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    delegate: Rectangle {
                        id: compositeRow
                        required property var modelData

                        width: compositeList.width
                        height: 30
                        radius: Theme.radius.sm
                        color: compositeHover.hovered ? Theme.colour.fill : "transparent"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.space.sm
                            anchors.rightMargin: Theme.space.sm
                            spacing: Theme.space.sm

                            Text {
                                text: "\u25A3"
                                font.pixelSize: 12
                                color: Theme.colour.accent
                            }

                            Text {
                                Layout.fillWidth: true
                                text: App.chinese ? compositeRow.modelData.labelZh
                                                  : compositeRow.modelData.labelEn
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.font.sizeFootnote
                                color: Theme.colour.text
                                elide: Text.ElideRight
                            }
                        }

                        HoverHandler { id: compositeHover }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: Builder.insertComposite(compositeRow.modelData.id, "", -1)
                        }
                    }
                }
            }
        }

        // ===================================================================
        //  Canvas / 画布
        // ===================================================================
        Rectangle {
            id: canvasArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.colour.canvasBackdrop
            clip: true

            Flickable {
                id: canvasFlick
                anchors.fill: parent
                anchors.margins: Theme.space.lg
                contentWidth: Math.max(width, pageFrame.width * root.zoom + Theme.space.xxl)
                contentHeight: Math.max(height, pageFrame.height * root.zoom + Theme.space.xxl)
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                Item {
                    id: pageFrame
                    width: Builder.canvasWidth
                    height: Math.max(Builder.canvasHeight, flowColumn.height + Theme.space.xxl)
                    x: Math.max(Theme.space.md, (canvasFlick.width - width * root.zoom) / 2)
                    y: Theme.space.md
                    scale: root.zoom
                    transformOrigin: Item.TopLeft

                    Rectangle {
                        anchors.fill: parent
                        color: Theme.colour.canvas
                        radius: Theme.radius.md
                        border.width: 1
                        border.color: Theme.colour.separator
                    }

                    //  Page-level drop target / 页面级放置目标
                    DropArea {
                        anchors.fill: parent
                        keys: ["tcbw-element"]
                        onEntered: function (drag) { drag.accept(Qt.CopyAction) }
                        onDropped: function (drop) {
                            var tag = drop.getDataAsString("text/plain")
                            if (tag !== "")
                                root.appendElement(tag)
                        }
                    }

                    Column {
                        id: flowColumn
                        width: parent.width - Theme.space.xl
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        anchors.topMargin: Theme.space.lg
                        spacing: 6

                        Repeater {
                            model: Builder.outlineNodes

                            delegate: Rectangle {
                                id: outlineNode
                                required property var modelData

                                width: flowColumn.width
                                height: outlineNode.modelData.isTextual ? 30 : 44
                                radius: Theme.radius.sm
                                color: outlineNode.modelData.id === Builder.selectedNodeId
                                       ? Theme.colour.selectionSoft
                                       : (nodeHover.hovered ? Theme.colour.fill
                                                            : Theme.colour.accentWash)
                                border.width: outlineNode.modelData.id === Builder.selectedNodeId ? 2 : 1
                                border.color: outlineNode.modelData.id === Builder.selectedNodeId
                                              ? Theme.colour.selection : Theme.colour.hairline

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: Theme.space.sm
                                    anchors.rightMargin: Theme.space.sm
                                    spacing: Theme.space.sm

                                    Text {
                                        Layout.preferredWidth: 108
                                        text: (App.chinese ? outlineNode.modelData.labelZh
                                                           : outlineNode.modelData.labelEn)
                                        font.family: Theme.fontFamilyMono
                                        font.pixelSize: Theme.font.sizeCaption2
                                        color: Theme.colour.textTertiary
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: outlineNode.modelData.text !== ""
                                              ? outlineNode.modelData.text
                                              : (App.chinese ? outlineNode.modelData.descriptionZh
                                                             : outlineNode.modelData.descriptionEn)
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.font.sizeBody
                                        color: Theme.colour.text
                                        elide: Text.ElideRight
                                        maximumLineCount: 1
                                    }

                                    Text {
                                        visible: outlineNode.modelData.childCount > 0
                                        text: String(outlineNode.modelData.childCount)
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.font.sizeCaption2
                                        color: Theme.colour.textTertiary
                                    }
                                }

                                //  Nested drop target / 嵌套放置目标
                                DropArea {
                                    anchors.fill: parent
                                    keys: ["tcbw-element"]
                                    onEntered: function (drag) {
                                        if (outlineNode.modelData.allowsChildren)
                                            drag.accept(Qt.CopyAction)
                                    }
                                    onDropped: function (drop) {
                                        var tag = drop.getDataAsString("text/plain")
                                        if (tag !== "" && outlineNode.modelData.allowsChildren)
                                            Builder.insertElement(tag, outlineNode.modelData.id, -1)
                                    }
                                }

                                HoverHandler { id: nodeHover }

                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: Builder.selectNode(outlineNode.modelData.id)
                                    onDoubleClicked: {
                                        Builder.selectNode(outlineNode.modelData.id)
                                        inspector.focusContentTab()
                                    }
                                }
                            }
                        }
                    }

                    AppEmptyState {
                        anchors.centerIn: parent
                        visible: Builder.outlineNodes.length === 0
                        width: Math.min(parent.width - Theme.space.xxl, 340)
                        glyph: "\u25A6"
                        title: Tr.t("canvas.empty")
                        detail: Tr.t("canvas.emptyHint")
                    }

                    //  Grid overlay / 网格叠加层
                    Canvas {
                        anchors.fill: parent
                        visible: Builder.gridVisible
                        opacity: 0.6
                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.reset()
                            ctx.strokeStyle = Theme.colour.gridLine
                            ctx.lineWidth = 1
                            var step = Math.max(4, Builder.gridSize)
                            for (var x = 0; x <= width; x += step) {
                                ctx.beginPath()
                                ctx.moveTo(x + 0.5, 0)
                                ctx.lineTo(x + 0.5, height)
                                ctx.stroke()
                            }
                            for (var y = 0; y <= height; y += step) {
                                ctx.beginPath()
                                ctx.moveTo(0, y + 0.5)
                                ctx.lineTo(width, y + 0.5)
                                ctx.stroke()
                            }
                        }
                        Connections {
                            target: Builder
                            function onCanvasSettingsChanged() { parent.requestPaint() }
                        }
                    }
                }
            }

            //  Floating canvas toolbar / 浮动画布工具条
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.space.md
                width: toolbarRow.implicitWidth + Theme.space.lg
                height: 34
                radius: Theme.radius.pill
                color: Theme.colour.chrome
                border.width: 1
                border.color: Theme.colour.separator

                Row {
                    id: toolbarRow
                    anchors.centerIn: parent
                    spacing: Theme.space.xxs

                    AppIconButton {
                        glyph: "\u2212"
                        tooltip: Tr.t("action.zoomOut")
                        buttonSize: 26
                        onClicked: root.zoom = Math.max(0.25, root.zoom - 0.1)
                    }

                    Text {
                        width: 46
                        height: 26
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        text: Math.round(root.zoom * 100) + "%"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.sizeCaption
                        color: Theme.colour.textSecondary
                    }

                    AppIconButton {
                        glyph: "\uFF0B"
                        tooltip: Tr.t("action.zoomIn")
                        buttonSize: 26
                        onClicked: root.zoom = Math.min(2.0, root.zoom + 0.1)
                    }

                    Rectangle { width: 1; height: 16; color: Theme.colour.separator }

                    AppIconButton {
                        glyph: "\u25A6"
                        tooltip: Tr.t("action.zoomReset")
                        buttonSize: 26
                        onClicked: root.zoom = 1.0
                    }

                    AppIconButton {
                        glyph: "\u2317"
                        tooltip: Tr.t("action.toggleGrid")
                        buttonSize: 26
                        active: Builder.gridVisible
                        onClicked: Builder.gridVisible = !Builder.gridVisible
                    }

                    Rectangle { width: 1; height: 16; color: Theme.colour.separator }

                    AppIconButton {
                        glyph: "\u29C9"
                        tooltip: Tr.t("layers.bringForward")
                        buttonSize: 26
                        enabled: Builder.hasSelection
                        onClicked: Builder.reorderSelected(-1)
                    }

                    AppIconButton {
                        glyph: "\u29C8"
                        tooltip: Tr.t("layers.sendBackward")
                        buttonSize: 26
                        enabled: Builder.hasSelection
                        onClicked: Builder.reorderSelected(1)
                    }

                    AppIconButton {
                        glyph: "\u2715"
                        tooltip: Tr.t("action.delete")
                        buttonSize: 26
                        danger: true
                        enabled: Builder.hasSelection
                        onClicked: Builder.deleteSelected()
                    }
                }
            }
        }
    }
}
