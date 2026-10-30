import QtQuick

Item {
    id:root
    property string plugin:""
    property var geometry:{App.registry;return plugin.length&&!plugin.startsWith("layout.")?App.previewGeometry(plugin):({width:180,height:104,ports:[],center:[0,0,180,104],title:"布局容器",icon:"layout"})}
    property real previewScale:Math.min((width-28)/geometry.width,(height-18)/geometry.height,1)
    NodeCapsule { visible:!root.plugin.startsWith("layout.")&&!root.geometry.error;geometry:root.geometry;x:(root.width-width*root.previewScale)/2;y:(root.height-height*root.previewScale)/2;scale:root.previewScale;transformOrigin:Item.TopLeft }
    Text{anchors.fill:parent;anchors.margins:10;visible:!!root.geometry.error;text:root.geometry.error||"";wrapMode:Text.WrapAnywhere;color:Theme.error;font.pixelSize:11}
    Text{anchors.centerIn:parent;visible:root.plugin.startsWith("layout.");text:"仅用于界面布局";color:Theme.muted;font.pixelSize:11}
}
