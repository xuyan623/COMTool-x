import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One shell handles public-command confirmations for all detached windows.
// Applying drafts waits for the real job before resuming the navigation.
Item {
    id: root
    property string hostId: ""
    property var continuation: null
    property var drafts: []
    property bool applying: false
    property var waitingBridge: null
    property string error: ""
    function ownsPrompt() {
        const windows=App.workspaces.windows
        const host=windows.find(window=>window.focused)||windows.find(window=>!window.child)||windows[0]
        return host&&host.id===hostId
    }
    function guard(next) {
        const pending = App.invoke("property.drafts", {})
        if (!pending.length) { next(); return }
        continuation = next; drafts = pending; error = ""; applying = false; draftDialog.open()
    }
    function advance() {
        if (!applying || portDialog.opened) return
        drafts = App.invoke("property.drafts", {})
        if (!drafts.length) {
            const next = continuation; continuation = null; applying = false; waitingBridge = null
            draftDialog.close(); if(next)next()
            return
        }
        const current = App.propertyBridge(drafts[0].scope, drafts[0].target)
        if (current.pending) { waitingBridge = current; return }
        const reply = current.commit()
        if (reply.error) { error = reply.error; applying = false }
        else if (reply.requires_confirmation) waitingBridge = null
        else if (current.pending) waitingBridge = current
        else Qt.callLater(advance)
    }
    Connections {
        target: root.waitingBridge
        function onChanged() {
            if (!root.applying || !root.waitingBridge || root.waitingBridge.pending) return
            const message = root.waitingBridge.error
            root.waitingBridge = null
            if(message){root.error=message;root.applying=false}
            else Qt.callLater(root.advance)
        }
    }
    Connections {
        target: App
        function onDraftResolutionRequested(operation, arguments, drafts) {
            if(root.ownsPrompt())root.guard(()=>App.invoke(operation,arguments))
        }
        function onConfigurationConfirmationRequested(operation, arguments, plan) {
            if(!root.ownsPrompt())return
            portDialog.operation=operation;portDialog.arguments=arguments;portDialog.plan=plan;portDialog.open()
        }
    }
    Dialog {
        id:draftDialog; objectName:"property.leave"; title:"属性有未应用的修改"; modal:true
        anchors.centerIn:parent; width:440
        closePolicy:Popup.CloseOnEscape
        onRejected:{root.applying=false;root.continuation=null;root.waitingBridge=null}
        ColumnLayout {
            width:parent.width; spacing:12
            Label { text:"先处理当前草稿，再继续离开。"; color:Theme.text; Layout.fillWidth:true; wrapMode:Text.WordWrap }
            Label { text:root.drafts.map(row=>row.target).join("\n"); color:Theme.muted; Layout.fillWidth:true; wrapMode:Text.WrapAnywhere }
            Label { visible:!!root.error; text:root.error; color:Theme.error; Layout.fillWidth:true; wrapMode:Text.WordWrap }
        }
        footer:DialogButtonBox {
            ActionButton { objectName:"property.leave.apply"; text:root.applying?"正在应用…":"应用"; enabled:!root.applying; onClicked:{root.error="";root.applying=true;root.advance()} }
            ActionButton {
                objectName:"property.leave.discard"; text:"放弃"; enabled:!root.applying
                onClicked: {
                    for(const row of root.drafts) {
                        const bridge=App.propertyBridge(row.scope,row.target)
                        if(bridge.pending){root.error="请等待当前属性提交完成";return}
                    }
                    for(const row of root.drafts)App.invoke("property.discard",{scope:row.scope,target:row.target})
                    const next=root.continuation;root.continuation=null;draftDialog.close();if(next)next()
                }
            }
            ActionButton { objectName:"property.leave.cancel"; text:"取消"; onClicked:draftDialog.reject() }
        }
    }
    Dialog {
        id:portDialog; objectName:"property.ports"; title:"端口变化将断开连线"; modal:true
        anchors.centerIn:parent; width:520; closePolicy:Popup.CloseOnEscape
        property string operation:""
        property var arguments:({})
        property var plan:({})
        // Cancelling a pending draft application must also detach the old
        // continuation; otherwise its later signal could resume navigation.
        onRejected:if(root.applying){root.applying=false;root.continuation=null;root.waitingBridge=null;root.error="已保留草稿和连线"}
        ColumnLayout {
            width:parent.width; spacing:12
            Label { text:"以下连线将随属性修改一起移除，撤销时一起恢复。"; color:Theme.text; Layout.fillWidth:true; wrapMode:Text.WordWrap }
            Label { text:(portDialog.plan.affected_wires||[]).map(w=>w.id+"    "+w.source.join("/")+" → "+w.target.join("/")).join("\n"); color:Theme.muted; Layout.fillWidth:true; wrapMode:Text.WrapAnywhere }
        }
        footer:DialogButtonBox {
            ActionButton {
                objectName:"property.ports.apply"; text:"应用并断开"
                onClicked: {
                    let args=Object.assign({},portDialog.arguments,{confirmed:true,revision:portDialog.plan.revision})
                    portDialog.close()
                    const reply=App.invoke(portDialog.operation,args)
                    if(root.applying) {
                        if(reply.error){root.error=reply.error;root.applying=false}
                        else if(!reply.requires_confirmation) {
                            if(args.scope)root.waitingBridge=App.propertyBridge(args.scope,args.target)
                            if(!root.waitingBridge||!root.waitingBridge.pending)Qt.callLater(root.advance)
                        }
                    }
                }
            }
            ActionButton { objectName:"property.ports.cancel"; text:"取消"; onClicked:portDialog.reject() }
        }
    }
}
