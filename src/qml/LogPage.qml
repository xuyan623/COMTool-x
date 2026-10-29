import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id:root;anchors.fill:parent;objectName:"page.logs"
    Rectangle{anchors.fill:parent;color:Theme.panel;z:-1}
    property string requestJob:""
    property double cursor:0
    property bool follow:true
    property double retainedCount:0
    property double retainedBytes:0
    function reset(){requestJob="";records.clear();cursor=0;fetch()}
    function filters(){return{node:nodeFilter.currentValue||"",level:severity.currentValue||"",text:query.text,batch:batchFilter.value}}
    function fetch(){if(!visible||requestJob.length)return;requestJob=App.invoke("log.query",Object.assign({after:cursor,limit:300},filters())).job||""}
    ListModel{id:records}
    Timer{interval:200;running:root.visible;repeat:true;onTriggered:root.fetch()}
    Connections {
        target:App
        function onJobFinished(id){
            if(id!==root.requestJob)return;root.requestJob="";const job=App.job(id);if(job.state!=="complete")return;const result=job.result||{}
            root.retainedCount=result.count||0;root.retainedBytes=result.retained_bytes||0
            if(result.gap)records.append({level:"warning",time:"",node:"",batch:"",kind:"gap",message:"较早日志已超出保留范围",details:""})
            ;(result.records||[]).forEach(r=>{records.append({level:r.level,time:r.time,node:r.node||"",batch:String(r.batch||""),kind:r.kind||"",message:r.message,details:JSON.stringify(r,null,2)});root.cursor=Math.max(root.cursor,r.cursor)})
            while(records.count>2000)records.remove(0)
            if((result.records||[]).length<300)root.cursor=Math.max(root.cursor,result.latest_cursor||0)
            if(root.follow)list.positionViewAtEnd()
        }
    }
    ColumnLayout{
        anchors.fill:parent;spacing:0
        RowLayout{
            Layout.fillWidth:true;Layout.preferredHeight:44;spacing:7
            TextField{id:query;objectName:"logs.search";Layout.leftMargin:10;Layout.fillWidth:true;placeholderText:"搜索日志内容";onAccepted:root.reset()}
            ComboBox{id:nodeFilter;objectName:"logs.filter.node";Layout.preferredWidth:180;model:[{id:"",name:"全部节点"}].concat((App.project.nodes||[]).map(n=>({id:n.id,name:n.title||n.id})));textRole:"name";valueRole:"id";onActivated:root.reset()}
            ComboBox{id:severity;objectName:"logs.filter.level";model:[{id:"",name:"全部级别"},{id:"debug",name:"详细执行"},{id:"info",name:"关键事件"},{id:"warning",name:"警告"},{id:"error",name:"错误"}];textRole:"name";valueRole:"id";onActivated:root.reset()}
            SpinBox{id:batchFilter;objectName:"logs.filter.batch";Layout.preferredWidth:140;from:0;to:2147483647;editable:true;textFromValue:(value,locale)=>value?"批次 "+value:"全部批次";valueFromText:(text,locale)=>Number(text.replace(/[^0-9]/g,""))||0;onValueModified:root.reset()}
        }
        RowLayout{
            Layout.fillWidth:true;Layout.preferredHeight:36
            CheckBox{objectName:"logs.follow";Layout.leftMargin:8;text:"自动跟随";checked:root.follow;onClicked:root.follow=checked}
            CheckBox{objectName:"logs.detail";text:"普通运行也记录详细数据";checked:!!App.runtime.log_detail;onClicked:App.invoke("log.configure",{detail:checked})}
            Item{Layout.fillWidth:true}
            IconButton{symbol:"export";objectName:"logs.export";text:"导出日志…";onClicked:exportFile.open()}
            IconButton{symbol:"trash";objectName:"logs.clear";text:"清空";Layout.rightMargin:8;onClicked:{App.invoke("log.clear",{});records.clear();root.cursor=0}}
        }
        Rectangle{Layout.fillWidth:true;height:1;color:Theme.border}
        ListView{
            id:list;Layout.fillWidth:true;Layout.fillHeight:true;clip:true;model:records;spacing:1
            delegate:Rectangle{
                id:entry;required property string level;required property string time;required property string node;required property string batch;required property string kind;required property string message;required property string details
                property bool expanded:false
                width:list.width;height:expanded?header.height+detail.implicitHeight+20:header.height
                color:level==="error"?(Theme.dark?"#342527":"#FFE8E7"):Theme.panel
                RowLayout{
                    id:header;anchors.left:parent.left;anchors.right:parent.right;height:38;spacing:12
                    Label{Layout.leftMargin:12;Layout.preferredWidth:92;text:entry.time.slice(11,23);font.family:"Consolas";font.pixelSize:11;color:Theme.muted}
                    Label{Layout.preferredWidth:62;text:entry.level;color:entry.level==="error"?Theme.error:entry.level==="warning"?"#DCA870":Theme.muted;font.pixelSize:11}
                    Label{Layout.preferredWidth:60;text:entry.batch?"#"+entry.batch:"";color:Theme.muted;font.pixelSize:11}
                    Label{Layout.preferredWidth:180;text:entry.node;elide:Text.ElideMiddle;color:Theme.blue;font.pixelSize:11}
                    Label{Layout.fillWidth:true;Layout.rightMargin:12;text:entry.message;elide:Text.ElideRight;color:Theme.text}
                }
                TextArea{id:detail;x:20;y:header.height;width:parent.width-40;visible:entry.expanded;text:entry.details;readOnly:true;selectByMouse:true;wrapMode:Text.WrapAnywhere;font.family:"Consolas";font.pixelSize:11;background:null}
                MouseArea{height:header.height;width:parent.width;onClicked:entry.expanded=!entry.expanded;onDoubleClicked:if(entry.node){App.workspaces.open("graph");App.invoke("graph.select",{id:entry.node});App.graphFocusRequested(entry.node)}}
            }
            Label{anchors.centerIn:parent;visible:!records.count;text:"暂无匹配日志";color:Theme.muted}
            ScrollBar.vertical:ScrollBar{}
        }
        Label{Layout.fillWidth:true;Layout.margins:10;text:"保留 "+root.retainedCount+" 条 / "+(root.retainedBytes/1048576).toFixed(2)+" MiB，显示最近 "+records.count+" 条；单击展开输入、输出与执行耗时，双击定位节点";color:Theme.muted;font.pixelSize:11}
    }
    FileDialog{id:exportFile;title:"导出日志";fileMode:FileDialog.SaveFile;defaultSuffix:"jsonl";nameFilters:["JSON Lines (*.jsonl)"];onAccepted:App.invoke("log.export",Object.assign({path:selectedFile.toString()},root.filters()))}
}
