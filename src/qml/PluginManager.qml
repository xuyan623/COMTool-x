import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// This dialog manages the local folder scan only. Embedded project packages
// remain in the project dependency and export flows, including other versions.
Dialog {
    id:root
    title:"本地插件管理";objectName:"plugins.manager";modal:true;anchors.centerIn:parent
    width:Math.min(1060,parent.width-48);height:Math.min(800,parent.height-48)
    property string selectedPackage:""
    property string enableJob:""
    property string enableError:""
    readonly property var inventory:App.pluginState
    readonly property var picked:(inventory.packages||[]).find(p=>p.id===selectedPackage)||({})
    readonly property var entries:(inventory.plugins||[]).filter(p=>p.package===picked.id)
    readonly property var packageStates:{
        const result={}
        ;(inventory.packages||[]).forEach(p=>{
            const nodes=(inventory.plugins||[]).filter(n=>n.package===p.id),count=nodes.filter(n=>n.enabled).length
            result[p.id]=!count?Qt.Unchecked:count===nodes.length?Qt.Checked:Qt.PartiallyChecked
        })
        return result
    }
    onOpened:{managerQuery.text="";selectedPackage="";enableError="";tree.expandAll(false)}
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
        Label{objectName:"plugins.enable_error";visible:!!root.enableError;Layout.fillWidth:true;text:root.enableError;color:Theme.error;wrapMode:Text.WordWrap}
        SplitView {
            Layout.fillWidth:true;Layout.fillHeight:true
            handle:Rectangle{implicitWidth:6;color:SplitHandle.hovered?Theme.border:Theme.canvas}
            PluginTree {
                id:tree;objectName:"plugins.tree";controlPrefix:"plugins.tree"
                SplitView.preferredWidth:300;SplitView.minimumWidth:220
                treeData:root.inventory.tree||[];query:managerQuery.text;activePlugin:root.selectedPackage
                enableMode:true;enableEditable:!App.readOnly&&!App.runtime.running&&!App.busy&&!root.enableJob;packageStates:root.packageStates
                onPluginChosen:packageId=>root.selectedPackage=packageId
                onEnableRequested:(packages,value)=>{
                    const plugins=(root.inventory.plugins||[]).filter(p=>packages.indexOf(p.package)>=0).map(p=>p.id)
                    const reply=App.invoke("plugin.enable",{plugins:plugins,enabled:value})
                    root.enableError=reply.error||"";root.enableJob=reply.job||""
                }
                Label{anchors.centerIn:parent;width:parent.width-20;visible:!tree.rows.length;text:managerQuery.text?"没有匹配的本地插件":"本地插件文件夹中没有插件。";color:Theme.muted;wrapMode:Text.WordWrap;horizontalAlignment:Text.AlignHCenter}
            }
            ScrollView {
                id:detailsScroll
                SplitView.fillWidth:true;SplitView.minimumWidth:250;clip:true
                ScrollBar.horizontal.policy:ScrollBar.AlwaysOff
                ColumnLayout {
                    width:detailsScroll.availableWidth;spacing:14
                    Label{Layout.fillWidth:true;Layout.margins:12;visible:!root.picked.id;text:"在左侧选择插件，查看功能与界面预览、用途介绍和启用设置。";color:Theme.muted;wrapMode:Text.WordWrap}
                    ColumnLayout {
                        visible:!!root.picked.id;Layout.fillWidth:true;Layout.leftMargin:14;Layout.rightMargin:14;spacing:8
                        Label{Layout.fillWidth:true;text:root.picked.name||"";color:Theme.text;font.pixelSize:20;font.bold:true;wrapMode:Text.WordWrap}
                        Label{Layout.fillWidth:true;text:(root.picked.version||"")+" · "+(root.picked.official?"官方":"用户")+" · "+(root.picked.implementation==="native"?"C++":root.picked.implementation==="qml"?"QML":"Python");color:Theme.muted;wrapMode:Text.WordWrap}
                        Label{Layout.fillWidth:true;visible:!!root.picked.pending_version;text:"待重启应用版本："+root.picked.pending_version;color:Theme.blue;wrapMode:Text.WordWrap}
                        Label{Layout.fillWidth:true;visible:!!root.picked.in_use;text:"当前工程正在使用此插件。";color:Theme.muted;wrapMode:Text.WordWrap}
                        Repeater {
                            model:root.entries
                            // Inspect the installed package independently of
                            // enable state and the project's version choice.
                            delegate:ColumnLayout {
                                id:detail
                                required property var modelData
                                readonly property var preview:{App.registry;return App.invoke("library.preview",{plugin:modelData.id,scope:"library"})}
                                readonly property var spec:preview.definition||({})
                                readonly property var views:Object.keys(spec.views||{})
                                readonly property var ports:(preview.geometry||{}).ports||spec.ports||[]
                                Layout.fillWidth:true;spacing:10
                                Label{visible:root.entries.length>1;text:detail.modelData.name;color:Theme.text;font.bold:true;Layout.fillWidth:true}
                                Label{objectName:"plugins.description."+detail.modelData.id;Layout.fillWidth:true;text:detail.spec.description||"此插件尚未提供用途介绍。";color:Theme.text;wrapMode:Text.WordWrap}
                                Label{visible:!!detail.preview.error;Layout.fillWidth:true;text:detail.preview.error||"";color:Theme.error;wrapMode:Text.WordWrap}
                                Label{visible:(detail.spec.kind||"node")==="node";text:"功能设计预览";color:Theme.muted;font.pixelSize:12}
                                Rectangle {
                                    visible:(detail.spec.kind||"node")==="node"
                                    Layout.fillWidth:true;Layout.preferredHeight:150;color:Theme.canvas;border.color:Theme.border;radius:4
                                    NodePreview{objectName:"plugins.node_preview."+detail.modelData.id;anchors.fill:parent;plugin:detail.modelData.id;geometry:detail.preview.geometry||({width:180,height:104,ports:[],center:[0,0,180,104],error:detail.preview.error||"无法预览此节点"})}
                                }
                                RowLayout {
                                    Layout.fillWidth:true
                                    Label{text:"界面预览";color:Theme.muted;font.pixelSize:12;Layout.fillWidth:true}
                                    ComboBox {
                                        id:previewView;objectName:"plugins.preview_view."+detail.modelData.id
                                        visible:detail.views.length>1;model:detail.views;Layout.maximumWidth:180
                                    }
                                }
                                UiPreview {
                                    objectName:"plugins.ui_preview."+detail.modelData.id
                                    Layout.fillWidth:true;Layout.preferredHeight:detail.views.length||detail.spec.kind==="container"||detail.spec.kind==="workspace"?180:60
                                    plugin:detail.modelData.id;spec:detail.spec;config:detail.preview.config||({})
                                    presentationUrl:detail.preview.presentation_url||""
                                    viewKey:detail.views[previewView.currentIndex]||"";viewUrl:(detail.preview.view_urls||{})[viewKey]||""
                                }
                                Label{visible:detail.views.length>0;text:"按默认参数展示，预览操作不会运行插件。";color:Theme.muted;font.pixelSize:11;Layout.fillWidth:true;wrapMode:Text.WordWrap}
                                Label{visible:(detail.spec.kind||"node")==="node";text:"输入 / 输出";color:Theme.muted;font.pixelSize:12}
                                Label{visible:(detail.spec.kind||"node")==="node"&&!detail.ports.length;text:"此节点没有输入或输出端口。";color:Theme.muted;Layout.fillWidth:true;wrapMode:Text.WordWrap}
                                Repeater {
                                    model:detail.ports
                                    delegate:RowLayout {
                                        required property var modelData
                                        Layout.fillWidth:true
                                        Label{text:modelData.direction==="in"?"输入":"输出";color:Theme.muted;Layout.preferredWidth:42}
                                        Label{text:modelData.label||modelData.id;Layout.fillWidth:true;color:Theme.text;wrapMode:Text.WrapAnywhere}
                                        Label{text:modelData.data_type+(modelData.unit?" · "+modelData.unit:"");color:Theme.muted;wrapMode:Text.WordWrap}
                                    }
                                }
                                Label{visible:Object.keys(detail.spec.properties||{}).length>0;text:"默认参数";color:Theme.muted;font.pixelSize:12}
                                Repeater {
                                    model:Object.keys(detail.spec.properties||{})
                                    delegate:ColumnLayout {
                                        required property string modelData
                                        readonly property var field:detail.spec.properties[modelData]||({})
                                        Layout.fillWidth:true;spacing:3
                                        Label{text:parent.field.label||parent.modelData;color:Theme.muted;Layout.fillWidth:true;wrapMode:Text.WordWrap}
                                        Label{text:{const value=(detail.preview.config||{})[parent.modelData];return value===undefined?"未设置":typeof value==="object"?JSON.stringify(value):String(value)}color:Theme.text;Layout.fillWidth:true;wrapMode:Text.WrapAnywhere}
                                    }
                                }
                            }
                        }
                        Rectangle{Layout.fillWidth:true;implicitHeight:1;color:Theme.border;Layout.topMargin:8}
                        Label{Layout.fillWidth:true;text:root.picked.id||"";color:Theme.muted;font.pixelSize:11;wrapMode:Text.WrapAnywhere}
                        Label{Layout.fillWidth:true;text:root.picked.relative_path||"";color:Theme.muted;font.pixelSize:11;wrapMode:Text.WrapAnywhere}
                        Item{Layout.preferredHeight:8}
                    }
                }
            }
        }
    }
    footer:DialogButtonBox{ActionButton{objectName:"plugins.close";text:"关闭";onClicked:root.close()}}
    Connections{target:App;function onJobFinished(id){if(id===root.enableJob){root.enableError=App.job(id).error||"";root.enableJob=""}}}
}
