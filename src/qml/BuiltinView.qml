import QtQuick
import QtQuick.Controls

Item {
    id:root
    required property string nodeId
    required property string viewKey
    property bool designMode:false
    property var designOverride:({})
    property var definitionOverride:({})
    property string viewUrlOverride:""
    property var viewProperties:({})
    property string uiItem:""
    property var design:{App.project;const live=App.nodeDesign(nodeId);return designOverride.plugin&&(!live.plugin||live.plugin!==designOverride.plugin)?designOverride:live}
    property var definition:{App.registry;return definitionOverride.id?definitionOverride:App.definition(design.plugin||"")}
    property var snapshot:{App.runtime;App.project;return App.nodeDesign(nodeId).plugin===design.plugin?App.nodeState(nodeId):({})}
    property var observation:({})
    readonly property var config:snapshot.config||design.config||({})
    objectName:"view."+nodeId+"."+viewKey
    implicitHeight:design.plugin==="plot.curves"||design.plugin==="plot.attitude"||design.plugin==="terminal.ansi"?340:design.plugin==="debug.console"?320:design.plugin&&design.plugin.startsWith("io.")?200:76
    function action(name,args){if(!designMode&&App.nodeDesign(nodeId).placed!==false&&App.nodeDesign(nodeId).plugin===design.plugin)return App.invoke("node.action",{node:nodeId,action:name,args:args||{}});return({})}
    function viewAction(action,args){if(content.item&&content.item.call)return content.item.call(action,args);return({ok:false,error:"视图不支持该操作"})}
    // Layout loaders assign their item identity after construction. Register
    // once both bindings have settled, including views moved between layouts.
    function register(){if(typeof App!=="undefined"&&nodeId.length&&viewKey.length)App.registerView(root,nodeId,viewKey)}
    Component.onCompleted:Qt.callLater(register)
    onNodeIdChanged:Qt.callLater(register)
    onViewKeyChanged:Qt.callLater(register)
    Connections{target:App;function onViewChanged(key){if(key===root.nodeId+"/"+root.viewKey)root.observation=App.viewState(root.nodeId,root.viewKey)}}
    // External QML gets declared defaults while previewing, with no route to
    // node actions. Real views continue to use the observed runtime bridge.
    QtObject {
        id:previewBridge
        readonly property var state:({})
        readonly property var config:root.config
        readonly property var properties:root.viewProperties
        readonly property var values:root.viewProperties
        readonly property var draft:root.viewProperties
        readonly property bool editable:false
        function action(name,args){return({error:"预览中不可执行动作"})}
        function commit(changes){return({error:"预览中不可修改外观"})}
        function configure(changes){return({error:"预览中不可修改功能参数"})}
    }
    Loader {
        id:content;anchors.fill:parent
        enabled:!root.designMode&&!!root.nodeId&&App.nodeDesign(root.nodeId).placed!==false&&App.nodeDesign(root.nodeId).plugin===root.design.plugin
        function load(){
            if(typeof App==="undefined"||!root.design.plugin||!root.viewKey){source="";return}
            const declaration=(root.definition.views||{})[root.viewKey]||{},url=root.viewUrlOverride||App.pluginViewUrl(root.design.plugin,root.viewKey)
            if(url)setSource(url,declaration.interface==="host"?{host:root}:{bridge:root.designMode?previewBridge:App.userViewBridge(root.nodeId,root.viewKey,root.uiItem)})
            else source=""
        }
        Component.onCompleted:Qt.callLater(load)
        Connections{target:root;function onNodeIdChanged(){Qt.callLater(content.load)}function onViewKeyChanged(){Qt.callLater(content.load)}function onDesignChanged(){Qt.callLater(content.load)}function onDefinitionChanged(){Qt.callLater(content.load)}function onViewUrlOverrideChanged(){Qt.callLater(content.load)}}
        Connections{target:App;function onCatalogChanged(){Qt.callLater(content.load)}}
        Label{anchors.centerIn:parent;visible:content.status===Loader.Error;text:"插件视图加载失败";color:Theme.error}
    }
}
