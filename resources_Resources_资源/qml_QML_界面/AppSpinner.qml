// ============================================================================
//  AppSpinner.qml -- minimal activity indicator
//  AppSpinner.qml —— 极简活动指示器
// ============================================================================
import QtQuick
import TCTools.BuildWeb

Item {
    id: spinner

    property color colour: Theme.colour.accent
    property int lineWidth: 2
    property bool running: true

    implicitWidth: 18
    implicitHeight: 18

    onRunningChanged: if (running) rotationAnim.restart()

    Canvas {
        id: arc
        anchors.fill: parent
        antialiasing: true

        Connections {
            target: spinner
            function onColourChanged() { arc.requestPaint() }
            function onLineWidthChanged() { arc.requestPaint() }
        }

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.clearRect(0, 0, width, height)
            var r = Math.min(width, height) / 2 - spinner.lineWidth
            ctx.strokeStyle = spinner.colour
            ctx.lineWidth = spinner.lineWidth
            ctx.lineCap = "round"
            ctx.beginPath()
            ctx.arc(width / 2, height / 2, r, 0, Math.PI * 1.5)
            ctx.stroke()
            //  A dimmed full ring keeps the indicator readable while it turns.
            //  淡化的整圈让指示器在旋转时更易辨认。
            ctx.globalAlpha = 0.18
            ctx.beginPath()
            ctx.arc(width / 2, height / 2, r, 0, Math.PI * 2)
            ctx.stroke()
            ctx.globalAlpha = 1
        }
    }

    RotationAnimator {
        id: rotationAnim
        target: arc
        from: 0
        to: 360
        duration: 900
        loops: Animation.Infinite
        running: spinner.running && spinner.visible && Theme.motion.normal > 0
    }
}
