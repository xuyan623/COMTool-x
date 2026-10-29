import QtQuick
import QtQuick.Controls

// Visible pressed feedback is shared by the actual Qt button implementations.
Button {
    id:root
    implicitWidth:Math.max(64,contentItem.implicitWidth+24)
    implicitHeight:Math.max(30,contentItem.implicitHeight+12)
    padding:6;leftPadding:12;rightPadding:12
    contentItem:Text{
        text:root.text;font:root.font
        color:!root.enabled?Theme.muted:root.down?Theme.canvas:Theme.text
        horizontalAlignment:Text.AlignHCenter;verticalAlignment:Text.AlignVCenter;elide:Text.ElideRight
    }
    background:Rectangle{
        radius:3;opacity:root.enabled?1:.45
        color:root.down?Theme.blue:root.checked?Qt.darker(Theme.blue,1.7):root.hovered?Qt.lighter(Theme.raised,1.18):Theme.raised
        border.width:1;border.color:root.activeFocus||root.checked?Theme.blue:Theme.border
    }
}
