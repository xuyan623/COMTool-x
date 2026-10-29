import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Every synchronization, including deletions, uses the same checked plan.
Dialog {
    id: root
    objectName:"sync.dialog"
    property var plan: ({changes:[]})
    property var chosen: []
    modal:true;anchors.centerIn:parent;width:Math.min(620,parent?parent.width-32:620)
    title:plan.direction==="graph_to_ui"?"功能设计 → 界面设计":"界面设计 → 功能设计"
    standardButtons:Dialog.Ok|Dialog.Cancel
    function inspect(direction) {
        const result=App.invoke("sync.plan",{direction:direction});if(result.error)return
        plan=result;chosen=(plan.changes||[]).map(c=>c.key);open()
    }
    onAccepted:App.invoke("sync.apply",{direction:plan.direction,revision:plan.revision,keys:chosen})
    contentItem:ColumnLayout {
        spacing:12
        Label{Layout.fillWidth:true;wrapMode:Text.WordWrap;text:"勾选要执行的变更。新增对象进入待放置区；已有摆放与图层保留。取消勾选删除项即可保留目标页对象。";color:Theme.muted}
        ScrollView {
            Layout.fillWidth:true;Layout.preferredHeight:Math.min(360,Math.max(64,(root.plan.changes||[]).length*46));clip:true
            ColumnLayout {
                width:root.availableWidth-16
                Repeater {
                    model:root.plan.changes||[]
                    CheckBox {
                        required property var modelData
                        Layout.fillWidth:true;checked:root.chosen.indexOf(modelData.key)>=0
                        text:modelData.label
                        contentItem:Label {text:parent.text;leftPadding:parent.indicator.width+parent.spacing;wrapMode:Text.WordWrap;color:parent.modelData.action==="remove"?Theme.error:Theme.text;verticalAlignment:Text.AlignVCenter}
                        onClicked:{let values=root.chosen.slice();const at=values.indexOf(modelData.key);if(checked&&at<0)values.push(modelData.key);if(!checked&&at>=0)values.splice(at,1);root.chosen=values}
                    }
                }
                Label{visible:!(root.plan.changes||[]).length;text:"两页已经同步，没有需要执行的变更。";color:Theme.text;Layout.fillWidth:true;wrapMode:Text.WordWrap}
            }
        }
    }
}
