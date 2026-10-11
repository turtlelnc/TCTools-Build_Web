// ============================================================================
//  ExportDialog.qml -- choose an export format and destination
//  ExportDialog.qml —— 选择导出格式与目标位置
// ============================================================================
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import TCTools.BuildWeb

Rectangle {
    id: root

    signal closed()

    property string mode: App.exportMode
    property string folder: App.documentsPath()

    width: 640
    height: 520
    radius: Theme.radius.xxl
    color: Theme.colour.surfaceRaised
    border.width: 1
    border.color: Theme.colour.separator

    readonly property var modes: [
        { value: "single-html",      title: "code.mode.singleHtml",
          hint: "code.modeHint.singleHtml" },
        { value: "html-with-assets", title: "code.mode.htmlWithAssets",
          hint: "code.modeHint.htmlWithAssets" },
        { value: "script-only",      title: "code.mode.scriptOnly",
          hint: "code.modeHint.scriptOnly" },
        { value: "headless-bundle",  title: "code.mode.headlessBundle",
          hint: "code.modeHint.headlessBundle" }
    ]

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.space.panelHeader + Theme.space.md

            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.space.xl
                anchors.verticalCenter: parent.verticalCenter
                text: Tr.t("dialog.exportTitle")
                font.family: Theme.fontFamilyDisplay
                font.pixelSize: Theme.font.sizeTitle3
                font.weight: Theme.font.weightSemibold
                color: Theme.colour.text
            }

            AppIconButton {
                anchors.right: parent.right
                anchors.rightMargin: Theme.space.md
                anchors.verticalCenter: parent.verticalCenter
                glyph: "\u2715"
                tooltip: Tr.t("common.close")
                onClicked: root.closed()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.colour.hairline
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.space.xl
            spacing: Theme.space.md

            Text {
                Layout.fillWidth: true
                text: Tr.t("dialog.exportDetail")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.sizeFootnote
                color: Theme.colour.textSecondary
                wrapMode: Text.WordWrap
            }

            // --- Mode grid / 格式网格 ---------------------------------------
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Theme.space.md
                rowSpacing: Theme.space.md

                Repeater {
                    model: root.modes

                    delegate: AppCard {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 88
                        padding: Theme.space.md
                        hoverable: true
                        interactive: true
                        selected: root.mode === modelData.value

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 2

                            Text {
                                Layout.fillWidth: true
                                text: Tr.t(modelData.title)
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.font.sizeFootnote
                                font.weight: Theme.font.weightSemibold
                                color: Theme.colour.text
                                elide: Text.ElideRight
                            }

                            Text {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                text: Tr.t(modelData.hint)
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.font.sizeCaption
                                color: Theme.colour.textSecondary
                                wrapMode: Text.WordWrap
                            }
                        }

                        onClicked: root.mode = modelData.value
                    }
                }
            }

            // --- Destination / 目标位置 --------------------------------------
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space.sm

                AppTextField {
                    Layout.fillWidth: true
                    readOnly: true
                    text: root.folder
                    monospaced: true
                }

                AppButton {
                    text: Tr.t("common.open")
                    variant: "secondary"
                    onClicked: folderDialog.open()
                }
            }

            // --- Planned files / 计划生成的文件 ------------------------------
            AppSectionHeader {
                Layout.fillWidth: true
                title: Tr.t("code.files")
                uppercase: true
            }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: Backend.generatedFileList(root.mode)
                spacing: 2
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: RowLayout {
                    required property var modelData
                    width: ListView.view.width
                    height: 24
                    spacing: Theme.space.sm

                    Text {
                        Layout.fillWidth: true
                        text: modelData.path
                        font.family: Theme.fontFamilyMono
                        font.pixelSize: Theme.font.sizeCaption
                        color: Theme.colour.text
                        elide: Text.ElideMiddle
                    }

                    Text {
                        text: modelData.size + " B"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.sizeCaption2
                        color: Theme.colour.textTertiary
                    }
                }
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Theme.space.sm

                AppButton {
                    text: Tr.t("common.cancel")
                    variant: "ghost"
                    onClicked: root.closed()
                }

                AppButton {
                    text: Tr.t("code.export")
                    variant: "primary"
                    enabled: App.hasProject
                    onClicked: {
                        var summary = App.exportToDirectory(root.folder, root.mode)
                        if (summary !== "")
                            App.setStatusMessage(summary)
                        root.closed()
                    }
                }
            }
        }
    }

    FolderDialog {
        id: folderDialog
        title: Tr.t("code.exportDirectory")
        currentFolder: root.folder
        onAccepted: root.folder = selectedFolder
    }
}
