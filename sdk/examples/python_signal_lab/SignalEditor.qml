import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id:root
    required property var bridge
    implicitHeight:fields.implicitHeight+70
    function display(key){const raw=bridge.textDraft;return raw[key]===undefined?String(bridge.draft[key]):raw[key]}
    ColumnLayout {
        id:fields;Layout.fillWidth:true;spacing:8
        Repeater {
            model:[{key:"channels",label:"输出路数"},{key:"period_ms",label:"生成间隔 / ms"},{key:"frequency",label:"频率 / Hz"},{key:"minimum",label:"下界"},{key:"maximum",label:"上界"}]
            ColumnLayout {
                id:field;required property var modelData;Layout.fillWidth:true
                Label { text:field.modelData.label;color:"#718393" }
                TextField {
                    objectName:"signal.editor."+field.modelData.key;Layout.fillWidth:true
                    text:root.display(field.modelData.key);enabled:root.bridge.editable
                    onTextEdited:root.bridge.stageText(field.modelData.key,text)
                    onEditingFinished:if(text!==root.display(field.modelData.key))root.bridge.stageText(field.modelData.key,text)
                }
            }
        }
    }
    Label { text:"上下界会一起校验，应用后只产生一步撤销。";color:"#718393";Layout.fillWidth:true;wrapMode:Text.WordWrap }
    Button { objectName:"signal.editor.apply";text:"应用本组参数";enabled:root.bridge.dirty&&!root.bridge.pending;onClicked:root.bridge.commit() }
}
