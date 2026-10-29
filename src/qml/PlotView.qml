import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import CoRig.Native 1.0

Item {
    id:root;required property var host
    property bool follow:true
    property bool fft:false
    property var range:({})
    property string exportKind:"png"
    function observe(){if(host.designMode||!host.nodeId)return;let args=Object.assign({width:Math.round(plot.width),follow:follow,fft:fft},range);App.observeView(host.nodeId,host.viewKey,args)}
    function patch(key,value){let changes={};changes[key]=value;App.invoke("node.configure",{node:host.nodeId,changes:changes,persist:!App.readOnly})}
    function call(action,args){
        if(action==="range"||action==="fft"){
            if(action==="fft")fft=args.enabled===undefined?true:args.enabled
            if(args.follow!==undefined)follow=args.follow
            if(args.fft!==undefined)fft=args.fft
            let next=Object.assign({},range)
            for(const key of ["x_min","x_max","y_min","y_max"])if(args[key]!==undefined)next[key]=args[key]
            if(args.follow===true)next=({})
            range=next;observe();return {follow:follow,fft:fft,range:range}
        }
        if(action==="export_image")return plot.exportImage(args.path,args.width||1600,args.height||900)
        return({ok:false,error:"未知曲线视图操作"})
    }
    Component.onCompleted:observe()
    onFollowChanged:observe()
    onFftChanged:observe()
    onRangeChanged:observe()
    ColumnLayout{
        anchors.fill:parent;spacing:0
        RowLayout{
            Layout.fillWidth:true;Layout.preferredHeight:32;spacing:2
            IconButton{symbol:"follow";text:root.follow?"跟随":"自由浏览";onClicked:{root.follow=!root.follow;if(root.follow)root.range=({})}}
            IconButton{symbol:"spectrum";text:root.fft?"时域":"FFT";onClicked:{root.fft=!root.fft;root.range=({})}}
            IconButton{symbol:root.host.observation.paused?"run":"pause";text:root.host.observation.paused?"恢复绘制":"暂停绘制";onClicked:root.host.action("pause_display",{value:!root.host.observation.paused})}
            IconButton{symbol:"fit";text:"复位范围";onClicked:{root.follow=true;root.range=({})}}
            Item{Layout.fillWidth:true}
            IconButton{symbol:"export";text:"导出";onClicked:exportMenu.popup()}
        }
        RowLayout{
            Layout.fillWidth:true;Layout.fillHeight:true;spacing:0
            Item {
                Layout.fillWidth:true;Layout.fillHeight:true
                NativePlot{id:plot;anchors.fill:parent;observation:root.host.observation;onWidthChanged:root.observe()}
                Repeater{model:6;Label{required property int index;x:2;y:16+(plot.height-56)*index/5-7;width:48;horizontalAlignment:Text.AlignRight;color:Theme.muted;font.pixelSize:10;font.family:"Consolas";text:(Number(root.host.observation.y_max||1)-(Number(root.host.observation.y_max||1)-Number(root.host.observation.y_min||-1))*index/5).toPrecision(3)}}
                Repeater{model:6;Label{required property int index;x:54+(plot.width-72)*index/5-25;y:plot.height-33;width:50;horizontalAlignment:Text.AlignHCenter;color:Theme.muted;font.pixelSize:10;font.family:"Consolas";text:(Number(root.host.observation.x_min||0)+(Number(root.host.observation.x_max||1)-Number(root.host.observation.x_min||0))*index/5).toPrecision(3)}}
                Label{anchors.bottom:parent.bottom;anchors.horizontalCenter:parent.horizontalCenter;color:Theme.muted;font.pixelSize:10;text:root.host.observation.x_label||"时间 / s"}
                MouseArea{
                    id:pointer;anchors.fill:parent;hoverEnabled:true;enabled:!root.host.designMode;acceptedButtons:Qt.LeftButton|Qt.RightButton
                    // Once a plot drag starts, its outer ScrollView must not
                    // steal the gesture and turn a vertical pan into scrolling.
                    preventStealing:true
                    cursorShape:pressed?Qt.ClosedHandCursor:Qt.OpenHandCursor
                    property point start
                    property var original:({})
                    onPressed:mouse=>{start=Qt.point(mouse.x,mouse.y);original=Object.assign({},root.host.observation);root.range={x_min:original.x_min,x_max:original.x_max,y_min:original.y_min,y_max:original.y_max};root.follow=false}
                    onPositionChanged:mouse=>{if(pressed){const dx=(mouse.x-start.x)/Math.max(1,width-72)*(original.x_max-original.x_min),dy=(mouse.y-start.y)/Math.max(1,height-56)*(original.y_max-original.y_min);root.range={x_min:original.x_min-dx,x_max:original.x_max-dx,y_min:original.y_min+dy,y_max:original.y_max+dy}}}
                    onWheel:wheel=>{const d=root.host.observation,f=Math.pow(1.0015,-wheel.angleDelta.y),cx=(d.x_min+d.x_max)/2,cy=(d.y_min+d.y_max)/2;root.follow=false;root.range={x_min:cx+(d.x_min-cx)*f,x_max:cx+(d.x_max-cx)*f,y_min:cy+(d.y_min-cy)*f,y_max:cy+(d.y_max-cy)*f};wheel.accepted=true}
                }
                Rectangle{x:pointer.mouseX;y:16;width:1;height:plot.height-56;color:"#648CA7";visible:pointer.containsMouse&&pointer.mouseX>=54&&pointer.mouseX<=plot.width-18}
                Label{visible:pointer.containsMouse;x:Math.min(plot.width-width-8,Math.max(54,pointer.mouseX+8));y:20;color:Theme.text;font.pixelSize:10;font.family:"Consolas";text:{const d=root.host.observation;return"x "+(Number(d.x_min||0)+(pointer.mouseX-54)/Math.max(1,plot.width-72)*(Number(d.x_max||1)-Number(d.x_min||0))).toPrecision(5)+"\ny "+(Number(d.y_max||1)-(pointer.mouseY-16)/Math.max(1,plot.height-56)*(Number(d.y_max||1)-Number(d.y_min||-1))).toPrecision(5)}}
            }
            ScrollView{
                Layout.preferredWidth:148;Layout.fillHeight:true;clip:true
                Column{
                    width:142
                    Repeater{
                        model:root.host.observation.channels||[]
                        Row{
                            required property var modelData;width:142;height:32;spacing:4
                            CheckBox{width:28;height:30;checked:modelData.visible;onClicked:{let map=Object.assign({},root.host.config.visibility||{});map[modelData.name]=checked;root.patch("visibility",map)}}
                            Rectangle{width:9;height:9;radius:2;color:modelData.color;anchors.verticalCenter:parent.verticalCenter;TapHandler{onTapped:{colorPicker.channel=modelData.name;colorPicker.selectedColor=modelData.color;colorPicker.open()}}}
                            Label{width:98;text:modelData.name;color:Theme.text;elide:Text.ElideRight;anchors.verticalCenter:parent.verticalCenter;ToolTip.visible:hover.hovered;ToolTip.text:modelData.name;HoverHandler{id:hover}}
                        }
                    }
                }
            }
        }
    }
    Menu{id:exportMenu;MenuItem{text:"原始数据 CSV…";onTriggered:{root.exportKind="csv";exportFile.nameFilters=["CSV (*.csv)"];exportFile.defaultSuffix="csv";exportFile.open()}}MenuItem{text:"图片 PNG…";onTriggered:{root.exportKind="png";exportFile.nameFilters=["PNG (*.png)"];exportFile.defaultSuffix="png";exportFile.open()}}MenuItem{text:"矢量图 SVG…";onTriggered:{root.exportKind="svg";exportFile.nameFilters=["SVG (*.svg)"];exportFile.defaultSuffix="svg";exportFile.open()}}MenuSeparator{}MenuItem{text:"清空曲线";onTriggered:root.host.action("clear",{})}}
    FileDialog{id:exportFile;title:"导出曲线";fileMode:FileDialog.SaveFile;onAccepted:{const path=decodeURIComponent(selectedFile.toString().replace(/^file:\/\/\//,""));if(root.exportKind==="csv")root.host.action("export_csv",{path:path});else App.invoke("view.call",{node:root.host.nodeId,view:root.host.viewKey,action:"export_image",args:{path:path}})}}
    ColorDialog{id:colorPicker;property string channel:"";title:"选择曲线颜色";onAccepted:{let colors=Object.assign({},root.host.config.colors||{});colors[channel]=selectedColor.toString();root.patch("colors",colors)}}
}

