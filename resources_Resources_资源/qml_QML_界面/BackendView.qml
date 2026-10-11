// ============================================================================
//  BackendView.qml -- endpoints, scaffold generator and preview server
//  BackendView.qml —— 接口端点、脚手架生成器与预览服务器
// ============================================================================
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import TCTools.BuildWeb

Item {
    id: root

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // -------------------------------------------------------------------
        //  Endpoint list and editor
        // -------------------------------------------------------------------
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 300
            color: Theme.colour.surfaceSunken

            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.colour.hairline
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: Theme.space.sm

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.space.panelHeader + Theme.space.sm

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.space.md
                        anchors.rightMargin: Theme.space.sm

                        Text {
                            Layout.fillWidth: true
                            text: Tr.t("backend.endpoints")
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.font.sizeFootnote
                            font.weight: Theme.font.weightSemibold
                            color: Theme.colour.textTertiary
                        }

                        AppIconButton {
                            glyph: "\uFF0B"
                            buttonSize: 22
                            tooltip: Tr.t("backend.addEndpoint")
                            onClicked: {
                                var created = Backend.addEndpoint(Backend.emptyEndpointTemplate())
                                if (created.id !== undefined)
                                    endpointEditor.loadEndpoint(created)
                            }
                        }
                    }
                }

                ListView {
                    id: endpointList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.leftMargin: Theme.space.sm
                    Layout.rightMargin: Theme.space.sm
                    clip: true
                    model: Backend.endpoints
                    spacing: 3
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    delegate: Rectangle {
                        id: endpointRow
                        required property var modelData

                        width: endpointList.width
                        height: 46
                        radius: Theme.radius.md
                        color: endpointHover.hovered ? Theme.colour.fill : Theme.colour.surface
                        border.width: 1
                        border.color: Theme.colour.hairline

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: Theme.space.sm
                            spacing: 0

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.space.xs

                                AppBadge {
                                    text: endpointRow.modelData.method
                                    tone: endpointRow.modelData.valid ? "accent" : "warning"
                                    outline: true
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: endpointRow.modelData.name
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.font.sizeFootnote
                                    color: Theme.colour.text
                                    elide: Text.ElideRight
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: endpointRow.modelData.url
                                font.family: Theme.fontFamilyMono
                                font.pixelSize: Theme.font.sizeCaption2
                                color: Theme.colour.textTertiary
                                elide: Text.ElideMiddle
                            }
                        }

                        HoverHandler { id: endpointHover }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: endpointEditor.loadEndpoint(endpointRow.modelData)
                        }
                    }
                }

                AppEmptyState {
                    Layout.fillWidth: true
                    Layout.margins: Theme.space.md
                    visible: Backend.endpoints.length === 0
                    compact: true
                    glyph: "\u21C4"
                    title: Tr.t("backend.noEndpoints")
                    detail: Tr.t("backend.noEndpointsHint")
                }

                // --- Scaffold generator / 脚手架生成器 ----------------------
                AppSectionHeader {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.space.md
                    Layout.rightMargin: Theme.space.md
                    title: Tr.t("backend.scaffold")
                    subtitle: Tr.t("backend.scaffoldHint")
                    uppercase: true
                }

                AppComboBox {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.space.md
                    Layout.rightMargin: Theme.space.md
                    model: Backend.scaffoldOptions()
                    onActivated: Backend.scaffoldTargetId = currentValue()
                    Component.onCompleted: selectValue(Backend.scaffoldTargetId)
                }

                AppButton {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.space.md
                    Layout.rightMargin: Theme.space.md
                    Layout.bottomMargin: Theme.space.md
                    text: Tr.t("backend.scaffoldCreate")
                    variant: "secondary"
                    size: "small"
                    onClicked: scaffoldFolderDialog.open()
                }
            }
        }

        // -------------------------------------------------------------------
        //  Endpoint editor / 端点编辑器
        // -------------------------------------------------------------------
        EndpointEditor {
            id: endpointEditor
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }

    FolderDialog {
        id: scaffoldFolderDialog
        title: Tr.t("backend.scaffold")
        onAccepted: {
            var summary = Backend.scaffoldBackend(Backend.scaffoldTargetId,
                                                  selectedFolder, {})
            if (summary !== "")
                App.setStatusMessage(summary)
        }
    }
}
