import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The editor owns destructive gestures and previews their concrete effects.
Item {
    id:root
    property string page:"graph"
    property var ids:[]
    property var proposed:({})
    property var impact:({effects:[]})
    property var replacementChoices:root.page==="layout"&&ids.length&&App.uiItem(ids[0]).plugin?[{id:"layout.row",name:"横向容器"},{id:"layout.column",name:"纵向容器"},{id:"layout.grid",name:"网格容器"},{id:"layout.tabs",name:"分页容器"}]:App.registry.filter(p=>root.page==="graph"||(p.views&&Object.keys(p.views).length))
    function ask(operation,selection,plugin) {
        if(!selection.length||App.readOnly||App.busy)return
        proposed={page:page,ids:selection,operation:operation};if(plugin)proposed.plugin=plugin
        impact=App.invoke("editor.plan",proposed);if(!impact.error)confirmation.open()
    }
    function copy(){if(ids.length)App.invoke("editor.copy",{page:page,ids:ids})}
    function paste(){App.invoke("editor.paste",{page:page})}
    function replace(){if(ids.length)replacement.open()}
    Dialog {
        id:confirmation;objectName:"editor.confirm";modal:true;anchors.centerIn:parent
        width:Math.min(580,parent.width-32);title:root.proposed.operation==="remove"?"删除对象及其影响":root.proposed.operation==="replace"?"替换对象及其影响":"撤下对象及其影响"
        standardButtons:Dialog.Ok|Dialog.Cancel
        contentItem:ScrollView{id:impactScroll;implicitHeight:Math.min(350,Math.max(90,details.implicitHeight+16));clip:true;Label{id:details;width:impactScroll.availableWidth;text:(root.impact.effects||[]).join("\n");wrapMode:Text.WrapAnywhere;color:Theme.text;padding:8}}
        onAccepted:{let args={page:root.page,ids:root.proposed.ids};if(root.proposed.operation==="remove"||root.proposed.operation==="replace"){args.confirmed=true;args.revision=root.impact.revision}if(root.proposed.plugin)args.plugin=root.proposed.plugin;App.invoke("editor."+root.proposed.operation,args)}
    }
    Dialog {
        id:replacement;modal:true;anchors.centerIn:parent;width:420;title:"选择替换插件";standardButtons:Dialog.Ok|Dialog.Cancel
        contentItem:ComboBox{id:replacementPicker;model:root.replacementChoices;textRole:"name";valueRole:"id"}
        onAccepted:root.ask("replace",root.ids,replacementPicker.currentValue)
    }
}
