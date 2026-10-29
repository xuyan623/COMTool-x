import QtQuick
import QtQuick.Controls

Label {
    required property var bridge
    objectName: "example.gain.result"
    text: "增益结果：" + JSON.stringify((bridge.state.state || {}).values || {})
    color: "#66B4F4"
    font.pixelSize: 18
}
