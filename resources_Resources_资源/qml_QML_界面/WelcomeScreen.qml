// ============================================================================
//  WelcomeScreen.qml -- launch surface shown when no project is open
//  WelcomeScreen.qml —— 未打开工程时显示的启动界面
// ============================================================================
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import TCTools.BuildWeb

Item {
    id: root

    signal newProjectRequested(bool chinese)
    signal openProjectRequested(string path)
    signal browseRequested()

    // Soft, Apple-like backdrop: a very gentle vertical wash rather than a
    // decorative gradient, so text stays perfectly legible.
    // 柔和的 Apple 风格背景：极轻的纵向渐变而非装饰性渐变，确保文字清晰易读。
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            orientation: Gradient.Vertical
            GradientStop { position: 0.0; color: Theme.brush.heroGradient.start }
            GradientStop { position: 1.0; color: Theme.brush.heroGradient.end }
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: Math.min(root.width - Theme.space.xxxl, 880)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.space.xxl

            Item { Layout.preferredHeight: Theme.space.xxxl }

            // --- Hero / 主视觉 ---------------------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.space.md

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: Tr.t("welcome.title")
                    font.family: Theme.fontFamilyDisplay
                    font.pixelSize: Theme.font.sizeHero
                    font.weight: Theme.font.weightBold
                    font.letterSpacing: -0.8
                    color: Theme.colour.text
                    horizontalAlignment: Text.AlignHCenter
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.maximumWidth: 560
                    text: Tr.t("welcome.subtitle")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.sizeTitle3
                    color: Theme.colour.textSecondary
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    lineHeight: 1.4
                }

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: Theme.space.md
                    spacing: Theme.space.md

                    AppButton {
                        text: Tr.t("welcome.newProject")
                        variant: "primary"
                        size: "large"
                        onClicked: root.newProjectRequested(App.chinese)
                    }

                    AppButton {
                        text: Tr.t("welcome.openProject")
                        variant: "secondary"
                        size: "large"
                        onClicked: root.browseRequested()
                    }
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: Theme.space.sm
                    text: Tr.t("welcome.shortcutHint")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.sizeCaption
                    color: Theme.colour.textTertiary
                }
            }

            // --- How it works / 使用步骤 ------------------------------------
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space.lg

                Repeater {
                    model: [
                        { glyph: "\u25A6", titleKey: "mode.design",  hintKey: "mode.designHint" },
                        { glyph: "\u25A4", titleKey: "mode.blocks",  hintKey: "mode.blocksHint" },
                        { glyph: "\u2726", titleKey: "mode.ai",      hintKey: "mode.aiHint" }
                    ]

                    delegate: AppCard {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 132
                        padding: Theme.space.lg

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: Theme.space.sm

                            Text {
                                text: modelData.glyph
                                font.pixelSize: 22
                                color: Theme.colour.accent
                            }

                            Text {
                                text: Tr.t(modelData.titleKey)
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.font.sizeCallout
                                font.weight: Theme.font.weightSemibold
                                color: Theme.colour.text
                            }

                            Text {
                                Layout.fillWidth: true
                                text: Tr.t(modelData.hintKey)
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.font.sizeFootnote
                                color: Theme.colour.textSecondary
                                wrapMode: Text.WordWrap
                                lineHeight: 1.35
                            }

                            Item { Layout.fillHeight: true }
                        }
                    }
                }
            }

            // --- Recent projects / 最近工程 ---------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.space.sm

                AppSectionHeader {
                    Layout.fillWidth: true
                    title: Tr.t("welcome.recent")
                    uppercase: true
                }

                Text {
                    visible: App.recentFiles.length === 0
                    text: Tr.t("welcome.noRecent")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.sizeFootnote
                    color: Theme.colour.textTertiary
                }

                Repeater {
                    model: App.recentFiles

                    delegate: AppCard {
                        required property string modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 52
                        padding: Theme.space.md
                        hoverable: true
                        interactive: true

                        RowLayout {
                            anchors.fill: parent
                            spacing: Theme.space.md

                            Text {
                                text: "\u25A6"
                                font.pixelSize: 15
                                color: Theme.colour.accent
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0

                                Text {
                                    Layout.fillWidth: true
                                    text: {
                                        var parts = modelData.replace(/\\/g, "/").split("/")
                                        return parts[parts.length - 1]
                                    }
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.font.sizeBody
                                    color: Theme.colour.text
                                    elide: Text.ElideMiddle
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: modelData
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.font.sizeCaption
                                    color: Theme.colour.textTertiary
                                    elide: Text.ElideMiddle
                                }
                            }

                            AppIconButton {
                                glyph: "\u2715"
                                tooltip: Tr.t("common.remove")
                                onClicked: App.removeRecentFile(modelData)
                            }
                        }

                        onClicked: root.openProjectRequested(modelData)
                    }
                }
            }

            // --- Recovery banner / 恢复提示 ---------------------------------
            AppCard {
                Layout.fillWidth: true
                visible: App.hasRecoverableAutosave()
                padding: Theme.space.md
                ColumnLayout {
                    anchors.fill: parent
                    spacing: Theme.space.sm

                    Text {
                        text: Tr.t("welcome.recover")
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.sizeCallout
                        font.weight: Theme.font.weightSemibold
                        color: Theme.colour.text
                    }

                    Text {
                        Layout.fillWidth: true
                        text: Tr.t("welcome.recoverDetail")
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.sizeFootnote
                        color: Theme.colour.textSecondary
                        wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        Layout.alignment: Qt.AlignRight
                        spacing: Theme.space.sm

                        AppButton {
                            text: Tr.t("welcome.discardAutosave")
                            variant: "ghost"
                            size: "small"
                            onClicked: App.discardAutosave()
                        }

                        AppButton {
                            text: Tr.t("common.ok")
                            variant: "primary"
                            size: "small"
                            onClicked: {
                                if (App.recoverAutosave())
                                    root.openProjectRequested(App.projectPath)
                            }
                        }
                    }
                }
            }

            // --- Attribution / 署名 ----------------------------------------
            Text {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: Theme.space.lg
                text: Tr.t("app.attribution") + "  ·  v" + App.versionFull
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.sizeCaption
                color: Theme.colour.textTertiary
            }

            Item { Layout.preferredHeight: Theme.space.xxxl }
        }
    }
}
