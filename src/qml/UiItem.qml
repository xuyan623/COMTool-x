import QtQuick
import QtQuick.Controls
import "DesignGrid.js" as DesignGrid

// Rendering is independent of editor gestures. All auto-layout bounds use
// integer grid cells, including padding, headers, gaps and container contents.
Rectangle {
    id:root
    property string itemId:""
    property bool designMode:false
    property var recordOverride:({})
    property var record:{App.project;return recordOverride.id?recordOverride:App.uiItem(itemId)}
    property var props:record.properties||({})
    property var definitionOverride:({})
    property string qmlUrlOverride:""
    property bool freeCanvas:record.plugin==="layout.canvas"
    property bool container:!!record.plugin
    property bool customContainer:container&&["layout.canvas","layout.row","layout.column","layout.grid","layout.tabs"].indexOf(record.plugin)<0
    property var containerSpec:{App.registry;return definitionOverride.id?definitionOverride:customContainer?App.definition(record.plugin):({})}
    property int gridSize:{App.project;return (App.uiItem((App.project.ui||{}).root||"root").properties||{}).grid_size||16}
    property var renderedChildren:(record.children||[]).filter(id=>!App.uiItem(id).pending)
    property bool emptyContainer:container&&!freeCanvas&&!customContainer&&!renderedChildren.length
    property real padding:freeCanvas?0:Math.max(0,DesignGrid.snap(props.padding===undefined?gridSize:props.padding,gridSize))
    readonly property real headerHeight:0
    property alias contentArea:content
    objectName:"ui."+itemId
    implicitWidth:props.width||320;implicitHeight:props.height||200
    visible:!record.pending
    clip:!freeCanvas||!designMode
    color:freeCanvas&&designMode?"transparent":props.background||Theme.panel
    radius:3
    border.width:designMode&&!freeCanvas?1:0
    border.color:App.selectedKind==="ui"&&App.selected===itemId?Theme.blue:Theme.border
    QtObject {
        id:previewBridge
        readonly property var properties:root.props
        readonly property var values:root.props
        readonly property var draft:root.props
        readonly property var config:root.props
        readonly property var state:({})
        readonly property bool editable:false
        function commit(changes){return({error:"预览中不可修改属性"})}
        function invoke(operation,args){return({error:"预览中不可执行操作"})}
    }
    Loader {
        id:content;x:root.padding;y:root.padding+root.headerHeight;width:Math.max(0,parent.width-root.padding*2);height:Math.max(0,parent.height-y-root.padding)
        visible:!root.emptyContainer||!root.designMode;enabled:!root.designMode
        sourceComponent:root.freeCanvas?canvasComponent:root.container?containerComponent:viewComponent
    }
    Component {
        id:canvasComponent
        Item {
            Repeater {
                model:root.renderedChildren
                Loader {
                    id:freeChild;required property string modelData;required property int index
                    property var childProps:{App.project;return App.uiItem(modelData).properties||({})}
                    x:childProps.x===undefined?root.gridSize:childProps.x;y:childProps.y===undefined?root.gridSize:childProps.y
                    width:childProps.width||320;height:childProps.height||208;z:root.renderedChildren.length-index
                    source:"UiItem.qml"
                    onLoaded:{item.itemId=Qt.binding(()=>freeChild.modelData);item.designMode=Qt.binding(()=>root.designMode)}
                }
            }
        }
    }
    Component {
        id:containerComponent
        Item {
            id:arrangement
            property bool tabs:root.record.plugin==="layout.tabs"
            property real tabHeight:tabs?Math.ceil(32/root.gridSize)*root.gridSize:0
            property var records:{App.project;return root.renderedChildren.map(id=>App.uiItem(id))}
            property var layout:root.customContainer?(records.length?App.containerLayout(root.record.plugin,root.props,records,width,height,root.gridSize):{rectangles:[]}):{rectangles:DesignGrid.arrange(root.record.plugin,root.props,records,width,Math.max(0,height-tabHeight),root.gridSize)}
            property var rectangles:layout.rectangles||[]
            Loader {
                id:customSurface;anchors.fill:parent;visible:root.customContainer
                function load() {
                    const file=root.containerSpec.qml
                    if(root.customContainer&&file)setSource(root.qmlUrlOverride||App.pluginResourceUrl(root.record.plugin,file),{bridge:root.designMode?previewBridge:App.propertyBridge("ui",root.itemId)})
                    else source=""
                }
                Component.onCompleted:load()
                Connections{target:root;function onContainerSpecChanged(){customSurface.load()}function onItemIdChanged(){customSurface.load()}function onDesignModeChanged(){customSurface.load()}}
            }
            Label{anchors.centerIn:parent;visible:!!arrangement.layout.error;text:arrangement.layout.error||"";color:Theme.error;width:parent.width;wrapMode:Text.WordWrap}
            TabBar {
                id:tabbar;visible:arrangement.tabs;width:parent.width;height:arrangement.tabHeight
                currentIndex:Math.min(Math.max(0,root.props.current_index||0),Math.max(0,count-1))
                Repeater{model:arrangement.tabs?root.renderedChildren:[];TabButton{required property string modelData;text:{App.project;const r=App.uiItem(modelData);return(r.properties||{}).title||modelData}width:Math.max(root.gridSize*4,implicitWidth)}}
            }
            Repeater {
                model:root.renderedChildren
                Loader {
                    id:child;required property string modelData;required property int index
                    property var box:arrangement.rectangles[index]||({x:0,y:0,width:0,height:0})
                    x:box.x;y:box.y+arrangement.tabHeight;width:box.width;height:box.height;z:root.renderedChildren.length-index
                    visible:box.visible!==false&&(!arrangement.tabs||index===tabbar.currentIndex)
                    source:"UiItem.qml"
                    onLoaded:{item.itemId=Qt.binding(()=>child.modelData);item.designMode=Qt.binding(()=>root.designMode)}
                }
            }
        }
    }
    Component{id:viewComponent;BuiltinView{nodeId:root.record.node||"";viewKey:root.record.view||"";uiItem:root.itemId;designOverride:root.record.binding||({});designMode:root.designMode;viewProperties:root.props}}
    Item {
        x:content.x;y:content.y;width:content.width;height:content.height;visible:root.emptyContainer&&root.designMode
        Repeater {
            model:root.record.plugin==="layout.grid"?4:2
            Rectangle {
                required property int index
                readonly property bool horizontal:root.record.plugin==="layout.row"||root.record.plugin==="layout.tabs"
                readonly property real gap:root.gridSize
                width:horizontal||root.record.plugin==="layout.grid"?(parent.width-gap)/2:parent.width
                height:horizontal?parent.height:root.record.plugin==="layout.grid"?(parent.height-gap)/2:(parent.height-gap)/2
                x:horizontal?index*(width+gap):root.record.plugin==="layout.grid"?(index%2)*(width+gap):0
                y:horizontal?0:root.record.plugin==="layout.grid"?Math.floor(index/2)*(height+gap):index*(height+gap)
                color:"transparent";border.color:Theme.border;radius:2
                Label{anchors.centerIn:parent;text:"拖入视图";color:Theme.muted;font.pixelSize:11}
            }
        }
    }
}
