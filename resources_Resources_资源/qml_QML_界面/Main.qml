// ============================================================================
//  Main.qml -- the application shell
//  Main.qml —— 应用程序外壳
//
//  A three-pane layout in the Apple idiom: a mode rail on the far left, a
//  contextual sidebar, the working surface in the centre and a detail panel on
//  the right, with a translucent header and a quiet status bar.
//  Apple 风格的三栏布局：最左侧为模式导航条，其次是上下文侧栏，中间是工作区，
//  右侧是详情面板，并配有半透明顶栏与克制安静的状态栏。
// ============================================================================
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import TCTools.BuildWeb

ApplicationWindow {
    id: window

    visible: true
    width: 1440
    height: 900
    minimumWidth: 1040
    minimumHeight: 660
    color: Theme.colour.canvas
    title: App.hasProject
           ? (App.modified ? "\u2022 " : "") + App.projectName + " — " + Tr.t("app.short")
           : Tr.t("app.name")

    // -----------------------------------------------------------------------
    //  Shell state
    //  外壳状态
    // -----------------------------------------------------------------------
    property string mode: "design"
    property bool sidebarVisible: true
    property bool rightPanelVisible: true
    property bool welcomeVisible: !App.hasProject
    property bool settingsVisible: false
    property bool exportVisible: false
    property bool aboutVisible: false
    property string modeBeforeWelcome: "design"

    readonly property var modeItems: [
        { value: "design",  text: Tr.t("mode.design"),  glyph: "\u25A6" },
        { value: "blocks",  text: Tr.t("mode.blocks"),  glyph: "\u25A4" },
        { value: "ai",      text: Tr.t("mode.ai"),      glyph: "\u2726" },
        { value: "backend", text: Tr.t("mode.backend"), glyph: "\u21C4" },
        { value: "code",    text: Tr.t("mode.code"),    glyph: "\u2039\u203A" },
        { value: "preview", text: Tr.t("mode.preview"), glyph: "\u25C9" }
    ]

    readonly property string modeHint: Tr.t("mode." + mode + "Hint")

    function notify(message, tone) {
        if (message === undefined || message === "")
            return
        toast.show(message, tone === undefined ? "info" : tone)
    }

    function setMode(value) {
        if (mode === value)
            return
        mode = value
        if (value === "preview" && App.hasProject)
            Backend.refreshPreviewFiles()
    }

    // -----------------------------------------------------------------------
    //  Controller notices
    //  控制器通知
    // -----------------------------------------------------------------------
    Connections {
        target: Agent
        function onNotice(messageEn, messageZh, isError) {
            window.notify(App.chinese ? messageZh : messageEn, isError ? "danger" : "info")
        }
        function onAuthorizationSucceeded(vendorId) {
            window.notify(App.chinese ? "授权成功。" : "Authorisation succeeded.", "success")
        }
        function onAuthorizationFailed(message) {
            window.notify(message, "danger")
        }
        function onUserCodeReady(userCode, verificationUri) {
            window.notify(Tr.t("ai.userCodeHint") + "  " + userCode, "info")
        }
    }

    Connections {
        target: Backend
        function onNotice(messageEn, messageZh, isError) {
            window.notify(App.chinese ? messageZh : messageEn, isError ? "danger" : "info")
        }
    }

    Connections {
        target: App
        function onStatusMessageChanged() {
            if (App.statusMessage !== "")
                statusBar.flash(App.statusMessage)
        }
        function onLastErrorChanged() {
            if (App.lastError !== "")
                window.notify(App.lastError, "danger")
        }
        function onProjectChanged() {
            welcomeVisible = !App.hasProject
            if (App.hasProject)
                window.mode = window.modeBeforeWelcome
        }
    }

    // -----------------------------------------------------------------------
    //  Menu bar (native on macOS, in-window elsewhere)
    //  菜单栏（macOS 上为原生菜单，其他平台位于窗口内）
    // -----------------------------------------------------------------------
    menuBar: MenuBar {
        Menu {
            title: Tr.t("menu.file")

            MenuItem {
                text: Tr.t("action.newProject")
                onTriggered: window.newProjectFlow()
            }
            MenuItem {
                text: Tr.t("action.openProject")
                onTriggered: window.openProjectFlow()
            }
            MenuSeparator { }
            MenuItem {
                text: Tr.t("action.saveProject")
                enabled: App.hasProject && App.modified
                onTriggered: App.saveProject()
            }
            MenuItem {
                text: Tr.t("action.saveProjectAs")
                enabled: App.hasProject
                onTriggered: window.saveAsFlow()
            }
            MenuSeparator { }
            MenuItem {
                text: Tr.t("action.export")
                enabled: App.hasProject
                onTriggered: window.exportVisible = true
            }
            MenuSeparator { }
            MenuItem {
                text: Tr.t("action.closeProject")
                enabled: App.hasProject
                onTriggered: window.closeProjectFlow()
            }
            MenuItem {
                text: Tr.t("action.quit")
                onTriggered: Qt.quit()
            }
        }

        Menu {
            title: Tr.t("menu.edit")

            MenuItem {
                text: Tr.t("action.undo") + (App.undoText !== "" ? " — " + App.undoText : "")
                enabled: App.canUndo
                onTriggered: App.undo()
            }
            MenuItem {
                text: Tr.t("action.redo") + (App.redoText !== "" ? " — " + App.redoText : "")
                enabled: App.canRedo
                onTriggered: App.redo()
            }
            MenuSeparator { }
            MenuItem {
                text: Tr.t("action.cut")
                enabled: Builder.hasSelection
                onTriggered: Builder.cutSelected()
            }
            MenuItem {
                text: Tr.t("action.copy")
                enabled: Builder.hasSelection
                onTriggered: Builder.copySelected()
            }
            MenuItem {
                text: Tr.t("action.paste")
                enabled: Builder.canPaste
                onTriggered: Builder.pasteInto("")
            }
            MenuItem {
                text: Tr.t("action.duplicate")
                enabled: Builder.hasSelection
                onTriggered: Builder.duplicateSelected()
            }
            MenuItem {
                text: Tr.t("action.delete")
                enabled: Builder.hasSelection
                onTriggered: Builder.deleteSelected()
            }
        }

        Menu {
            title: Tr.t("menu.view")

            MenuItem {
                text: Tr.t("panel.pages")
                checkable: true
                checked: window.sidebarVisible
                onTriggered: window.sidebarVisible = !window.sidebarVisible
            }
            MenuItem {
                text: Tr.t("panel.inspector")
                checkable: true
                checked: window.rightPanelVisible
                onTriggered: window.rightPanelVisible = !window.rightPanelVisible
            }
            MenuSeparator { }
            MenuItem {
                text: Tr.t("action.toggleGrid")
                checkable: true
                checked: Builder.gridVisible
                onTriggered: Builder.gridVisible = !Builder.gridVisible
            }
            MenuItem {
                text: Tr.t("action.snapToGrid")
                checkable: true
                checked: Builder.snapping
                onTriggered: Builder.snapping = !Builder.snapping
            }
            MenuSeparator { }
            MenuItem {
                text: Tr.t("canvas.desktop")
                onTriggered: App.breakpoint = "desktop"
            }
            MenuItem {
                text: Tr.t("canvas.tablet")
                onTriggered: App.breakpoint = "tablet"
            }
            MenuItem {
                text: Tr.t("canvas.mobile")
                onTriggered: App.breakpoint = "mobile"
            }
        }

        Menu {
            title: Tr.t("menu.help")

            MenuItem {
                text: Tr.t("action.settings")
                onTriggered: window.settingsVisible = true
            }
            MenuSeparator { }
            MenuItem {
                text: Tr.t("action.shortcuts")
                onTriggered: window.aboutVisible = true
            }
            MenuItem {
                text: Tr.t("action.documentation")
                onTriggered: App.openExternalUrl(
                    "https://github.com/turtlelnc/TCTools-Build_Web/tree/main/docs_Documentation_%E6%96%87%E6%A1%A3")
            }
            MenuItem {
                text: Tr.t("action.reportIssue")
                onTriggered: App.openExternalUrl(
                    "https://github.com/turtlelnc/TCTools-Build_Web/issues")
            }
            MenuSeparator { }
            MenuItem {
                text: Tr.t("action.about")
                onTriggered: window.aboutVisible = true
            }
        }
    }

    // -----------------------------------------------------------------------
    //  Window-level flows
    //  窗口级流程
    // -----------------------------------------------------------------------
    //  The QML layer owns every file picker; AppController exposes path-based
    //  entry points so the C++ side never needs a Qt Widgets dependency.
    //  QML 层负责所有文件选择器；AppController 暴露基于路径的入口，
    //  使 C++ 侧无需依赖 Qt Widgets。
    function newProjectFlow() {
        if (App.modified && App.hasProject) {
            unsavedDialog.pendingAction = "new"
            unsavedDialog.open()
        } else {
            App.newProject(App.chinese)
            window.mode = "design"
        }
    }

    function openProjectFlow() {
        if (App.modified && App.hasProject) {
            unsavedDialog.pendingAction = "open"
            unsavedDialog.open()
        } else {
            openFileDialog.open()
        }
    }

    function saveAsFlow() {
        saveFileDialog.currentFile = App.suggestExportFileName()
        saveFileDialog.open()
    }

    function closeProjectFlow() {
        if (App.modified && App.hasProject) {
            unsavedDialog.pendingAction = "close"
            unsavedDialog.open()
        } else {
            App.closeProject()
        }
    }

    function exportSiteFlow() {
        exportFolderDialog.open()
    }

    // -----------------------------------------------------------------------
    //  Shortcuts
    //  快捷键
    // -----------------------------------------------------------------------
    Shortcut {
        sequences: ["Ctrl+N", "Meta+N"]
        onActivated: window.newProjectFlow()
    }
    Shortcut {
        sequences: ["Ctrl+O", "Meta+O"]
        onActivated: window.openProjectFlow()
    }
    Shortcut {
        sequences: ["Ctrl+S", "Meta+S"]
        onActivated: if (App.hasProject) App.saveProject()
    }
    Shortcut {
        sequences: ["Ctrl+Shift+S", "Meta+Shift+S"]
        onActivated: if (App.hasProject) window.saveAsFlow()
    }
    Shortcut {
        sequences: ["Ctrl+E", "Meta+E"]
        onActivated: if (App.hasProject) window.exportVisible = true
    }
    Shortcut {
        sequences: ["Ctrl+Z", "Meta+Z"]
        onActivated: App.undo()
    }
    Shortcut {
        sequences: ["Ctrl+Shift+Z", "Meta+Shift+Z", "Ctrl+Y"]
        onActivated: App.redo()
    }
    Shortcut {
        sequences: ["Ctrl+D", "Meta+D"]
        onActivated: Builder.duplicateSelected()
    }
    //  NOTE: Delete/Backspace is deliberately NOT a window-level shortcut.
    //  A Shortcut with those sequences would steal the key from every text
    //  field in the application. Deletion is instead handled by the canvas,
    //  which only has focus when no text editor does.
    //  注意：Delete/Backspace 有意不作为窗口级快捷键。若设为快捷键，它会从应用内
    //  所有文本输入框中抢走该按键。删除改由画布处理，而画布只在没有文本编辑器
    //  获得焦点时才持有焦点。
    Shortcut {
        sequences: ["Escape"]
        onActivated: {
            if (Builder.hasSelection)
                Builder.clearSelection()
        }
    }
    Shortcut {
        sequences: ["Ctrl+K", "Meta+K"]
        onActivated: {
            if (!App.hasProject)
                return
            window.setMode("ai")
            window.rightPanelVisible = true
            aiView.focusComposer()
        }
    }
    Shortcut {
        sequences: ["Ctrl+1", "Meta+1"]
        onActivated: window.setMode("design")
    }
    Shortcut {
        sequences: ["Ctrl+2", "Meta+2"]
        onActivated: window.setMode("blocks")
    }
    Shortcut {
        sequences: ["Ctrl+3", "Meta+3"]
        onActivated: window.setMode("ai")
    }
    Shortcut {
        sequences: ["Ctrl+4", "Meta+4"]
        onActivated: window.setMode("backend")
    }
    Shortcut {
        sequences: ["Ctrl+5", "Meta+5"]
        onActivated: window.setMode("code")
    }
    Shortcut {
        sequences: ["Ctrl+6", "Meta+6"]
        onActivated: window.setMode("preview")
    }
    Shortcut {
        sequences: ["Ctrl+,", "Meta+,"]
        onActivated: window.settingsVisible = true
    }
    Shortcut {
        sequences: ["Ctrl+0", "Meta+0"]
        onActivated: Builder.setBreakpoint("desktop")
    }

    // -----------------------------------------------------------------------
    //  Header
    //  顶栏
    // -----------------------------------------------------------------------
    header: Rectangle {
        id: headerBar
        height: Theme.space.topBarHeight
        color: Theme.colour.chrome
        border.width: 0

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.colour.hairline
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.space.md
            anchors.rightMargin: Theme.space.md
            spacing: Theme.space.md

            // Brand / 品牌标识
            Row {
                spacing: Theme.space.sm
                Layout.alignment: Qt.AlignVCenter

                Rectangle {
                    width: 22
                    height: 22
                    radius: Theme.radius.sm
                    gradient: Gradient {
                        orientation: Gradient.Vertical
                        GradientStop { position: 0.0; color: "#3A8DFF" }
                        GradientStop { position: 1.0; color: "#0A5BD3" }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: "\u25A6"
                        color: "#FFFFFF"
                        font.pixelSize: 12
                        font.weight: Font.Bold
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: Tr.t("app.short")
                    font.family: Theme.fontFamilyDisplay
                    font.pixelSize: Theme.font.sizeFootnote
                    font.weight: Theme.font.weightSemibold
                    color: Theme.colour.text
                }
            }

            Rectangle {
                width: 1
                height: 18
                color: Theme.colour.separator
                Layout.alignment: Qt.AlignVCenter
            }

            // Document name and dirty marker / 文档名称与未保存标记
            Text {
                Layout.alignment: Qt.AlignVCenter
                Layout.maximumWidth: 260
                text: App.hasProject ? App.projectName : Tr.t("status.noProject")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.sizeFootnote
                color: Theme.colour.textSecondary
                elide: Text.ElideMiddle
            }

            Text {
                visible: App.modified
                Layout.alignment: Qt.AlignVCenter
                text: Tr.t("status.saving")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.sizeCaption
                color: Theme.colour.textTertiary
            }

            Item { Layout.fillWidth: true }

            // Breakpoint switcher / 断点切换
            AppSegmented {
                Layout.alignment: Qt.AlignVCenter
                visible: App.hasProject && window.mode === "design"
                itemHeight: 24
                items: [
                    { value: "desktop", text: Tr.t("canvas.desktop") },
                    { value: "tablet",  text: Tr.t("canvas.tablet") },
                    { value: "mobile",  text: Tr.t("canvas.mobile") }
                ]
                currentValue: App.breakpoint
                onActivated: function (value) { App.breakpoint = value }
            }

            // Undo / redo / panel toggles / 撤销、重做、面板开关
            Row {
                Layout.alignment: Qt.AlignVCenter
                spacing: Theme.space.xxs

                AppIconButton {
                    glyph: "\u21B6"
                    tooltip: Tr.t("action.undo")
                    enabled: App.canUndo
                    onClicked: App.undo()
                }
                AppIconButton {
                    glyph: "\u21B7"
                    tooltip: Tr.t("action.redo")
                    enabled: App.canRedo
                    onClicked: App.redo()
                }
                AppIconButton {
                    glyph: "\u2261"
                    tooltip: Tr.t("panel.pages")
                    active: window.sidebarVisible
                    onClicked: window.sidebarVisible = !window.sidebarVisible
                }
                AppIconButton {
                    glyph: "\u2699"
                    tooltip: Tr.t("action.settings")
                    onClicked: window.settingsVisible = true
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    //  Footer / status bar
    //  底栏 / 状态栏
    // -----------------------------------------------------------------------
    footer: Rectangle {
        id: statusBar
        height: Theme.space.statusBarHeight
        color: Theme.colour.chrome

        property string flashMessage: ""
        property string flashTone: "info"

        function flash(message, tone) {
            flashMessage = message
            flashTone = tone === undefined ? "info" : tone
            flashTimer.restart()
        }

        Timer {
            id: flashTimer
            interval: 2600
            onTriggered: statusBar.flashMessage = ""
        }

        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: Theme.colour.hairline
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.space.md
            anchors.rightMargin: Theme.space.md
            spacing: Theme.space.md

            Text {
                Layout.alignment: Qt.AlignVCenter
                text: statusBar.flashMessage !== "" ? statusBar.flashMessage : window.modeHint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.sizeCaption
                color: statusBar.flashMessage !== "" ? Theme.colour.textSecondary
                                                     : Theme.colour.textTertiary
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            Text {
                Layout.alignment: Qt.AlignVCenter
                visible: App.hasProject
                text: App.pages.length + " " + Tr.t("status.pages")
                      + " · " + Builder.layerTree.length + " " + Tr.t("status.nodes")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.sizeCaption
                color: Theme.colour.textTertiary
            }

            Text {
                Layout.alignment: Qt.AlignVCenter
                visible: Backend.previewRunning
                text: "\u25CF " + Tr.t("backend.previewClients").arg(Backend.previewClients)
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.sizeCaption
                color: Theme.colour.success
            }

            Text {
                Layout.alignment: Qt.AlignVCenter
                visible: !App.hasProject
                text: App.versionFull
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.sizeCaption
                color: Theme.colour.textTertiary
            }
        }
    }

    // -----------------------------------------------------------------------
    //  Body
    //  主体
    // -----------------------------------------------------------------------
    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Mode rail / 模式导航条
        Rectangle {
            id: modeRail
            Layout.fillHeight: true
            Layout.preferredWidth: 52
            color: Theme.colour.surfaceSunken

            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.colour.hairline
            }

            Column {
                anchors.top: parent.top
                anchors.topMargin: Theme.space.sm
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.space.xxs

                Repeater {
                    model: window.modeItems

                    delegate: Item {
                        id: railItem
                        required property var modelData
                        width: 44
                        height: 46

                        Rectangle {
                            anchors.centerIn: parent
                            width: 40
                            height: 40
                            radius: Theme.radius.md
                            color: window.mode === railItem.modelData.value
                                   ? Theme.colour.accent
                                   : (railMouse.containsMouse ? Theme.colour.fill : "transparent")

                            Behavior on color {
                                ColorAnimation {
                                    duration: Theme.motion.fast
                                    easing.type: Theme.motion.easingStandard
                                }
                            }
                        }

                        Column {
                            anchors.centerIn: parent
                            spacing: 1

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: railItem.modelData.glyph
                                font.pixelSize: 15
                                color: window.mode === railItem.modelData.value
                                       ? Theme.colour.textOnAccent
                                       : Theme.colour.textSecondary
                            }

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: railItem.modelData.text
                                font.family: Theme.fontFamily
                                font.pixelSize: 8
                                color: window.mode === railItem.modelData.value
                                       ? Theme.colour.textOnAccent
                                       : Theme.colour.textTertiary
                            }
                        }

                        MouseArea {
                            id: railMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (!App.hasProject) {
                                    window.notify(Tr.t("error.noProject"), "warning")
                                    return
                                }
                                window.setMode(railItem.modelData.value)
                            }
                        }

                        ToolTip.visible: railMouse.containsMouse
                        ToolTip.text: railItem.modelData.text + "  ·  "
                                      + Tr.t("mode." + railItem.modelData.value + "Hint")
                        ToolTip.delay: 500
                    }
                }
            }
        }

        // Contextual sidebar / 上下文侧栏
        SidebarPanel {
            id: sidebar
            Layout.fillHeight: true
            Layout.preferredWidth: window.sidebarVisible ? Theme.space.sidebarWidth : 0
            visible: Layout.preferredWidth > 0
            clip: true
            mode: window.mode

            Behavior on Layout.preferredWidth {
                NumberAnimation {
                    duration: Theme.motion.normal
                    easing.type: Theme.motion.easingStandard
                }
            }
        }

        // Working surface / 工作区
        StackLayout {
            id: workStack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: {
                switch (window.mode) {
                case "design":  return 0
                case "blocks":  return 1
                case "ai":      return 2
                case "backend": return 3
                case "code":    return 4
                case "preview": return 5
                default:        return 0
                }
            }

            DesignView { }
            BlocksView { id: blocksView }
            AiView { id: aiView }
            BackendView { id: backendView }
            CodeView { id: codeView }
            PreviewView { id: previewView }
        }

        // Detail panel / 详情面板
        InspectorPanel {
            id: inspector
            Layout.fillHeight: true
            Layout.preferredWidth: window.rightPanelVisible ? Theme.space.inspectorWidth : 0
            visible: Layout.preferredWidth > 0
            clip: true
            mode: window.mode

            Behavior on Layout.preferredWidth {
                NumberAnimation {
                    duration: Theme.motion.normal
                    easing.type: Theme.motion.easingStandard
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    //  Overlays
    //  叠加层
    // -----------------------------------------------------------------------
    WelcomeScreen {
        id: welcome
        anchors.fill: parent
        visible: window.welcomeVisible
        opacity: window.welcomeVisible ? 1 : 0
        z: 50

        onNewProjectRequested: function (chinese) {
            App.newProject(chinese)
            window.welcomeVisible = false
            window.mode = "design"
        }
        onOpenProjectRequested: function (path) {
            if (App.openProject(path)) {
                window.welcomeVisible = false
                window.mode = "design"
            }
        }
        onBrowseRequested: openFileDialog.open()
    }

    // Toast host / 通知宿主
    Item {
        id: toastHost
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Theme.space.topBarHeight + Theme.space.md
        width: parent.width
        height: 80
        z: 200

        AppToast { id: toast }
    }

    // -----------------------------------------------------------------------
    //  File pickers (QML-owned, see the note in window.newProjectFlow)
    //  文件选择器（由 QML 负责，参见 newProjectFlow 中的说明）
    // -----------------------------------------------------------------------
    FileDialog {
        id: openFileDialog
        title: Tr.t("action.openProject")
        fileMode: FileDialog.OpenFile
        nameFilters: [Tr.t("app.short") + " (*.tcbw)", "*"]
        currentFolder: App.documentsPath()

        onAccepted: {
            if (App.openProject(selectedFile)) {
                window.welcomeVisible = false
                window.mode = "design"
            }
        }
    }

    FileDialog {
        id: saveFileDialog
        title: Tr.t("action.saveProjectAs")
        fileMode: FileDialog.SaveFile
        nameFilters: [Tr.t("app.short") + " (*.tcbw)"]
        currentFolder: App.documentsPath()

        onAccepted: {
            if (App.saveProjectTo(selectedFile))
                window.notify(Tr.t("status.saved"), "success")
        }
    }

    FolderDialog {
        id: exportFolderDialog
        title: Tr.t("dialog.exportTitle")
        currentFolder: App.documentsPath()

        onAccepted: {
            var summary = App.exportToDirectory(selectedFolder, "")
            if (summary !== "")
                window.notify(summary, "success")
        }
    }

    // -----------------------------------------------------------------------
    //  Dialogs
    //  对话框
    // -----------------------------------------------------------------------
    Dialog {
        id: unsavedDialog
        anchors.centerIn: parent
        modal: true
        width: 400
        padding: Theme.space.lg
        title: Tr.t("dialog.unsavedTitle")
        property string pendingAction: "new"

        background: Rectangle {
            radius: Theme.radius.xl
            color: Theme.colour.surfaceRaised
            border.width: 1
            border.color: Theme.colour.separator
        }

        contentItem: Column {
            spacing: Theme.space.md

            Text {
                width: parent.width
                text: Tr.t("dialog.unsavedDetail")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.font.sizeBody
                color: Theme.colour.textSecondary
                wrapMode: Text.WordWrap
            }

            Row {
                anchors.right: parent.right
                spacing: Theme.space.sm

                AppButton {
                    text: Tr.t("dialog.unsavedCancel")
                    variant: "ghost"
                    onClicked: unsavedDialog.close()
                }
                AppButton {
                    text: Tr.t("dialog.unsavedDiscard")
                    variant: "secondary"
                    onClicked: {
                        unsavedDialog.close()
                        unsavedDialog.finish("discard")
                    }
                }
                AppButton {
                    text: Tr.t("dialog.unsavedSave")
                    variant: "primary"
                    onClicked: {
                        unsavedDialog.close()
                        unsavedDialog.finish("save")
                    }
                }
            }
        }

        function finish(choice) {
            var proceed = true
            if (choice === "save") {
                if (App.projectPath !== "") {
                    proceed = App.saveProject()
                } else {
                    proceed = false
                    saveFileDialog.open()
                }
            }
            if (!proceed && choice === "save")
                return

            switch (pendingAction) {
            case "new":
                App.newProject(App.chinese)
                window.welcomeVisible = false
                window.mode = "design"
                break
            case "open":
                openFileDialog.open()
                break
            case "close":
                App.closeProject()
                window.welcomeVisible = true
                break
            }
        }
    }

    SettingsDialog {
        id: settingsDialog
        anchors.centerIn: parent
        visible: window.settingsVisible
        onClosed: window.settingsVisible = false
    }

    ExportDialog {
        id: exportDialog
        anchors.centerIn: parent
        visible: window.exportVisible
        onClosed: window.exportVisible = false
    }

    AboutDialog {
        id: aboutDialog
        anchors.centerIn: parent
        visible: window.aboutVisible
        onClosed: window.aboutVisible = false
    }
}
