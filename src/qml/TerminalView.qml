import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id:root;required property var host
    property var screen:host.observation
    function send(text){if(text.length&&!host.designMode)host.action("send",{text:text})}
    function paste(){const result=App.invoke("clipboard.read",{});if(result.text)send(result.text)}
    function resize(){if(!visible||host.designMode)return;const columns=Math.max(2,Math.min(500,Math.floor((display.width-12)/metrics.advanceWidth("M")))),rows=Math.max(2,Math.min(300,Math.floor((display.height-10)/metrics.lineSpacing)));if(columns!==screen.columns||rows!==screen.rows)host.action("resize",{columns:columns,rows:rows})}
    FontMetrics{id:metrics;font.family:"Consolas";font.pixelSize:14}
    Timer{id:resizeTimer;interval:160;onTriggered:root.resize()}
    ColumnLayout{
        anchors.fill:parent;spacing:0
        RowLayout{
            Layout.fillWidth:true;Layout.preferredHeight:30
            IconButton{symbol:"previous";text:"上一页";onClicked:root.host.action("previous_page",{})}
            IconButton{symbol:"next";text:"下一页";onClicked:root.host.action("next_page",{})}
            IconButton{symbol:"copy";text:"复制";onClicked:terminal.copy()}
            IconButton{symbol:"paste";text:"粘贴";enabled:!root.host.designMode&&!!App.runtime.active;onClicked:root.paste()}
            Item{Layout.fillWidth:true}
            Label{text:root.screen.title||((root.screen.columns||80)+" × "+(root.screen.rows||24));color:Theme.muted;font.pixelSize:11;elide:Text.ElideRight;Layout.maximumWidth:240}
            IconButton{symbol:"trash";text:"清屏";onClicked:root.host.action("clear",{})}
        }
        Rectangle{
            id:display;Layout.fillWidth:true;Layout.fillHeight:true;clip:true;color:"#111923"
            onWidthChanged:resizeTimer.restart()
            onHeightChanged:resizeTimer.restart()
            TextEdit{
                id:terminal;x:6;y:4;width:Math.max(parent.width,implicitWidth);readOnly:true;selectByMouse:true;wrapMode:TextEdit.NoWrap;textFormat:TextEdit.RichText;color:"#DAE5EF";font.family:"Consolas";font.pixelSize:14
                text:"<pre style='margin:0;font-family:Consolas;font-size:14px'>"+(root.screen.html||"")+"</pre>"
            }
            Rectangle{
                x:6+((root.screen.cursor||[0,0])[0])*metrics.advanceWidth("M");y:4+((root.screen.cursor||[0,0])[1])*metrics.lineSpacing
                width:metrics.advanceWidth("M");height:metrics.lineSpacing;color:"#5488A6";opacity:.55
                visible:keyboard.activeFocus&&!root.host.designMode&&(root.screen.page_offset||0)===0
            }
            TextInput{
                id:keyboard;objectName:"terminal."+root.host.nodeId+".input"
                x:6+((root.screen.cursor||[0,0])[0])*metrics.advanceWidth("M");y:4+((root.screen.cursor||[0,0])[1])*metrics.lineSpacing
                width:Math.max(10,display.width-x);height:metrics.lineSpacing
                color:"transparent";selectionColor:"transparent";selectedTextColor:"transparent";cursorVisible:false
                inputMethodHints:Qt.ImhNoPredictiveText|Qt.ImhNoAutoUppercase
                enabled:!root.host.designMode&&!!App.runtime.active
                onTextEdited:if(text.length&&!inputMethodComposing){root.send(text);text=""}
                Keys.onPressed:event=>{
                    if(inputMethodComposing)return
                    if(event.modifiers&Qt.ControlModifier&&event.modifiers&Qt.ShiftModifier&&event.key===Qt.Key_C){terminal.copy();event.accepted=true;return}
                    if(event.modifiers&Qt.ControlModifier&&(event.key===Qt.Key_V)){root.paste();event.accepted=true;return}
                    const keys={};keys[Qt.Key_Return]="\r";keys[Qt.Key_Enter]="\r";keys[Qt.Key_Backspace]="\x7f";keys[Qt.Key_Tab]="\t";keys[Qt.Key_Escape]="\x1b";keys[Qt.Key_Up]="\x1b[A";keys[Qt.Key_Down]="\x1b[B";keys[Qt.Key_Right]="\x1b[C";keys[Qt.Key_Left]="\x1b[D";keys[Qt.Key_Home]="\x1b[H";keys[Qt.Key_End]="\x1b[F";keys[Qt.Key_Delete]="\x1b[3~";keys[Qt.Key_PageUp]="\x1b[5~";keys[Qt.Key_PageDown]="\x1b[6~"
                    if(keys[event.key]){root.send(keys[event.key]);event.accepted=true}
                    else if(event.modifiers&Qt.ControlModifier&&event.key>=Qt.Key_A&&event.key<=Qt.Key_Z){root.send(String.fromCharCode(event.key-Qt.Key_A+1));event.accepted=true}
                }
            }
            TapHandler{acceptedButtons:Qt.LeftButton;onTapped:keyboard.forceActiveFocus()}
            TapHandler{acceptedButtons:Qt.RightButton;onTapped:terminalMenu.popup()}
            WheelHandler{onWheel:event=>{root.host.action(event.angleDelta.y>0?"previous_page":"next_page",{});event.accepted=true}}
        }
    }
    Menu{id:terminalMenu;MenuItem{text:"复制选择";onTriggered:terminal.copy()}MenuItem{text:"粘贴";enabled:!!App.runtime.active;onTriggered:root.paste()}MenuItem{text:"清屏";onTriggered:root.host.action("clear",{})}}
}

