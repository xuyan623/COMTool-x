import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id:root
    required property var bridge
    implicitHeight:170
    Label { text:"此视图的显示名称";color:"#718393" }
    TextField {
        objectName:"signal.view.label";Layout.fillWidth:true
        text:root.bridge.textDraft.display_label===undefined?root.bridge.draft.display_label:root.bridge.textDraft.display_label
        enabled:root.bridge.editable;onTextEdited:root.bridge.stageText("display_label",text)
    }
    Label { text:"强调色";color:"#718393" }
    TextField {
        objectName:"signal.view.accent";Layout.fillWidth:true
        text:root.bridge.textDraft.accent===undefined?root.bridge.draft.accent:root.bridge.textDraft.accent
        enabled:root.bridge.editable;onTextEdited:root.bridge.stageText("accent",text)
    }
    Button { objectName:"signal.view.apply";text:"应用此外观";enabled:root.bridge.dirty&&!root.bridge.pending;onClicked:root.bridge.commit() }
}
