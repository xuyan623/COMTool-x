import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Dialog {
    id:root;title:"选择要导出的插件";modal:true;anchors.centerIn:parent
    width:Math.min(760,parent.width-48);height:Math.min(680,parent.height-48)
    property var inventory:({tree:[],plugins:[]})
    property string scope:"project"
    property string directory:""
    property string exportJob:""
    property var collisions:[]
    function present(scope,plugin){root.scope=scope;const reply=App.invoke("plugin.tree",{scope:scope});if(reply.error)return;inventory=reply;tree.selected=plugin?{[plugin]:true}:{};tree.expanded={};open()}
    function write(overwrite){const reply=App.invoke("plugin.export",{scope:scope,plugins:Object.keys(tree.selected),directory:directory,overwrite:overwrite});if(reply.requires_confirmation){collisions=reply.conflicts;replace.open()}else if(reply.job)exportJob=reply.job}
    ColumnLayout {
        anchors.fill:parent;spacing:10
        Label{Layout.fillWidth:true;text:(root.scope==="project"?"当前工程":"当前插件库")+" · 勾选需要的插件，导出后保留原文件夹层级。";color:Theme.muted;wrapMode:Text.WordWrap}
        RowLayout {
            ActionButton{objectName:"plugin_export.all";text:"全选";onClicked:tree.selectAll()}
            ActionButton{objectName:"plugin_export.none";text:"全不选";onClicked:tree.selected={}}
            ActionButton{objectName:"plugin_export.invert";text:"反选";onClicked:tree.invert()}
            ActionButton{objectName:"plugin_export.only_folder";text:"仅选此分类";enabled:!!tree.focusedFolder;onClicked:tree.onlyFolder()}
            Item{Layout.fillWidth:true}
            Label{text:"已选 "+Object.keys(tree.selected).length+" / "+(root.inventory.plugins||[]).length;color:Theme.muted}
        }
        PluginTree{id:tree;Layout.fillWidth:true;Layout.fillHeight:true;treeData:root.inventory.tree||[];selectionMode:true;enabled:!root.exportJob}
    }
    footer:DialogButtonBox {
        ActionButton{objectName:"plugin_export.export";text:root.exportJob?"正在导出…":"选择导出目录…";enabled:!root.exportJob&&Object.keys(tree.selected).length>0;onClicked:destination.open()}
        ActionButton{objectName:"plugin_export.close";text:"关闭";enabled:!root.exportJob;onClicked:root.close()}
    }
    FolderDialog{id:destination;title:"选择插件导出目录";onAccepted:{root.directory=selectedFolder.toString();root.write(false)}}
    Dialog {
        id:replace;title:"目标文件已存在";modal:true;anchors.centerIn:parent;width:Math.min(580,root.width-32)
        Label{width:parent.width;text:"以下插件文件已存在，是否覆盖？\n\n"+root.collisions.join("\n");wrapMode:Text.WrapAnywhere;color:Theme.text}
        footer:DialogButtonBox{ActionButton{text:"覆盖这些文件";onClicked:{replace.close();root.write(true)}}ActionButton{text:"取消";onClicked:replace.close()}}
    }
    Connections{target:App;function onJobFinished(id){if(id===root.exportJob){root.exportJob="";if(App.job(id).state==="complete")root.close()}}}
}
