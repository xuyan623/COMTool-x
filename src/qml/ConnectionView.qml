import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id:root;required property var host
    property var connectionState:host.snapshot.state||({})
    property var ports:[]
    property string portsJob:""
    function configure(key,value){let changes={};changes[key]=value;App.invoke("node.configure",{node:host.nodeId,changes:changes,persist:!App.readOnly})}
    Component.onCompleted:if(host.design.plugin==="io.serial")portsJob=App.invoke("io.ports",{}).job||""
    Connections{target:App;function onJobFinished(id){if(id===root.portsJob&&App.job(id).state==="complete")root.ports=(App.job(id).result.ports||[]).map(p=>p.port)}}
    ColumnLayout{
        anchors.fill:parent;spacing:8
        RowLayout{
            Rectangle{width:8;height:8;radius:4;color:root.connectionState.connected?Theme.green:Theme.muted}
            Label{Layout.fillWidth:true;text:root.connectionState.connected?"已连接":"未连接";color:Theme.text}
            Label{text:"收 "+(root.connectionState.rx_bytes||0)+"　发 "+(root.connectionState.tx_bytes||0);color:Theme.muted;font.pixelSize:11}
            IconButton{symbol:"sliders";text:"配置";onClicked:{App.workspaces.open("graph");App.invoke("graph.select",{id:root.host.nodeId})}}
        }
        RowLayout{
            Layout.fillWidth:true
            ComboBox{visible:root.host.design.plugin==="io.serial";objectName:"connection."+root.host.nodeId+".port";Layout.fillWidth:true;editable:true;model:root.ports;editText:root.host.config.port||"";enabled:!root.connectionState.connected&&!root.host.designMode;onAccepted:root.configure("port",editText);onActivated:root.configure("port",currentText)}
            TextField{visible:root.host.design.plugin!=="io.serial";Layout.fillWidth:true;text:root.host.config.host||"";placeholderText:"主机";enabled:!root.connectionState.connected&&!root.host.designMode;onEditingFinished:if(text!==root.host.config.host)root.configure("host",text)}
            TextField{Layout.preferredWidth:100;text:String(root.host.design.plugin==="io.serial"?root.host.config.baudrate:root.host.config.port);placeholderText:root.host.design.plugin==="io.serial"?"波特率":"端口";enabled:!root.connectionState.connected&&!root.host.designMode;onEditingFinished:root.configure(root.host.design.plugin==="io.serial"?"baudrate":"port",Number(text))}
            ActionButton{objectName:"connection."+root.host.nodeId+".connect";text:root.connectionState.connected?"断开":"连接";enabled:!root.host.designMode&&!App.busy;onClicked:{if(!App.runtime.active)App.invoke("runtime.start",{});root.host.action(root.connectionState.connected?"disconnect":"connect",{})}}
        }
        RowLayout{
            visible:root.host.design.plugin==="io.network"
            ComboBox{model:["tcp","udp"];currentIndex:model.indexOf(root.host.config.protocol);enabled:!root.connectionState.connected&&!root.host.designMode;onActivated:root.configure("protocol",currentText)}
            ComboBox{model:["client","server"];currentIndex:model.indexOf(root.host.config.mode);displayText:currentIndex?"服务端":"客户端";enabled:!root.connectionState.connected&&!root.host.designMode;onActivated:root.configure("mode",currentValue)}
            Label{Layout.fillWidth:true;text:root.connectionState.clients&&root.connectionState.clients.length?root.connectionState.clients.length+" 个客户端":"";color:Theme.muted}
        }
        RowLayout{
            visible:root.host.design.plugin==="io.ssh"
            TextField{Layout.fillWidth:true;text:root.host.config.user||"";placeholderText:"用户";enabled:!root.connectionState.connected&&!root.host.designMode;onEditingFinished:root.configure("user",text)}
            TextField{Layout.fillWidth:true;text:root.host.config.password||"";placeholderText:"密码";echoMode:TextInput.Password;enabled:!root.connectionState.connected&&!root.host.designMode;onEditingFinished:root.configure("password",text)}
        }
        Label{Layout.fillWidth:true;visible:!!root.connectionState.last_error;text:root.connectionState.last_error||"";color:Theme.error;wrapMode:Text.WrapAnywhere}
        Label{Layout.fillWidth:true;visible:!!root.connectionState.host_fingerprint;text:root.connectionState.host_fingerprint||"";color:Theme.muted;font.pixelSize:10;elide:Text.ElideMiddle}
        Item{Layout.fillHeight:true}
    }
}

