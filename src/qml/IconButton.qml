import QtQuick
import QtQuick.Controls

ToolButton {
    id:root
    property string symbol:""
    property color ink:Theme.text
    implicitWidth:32;implicitHeight:32
    display:AbstractButton.IconOnly
    icon.source:"image://icons/"+symbol+"/"+encodeURIComponent(ink.toString())
    icon.width:21;icon.height:21;icon.color:"transparent"
    Accessible.name:text
    ToolTip.visible:hovered
    ToolTip.delay:400
    ToolTip.text:text
}
