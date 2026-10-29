import QtQuick
import QtQuick.Controls

// Shared horizontal switch retains standard Qt input and accessibility rules.
Switch {
    id:root
    implicitWidth:Math.max(42,contentItem.implicitWidth)
    implicitHeight:32
    padding:0;spacing:8
    indicator:Rectangle{
        implicitWidth:42;implicitHeight:24
        x:root.leftPadding;y:(root.height-height)/2
        radius:height/2
        color:root.checked?Theme.blue:Theme.raised
        opacity:root.enabled?1:.45
        border.width:1;border.color:root.down?Theme.text:root.checked?Theme.blue:Theme.border
        Rectangle{
            width:18;height:18;radius:9;y:3
            x:3+root.visualPosition*(parent.width-width-6)
            color:root.checked?Theme.canvas:Theme.text
            Behavior on x{enabled:!root.down;NumberAnimation{duration:90}}
        }
    }
    contentItem:Text{
        text:root.text;font:root.font;color:root.enabled?Theme.text:Theme.muted
        leftPadding:root.indicator.width+root.spacing
        verticalAlignment:Text.AlignVCenter
    }
}
