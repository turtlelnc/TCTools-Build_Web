// ============================================================================
//  SettingsDialog.qml -- user preferences
//  SettingsDialog.qml —— 用户偏好设置
// ============================================================================
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import TCTools.BuildWeb

Rectangle {
    id: root

    signal closed()

    property int section: 0
    property var settings: App.editorSettings

    width: 720
    height: 560
    radius: Theme.radius.xxl
    color: Theme.colour.surfaceRaised
    border.width: 1
    border.color: Theme.colour.separator

    function write(key, value) {
        App.updateEditorSetting(key, value)
        root.settings = App.editorSettings
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- Title bar ------------------------------------------------------
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.space.panelHeader + Theme.space.md

            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.space.xl
                anchors.verticalCenter: parent.verticalCenter
                text: Tr.t("settings.title")
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

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // --- Section list ----------------------------------------------
            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: 190
                color: Theme.colour.surfaceSunken

                Column {
                    anchors.fill: parent
                    anchors.margins: Theme.space.sm
                    spacing: 2

                    Repeater {
                        model: [
                            { value: 0, text: Tr.t("settings.appearance") },
                            { value: 1, text: Tr.t("settings.editor") },
                            { value: 2, text: Tr.t("settings.export") },
                            { value: 3, text: Tr.t("settings.ai") },
                            { value: 4, text: Tr.t("settings.privacy") },
                            { value: 5, text: Tr.t("settings.preview") }
                        ]

                        delegate: Rectangle {
                            id: sectionRow
                            required property var modelData

                            width: parent.width
                            height: 32
                            radius: Theme.radius.sm
                            color: root.section === sectionRow.modelData.value
                                   ? Theme.colour.accentSoft
                                   : (sectionHover.hovered ? Theme.colour.fill : "transparent")

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: Theme.space.md
                                anchors.verticalCenter: parent.verticalCenter
                                text: sectionRow.modelData.text
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.font.sizeFootnote
                                font.weight: root.section === sectionRow.modelData.value
                                             ? Theme.font.weightMedium : Theme.font.weightRegular
                                color: Theme.colour.text
                            }

                            HoverHandler { id: sectionHover }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.section = sectionRow.modelData.value
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: Theme.colour.hairline
            }

            // --- Detail ----------------------------------------------------
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

                    // --- Appearance ------------------------------------------
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: root.section === 0
                        spacing: Theme.space.md

                        AppSectionHeader {
                            Layout.fillWidth: true
                            title: Tr.t("settings.theme")
                            uppercase: true
                        }

                        AppSegmented {
                            Layout.fillWidth: true
                            fullWidth: true
                            items: [
                                { value: "light", text: Tr.t("settings.theme.light") },
                                { value: "dark",  text: Tr.t("settings.theme.dark") },
                                { value: "auto",  text: Tr.t("settings.theme.auto") }
                            ]
                            currentValue: root.settings.themeMode
                            onActivated: function (value) { root.write("themeMode", value) }
                        }

                        AppSectionHeader {
                            Layout.fillWidth: true
                            title: Tr.t("settings.language")
                            uppercase: true
                        }

                        AppSegmented {
                            Layout.fillWidth: true
                            fullWidth: true
                            items: [
                                { value: "en", text: Tr.t("settings.language.en") },
                                { value: "zh", text: Tr.t("settings.language.zh") }
                            ]
                            currentValue: App.chinese ? "zh" : "en"
                            onActivated: function (value) { root.write("language", value) }
                        }

                        AppSwitch {
                            Layout.fillWidth: true
                            text: Tr.t("settings.reduceMotion")
                            checked: root.settings.reduceMotion === true
                            onToggled: root.write("reduceMotion", checked)
                        }
                    }

                    // --- Editor ----------------------------------------------
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: root.section === 1
                        spacing: Theme.space.sm

                        AppSwitch {
                            Layout.fillWidth: true
                            text: Tr.t("settings.showGrid")
                            checked: root.settings.showGrid === true
                            onToggled: root.write("showGrid", checked)
                        }
                        AppSwitch {
                            Layout.fillWidth: true
                            text: Tr.t("settings.snapToGrid")
                            checked: root.settings.snapToGrid === true
                            onToggled: root.write("snapToGrid", checked)
                        }
                        AppSwitch {
                            Layout.fillWidth: true
                            text: Tr.t("settings.snapToGuides")
                            checked: root.settings.snapToGuides === true
                            onToggled: root.write("snapToGuides", checked)
                        }
                        AppSwitch {
                            Layout.fillWidth: true
                            text: Tr.t("settings.showRulers")
                            checked: root.settings.showRulers === true
                            onToggled: root.write("showRulers", checked)
                        }
                        AppSwitch {
                            Layout.fillWidth: true
                            text: Tr.t("settings.autoSave")
                            checked: root.settings.autoSave === true
                            onToggled: root.write("autoSave", checked)
                        }
                        AppTextField {
                            Layout.fillWidth: true
                            label: Tr.t("settings.autoSaveInterval")
                            suffixText: "s"
                            text: String(root.settings.autoSaveIntervalSeconds)
                            onEditingFinished: root.write("autoSaveIntervalSeconds",
                                                          parseInt(text, 10) || 120)
                        }
                    }

                    // --- Export ----------------------------------------------
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: root.section === 2
                        spacing: Theme.space.sm

                        AppComboBox {
                            Layout.fillWidth: true
                            model: [
                                { value: "single-html",      text: Tr.t("code.mode.singleHtml") },
                                { value: "html-with-assets", text: Tr.t("code.mode.htmlWithAssets") },
                                { value: "script-only",      text: Tr.t("code.mode.scriptOnly") },
                                { value: "headless-bundle",  text: Tr.t("code.mode.headlessBundle") }
                            ]
                            Component.onCompleted: selectValue(root.settings.defaultExportMode)
                            onActivated: root.write("defaultExportMode", currentValue())
                        }
                        AppSwitch {
                            Layout.fillWidth: true
                            text: Tr.t("settings.prettyPrint")
                            checked: root.settings.prettyPrint === true
                            onToggled: root.write("prettyPrint", checked)
                        }
                        AppSwitch {
                            Layout.fillWidth: true
                            text: Tr.t("settings.includeResetCss")
                            checked: root.settings.includeResetCss === true
                            onToggled: root.write("includeResetCss", checked)
                        }
                        AppSwitch {
                            Layout.fillWidth: true
                            text: Tr.t("settings.openAfterExport")
                            checked: root.settings.openAfterExport === true
                            onToggled: root.write("openAfterExport", checked)
                        }
                        AppTextField {
                            Layout.fillWidth: true
                            label: Tr.t("settings.indentWidth")
                            text: String(root.settings.indentWidth)
                            onEditingFinished: root.write("indentWidth", parseInt(text, 10) || 2)
                        }
                    }

                    // --- AI ---------------------------------------------------
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: root.section === 3
                        spacing: Theme.space.sm

                        AppSwitch {
                            Layout.fillWidth: true
                            text: Tr.t("settings.aiEnabled")
                            checked: Agent.enabled
                            onToggled: Agent.enabled = checked
                        }
                        AppSwitch {
                            Layout.fillWidth: true
                            text: Tr.t("settings.sendOutline")
                            checked: root.settings.sendProjectOutlineToModel === true
                            onToggled: root.write("sendProjectOutlineToModel", checked)
                        }
                        AppSwitch {
                            Layout.fillWidth: true
                            text: Tr.t("settings.redactTranscripts")
                            checked: root.settings.redactSecretsInTranscripts === true
                            onToggled: root.write("redactSecretsInTranscripts", checked)
                        }
                        AppSwitch {
                            Layout.fillWidth: true
                            text: Tr.t("settings.maxToolIterations")
                            checked: false
                            visible: false
                        }
                        AppSectionHeader {
                            Layout.fillWidth: true
                            title: Tr.t("ai.protection")
                            uppercase: true
                        }
                        Text {
                            Layout.fillWidth: true
                            text: Agent.protectionDescription
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.font.sizeCaption
                            color: Theme.colour.textSecondary
                            wrapMode: Text.WordWrap
                        }
                    }

                    // --- Privacy ---------------------------------------------
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: root.section === 4
                        spacing: Theme.space.sm

                        Text {
                            Layout.fillWidth: true
                            text: Tr.t("settings.telemetryOff")
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.font.sizeFootnote
                            color: Theme.colour.textSecondary
                            wrapMode: Text.WordWrap
                        }

                        AppSectionHeader {
                            Layout.fillWidth: true
                            title: Tr.t("settings.privacy")
                            uppercase: true
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "TCTools-Build_Web " + App.versionFull + "  ·  "
                                  + App.platformName() + " / " + App.architecture()
                            font.family: Theme.fontFamilyMono
                            font.pixelSize: Theme.font.sizeCaption
                            color: Theme.colour.textTertiary
                            wrapMode: Text.WordWrap
                        }

                        AppButton {
                            text: Tr.t("settings.resetAll")
                            variant: "danger"
                            size: "small"
                            onClicked: resetConfirm.open()
                        }
                    }

                    // --- Preview ---------------------------------------------
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: root.section === 5
                        spacing: Theme.space.sm

                        AppSwitch {
                            Layout.fillWidth: true
                            text: Tr.t("settings.previewServer")
                            checked: root.settings.previewServerEnabled === true
                            onToggled: root.write("previewServerEnabled", checked)
                        }
                        AppSwitch {
                            Layout.fillWidth: true
                            text: Tr.t("settings.previewReload")
                            checked: root.settings.previewReloadOnChange === true
                            onToggled: root.write("previewReloadOnChange", checked)
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.space.sm
                            AppBadge {
                                text: Backend.previewRunning ? Tr.t("backend.previewStarted")
                                                             : Tr.t("backend.previewStopped")
                                tone: Backend.previewRunning ? "success" : "neutral"
                            }
                            Text {
                                Layout.fillWidth: true
                                text: Backend.previewUrl
                                font.family: Theme.fontFamilyMono
                                font.pixelSize: Theme.font.sizeCaption
                                color: Theme.colour.textTertiary
                                elide: Text.ElideMiddle
                            }
                        }
                    }

                    Item { Layout.preferredHeight: Theme.space.xxl }
                }
            }
        }
    }

    Dialog {
        id: resetConfirm
        anchors.centerIn: parent
        modal: true
        width: 320
        padding: Theme.space.lg
        title: Tr.t("settings.resetAll")

        background: Rectangle {
            radius: Theme.radius.xl
            color: Theme.colour.surfaceRaised
            border.width: 1
            border.color: Theme.colour.separator
        }

        contentItem: ColumnLayout {
            spacing: Theme.space.md

            Text {
                Layout.fillWidth: true
                text: Tr.t("settings.resetConfirm")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.sizeBody
                color: Theme.colour.textSecondary
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Theme.space.sm

                AppButton {
                    text: Tr.t("common.cancel")
                    variant: "ghost"
                    onClicked: resetConfirm.close()
                }

                AppButton {
                    text: Tr.t("common.reset")
                    variant: "danger"
                    onClicked: {
                        //  Every key is written back to its documented default.
                        //  将每个键写回其文档化的默认值。
                        root.write("themeMode", "auto")
                        root.write("language", Qt.locale().name.startsWith("zh") ? "zh" : "en")
                        root.write("reduceMotion", false)
                        root.write("showGrid", false)
                        root.write("gridSize", 8)
                        root.write("snapToGrid", true)
                        root.write("snapToGuides", true)
                        root.write("showRulers", false)
                        root.write("autoSave", true)
                        root.write("autoSaveIntervalSeconds", 120)
                        root.write("defaultExportMode", "single-html")
                        root.write("prettyPrint", true)
                        root.write("indentWidth", 2)
                        root.write("includeResetCss", true)
                        root.write("openAfterExport", false)
                        root.write("previewServerEnabled", true)
                        root.write("previewReloadOnChange", true)
                        root.write("sendProjectOutlineToModel", true)
                        root.write("redactSecretsInTranscripts", true)
                        resetConfirm.close()
                    }
                }
            }
        }
    }
}
