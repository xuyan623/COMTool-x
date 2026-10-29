import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Runtime presents the complete design at one uniform scale, with letterboxing.
Item {
    id:root;anchors.fill:parent;objectName:"page.runtime"
    property var props:{App.project;return App.uiItem((App.project.ui||{}).root||"root").properties||({})}
    property real designWidth:props.width||1280
    property real designHeight:props.height||720
    Rectangle{anchors.fill:parent;color:Theme.canvas}
    Item {
        id:screen;anchors.fill:parent;clip:true
        UiItem {
            itemId:(App.project.ui||{}).root||"root";width:root.designWidth;height:root.designHeight
            scale:Math.min(screen.width/width,screen.height/height);transformOrigin:Item.TopLeft
            x:(screen.width-width*scale)/2;y:(screen.height-height*scale)/2
        }
    }
}
