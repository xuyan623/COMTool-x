import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id:root
    required property var host
    property var state:host.snapshot.state||({})
    property string fileJob:""
    property bool operable:!host.designMode&&!!App.runtime.active&&!!state.loaded&&!fileJob
    property var speeds:[0.125,0.25,0.5,1,2,4,8,16]
    Connections{target:App;function onJobFinished(id){if(id===root.fileJob)root.fileJob=""}}
    ColumnLayout{
        anchors.fill:parent;spacing:6
        RowLayout{
            Layout.fillWidth:true;spacing:3
            IconButton{objectName:"playback."+root.host.nodeId+".load";symbol:"open";text:"临时导入录制文件";enabled:!root.host.designMode&&!root.fileJob;onClicked:openFile.open()}
            IconButton{
                objectName:"playback."+root.host.nodeId+".toggle";symbol:root.state.playing?"pause":"run"
                ink:Theme.green;text:root.state.playing?"暂停回放":"播放／继续"
                enabled:root.operable&&!!root.state.cutoff
                onClicked:root.host.action(root.state.playing?"pause":"play",{})
            }
            IconButton{objectName:"playback."+root.host.nodeId+".rewind";symbol:"rewind";text:"回到开头";enabled:root.operable;onClicked:root.host.action("seek",{position:0})}
            IconButton{objectName:"playback."+root.host.nodeId+".previous";symbol:"previous";text:"上一条并暂停";enabled:root.operable;onClicked:root.host.action("step",{direction:-1})}
            IconButton{objectName:"playback."+root.host.nodeId+".next";symbol:"next";text:"下一条并暂停";enabled:root.operable;onClicked:root.host.action("step",{direction:1})}
            Item{Layout.fillWidth:true}
            ToggleSwitch{
                objectName:"playback."+root.host.nodeId+".cutoff";text:"截断";checked:!!root.state.cutoff
                enabled:root.operable;onClicked:root.host.action("cutoff",{value:checked})
                ToolTip.visible:hovered;ToolTip.text:"打开后屏蔽实时输入；关闭后恢复实时直通"
            }
        }
        RowLayout{
            Layout.fillWidth:true;spacing:6
            Slider{
                id:progress;objectName:"playback."+root.host.nodeId+".position";Layout.fillWidth:true;Layout.minimumWidth:48
                from:0;to:Math.max(.000001,Number(root.state.duration||0));enabled:root.operable
                onMoved:root.host.action("seek",{position:value})
                Binding{target:progress;property:"value";value:Number(root.state.position||0);when:!progress.pressed;restoreMode:Binding.RestoreNone}
            }
            ComboBox{
                id:speed;objectName:"playback."+root.host.nodeId+".speed";Layout.preferredWidth:68
                model:root.speeds;currentIndex:Math.max(0,root.speeds.indexOf(Number(root.state.speed||1)))
                displayText:Number(root.state.speed||1)+"×";editable:true;enabled:!root.host.designMode&&!root.fileJob
                onActivated:root.host.action("set_speed",{value:Number(currentValue)})
                onAccepted:{const value=parseFloat(editText);if(isFinite(value))root.host.action("set_speed",{value:value})}
                Binding{target:speed;property:"editText";value:String(root.state.speed||1);when:!speed.activeFocus;restoreMode:Binding.RestoreNone}
            }
            Label{text:"×";color:Theme.muted}
        }
        RowLayout{
            Layout.fillWidth:true
            Label{Layout.fillWidth:true;text:root.state.last_error||root.state.path||"导入文件后可预选进度";color:root.state.last_error?Theme.error:Theme.muted;elide:Text.ElideMiddle;font.pixelSize:11}
            Label{text:Number(root.state.position||0).toFixed(2)+" / "+Number(root.state.duration||0).toFixed(2)+" s";color:Theme.muted;font.family:"Consolas";font.pixelSize:11}
        }
        Item{Layout.fillHeight:true}
    }
    FileDialog{
        id:openFile;title:"导入录制文件";fileMode:FileDialog.OpenFile;nameFilters:["CoRig 录制 (*.crrc)","所有文件 (*)"]
        onAccepted:{const job=root.host.action("load",{path:selectedFile.toString()});root.fileJob=job.job||""}
    }
}
