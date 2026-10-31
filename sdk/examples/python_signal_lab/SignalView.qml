import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id:root
    required property var bridge
    color:"#F1F6F9";radius:4
    implicitWidth:320;implicitHeight:180
    ColumnLayout {
        anchors.fill:parent;anchors.margins:16;spacing:10
        Label { text:root.bridge.properties.display_label||"合成信号";color:root.bridge.properties.accent||"#3C91B8";font.pixelSize:20;font.bold:true }
        Label { text:"已生成 "+(((root.bridge.state.state||{}).generated)||0)+" 帧";color:"#405161" }
        Label { text:"RMS "+Number(((root.bridge.state.state||{}).script_state||{}).last_rms||0).toFixed(4);color:"#253744";font.pixelSize:18 }
        RowLayout {
            Button { objectName:"signal.measure";text:"计算 RMS";onClicked:root.bridge.action("measure",{steps:100}) }
            Button { objectName:"signal.appearance";text:"切换强调色";enabled:root.bridge.editable;onClicked:root.bridge.commit({accent:root.bridge.properties.accent==="#3C91B8"?"#AD6634":"#3C91B8"}) }
        }
    }
}
