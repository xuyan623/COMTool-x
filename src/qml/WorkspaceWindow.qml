import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: win
    required property string hostId
    property bool childWindow:false
    property bool pinned:false
    property var tabs: []
    property string activePage: ""
    property string pendingAction: ""
    property string savingJob: ""
    objectName: "window." + hostId
    width:childWindow?1000:1420;height:childWindow?700:900;minimumWidth:childWindow?540:800;minimumHeight:childWindow?320:540
    visible: false
    color: Theme.panel
    title: (App.dirty ? "● " : "") + (App.project.name || "未命名工程") + " — 联调 · CoRig"
    font.family: "Microsoft YaHei UI"
    font.pixelSize: 13
    palette.window: Theme.panel
    palette.windowText: Theme.text
    palette.base: Theme.dark ? "#111923" : "#FFFFFF"
    palette.alternateBase: Theme.raised
    palette.text: Theme.text
    palette.button: Theme.raised
    palette.buttonText: Theme.text
    palette.highlight: Theme.blue
    palette.highlightedText: Theme.dark ? "#111923" : "#FFFFFF"
    palette.mid: Theme.border
    palette.dark: Theme.border
    palette.light: Theme.raised

    function requestAction(action) {
        pendingAction = action
        if (App.dirty) unsaved.open()
        else continueAction()
    }
    function continueAction() {
        let action = pendingAction; pendingAction = ""
        if (action === "open") openProject.open()
        else if (action === "new") App.invoke("project.new", {discard:true})
        else if (action === "close") App.workspaces.closeWindow(hostId, true)
    }
    function save() {
        if (App.projectPath.length) savingJob = App.invoke("project.save", {}).job || ""
        else projectSaveOptions.present("save")
    }
    onClosing: close => { close.accepted = App.workspaces.closeWindow(hostId, false) }
    Connections {
        target: App
        function onExitConfirmation(window) { if (window === hostId) { pendingAction = "close"; unsaved.open() } }
        function onJobFinished(id) { if (id === savingJob) { savingJob = ""; if (App.job(id).state === "complete" && pendingAction.length) continueAction() } }
        function onPluginSettingsRequested(){if(!win.childWindow)pluginSettings.open()}
        function onPluginOpenRequested(){if(!win.childWindow)pluginVersions.present()}
        function onPluginExportRequested(scope,plugin){if(!win.childWindow)pluginExporter.present(scope,plugin)}
    }
    Shortcut { sequence: "Ctrl+Tab"; onActivated: App.workspaces.cycle(hostId, 1) }
    Shortcut { sequence: "Ctrl+Shift+Tab"; onActivated: App.workspaces.cycle(hostId, -1) }
    Shortcut { sequence: "F5"; onActivated: App.invoke("runtime.start",{debug:true}) }
    Shortcut { sequence: "Ctrl+F5"; onActivated: App.invoke("runtime.start",{debug:false}) }
    Shortcut { sequence: "F6"; onActivated: App.invoke("runtime.pause",{}) }
    Shortcut { sequence: "F10"; enabled:!!App.runtime.debug&&!App.runtime.running;onActivated:App.invoke("runtime.step",{mode:"batch"}) }
    Shortcut { sequence: "F11"; enabled:!!App.runtime.debug&&!App.runtime.running;onActivated:App.invoke("runtime.step",{mode:"node"}) }
    Shortcut { sequence: "Ctrl+Shift+F5";onActivated:App.invoke("runtime.reset",{}) }
    Shortcut { sequence: "Shift+F5"; onActivated: App.invoke("runtime.stop", {}) }
    menuBar: MenuBar {
        visible:!win.childWindow
        height:visible?implicitHeight:0
        enabled:visible
        Menu {
            title: "文件"
            Action { text: "新建工程"; shortcut: StandardKey.New; onTriggered: win.requestAction("new") }
            Action { text: "打开工程…"; shortcut: StandardKey.Open; onTriggered: win.requestAction("open") }
            Action { text: "保存"; shortcut: StandardKey.Save; onTriggered: win.save() }
            Action { text: "另存为…"; onTriggered: projectSaveOptions.present("save") }
            Action { text:"导出工程…";onTriggered:projectSaveOptions.present("export") }
            Action { text:"工程保存选项…";onTriggered:projectSaveOptions.present("options") }
            Action { text:"导出工程插件…";onTriggered:pluginExporter.present("project","") }
            MenuSeparator {}
            Action { text: "关闭窗口"; onTriggered: win.requestAction("close") }
        }
        Menu {
            title: "编辑"
            Action { text: "撤销"; shortcut: StandardKey.Undo; enabled: !App.readOnly; onTriggered: App.invoke("history.undo", {}) }
            Action { text: "重做"; shortcut: StandardKey.Redo; enabled: !App.readOnly; onTriggered: App.invoke("history.redo", {}) }
            MenuSeparator {}
            Action { text: App.readOnly ? "进入编辑模式" : "设为只读"; onTriggered: App.invoke("preferences.set", {read_only: !App.readOnly}) }
        }
        Menu {
            title: "工作区"
            Repeater {
                model: [{key:"graph", title:"功能设计"}, {key:"layout", title:"界面设计"}, {key:"runtime", title:"运行界面"}, {key:"logs", title:"日志"}, {key:"library", title:"插件库"}]
                MenuItem { required property var modelData; text: modelData.title; onTriggered: App.workspaces.open(modelData.key, hostId) }
            }
        }
        Menu {
            title: "设置"
            Action { text:"插件管理…";onTriggered:pluginSettings.open() }
            Action { text: "本地 Python 环境…"; onTriggered: { pythonInput.text = App.pythonPath; pythonSettings.open() } }
            Action { text: Theme.dark ? "切换浅色主题" : "切换深色主题"; onTriggered: App.invoke("preferences.set", {theme: Theme.dark ? "light" : "dark"}) }
        }
        Menu {
            title: "帮助"
            Action { text: "关于与本地接口"; onTriggered: about.open() }
        }
    }
    // Workspace navigation stays above the page; shared engine controls stay
    // below it. The library has a compact window header and no workspace tab.
    ColumnLayout {
        anchors.fill:parent;spacing:0
        Rectangle {
            objectName:"workspace.tabs."+win.hostId
            Layout.fillWidth:true;Layout.preferredHeight:38;color:Theme.dark?"#101924":"#DCE5ED"
            Row {
                id:tabStrip;spacing:1;visible:win.activePage!=="library"
                Repeater {
                    model:win.tabs
                    Rectangle {
                        id:tab;required property var modelData;required property int index
                        objectName:"workspace.tab."+modelData.key;width:140;height:38
                        color:modelData.key===win.activePage?Theme.panel:hover.containsMouse?Theme.raised:"transparent"
                        Rectangle{anchors.bottom:parent.bottom;width:parent.width;height:2;color:Theme.blue;visible:tab.modelData.key===win.activePage}
                        Image{x:10;anchors.verticalCenter:parent.verticalCenter;width:18;height:18;source:"image://icons/"+tab.modelData.key+"/"+encodeURIComponent((tab.modelData.key===win.activePage?Theme.text:Theme.muted).toString());sourceSize.width:36;sourceSize.height:36}
                        Label{x:35;width:72;anchors.verticalCenter:parent.verticalCenter;text:tab.modelData.title;elide:Text.ElideRight;color:tab.modelData.key===win.activePage?Theme.text:Theme.muted}
                        MouseArea {
                            id:hover;anchors.fill:parent;hoverEnabled:true;acceptedButtons:Qt.LeftButton|Qt.RightButton
                            property point start;property bool dragging:false
                            onPressed:m=>{start=Qt.point(m.x,m.y);dragging=false;if(m.button===Qt.RightButton){tabMenu.key=tab.modelData.key;tabMenu.popup()}}
                            onClicked:m=>{if(m.button===Qt.LeftButton&&!dragging)App.workspaces.open(tab.modelData.key,win.hostId)}
                            onPositionChanged:m=>{if(pressed&&!dragging&&Math.hypot(m.x-start.x,m.y-start.y)>10){dragging=true;App.workspaces.dragPage(tab.modelData.key)}}
                        }
                        IconButton{anchors.right:parent.right;anchors.verticalCenter:parent.verticalCenter;symbol:"close";text:"关闭标签";width:28;height:28;onClicked:App.workspaces.closePage(tab.modelData.key)}
                        DropArea{anchors.fill:parent;keys:["application/x-corig-workspace"];onEntered:mark.visible=true;onExited:mark.visible=false;onDropped:d=>{mark.visible=false;App.workspaces.acceptDrop(d.getDataAsString("application/x-corig-workspace"),win.hostId,tab.index);d.acceptProposedAction()}}
                        Rectangle{id:mark;visible:false;width:3;height:parent.height;color:Theme.blue}
                    }
                }
                IconButton{objectName:"workspace.add";symbol:"plus";text:"打开工作区";visible:!win.childWindow;onClicked:pagesMenu.popup()}
            }
            Label{visible:win.activePage==="library";anchors.left:parent.left;anchors.leftMargin:12;anchors.verticalCenter:parent.verticalCenter;text:"插件库 · "+(App.libraryTarget==="layout"?"界面设计":"功能设计");color:Theme.text}
            Row {
                id:childTools;visible:win.childWindow;anchors.right:parent.right;height:parent.height
                IconButton{objectName:"window.pin."+win.hostId;symbol:"pin";ink:win.pinned?Theme.blue:Theme.text;text:win.pinned?"取消置顶":"固定在顶部";height:36;checkable:true;checked:win.pinned;onClicked:App.workspaces.pinWindow(win.hostId,checked)}
                IconButton{symbol:"dock";text:"定位主窗口";height:36;onClicked:App.workspaces.focusMain()}
            }
            DropArea{visible:win.activePage!=="library";x:tabStrip.width;width:Math.max(0,(win.childWindow?childTools.x:parent.width)-x);height:parent.height;keys:["application/x-corig-workspace"];onDropped:d=>{App.workspaces.acceptDrop(d.getDataAsString("application/x-corig-workspace"),win.hostId,-1);d.acceptProposedAction()}}
        }
        Item {
            objectName:"workspace.content";Layout.fillWidth:true;Layout.fillHeight:true;clip:true
            Label{anchors.centerIn:parent;visible:!win.activePage.length;text:"工作区已移至子窗口。\n从“工作区”菜单打开或定位页面。";color:Theme.muted;horizontalAlignment:Text.AlignHCenter;lineHeight:1.5}
        }
        Rectangle{visible:!win.childWindow;Layout.fillWidth:true;height:visible?1:0;color:Theme.border}
        Rectangle {
            visible:!win.childWindow;Layout.fillWidth:true;Layout.preferredHeight:visible?42:0;color:Theme.panel
            RowLayout{objectName:"execution.toolbar";anchors.fill:parent;spacing:4;DebugControls{Layout.leftMargin:8}Item{Layout.fillWidth:true}}
        }
        Rectangle {
            visible:!win.childWindow;Layout.fillWidth:true;Layout.preferredHeight:visible?27:0;color:Theme.panel
            RowLayout {
                anchors.fill:parent;spacing:14
                Label{Layout.leftMargin:12;Layout.fillWidth:true;text:App.notice;elide:Text.ElideRight;color:App.noticeLevel==="error"?Theme.error:Theme.muted;font.pixelSize:12}
                BusyIndicator{running:App.busy;visible:running;implicitWidth:20;implicitHeight:20}
                Label{text:App.readOnly?"只读":"编辑";color:Theme.muted;font.pixelSize:12}
                Label{text:App.runtime.running?"运行中":App.runtime.active?"已暂停":"已停止";color:App.runtime.running?Theme.green:Theme.muted;font.pixelSize:12}
                Label{Layout.rightMargin:12;text:Number(App.runtime.elapsed||0).toFixed(2)+" s";color:Theme.muted;font.family:"Consolas";font.pixelSize:12}
            }
        }
    }
    Menu {
        id: pagesMenu
        MenuItem { text: "功能设计"; onTriggered: App.workspaces.open("graph", win.hostId) }
        MenuItem { text: "界面设计"; onTriggered: App.workspaces.open("layout", win.hostId) }
        MenuItem { text: "运行界面"; onTriggered: App.workspaces.open("runtime", win.hostId) }
        MenuItem { text: "日志"; onTriggered: App.workspaces.open("logs", win.hostId) }
        MenuItem { text: "插件库"; onTriggered: App.workspaces.open("library", win.hostId) }
    }
    Menu {
        id: tabMenu; property string key
        MenuItem { text: "移至新窗口"; onTriggered: App.workspaces.detach(tabMenu.key) }
        MenuItem { text: "关闭标签"; onTriggered: App.workspaces.closePage(tabMenu.key) }
    }
    FileDialog { id: openProject; title: "打开工程"; nameFilters: ["CoRig 工程 (*.crpj *.corig)"]; onAccepted: App.invoke("project.open", {path: selectedFile.toString(), discard:true}) }
    FileDialog {
        id:saveProject;title:operation==="export"?"导出工程":"保存工程";fileMode:FileDialog.SaveFile;defaultSuffix:"crpj";nameFilters:["CoRig 工程 (*.crpj)"]
        property string operation:"save"
        property bool includePlugins:true
        onAccepted:{const reply=App.invoke(operation==="export"?"project.export":"project.save",{path:selectedFile.toString(),include_plugins:includePlugins});if(operation!=="export")win.savingJob=reply.job||""}
    }
    ProjectSaveOptions{id:projectSaveOptions;onFileRequested:(operation,includePlugins)=>{saveProject.operation=operation;saveProject.includePlugins=includePlugins;saveProject.open()}}
    PluginExportDialog{id:pluginExporter}
    PluginManager{id:pluginSettings}
    PluginVersionDialog{id:pluginVersions}
    Dialog {
        id: unsaved; title: "工程有未保存修改"; modal: true; anchors.centerIn: parent; width: 400
        contentItem: Label { text: "保存当前工程后再继续？"; color: Theme.text; padding: 12 }
        footer: DialogButtonBox {
            ActionButton { text: "保存"; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole; onClicked: { unsaved.close(); win.save() } }
            ActionButton { text: "丢弃修改"; DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole; onClicked: { unsaved.close(); win.continueAction() } }
            ActionButton { text: "取消"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole; onClicked: { win.pendingAction = ""; unsaved.close() } }
        }
    }
    Dialog {
        id: pythonSettings; title: "本地 Python 环境"; modal: true; width: 590; anchors.centerIn: parent
        ColumnLayout {
            anchors.fill: parent; spacing: 12
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "选择本机或虚拟环境中的 Python 3.10 及以上版本。官方功能无需 Python；软件不会修改所选环境。"; color: Theme.muted }
            RowLayout { Layout.fillWidth: true; TextField { id: pythonInput; objectName: "settings.python"; Layout.fillWidth: true; placeholderText: "选择 python.exe" } ActionButton { text: "浏览…"; onClicked: pythonFile.open() } }
            RowLayout { ActionButton { text: "保存选择"; onClicked: App.invoke("preferences.set", {python:pythonInput.text}) } ActionButton { text: "检查环境"; onClicked: { pythonProbe.jobId = App.invoke("python.probe", {}).job || "" } } }
            TextArea { id: pythonProbe; property string jobId: ""; Layout.fillWidth: true; Layout.preferredHeight: 120; readOnly: true; wrapMode: Text.WrapAnywhere; text: App.pythonPath ? "已选择：" + App.pythonPath : "尚未选择本地解释器" }
            Connections { target: App; function onJobFinished(id) { if (id === pythonProbe.jobId) pythonProbe.text = JSON.stringify(App.job(id), null, 2) } }
        }
        standardButtons: Dialog.Close
    }
    FileDialog { id: pythonFile; title: "选择本地 Python"; nameFilters: ["Python (python.exe python3.exe)", "所有文件 (*)"]; onAccepted: pythonInput.text = decodeURIComponent(selectedFile.toString().replace(/^file:\/\/\//, "")) }
    Dialog {
        id: about; title: "联调 · CoRig 0.4"; width: 510; anchors.centerIn: parent; standardButtons: Dialog.Close
        Label { width: parent.width; wrapMode: Text.WrapAnywhere; color: Theme.text; text: "原生 C++ / Qt Quick 功能设计工作台\n\n本地接口：" + App.dataDirectory + "/.sessions/api.json\n\n五个工作区共享同一个工程与运行会话。可将标签拖出为独立窗口，也可拖回其他窗口。" }
    }
}
