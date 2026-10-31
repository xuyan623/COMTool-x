import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import CoRig.Native 1.0

Item {
    id: root
    anchors.fill: parent
    objectName: "page.graph"
    Rectangle{anchors.fill:parent;color:Theme.panel;z:-1}
    property real zoom: 1
    property real panX: 50
    property real panY: 50
    property bool animationEnabled: true
    property var linkFrom: ({})
    property string disconnectJob:""
    property string connectionJob:""
    property var pendingSource:({})
    property var hoverPort:({})
    property string sourceKey:linkFrom.node?linkFrom.node+"/"+linkFrom.id:""
    property string hoverKey:hoverPort.node?hoverPort.node+"/"+hoverPort.id:""
    property var options:{App.project;return linkFrom.node?App.connectionOptions(linkFrom.node,linkFrom.id):({})}
    property var placement:({})
    property var movement:({})
    signal cancelDragRequested()
    property var portColors:({number:"#8CBEEA",bytes:"#E4B581",string:"#BDA6E7",boolean:"#8AC9BE",object:"#B8C5D2",samples:"#8ACFA4",any:"#C4D0DB"})
    property var connections:{
        const result={};(App.project.wires||[]).forEach(w=>{result[w.source[0]+"/"+w.source[1]]={wire:w.id,record:w,color:w.color};result[w.target[0]+"/"+w.target[1]]={wire:w.id,record:w,color:w.color}});return result
    }
    property point cursorWorld: Qt.point(0,0)
    property point addPosition: Qt.point(80,80)
    property string selectedWire: App.selectedKind === "wire" ? App.selected : ""
    property var wireEditor:{App.project;return App.wireEditState(selectedWire)}
    property alias wireRenderer:wireCanvas
    // The public editor API reads the same live presentation state as gestures.
    function automationState(){return {zoom:zoom,pan_x:panX,pan_y:panY,left_width:objects.width,right_width:inspector.width,viewport_width:viewport.width,viewport_height:viewport.height,animation:animationEnabled}}
    function configureAutomation(options){
        if(options.fit)fit('')
        if(options.zoom!==undefined)zoom=options.zoom
        if(options.pan_x!==undefined)panX=options.pan_x
        if(options.pan_y!==undefined)panY=options.pan_y
        if(options.left_width!==undefined)objects.SplitView.preferredWidth=options.left_width
        if(options.right_width!==undefined)inspector.SplitView.preferredWidth=options.right_width
        if(options.animation!==undefined)animationEnabled=options.animation
        return automationState()
    }
    function worldPoint(x, y) { return Qt.point((x - panX) / zoom, (y - panY) / zoom) }
    function rotateSelection() {
        if(App.readOnly || App.selectedKind!=="node" || !App.selectedNodes.length)return
        App.invoke("batch",{commands:App.selectedNodes.map(id=>({op:"graph.node.rotate",args:{id:id,degrees:90}}))})
    }
    function addWirePoint(id,position) {
        if(App.readOnly || !id.length)return
        const edit=App.wireEditState(id,position.x,position.y),points=(edit.points||[]).slice()
        points.splice(edit.insertion_index||0,0,[position.x,position.y])
        App.invoke("graph.wire.patch",{id:id,changes:{points:points}})
    }
    function fit(node) {
        const nodes = App.graphNodes.filter(n => !node || n.id === node)
        if (!nodes.length) { panX = 50; panY = 50; zoom = 1; return }
        let minX=Infinity, minY=Infinity, maxX=-Infinity, maxY=-Infinity
        nodes.forEach(n => { minX=Math.min(minX,n.position[0]); minY=Math.min(minY,n.position[1]); maxX=Math.max(maxX,n.position[0]+n.geometry.width); maxY=Math.max(maxY,n.position[1]+n.geometry.height) })
        zoom = Math.min(1.5, Math.max(.1, Math.min((viewport.width-100)/(maxX-minX), (viewport.height-100)/(maxY-minY))))
        panX = viewport.width/2 - (minX+maxX)/2*zoom; panY = viewport.height/2 - (minY+maxY)/2*zoom
    }
    function sourcePort(node,id) {
        const n=App.graphNodes.find(n=>n.id===node);if(!n)return({})
        const p=n.geometry.ports.find(p=>p.id===id);if(!p)return({})
        return Object.assign({},p,{node:node,world_x:n.position[0]+p.x,world_y:n.position[1]+p.y})
    }
    function cancelLink(){if(connectionJob.length)return;linkFrom=({});hoverPort=({});pendingSource=({})}
    function cancelHeld(){cancelLink();placement=({});movement=({});wireRenderer.clearPreview();cancelDragRequested()}
    function connectPort(port,node) {
        if (App.readOnly || App.runtime.running || App.busy) return
        const key=node+"/"+port.id,connection=connections[key]
        if(linkFrom.node) {
            if(key===sourceKey){cancelLink();return}
            if(!(options[key]||{}).compatible)return
            let args={source:[linkFrom.node,linkFrom.id],target:[node,port.id],color:portColors[linkFrom.data_type]||"#8CBEEA"}
            if(linkFrom.previous){const w=linkFrom.previous;args.id=w.id;args.routing=w.routing;args.width=w.width;args.color=w.color;args.points=w.points||[]}
            const result=App.invoke("graph.wire.add",args);connectionJob=result.job||"";return
        }
        if(port.direction==="out"&&!connection){linkFrom=sourcePort(node,port.id);return}
        if(port.direction==="in"&&connection){const w=connection.record;pendingSource=sourcePort(w.source[0],w.source[1]);pendingSource.previous=w;disconnectJob=App.invoke("graph.wire.remove",{id:w.id}).job||""}
    }
    function choose(id,modifiers){let ids=App.selectedKind==="node"?App.selectedNodes.slice():[];if(modifiers&Qt.ControlModifier){const at=ids.indexOf(id);if(at<0)ids.push(id);else ids.splice(at,1)}else ids=[id];App.invoke("graph.select",{id:id,ids:ids});viewport.forceActiveFocus()}
    function edit(operation){if(operation==="copy")actions.copy();else if(operation==="replace")actions.replace();else if(App.selectedKind==="wire"&&operation==="remove")wireDelete.open();else actions.ask(operation,actions.ids)}
    function placeAt(point){
        if(placement.id)App.invoke("editor.place",{page:"graph",id:placement.id,position:[point.x,point.y]})
        else if(placement.plugin)App.invoke("graph.node.add",{plugin:placement.plugin,position:[point.x,point.y]})
        placement=({})
    }
    function openQuick(x,y) {
        addPosition = worldPoint(x,y); quick.x = Math.max(0,Math.min(viewport.width-quick.width,x)); quick.y = Math.max(42,Math.min(root.height-quick.height,y+42)); quick.open()
    }
    Connections {
        target: App
        function onGraphFocusRequested(node) { root.fit(node) }
        function onJobFinished(id){if(id===root.disconnectJob){root.disconnectJob="";if(App.job(id).state==="complete")root.linkFrom=root.pendingSource;root.pendingSource=({})}else if(id===root.connectionJob){root.connectionJob="";if(App.job(id).state==="complete")root.cancelLink()}}
        function onRuntimeChanged(){if(App.runtime.running)root.cancelLink()}
    }
    EditorActions{id:actions;anchors.fill:parent;page:"graph";ids:App.selectedKind==="node"?App.selectedNodes:[]}
    SyncDialog{id:syncDialog}
    Dialog{id:wireDelete;width:430;title:"删除导线";modal:true;anchors.centerIn:parent;standardButtons:Dialog.Ok|Dialog.Cancel;contentItem:Label{text:"解除当前导线连接："+App.selected;color:Theme.text;padding:12;wrapMode:Text.WrapAnywhere}onAccepted:App.invoke("graph.wire.remove",{id:App.selected})}
    ColumnLayout {
        anchors.fill: parent; spacing: 0
        RowLayout {
            Layout.fillWidth: true; Layout.preferredHeight: 42; spacing: 4
            IconButton { objectName:"graph.library";symbol:"library";text:"插件库";Layout.leftMargin:8;onClicked:App.openLibrary("graph") }
            IconButton { objectName:"graph.quickadd";symbol:"plus";text:"快速添加";enabled:!App.readOnly;onClicked:root.openQuick(100,70) }
            IconButton { objectName:"graph.sync";symbol:"sync-to";text:"同步到界面设计";enabled:!App.readOnly&&!App.busy;onClicked:syncDialog.inspect("graph_to_ui") }
            Rectangle { width: 1; height: 22; color: Theme.border }
            IconButton { symbol:"fit";text:"适应画布（F）";onClicked:root.fit("") }
            IconButton { symbol:"rotate";text:"旋转选中节点 90°（空格）";enabled:App.selectedKind==="node"&&App.selected.length>0&&!App.readOnly;onClicked:root.rotateSelection() }
            Item { Layout.fillWidth: true }
            IconButton {symbol:"sidebar-left";text:"展开／收起对象栏";onClicked:objects.SplitView.preferredWidth=objects.width>8?0:220}
            IconButton {symbol:"sidebar-right";text:"展开／收起属性栏";onClicked:inspector.SplitView.preferredWidth=inspector.width>8?0:306}
            CheckBox { objectName:"graph.activity";text: "数据活动"; checked: root.animationEnabled; onClicked: root.animationEnabled = checked }
            Label { text: Math.round(root.zoom*100) + "%"; color: Theme.muted; Layout.preferredWidth: 48 }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
        SplitView {
            Layout.fillWidth: true; Layout.fillHeight: true; orientation: Qt.Horizontal
            handle: Rectangle { implicitWidth:6;implicitHeight:6;color:SplitHandle.pressed?Theme.blue:SplitHandle.hovered?Theme.border:Theme.canvas }
            ObjectPanel{id:objects;objectName:"graph.objects";page:"graph";selection:App.selectedKind==="node"?App.selectedNodes:[];SplitView.preferredWidth:220;SplitView.minimumWidth:0;SplitView.maximumWidth:420;onPicked:(id,modifiers)=>root.choose(id,modifiers);onPlacementRequested:id=>{root.placement={id:id};viewport.forceActiveFocus()};onEditRequested:operation=>root.edit(operation)}
            Rectangle {
                id: viewport
                objectName:"graph.viewport"
                SplitView.fillWidth: true; SplitView.minimumWidth: 300; color: Theme.canvas; clip: true
                focus: true
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Escape) { root.cancelHeld();App.invoke("graph.select", {id:""}); event.accepted=true }
                    else if (event.key === Qt.Key_Delete && !App.readOnly) {root.edit("remove");event.accepted=true}
                    else if(event.key===Qt.Key_C&&event.modifiers&Qt.ControlModifier){actions.copy();event.accepted=true}
                    else if(event.key===Qt.Key_V&&event.modifiers&Qt.ControlModifier&&!App.readOnly){actions.paste();event.accepted=true}
                    else if (event.key === Qt.Key_Space && !event.isAutoRepeat && !App.readOnly && App.selectedKind === "node") { root.rotateSelection(); event.accepted=true }
                    else if (event.key === Qt.Key_A && event.modifiers & Qt.ControlModifier) { const ids=App.graphNodes.map(n=>n.id); App.invoke("graph.select", {id:ids[0]||"",ids:ids}); event.accepted=true }
                    else if (event.key === Qt.Key_F) { root.fit(""); event.accepted=true }
                }
                HoverHandler{onPointChanged:{root.cursorWorld=root.worldPoint(point.position.x,point.position.y)}}
                Canvas {
                    id: grid; anchors.fill: parent
                    Connections { target: root; function onPanXChanged() { grid.requestPaint() } function onPanYChanged() { grid.requestPaint() } function onZoomChanged() { grid.requestPaint() } }
                    onPaint: { let ctx=getContext("2d"); ctx.reset(); ctx.fillStyle="#2B3B4C"; const step=32*root.zoom; if(step<10)return; for(let x=((root.panX%step)+step)%step;x<width;x+=step)for(let y=((root.panY%step)+step)%step;y<height;y+=step)ctx.fillRect(x,y,1,1) }
                }
                MouseArea {
                    id: background; anchors.fill: parent; acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton; hoverEnabled: true
                    property point pressedAt
                    property point oldPan
                    property bool panning: false
                    property bool selecting: false
                    onPressed: mouse => {
                        viewport.forceActiveFocus(); pressedAt=Qt.point(mouse.x,mouse.y); oldPan=Qt.point(root.panX,root.panY)
                        if(mouse.button===Qt.RightButton&&(root.linkFrom.node||root.placement.id||root.placement.plugin||Object.keys(root.movement).length)){root.cancelHeld();return}
                        panning=mouse.button===Qt.MiddleButton || (mouse.button===Qt.LeftButton && mouse.modifiers & Qt.AltModifier)
                        const p=root.worldPoint(mouse.x,mouse.y); root.cursorWorld=p
                        if(mouse.button===Qt.LeftButton&&(root.placement.id||root.placement.plugin)){root.placeAt(p);return}
                        if(mouse.button===Qt.LeftButton && !panning) {
                            if(root.linkFrom.node) { root.cancelLink(); return }
                            const wire=App.hitWire(p.x,p.y,root.zoom)
                            App.invoke("graph.select",{id:wire,kind:wire?"wire":"node"}); selecting=!wire
                        }
                        if(mouse.button===Qt.RightButton) { const wire=App.hitWire(p.x,p.y,root.zoom); if(wire){App.invoke("graph.select",{id:wire,kind:"wire"}); wireMenu.popup()}else root.openQuick(mouse.x,mouse.y) }
                    }
                    onPositionChanged: mouse => {
                        root.cursorWorld=root.worldPoint(mouse.x,mouse.y)
                        if(pressed && panning){root.panX=oldPan.x+mouse.x-pressedAt.x;root.panY=oldPan.y+mouse.y-pressedAt.y}
                        if(pressed && selecting){rubber.x=Math.min(mouse.x,pressedAt.x);rubber.y=Math.min(mouse.y,pressedAt.y);rubber.width=Math.abs(mouse.x-pressedAt.x);rubber.height=Math.abs(mouse.y-pressedAt.y);rubber.visible=rubber.width+rubber.height>8}
                    }
                    onReleased: mouse => {
                        if(selecting && rubber.visible){const a=root.worldPoint(rubber.x,rubber.y),b=root.worldPoint(rubber.x+rubber.width,rubber.y+rubber.height);const ids=App.graphNodes.filter(n=>n.position[0]>=a.x&&n.position[1]>=a.y&&n.position[0]+n.geometry.width<=b.x&&n.position[1]+n.geometry.height<=b.y).map(n=>n.id);App.invoke("graph.select",{id:ids[0]||"",ids:ids})}
                        rubber.visible=false;selecting=false;panning=false
                    }
                    onDoubleClicked:mouse=>{if(mouse.button===Qt.LeftButton&&!App.readOnly){const p=root.worldPoint(mouse.x,mouse.y),id=App.hitWire(p.x,p.y,root.zoom);if(id){App.invoke("graph.select",{id:id,kind:"wire"});root.addWirePoint(id,p)}}}
                    onWheel: wheel => {
                        const before=root.worldPoint(wheel.x,wheel.y);root.zoom=Math.max(.1,Math.min(3,root.zoom*Math.pow(1.0015,wheel.angleDelta.y)));root.panX=wheel.x-before.x*root.zoom;root.panY=wheel.y-before.y*root.zoom;wheel.accepted=true
                    }
                }
                Item {
                    id: world; x:root.panX; y:root.panY; scale:root.zoom; transformOrigin:Item.TopLeft
                    width:viewport.width/root.zoom; height:viewport.height/root.zoom
                    WireLayer { id:wireCanvas; anchors.fill:parent; nodes:App.graphNodes; wires:App.project.wires||[]; active:!!App.runtime.active;running:!!App.runtime.running;statistics:App.runtime.wires||({}); selected:root.selectedWire; animated:root.animationEnabled; zoom:root.zoom }
                    Shape {
                        id:linkShape;anchors.fill:parent;visible:!!root.linkFrom.node;z:30
                        property bool snap:!!(root.options[root.hoverKey]||{}).compatible
                        property point end:snap?Qt.point(root.hoverPort.world_x,root.hoverPort.world_y):root.cursorWorld
                        ShapePath {strokeColor:root.portColors[root.linkFrom.data_type]||"#9CD3FF";strokeWidth:1.8;fillColor:"transparent";startX:root.linkFrom.world_x||0;startY:root.linkFrom.world_y||0;PathCubic{x:linkShape.end.x;y:linkShape.end.y;control1X:(root.linkFrom.world_x||0)+(root.linkFrom.nx||0)*80;control1Y:(root.linkFrom.world_y||0)+(root.linkFrom.ny||0)*80;control2X:linkShape.end.x+(root.hoverPort.nx||-1)*60;control2Y:linkShape.end.y+(root.hoverPort.ny||0)*60}}
                    }
                    Repeater {
                        model:App.graphNodes
                        Item {
                            id:nodeBox
                            required property var modelData
                            property var geometry:modelData.geometry
                            property string executionMarker:(App.runtime.errors||{})[modelData.id]?"error":App.runtime.debug&&(App.runtime.cursor||{}).next_node===modelData.id?"next":App.runtime.debug&&App.runtime.current_node===modelData.id?"last":""
                            required property int index
                            x:(root.movement[modelData.id]||modelData.position)[0];y:(root.movement[modelData.id]||modelData.position)[1];width:geometry.width;height:geometry.height;z:App.graphNodes.length-index
                            objectName:"graph.node."+modelData.id
                            MouseArea {
                                id:nodeMouse;anchors.fill:parent;acceptedButtons:Qt.LeftButton|Qt.RightButton
                                property point start
                                property point nodeStart
                                property bool moved:false
                                property var origins:({})
                                onPressed:mouse=>{if(mouse.button===Qt.RightButton&&Object.keys(root.movement).length){root.cancelHeld();return}viewport.forceActiveFocus();const p=mapToItem(viewport,mouse.x,mouse.y);start=Qt.point(p.x,p.y);nodeStart=Qt.point(nodeBox.x,nodeBox.y);moved=false;let ids=App.selectedNodes.slice();if(mouse.modifiers&Qt.ControlModifier){const at=ids.indexOf(nodeBox.modelData.id);if(at<0)ids.push(nodeBox.modelData.id);else ids.splice(at,1)}else if(ids.indexOf(nodeBox.modelData.id)<0)ids=[nodeBox.modelData.id];App.invoke("graph.select",{id:nodeBox.modelData.id,ids:ids});if(mouse.button===Qt.RightButton)nodeMenu.popup()}
                                onPositionChanged:mouse=>{const p=mapToItem(viewport,mouse.x,mouse.y);root.cursorWorld=root.worldPoint(p.x,p.y);if(pressed&&pressedButtons===Qt.LeftButton&&!App.readOnly&&!App.busy){const dx=(p.x-start.x)/root.zoom,dy=(p.y-start.y)/root.zoom;if(Math.hypot(dx,dy)>2){if(!moved){origins={};App.graphNodes.filter(n=>App.selectedNodes.indexOf(n.id)>=0).forEach(n=>origins[n.id]=n.position.slice())}moved=true;let next={};Object.keys(origins).forEach(id=>{next[id]=[origins[id][0]+dx,origins[id][1]+dy];root.wireRenderer.previewMove(id,next[id][0],next[id][1])});root.movement=next}}}
                                onReleased:mouse=>{if(moved){const commands=Object.keys(root.movement).map(id=>({op:"graph.node.move",args:{id:id,position:root.movement[id]}}));root.wireRenderer.clearPreview();root.movement=({});App.invoke("batch",{commands:commands})}}
                                Connections{target:root;function onCancelDragRequested(){nodeMouse.moved=false}}
                            }
                            NodeCapsule{anchors.fill:parent;nodeId:nodeBox.modelData.id;geometry:nodeBox.geometry;interactive:true;selected:App.selectedNodes.indexOf(nodeId)>=0;connections:root.connections;options:root.options;sourceKey:root.sourceKey;hoveredKey:root.hoverKey;outline:nodeBox.executionMarker==="error"?Theme.error:nodeBox.executionMarker==="next"?"#E4BD73":nodeBox.executionMarker==="last"?"#78B9F2":selected?"#9BD5FF":nodeBox.modelData.overlap?"#C69B71":"#42566B";onPortPressed:port=>root.connectPort(port,nodeBox.modelData.id)}
                            Rectangle {
                                visible:App.selectedKind==="node"&&App.selectedNodes.indexOf(nodeBox.modelData.id)>=0&&!App.readOnly
                                x:parent.width-5;y:parent.height-5;width:9;height:9;color:Theme.text;border.color:Theme.blue;z:3
                                MouseArea {
                                    id:nodeResize;objectName:"graph.resize."+nodeBox.modelData.id;anchors.fill:parent;anchors.margins:-4;cursorShape:Qt.SizeFDiagCursor;acceptedButtons:Qt.LeftButton|Qt.RightButton
                                    property point start;property point original;property var proposed:[]
                                    onPressed:m=>{if(m.button===Qt.RightButton){proposed=[];return}const p=mapToItem(viewport,m.x,m.y);start=Qt.point(p.x,p.y);original=Qt.point(nodeBox.width,nodeBox.height);proposed=[]}
                                    onPositionChanged:m=>{if(!pressed||pressedButtons!==Qt.LeftButton)return;const p=mapToItem(viewport,m.x,m.y);proposed=[Math.max(40,original.x+(p.x-start.x)/root.zoom),Math.max(32,original.y+(p.y-start.y)/root.zoom)]}
                                    onReleased:m=>{if(proposed.length){const size=proposed.slice();proposed=[];App.invoke("graph.node.patch",{id:nodeBox.modelData.id,changes:{size:size}})}}
                                    Connections{target:root;function onCancelDragRequested(){nodeResize.proposed=[]}}
                                }
                            }
                            Rectangle{visible:nodeResize.proposed.length>0;width:nodeResize.proposed[0]||0;height:nodeResize.proposed[1]||0;color:"transparent";border.color:Theme.blue;border.width:1/root.zoom;z:4}
                        }
                    }
                    Repeater {
                        model:App.readOnly?[]:root.wireEditor.handles||[]
                        Rectangle {
                            id:handle;required property var modelData;required property int index
                            property var previewPoints:[]
                            objectName:"wire.handle."+root.selectedWire+"."+modelData.index+"."+(modelData.insertion?"insert":"point")
                            x:modelData.position[0]-5/root.zoom;y:modelData.position[1]-5/root.zoom;width:10/root.zoom;height:10/root.zoom;radius:width/2;color:modelData.insertion?"#182330":"#B7DFFD";border.color:"#9BCBF3";border.width:1.4/root.zoom
                            MouseArea {
                                anchors.fill:parent;anchors.margins:-5/root.zoom;acceptedButtons:Qt.LeftButton|Qt.RightButton;cursorShape:Qt.SizeAllCursor;hoverEnabled:true
                                property point start
                                property var original:[]
                                property bool moved:false
                                onPressed:mouse=>{
                                    const p=mapToItem(viewport,mouse.x,mouse.y);start=Qt.point(p.x,p.y);original=(root.wireEditor.points||[]).map(v=>v.slice());moved=false
                                    if(mouse.button===Qt.RightButton&&!handle.modelData.insertion){pointMenu.pointIndex=handle.modelData.index;pointMenu.popup()}
                                }
                                onPositionChanged:mouse=>{
                                    if(!pressed||pressedButtons!==Qt.LeftButton)return
                                    const p=mapToItem(viewport,mouse.x,mouse.y);if(!moved&&Math.hypot(p.x-start.x,p.y-start.y)<2)return
                                    moved=true;const position=root.worldPoint(p.x,p.y),next=original.map(v=>v.slice())
                                    if(handle.modelData.insertion)next.splice(handle.modelData.index,0,[position.x,position.y]);else next[handle.modelData.index]=[position.x,position.y]
                                    handle.previewPoints=next;handle.x=position.x-handle.width/2;handle.y=position.y-handle.height/2;root.wireRenderer.previewRoute(root.selectedWire,next)
                                }
                                onReleased:mouse=>{if(moved){const id=root.selectedWire,points=handle.previewPoints;root.wireRenderer.clearPreview();App.invoke("graph.wire.patch",{id:id,changes:{points:points}})}}
                                ToolTip.visible:containsMouse;ToolTip.delay:400;ToolTip.text:handle.modelData.insertion?"拖动空心点，新增路径点":"拖动调整路径；右键删除此点"
                            }
                        }
                    }
                }
                MouseArea {
                    id:linkArea;anchors.fill:parent;z:100;enabled:!!root.linkFrom.node||!!root.placement.id||!!root.placement.plugin;hoverEnabled:true;acceptedButtons:Qt.LeftButton|Qt.RightButton
                    onPositionChanged:m=>{root.cursorWorld=root.worldPoint(m.x,m.y);root.hoverPort=root.linkFrom.node?App.hitPort(root.cursorWorld.x,root.cursorWorld.y,root.zoom):({})}
                    onClicked:m=>{viewport.forceActiveFocus();if(m.button===Qt.RightButton){root.cancelHeld();return}const point=root.worldPoint(m.x,m.y);root.cursorWorld=point;if(root.placement.id||root.placement.plugin){root.placeAt(point);return}const p=App.hitPort(point.x,point.y,root.zoom);if(p.node)root.connectPort(p,p.node);else root.cancelLink()}
                    ToolTip.visible:containsMouse&&!!root.hoverPort.node&&!(root.options[root.hoverKey]||{}).compatible;ToolTip.delay:350;ToolTip.text:root.hoverKey===root.sourceKey?"点击回到输出母体并断开连接":(root.options[root.hoverKey]||{}).reason||""
                }
                Rectangle{z:110;visible:!!root.linkFrom.node&&!(root.options[root.hoverKey]||{}).compatible;x:root.panX+root.cursorWorld.x*root.zoom-8;y:root.panY+root.cursorWorld.y*root.zoom-8;width:16;height:16;radius:8;color:root.portColors[root.linkFrom.data_type]||"#8CBEEA";border.color:"#E5F5FF";border.width:2}
                NodeCapsule {
                    z:110;visible:!!root.placement.id||!!root.placement.plugin;opacity:.65;scale:root.zoom;transformOrigin:Item.TopLeft;x:root.panX+root.cursorWorld.x*root.zoom;y:root.panY+root.cursorWorld.y*root.zoom
                    geometry:{App.project;const n=root.placement.id?App.nodeDesign(root.placement.id):({});return root.placement.id||root.placement.plugin?App.previewGeometry(root.placement.plugin||n.plugin,n.config||{}):({width:180,height:104,center:[0,0,180,104],ports:[]})}
                }
                Rectangle { id:rubber;visible:false;color:"#2266B4F4";border.color:"#66B4F4" }
                Label { anchors.centerIn:parent;visible:!App.graphNodes.length;text:"右键搜索并添加节点\n从输出端口连接到输入端口";color:"#91A7BB";horizontalAlignment:Text.AlignHCenter;lineHeight:1.5 }
                DropArea { anchors.fill:parent;keys:["application/x-corig-plugin","application/x-corig-object"];onDropped:drop=>{const p=root.worldPoint(drop.x,drop.y);if(drop.formats.indexOf("application/x-corig-object")>=0){const payload=JSON.parse(drop.getDataAsString("application/x-corig-object"));if(payload.page!=="graph")return;App.invoke("editor.place",{page:"graph",id:payload.id,position:[p.x,p.y]})}else App.invoke("graph.node.add",{plugin:drop.getDataAsString("application/x-corig-plugin"),position:[p.x,p.y]});drop.acceptProposedAction()} }
            }
            // Keep the edge draggable after the inspector reaches zero width.
            Inspector {id:inspector;page:"graph";objectName:"graph.inspector";SplitView.preferredWidth:306;SplitView.minimumWidth:0;SplitView.maximumWidth:520;clip:true }
        }
    }
    Menu {
        id:nodeMenu
        MenuItem{text:"旋转 90°";enabled:!App.readOnly;onTriggered:App.invoke("graph.node.rotate",{id:App.selected})}
        MenuItem{text:"复制";onTriggered:actions.copy()}
        MenuItem{text:"替换";enabled:!App.readOnly;onTriggered:actions.replace()}
        MenuItem{text:"撤下节点";enabled:!App.readOnly&&!App.runtime.running;onTriggered:root.edit("withdraw")}
        MenuItem{text:"删除节点";enabled:!App.readOnly&&!App.runtime.running;onTriggered:root.edit("remove")}
    }
    Menu {
        id:wireMenu
        MenuItem{text:"在此添加路径点";enabled:!App.readOnly;onTriggered:root.addWirePoint(root.selectedWire,root.cursorWorld)}
        MenuItem{text:"重置为自动路径";enabled:!App.readOnly;onTriggered:App.invoke("graph.wire.patch",{id:root.selectedWire,changes:{points:[]}})}
        MenuSeparator{}
        MenuItem{text:"平滑曲线";enabled:!App.readOnly;checkable:true;checked:root.wireEditor.routing==="curve";onTriggered:App.invoke("graph.wire.patch",{id:root.selectedWire,changes:{routing:"curve"}})}
        MenuItem{text:"折线";enabled:!App.readOnly;checkable:true;checked:root.wireEditor.routing==="polyline";onTriggered:App.invoke("graph.wire.patch",{id:root.selectedWire,changes:{routing:"polyline"}})}
        MenuSeparator{}
        MenuItem{text:"删除导线";enabled:!App.readOnly&&!App.runtime.running;onTriggered:wireDelete.open()}
    }
    Menu{id:pointMenu;property int pointIndex:-1;MenuItem{text:"删除路径点";onTriggered:{const points=(root.wireEditor.points||[]).slice();points.splice(pointMenu.pointIndex,1);App.invoke("graph.wire.patch",{id:root.selectedWire,changes:{points:points}})}}}
    Popup {
        id:quick;objectName:"graph.quickmenu";width:370;height:420;padding:10;modal:false;focus:true;closePolicy:Popup.CloseOnEscape|Popup.CloseOnPressOutside
        background:Rectangle{color:Theme.panel;radius:5;border.color:Theme.blue}
        onOpened:{search.text="";search.forceActiveFocus();results.currentIndex=0}
        ColumnLayout {
            anchors.fill:parent;spacing:8
            TextField { id:search;objectName:"graph.quickadd.search";Layout.fillWidth:true;placeholderText:"搜索名称、用途或 ID";selectByMouse:true
                // TextField owns the real input method session. Preedit text is
                // never redirected through a canvas key handler.
                Keys.onPressed:event=>{if(inputMethodComposing)return;if(event.key===Qt.Key_Down){results.currentIndex=Math.min(results.count-1,results.currentIndex+1);event.accepted=true}else if(event.key===Qt.Key_Up){results.currentIndex=Math.max(0,results.currentIndex-1);event.accepted=true}else if(event.key===Qt.Key_Return||event.key===Qt.Key_Enter){if(results.currentItem)results.currentItem.place();event.accepted=true}}
            }
            ListView {
                id:results;Layout.fillWidth:true;Layout.fillHeight:true;clip:true;currentIndex:0
                model:{App.registry;return App.catalogEntries(search.text,[],"all").filter(p=>(p.kind||"node")==="node")}
                delegate:ItemDelegate{required property var modelData;required property int index;width:results.width;height:54;highlighted:ListView.isCurrentItem
                    function place(){root.placement={plugin:modelData.id};root.cursorWorld=root.addPosition;quick.close();viewport.forceActiveFocus()}
                    contentItem:Column{spacing:3;Label{text:modelData.name;color:Theme.text}Label{text:modelData.description;color:Theme.muted;font.pixelSize:11;width:parent.width;elide:Text.ElideRight}}
                    onClicked:place()
                }
                Label{anchors.centerIn:parent;visible:!results.count;text:"没有匹配的插件";color:Theme.muted}
            }
            Label{text:"↑↓ 选择　Enter 放置　Esc 取消";color:Theme.muted;font.pixelSize:11}
        }
    }
}

