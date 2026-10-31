import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id:root;objectName:"tasks.background";title:"后台任务";modal:true;anchors.centerIn:parent
    width:650;height:Math.min(580,parent.height-48)
    property var jobs:[]
    function refresh(){jobs=App.invoke("job.list",{}).filter(job=>!!job.python_task)}
    onOpened:refresh()
    Timer{interval:250;running:root.opened;repeat:true;onTriggered:root.refresh()}
    ColumnLayout {
        anchors.fill:parent;spacing:12
        Label{visible:!root.jobs.length;text:"当前没有后台任务。";color:Theme.muted}
        ScrollView {
            Layout.fillWidth:true;Layout.fillHeight:true;clip:true
            ColumnLayout {
                width:parent.width-16;spacing:14
                Repeater {
                    model:root.jobs
                    ColumnLayout {
                        id:row;required property var modelData;Layout.fillWidth:true;spacing:5
                        RowLayout {
                            Layout.fillWidth:true
                            Label{text:row.modelData.node||row.modelData.op;color:Theme.text;Layout.fillWidth:true;elide:Text.ElideRight}
                            Label{text:({running:"运行中",complete:"已完成",cancelled:"已取消",failed:"失败"})[row.modelData.state]||row.modelData.state;color:row.modelData.state==="failed"?Theme.error:Theme.muted}
                            ActionButton{objectName:"tasks.cancel."+row.modelData.id;text:"取消";visible:row.modelData.state==="running";onClicked:App.invoke("job.cancel",{id:row.modelData.id})}
                        }
                        ProgressBar{visible:row.modelData.progress!==undefined;value:row.modelData.progress||0;Layout.fillWidth:true}
                        Label{text:row.modelData.error||row.modelData.message||"";visible:!!text;color:row.modelData.error?Theme.error:Theme.muted;wrapMode:Text.WordWrap;Layout.fillWidth:true}
                    }
                }
            }
        }
    }
    standardButtons:Dialog.Close
}
