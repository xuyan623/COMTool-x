import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id:root;title:"检查更新";modal:true;anchors.centerIn:parent;width:Math.min(540,parent.width-48)
    property string checking:""
    property string error:""
    property var checkResult:({})
    ColumnLayout {
        width:root.availableWidth;spacing:14
        Label{Layout.fillWidth:true;color:Theme.text;wrapMode:Text.WordWrap;text:root.checking?"正在检查更新…":root.error?root.error:root.checkResult.checked_at?(root.checkResult.available?"发现可用更新。保存工程后，可启动安装式更新。":"当前已安装的内容没有可用更新。"):"检查主程序、官方插件与运行依赖的可用更新。"}
        Repeater{model:root.checkResult.updates||[];delegate:Label{required property var modelData;Layout.fillWidth:true;color:Theme.muted;wrapMode:Text.WordWrap;text:(modelData.displayname||modelData.name||"更新")+" "+(modelData.version||"")}}
        RowLayout {
            ActionButton{objectName:"updates.check";text:"检查更新";enabled:!root.checking;onClicked:{root.error="";const reply=App.invoke("update.check",{});if(reply.error)root.error=reply.error;else root.checking=reply.job||""}}
            ActionButton{objectName:"updates.install";text:"开始更新…";enabled:!root.checking&&!!root.checkResult.available;onClicked:{const reply=App.invoke("update.install",{});if(reply.error)root.error=reply.error;else root.close()}}
        }
        Label{visible:App.dirty;Layout.fillWidth:true;color:Theme.muted;text:"当前工程有未保存修改，请先保存。";wrapMode:Text.WordWrap}
    }
    Connections{target:App;function onJobFinished(id){if(id===root.checking){const job=App.job(id);root.checking="";if(job.state==="complete")root.checkResult=job.result;else root.error=job.error||"更新检查失败"}}}
    footer:DialogButtonBox{ActionButton{objectName:"updates.close";text:"关闭";onClicked:root.close()}}
}
