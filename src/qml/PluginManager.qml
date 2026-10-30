import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// This dialog manages the local folder scan only. Embedded project packages
// remain in the project dependency and export flows, including other versions.
Dialog {
    id:root
    title:"本地插件管理";objectName:"plugins.manager";modal:true;anchors.centerIn:parent
    width:Math.min(960,parent.width-48);height:Math.min(720,parent.height-48)
    property string selectedPackage:""
    readonly property var inventory:App.pluginState
    readonly property var picked:(inventory.packages||[]).find(p=>p.id===selectedPackage)||({})
    onOpened:{managerQuery.text="";selectedPackage="";tree.expandAll(false)}
    ColumnLayout {
        anchors.fill:parent;spacing:10
        RowLayout {
            Layout.fillWidth:true
            Label{Layout.fillWidth:true;text:"本地插件文件夹 · "+(root.inventory.packages||[]).length+" 个插件包";color:Theme.text}
            ActionButton{objectName:"plugins.directory";text:"打开插件文件夹";onClicked:App.invoke("plugin.directory",{})}
        }
        Label{Layout.fillWidth:true;text:root.inventory.directory||"";color:Theme.muted;elide:Text.ElideMiddle}
        Label{Layout.fillWidth:true;text:"目录或文件在外部调整后，重启生效。工程携带的插件可在“文件 → 导出工程插件”中查看。";color:Theme.muted;wrapMode:Text.WordWrap}
        Rectangle {
            visible:root.inventory.restart_required;Layout.fillWidth:true;implicitHeight:restartRow.implicitHeight+20;color:Theme.raised;radius:6
            RowLayout{id:restartRow;anchors.fill:parent;anchors.margins:10;Label{Layout.fillWidth:true;text:"插件更新已保存，重启后生效。请先保存当前工程。";color:Theme.text;wrapMode:Text.WordWrap}ActionButton{text:"重启 CoRig";onClicked:App.invoke("app.restart",{})}}
        }
        RowLayout {
            Layout.fillWidth:true
            TextField{id:managerQuery;objectName:"plugins.search";Layout.fillWidth:true;placeholderText:"搜索名称、插件 ID 或文件夹路径";selectByMouse:true;onTextChanged:root.selectedPackage=""}
            ActionButton{objectName:"plugins.expand_all";text:"全部展开";enabled:!managerQuery.text;onClicked:tree.expandAll(true)}
            ActionButton{objectName:"plugins.collapse_all";text:"全部折叠";enabled:!managerQuery.text;onClicked:tree.expandAll(false)}
        }
        SplitView {
            Layout.fillWidth:true;Layout.fillHeight:true
            handle:Rectangle{implicitWidth:6;color:SplitHandle.hovered?Theme.border:Theme.canvas}
            PluginTree {
                id:tree;objectName:"plugins.tree";controlPrefix:"plugins.tree"
                SplitView.preferredWidth:360;SplitView.minimumWidth:220
                treeData:root.inventory.tree||[];query:managerQuery.text;activePlugin:root.selectedPackage
                onPluginChosen:packageId=>root.selectedPackage=packageId
                Label{anchors.centerIn:parent;width:parent.width-20;visible:!tree.rows.length;text:managerQuery.text?"没有匹配的本地插件":"本地插件文件夹中没有插件。";color:Theme.muted;wrapMode:Text.WordWrap;horizontalAlignment:Text.AlignHCenter}
            }
            ScrollView {
                id:detailsScroll
                SplitView.fillWidth:true;SplitView.minimumWidth:250;clip:true
                ColumnLayout {
                    width:detailsScroll.availableWidth;spacing:12
                    Label{Layout.fillWidth:true;visible:!root.picked.id;text:"在左侧展开文件夹并选择插件，查看本地状态与启用设置。";color:Theme.muted;wrapMode:Text.WordWrap}
                    ColumnLayout {
                        visible:!!root.picked.id;Layout.fillWidth:true;spacing:8
                        Label{Layout.fillWidth:true;text:root.picked.name||"";color:Theme.text;font.bold:true;wrapMode:Text.WordWrap}
                        Label{Layout.fillWidth:true;text:root.picked.id||"";color:Theme.muted;wrapMode:Text.WrapAnywhere}
                        Label{Layout.fillWidth:true;text:(root.picked.version||"")+" · "+(root.picked.official?"官方":"用户")+" · "+(root.picked.implementation==="native"?"C++":"Python");color:Theme.muted;wrapMode:Text.WordWrap}
                        Label{Layout.fillWidth:true;text:root.picked.relative_path||"";color:Theme.muted;wrapMode:Text.WrapAnywhere}
                        Label{Layout.fillWidth:true;visible:!!root.picked.pending_version;text:"待重启应用版本："+root.picked.pending_version;color:Theme.blue;wrapMode:Text.WordWrap}
                        Label{Layout.fillWidth:true;visible:!!root.picked.in_use;text:"当前工程正在使用此插件的节点。";color:Theme.muted;wrapMode:Text.WordWrap}
                        Repeater {
                            model:(root.inventory.plugins||[]).filter(p=>p.package===root.picked.id)
                            delegate:CheckBox {
                                required property var modelData
                                objectName:"plugins.enabled."+modelData.id
                                text:modelData.name;checked:modelData.enabled
                                enabled:!App.readOnly&&!App.runtime.running&&!App.busy
                                onClicked:{const result=App.invoke("plugin.enable",{plugin:modelData.id,enabled:checked});if(result.error)checked=modelData.enabled}
                            }
                        }
                    }
                }
            }
        }
    }
    footer:DialogButtonBox{ActionButton{objectName:"plugins.close";text:"关闭";onClicked:root.close()}}
}
