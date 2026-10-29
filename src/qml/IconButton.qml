import QtQuick
import QtQuick.Controls

ToolButton {
    id:root
    property string symbol:""
    property color ink:Theme.text
    implicitWidth:32;implicitHeight:32
    display:AbstractButton.IconOnly
    icon.source:"image://icons/"+symbol+"/"+encodeURIComponent((down?Theme.canvas:enabled?ink:Theme.muted).toString())
    icon.width:21;icon.height:21;icon.color:"transparent"
    Accessible.name:text
    ToolTip.visible:hovered
    ToolTip.delay:400
    ToolTip.text:text
    background:Rectangle{
        radius:3;opacity:root.enabled?1:.45
        color:root.down?Theme.blue:root.checked?Qt.darker(Theme.blue,1.8):root.hovered?Theme.raised:"transparent"
        border.width:root.checked||root.activeFocus?1:0;border.color:Theme.blue
    }
}
