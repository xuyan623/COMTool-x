import QtQuick
import QtQuick.Controls

// Custom workspaces share the same tab/window lifecycle and property session.
SplitView {
    id:root
    required property string plugin
    property var bridge:App.propertyBridge("workspace",plugin)
    objectName:"page."+plugin
    handle:Rectangle{implicitWidth:6;color:SplitHandle.hovered?Theme.border:Theme.canvas}
    Loader {
        id:content; SplitView.fillWidth:true; SplitView.minimumWidth:100
        function load() {
            const spec=App.definition(root.plugin)
            setSource(App.pluginResourceUrl(root.plugin,spec.qml),{bridge:root.bridge})
        }
        Component.onCompleted:load()
        Label{anchors.centerIn:parent;visible:content.status===Loader.Error;text:"工作区加载失败，请检查插件 QML。";color:Theme.error}
    }
    Inspector { workspace:root.plugin; SplitView.preferredWidth:306; SplitView.minimumWidth:0; SplitView.maximumWidth:520 }
}
