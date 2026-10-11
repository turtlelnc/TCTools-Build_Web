// ============================================================================
//  PreviewView.qml -- preview surface and live preview server controls
//  PreviewView.qml —— 预览界面与实时预览服务器控制
// ============================================================================
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import TCTools.BuildWeb

Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // -------------------------------------------------------------------
        //  Toolbar
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
                spacing: Theme.space.md

                AppSegmented {
                    Layout.alignment: Qt.AlignVCenter
                    itemHeight: 24
                    items: [
                        { value: "desktop", text: Tr.t("canvas.desktop") },
                        { value: "tablet",  text: Tr.t("canvas.tablet") },
                        { value: "mobile",  text: Tr.t("canvas.mobile") }
                    ]
                    currentValue: App.breakpoint
                    onActivated: function (value) { App.breakpoint = value }
                }

                AppIconButton {
                    glyph: "\u21BB"
                    tooltip: Tr.t("preview.reload")
                    onClicked: {
                        Backend.refreshPreviewFiles()
                        previewText.text = Backend.generatedFileContents("@preview",
                                                                         App.exportMode)
                    }
                }

                Item { Layout.fillWidth: true }

                AppBadge {
                    visible: Backend.previewRunning
                    text: Backend.previewClients + " " + Tr.t("backend.previewClients")
                    tone: "success"
                    outline: true
                }

                AppButton {
                    text: Backend.previewRunning ? Tr.t("backend.previewStop")
                                                 : Tr.t("backend.previewStart")
                    variant: "secondary"
                    size: "small"
                    onClicked: {
                        if (Backend.previewRunning)
                            Backend.stopPreview()
                        else
                            Backend.startPreview()
                    }
                }

                AppButton {
                    text: Tr.t("preview.openBrowser")
                    variant: "primary"
                    size: "small"
                    enabled: Backend.previewRunning
                    onClicked: Backend.openPreviewInBrowser()
                }
            }
        }

        // -------------------------------------------------------------------
        //  Body
        // -------------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.colour.canvasBackdrop
            clip: true

            AppEmptyState {
                anchors.centerIn: parent
                width: Math.min(parent.width - Theme.space.xxl, 380)
                visible: !App.hasProject
                glyph: "\u25C9"
                title: Tr.t("preview.noProject")
                detail: App.hasProject ? "" : Tr.t("error.noProject")
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.space.lg
                visible: App.hasProject
                spacing: Theme.space.sm

                //  Honest notice: this build has no embedded browser engine, so
                //  the authoritative preview is the real browser.
                //  如实说明：本版本没有内嵌浏览器引擎，因此权威预览方式是真实浏览器。
                RowLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignHCenter
                    spacing: Theme.space.sm

                    Text {
                        text: "\u2139"
                        font.pixelSize: 13
                        color: Theme.colour.info
                    }

                    Text {
                        Layout.fillWidth: true
                        text: Tr.t("preview.hint")
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.sizeCaption
                        color: Theme.colour.textSecondary
                        wrapMode: Text.WordWrap
                    }
                }

                //  Device frame / 设备框架
                Rectangle {
                    id: deviceFrame
                    Layout.alignment: Qt.AlignHCenter
                    Layout.fillHeight: true
                    Layout.preferredWidth: App.breakpoint === "mobile" ? 390
                                          : App.breakpoint === "tablet" ? 720
                                          : Math.min(parent.width - Theme.space.xl, 1100)
                    radius: Theme.radius.lg
                    color: Theme.colour.canvas
                    border.width: 1
                    border.color: Theme.colour.separator

                    Flickable {
                        anchors.fill: parent
                        anchors.margins: Theme.space.md
                        clip: true
                        contentWidth: width
                        contentHeight: previewText.contentHeight

                        TextEdit {
                            id: previewText
                            width: parent.width
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
                            textFormat: TextEdit.PlainText
                            font.family: Theme.fontFamilyMono
                            font.pixelSize: Theme.font.sizeCaption
                            color: Theme.colour.text
                            text: Backend.generatedFileContents("@preview", App.exportMode)
                        }
                    }
                }

                //  Preview server address / 预览服务器地址
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: Theme.space.sm

                    AppBadge {
                        text: Backend.previewRunning ? Backend.previewUrl
                                                     : Tr.t("preview.liveServerOff")
                        tone: Backend.previewRunning ? "accent" : "neutral"
                        outline: true
                    }

                    AppIconButton {
                        glyph: "\u29C9"
                        buttonSize: 22
                        tooltip: Tr.t("backend.previewCopy")
                        enabled: Backend.previewRunning
                        onClicked: Backend.copyPreviewUrl()
                    }
                }
            }
        }
    }
}
