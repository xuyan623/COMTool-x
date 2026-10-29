import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Rectangle {
    id: root
    property string page:"graph"
    property bool matching:page==="layout"?App.selectedKind==="ui":App.selectedKind!=="ui"
    property var inspected: { App.selected; App.selectedKind; App.project; return matching?App.inspector():({}) }
    property string fileKey: ""
    color: Theme.panel
    implicitWidth: 306
    function title(key) {
        if(key==="grid_size")return "网格粒度 / px"
        if(page==="layout"&&key==="width")return "宽度"
        if(page==="layout"&&key==="height")return "高度"
        const names = {value:"数值", label:"标签", minimum:"最小值", maximum:"最大值", step:"步长", decimals:"小数位", unit:"单位", channels:"通道", inputs:"输入端口", outputs:"输出端口", code:"Python 代码", timeout_ms:"执行时限 / ms", period_ms:"周期 / ms", port:"端口", baudrate:"波特率", bytesize:"数据位", parity:"校验", stopbits:"停止位", flow_control:"流控", host:"主机", user:"用户", password:"密码", protocol:"协议", mode:"模式", format:"格式", encoding:"编码", newline:"换行", title:"标题", stretch:"伸缩权重", gap:"间距", padding:"内边距", color:"颜色", background:"背景", width:"线宽", routing:"路径", font_size:"字号", font_family:"字体", min_width:"最小宽度", min_height:"最小高度", max_width:"最大宽度", max_height:"最大高度", row:"行", column:"列", rowspan:"跨行", colspan:"跨列", columns:"列数", window_seconds:"显示时长 / s", max_points:"保留点数", visibility:"通道显隐", colors:"通道颜色", line_width:"曲线宽度", path:"文件路径", auto_reconnect:"自动重连", reconnect_interval:"重连间隔 / s", template:"文本模板", choices:"选项", current_index:"当前页", text:"文本", initial:"初值", cutoff_hz:"截止频率 / Hz", time_unit:"时间单位"}
        return names[key] || key
    }
    function change(key, value) {
        let changes = {}; changes[key] = value
        if (App.selectedKind === "ui") App.invoke("ui.item.patch", {id:App.selected, properties:changes})
        else if (App.selectedKind === "wire") App.invoke("graph.wire.patch", {id:App.selected, changes:changes})
        else App.invoke("node.configure", {node:App.selected, changes:changes, persist:!App.readOnly})
    }
    ScrollView {
        anchors.fill: parent; anchors.margins: 12; clip: true
        ColumnLayout {
            width: root.width - 30; spacing: 10
            Label { text: root.page==="layout" ? "界面属性" : App.selectedKind === "wire" ? "导线属性" : "节点属性"; color: Theme.text; font.pixelSize: 15; font.bold: true }
            Label { visible: !App.selected.length||!root.matching; text: root.page==="layout"?"选择界面对象，编辑外观与布局。":"选择节点或导线，编辑功能属性。"; color: Theme.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Label { visible: root.matching&&App.selected.length > 0; text: App.selected; color: Theme.muted; font.pixelSize: 11; elide: Text.ElideMiddle; Layout.fillWidth: true }
            TextField {
                Layout.fillWidth: true; visible: root.matching&&App.selectedKind === "node" && App.selected.length > 0
                text: root.inspected.item ? root.inspected.item.title || "" : ""; placeholderText: "节点名称"; enabled: !App.readOnly && !App.busy
                onEditingFinished: if (root.inspected.item && text !== root.inspected.item.title) App.invoke("graph.node.patch", {id:App.selected, changes:{title:text}})
            }
            Repeater {
                model: Object.keys(root.inspected.schema || {})
                ColumnLayout {
                    id: field
                    required property string modelData
                    property var spec: root.inspected.schema[modelData] || {}
                    property var value: (root.inspected.values || {})[modelData]
                    property bool editable: !App.busy && (App.selectedKind === "node" ? (!App.readOnly && !App.runtime.running) || !!spec.runtime_editable : !App.readOnly)
                    Layout.fillWidth: true; spacing: 4
                    visible: {
                        const rule = spec.visible_if || {}, values = root.inspected.values || {}
                        return Object.keys(rule).every(key => rule[key].indexOf(values[key]) >= 0)
                    }
                    Label { text: field.spec.label || root.title(field.modelData); color: Theme.muted; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Loader {
                        Layout.fillWidth: true
                        sourceComponent: field.spec.choices ? choiceEditor : field.spec.type === "boolean" ? boolEditor : field.modelData === "code" || field.spec.type === "json" || typeof field.value === "object" ? jsonEditor : textEditor
                    }
                    Component {
                        id: boolEditor
                        CheckBox { checked: !!field.value; text: checked ? "开启" : "关闭"; enabled: field.editable; onClicked: root.change(field.modelData, checked) }
                    }
                    Component {
                        id: choiceEditor
                        ComboBox {
                            model: field.spec.choices; enabled: field.editable
                            currentIndex: Math.max(0, field.spec.choices.indexOf(field.value))
                            displayText: (field.spec.choice_labels || {})[currentValue] || currentValue || ""
                            onActivated: root.change(field.modelData, currentValue)
                        }
                    }
                    Component {
                        id: textEditor
                        RowLayout {
                            TextField {
                                objectName: "property." + field.modelData; Layout.fillWidth: true; text: field.value === undefined ? "" : String(field.value)
                                enabled: field.editable; selectByMouse: true; echoMode: field.spec.secret ? TextInput.Password : TextInput.Normal
                                onEditingFinished: {
                                    let next = field.spec.type === "number" || field.spec.type === "integer" ? Number(text) : text
                                    if (next !== field.value) root.change(field.modelData, next)
                                }
                            }
                            Button { visible: field.spec.format === "file"; text: "…"; implicitWidth: 30; enabled: field.editable; onClicked: { root.fileKey = field.modelData; resourceFile.open() } }
                        }
                    }
                    Component {
                        id: jsonEditor
                        ColumnLayout {
                            TextArea { id: jsonText; objectName: "property." + field.modelData; Layout.fillWidth: true; Layout.preferredHeight: field.modelData === "code" ? 220 : 94; enabled: field.editable; text: typeof field.value === "string" ? field.value : JSON.stringify(field.value, null, 2); wrapMode: Text.WrapAnywhere; selectByMouse: true; font.family: "Consolas"; font.pixelSize: 12 }
                            Button { text: "应用"; enabled: field.editable; onClicked: { try { root.change(field.modelData, field.spec.type === "json" ? JSON.parse(jsonText.text) : jsonText.text) } catch (e) { jsonText.focus = true } } }
                        }
                    }
                }
            }
            Rectangle { visible: App.selected.length > 0; Layout.fillWidth: true; height: 1; color: Theme.border }
            Label { visible: root.matching&&App.selectedKind === "node" && App.selected.length > 0; text: "运行状态"; color: Theme.muted }
            TextArea { visible: root.matching&&App.selectedKind === "node" && App.selected.length > 0; Layout.fillWidth: true; readOnly: true; selectByMouse: true; text: { App.runtime; return JSON.stringify(App.nodeState(App.selected), null, 2) } font.family: "Consolas"; font.pixelSize: 11; wrapMode: Text.WrapAnywhere; background: null }
        }
    }
    FileDialog { id: resourceFile; title: "选择工程资源"; onAccepted: root.change(root.fileKey, decodeURIComponent(selectedFile.toString().replace(/^file:\/\/\//, ""))) }
}
