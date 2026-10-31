import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id:root
    required property var bridge
    property var design:({})
    color:"#EAF1F5";implicitWidth:600;implicitHeight:360
    function observe(){if(bridge.scope==="workspace")design=bridge.invoke("project.get",{})}
    Component.onCompleted:observe()
    Timer{interval:500;running:root.bridge.scope==="workspace";repeat:true;onTriggered:root.observe()}
    ColumnLayout {
        anchors.fill:parent;anchors.margins:28;spacing:16
        Label{objectName:"overview.heading";text:root.bridge.values.heading||"实验总览";font.pixelSize:26;font.bold:true;color:"#253744"}
        Label{text:root.design.name||"工作区预览";font.pixelSize:18;color:"#405161"}
        RowLayout {
            spacing:24
            Label{text:"节点 "+(root.design.nodes||[]).length;color:"#405161"}
            Label{text:"导线 "+(root.design.wires||[]).length;color:"#405161"}
        }
        TextArea {
            id:note;objectName:"overview.note";Layout.fillWidth:true;Layout.fillHeight:true
            text:root.bridge.values.note||"";placeholderText:"记录本次实验";wrapMode:Text.Wrap;selectByMouse:true
            enabled:root.bridge.editable
            onTextChanged:if(activeFocus)root.bridge.stageText("note",text)
        }
        RowLayout {
            Button{objectName:"overview.save";text:"保存记录";enabled:root.bridge.editable;onClicked:root.bridge.commit()}
            Button{objectName:"overview.graph";text:"功能设计";enabled:root.bridge.scope==="workspace";onClicked:root.bridge.invoke("workspace.open",{page:"graph"})}
        }
    }
}
