import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Folder rows share the filesystem hierarchy across browsing and export.
// ListView creates only visible rows; collapsed children retain selection.
ListView {
    id:root
    property var treeData:[]
    property bool showPlugins:true
    property bool selectionMode:false
    property bool enableMode:false
    property bool enableEditable:true
    property var packageStates:({})
    property var selected:({})
    property var expanded:({})
    property string activePath:""
    property string activePlugin:""
    property string focusedFolder:""
    property string query:""
    property string controlPrefix:"plugin_tree"
    signal categoryChosen(string path)
    signal pluginChosen(string packageId)
    signal enableRequested(var packages,bool value)
    clip:true;reuseItems:true;boundsBehavior:Flickable.StopAtBounds;spacing:1
    ScrollBar.vertical:ScrollBar{}
    function ids(node){if(node.kind==="plugin")return[node.id];let result=[];(node.children||[]).forEach(n=>result=result.concat(ids(n)));return result}
    function allIds(){let result=[];treeData.forEach(n=>result=result.concat(ids(n)));return result}
    function select(node,value,exclusive){const next=exclusive?{}:Object.assign({},selected);ids(node).forEach(id=>{if(value)next[id]=true;else delete next[id]});selected=next}
    function selectAll(){const next={};allIds().forEach(id=>next[id]=true);selected=next}
    function invert(){const next={};allIds().forEach(id=>{if(!selected[id])next[id]=true});selected=next}
    function state(node){
        const children=ids(node)
        if(enableMode){
            const checked=children.filter(id=>packageStates[id]===Qt.Checked).length
            const unchecked=children.filter(id=>packageStates[id]===undefined||packageStates[id]===Qt.Unchecked).length
            return !children.length||unchecked===children.length?Qt.Unchecked:checked===children.length?Qt.Checked:Qt.PartiallyChecked
        }
        const count=children.filter(id=>!!selected[id]).length
        return !count?Qt.Unchecked:count===children.length?Qt.Checked:Qt.PartiallyChecked
    }
    function toggle(path){const next=Object.assign({},expanded);next[path]=expanded[path]===false;expanded=next}
    function expandAll(value){const next={};function visit(nodes){nodes.forEach(n=>{if(n.kind==="folder"){next[n.path]=value;visit(n.children||[])}})}visit(treeData);expanded=next}
    function onlyFolder(){function find(nodes){for(const n of nodes){if(n.kind==="folder"&&n.path===focusedFolder)return n;const nested=find(n.children||[]);if(nested)return nested}return null}const node=find(treeData);if(node)select(node,true,true)}
    // Searches keep matching ancestors and reveal descendants independently of
    // the user's saved expansion choices; clearing search restores those choices.
    readonly property var rows:{
        const result=[],term=root.query.trim().toLowerCase()
        function ownMatch(n){return (n.name+" "+(n.id||"")+" "+(n.relative_path||n.path||"")).toLowerCase().indexOf(term)>=0}
        function matches(n){return ownMatch(n)||(n.children||[]).some(matches)}
        function visit(nodes,depth,parentMatch){nodes.forEach(n=>{
            if(n.kind==="plugin"&&!root.showPlugins)return
            if(term&&!parentMatch&&!matches(n))return
            result.push({node:n,depth:depth})
            if(n.kind==="folder"&&(term||root.expanded[n.path]!==false))visit(n.children||[],depth+1,parentMatch||!!term&&ownMatch(n))
        })}
        visit(root.treeData,0,false);return result
    }
    model:root.rows
    delegate:RowLayout {
        id:row;required property var modelData;readonly property var node:modelData.node
        width:root.width-12;height:implicitHeight;spacing:0
        Item{Layout.preferredWidth:row.modelData.depth*14;Layout.preferredHeight:1}
        ToolButton {
            objectName:root.controlPrefix+".toggle."+row.node.path
            Layout.preferredWidth:24;visible:row.node.kind==="folder"
            text:!root.query&&root.expanded[row.node.path]===false?"▸":"▾"
            enabled:!root.query;onClicked:root.toggle(row.node.path)
        }
        Item{visible:row.node.kind!=="folder";Layout.preferredWidth:24;Layout.preferredHeight:1}
        CheckBox {
            visible:root.selectionMode||root.enableMode;tristate:row.node.kind==="folder"||root.enableMode
            enabled:!root.enableMode||root.enableEditable
            objectName:root.enableMode?"plugins.enabled."+(row.node.kind==="folder"?"folder."+row.node.path:"package."+row.node.id):row.node.kind==="folder"?"plugin_export.folder."+row.node.path:"plugin_export.check."+row.node.id
            checkState:root.state(row.node)
            nextCheckState:function(){return checkState===Qt.Checked?Qt.Unchecked:Qt.Checked}
            onClicked:{
                if(root.enableMode){
                    root.enableRequested(root.ids(row.node),checkState===Qt.Checked)
                    // Rebind after either a rejected request or an async update;
                    // persisted enable state remains the checkbox's authority.
                    checkState=Qt.binding(()=>root.state(row.node))
                }else root.select(row.node,checkState===Qt.Checked,false)
            }
        }
        ItemDelegate {
            id:entry
            objectName:root.controlPrefix+(row.node.kind==="folder"?".folder."+row.node.path:".package."+row.node.id)
            Layout.fillWidth:true;leftPadding:3;rightPadding:4
            text:row.node.name+(row.node.kind==="plugin"?"  "+row.node.version:"")
            contentItem:Label{text:entry.text;font:entry.font;color:Theme.text;elide:Text.ElideRight;verticalAlignment:Text.AlignVCenter}
            highlighted:row.node.kind==="folder"?root.activePath===row.node.path:root.activePlugin===row.node.id
            ToolTip.visible:hovered&&row.node.kind==="plugin";ToolTip.text:row.node.relative_path||""
            onClicked:{if(row.node.kind==="folder"){root.activePath=row.node.path;root.focusedFolder=row.node.path;root.categoryChosen(row.node.path)}else if(root.selectionMode)root.select(row.node,!root.selected[row.node.id],false);else root.pluginChosen(row.node.id)}
        }
    }
}
