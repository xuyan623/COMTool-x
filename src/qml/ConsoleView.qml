import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id:root;required property var host
    property bool hexDisplay:false
    property bool timestamps:false
    property bool frozen:false
    property string frozenText:""
    property string fileJob:""
    property string fileOperation:"send"
    property var history:host.config.history||[]
    function options(){return{text:outgoing.text,encoding:encoding.editText,hex:sendHex.checked,escape:escapeToggle.checked,newline:newline.currentText}}
    function send(){const args=options();host.action("send",args);let values=history.slice();values=values.filter(v=>v!==outgoing.text);values.unshift(outgoing.text);history=values.slice(0,30)}
    function observe(){App.observeView(host.nodeId,host.viewKey,{hex:hexDisplay,timestamps:timestamps,limit:65536})}
    Component.onCompleted:observe()
    onHexDisplayChanged:observe()
    onTimestampsChanged:observe()
    Connections{target:App;function onJobFinished(id){if(id===root.fileJob)root.fileJob=""}}
    ColumnLayout{
        anchors.fill:parent;spacing:5
        RowLayout{
            Layout.fillWidth:true;spacing:4
            CheckBox{text:"HEX";checked:root.hexDisplay;onClicked:root.hexDisplay=checked}
            CheckBox{text:"时间戳";checked:root.timestamps;onClicked:root.timestamps=checked}
            IconButton{symbol:root.frozen?"run":"pause";text:root.frozen?"恢复显示":"暂停显示";onClicked:{if(!root.frozen)root.frozenText=received.text;root.frozen=!root.frozen}}
            IconButton{symbol:"trash";text:"清空";enabled:!root.host.designMode;onClicked:root.host.action("clear",{})}
            Item{Layout.fillWidth:true}
            IconButton{symbol:"export";text:"保存接收…";onClicked:{root.fileOperation="save";dataFile.fileMode=FileDialog.SaveFile;dataFile.open()}}
        }
        ScrollView{
            Layout.fillWidth:true;Layout.fillHeight:true;clip:true
            TextArea{id:received;objectName:"console."+root.host.nodeId+".receive";readOnly:true;selectByMouse:true;wrapMode:Text.WrapAnywhere;font.family:"Consolas";font.pixelSize:12;text:root.frozen?root.frozenText:root.host.observation.text||root.host.snapshot.receive_tail||"";onTextChanged:cursorPosition=length;background:Rectangle{color:Theme.canvas}}
        }
        RowLayout{
            Layout.fillWidth:true
            ComboBox{Layout.fillWidth:true;model:root.history;displayText:"发送历史";onActivated:outgoing.text=currentValue}
            ComboBox{id:encoding;Layout.preferredWidth:95;editable:true;model:["utf-8","gb18030","ascii","latin1"];editText:root.host.config.encoding||"utf-8"}
            ComboBox{id:newline;Layout.preferredWidth:85;model:["None","LF","CRLF"];currentIndex:Math.max(0,model.indexOf(root.host.config.newline||"None"))}
            CheckBox{id:sendHex;text:"HEX"}
            CheckBox{id:escapeToggle;text:"转义"}
        }
        RowLayout{
            Layout.fillWidth:true
            ScrollView{Layout.fillWidth:true;Layout.preferredHeight:60;TextArea{id:outgoing;objectName:"console."+root.host.nodeId+".send";placeholderText:"输入发送内容";selectByMouse:true;wrapMode:Text.WrapAnywhere;enabled:!root.host.designMode;Keys.onPressed:event=>{if(event.key===Qt.Key_Return&&event.modifiers&Qt.ControlModifier){root.send();event.accepted=true}}}}
            Button{objectName:"console."+root.host.nodeId+".sendButton";text:"发送";enabled:!root.host.designMode&&!!App.runtime.active;onClicked:root.send()}
        }
        Flow{
            Layout.fillWidth:true;spacing:4
            Repeater{model:root.host.config.quick_commands||[];Button{required property var modelData;text:typeof modelData==="string"?modelData:modelData.label||modelData.text;enabled:!root.host.designMode&&!!App.runtime.active;onClicked:{outgoing.text=typeof modelData==="string"?modelData:modelData.text||"";root.send()}}}
        }
        RowLayout{
            Layout.fillWidth:true
            SpinBox{id:period;from:1;to:3600000;value:1000;editable:true;Layout.preferredWidth:120}
            Label{text:"ms";color:Theme.muted}
            Button{text:(root.host.snapshot.state||{}).periodic?"停止定时":"定时发送";enabled:!root.host.designMode&&!!App.runtime.active;onClicked:{if((root.host.snapshot.state||{}).periodic)root.host.action("periodic_stop",{});else{let args=root.options();args.interval_ms=period.value;root.host.action("periodic_start",args)}}}
            Button{text:root.fileJob?"取消发送":"发送文件…";enabled:!root.host.designMode&&!!App.runtime.active;onClicked:{if(root.fileJob)root.host.action("cancel_file",{});else{root.fileOperation="send";dataFile.fileMode=FileDialog.OpenFile;dataFile.open()}}}
            Item{Layout.fillWidth:true}
            Label{text:"收 "+((root.host.snapshot.state||{}).rx_bytes||0)+" B";color:Theme.muted;font.pixelSize:11}
        }
    }
    FileDialog{id:dataFile;title:root.fileOperation==="save"?"保存接收数据":"发送文件";onAccepted:{const path=decodeURIComponent(selectedFile.toString().replace(/^file:\/\/\//,""));const job=root.host.action(root.fileOperation==="save"?"save_receive":"send_file",{path:path});if(root.fileOperation==="send")root.fileJob=job.job||""}}
}

