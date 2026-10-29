import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id:root
    required property var host
    spacing:5
    RowLayout{
        visible:root.host.design.plugin==="source.timer"
        Button{text:(root.host.snapshot.state||{}).enabled?"停止定时器":"启动定时器";enabled:!root.host.designMode&&!!App.runtime.active;onClicked:root.host.action((root.host.snapshot.state||{}).enabled?"stop":"start",{})}
        Label{text:root.host.config.interval_ms+" ms";color:Theme.muted}
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
