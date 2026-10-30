import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id:root;title:mode==="options"?"工程保存选项":mode==="export"?"导出工程":"保存工程"
    modal:true;anchors.centerIn:parent;width:Math.min(560,parent.width-48)
    property string mode:"save"
    property bool includePlugins:true
    signal fileRequested(string operation,bool includePlugins)
    function present(mode){root.mode=mode;root.includePlugins=(App.project.save_options||{}).include_plugins!==false;open()}
    ColumnLayout {
        width:root.availableWidth;spacing:12
        CheckBox{objectName:"project.options.include_plugins";text:"包含工程使用的全部插件";checked:root.includePlugins;onToggled:root.includePlugins=checked}
        Label{Layout.fillWidth:true;text:root.includePlugins?"保存所用官方、用户插件及随包资源，换电脑后可从工程内加载。":"只记录所需插件和版本，打开时需要本机具有这些插件。";color:Theme.muted;wrapMode:Text.WordWrap}
        Label{Layout.fillWidth:true;text:"可随时在“文件 → 工程保存选项”中修改；普通保存沿用当前设置。";color:Theme.muted;wrapMode:Text.WordWrap}
    }
    footer:DialogButtonBox {
        ActionButton{objectName:"project.options.apply";text:root.mode==="options"?"应用":"选择保存位置…";onClicked:{if(root.mode==="options"){const reply=App.invoke("project.options",{include_plugins:root.includePlugins});if(!reply.error)root.close()}else{root.close();root.fileRequested(root.mode,root.includePlugins)}}}
        ActionButton{text:"取消";onClicked:root.close()}
    }
}
