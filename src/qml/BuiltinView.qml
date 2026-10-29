import QtQuick
import QtQuick.Controls

Item {
    id:root
    required property string nodeId
    required property string viewKey
    property bool designMode:false
    property var designOverride:({})
    property var viewProperties:({})
    property var design:{App.project;const live=App.nodeDesign(nodeId);return designOverride.plugin&&(!live.plugin||live.plugin!==designOverride.plugin)?designOverride:live}
    property var definition:{App.registry;return App.definition(design.plugin||"")}
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
    Loader {
        id:content;anchors.fill:parent;enabled:!root.designMode&&!!root.nodeId&&App.nodeDesign(root.nodeId).placed!==false&&App.nodeDesign(root.nodeId).plugin===root.design.plugin
        sourceComponent:root.definition.implementation==="python"?userComponent:root.design.plugin==="plot.curves"?plotComponent:root.design.plugin==="plot.attitude"?attitudeComponent:root.design.plugin==="terminal.ansi"?terminalComponent:root.design.plugin==="debug.console"?consoleComponent:root.design.plugin&&root.design.plugin.startsWith("io.")?connectionComponent:root.viewKey==="values"||root.viewKey==="timer"?valuesComponent:controlComponent
    }
    Component{id:plotComponent;PlotView{host:root}}
    Component{id:attitudeComponent;AttitudeView{host:root}}
    Component{id:terminalComponent;TerminalView{host:root}}
    Component{id:consoleComponent;ConsoleView{host:root}}
    Component{id:connectionComponent;ConnectionView{host:root}}
    Component{id:controlComponent;ControlView{host:root}}
    Component{id:valuesComponent;ValueView{host:root}}
    Component{
        id:userComponent
        Loader{
            id:userLoader
            property string node:root.nodeId
            property string view:root.viewKey
            // Queued reloads may outlive the application's context at exit.
            function load(){if(typeof App!=="undefined"&&node.length&&view.length){const url=App.userViewUrl(node,view);if(url)setSource(url,{bridge:App.userViewBridge(node,view)})}}
            Component.onCompleted:Qt.callLater(load)
            onNodeChanged:Qt.callLater(load)
            onViewChanged:Qt.callLater(load)
            Connections{target:App;function onCatalogChanged(){Qt.callLater(userLoader.load)}}
            Label{anchors.centerIn:parent;visible:parent.status===Loader.Error;text:"用户 QML 视图加载失败";color:Theme.error}
        }
    }
}
