import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id:root
    property string page:"graph"
    property var selection:[]
    property var rows: {
        const project=App.project,result=[]
        if(page==="graph") {
            const all=project.nodes||[]
            result.push({heading:true,title:"已放置"});all.filter(n=>n.placed!==false).forEach(n=>result.push({id:n.id,title:n.title||n.plugin,pending:false,depth:0,index:all.indexOf(n)}))
            result.push({heading:true,title:"待放置"});all.filter(n=>n.placed===false).forEach(n=>result.push({id:n.id,title:n.title||n.plugin,pending:true,depth:0,index:all.indexOf(n)}))
        } else {
            const all=(project.ui||{}).items||[],records={};all.forEach(i=>records[i.id]=i)
            function visit(id,depth,pending,index){const r=records[id];if(!r)return;result.push({id:id,title:(r.properties||{}).title||(r.binding||{}).title||r.view||({"layout.row":"横向容器","layout.column":"纵向容器","layout.grid":"网格容器","layout.tabs":"分页容器","layout.canvas":"设计画布"})[r.plugin]||id,depth:depth,pending:pending,index:index});(r.children||[]).filter(c=>c!=="pending").forEach((c,i)=>visit(c,depth+1,pending,i))}
            result.push({heading:true,title:"已放置 / 图层上方优先"});visit((project.ui||{}).root||"root",0,false,0)
            result.push({heading:true,title:"待放置"});((records.pending||{}).children||[]).forEach((c,i)=>visit(c,0,true,i))
        }
        return result
    }
    signal picked(string id,int modifiers)
    signal placementRequested(string id)
    signal editRequested(string operation)
    color:Theme.panel;clip:true
    ColumnLayout {
        anchors.fill:parent;spacing:0
        RowLayout {
            Layout.fillWidth:true;Layout.preferredHeight:38;spacing:1
            Label{Layout.leftMargin:10;Layout.fillWidth:true;text:"对象";color:Theme.text;font.bold:true}
            IconButton{symbol:"copy";text:"复制";enabled:root.selection.length>0;onClicked:root.editRequested("copy")}
            IconButton{symbol:"replace";text:"替换";enabled:root.selection.length>0&&!App.readOnly;onClicked:root.editRequested("replace")}
            IconButton{symbol:"trash";text:"删除对象";enabled:root.selection.length>0&&!App.readOnly;onClicked:root.editRequested("remove")}
        }
        ListView {
            id:list;Layout.fillWidth:true;Layout.fillHeight:true;model:root.rows;clip:true;spacing:1
            ScrollBar.vertical:ScrollBar{}
            delegate:Rectangle {
                id:row;required property var modelData
                width:list.width;height:modelData.heading?32:36
                color:modelData.heading?Theme.raised:root.selection.indexOf(modelData.id)>=0?(Theme.dark?"#29465E":"#C6E0F2"):rowMouse.containsMouse?Theme.raised:"transparent"
                Label{x:10+(row.modelData.depth||0)*12;anchors.verticalCenter:parent.verticalCenter;width:Math.max(0,parent.width-x-(row.modelData.heading?6:80));text:row.modelData.title;color:row.modelData.heading?Theme.muted:Theme.text;font.pixelSize:row.modelData.heading?11:12;elide:Text.ElideRight}
                MouseArea {
                    id:rowMouse;anchors.fill:parent;hoverEnabled:true;enabled:!row.modelData.heading;acceptedButtons:Qt.LeftButton|Qt.RightButton
                    property point start;property bool dragged:false
                    onPressed:m=>{start=Qt.point(m.x,m.y);dragged=false;root.picked(row.modelData.id,m.modifiers)}
                    onPositionChanged:m=>{if(pressed&&!dragged&&!App.readOnly&&Math.hypot(m.x-start.x,m.y-start.y)>10){dragged=true;App.dragObject(root.page,row.modelData.id)}}
                    onDoubleClicked:if(row.modelData.pending)root.placementRequested(row.modelData.id)
                    ToolTip.visible:containsMouse;ToolTip.delay:500;ToolTip.text:row.modelData.title+(row.modelData.pending?"\n拖到画布或双击开始放置":"\n拖动可调整层序")
                }
                Row {
                    anchors.right:parent.right;anchors.verticalCenter:parent.verticalCenter
                    visible:!row.modelData.heading&&row.modelData.id!=="root"
                    IconButton{width:24;height:28;symbol:row.modelData.pending?"plus":"up";text:row.modelData.pending?"放置":"上移一层";enabled:!App.readOnly;onClicked:{if(row.modelData.pending)root.placementRequested(row.modelData.id);else App.invoke("editor.reorder",{page:root.page,id:row.modelData.id,index:Math.max(0,row.modelData.index-1)})}}
                    IconButton{width:24;height:28;symbol:"down";text:"下移一层";enabled:!App.readOnly;onClicked:App.invoke("editor.reorder",{page:root.page,id:row.modelData.id,index:row.modelData.index+1})}
                    IconButton{width:24;height:28;visible:!row.modelData.pending;symbol:"withdraw";text:"撤下";enabled:!App.readOnly;onClicked:{root.picked(row.modelData.id,0);root.editRequested("withdraw")}}
                }
                DropArea {
                    anchors.fill:parent;enabled:!row.modelData.heading;keys:["application/x-comtoolx-object"]
                    onEntered:drag=>{const payload=JSON.parse(drag.getDataAsString("application/x-comtoolx-object"));drag.accepted=payload.page===root.page;layerMark.visible=drag.accepted}
                    onExited:layerMark.visible=false
                    onDropped:drop=>{layerMark.visible=false;const payload=JSON.parse(drop.getDataAsString("application/x-comtoolx-object"));if(payload.page===root.page){App.invoke("editor.reorder",{page:root.page,id:payload.id,index:row.modelData.index});drop.acceptProposedAction()}}
                }
                Rectangle{id:layerMark;visible:false;anchors.top:parent.top;width:parent.width;height:2;color:Theme.blue}
            }
        }
    }
}
