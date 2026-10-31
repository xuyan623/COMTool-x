import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    required property var bridge
    Label { text:(bridge.properties.display_label||"C++ 输出")+"："+Number((bridge.state.outputs||{}).out||0).toFixed(3);color:bridge.properties.accent||"#3C91B8" }
    Button { objectName:"native_gain.reset";text:"重置";onClicked:bridge.action("reset",{}) }
}
