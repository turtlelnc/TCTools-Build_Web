// ============================================================================
//  InspectorPanel.qml -- the contextual right-hand detail panel
//  InspectorPanel.qml —— 上下文右侧详情面板
// ============================================================================
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import TCTools.BuildWeb

Rectangle {
    id: root

    property string mode: "design"
    property int activeTab: 0

    color: Theme.colour.surface
    border.width: 0

    Rectangle {
        anchors.left: parent.left
        width: 1
        height: parent.height
        color: Theme.colour.hairline
    }

    function focusContentTab() {
        root.activeTab = 0
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // -------------------------------------------------------------------
        //  Tab strip / 标签栏
        // -------------------------------------------------------------------
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.space.panelHeader + Theme.space.sm

            AppSegmented {
                anchors.fill: parent
                anchors.margins: Theme.space.sm
                visible: root.mode === "design"
                itemHeight: 24
                fullWidth: true
                items: [
                    { value: 0, text: Tr.t("inspector.tab.content") },
                    { value: 1, text: Tr.t("inspector.tab.style") },
                    { value: 2, text: Tr.t("inspector.tab.attributes") }
                ]
                currentValue: root.activeTab
                onActivated: function (value) { root.activeTab = value }
            }

            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.space.md
                anchors.verticalCenter: parent.verticalCenter
                visible: root.mode !== "design"
                text: {
                    switch (root.mode) {
                    case "blocks":  return Tr.t("blocks.generatedCode")
                    case "ai":      return Tr.t("ai.provider")
                    case "backend": return Tr.t("backend.testResult")
                    case "code":    return Tr.t("code.files")
                    case "preview": return Tr.t("preview.title")
                    default:        return Tr.t("panel.inspector")
                    }
                }
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.sizeFootnote
                font.weight: Theme.font.weightSemibold
                color: Theme.colour.textTertiary
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.colour.hairline
        }

        // -------------------------------------------------------------------
        //  Design mode: element inspector
        //  设计模式：元素检查器
        // -------------------------------------------------------------------
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.mode === "design"
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: parent.width - Theme.space.lg
                x: Theme.space.md
                spacing: Theme.space.md

                Item { Layout.preferredHeight: Theme.space.sm }

                AppEmptyState {
                    Layout.fillWidth: true
                    visible: !Builder.hasSelection
                    glyph: "\u2316"
                    title: Tr.t("inspector.noSelection")
                    detail: Tr.t("inspector.noSelectionHint")
                }

                // --- Layer name / 图层名称 --------------------------------
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: Builder.hasSelection
                    spacing: Theme.space.xs

                    AppSectionHeader {
                        Layout.fillWidth: true
                        title: Tr.t("inspector.layerName")
                        uppercase: true
                    }

                    AppTextField {
                        Layout.fillWidth: true
                        text: Builder.hasSelection ? (Builder.selectedNode.name !== undefined
                                                      ? Builder.selectedNode.name : "") : ""
                        placeholderText: Tr.t("inspector.layerName")
                        onEditingFinished: if (Builder.hasSelection)
                                               Builder.setNodeName(Builder.selectedNodeId, text)
                    }

                    Text {
                        Layout.fillWidth: true
                        text: Tr.t("inspector.tag") + ":  <"
                              + (Builder.hasSelection ? Builder.selectedNode.tag : "") + ">"
                        font.family: Theme.fontFamilyMono
                        font.pixelSize: Theme.font.sizeCaption
                        color: Theme.colour.textTertiary
                    }
                }

                // --- Content tab / 内容标签页 ------------------------------
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: Builder.hasSelection && root.activeTab === 0
                    spacing: Theme.space.xs

                    AppSectionHeader {
                        Layout.fillWidth: true
                        title: Tr.t("inspector.text")
                        uppercase: true
                    }

                    AppTextArea {
                        Layout.fillWidth: true
                        areaHeight: 90
                        text: Builder.hasSelection ? (Builder.selectedNode.text !== undefined
                                                      ? Builder.selectedNode.text : "") : ""
                        placeholderText: Tr.t("inspector.placeholder.text")
                        onTextChanged: {
                            if (Builder.hasSelection && Builder.selectedNode.text !== undefined
                                    && Builder.selectedNode.text !== text)
                                Builder.setNodeText(Builder.selectedNodeId, text)
                        }
                    }
                }

                // --- Style tab / 样式标签页 -------------------------------
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: Builder.hasSelection && root.activeTab === 1
                    spacing: Theme.space.md

                    AppSectionHeader {
                        Layout.fillWidth: true
                        title: Tr.t("inspector.quickStyles")
                        uppercase: true
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.space.xs

                        Repeater {
                            model: Builder.stylePresets()

                            delegate: AppButton {
                                required property var modelData
                                text: App.chinese ? modelData.labelZh : modelData.labelEn
                                variant: "secondary"
                                size: "small"
                                onClicked: Builder.applyStylePreset(modelData.id)
                            }
                        }
                    }

                    AppSectionHeader {
                        Layout.fillWidth: true
                        title: Tr.t("inspector.font")
                        uppercase: true
                    }

                    AppTextField {
                        Layout.fillWidth: true
                        label: Tr.t("inspector.fontSize")
                        suffixText: "px"
                        text: Builder.hasSelection ? Builder.selectedNode.style.fontSize || ""
                                                   : ""
                        placeholderText: "16"
                        onEditingFinished: if (Builder.hasSelection && text !== "")
                                               Builder.setNodeStyle(Builder.selectedNodeId,
                                                                    "font-size", text + "px")
                    }

                    AppTextField {
                        Layout.fillWidth: true
                        label: Tr.t("inspector.color")
                        prefixText: "#"
                        text: Builder.hasSelection ? Builder.selectedNode.style.color || "" : ""
                        placeholderText: "1d1d1f"
                        onEditingFinished: if (Builder.hasSelection && text !== "")
                                               Builder.setNodeStyle(Builder.selectedNodeId,
                                                                    "color", "#" + text)
                    }

                    AppTextField {
                        Layout.fillWidth: true
                        label: Tr.t("inspector.background")
                        prefixText: "#"
                        text: Builder.hasSelection
                              ? Builder.selectedNode.style.background || "" : ""
                        placeholderText: "ffffff"
                        onEditingFinished: if (Builder.hasSelection && text !== "")
                                               Builder.setNodeStyle(Builder.selectedNodeId,
                                                                    "background", "#" + text)
                    }

                    AppSectionHeader {
                        Layout.fillWidth: true
                        title: Tr.t("inspector.border")
                        uppercase: true
                    }

                    AppTextField {
                        Layout.fillWidth: true
                        label: Tr.t("inspector.borderRadius")
                        suffixText: "px"
                        text: Builder.hasSelection
                              ? Builder.selectedNode.style.borderRadius || "" : ""
                        placeholderText: "12"
                        onEditingFinished: if (Builder.hasSelection && text !== "")
                                               Builder.setNodeStyle(Builder.selectedNodeId,
                                                                    "border-radius", text + "px")
                    }

                    AppTextField {
                        Layout.fillWidth: true
                        label: Tr.t("inspector.padding")
                        suffixText: "px"
                        placeholderText: "16"
                        onEditingFinished: if (Builder.hasSelection && text !== "") {
                                               Builder.setNodeStyle(Builder.selectedNodeId,
                                                                    "padding", text + "px")
                                           }
                    }

                    AppTextArea {
                        Layout.fillWidth: true
                        areaHeight: 64
                        monospaced: true
                        placeholderText: Tr.t("inspector.advancedCss")
                    }

                    AppButton {
                        Layout.fillWidth: true
                        text: Tr.t("inspector.resetStyle")
                        variant: "ghost"
                        enabled: Builder.hasSelection
                        onClicked: {
                            var props = ["font-size", "color", "background", "border-radius",
                                         "padding", "margin", "width", "height",
                                         "box-shadow", "border", "opacity"]
                            for (var i = 0; i < props.length; ++i)
                                Builder.setNodeStyle(Builder.selectedNodeId, props[i], "")
                        }
                    }
                }

                // --- Attributes tab / 属性标签页 ---------------------------
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: Builder.hasSelection && root.activeTab === 2
                    spacing: Theme.space.sm

                    AppSectionHeader {
                        Layout.fillWidth: true
                        title: Tr.t("inspector.tab.attributes")
                        uppercase: true
                    }

                    Repeater {
                        model: Builder.hasSelection
                               ? Object.keys(Builder.selectedNode.attributes || {}) : []

                        delegate: RowLayout {
                            required property string modelData
                            Layout.fillWidth: true
                            spacing: Theme.space.xs

                            Text {
                                Layout.preferredWidth: 90
                                text: modelData
                                font.family: Theme.fontFamilyMono
                                font.pixelSize: Theme.font.sizeCaption
                                color: Theme.colour.textSecondary
                                elide: Text.ElideRight
                            }

                            AppTextField {
                                Layout.fillWidth: true
                                text: Builder.selectedNode.attributes[modelData] || ""
                                onEditingFinished: Builder.setNodeAttribute(
                                                       Builder.selectedNodeId, modelData, text)
                            }

                            AppIconButton {
                                glyph: "\u2715"
                                buttonSize: 22
                                danger: true
                                tooltip: Tr.t("common.remove")
                                onClicked: Builder.setNodeAttribute(
                                               Builder.selectedNodeId, modelData, "")
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: Builder.hasSelection
                                 && Object.keys(Builder.selectedNode.attributes || {}).length === 0
                        text: Tr.t("inspector.attributes.empty")
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.sizeCaption
                        color: Theme.colour.textTertiary
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: Builder.hasSelection
                        spacing: Theme.space.xs

                        AppTextField {
                            id: newAttributeName
                            Layout.fillWidth: true
                            placeholderText: Tr.t("inspector.attributeKey")
                        }

                        AppTextField {
                            id: newAttributeValue
                            Layout.fillWidth: true
                            placeholderText: Tr.t("inspector.attributeValue")
                        }

                        AppButton {
                            text: Tr.t("common.add")
                            variant: "secondary"
                            size: "small"
                            enabled: newAttributeName.text !== ""
                            onClicked: {
                                Builder.setNodeAttribute(Builder.selectedNodeId,
                                                         newAttributeName.text,
                                                         newAttributeValue.text)
                                newAttributeName.text = ""
                                newAttributeValue.text = ""
                            }
                        }
                    }
                }

                Item { Layout.preferredHeight: Theme.space.lg }
            }
        }

        // -------------------------------------------------------------------
        //  AI mode: provider and tool configuration
        //  智能模式：服务商与工具配置
        // -------------------------------------------------------------------
        AiProviderPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.mode === "ai"
        }

        // -------------------------------------------------------------------
        //  Blocks mode: compiled code and diagnostics
        //  积木模式：编译产物与诊断
        // -------------------------------------------------------------------
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.mode === "blocks"
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: parent.width - Theme.space.lg
                x: Theme.space.md
                spacing: Theme.space.sm

                Item { Layout.preferredHeight: Theme.space.sm }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space.sm

                    AppBadge {
                        text: Blocks.compileErrors.length + " " + Tr.t("blocks.errors")
                        tone: Blocks.compileErrors.length > 0 ? "danger" : "success"
                    }

                    AppBadge {
                        text: Blocks.compileWarnings.length + " " + Tr.t("blocks.warnings")
                        tone: Blocks.compileWarnings.length > 0 ? "warning" : "neutral"
                    }
                }

                AppButton {
                    Layout.fillWidth: true
                    text: Tr.t("blocks.copyCode")
                    variant: "secondary"
                    size: "small"
                    onClicked: {
                        Blocks.copyCodeToClipboard()
                        App.copyToClipboard("")
                    }
                }

                AppTextArea {
                    Layout.fillWidth: true
                    areaHeight: 420
                    monospaced: true
                    wrapText: false
                    text: Blocks.generatedCode
                }

                Item { Layout.preferredHeight: Theme.space.lg }
            }
        }

        // -------------------------------------------------------------------
        //  Other modes: a compact summary
        //  其他模式：紧凑摘要
        // -------------------------------------------------------------------
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.mode === "backend" || root.mode === "code"
                     || root.mode === "preview"
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: parent.width - Theme.space.lg
                x: Theme.space.md
                spacing: Theme.space.md

                Item { Layout.preferredHeight: Theme.space.sm }

                AppSectionHeader {
                    Layout.fillWidth: true
                    title: Tr.t("backend.previewServer")
                    uppercase: true
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space.sm

                    AppBadge {
                        text: Backend.previewRunning ? Tr.t("backend.previewStarted")
                                                     : Tr.t("backend.previewStopped")
                        tone: Backend.previewRunning ? "success" : "neutral"
                    }

                    AppBadge {
                        visible: Backend.previewRunning
                        text: Backend.previewClients + " " + Tr.t("backend.previewClients")
                        tone: "info"
                    }
                }

                AppButton {
                    Layout.fillWidth: true
                    text: Backend.previewRunning ? Tr.t("backend.previewStop")
                                                 : Tr.t("backend.previewStart")
                    variant: Backend.previewRunning ? "secondary" : "primary"
                    onClicked: {
                        if (Backend.previewRunning)
                            Backend.stopPreview()
                        else
                            Backend.startPreview()
                    }
                }

                AppButton {
                    Layout.fillWidth: true
                    text: Tr.t("backend.previewOpen")
                    variant: "ghost"
                    enabled: Backend.previewRunning
                    onClicked: Backend.openPreviewInBrowser()
                }

                AppSectionHeader {
                    Layout.fillWidth: true
                    title: Tr.t("code.exportMode")
                    uppercase: true
                }

                AppComboBox {
                    Layout.fillWidth: true
                    model: [
                        { value: "single-html",      text: Tr.t("code.mode.singleHtml") },
                        { value: "html-with-assets", text: Tr.t("code.mode.htmlWithAssets") },
                        { value: "script-only",      text: Tr.t("code.mode.scriptOnly") },
                        { value: "headless-bundle",  text: Tr.t("code.mode.headlessBundle") }
                    ]
                    onActivated: App.exportMode = currentValue()
                    Component.onCompleted: selectValue(App.exportMode)
                }

                Item { Layout.preferredHeight: Theme.space.lg }
            }
        }
    }
}
