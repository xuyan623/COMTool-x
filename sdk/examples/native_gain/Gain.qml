import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    required property var bridge
    Label { text:"C++ 输出："+Number((bridge.state.outputs||{}).out||0).toFixed(3) }
    Button { objectName:"native_gain.reset";text:"重置";onClicked:bridge.action("reset",{}) }
}
