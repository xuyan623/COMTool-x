// Real Qt Quick windows and production controls receive native pointer/key
// events. This verifies gesture state independently of desktop focus/input.
#include <QtTest>
#include <QTemporaryDir>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QJSValue>
#include <QMimeData>
#include <QJsonDocument>
#include <cmath>
#include "app/controller.h"
#include "app/workspaces.h"
#include "quick/renderers.h"
#include "quick/icon_provider.h"

class WorkbenchUiTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void cleanup();
    void bubbleReturnsToSocketAndMother();
    void rightClickCancelsHeldObjects();
    void containerDragUsesEditorGestures();
    void externalDropsReachContainerSlots();
    void outsideCanvasObjectsRemainDraggable();
    void nestedControlsAlignWithConfigurableGrid();
    void synchronizationRequiresChecklist();
    void graphResizeAndDeleteWarning();
    void designAndRuntimeShareContainerLayout();
    void publicEditorPresentationRoundTrip();
    void externalControlEditsReachRealNodes();
    void switchesPressedFeedbackAndCombinedEngineControl();
private:
    QVariant run(const QString &op, const QVariantMap &args = {});
    QQuickItem *findItem(const QString &name, QQuickItem *from = nullptr);
    QPoint portPoint(const QString &node, const QString &port);
    QVariantMap mapProperty(QObject *object, const char *name);
    void click(const QPoint &point, Qt::MouseButton button = Qt::LeftButton);
    std::unique_ptr<QTemporaryDir> folder_;
    std::unique_ptr<corig::Controller> app_;
    std::unique_ptr<QQmlApplicationEngine> qml_;
    QQuickWindow *window_ = nullptr;
    QQuickItem *graph_ = nullptr;
};

void WorkbenchUiTest::initTestCase()
{
    QQuickStyle::setStyle("Basic");
    qmlRegisterType<corig::NodeSurface>("CoRig.Native",1,0,"NodeSurface");
    qmlRegisterType<corig::WireLayer>("CoRig.Native",1,0,"WireLayer");
    qmlRegisterType<corig::NativePlot>("CoRig.Native",1,0,"NativePlot");
    qmlRegisterType<corig::NativeAttitude>("CoRig.Native",1,0,"NativeAttitude");
}

void WorkbenchUiTest::init()
{
    folder_ = std::make_unique<QTemporaryDir>();
    app_ = std::make_unique<corig::Controller>(folder_->path());
    qml_ = std::make_unique<QQmlApplicationEngine>();qml_->addImageProvider("icons",new corig::IconProvider);
    app_->attachUi(qml_.get());
    const auto workspace = qobject_cast<corig::Workspaces *>(app_->workspaces());
    graph_ = workspace->page("graph");window_ = graph_->window();
    QVERIFY(QTest::qWaitForWindowExposed(window_));
    run("graph.node.add",{{"id","source"},{"plugin","control.number"},{"position",QVariantList{20.,40.}}});
    run("graph.node.add",{{"id","target"},{"plugin","function.exp"},{"position",QVariantList{360.,40.}}});
    run("graph.node.add",{{"id","text"},{"plugin","control.text"},{"position",QVariantList{20.,240.}}});
    run("graph.fit");QTest::qWait(30);
}

void WorkbenchUiTest::cleanup()
{
    app_->shutdown();qml_.reset();app_.reset();folder_.reset();graph_ = nullptr;window_ = nullptr;
}

QVariant WorkbenchUiTest::run(const QString &op, const QVariantMap &args)
{
    auto result = app_->invoke(op,args);const auto map = result.toMap();
    if (map.contains("error")) { QTest::qFail(qPrintable(map.value("error").toString()),__FILE__,__LINE__);return {}; }
    const auto job = map.value("job").toString();if (job.isEmpty()) return result;
    QElapsedTimer timer;timer.start();
    while (timer.elapsed()<10000) {
        const auto state = app_->job(job);if (state.value("state") == "complete") return state.value("result");
        if (state.value("state") == "failed") { QTest::qFail(qPrintable(state.value("error").toString()),__FILE__,__LINE__);return {}; }
        QTest::qWait(1);
    }
    QTest::qFail("Application job timed out",__FILE__,__LINE__);return {};
}

QVariantMap WorkbenchUiTest::mapProperty(QObject *object, const char *name)
{
    const auto value = object->property(name);return value.metaType() == QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant().toMap() : value.toMap();
}

QQuickItem *WorkbenchUiTest::findItem(const QString &name, QQuickItem *from)
{
    if (!from) from = window_->contentItem();if (from->objectName() == name) return from;
    for (auto child : from->childItems()) if (auto item = findItem(name,child)) return item;
    return nullptr;
}

QPoint WorkbenchUiTest::portPoint(const QString &id, const QString &port)
{
    const auto viewport = findItem("graph.viewport");
    for (const auto &entry : app_->graphNodes()) { const auto node = entry.toMap();if (node.value("id") != id) continue;
        for (const auto &item : node.value("geometry").toMap().value("ports").toList()) { const auto p = item.toMap();if (p.value("id") != port) continue;
            const auto pos = node.value("position").toList();const auto zoom = graph_->property("zoom").toDouble();
            return viewport->mapToScene({graph_->property("panX").toDouble()+(pos[0].toDouble()+p.value("x").toDouble())*zoom,graph_->property("panY").toDouble()+(pos[1].toDouble()+p.value("y").toDouble())*zoom}).toPoint();
        }
    }
    QTest::qFail("Port not found",__FILE__,__LINE__);return {};
}

void WorkbenchUiTest::click(const QPoint &point, Qt::MouseButton button)
{
    QTest::mouseMove(window_,point);QTest::mouseClick(window_,button,Qt::NoModifier,point);QTest::qWait(25);
}

void WorkbenchUiTest::bubbleReturnsToSocketAndMother()
{
    click(portPoint("source","value"));QVERIFY(!mapProperty(graph_,"linkFrom").isEmpty());
    QVERIFY(mapProperty(graph_,"options").value("target/x").toMap().value("compatible").toBool());
    click(portPoint("target","x"));QTRY_COMPARE(app_->project().value("wires").toList().size(),1);
    QTRY_VERIFY(mapProperty(graph_,"linkFrom").isEmpty());
    click(portPoint("target","x"));QTRY_VERIFY(app_->project().value("wires").toList().isEmpty());
    QTRY_VERIFY(!mapProperty(graph_,"linkFrom").isEmpty());
    QVERIFY(mapProperty(graph_,"options").value("target/x").toMap().value("compatible").toBool());
    click(portPoint("target","x"));QTRY_COMPARE(app_->project().value("wires").toList().size(),1);
    click(portPoint("target","x"));QTRY_VERIFY(!mapProperty(graph_,"linkFrom").isEmpty());
    click(portPoint("source","value"));QTRY_VERIFY(mapProperty(graph_,"linkFrom").isEmpty());
    QVERIFY(app_->project().value("wires").toList().isEmpty());
    // A different data contract cannot be accepted; the held bubble survives.
    click(portPoint("text","value"));QVERIFY(!mapProperty(graph_,"options").value("target/x").toMap().value("compatible").toBool());
    click(portPoint("target","x"));QVERIFY(!mapProperty(graph_,"linkFrom").isEmpty());
    click(findItem("graph.viewport")->mapToScene({100.,100.}).toPoint(),Qt::RightButton);
    QVERIFY(mapProperty(graph_,"linkFrom").isEmpty());
}

void WorkbenchUiTest::rightClickCancelsHeldObjects()
{
    click(portPoint("source","value"));
    const auto point = findItem("graph.viewport")->mapToScene({130.,130.}).toPoint();click(point,Qt::RightButton);
    QVERIFY(mapProperty(graph_,"linkFrom").isEmpty());
    auto quick = graph_->findChild<QObject *>("graph.quickmenu");QVERIFY(quick);QVERIFY(!quick->property("visible").toBool());
    const auto before = app_->project().value("nodes").toList().size();
    graph_->setProperty("placement",QVariantMap{{"plugin","function.cos"}});
    click(point,Qt::RightButton);QVERIFY(mapProperty(graph_,"placement").isEmpty());
    QVERIFY(!quick->property("visible").toBool());QCOMPARE(app_->project().value("nodes").toList().size(),before);
    run("workspace.open",{{"page","layout"}});QTest::qWait(30);auto page = qobject_cast<corig::Workspaces *>(app_->workspaces())->page("layout");QVERIFY(page);
    page->setProperty("placement",QVariantMap{{"plugin","control.button"}});
    click(findItem("layout.viewport")->mapToScene({150.,150.}).toPoint(),Qt::RightButton);
    QVERIFY(mapProperty(page,"placement").isEmpty());
    QCOMPARE(app_->project().value("nodes").toList().size(),before);
    click(findItem("layout.viewport")->mapToScene({150.,150.}).toPoint(),Qt::RightButton);
    auto layoutQuick = page->findChild<QObject *>("layout.quickmenu");QVERIFY(layoutQuick);QVERIFY(layoutQuick->property("visible").toBool());
}

void WorkbenchUiTest::containerDragUsesEditorGestures()
{
    run("editor.add",{{"page","layout"},{"id","control_view"},{"plugin","control.text"}});
    run("editor.add",{{"page","layout"},{"id","container"},{"plugin","layout.row"}});
    run("editor.place",{{"page","layout"},{"id","control_view"},{"parent","root"},{"position",QVariantList{40.,40.}}});
    run("editor.place",{{"page","layout"},{"id","container"},{"parent","root"},{"position",QVariantList{500.,100.}}});
    run("workspace.open",{{"page","layout"}});QTest::qWait(40);
    auto control = findItem("ui.control_view"), container = findItem("ui.container");QVERIFY(control);QVERIFY(container);
    const auto from = control->mapToScene({control->width()/2.,control->height()/2.}).toPoint(), to = container->mapToScene({container->width()/2.,container->height()/2.}).toPoint();
    QTest::mousePress(window_,Qt::LeftButton,Qt::NoModifier,from);QTest::mouseMove(window_,from+QPoint(12,6),20);QTest::mouseMove(window_,to,30);QTest::mouseRelease(window_,Qt::LeftButton,Qt::NoModifier,to);QTest::qWait(50);
    QVERIFY(app_->uiItem("container").value("children").toList().contains("control_view"));
    // A text field inside the dragged control never obtained focus/edited data.
    QCOMPARE(app_->project().value("nodes").toList().size(),3);
}

void WorkbenchUiTest::synchronizationRequiresChecklist()
{
    const auto before = app_->project().value("ui").toMap().value("items").toList().size();
    auto button = findItem("graph.sync");QVERIFY(button);click(button->mapToScene({button->width()/2.,button->height()/2.}).toPoint());
    auto dialog = graph_->findChild<QObject *>("sync.dialog");QVERIFY(dialog);QVERIFY(dialog->property("visible").toBool());
    QCOMPARE(app_->project().value("ui").toMap().value("items").toList().size(),before);
    const auto plan = mapProperty(dialog,"plan");QVERIFY(!plan.value("changes").toList().isEmpty());
    const auto chosen = dialog->property("chosen").value<QJSValue>().toVariant().toList();QCOMPARE(chosen.size(),plan.value("changes").toList().size());
    QMetaObject::invokeMethod(dialog,"reject");
    QCOMPARE(app_->project().value("ui").toMap().value("items").toList().size(),before);
}

void WorkbenchUiTest::graphResizeAndDeleteWarning()
{
    run("graph.select",{{"id","target"}});QTest::qWait(20);
    const auto before = app_->graphNodes();auto handle = findItem("graph.resize.target");QVERIFY(handle);
    const auto point = handle->mapToScene({handle->width()/2.,handle->height()/2.}).toPoint();
    QTest::mousePress(window_,Qt::LeftButton,Qt::NoModifier,point);QTest::mouseMove(window_,point+QPoint(50,30),25);QTest::mouseRelease(window_,Qt::LeftButton,Qt::NoModifier,point+QPoint(50,30));QTest::qWait(30);
    QVERIFY(app_->nodeDesign("target").value("size").toList().size()==2);
    const auto count = app_->project().value("nodes").toList().size();findItem("graph.viewport")->forceActiveFocus();
    QTest::keyClick(window_,Qt::Key_Delete);QTest::qWait(20);
    auto dialog = graph_->findChild<QObject *>("editor.confirm");QVERIFY(dialog);QVERIFY(dialog->property("visible").toBool());
    QCOMPARE(app_->project().value("nodes").toList().size(),count);
    QMetaObject::invokeMethod(dialog,"accept");QTRY_COMPARE(app_->project().value("nodes").toList().size(),count-1);
    run("history.undo");QVERIFY(!app_->nodeDesign("target").isEmpty());
}

void WorkbenchUiTest::externalDropsReachContainerSlots()
{
    run("editor.add",{{"page","layout"},{"id","container"},{"plugin","layout.row"}});
    run("editor.place",{{"page","layout"},{"id","container"},{"parent","root"},{"position",QVariantList{60.,60.}}});
    run("workspace.open",{{"page","layout"}});QTest::qWait(40);
    auto container = findItem("ui.container");QVERIFY(container);
    const auto point = container->mapToScene({container->width()/3.,container->height()/2.}).toPoint();
    QMimeData plugin;plugin.setData("application/x-corig-plugin","control.button");
    QDragEnterEvent enter(point,Qt::CopyAction,&plugin,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(window_,&enter);QVERIFY(enter.isAccepted());
    QDragMoveEvent move(point,Qt::CopyAction,&plugin,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(window_,&move);
    QDropEvent drop(point,Qt::CopyAction,&plugin,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(window_,&drop);QTest::qWait(30);QVERIFY(drop.isAccepted());
    QCOMPARE(app_->uiItem("container").value("children").toList().size(),1);
    run("editor.add",{{"page","layout"},{"id","pending_control"},{"plugin","control.text"}});
    QMimeData pending;pending.setData("application/x-corig-object",QJsonDocument::fromVariant(QVariantMap{{"page","layout"},{"id","pending_control"}}).toJson(QJsonDocument::Compact));
    QDragEnterEvent enterPending(point,Qt::MoveAction,&pending,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(window_,&enterPending);QVERIFY(enterPending.isAccepted());
    QDropEvent dropPending(point,Qt::MoveAction,&pending,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(window_,&dropPending);QTest::qWait(30);QVERIFY(dropPending.isAccepted());
    QVERIFY(app_->uiItem("container").value("children").toList().contains("pending_control"));
}

void WorkbenchUiTest::outsideCanvasObjectsRemainDraggable()
{
    run("editor.add",{{"page","layout"},{"id","outside"},{"plugin","control.text"}});
    run("editor.place",{{"page","layout"},{"id","outside"},{"parent","root"},{"position",QVariantList{40.,780.}}});
    run("workspace.open",{{"page","layout"}});QTest::qWait(40);
    auto item = findItem("ui.outside");QVERIFY(item);
    const auto from = item->mapToScene({80.,40.}).toPoint(), to = from-QPoint(0,180);
    QTest::mousePress(window_,Qt::LeftButton,Qt::NoModifier,from);QTest::mouseMove(window_,from-QPoint(0,12),20);QTest::mouseMove(window_,to,30);QTest::mouseRelease(window_,Qt::LeftButton,Qt::NoModifier,to);QTest::qWait(40);
    QVERIFY(app_->uiItem("outside").value("properties").toMap().value("y").toDouble()<780.);
}

void WorkbenchUiTest::nestedControlsAlignWithConfigurableGrid()
{
    run("ui.item.patch",{{"id","root"},{"properties",QVariantMap{{"grid_size",10}}}});
    run("editor.add",{{"page","layout"},{"id","row"},{"plugin","layout.row"}});
    run("editor.place",{{"page","layout"},{"id","row"},{"parent","root"},{"position",QVariantList{43.,57.}}});
    for (const auto &id : {"a","b","c"}) { run("editor.add",{{"page","layout"},{"id",id},{"plugin","control.text"}});run("editor.place",{{"page","layout"},{"id",id},{"parent","row"}}); }
    run("workspace.open",{{"page","layout"}});QTest::qWait(40);
    auto canvas = findItem("ui.root");QVERIFY(canvas);
    for (const auto &id : {"row","a","b","c"}) { const auto item = findItem("ui."+QString(id));QVERIFY(item);const auto p = item->mapToItem(canvas,{0.,0.});
        for (const auto value : {p.x(),p.y(),item->width(),item->height()}) QVERIFY2(std::abs(value/10.-std::round(value/10.))<1e-6,"A nested control missed the paper grid");
    }
}

void WorkbenchUiTest::designAndRuntimeShareContainerLayout()
{
    run("editor.add",{{"page","layout"},{"id","container"},{"plugin","layout.row"}});
    run("editor.place",{{"page","layout"},{"id","container"},{"parent","root"},{"position",QVariantList{32.,64.}}});
    run("ui.item.patch",{{"id","container"},{"properties",QVariantMap{{"title","仅用于左侧层级的容器名"}}}});
    run("editor.add",{{"page","layout"},{"id","child"},{"plugin","control.text"}});
    run("editor.place",{{"page","layout"},{"id","child"},{"parent","container"}});
    auto workspaces = qobject_cast<corig::Workspaces *>(app_->workspaces());
    run("workspace.open",{{"page","layout"}});QTest::qWait(40);
    auto design = workspaces->page("layout"), designRoot = findItem("ui.root",design), designChild = findItem("ui.child",design);
    QVERIFY(designRoot);QVERIFY(designChild);const auto position = designChild->mapToItem(designRoot,{0.,0.});const auto size = designChild->size();
    auto designContainer = findItem("ui.container",design);QCOMPARE(designContainer->property("headerHeight").toDouble(),0.);
    run("workspace.open",{{"page","runtime"}});QTest::qWait(40);
    auto runtime = workspaces->page("runtime"), runtimeRoot = findItem("ui.root",runtime), runtimeChild = findItem("ui.child",runtime);
    QVERIFY(runtimeRoot);QVERIFY(runtimeChild);QCOMPARE(runtimeChild->mapToItem(runtimeRoot,{0.,0.}),position);QCOMPARE(runtimeChild->size(),size);
    auto runtimeContainer = findItem("ui.container",runtime);QCOMPARE(runtimeContainer->property("headerHeight").toDouble(),0.);
}

void WorkbenchUiTest::publicEditorPresentationRoundTrip()
{
    const auto before = app_->project();
    for (const auto &page : {QString("graph"),QString("layout")}) {
        run("workspace.open",{{"page",page}});QTest::qWait(25);
        run("editor.configure",{{"page",page},{"zoom",.75},{"pan_x",123.},{"pan_y",-45.},{"left_width",180.},{"right_width",224.}});
        QTest::qWait(25);const auto state = run("editor.state",{{"page",page}}).toMap();
        QCOMPARE(state.value("zoom").toDouble(),.75);QCOMPARE(state.value("pan_x").toDouble(),123.);QCOMPARE(state.value("pan_y").toDouble(),-45.);
        QCOMPARE(state.value("left_width").toDouble(),180.);QCOMPARE(state.value("right_width").toDouble(),224.);
        QVERIFY_EXCEPTION_THROWN(app_->command("editor.configure",{{"page",page},{"zoom",0.},{"pan_x",999.}}),corig::Error);
        QCOMPARE(run("editor.state",{{"page",page}}).toMap(),state);
        run("editor.configure",{{"page",page},{"left_width",0.},{"right_width",0.}});QTest::qWait(25);
        QCOMPARE(run("editor.state",{{"page",page}}).toMap().value("left_width").toDouble(),0.);
    }
    QCOMPARE(app_->project(),before);
}

void WorkbenchUiTest::externalControlEditsReachRealNodes()
{
    run("graph.node.add",{{"id","slider"},{"plugin","control.slider"}});
    run("graph.node.add",{{"id","select"},{"plugin","control.select"},{"config",QVariantMap{{"choices",QStringList{"a","b"}},{"value","a"}}}});
    run("ui.update");
    int row = 0;
    for (const auto &entry : app_->project().value("ui").toMap().value("items").toList()) if (entry.toMap().contains("node")) {
        run("editor.place",{{"page","layout"},{"id",entry.toMap().value("id")},{"parent","root"},{"position",QVariantList{16.,16.+row++*144.}}});
        run("ui.item.patch",{{"id",entry.toMap().value("id")},{"properties",QVariantMap{{"height",128.}}}});
    }
    run("workspace.open",{{"page","runtime"}});run("runtime.start",{{"debug",false}});QTest::qWait(60);
    run("controls.set",{{"id","control.slider"},{"page","runtime"},{"property","value"},{"value",37.5}});
    QTRY_COMPARE(app_->nodeState("slider").value("state").toMap().value("value").toDouble(),37.5);
    run("controls.set",{{"id","control.select"},{"page","runtime"},{"property","currentIndex"},{"value",1}});
    QTRY_COMPARE(app_->nodeState("select").value("state").toMap().value("value").toString(),QString("b"));
    run("controls.set",{{"id","control.source"},{"page","runtime"},{"property","text"},{"value","7"}});
    run("controls.commit",{{"id","control.source"},{"page","runtime"}});
    QTRY_COMPARE(app_->nodeState("source").value("state").toMap().value("value").toDouble(),7.);
    run("runtime.stop");run("workspace.open",{{"page","layout"}});QTest::qWait(25);
    run("controls.set",{{"id","layout.grid"},{"page","layout"},{"property","value"},{"value",24}});
    QTRY_COMPARE(app_->uiItem("root").value("properties").toMap().value("grid_size").toInt(),24);
    const auto controls = run("controls.list",{{"page","layout"}}).toList();
    QVERIFY(std::any_of(controls.begin(),controls.end(),[](const QVariant &value) { const auto item = value.toMap();return item.value("name").toString().isEmpty() && item.value("path").toString().startsWith("auto:layout:"); }));
}

void WorkbenchUiTest::switchesPressedFeedbackAndCombinedEngineControl()
{
    run("graph.node.add",{{"id","flag"},{"plugin","control.checkbox"}});
    run("graph.node.add",{{"id","button"},{"plugin","control.button"},{"config",QVariantMap{{"label","触发"}}}});
    run("ui.view.add",{{"id","flag_view"},{"node","flag"},{"view","control"},{"properties",QVariantMap{{"x",16.},{"y",16.},{"width",320.},{"height",64.},{"title","冗余标题"}}}});
    run("ui.view.add",{{"id","button_view"},{"node","button"},{"view","control"},{"properties",QVariantMap{{"x",16.},{"y",96.},{"width",320.},{"height",64.},{"title","冗余标题"}}}});
    run("workspace.open",{{"page","runtime"}}); run("runtime.start",{{"debug",false}}); QTest::qWait(60);
    auto runtime = qobject_cast<corig::Workspaces *>(app_->workspaces())->page("runtime");
    auto flag = findItem("control.flag",runtime); QVERIFY(flag); QVERIFY(flag->inherits("QQuickSwitch"));
    const auto indicator = flag->property("indicator").value<QQuickItem *>(); QVERIFY(indicator); QVERIFY(indicator->width() > indicator->height());
    run("controls.set",{{"id","control.flag"},{"page","runtime"},{"property","checked"},{"value",true}});
    QTRY_VERIFY(app_->nodeState("flag").value("state").toMap().value("value").toBool());
    auto view = findItem("ui.flag_view",runtime); QVERIFY(view); QCOMPARE(view->property("headerHeight").toDouble(),0.);
    for (const auto child : view->findChildren<QObject *>()) QVERIFY(child->property("text").toString() != "冗余标题");
    auto button = findItem("control.button",runtime); QVERIFY(button);
    auto background = button->property("background").value<QQuickItem *>(); QVERIFY(background);
    const auto color = background->property("color");
    const auto position = button->mapToScene(button->boundingRect().center()).toPoint();
    QTest::mousePress(runtime->window(),Qt::LeftButton,Qt::NoModifier,position);
    QVERIFY(button->property("down").toBool()); QVERIFY(background->property("color") != color);
    QTest::mouseRelease(runtime->window(),Qt::LeftButton,Qt::NoModifier,position);
    QTRY_VERIFY(app_->nodeState("button").value("outputs").toMap().value("trigger").toBool());
    QVERIFY(!findItem("execution.pause"));
    run("controls.click",{{"id","execution.run"}}); QTRY_VERIFY(!app_->runtime().value("running").toBool());
    QCOMPARE(findItem("execution.run")->property("symbol").toString(),QString("run"));
    run("controls.click",{{"id","execution.run"}}); QTRY_VERIFY(app_->runtime().value("running").toBool());
}

QTEST_MAIN(WorkbenchUiTest)
#include "workbench_ui_test.moc"
