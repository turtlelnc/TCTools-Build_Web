// ============================================================================
//  McpPanel.qml -- Model Context Protocol server management
//  McpPanel.qml —— Model Context Protocol 服务器管理
// ============================================================================
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import TCTools.BuildWeb

ColumnLayout {
    id: root

    spacing: Theme.space.sm

    AppSectionHeader {
        Layout.fillWidth: true
        title: Tr.t("ai.mcpServers")
        uppercase: true
        trailingText: Tr.t("ai.mcpAdd")
        onTrailingClicked: root.showAddForm = !root.showAddForm
    }

    property bool showAddForm: false

    //  A new server starts untrusted, which means McpClient refuses to spawn it
    //  until the user explicitly opts in.
    //  新建服务器默认为不受信任，这意味着在用户明确同意之前 McpClient 会拒绝启动它。
    Text {
        Layout.fillWidth: true
        text: Tr.t("ai.mcpTrustWarning")
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.sizeCaption2
        color: Theme.colour.textTertiary
        wrapMode: Text.WordWrap
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: root.showAddForm
        spacing: Theme.space.xs

        AppTextField {
            id: newServerName
            Layout.fillWidth: true
            placeholderText: Tr.t("backend.name")
        }

        AppComboBox {
            id: newServerTransport
            Layout.fillWidth: true
            model: [
                { value: "stdio",     text: "stdio  (local process)" },
                { value: "http-sse",  text: "HTTP + SSE" },
                { value: "http-json", text: "HTTP JSON" }
            ]
            currentIndex: 0
        }

        AppTextField {
            Layout.fillWidth: true
            visible: newServerTransport.currentValue() === "stdio"
            id: newServerCommand
            placeholderText: "npx -y @modelcontextprotocol/server-filesystem"
        }

        AppTextField {
            Layout.fillWidth: true
            visible: newServerTransport.currentValue() !== "stdio"
            id: newServerUrl
            placeholderText: "https://example.com/mcp"
        }

        AppButton {
            Layout.fillWidth: true
            text: Tr.t("common.add")
            variant: "primary"
            size: "small"
            enabled: newServerName.text !== ""
                     && (newServerTransport.currentValue() === "stdio"
                         ? newServerCommand.text !== "" : newServerUrl.text !== "")
            onClicked: {
                var transport = newServerTransport.currentValue()
                var map = {
                    "name": newServerName.text,
                    "transport": transport,
                    "enabled": true,
                    "trusted": false
                }
                if (transport === "stdio") {
                    var parts = newServerCommand.text.split(" ")
                    map["command"] = parts[0]
                    map["arguments"] = parts.slice(1)
                } else {
                    map["url"] = newServerUrl.text
                }
                Agent.addMcpServer(map)
                newServerName.text = ""
                newServerCommand.text = ""
                newServerUrl.text = ""
                root.showAddForm = false
            }
        }
    }

    Repeater {
        model: Agent.mcpServers

        delegate: AppCard {
            required property var modelData
            Layout.fillWidth: true
            padding: Theme.space.sm

            readonly property var status: Agent.mcpServerStatus(modelData.id)

            ColumnLayout {
                anchors.fill: parent
                spacing: Theme.space.xs

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space.sm

                    Text {
                        Layout.fillWidth: true
                        text: modelData.name
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.sizeFootnote
                        font.weight: Theme.font.weightMedium
                        color: Theme.colour.text
                        elide: Text.ElideRight
                    }

                    AppBadge {
                        text: modelData.transport
                        tone: "neutral"
                        outline: true
                    }

                    AppBadge {
                        visible: status.toolCount !== undefined && status.toolCount > 0
                        text: status.toolCount + " " + Tr.t("ai.mcpTools")
                        tone: "info"
                        outline: true
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space.sm

                    AppSwitch {
                        text: Tr.t("ai.mcpTrust")
                        compact: true
                        checked: modelData.trusted === true
                        onToggled: Agent.setMcpServerTrusted(modelData.id, checked)
                    }

                    Item { Layout.fillWidth: true }

                    AppButton {
                        text: status.running ? Tr.t("ai.mcpDisconnect")
                                             : Tr.t("ai.mcpConnect")
                        variant: "secondary"
                        size: "small"
                        enabled: modelData.trusted === true
                        onClicked: {
                            if (status.running)
                                Agent.disconnectMcpServer(modelData.id)
                            else
                                Agent.connectMcpServer(modelData.id)
                        }
                    }

                    AppIconButton {
                        glyph: "\u2715"
                        buttonSize: 22
                        danger: true
                        tooltip: Tr.t("common.remove")
                        onClicked: Agent.removeMcpServer(modelData.id)
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: modelData.trusted !== true
                    text: Tr.t("ai.mcpNotTrusted")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.sizeCaption2
                    color: Theme.colour.warning
                }
            }
        }
    }

    Text {
        Layout.fillWidth: true
        visible: Agent.mcpServers.length === 0 && !root.showAddForm
        text: Tr.t("common.empty")
        font.family: Theme.fontFamily
        font.pixelSize: Theme.font.sizeCaption
        color: Theme.colour.textTertiary
    }
}
