// ============================================================================
//  AiProviderPanel.qml -- AI provider, credentials, tools and MCP servers
//  AiProviderPanel.qml —— AI 服务商、凭据、工具与 MCP 服务器
// ============================================================================
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import TCTools.BuildWeb

Item {
    id: root

    signal openSettingsRequested()

    ScrollView {
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width - Theme.space.lg
            x: Theme.space.md
            spacing: Theme.space.md

            Item { Layout.preferredHeight: Theme.space.sm }

            // --- Region filter / 地区筛选 -----------------------------------
            AppSegmented {
                Layout.fillWidth: true
                fullWidth: true
                itemHeight: 24
                items: [
                    { value: "all",    text: Tr.t("ai.regionAll") },
                    { value: "global", text: Tr.t("ai.regionGlobal") },
                    { value: "cn",     text: Tr.t("ai.regionCn") },
                    { value: "local",  text: Tr.t("ai.regionLocal") }
                ]
                currentValue: Agent.regionFilter === "" ? "all" : Agent.regionFilter
                onActivated: function (value) {
                    Agent.regionFilter = value === "all" ? "" : value
                }
            }

            AppTextField {
                Layout.fillWidth: true
                placeholderText: Tr.t("common.searchPlaceholder")
                onTextChanged: Agent.vendorFilter = text
            }

            Text {
                Layout.fillWidth: true
                text: Agent.vendorCount + " " + Tr.t("ai.vendors") + "  ·  "
                      + Agent.modelCount + " " + Tr.t("ai.modelCount")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.sizeCaption
                color: Theme.colour.textTertiary
            }

            // --- Vendor list / 服务商列表 -----------------------------------
            ListView {
                id: vendorList
                Layout.fillWidth: true
                Layout.preferredHeight: 220
                clip: true
                model: Agent.filteredVendors
                spacing: 2
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Rectangle {
                    id: vendorRow
                    required property var modelData

                    width: vendorList.width
                    height: 38
                    radius: Theme.radius.sm
                    color: vendorRow.modelData.id === Agent.currentVendorId
                           ? Theme.colour.accentSoft
                           : (vendorHover.hovered ? Theme.colour.fill : "transparent")

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.space.sm
                        anchors.rightMargin: Theme.space.sm
                        spacing: Theme.space.sm

                        Rectangle {
                            width: 6
                            height: 6
                            radius: 3
                            color: {
                                switch (vendorRow.modelData.region) {
                                case "cn":    return "#FF3B30"
                                case "local": return "#30D158"
                                default:      return Theme.colour.accent
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            Text {
                                Layout.fillWidth: true
                                text: App.chinese ? vendorRow.modelData.labelZh
                                                  : vendorRow.modelData.labelEn
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.font.sizeFootnote
                                color: Theme.colour.text
                                elide: Text.ElideRight
                            }

                            Text {
                                Layout.fillWidth: true
                                text: vendorRow.modelData.defaultModel
                                      + "  ·  " + vendorRow.modelData.modelCount
                                      + " " + Tr.t("ai.modelCount")
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.font.sizeCaption2
                                color: Theme.colour.textTertiary
                                elide: Text.ElideRight
                            }
                        }

                        Text {
                            visible: Agent.hasApiKey && vendorRow.modelData.id === Agent.currentVendorId
                            text: "\u2713"
                            font.pixelSize: 12
                            color: Theme.colour.success
                        }
                    }

                    HoverHandler { id: vendorHover }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Agent.currentVendorId = vendorRow.modelData.id
                    }
                }
            }

            // --- Model / 模型 ---------------------------------------------
            AppSectionHeader {
                Layout.fillWidth: true
                title: Tr.t("ai.model")
                uppercase: true
            }

            AppComboBox {
                Layout.fillWidth: true
                model: Agent.currentVendorModels
                Component.onCompleted: selectValue(Agent.currentModel)
                onActivated: Agent.currentModel = currentValue()
                Connections {
                    target: Agent
                    function onCurrentModelChanged() {
                        //  Keep the combo in step with the controller even when
                        //  the change came from elsewhere.
                        //  即使变更来自别处，也让下拉框与控制器保持同步。
                    }
                }
            }

            // --- Authorisation / 授权 --------------------------------------
            AppSectionHeader {
                Layout.fillWidth: true
                title: Tr.t("backend.auth")
                uppercase: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space.sm

                AppBadge {
                    text: Agent.authorised ? Tr.t("ai.keyStored")
                                           : Tr.t("ai.notConfigured")
                    tone: Agent.authorised ? "success" : "warning"
                }

                Text {
                    Layout.fillWidth: true
                    text: Agent.authStatus
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.sizeCaption
                    color: Theme.colour.textTertiary
                    elide: Text.ElideRight
                }
            }

            AppTextField {
                id: keyField
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: Agent.hasStoredKey && keyField.text === ""
                                 ? Agent.maskStoredKey() : Tr.t("ai.apiKeyPlaceholder")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space.sm

                AppButton {
                    Layout.fillWidth: true
                    text: Tr.t("ai.saveKey")
                    variant: "primary"
                    size: "small"
                    onClicked: {
                        if (Agent.saveApiKey(keyField.text))
                            keyField.text = ""
                    }
                }

                AppButton {
                    Layout.fillWidth: true
                    text: Tr.t("ai.clearKey")
                    variant: "ghost"
                    size: "small"
                    enabled: Agent.hasStoredKey
                    onClicked: Agent.clearApiKey()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space.sm

                AppButton {
                    Layout.fillWidth: true
                    text: Tr.t("ai.signInOAuth")
                    variant: "secondary"
                    size: "small"
                    onClicked: Agent.startOAuthPkce("", "")
                }

                AppButton {
                    Layout.fillWidth: true
                    text: Tr.t("ai.signInDevice")
                    variant: "secondary"
                    size: "small"
                    onClicked: Agent.startOAuthDevice("", "")
                }
            }

            //  Honest statement of how the credential is protected on this
            //  platform; the C++ side decides the wording.
            //  如实说明凭据在本平台上的保护方式；具体措辞由 C++ 侧决定。
            Text {
                Layout.fillWidth: true
                text: Tr.t("ai.protection") + ":  " + Agent.protectionDescription
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.sizeCaption2
                color: Theme.colour.textTertiary
                wrapMode: Text.WordWrap
            }

            // --- Approval policy / 审批策略 ---------------------------------
            AppSectionHeader {
                Layout.fillWidth: true
                title: Tr.t("ai.approvalPolicy")
                uppercase: true
            }

            AppComboBox {
                Layout.fillWidth: true
                model: [
                    { value: "never",            text: Tr.t("ai.policy.never") },
                    { value: "safe-only",        text: Tr.t("ai.policy.safeOnly") },
                    { value: "ask-every-change", text: Tr.t("ai.policy.askEveryChange") },
                    { value: "ask-destructive",  text: Tr.t("ai.policy.askDestructive") }
                ]
                Component.onCompleted: selectValue(Agent.approvalPolicy)
                onActivated: Agent.approvalPolicy = currentValue()
            }

            // --- Tools / 工具 ----------------------------------------------
            AppSectionHeader {
                Layout.fillWidth: true
                title: Tr.t("ai.tools")
                subtitle: Tr.t("ai.toolsHint")
                uppercase: true
            }

            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 240)
                clip: true
                model: Agent.availableTools
                spacing: 2
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Rectangle {
                    id: toolRow
                    required property var modelData

                    width: parent.width
                    height: 40
                    radius: Theme.radius.sm
                    color: Theme.colour.fill

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.space.sm
                        anchors.rightMargin: Theme.space.sm
                        spacing: Theme.space.sm

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            Text {
                                Layout.fillWidth: true
                                text: App.chinese ? toolRow.modelData.titleZh
                                                  : toolRow.modelData.titleEn
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.font.sizeCaption
                                color: Theme.colour.text
                                elide: Text.ElideRight
                            }

                            Text {
                                Layout.fillWidth: true
                                text: toolRow.modelData.name
                                font.family: Theme.fontFamilyMono
                                font.pixelSize: Theme.font.sizeCaption2
                                color: Theme.colour.textTertiary
                                elide: Text.ElideRight
                            }
                        }

                        AppBadge {
                            text: toolRow.modelData.destructive ? Tr.t("ai.toolsDestructive")
                                  : toolRow.modelData.mutatesProject ? Tr.t("ai.toolsMutating")
                                  : Tr.t("ai.toolsSafe")
                            tone: toolRow.modelData.destructive ? "danger"
                                  : toolRow.modelData.mutatesProject ? "warning" : "success"
                            outline: true
                        }
                    }
                }
            }

            // --- MCP servers / MCP 服务器 -----------------------------------
            McpPanel {
                Layout.fillWidth: true
            }

            Item { Layout.preferredHeight: Theme.space.lg }
        }
    }
}
