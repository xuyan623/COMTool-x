import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id:root;required property var host
    property string kind:host.design.plugin||""
    property var value:(host.snapshot.state||{}).value===undefined?host.config.value:(host.snapshot.state||{}).value
    function setValue(value){host.action("set_value",{value:value})}
    ColumnLayout{
        anchors.fill:parent;spacing:6
        Label{text:root.host.viewProperties.label||root.host.config.label||root.host.design.title||"";color:root.host.viewProperties.color||Theme.muted;font.pixelSize:root.host.viewProperties.font_size||13;font.family:root.host.viewProperties.font_family||"Microsoft YaHei UI";Layout.fillWidth:true;elide:Text.ElideRight}
        Loader{
            Layout.fillWidth:true;Layout.fillHeight:true
            sourceComponent:root.kind==="control.button"?button:root.kind==="control.checkbox"?checkbox:root.kind==="control.slider"?slider:root.kind==="control.select"?select:root.kind==="display.value"?display:input
        }
    }
    Component{id:button;Button{font.family:root.host.viewProperties.font_family||"Microsoft YaHei UI";font.pixelSize:root.host.viewProperties.font_size||13;objectName:"control."+root.host.nodeId;text:root.host.viewProperties.label||root.host.config.label||"执行";enabled:!root.host.designMode&&!!App.runtime.active;onClicked:root.host.action("click",{})}}
    Component{id:checkbox;CheckBox{objectName:"control."+root.host.nodeId;checked:!!root.value;text:checked?"开启":"关闭";enabled:!root.host.designMode&&!!App.runtime.active;onClicked:root.setValue(checked)}}
    Component{id:slider;RowLayout{Slider{objectName:"control."+root.host.nodeId;Layout.fillWidth:true;from:root.host.config.minimum||0;to:root.host.config.maximum||100;stepSize:root.host.config.step||.1;value:Number(root.value||0);enabled:!root.host.designMode&&!!App.runtime.active;onMoved:root.setValue(value)}Label{text:Number(root.value||0).toFixed(root.host.config.decimals||0)+" "+(root.host.config.unit||"");color:Theme.text}}}
    Component{id:select;ComboBox{objectName:"control."+root.host.nodeId;model:root.host.config.choices||[];currentIndex:Math.max(0,model.indexOf(root.value));enabled:!root.host.designMode&&!!App.runtime.active;onActivated:root.setValue(currentValue)}}
    Component{id:display;Label{text:(root.host.config.format||"{value}").replace("{value}",typeof root.value==="object"?JSON.stringify(root.value):String(root.value===undefined?"—":root.value));color:root.host.viewProperties.color||Theme.text;font.pixelSize:Math.max(18,root.host.viewProperties.font_size||22);verticalAlignment:Text.AlignVCenter;wrapMode:Text.WrapAnywhere}}
    Component{id:input;TextField{font.family:root.host.viewProperties.font_family||"Microsoft YaHei UI";font.pixelSize:root.host.viewProperties.font_size||13;objectName:"control."+root.host.nodeId;text:root.value===undefined?"":String(root.value);placeholderText:root.host.config.placeholder||"输入后按 Enter 应用";enabled:!root.host.designMode&&!!App.runtime.active;selectByMouse:true;onAccepted:root.setValue(root.kind==="control.number"?Number(text):text)}}
}

