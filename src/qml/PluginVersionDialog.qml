import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id:root;objectName:"plugins.versions";title:pending.requires_plugins?"工程缺少插件":pending.requires_restart?"切换插件版本需要重启":"选择工程使用的插件版本"
    modal:true;anchors.centerIn:parent;width:Math.min(700,parent.width-48)
    property var pending:({})
    property var choices:({})
    function present(){
        pending=App.pendingPluginOpen
        // A project that carries its own copies is reproducible by default.
        // Only conflicts without a project copy need an explicit local choice.
        const next=Object.assign({},pending.plugin_selections||{})
        for(const row of pending.conflicts||[]) if(!next[row.id]&&row.project_available!==false) next[row.id]="project"
        choices=next;open()
    }
    ColumnLayout {
        width:root.availableWidth;spacing:14
        Label{Layout.fillWidth:true;wrapMode:Text.WordWrap;color:Theme.text;text:root.pending.requires_plugins?"此工程只记录了以下插件依赖，本机尚未具备。请先导入所需插件，再重新打开工程。":root.pending.requires_restart?"此插件的其他版本已经加载。重启后可使用刚才选择的版本打开工程。":"工程内插件与本机插件不同。请选择本次打开工程使用的版本。"}
        Repeater{model:root.pending.missing_plugins||[];delegate:Label{required property var modelData;text:modelData.id+"  "+modelData.version;color:Theme.text;Layout.fillWidth:true;wrapMode:Text.WordWrap}}
        Repeater {
            model:root.pending.conflicts||[]
            delegate:ColumnLayout {
                id:entry;required property var modelData;Layout.fillWidth:true;spacing:5
                Label{Layout.fillWidth:true;text:entry.modelData.name;color:Theme.text;font.bold:true;wrapMode:Text.WordWrap}
                Label{visible:entry.modelData.project_version===entry.modelData.local_version;text:"版本号相同，插件内容不同";color:Theme.muted}
                ComboBox {
                    objectName:"plugins.version."+entry.modelData.id;Layout.fillWidth:true
                    model:["请选择使用的版本",(entry.modelData.project_available===false?"工程未携带版本 ":"工程内版本 ")+entry.modelData.project_version,"本机版本 "+entry.modelData.local_version]
                    currentIndex:root.choices[entry.modelData.id]==="project"?1:root.choices[entry.modelData.id]==="local"?2:0
                    onActivated:{let next=Object.assign({},root.choices);if(currentIndex===1&&entry.modelData.project_available===false){currentIndex=0;return}if(currentIndex)next[entry.modelData.id]=currentIndex===1?"project":"local";else delete next[entry.modelData.id];root.choices=next}
                }
            }
        }
    }
    footer:DialogButtonBox {
        ActionButton {
            objectName:"plugins.version.open"
            text:root.pending.requires_restart?"重启并打开":"打开工程"
            enabled:!root.pending.requires_plugins&&(root.pending.requires_restart||(root.pending.conflicts||[]).every(c=>!!root.choices[c.id]))
            onClicked:{const result=App.invoke(root.pending.requires_restart?"app.restart":"project.open",{path:root.pending.path,plugin_selections:root.choices,discard:true});if(!result.error)root.close()}
        }
        ActionButton{objectName:"plugins.version.cancel";text:"取消";onClicked:root.close()}
    }
}
