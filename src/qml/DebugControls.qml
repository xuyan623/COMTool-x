import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id:root
    property string prefix:"execution"
    property var sources:[{id:"",name:"自动数据源"}].concat(App.runtime.sources||[])
    spacing:3
    function step(kind){let args={mode:kind};if(source.currentValue)args.source=source.currentValue;App.invoke("runtime.step",args)}
    IconButton{objectName:root.prefix+".run";symbol:"run";ink:Theme.green;text:"运行／继续（F5 调试运行，Ctrl+F5 普通运行）";enabled:!App.runtime.running&&!App.executionPending&&!App.busy;onClicked:App.invoke("runtime.start",{debug:!!App.runtime.debug})}
    IconButton{objectName:root.prefix+".pause";symbol:"pause";text:"暂停（F6）";enabled:!!App.runtime.running;onClicked:App.invoke("runtime.pause",{})}
    IconButton{objectName:root.prefix+".debug";symbol:"debug";text:"调试模式";checkable:true;checked:!!App.runtime.debug;enabled:!App.executionPending;onClicked:App.invoke("runtime.debug",{enabled:checked})}
    IconButton{objectName:root.prefix+".batch";symbol:"batch";text:"整图下一步：完成一个采样批次（F10）";enabled:!!App.runtime.debug&&!App.runtime.running&&!App.executionPending&&!App.busy;onClicked:root.step("batch")}
    IconButton{objectName:root.prefix+".node";symbol:"node";text:"节点单步（F11）";enabled:!!App.runtime.debug&&!App.runtime.running&&!App.executionPending&&!App.busy;onClicked:root.step("node")}
    IconButton{objectName:root.prefix+".reset";symbol:"reset";text:"重置：恢复初始状态（Ctrl+Shift+F5）";enabled:!App.executionPending&&!App.busy;onClicked:App.invoke("runtime.reset",{})}
    IconButton{objectName:root.prefix+".end";symbol:"end";text:"结束会话：释放连接（Shift+F5）";enabled:!!App.runtime.active;onClicked:App.invoke("runtime.stop",{})}
    ComboBox{id:source;objectName:root.prefix+".source";visible:!!App.runtime.debug;Layout.preferredWidth:140;model:root.sources;textRole:"name";valueRole:"id";enabled:!App.runtime.running&&!App.executionPending&&!(App.runtime.cursor||{}).next_node}
    Label{
        objectName:root.prefix+".status";Layout.maximumWidth:180;elide:Text.ElideRight
        text:(App.runtime.debug?"调试　":"普通运行　")+(App.executionPending?"执行操作中":App.runtime.running?"运行中":App.runtime.active?"已暂停":"就绪")
        color:Theme.muted;font.pixelSize:12
        ToolTip.visible:statusHover.hovered
        ToolTip.text:(App.runtime.cursor||{}).next_node?"批次 "+App.runtime.cursor.batch+"，下一节点 "+App.runtime.cursor.next_node:"当前没有未完成批次"
        HoverHandler{id:statusHover}
    }
}
