import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "DesignGrid.js" as DesignGrid

// One editing surface owns selection, dragging, resizing and container insertion.
// UiItem renders the actual control tree underneath, without design handlers.
Item {
    id:root;anchors.fill:parent;objectName:"page.layout"
    property real zoom:0.65
    property real panX:30
    property real panY:30
    property var placement:({})
    property point cursorWorld:Qt.point(40,40)
    property var selection:App.selectedKind==="ui"?(App.selectedNodes.length?App.selectedNodes:App.selected?[App.selected]:[]):[]
    property var previewRects:[]
    property string targetId:""
    property int insertionIndex:-1
    property var canvasProperties:{App.project;return (App.uiItem((App.project.ui||{}).root||"root").properties||{})}
    property int gridSize:canvasProperties.grid_size||16
    function snap(value){return DesignGrid.snap(value,gridSize)}
    property real designWidth:canvasProperties.width||1280
    property real designHeight:canvasProperties.height||720
    property var containers:[{id:"layout.row",name:"横向容器",description:"在局部区域横向排列子对象"},{id:"layout.column",name:"纵向容器",description:"在局部区域纵向排列子对象"},{id:"layout.grid",name:"网格容器",description:"在局部区域按网格排列子对象"},{id:"layout.tabs",name:"分页容器",description:"在局部区域切换分页内容"}]
    // The public editor API reads the same live presentation state as gestures.
    function automationState(){return {zoom:zoom,pan_x:panX,pan_y:panY,left_width:objects.width,right_width:inspector.width,viewport_width:viewport.width,viewport_height:viewport.height}}
    function configureAutomation(options){
        if(options.fit)fit('')
        if(options.zoom!==undefined)zoom=options.zoom
        if(options.pan_x!==undefined)panX=options.pan_x
        if(options.pan_y!==undefined)panY=options.pan_y
        if(options.left_width!==undefined)objects.SplitView.preferredWidth=options.left_width
        if(options.right_width!==undefined)inspector.SplitView.preferredWidth=options.right_width
        return automationState()
    }
    function worldPoint(x,y){return Qt.point((x-panX)/zoom,(y-panY)/zoom)}
    function choose(id,modifiers){let ids=selection.slice();if(modifiers&Qt.ControlModifier){const at=ids.indexOf(id);if(at<0)ids.push(id);else ids.splice(at,1)}else if(ids.indexOf(id)<0)ids=[id];App.invoke("graph.select",{id:id,kind:"ui",ids:ids});viewport.forceActiveFocus()}
    function parentOf(id){const items=(App.project.ui||{}).items||[];return items.find(i=>(i.children||[]).indexOf(id)>=0)||({})}
    function isDescendant(id,ancestor){let p=id;while(p){if(p===ancestor)return true;p=parentOf(p).id||""}return false}
    function topSelection(){return selection.filter(id=>!selection.some(other=>id!==other&&isDescendant(id,other)))}
    function visual(id,item){if(!item)item=designRoot;if(item.itemId===id)return item;const children=item.children||[];for(let i=0;i<children.length;i++){const found=visual(id,children[i]);if(found)return found}return null}
    function rect(id){const item=visual(id);if(!item)return null;const p=item.mapToItem(canvas,0,0);return{id:id,x:p.x,y:p.y,width:item.width,height:item.height}}
    function contentRect(id){const item=visual(id);if(!item)return null;const area=item.contentArea,p=area.mapToItem(canvas,0,0);return{x:p.x,y:p.y,width:area.width,height:area.height}}
    function hit(point,item){if(!item)item=designRoot;if(!item.visible)return "";const p=canvas.mapToItem(item,point.x,point.y);const inside=p.x>=0&&p.y>=0&&p.x<=item.width&&p.y<=item.height;if(!inside&&item.clip)return "";const children=Array.from(item.children||[]).sort((a,b)=>a.z-b.z);for(let i=children.length-1;i>=0;i--){const found=hit(point,children[i]);if(found)return found}return inside?(item.itemId||""):""}
    function locateTarget(point,ignored){let id=hit(point)||((App.project.ui||{}).root||"root");while(id){const r=App.uiItem(id);if(r.plugin&&r.plugin.startsWith("layout.")&&!(ignored||[]).some(other=>isDescendant(id,other)))return id;id=parentOf(id).id||""}return(App.project.ui||{}).root||"root"}
    function insertion(point,id){const r=App.uiItem(id),p=rect(id);if(!p||r.plugin==="layout.canvas")return -1;const children=(r.children||[]).filter(c=>selection.indexOf(c)<0&&c!=="pending");for(let i=0;i<children.length;i++){const box=rect(children[i]);if(!box)continue;if(r.plugin==="layout.row"||r.plugin==="layout.tabs"){if(point.x<box.x+box.width/2)return i}else if(r.plugin==="layout.grid"){if(point.y<box.y+box.height/2&&point.x<box.x+box.width)return i}else if(point.y<box.y+box.height/2)return i}return children.length}
    function localPosition(point,parent){const item=visual(parent);if(!item)return[snap(point.x),snap(point.y)];const p=canvas.mapToItem(item.contentArea,point.x,point.y);return[snap(p.x),snap(p.y)]}
    function finishPlacement(point){
        const target=locateTarget(point,placement.id?[placement.id]:[]),position=localPosition(point,target),index=insertion(point,target)
        if(placement.id)App.invoke("editor.place",{page:"layout",id:placement.id,parent:target,position:position,index:index})
        else if(placement.plugin){const id="ui_"+Date.now().toString(36)+Math.random().toString(36).slice(2,7);App.invoke("batch",{commands:[{op:"editor.add",args:{page:"layout",plugin:placement.plugin,id:id}},{op:"editor.place",args:{page:"layout",id:id,parent:target,position:position,index:index}}]})}
        placement=({});targetId="";insertionIndex=-1
    }
    function edit(operation){if(operation==="copy")actions.copy();else if(operation==="replace")actions.replace();else actions.ask(operation,topSelection())}
    function fit(){zoom=Math.min(1.5,Math.max(.1,Math.min((viewport.width-60)/designWidth,(viewport.height-60)/designHeight)));panX=(viewport.width-designWidth*zoom)/2;panY=(viewport.height-designHeight*zoom)/2}
    function openQuick(x,y){const point=viewport.mapToItem(root,x,y);quick.x=Math.max(0,Math.min(root.width-quick.width,point.x));quick.y=Math.max(42,Math.min(root.height-quick.height,point.y));quick.open()}
    Rectangle{anchors.fill:parent;color:Theme.panel;z:-1}
    EditorActions{id:actions;anchors.fill:parent;page:"layout";ids:root.topSelection()}
    SyncDialog{id:syncDialog}
    ColumnLayout {
        anchors.fill:parent;spacing:0
        RowLayout {
            Layout.fillWidth:true;Layout.preferredHeight:42;spacing:4
            IconButton{objectName:"layout.library";Layout.leftMargin:8;symbol:"library";text:"插件库";onClicked:App.openLibrary("layout")}
            IconButton{objectName:"layout.quickadd";symbol:"plus";text:"快速添加";enabled:!App.readOnly;onClicked:root.openQuick(80,50)}
            IconButton{objectName:"layout.sync";symbol:"sync-to";text:"同步到功能设计";enabled:!App.readOnly&&!App.busy&&!App.runtime.running;onClicked:syncDialog.inspect("ui_to_graph")}
            IconButton{objectName:"layout.update";symbol:"ui_update";text:"检查功能设计到界面设计的同步清单";enabled:!App.readOnly&&!App.busy;onClicked:syncDialog.inspect("graph_to_ui")}
            Rectangle{width:1;height:22;color:Theme.border}
            IconButton{symbol:"fit";text:"适应画布（F）";onClicked:root.fit()}
            Item{Layout.fillWidth:true}
            Label{text:"网格";color:Theme.muted}
            SpinBox{objectName:"layout.grid";from:1;to:128;editable:true;value:root.gridSize;Layout.preferredWidth:100;onValueModified:App.invoke("ui.item.patch",{id:(App.project.ui||{}).root||"root",properties:{grid_size:value}})}
            Label{text:Math.round(root.zoom*100)+"%";color:Theme.muted}
            IconButton{symbol:"runtime";text:"查看运行界面";onClicked:App.workspaces.open("runtime")}
            IconButton{symbol:"sidebar-left";text:"展开／收起对象栏";onClicked:objects.SplitView.preferredWidth=objects.width>8?0:220}
            IconButton{symbol:"sidebar-right";text:"展开／收起属性栏";onClicked:inspector.SplitView.preferredWidth=inspector.width>8?0:306}
        }
        Rectangle{Layout.fillWidth:true;height:1;color:Theme.border}
        SplitView {
            Layout.fillWidth:true;Layout.fillHeight:true
            handle:Rectangle{implicitWidth:6;implicitHeight:6;color:SplitHandle.pressed?Theme.blue:SplitHandle.hovered?Theme.border:Theme.canvas}
            ObjectPanel{id:objects;objectName:"layout.objects";page:"layout";selection:root.selection;SplitView.preferredWidth:220;SplitView.minimumWidth:0;SplitView.maximumWidth:420;onPicked:(id,modifiers)=>root.choose(id,modifiers);onPlacementRequested:id=>{root.placement={id:id};viewport.forceActiveFocus()};onEditRequested:operation=>root.edit(operation)}
            Rectangle {
                id:viewport;objectName:"layout.viewport";SplitView.fillWidth:true;SplitView.minimumWidth:260;color:Theme.canvas;clip:true;focus:true
                Keys.onPressed:e=>{
                    if(e.key===Qt.Key_Escape){root.placement=({});root.previewRects=[];root.targetId="";App.invoke("graph.select",{id:"",kind:"ui"});e.accepted=true}
                    else if(e.key===Qt.Key_Delete&&!App.readOnly){root.edit("remove");e.accepted=true}
                    else if(e.key===Qt.Key_C&&e.modifiers&Qt.ControlModifier){actions.copy();e.accepted=true}
                    else if(e.key===Qt.Key_V&&e.modifiers&Qt.ControlModifier&&!App.readOnly){actions.paste();e.accepted=true}
                    else if(e.key===Qt.Key_A&&e.modifiers&Qt.ControlModifier){const ids=(App.uiItem((App.project.ui||{}).root||"root").children||[]).filter(id=>id!=="pending");App.invoke("graph.select",{id:ids[0]||"",kind:"ui",ids:ids});e.accepted=true}
                    else if(e.key===Qt.Key_F){root.fit();e.accepted=true}
                }
                Rectangle{x:root.panX;y:root.panY;width:root.designWidth*root.zoom;height:root.designHeight*root.zoom;color:root.canvasProperties.background||Theme.canvas}
                Canvas {
                    id:paper;anchors.fill:parent
                    Connections{target:root;function onPanXChanged(){paper.requestPaint()}function onPanYChanged(){paper.requestPaint()}function onZoomChanged(){paper.requestPaint()}function onGridSizeChanged(){paper.requestPaint()}}
                    onPaint:{const c=getContext("2d");c.reset();let step=root.gridSize*root.zoom;while(step<5)step*=2;c.strokeStyle=Theme.dark?"#243447":"#BDCEDB";c.lineWidth=1;c.beginPath();for(let x=((root.panX%step)+step)%step;x<width;x+=step){c.moveTo(Math.round(x)+.5,0);c.lineTo(Math.round(x)+.5,height)}for(let y=((root.panY%step)+step)%step;y<height;y+=step){c.moveTo(0,Math.round(y)+.5);c.lineTo(width,Math.round(y)+.5)}c.stroke()}
                }
                Item {
                    id:canvas;x:root.panX;y:root.panY;width:root.designWidth;height:root.designHeight;scale:root.zoom;transformOrigin:Item.TopLeft
                    UiItem{id:designRoot;itemId:(App.project.ui||{}).root||"root";designMode:true;anchors.fill:parent}
                    Rectangle{anchors.fill:parent;color:"transparent";border.color:Theme.border;border.width:1/root.zoom}
                    Repeater {
                        model:root.selection
                        Rectangle {
                            required property string modelData
                            property var box:{App.project;return root.rect(modelData)}
                            visible:!!box;x:box?box.x:0;y:box?box.y:0;width:box?box.width:0;height:box?box.height:0
                            color:"transparent";border.color:Theme.blue;border.width:1.6/root.zoom;z:500
                        }
                    }
                    Repeater {
                        model:root.previewRects
                        Rectangle{required property var modelData;x:modelData.x;y:modelData.y;width:modelData.width;height:modelData.height;color:"#253D80AC";border.color:Theme.blue;border.width:1.5/root.zoom;z:600;Label{anchors.centerIn:parent;text:(App.uiItem(parent.modelData.id).properties||{}).title||"放置";color:Theme.text}}
                    }
                    Rectangle {
                        id:insertionMark;z:700
                        property var target:{App.project;return root.targetId?root.contentRect(root.targetId):null}
                        property var record:root.targetId?App.uiItem(root.targetId):({})
                        property bool horizontal:record.plugin==="layout.row"||record.plugin==="layout.tabs"
                        property var children:(record.children||[]).filter(id=>id!=="pending"&&root.selection.indexOf(id)<0)
                        property var next:root.insertionIndex>=0&&root.insertionIndex<children.length?root.rect(children[root.insertionIndex]):null
                        visible:!!target&&record.plugin!=="layout.canvas"&&root.insertionIndex>=0
                        x:target?(horizontal?(next?next.x:target.x+target.width):target.x):0
                        y:target?(horizontal?target.y:(next?next.y:target.y+target.height)):0
                        width:horizontal?3/root.zoom:target?target.width:0;height:horizontal&&target?target.height:3/root.zoom;color:Theme.blue
                    }
                }
                MouseArea {
                    id:gestures;anchors.fill:parent;hoverEnabled:true;acceptedButtons:Qt.LeftButton|Qt.MiddleButton|Qt.RightButton;objectName:"layout.gestures"
                    property point start;property point oldPan;property point worldStart;property bool panning:false;property bool dragging:false;property bool rubbering:false;property var original:[]
                    onPressed:m=>{
                        if(m.button===Qt.RightButton&&(root.placement.id||root.placement.plugin||dragging||root.previewRects.length)){root.placement=({});root.previewRects=[];root.targetId="";root.insertionIndex=-1;dragging=false;rubbering=false;original=[];rubber.visible=false;return}
                        viewport.forceActiveFocus();start=Qt.point(m.x,m.y);worldStart=root.worldPoint(m.x,m.y);oldPan=Qt.point(root.panX,root.panY);root.cursorWorld=worldStart;panning=m.button===Qt.MiddleButton||(m.button===Qt.LeftButton&&(m.modifiers&Qt.AltModifier));dragging=false;rubbering=false
                        if(panning)return
                        if(m.button===Qt.LeftButton&&(root.placement.id||root.placement.plugin)){root.finishPlacement(worldStart);return}
                        const id=root.hit(worldStart)
                        if(m.button===Qt.RightButton){if(id&&id!=="root")root.choose(id,0);root.openQuick(m.x,m.y);return}
                        if(id&&id!=="root"){root.choose(id,m.modifiers);original=root.topSelection().map(id=>root.rect(id)).filter(b=>!!b)}else{rubbering=true;original=[];if(!(m.modifiers&Qt.ControlModifier))App.invoke("graph.select",{id:"",kind:"ui"})}
                    }
                    onPositionChanged:m=>{
                        root.cursorWorld=root.worldPoint(m.x,m.y)
                        if(root.placement.id||root.placement.plugin){root.targetId=root.locateTarget(root.cursorWorld,root.placement.id?[root.placement.id]:[]);root.insertionIndex=root.insertion(root.cursorWorld,root.targetId)}
                        if(!pressed)return
                        if(panning){root.panX=oldPan.x+m.x-start.x;root.panY=oldPan.y+m.y-start.y;return}
                        if(rubbering){rubber.x=Math.min(m.x,start.x);rubber.y=Math.min(m.y,start.y);rubber.width=Math.abs(m.x-start.x);rubber.height=Math.abs(m.y-start.y);rubber.visible=rubber.width+rubber.height>8;return}
                        if(App.readOnly||!original.length||App.busy)return
                        const dx=(m.x-start.x)/root.zoom,dy=(m.y-start.y)/root.zoom
                        if(Math.hypot(dx,dy)>3){dragging=true;root.previewRects=original.map(b=>({id:b.id,x:root.snap(b.x+dx),y:root.snap(b.y+dy),width:b.width,height:b.height}));root.targetId=root.locateTarget(root.cursorWorld,root.topSelection());root.insertionIndex=root.insertion(root.cursorWorld,root.targetId)}
                    }
                    onReleased:m=>{
                        if(dragging){const target=root.targetId||"root",commands=[];root.previewRects.forEach((b,i)=>{const position=root.localPosition(Qt.point(b.x,b.y),target),parent=root.parentOf(b.id);if(parent.id===target&&App.uiItem(target).plugin==="layout.canvas")commands.push({op:"ui.item.patch",args:{id:b.id,properties:{x:position[0],y:position[1]}}});else commands.push({op:"editor.place",args:{page:"layout",id:b.id,parent:target,position:position,index:root.insertionIndex<0?-1:root.insertionIndex+i}})});App.invoke("batch",{commands:commands})}
                        if(rubbering&&rubber.visible){const a=root.worldPoint(rubber.x,rubber.y),b=root.worldPoint(rubber.x+rubber.width,rubber.y+rubber.height),ids=[];((App.project.ui||{}).items||[]).forEach(item=>{if(item.id==="root"||item.id==="pending")return;const box=root.rect(item.id);if(box&&box.x>=a.x&&box.y>=a.y&&box.x+box.width<=b.x&&box.y+box.height<=b.y)ids.push(item.id)});App.invoke("graph.select",{id:ids[0]||"",kind:"ui",ids:ids})}
                        root.previewRects=[];root.targetId="";root.insertionIndex=-1;rubber.visible=false;dragging=false;rubbering=false;panning=false
                    }
                    onWheel:w=>{const before=root.worldPoint(w.x,w.y);root.zoom=Math.max(.1,Math.min(3,root.zoom*Math.pow(1.0015,w.angleDelta.y)));root.panX=w.x-before.x*root.zoom;root.panY=w.y-before.y*root.zoom;w.accepted=true}
                }
                Rectangle{id:rubber;visible:false;color:"#2266B4F4";border.color:Theme.blue;z:30}
                Repeater {
                    model:root.selection.length===1&&!App.readOnly?[0,1,2,3]:[]
                    Rectangle {
                        id:resize;required property int modelData
                        property var box:{App.project;return root.rect(root.selection[0])}
                        visible:!!box;width:9;height:9;z:50;color:Theme.text;border.color:Theme.blue
                        x:box?root.panX+(box.x+(modelData%2?box.width:0))*root.zoom-4:0
                        y:box?root.panY+(box.y+(modelData>=2?box.height:0))*root.zoom-4:0
                        MouseArea {
                            anchors.fill:parent;anchors.margins:-5;cursorShape:resize.modelData===0||resize.modelData===3?Qt.SizeFDiagCursor:Qt.SizeBDiagCursor
                            property point start;property var original:({})
                            onPressed:m=>{const p=mapToItem(viewport,m.x,m.y);start=Qt.point(p.x,p.y);original=Object.assign({},resize.box)}
                            onPositionChanged:m=>{if(!pressed)return;const p=mapToItem(viewport,m.x,m.y),dx=(p.x-start.x)/root.zoom,dy=(p.y-start.y)/root.zoom,left=resize.modelData%2===0,top=resize.modelData<2,w=Math.max(root.gridSize*Math.ceil(40/root.gridSize),root.snap(original.width+(left?-dx:dx))),h=Math.max(root.gridSize*Math.ceil(32/root.gridSize),root.snap(original.height+(top?-dy:dy)));root.previewRects=[{id:original.id,x:left?original.x+original.width-w:original.x,y:top?original.y+original.height-h:original.y,width:w,height:h}]}
                            onReleased:{if(root.previewRects.length){const box=root.previewRects[0],parent=root.parentOf(box.id).id||"root",position=root.localPosition(Qt.point(box.x,box.y),parent);App.invoke("ui.item.patch",{id:box.id,properties:{x:position[0],y:position[1],width:box.width,height:box.height}})}root.previewRects=[]}
                        }
                    }
                }
                Rectangle{visible:!!root.placement.id||!!root.placement.plugin;z:40;x:root.panX+root.cursorWorld.x*root.zoom;y:root.panY+root.cursorWorld.y*root.zoom;width:Math.min(320,root.designWidth)*root.zoom;height:160*root.zoom;color:"#303D80AC";border.color:Theme.blue;Label{anchors.centerIn:parent;text:root.placement.plugin?(App.definition(root.placement.plugin).name||"容器"):(App.uiItem(root.placement.id).properties||{}).title||"放置对象";color:Theme.text}}
                DropArea {
                    anchors.fill:parent;keys:["application/x-corig-object","application/x-corig-plugin","application/x-corig-ui"]
                    onPositionChanged:drag=>{const point=root.worldPoint(drag.x,drag.y);root.targetId=root.locateTarget(point,root.selection);root.insertionIndex=root.insertion(point,root.targetId)}
                    onExited:{root.targetId="";root.insertionIndex=-1}
                    onDropped:drop=>{if(drop.formats.indexOf("application/x-corig-object")>=0){const payload=JSON.parse(drop.getDataAsString("application/x-corig-object"));if(payload.page!=="layout")return;root.placement={id:payload.id}}else if(drop.formats.indexOf("application/x-corig-ui")>=0){root.placement={id:drop.getDataAsString("application/x-corig-ui")}}else{root.placement={plugin:drop.getDataAsString("application/x-corig-plugin")}}root.finishPlacement(root.worldPoint(drop.x,drop.y));drop.acceptProposedAction()}
                }
            }
            Inspector{id:inspector;page:"layout";objectName:"layout.inspector";SplitView.preferredWidth:306;SplitView.minimumWidth:0;SplitView.maximumWidth:520;clip:true}
        }
    }
    Menu{id:objectMenu;MenuItem{text:"复制";onTriggered:actions.copy()}MenuItem{text:"替换";onTriggered:actions.replace()}MenuItem{text:"撤下";onTriggered:root.edit("withdraw")}MenuItem{text:"删除";onTriggered:root.edit("remove")}}
    Popup {
        id:quick;objectName:"layout.quickmenu";width:350;height:390;focus:true;closePolicy:Popup.CloseOnEscape|Popup.CloseOnPressOutside
        ColumnLayout {
            anchors.fill:parent
            TextField{id:search;Layout.fillWidth:true;placeholderText:"搜索控件或容器";onAccepted:if(results.currentItem)results.currentItem.place()}
            ListView {
                id:results;Layout.fillWidth:true;Layout.fillHeight:true;clip:true;currentIndex:0
                model:App.registry.filter(p=>p.views&&Object.keys(p.views).length).concat(root.containers).filter(p=>(p.name+" "+p.id).toLowerCase().indexOf(search.text.toLowerCase())>=0)
                delegate:ItemDelegate{required property var modelData;width:results.width;text:modelData.name;function place(){root.placement={plugin:modelData.id};quick.close();viewport.forceActiveFocus()}onClicked:place()}
            }
        }
        onOpened:{search.text="";search.forceActiveFocus()}
    }
}
