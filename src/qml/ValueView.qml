import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id:root
    required property var host
    spacing:5
    RowLayout{
        visible:root.host.design.plugin==="source.timer"
        IconButton{symbol:(root.host.snapshot.state||{}).enabled?"pause":"run";text:(root.host.snapshot.state||{}).enabled?"停止计时":"启动计时";enabled:!root.host.designMode&&!!App.runtime.active;onClicked:root.host.action((root.host.snapshot.state||{}).enabled?"stop":"start",{})}
        Label{text:Number((root.host.snapshot.state||{}).remaining_seconds||0).toFixed(2)+" s · "+((root.host.snapshot.state||{}).ticks||0)+" 次";color:Theme.muted}
    }
    Repeater{
        model:Object.keys(root.host.snapshot.outputs||{})
        RowLayout{
            required property string modelData
            Layout.fillWidth:true
            Label{text:modelData;color:Theme.muted;Layout.fillWidth:true;elide:Text.ElideRight}
            Label{text:{const v=(root.host.snapshot.outputs||{})[modelData];return typeof v==="number"?Number(v).toPrecision(7):JSON.stringify(v)}color:Theme.text;font.family:"Consolas";Layout.maximumWidth:200;elide:Text.ElideRight}
        }
    }
    Label{visible:!Object.keys(root.host.snapshot.outputs||{}).length;text:"等待数据";color:Theme.muted}
    Item{Layout.fillHeight:true}
}
