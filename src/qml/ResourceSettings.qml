import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id:root; objectName:"settings.resources"; title:"资源额度"; modal:true; anchors.centerIn:parent
    width:560; height:Math.min(690,parent.height-48)
    property var limits:({})
    property string error:""
    readonly property var labels:({archive_mib:"插件压缩包 / MiB",expanded_mib:"插件展开总量 / MiB",package_files:"每包文件数",ports:"每节点端口数",python_message_mib:"Python 单条消息 / MiB",python_timeout_ms:"普通 Python 调用 / ms",callback_timeout_ms:"声明校验与取消等待 / ms",event_queue_mib:"运行事件队列 / MiB",event_queue_count:"运行事件条数",resource_mib:"单个工程资源 / MiB"})
    onOpened:{limits=App.invoke("resources.get",{});error=""}
    ColumnLayout {
        anchors.fill:parent; spacing:12
        Label { text:"插件声明所需额度；可以按实际任务调整。停止运行会话后生效。"; color:Theme.muted; Layout.fillWidth:true; wrapMode:Text.WordWrap }
        ScrollView {
            Layout.fillWidth:true; Layout.fillHeight:true; clip:true
            ColumnLayout {
                width:parent.width-16; spacing:10
                Repeater {
                    id:fields;model:Object.keys(root.limits)
                    RowLayout {
                        id:field;required property string modelData
                        property alias text:input.text
                        Layout.fillWidth:true
                        Label { text:root.labels[field.modelData]||field.modelData; color:Theme.text; Layout.fillWidth:true }
                        TextField { id:input;objectName:"resources."+field.modelData; text:String(root.limits[field.modelData]); Layout.preferredWidth:145; selectByMouse:true }
                    }
                }
            }
        }
        Label { visible:!!root.error; text:root.error; color:Theme.error; Layout.fillWidth:true; wrapMode:Text.WordWrap }
    }
    footer:DialogButtonBox {
        ActionButton {
            objectName:"resources.apply";text:"应用";enabled:!App.runtime.active&&!App.busy
            onClicked: {
                let values={}
                for(let i=0;i<fields.count;i++){const field=fields.itemAt(i);values[field.modelData]=Number(field.text)}
                const result=App.invoke("resources.set",{limits:values});root.error=result.error||""
                if(!root.error)root.close()
            }
        }
        ActionButton { text:"恢复默认";enabled:!App.runtime.active&&!App.busy;onClicked:{const result=App.invoke("resources.set",{reset:true});root.error=result.error||"";if(!root.error)root.limits=result} }
        ActionButton { text:"关闭";onClicked:root.close() }
    }
}
