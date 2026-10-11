// ============================================================================
//  CodeView.qml -- generated source browser
//  CodeView.qml —— 生成的源代码浏览器
// ============================================================================
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import TCTools.BuildWeb

Item {
    id: root

    property string currentFile: "@preview"

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

                Text {
                    text: Tr.t("code.title")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.font.sizeFootnote
                    font.weight: Theme.font.weightSemibold
                    color: Theme.colour.textSecondary
                }

                AppSegmented {
                    Layout.alignment: Qt.AlignVCenter
                    itemHeight: 24
                    items: [
                        { value: "single-html",      text: Tr.t("code.mode.singleHtml") },
                        { value: "html-with-assets", text: Tr.t("code.mode.htmlWithAssets") },
                        { value: "script-only",      text: Tr.t("code.mode.scriptOnly") },
                        { value: "headless-bundle",  text: Tr.t("code.mode.headlessBundle") }
                    ]
                    currentValue: App.exportMode
                    onActivated: function (value) {
                        App.exportMode = value
                        root.currentFile = "@preview"
                    }
                }

                Item { Layout.fillWidth: true }

                AppButton {
                    text: Tr.t("code.copyAll")
                    variant: "ghost"
                    size: "small"
                    onClicked: App.copyToClipboard(codeArea.text)
                }

                AppButton {
                    text: Tr.t("code.export")
                    variant: "primary"
                    size: "small"
                    enabled: App.hasProject
                    onClicked: exportFolderDialog.open()
                }
            }
        }

        // -------------------------------------------------------------------
        //  File list + source
        // -------------------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: 240
                color: Theme.colour.surfaceSunken

                Rectangle {
                    anchors.right: parent.right
                    width: 1
                    height: parent.height
                    color: Theme.colour.hairline
                }

                ListView {
                    id: fileList
                    anchors.fill: parent
                    anchors.margins: Theme.space.sm
                    clip: true
                    spacing: 2
                    model: Backend.generatedFileList(App.exportMode)
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    delegate: Rectangle {
                        id: fileRow
                        required property var modelData

                        width: fileList.width
                        height: 30
                        radius: Theme.radius.sm
                        color: fileRow.modelData.path === root.currentFile
                               ? Theme.colour.accentSoft
                               : (fileHover.hovered ? Theme.colour.fill : "transparent")

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.space.sm
                            anchors.rightMargin: Theme.space.sm
                            spacing: Theme.space.sm

                            Text {
                                Layout.fillWidth: true
                                text: fileRow.modelData.path
                                font.family: Theme.fontFamilyMono
                                font.pixelSize: Theme.font.sizeCaption
                                color: Theme.colour.text
                                elide: Text.ElideMiddle
                            }

                            Text {
                                text: fileRow.modelData.size + " B"
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.font.sizeCaption2
                                color: Theme.colour.textTertiary
                            }
                        }

                        HoverHandler { id: fileHover }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.currentFile = fileRow.modelData.path
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.colour.codeBackground

                AppEmptyState {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - Theme.space.xxl, 320)
                    visible: !App.hasProject
                    glyph: "\u2039\u203A"
                    title: Tr.t("code.noProject")
                }

                Flickable {
                    id: codeFlick
                    anchors.fill: parent
                    anchors.margins: Theme.space.md
                    visible: App.hasProject
                    clip: true
                    contentWidth: Math.max(width, codeArea.contentWidth + lineNumbers.width + Theme.space.md)
                    contentHeight: Math.max(height, codeArea.contentHeight)

                    Row {
                        spacing: Theme.space.sm

                        //  Line-number gutter / 行号栏
                        Text {
                            id: lineNumbers
                            width: 44
                            text: root.lineNumbersFor(codeArea.text)
                            font.family: Theme.fontFamilyMono
                            font.pixelSize: Theme.font.sizeFootnote
                            color: Theme.colour.textTertiary
                            horizontalAlignment: Text.AlignRight
                            lineHeight: 1.5
                        }

                        TextEdit {
                            id: codeArea
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.NoWrap
                            textFormat: TextEdit.PlainText
                            font.family: Theme.fontFamilyMono
                            font.pixelSize: Theme.font.sizeFootnote
                            color: Theme.colour.codeText
                            text: Backend.generatedFileContents(root.currentFile, App.exportMode)
                            lineHeight: 1.5
                        }
                    }
                }

                //  Footer with counts / 底部统计信息
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 24
                    color: Theme.colour.chrome

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.space.md
                        anchors.verticalCenter: parent.verticalCenter
                        text: codeArea.lineCount + " " + Tr.t("code.lines") + "  ·  "
                              + codeArea.length + " " + Tr.t("code.characters")
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.font.sizeCaption2
                        color: Theme.colour.textTertiary
                    }
                }
            }
        }
    }

    //  Builds a right-aligned gutter string matching the source line count.
    //  生成与源码行数匹配的右对齐行号字符串。
    function lineNumbersFor(text) {
        var lines = (text === "" ? 0 : text.split("\n").length)
        var out = ""
        for (var i = 1; i <= lines; ++i)
            out += (i < lines ? i + "\n" : String(i))
        return out
    }

    FolderDialog {
        id: exportFolderDialog
        title: Tr.t("dialog.exportTitle")
        onAccepted: {
            var summary = App.exportToDirectory(selectedFolder, App.exportMode)
            if (summary !== "")
                App.setStatusMessage(summary)
        }
    }
}
