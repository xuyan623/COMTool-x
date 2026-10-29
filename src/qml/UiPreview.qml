import QtQuick
import QtQuick.Controls

// Preview real production view components using their declared initial config.
// Preview controls cannot send actions or join the project's computation graph.
Rectangle {
    id:root
    property string plugin:""
    property var spec:{App.registry;return plugin&&!plugin.startsWith("layout.")?App.definition(plugin):({})}
    property var config:{const result={},schema=spec.properties||{};Object.keys(schema).forEach(key=>result[key]=schema[key].default);return result}
    property var views:spec.views?(typeof spec.views.length==="number"?Array.from(spec.views):Object.keys(spec.views)):[]
    property bool container:plugin.startsWith("layout.")
    color:Theme.canvas;clip:true
    BuiltinView {anchors.fill:parent;anchors.margins:8;visible:!root.container&&root.views.length>0;nodeId:"";viewKey:root.views[0]||"";designMode:true;designOverride:({plugin:root.plugin,title:root.spec.name||"",config:root.config})}
    UiItem {anchors.fill:parent;anchors.margins:8;visible:root.container;designMode:true;itemId:"";recordOverride:root.container?({id:"container-preview",plugin:root.plugin,children:[],properties:{padding:8}}):({})}
    Label {anchors.centerIn:parent;visible:!root.container&&!root.views.length;text:"无界面视图";color:Theme.muted;font.pixelSize:11}
}
