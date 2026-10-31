import QtQuick

Rectangle {
    required property var bridge
    color:"transparent";radius:4
    border.color:bridge.properties.highlight||"#3C91B8";border.width:2
}
