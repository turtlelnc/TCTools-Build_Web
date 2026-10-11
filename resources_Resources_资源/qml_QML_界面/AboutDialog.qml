// ============================================================================
//  AboutDialog.qml -- product information, attribution and shortcuts
//  AboutDialog.qml —— 产品信息、署名与快捷键
// ============================================================================
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import TCTools.BuildWeb

Rectangle {
    id: root

    signal closed()

    width: 520
    height: 520
    radius: Theme.radius.xxl
    color: Theme.colour.surfaceRaised
    border.width: 1
    border.color: Theme.colour.separator

    readonly property string repository: "https://github.com/turtlelnc/TCTools-Build_Web"

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
                text: Tr.t("dialog.aboutTitle")
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

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: parent.width - Theme.space.xl * 2
                x: Theme.space.xl
                spacing: Theme.space.md

                Item { Layout.preferredHeight: Theme.space.md }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space.md

                    Rectangle {
                        width: 52
                        height: 52
                        radius: Theme.radius.lg
                        gradient: Gradient {
                            orientation: Gradient.Vertical
                            GradientStop { position: 0.0; color: "#3A8DFF" }
                            GradientStop { position: 1.0; color: "#0A5BD3" }
                        }

                        Text {
                            anchors.centerIn: parent
                            text: "\u25A6"
                            font.pixelSize: 24
                            color: "#FFFFFF"
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            text: Tr.t("app.name")
                            font.family: Theme.fontFamilyDisplay
                            font.pixelSize: Theme.font.sizeTitle2
                            font.weight: Theme.font.weightSemibold
                            color: Theme.colour.text
                        }

                        Text {
                            text: Tr.t("app.version") + " " + App.versionFull
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.font.sizeFootnote
                            color: Theme.colour.textSecondary
                        }

                        Text {
                            Layout.fillWidth: true
                            text: App.platformName() + "  ·  " + App.architecture()
                                  + "  ·  Qt " + Qt.version
                            font.family: Theme.fontFamilyMono
                            font.pixelSize: Theme.font.sizeCaption2
                            color: Theme.colour.textTertiary
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: Tr.t("dialog.aboutBody")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.sizeFootnote
                    color: Theme.colour.textSecondary
                    wrapMode: Text.WordWrap
                    lineHeight: 1.45
                }

                //  Attribution is required by the project brief and must stay
                //  visible in the shipped product, not only in the README.
                //  署名是项目要求的必选项，必须在本产品中可见，而不仅仅出现在 README。
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: attributionText.implicitHeight + Theme.space.md * 2
                    radius: Theme.radius.lg
                    color: Theme.colour.accentWash

                    Text {
                        id: attributionText
                        anchors.centerIn: parent
                        width: parent.width - Theme.space.lg * 2
                        text: Tr.t("app.attribution")
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.sizeCaption
                        color: Theme.colour.textSecondary
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }
                }

                AppSectionHeader {
                    Layout.fillWidth: true
                    title: Tr.t("dialog.shortcutsTitle")
                    uppercase: true
                }

                Repeater {
                    model: [
                        { keys: "Ctrl / ⌘ + N",  key: "action.newProject" },
                        { keys: "Ctrl / ⌘ + O",  key: "action.openProject" },
                        { keys: "Ctrl / ⌘ + S",  key: "action.saveProject" },
                        { keys: "Ctrl / ⌘ + E",  key: "action.export" },
                        { keys: "Ctrl / ⌘ + Z",  key: "action.undo" },
                        { keys: "Ctrl / ⌘ + ⇧ + Z", key: "action.redo" },
                        { keys: "Ctrl / ⌘ + D",  key: "action.duplicate" },
                        { keys: "Ctrl / ⌘ + K",  key: "mode.ai" },
                        { keys: "Ctrl / ⌘ + 1…6", key: "menu.view" },
                        { keys: "Ctrl / ⌘ + ,",  key: "action.settings" }
                    ]

                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: Theme.space.md

                        Text {
                            Layout.preferredWidth: 150
                            text: modelData.keys
                            font.family: Theme.fontFamilyMono
                            font.pixelSize: Theme.font.sizeCaption
                            color: Theme.colour.text
                        }

                        Text {
                            Layout.fillWidth: true
                            text: Tr.t(modelData.key)
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.font.sizeCaption
                            color: Theme.colour.textSecondary
                            elide: Text.ElideRight
                        }
                    }
                }

                AppSectionHeader {
                    Layout.fillWidth: true
                    title: Tr.t("menu.help")
                    uppercase: true
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space.sm

                    AppButton {
                        text: Tr.t("action.documentation")
                        variant: "secondary"
                        size: "small"
                        onClicked: App.openExternalUrl(root.repository
                                                       + "/tree/main/docs_Documentation_%E6%96%87%E6%A1%A3")
                    }

                    AppButton {
                        text: Tr.t("action.reportIssue")
                        variant: "ghost"
                        size: "small"
                        onClicked: App.openExternalUrl(root.repository + "/issues")
                    }

                    Item { Layout.fillWidth: true }
                }

                Item { Layout.preferredHeight: Theme.space.lg }
            }
        }
    }
}
