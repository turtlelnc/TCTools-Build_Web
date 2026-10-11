// ============================================================================
//  EndpointEditor.qml -- form for one backend endpoint
//  EndpointEditor.qml —— 单个后端端点的编辑表单
// ============================================================================
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import TCTools.BuildWeb

Item {
    id: root

    property var endpoint: ({})

    signal saved(var endpoint)
    signal cancelled()

    function loadEndpoint(map) {
        root.endpoint = map
        nameField.text = map.name !== undefined ? map.name : ""
        urlField.text = map.url !== undefined ? map.url : ""
        descriptionField.text = map.descriptionEn !== undefined ? map.descriptionEn : ""
        bodyField.text = map.bodyTemplate !== undefined ? map.bodyTemplate : ""
        methodCombo.selectValue(map.method !== undefined ? map.method : "GET")
    }

    function collect() {
        var map = {}
        for (var key in root.endpoint)
            map[key] = root.endpoint[key]
        map.name = nameField.text
        map.url = urlField.text
        map.descriptionEn = descriptionField.text
        map.descriptionZh = descriptionField.text
        map.bodyTemplate = bodyField.text
        map.method = methodCombo.currentValue()
        map.timeoutMs = Math.round(timeoutSlider.value) * 1000
        return map
    }

    ScrollView {
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: Math.min(parent.width - Theme.space.xxl, 720)
            x: Theme.space.xl
            spacing: Theme.space.md

            Item { Layout.preferredHeight: Theme.space.sm }

            AppEmptyState {
                Layout.fillWidth: true
                visible: root.endpoint.id === undefined
                glyph: "\u21C4"
                title: Tr.t("backend.noEndpoints")
                detail: Tr.t("backend.noEndpointsHint")
            }

            ColumnLayout {
                Layout.fillWidth: true
                visible: root.endpoint.id !== undefined
                spacing: Theme.space.md

                AppSectionHeader {
                    Layout.fillWidth: true
                    title: Tr.t("backend.endpoints")
                    uppercase: true
                }

                AppTextField {
                    id: nameField
                    Layout.fillWidth: true
                    placeholderText: Tr.t("backend.name")
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space.sm

                    AppComboBox {
                        id: methodCombo
                        Layout.preferredWidth: 120
                        model: ["GET", "POST", "PUT", "PATCH", "DELETE"]
                    }

                    AppTextField {
                        id: urlField
                        Layout.fillWidth: true
                        placeholderText: "https://api.example.com/v1/items"
                        monospaced: true
                    }
                }

                AppTextField {
                    id: descriptionField
                    Layout.fillWidth: true
                    placeholderText: Tr.t("backend.description")
                }

                AppSectionHeader {
                    Layout.fillWidth: true
                    title: Tr.t("backend.auth")
                    uppercase: true
                }

                AppComboBox {
                    id: authCombo
                    Layout.fillWidth: true
                    model: [
                        { value: "none",         text: Tr.t("common.none") },
                        { value: "api-key",      text: Tr.t("ai.apiKey") },
                        { value: "bearer",       text: Tr.t("backend.authHeader") },
                        { value: "oauth-pkce",   text: "OAuth 2.0 (PKCE)" },
                        { value: "oauth-device", text: "OAuth 2.0 (device code)" }
                    ]
                }

                AppTextField {
                    id: secretField
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    placeholderText: Tr.t("backend.authSecretHint")
                }

                AppSectionHeader {
                    Layout.fillWidth: true
                    title: Tr.t("backend.body")
                    uppercase: true
                }

                AppTextArea {
                    id: bodyField
                    Layout.fillWidth: true
                    areaHeight: 110
                    monospaced: true
                    placeholderText: "{\n  \"key\": \"{{value}}\"\n}"
                }

                AppSectionHeader {
                    Layout.fillWidth: true
                    title: Tr.t("backend.timeout")
                    uppercase: true
                }

                AppSlider {
                    id: timeoutSlider
                    Layout.fillWidth: true
                    from: 1
                    to: 120
                    stepSize: 1
                    value: root.endpoint.timeoutMs !== undefined
                           ? root.endpoint.timeoutMs / 1000 : 30
                }

                Text {
                    text: Math.round(timeoutSlider.value) + " " + Tr.t("backend.seconds")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.sizeCaption
                    color: Theme.colour.textTertiary
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space.sm

                    AppButton {
                        text: Tr.t("common.save")
                        variant: "primary"
                        onClicked: {
                            var map = root.collect()
                            if (!Backend.validateEndpoint(map, true))
                                return
                            if (Backend.updateEndpoint(map))
                                root.saved(map)
                        }
                    }

                    AppButton {
                        text: Tr.t("backend.test")
                        variant: "secondary"
                        loading: Backend.testing
                        onClicked: Backend.testEndpoint(root.endpoint.id, {})
                    }

                    AppButton {
                        text: Tr.t("common.delete")
                        variant: "danger"
                        onClicked: {
                            Backend.removeEndpoint(root.endpoint.id)
                            root.endpoint = ({})
                        }
                    }

                    Item { Layout.fillWidth: true }

                    AppButton {
                        text: Tr.t("common.cancel")
                        variant: "ghost"
                        onClicked: root.cancelled()
                    }
                }

                // --- Response / 响应 ---------------------------------------
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.space.md
                    visible: Backend.lastTestResult.ok !== undefined
                    spacing: Theme.space.xs

                    AppSectionHeader {
                        Layout.fillWidth: true
                        title: Tr.t("backend.testResult")
                        uppercase: true
                    }

                    RowLayout {
                        spacing: Theme.space.sm

                        AppBadge {
                            text: Tr.t("backend.status") + " "
                                  + (Backend.lastTestResult.status !== undefined
                                     ? Backend.lastTestResult.status : 0)
                            tone: Backend.lastTestResult.ok ? "success" : "danger"
                        }

                        AppBadge {
                            text: (Backend.lastTestResult.elapsedMs !== undefined
                                   ? Backend.lastTestResult.elapsedMs : 0) + " ms"
                            tone: "neutral"
                            outline: true
                        }
                    }

                    AppTextArea {
                        Layout.fillWidth: true
                        areaHeight: 180
                        monospaced: true
                        wrapText: false
                        readOnly: true
                        text: Backend.lastTestResult.bodyPreview !== undefined
                              ? Backend.lastTestResult.bodyPreview : ""
                    }
                }

                Item { Layout.preferredHeight: Theme.space.xxl }
            }
        }
    }
}
