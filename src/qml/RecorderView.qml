import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id:root
    required property var host
    property var state:host.snapshot.state||({})
    property string fileJob:""
    Connections{target:App;function onJobFinished(id){if(id===root.fileJob)root.fileJob=""}}
    ColumnLayout{
        anchors.fill:parent;spacing:6
        RowLayout{
            Layout.fillWidth:true;spacing:4
            IconButton{
                objectName:"recording."+root.host.nodeId+".toggle"
                symbol:root.state.recording?"pause":"record";ink:root.state.recording?Theme.blue:Theme.error
                text:root.state.recording?"暂停录制":"开始／继续录制"
                enabled:!root.host.designMode&&!!App.runtime.active&&!root.fileJob
                onClicked:root.host.action(root.state.recording?"pause":"start",{})
            }
            IconButton{
                objectName:"recording."+root.host.nodeId+".save";symbol:"save";text:"结束并保存录制"
                enabled:!root.host.designMode&&Number(root.state.records||0)>0&&!root.fileJob
                onClicked:{root.host.action("pause",{});saveFile.open()}
            }
            IconButton{
                objectName:"recording."+root.host.nodeId+".reset";symbol:"reset";text:"重置录制：清空本次内容"
                enabled:!root.host.designMode&&!root.fileJob
                onClicked:root.host.action("reset",{})
            }
            Item{Layout.fillWidth:true}
            Label{text:Number(root.state.duration||0).toFixed(2)+" s";color:Theme.text;font.family:"Consolas"}
        }
        Label{Layout.fillWidth:true;text:(root.state.records||0)+" 条 · "+((root.state.written_bytes||0)/1024).toFixed(1)+" KiB";color:Theme.muted;font.pixelSize:12;elide:Text.ElideRight}
        Label{Layout.fillWidth:true;visible:!!root.state.last_error||!!root.state.saved_path;text:root.state.last_error||root.state.saved_path||"";color:root.state.last_error?Theme.error:Theme.muted;elide:Text.ElideMiddle;font.pixelSize:11}
        Item{Layout.fillHeight:true}
    }
    FileDialog{
        id:saveFile;title:"保存录制文件";fileMode:FileDialog.SaveFile;defaultSuffix:"crrc"
        nameFilters:["CoRig 录制 (*.crrc)"]
        onAccepted:{const job=root.host.action("save",{path:selectedFile.toString()});root.fileJob=job.job||""}
    }
}
