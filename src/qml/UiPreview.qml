import QtQuick
import QtQuick.Controls

// Preview real production view components using their declared initial config.
// Preview controls cannot send actions or join the project's computation graph.
Rectangle {
    id:root
    property string plugin:""
    property var spec:{App.registry;return plugin&&["layout.canvas","layout.row","layout.column","layout.grid","layout.tabs"].indexOf(plugin)<0?App.definition(plugin):({})}
    property var config:{const result={},schema=spec.properties||{};Object.keys(schema).forEach(key=>result[key]=schema[key].default);return result}
    property var views:spec.views?(typeof spec.views.length==="number"?Array.from(spec.views):Object.keys(spec.views)):[]
    property string viewKey:views[0]||""
    property string viewUrl:""
    property string presentationUrl:""
    property bool container:spec.kind==="container"||["layout.canvas","layout.row","layout.column","layout.grid","layout.tabs"].indexOf(plugin)>=0
    property bool workspace:spec.kind==="workspace"
    color:Theme.canvas;clip:true
    BuiltinView {anchors.fill:parent;anchors.margins:8;visible:!root.container&&root.views.length>0;nodeId:"";viewKey:root.viewKey;designMode:true;definitionOverride:root.spec;viewUrlOverride:root.viewUrl;designOverride:({plugin:root.plugin,title:root.spec.name||"",config:root.config})}
    UiItem {anchors.fill:parent;anchors.margins:8;visible:root.container;designMode:true;itemId:"";definitionOverride:root.spec;qmlUrlOverride:root.presentationUrl;recordOverride:root.container?({id:"container-preview",plugin:root.plugin,children:[],properties:Object.assign({padding:8},root.config)}):({})}
    QtObject {
        id:previewBridge
        readonly property var properties:root.config
        readonly property var values:root.config
        readonly property var draft:root.config
        readonly property var config:root.config
        readonly property var state:({})
        readonly property bool editable:false
        function commit(changes){return({error:"预览中不可修改属性"})}
        function invoke(operation,args){return({error:"预览中不可执行操作"})}
    }
    Loader {
        id:workspacePreview;anchors.fill:parent;anchors.margins:8;enabled:false
        function load(){if(root.workspace&&root.spec.qml)setSource(root.presentationUrl||App.pluginResourceUrl(root.plugin,root.spec.qml),{bridge:previewBridge});else source=""}
        Component.onCompleted:load()
        Connections{target:root;function onSpecChanged(){workspacePreview.load()}function onPresentationUrlChanged(){workspacePreview.load()}}
    }
    Label {anchors.centerIn:parent;visible:!root.container&&!root.workspace&&!root.views.length;text:"无界面视图";color:Theme.muted;font.pixelSize:11}
}
