import QtQuick
import QtQuick.Controls
import ComToolX.Native 1.0

// A reusable capsule keeps library previews and real graph nodes identical.
Item {
    id: root
    property string nodeId: ""
    property var geometry: ({width:180,height:104,ports:[],center:[0,0,180,104]})
    property bool selected: false
    property bool interactive: false
    property color outline: selected ? "#9BD5FF" : Theme.dark ? "#42566B" : "#94ADBF"
    property var connections: ({})
    property var options: ({})
    property string sourceKey: ""
    property string hoveredKey: ""
    property bool linking: sourceKey.length > 0
    signal portPressed(var port)
    width: geometry.width; height: geometry.height
    function key(port) { return nodeId + "/" + port.id }
    function typeColor(type) { return ({number:"#8CBEEA",bytes:"#E4B581",string:"#BDA6E7",boolean:"#8AC9BE",object:"#B8C5D2",samples:"#8ACFA4",any:"#C4D0DB"})[type] || "#C4D0DB" }
    function typeName(type) { return ({number:"Number",bytes:"Bytes",string:"String",boolean:"Boolean",object:"Object",samples:"Samples",trigger:"Trigger",any:"Any"})[type] || type }
    NodeSurface { anchors.fill: parent; geometry: root.geometry; outline: root.outline; emphasized: root.selected; dark: Theme.dark }
    Repeater {
        model: [{area:root.geometry.input_area||[],label:"Input"},{area:root.geometry.output_area||[],label:"Output"}]
        Text {
            required property var modelData
            visible: modelData.area.length===4 && modelData.area[2]>0 && modelData.area[3]>0
            x: modelData.area[0]||0; y:(modelData.area[1]||0)+14
            width:modelData.area[2]||0; text:modelData.label; color:Theme.muted; font.pixelSize:10; font.family:"Segoe UI"; horizontalAlignment:Text.AlignHCenter
        }
    }
    Column {
        x:(root.geometry.center||[0,0,0,0])[0]+8
        y:(root.geometry.center||[0,0,0,0])[1]+((root.geometry.center||[0,0,0,104])[3]-height)/2
        width:(root.geometry.center||[0,0,180,0])[2]-16; spacing:8
        Item {
            width:parent.width; height:34
            Text { anchors.centerIn:parent; visible:!!root.geometry.symbol; text:root.geometry.symbol||""; font.family:"Cambria Math"; font.pixelSize:27; color:Theme.text }
            Image { anchors.centerIn:parent; visible:!root.geometry.symbol; width:32;height:32;source:"image://icons/"+(root.geometry.icon||"graph")+"/"+encodeURIComponent(Theme.text.toString());sourceSize.width:64;sourceSize.height:64 }
        }
        Text { width:parent.width; text:root.geometry.title||"";font.pixelSize:12;font.family:"Microsoft YaHei UI";color:Theme.text;horizontalAlignment:Text.AlignHCenter;elide:Text.ElideRight;maximumLineCount:2;wrapMode:Text.Wrap }
    }
    Repeater {
        model:root.geometry.ports||[]
        Item {
            id: port
            required property var modelData
            property string portKey:root.key(modelData)
            property var connection:root.connections[portKey]||({})
            property var option:root.options[portKey]||({})
            property bool compatible:!!option.compatible
            property bool engaged:root.sourceKey===portKey
            property bool preview:root.linking&&compatible&&root.hoveredKey===portKey
            x:modelData.x;y:modelData.y
            opacity:root.linking&&!compatible&&!engaged?.32:1
            Column {
                x:port.modelData.label_x-port.x;y:port.modelData.label_y-port.y;width:port.modelData.label_width;spacing:2
                Text { width:parent.width;text:port.modelData.label;color:Theme.text;font.pixelSize:12;font.family:"Segoe UI";elide:Text.ElideRight;horizontalAlignment:port.modelData.alignment==="center"?Text.AlignHCenter:port.modelData.alignment==="right"?Text.AlignRight:Text.AlignLeft }
                Text { width:parent.width;text:root.typeName(port.modelData.data_type)+(port.modelData.unit?" · "+port.modelData.unit:"");color:root.typeColor(port.modelData.data_type);font.pixelSize:10;font.family:"Segoe UI";elide:Text.ElideRight;horizontalAlignment:port.modelData.alignment==="center"?Text.AlignHCenter:port.modelData.alignment==="right"?Text.AlignRight:Text.AlignLeft }
            }
            PortGlyph {
                x:-14;y:-14;input:port.modelData.direction==="in";nx:port.modelData.nx;ny:port.modelData.ny
                ink:port.preview?"#D2EDFF":port.modelData.direction==="in"&&port.connection.color?port.connection.color:root.typeColor(port.modelData.data_type)
                filled:!!port.connection.wire||port.preview
                stub:port.modelData.direction==="out"&&(!!port.connection.wire||port.engaged)
                highlighted:port.preview||root.linking&&port.compatible||portMouse.containsMouse
            }
            MouseArea {
                id:portMouse; x:-14;y:-14;width:28;height:28;hoverEnabled:root.interactive;enabled:root.interactive
                onClicked:root.portPressed(port.modelData)
                ToolTip.visible:containsMouse&&!root.linking;ToolTip.delay:350
                ToolTip.text:port.modelData.label+" · "+root.typeName(port.modelData.data_type)+(port.connection.wire?"\n"+(port.modelData.direction==="in"?"点击取出囊泡并改接":"囊泡已接入目标凹口"):"\n"+(port.modelData.direction==="out"?"点击牵引囊泡":"等待输出囊泡接入"))
            }
        }
    }
}
