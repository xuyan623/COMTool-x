import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id:root;anchors.fill:parent;objectName:"page.library"
    property string category:""
    property string scope:"all"
    property var picked:({})
    property var containers:[{id:"layout.row",name:"横向容器",description:"在局部区域横向排列子对象",category_path:["布局容器"],views:[]},{id:"layout.column",name:"纵向容器",description:"在局部区域纵向排列子对象",category_path:["布局容器"],views:[]},{id:"layout.grid",name:"网格容器",description:"在局部区域按网格排列子对象",category_path:["布局容器"],views:[]},{id:"layout.tabs",name:"分页容器",description:"在局部区域切换分页",category_path:["布局容器"],views:[]}]
    property var entries:{App.registry;App.libraryPreferences;let result=App.catalogEntries(query.text,category&&category!=="布局容器"?category.split("/"):[],scope);if(category==="布局容器")result=[];if(App.libraryTarget==="layout"&&scope==="all"&&(!category||category==="布局容器"))result=result.concat(containers.filter(p=>(p.name+" "+p.id).toLowerCase().indexOf(query.text.toLowerCase())>=0));return result}
    property var previewViews:picked.views?(Array.isArray(picked.views)?picked.views:Object.keys(picked.views)):[]
    property var previewConfig:{const result={},schema=picked.properties||{};Object.keys(schema).forEach(key=>result[key]=schema[key].default);return result}
    function reason(spec){return App.libraryTarget==="graph"?(spec.id&&spec.id.startsWith("layout.")?"容器只用于界面设计":""):(spec.id&&spec.id.startsWith("layout.")?"":spec.views&&Object.keys(spec.views).length?"":"该插件没有界面视图，请在功能设计添加")}
    function place(){if(!picked.id||reason(picked))return;const result=App.invoke("editor.add",{page:App.libraryTarget,plugin:picked.id});if(!result.error)App.workspaces.open(App.libraryTarget)}
    Rectangle{anchors.fill:parent;color:Theme.panel;z:-1}
    ColumnLayout {
        anchors.fill:parent;spacing:0
        RowLayout {
            Layout.fillWidth:true;Layout.preferredHeight:48;spacing:8
            IconButton{Layout.leftMargin:10;symbol:"graph";text:"功能设计插件库";checkable:true;checked:App.libraryTarget==="graph";onClicked:{root.category="";App.openLibrary("graph")}}
            IconButton{symbol:"layout";text:"界面设计插件库";checkable:true;checked:App.libraryTarget==="layout";onClicked:{root.category="";App.openLibrary("layout")}}
            TextField{id:query;objectName:"library.search";Layout.fillWidth:true;placeholderText:"搜索插件名称、用途或别名";selectByMouse:true}
            IconButton{symbol:App.libraryPreferences.view==="grid"?"logs":"library";text:"切换列表／预览";onClicked:App.invoke("library.configure",{view:App.libraryPreferences.view==="grid"?"list":"grid"})}
            IconButton{symbol:"plus";text:"导入用户插件";Layout.rightMargin:10;enabled:!App.readOnly&&!App.runtime.running;onClicked:pluginFile.open()}
        }
        Rectangle{Layout.fillWidth:true;height:1;color:Theme.border}
        SplitView {
            Layout.fillWidth:true;Layout.fillHeight:true
            handle:Rectangle{implicitWidth:6;color:SplitHandle.hovered?Theme.border:Theme.canvas}
            ScrollView {
                SplitView.preferredWidth:154;SplitView.minimumWidth:80;clip:true
                ColumnLayout {
                    width:150;spacing:2
                    ItemDelegate{Layout.fillWidth:true;text:"全部插件";highlighted:root.scope==="all"&&!root.category;onClicked:{root.category="";root.scope="all"}}
                    ItemDelegate{Layout.fillWidth:true;text:"收藏";highlighted:root.scope==="favorites";onClicked:{root.category="";root.scope="favorites"}}
                    ItemDelegate{Layout.fillWidth:true;text:"最近使用";highlighted:root.scope==="recent";onClicked:{root.category="";root.scope="recent"}}
                    Repeater {
                        model:{const paths=[];App.registry.forEach(p=>{const path=p.category_path&&p.category_path.length?p.category_path:[p.category||"其他"];for(let i=1;i<=path.length;i++){const key=path.slice(0,i).join("/");if(paths.indexOf(key)<0)paths.push(key)}});if(App.libraryTarget==="layout")paths.push("布局容器");return paths.sort()}
                        ItemDelegate{required property string modelData;Layout.fillWidth:true;leftPadding:12+(modelData.split("/").length-1)*12;text:modelData.split("/").pop();highlighted:root.category===modelData;onClicked:{root.category=modelData;root.scope="all"}}
                    }
                }
            }
            Item {
                SplitView.fillWidth:true;SplitView.minimumWidth:230
                GridView {
                    id:cards;anchors.fill:parent;anchors.margins:10;clip:true
                    property bool listMode:App.libraryPreferences.view==="list"
                    cellWidth:listMode?width:Math.floor(width/Math.max(1,Math.floor(width/(420*App.libraryPreferences.size))))
                    cellHeight:listMode?80:230*App.libraryPreferences.size;model:root.entries
                    delegate:Rectangle {
                        id:card;required property var modelData;property string unavailable:root.reason(modelData)
                        width:cards.cellWidth-8;height:cards.cellHeight-8;radius:5;color:root.picked.id===modelData.id?Theme.raised:Theme.panel
                        border.width:unavailable?1.6:1;border.color:unavailable?"#D8B56C":root.picked.id===modelData.id?Theme.blue:Theme.border
                        ColumnLayout {
                            anchors.fill:parent;anchors.margins:12;spacing:5
                            Label{text:card.modelData.name;color:Theme.text;font.bold:true;Layout.fillWidth:true;elide:Text.ElideRight}
                            // Keep both previews side by side at every card
                            // width; the divider stays inside the preview area.
                            RowLayout {
                                visible:!cards.listMode;Layout.fillWidth:true;Layout.fillHeight:true;spacing:10
                                ColumnLayout {
                                    Layout.fillWidth:true;Layout.fillHeight:true;Layout.preferredWidth:1;Layout.minimumWidth:0;spacing:2
                                    Label{text:"功能设计";color:Theme.muted;font.pixelSize:10}
                                    NodePreview{Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:80;plugin:card.modelData.id}
                                }
                                Rectangle{Layout.preferredWidth:1;Layout.fillHeight:true;Layout.topMargin:2;Layout.bottomMargin:2;color:Theme.muted;opacity:0.2}
                                ColumnLayout {
                                    Layout.fillWidth:true;Layout.fillHeight:true;Layout.preferredWidth:1;Layout.minimumWidth:0;spacing:2
                                    Label{text:"界面";color:Theme.muted;font.pixelSize:10}
                                    UiPreview{Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:80;plugin:card.modelData.id}
                                }
                            }
                            Label{text:card.unavailable||card.modelData.description;color:card.unavailable?"#D8B56C":Theme.muted;font.pixelSize:11;Layout.fillWidth:true;elide:Text.ElideRight}
                        }
                        MouseArea {
                            anchors.fill:parent;property point start;property bool dragged:false
                            onPressed:m=>{start=Qt.point(m.x,m.y);dragged=false;root.picked=card.modelData}
                            onPositionChanged:m=>{if(pressed&&!dragged&&!card.unavailable&&!App.readOnly&&Math.hypot(m.x-start.x,m.y-start.y)>10){dragged=true;App.dragPlugin(card.modelData.id)}}
                            onDoubleClicked:if(!App.readOnly)root.place()
                            ToolTip.visible:containsMouse&&!!card.unavailable;ToolTip.text:card.unavailable;hoverEnabled:true
                        }
                    }
                    Label{anchors.centerIn:parent;visible:!cards.count;text:"没有匹配的插件";color:Theme.muted}
                }
            }
            ScrollView {
                SplitView.preferredWidth:350;SplitView.minimumWidth:260;clip:true
                ColumnLayout {
                    width:320;spacing:12
                    Label{Layout.topMargin:14;text:root.picked.name||"选择插件";color:Theme.text;font.pixelSize:18;Layout.fillWidth:true;wrapMode:Text.WordWrap}
                    Label{text:root.picked.description||"双击添加到目标页待放置区，或拖到画布落位。";color:Theme.muted;Layout.fillWidth:true;wrapMode:Text.WordWrap}
                    Label{text:"功能设计预览";visible:!!root.picked.id;color:Theme.muted}
                    NodePreview{visible:!!root.picked.id&&!root.picked.id.startsWith("layout.");Layout.fillWidth:true;Layout.preferredHeight:160;plugin:root.picked.id||""}
                    Label{visible:!!root.picked.id&&root.picked.id.startsWith("layout.");text:"容器只参与界面布局";color:Theme.muted}
                    Label{text:"界面预览";visible:!!root.picked.id;color:Theme.muted}
                    UiPreview{visible:!!root.picked.id;Layout.fillWidth:true;Layout.preferredHeight:root.previewViews.length||root.picked.id&&root.picked.id.startsWith("layout.")?230:80;plugin:root.picked.id||""}
                    Label{visible:!!root.picked.id&&!!root.reason(root.picked);text:root.reason(root.picked);color:"#D8B56C";Layout.fillWidth:true;wrapMode:Text.WordWrap}
                    RowLayout {
                        visible:!!root.picked.id
                        ActionButton{objectName:"library.add";text:"添加到待放置区";enabled:!App.readOnly&&!App.busy&&!root.reason(root.picked)&&(App.libraryTarget==="layout"||!App.runtime.running);onClicked:root.place()}
                        IconButton{symbol:"star";text:"收藏／取消收藏";visible:!!root.picked.id&&!root.picked.id.startsWith("layout.");checked:(App.libraryPreferences.favorites||[]).indexOf(root.picked.id)>=0;checkable:true;onClicked:App.invoke("library.favorite",{plugin:root.picked.id,enabled:checked})}
                    }
                    Item{Layout.preferredHeight:10}
                }
            }
        }
    }
    FileDialog{id:pluginFile;title:"导入用户插件声明";nameFilters:["CoRig 插件声明 (*.json)"];onAccepted:App.invoke("plugin.import",{path:selectedFile.toString()})}
}
