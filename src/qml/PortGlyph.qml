import QtQuick

// Port shape owns direction; color supplements the visible English type label.
Canvas {
    id: root
    property bool input: false
    property real nx: 1
    property real ny: 0
    property color ink: "#8BBCE5"
    property bool filled: false
    property bool stub: false
    property bool highlighted: false
    width: 28; height: 28
    onInkChanged: requestPaint()
    onFilledChanged: requestPaint()
    onStubChanged: requestPaint()
    onHighlightedChanged: requestPaint()
    onNxChanged: requestPaint()
    onNyChanged: requestPaint()
    onInputChanged: requestPaint()
    onPaint: {
        const c = getContext("2d"); c.reset(); c.translate(14,14)
        c.strokeStyle = ink; c.fillStyle = ink; c.lineWidth = highlighted ? 2.6 : 1.6
        if (stub) {
            c.lineWidth = 3; c.lineCap = "round"; c.beginPath()
            c.moveTo(-ny*7,nx*7); c.lineTo(ny*7,-nx*7); c.stroke(); return
        }
        c.beginPath()
        if (input) {
            const start = nx < 0 ? -Math.PI/2 : nx > 0 ? Math.PI/2 : ny < 0 ? 0 : Math.PI
            c.arc(0,0,8,start,start+Math.PI)
            if (filled) { c.closePath(); c.fill() }
            c.stroke()
        } else {
            c.arc(0,0,7.5,0,Math.PI*2); c.fill()
            if (highlighted) { c.strokeStyle = "#E5F5FF"; c.stroke() }
        }
    }
}
