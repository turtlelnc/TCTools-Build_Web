// ============================================================================
//  AiView.qml -- chat surface for TI-UseAgent
//  AiView.qml —— TI-UseAgent 对话界面
// ============================================================================
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import TCTools.BuildWeb

Item {
    id: root

    signal openSettingsRequested()

    function focusComposer() {
        composer.forceActiveFocus()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // -------------------------------------------------------------------
        //  Header
        // -------------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.space.toolbarHeight
            color: Theme.colour.chrome

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.colour.hairline
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.space.lg
                anchors.rightMargin: Theme.space.lg
                spacing: Theme.space.sm

                Text {
                    text: "\u2726"
                    font.pixelSize: 15
                    color: Theme.colour.accent
                }

                Text {
                    text: Tr.t("ai.title")
                    font.family: Theme.fontFamilyDisplay
                    font.pixelSize: Theme.font.sizeSubhead
                    font.weight: Theme.font.weightSemibold
                    color: Theme.colour.text
                }

                AppBadge {
                    text: Agent.currentVendor.labelEn !== undefined
                          ? (App.chinese && Agent.currentVendor.labelZh !== undefined
                             ? Agent.currentVendor.labelZh : Agent.currentVendor.labelEn)
                          : Tr.t("ai.notConfigured")
                    tone: Agent.authorised ? "accent" : "warning"
                    outline: true
                }

                Text {
                    text: Agent.currentModel
                    font.family: Theme.fontFamilyMono
                    font.pixelSize: Theme.font.sizeCaption
                    color: Theme.colour.textTertiary
                    elide: Text.ElideRight
                    Layout.maximumWidth: 200
                }

                Item { Layout.fillWidth: true }

                AppBadge {
                    visible: Agent.inputTokens + Agent.outputTokens > 0
                    text: Tr.t("ai.usage") + "  "
                          + (Agent.inputTokens + Agent.outputTokens) + " / "
                          + Math.max(1, Agent.estimatedContextTokens)
                    tone: "neutral"
                    outline: true
                }

                AppButton {
                    text: Tr.t("ai.clear")
                    variant: "ghost"
                    size: "small"
                    enabled: Agent.transcript.length > 0
                    onClicked: Agent.clearTranscript()
                }
            }
        }

        // -------------------------------------------------------------------
        //  Transcript
        // -------------------------------------------------------------------
        ListView {
            id: transcriptList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: Agent.transcript
            spacing: Theme.space.md
            topMargin: Theme.space.lg
            bottomMargin: Theme.space.lg
            leftMargin: Theme.space.xl
            rightMargin: Theme.space.xl
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            onCountChanged: Qt.callLater(function () {
                transcriptList.positionViewAtEnd()
            })

            delegate: Item {
                id: messageRow
                required property var modelData
                width: transcriptList.width - transcriptList.leftMargin - transcriptList.rightMargin
                height: bubble.height

                readonly property bool isUser: modelData.role === "user"
                readonly property bool isTool: modelData.role === "tool"

                Column {
                    id: bubble
                    width: parent.width
                    spacing: 4

                    Text {
                        text: {
                            if (messageRow.isUser)
                                return Tr.t("ai.you")
                            if (messageRow.isTool)
                                return Tr.t("ai.tool")
                            return Tr.t("ai.assistant")
                        }
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.sizeCaption2
                        font.weight: Theme.font.weightSemibold
                        font.letterSpacing: 0.6
                        color: messageRow.isUser ? Theme.colour.accent
                                                 : Theme.colour.textTertiary
                    }

                    AppCard {
                        width: parent.width
                        padding: Theme.space.md
                        selected: messageRow.isUser
                        visible: !messageRow.isTool

                        Text {
                            width: parent.width
                            text: messageRow.modelData.content
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.font.sizeBody
                            color: Theme.colour.text
                            wrapMode: Text.WordWrap
                            lineHeight: 1.45
                        }
                    }

                    //  Tool card / 工具卡片
                    Row {
                        width: parent.width
                        spacing: Theme.space.sm
                        visible: messageRow.isTool

                        AppBadge {
                            text: messageRow.modelData.toolName !== undefined
                                  ? messageRow.modelData.toolName : Tr.t("ai.tool")
                            tone: messageRow.modelData.isError ? "danger" : "success"
                            outline: true
                        }

                        Text {
                            width: parent.width - 120
                            text: messageRow.modelData.content
                            font.family: Theme.fontFamilyMono
                            font.pixelSize: Theme.font.sizeCaption
                            color: Theme.colour.textSecondary
                            wrapMode: Text.WordWrap
                            maximumLineCount: 6
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        // -------------------------------------------------------------------
        //  Streaming bubble
        // -------------------------------------------------------------------
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? streamingCard.height + Theme.space.md : 0
            visible: Agent.streamingText !== ""

            AppCard {
                id: streamingCard
                width: Math.min(parent.width - Theme.space.xxl, 720)
                anchors.horizontalCenter: parent.horizontalCenter
                padding: Theme.space.md

                RowLayout {
                    width: parent.width
                    spacing: Theme.space.sm

                    AppSpinner { Layout.alignment: Qt.AlignTop }

                    Text {
                        Layout.fillWidth: true
                        text: Agent.streamingText
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.sizeBody
                        color: Theme.colour.text
                        wrapMode: Text.WordWrap
                        lineHeight: 1.45
                    }
                }
            }
        }

        // -------------------------------------------------------------------
        //  Approval strip
        // -------------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? approvalColumn.implicitHeight + Theme.space.lg : 0
            visible: Agent.pendingToolCalls.length > 0
            color: Theme.alpha(Theme.colour.warning, 0.12)
            clip: true

            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: Theme.colour.warning
            }

            ColumnLayout {
                id: approvalColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Theme.space.lg
                anchors.rightMargin: Theme.space.lg
                spacing: Theme.space.sm

                Text {
                    text: Tr.t("ai.approvalTitle")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.sizeCallout
                    font.weight: Theme.font.weightSemibold
                    color: Theme.colour.text
                }

                Text {
                    Layout.fillWidth: true
                    text: Tr.t("ai.approvalDetail")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.sizeCaption
                    color: Theme.colour.textSecondary
                    wrapMode: Text.WordWrap
                }

                Repeater {
                    model: Agent.pendingToolCalls

                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: Theme.space.sm

                        AppBadge {
                            text: modelData.name
                            tone: "warning"
                            outline: true
                        }

                        Text {
                            Layout.fillWidth: true
                            text: modelData.argumentsJson
                            font.family: Theme.fontFamilyMono
                            font.pixelSize: Theme.font.sizeCaption2
                            color: Theme.colour.textSecondary
                            elide: Text.ElideRight
                        }

                        AppButton {
                            text: Tr.t("ai.approve")
                            variant: "primary"
                            size: "small"
                            onClicked: Agent.approveToolCall(modelData.id)
                        }

                        AppButton {
                            text: Tr.t("ai.reject")
                            variant: "ghost"
                            size: "small"
                            onClicked: Agent.rejectToolCall(modelData.id)
                        }
                    }
                }

                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    spacing: Theme.space.sm

                    AppButton {
                        text: Tr.t("ai.rejectAll")
                        variant: "ghost"
                        size: "small"
                        onClicked: Agent.rejectAllPending()
                    }

                    AppButton {
                        text: Tr.t("ai.approveAll")
                        variant: "secondary"
                        size: "small"
                        onClicked: Agent.approveAllPending()
                    }
                }
            }
        }

        // -------------------------------------------------------------------
        //  Composer
        // -------------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: composerColumn.implicitHeight + Theme.space.lg
            color: Theme.colour.chrome

            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: Theme.colour.hairline
            }

            ColumnLayout {
                id: composerColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Theme.space.xl
                anchors.rightMargin: Theme.space.xl
                spacing: Theme.space.sm

                //  Setup prompt when the provider is not usable yet.
                //  服务商尚不可用时显示的配置提示。
                RowLayout {
                    Layout.fillWidth: true
                    visible: !Agent.authorised
                    spacing: Theme.space.sm

                    Text {
                        Layout.fillWidth: true
                        text: Tr.t("ai.configureFirst")
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.sizeFootnote
                        color: Theme.colour.warning
                        wrapMode: Text.WordWrap
                    }

                    AppButton {
                        text: Tr.t("ai.configure")
                        variant: "primary"
                        size: "small"
                        onClicked: root.openSettingsRequested()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: Agent.authorised
                    spacing: Theme.space.sm

                    AppTextArea {
                        id: composer
                        Layout.fillWidth: true
                        areaHeight: 70
                        placeholderText: Tr.t("ai.inputPlaceholder")
                        enabled: !Agent.busy

                        Keys.onPressed: function (event) {
                            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                                    && (event.modifiers & Qt.ControlModifier
                                        || event.modifiers & Qt.MetaModifier)) {
                                root.submit()
                                event.accepted = true
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.alignment: Qt.AlignBottom
                        spacing: Theme.space.xs

                        AppButton {
                            text: Agent.busy ? Tr.t("ai.stop") : Tr.t("ai.send")
                            variant: Agent.busy ? "secondary" : "primary"
                            enabled: Agent.busy || composer.text.trim() !== ""
                            onClicked: {
                                if (Agent.busy)
                                    Agent.cancelResponse()
                                else
                                    root.submit()
                            }
                        }

                        AppButton {
                            text: Tr.t("ai.retry")
                            variant: "ghost"
                            size: "small"
                            visible: !Agent.busy && Agent.transcript.length > 0
                            onClicked: Agent.retryLastMessage()
                        }
                    }
                }
            }
        }
    }

    function submit() {
        var text = composer.text.trim()
        if (text === "" || Agent.busy)
            return
        composer.text = ""
        Agent.sendMessage(text)
    }
}
