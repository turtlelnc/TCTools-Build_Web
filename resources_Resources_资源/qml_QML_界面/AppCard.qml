// ============================================================================
//  AppCard.qml -- translucent, rounded surface container
//  AppCard.qml —— 半透明圆角容器
// ============================================================================
import QtQuick
import TCTools.BuildWeb

Rectangle {
    id: card

    //  `default property alias` redirects child declarations into the padded
    //  inner holder, so call sites simply nest their content.
    //  `default property alias` 会把子项重定向到带内边距的内层容器，
    //  因此调用处只需直接嵌套内容即可。
    default property alias contentData: contentHolder.data

    property bool hoverable: false
    property bool interactive: false
    property bool selected: false
    property bool showHairline: true
    property int padding: Theme.space.lg

    signal clicked()

    radius: Theme.radius.xl
    color: card.selected ? Theme.colour.accentWash : Theme.colour.surface
    border.width: 1
    border.color: card.selected ? Theme.colour.accent : Theme.colour.separator
    clip: true

    //  Soft Apple-style elevation drawn as a faint outer hairline rather than
    //  a shader effect, so it stays cheap on integrated GPUs and on the
    //  software renderer used by headless CI.
    //  柔和的 Apple 风格层级感用一条极淡的外描边表现，而非着色器效果，
    //  以便在集成显卡与无头 CI 使用的软件渲染器上保持低开销。
    Rectangle {
        z: -1
        anchors.fill: parent
        anchors.margins: -1
        radius: card.radius + 1
        color: "transparent"
        border.width: 1
        border.color: Theme.colour.hairline
        visible: card.showHairline
    }

    Item {
        id: contentHolder
        anchors.fill: parent
        anchors.margins: card.padding
    }

    MouseArea {
        anchors.fill: parent
        enabled: card.interactive
        cursorShape: card.interactive ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: card.clicked()
    }

    HoverHandler {
        id: hoverHandler
        enabled: card.hoverable
    }

    //  Hover feedback is expressed with opacity rather than a colour tween so
    //  it composes correctly with the `selected` tint.
    //  悬停反馈通过不透明度而非颜色补间表现，以便与选中态的着色正确叠加。
    Binding {
        target: card
        property: "color"
        when: card.hoverable && !card.selected && hoverHandler.hovered
        value: Theme.colour.surfaceRaised
        restoreMode: Binding.RestoreBindingOrValue
    }

    Behavior on border.color {
        ColorAnimation { duration: Theme.motion.fast; easing.type: Theme.motion.easingStandard }
    }
    Behavior on color {
        ColorAnimation { duration: Theme.motion.fast; easing.type: Theme.motion.easingStandard }
    }
}
