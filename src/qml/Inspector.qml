import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property string page: "graph"
    property string workspace: ""
    property bool matching: workspace.length || (page === "layout" ? App.selectedKind === "ui" : App.selectedKind !== "ui")
    property var inspected: { App.selected; App.selectedKind; App.project; return matching ? App.inspector() : ({}) }
    property var bridge: workspace.length ? App.propertyBridge("workspace",workspace) : matching && App.selected.length && App.selectedKind !== "wire" ? App.propertyBridge(App.selectedKind === "ui" ? "ui" : "node", App.selected) : null
    property bool baseExpanded: false
    color: Theme.panel
    implicitWidth: 306
    onBridgeChanged: baseExpanded = false
    ScrollView {
        anchors.fill: parent; anchors.margins: 12; clip: true
        ColumnLayout {
            width: root.width - 30; spacing: 10
            Label { text:root.workspace?"工作区属性":root.page==="layout"?"界面属性":App.selectedKind==="wire"?"导线属性":"节点属性"; color:Theme.text; font.pixelSize:15; font.bold:true }
            Label { visible:!root.workspace&&(!App.selected.length||!root.matching); text:root.page==="layout"?"选择界面对象，编辑外观与布局。":"选择节点或导线，编辑功能属性。"; color:Theme.muted; wrapMode:Text.WordWrap; Layout.fillWidth:true }
            Label { visible:!!root.bridge&&root.bridge.pending; text:"正在应用…"; color:Theme.muted }
            Label { visible:!!root.bridge&&!!root.bridge.error; text:root.bridge?root.bridge.error:""; color:Theme.error; wrapMode:Text.WordWrap; Layout.fillWidth:true }
            Loader {
                id: customEditor; Layout.fillWidth:true
                Layout.preferredHeight:item?Math.max(item.implicitHeight,item.height):0
                function load() {
                    const url = root.bridge ? root.bridge.info.editor : ""
                    if(url)setSource(url,{bridge:root.bridge})
                    else source=""
                }
                Component.onCompleted:load()
                Connections { target:root; function onBridgeChanged(){customEditor.load()} }
                Connections { target:App; function onCatalogChanged(){customEditor.load()} }
            }
            Label { visible:customEditor.status===Loader.Error; text:"自定义属性面板加载失败，请检查插件 QML。"; color:Theme.error; wrapMode:Text.WordWrap; Layout.fillWidth:true }
            PropertyForm {
                visible:!!root.bridge&&!root.bridge.info.editor; Layout.fillWidth:true
                bridge:root.bridge; immediate:!bridge||bridge.info.commit_mode!=="batch"
            }
            RowLayout {
                visible:!!root.bridge&&root.bridge.dirty&&!root.bridge.info.editor; Layout.fillWidth:true
                ActionButton { objectName:"property.apply"; text:"应用修改"; enabled:!!root.bridge&&!root.bridge.pending; onClicked:root.bridge.commit() }
                ActionButton { objectName:"property.discard"; text:"放弃"; enabled:!!root.bridge&&!root.bridge.pending; onClicked:root.bridge.discard() }
            }
            PropertyForm {
                visible:!root.workspace&&root.matching&&App.selectedKind==="wire"&&App.selected.length>0; Layout.fillWidth:true
                schema:root.inspected.schema||({}); values:root.inspected.values||({}); section:"all"; editable:!App.busy&&!App.readOnly
                onValueEdited:(key,value)=>{let changes={};changes[key]=value;App.invoke("graph.wire.patch",{id:App.selected,changes:changes})}
            }
            ActionButton { objectName:"property.base"; visible:!!root.bridge; text:(root.baseExpanded?"▾ ":"▸ ")+"基础属性"; onClicked:root.baseExpanded=!root.baseExpanded; Layout.fillWidth:true }
            ColumnLayout {
                visible:!!root.bridge&&root.baseExpanded; Layout.fillWidth:true; spacing:10
                Label { text:root.bridge?root.bridge.target:""; color:Theme.muted; font.pixelSize:11; wrapMode:Text.WrapAnywhere; Layout.fillWidth:true }
                TextField {
                    objectName:"property.node_title"; Layout.fillWidth:true; visible:!!root.bridge&&root.bridge.scope==="node"
                    text:root.inspected.item?root.inspected.item.title||"":""; placeholderText:"节点名称"; enabled:!App.readOnly&&!App.busy
                    onEditingFinished:if(root.inspected.item&&text!==root.inspected.item.title)App.invoke("graph.node.patch",{id:App.selected,changes:{title:text}})
                }
                PropertyForm { Layout.fillWidth:true; bridge:root.bridge; section:"base"; immediate:!bridge||bridge.info.commit_mode!=="batch" }
            }
            ActionButton { property bool expanded:false; visible:!!root.bridge&&root.bridge.scope==="node"; text:(expanded?"▾ ":"▸ ")+"运行状态"; onClicked:expanded=!expanded; id:stateToggle }
            TextArea { visible:stateToggle.visible&&stateToggle.expanded; Layout.fillWidth:true; readOnly:true; selectByMouse:true; text:{App.runtime;return JSON.stringify(App.nodeState(App.selected),null,2)} font.family:"Consolas"; font.pixelSize:11; wrapMode:Text.WrapAnywhere; background:null }
        }
    }
}
