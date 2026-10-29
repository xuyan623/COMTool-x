// Verify real saved designs, synchronization plans and immutable object IDs.
#include <QtTest>
#include <QTemporaryDir>
#include "app/model_commands.h"
#include "quick/graph_geometry.h"

class EditorTest : public QObject {
    Q_OBJECT
private slots:
    void pendingAndPlacement();
    void syncSelectionAndIdentity();
    void independentCopiesAndReplacement();
    void freeCanvasAndPropertyOwnership();
    void capsuleGeometry();
    void configurableGridQuantizesSavedGeometry();
};

static QVariantMap apply(QVariantMap project, const QString &op, const QVariantMap &args)
{
    return corig::editProject(project,corig::Catalog{},op,args).project;
}

static QVariantMap synchronize(QVariantMap project, const QString &direction)
{
    const auto plan = corig::synchronizationPlan(project,corig::Catalog{},direction); QStringList keys;
    for (const auto &entry : plan.value("changes").toList()) keys.append(entry.toMap().value("key").toString());
    return apply(project,"sync.apply",{{"direction",direction},{"keys",keys},{"revision",plan.value("revision")}});
}

void EditorTest::pendingAndPlacement()
{
    auto p = apply(corig::emptyProject(),"editor.add",{{"page","graph"},{"id","input"},{"plugin","control.number"}});
    QCOMPARE(p.value("nodes").toList().size(),1);
    QVERIFY(corig::executionOrder(p,corig::Catalog{}).isEmpty());
    QVERIFY(corig::graphLayout(p,corig::Catalog{}).isEmpty());
    p = apply(p,"editor.place",{{"page","graph"},{"id","input"},{"position",QVariantList{30.,40.}}});
    QCOMPARE(corig::executionOrder(p,corig::Catalog{}),QVector<QString>{"input"});
    p = apply(p,"graph.node.add",{{"id","sink"},{"plugin","display.value"},{"position",QVariantList{400.,40.}}});
    p = apply(p,"graph.wire.add",{{"source",QVariantList{"input","value"}},{"target",QVariantList{"sink","value"}}});
    p = apply(p,"editor.withdraw",{{"page","graph"},{"id","input"}});
    QVERIFY(p.value("wires").toList().isEmpty());
    QCOMPARE(p.value("nodes").toList().size(),2);
    QCOMPARE(corig::executionOrder(p,corig::Catalog{}),QVector<QString>{"sink"});
    QTemporaryDir folder;corig::saveProject(folder.filePath("pending.corig"),p);
    QCOMPARE(corig::loadProject(folder.filePath("pending.corig")),p);
}

void EditorTest::syncSelectionAndIdentity()
{
    auto p = apply(corig::emptyProject(),"graph.node.add",{{"id","control"},{"plugin","control.text"},{"position",QVariantList{0.,0.}}});
    p = synchronize(p,"graph_to_ui");
    const auto items = p.value("ui").toMap().value("items").toList(); QString id;
    for (const auto &entry : items) if (entry.toMap().value("node") == "control") id = entry.toMap().value("id").toString();
    QVERIFY(!id.isEmpty());
    p = apply(p,"editor.place",{{"page","layout"},{"id",id},{"parent","root"},{"position",QVariantList{160.,90.}}});
    p = apply(p,"ui.item.patch",{{"id",id},{"properties",QVariantMap{{"width",456.},{"title","独立界面标题"}}}});
    QCOMPARE(synchronize(p,"graph_to_ui"),p);
    const auto before = p;
    p = apply(p,"editor.remove",{{"page","graph"},{"ids",QStringList{"control"}},{"confirmed",true}});
    const auto deletion = corig::synchronizationPlan(p,corig::Catalog{},"graph_to_ui");
    QCOMPARE(deletion.value("changes").toList().size(),1);
    QCOMPARE(apply(p,"sync.apply",{{"direction","graph_to_ui"},{"keys",QStringList{}},{"revision",deletion.value("revision")}}),p);
    p = synchronize(p,"ui_to_graph");
    QCOMPARE(p.value("nodes").toList().size(),1);
    QVERIFY(!p.value("nodes").toList()[0].toMap().value("placed").toBool());
    QCOMPARE(p.value("ui"),before.value("ui"));
    QVERIFY_EXCEPTION_THROWN(apply(p,"sync.apply",{{"direction","graph_to_ui"},{"keys",QStringList{}},{"revision",deletion.value("revision")}}),corig::Error);
}

void EditorTest::independentCopiesAndReplacement()
{
    auto p = apply(corig::emptyProject(),"graph.node.add",{{"id","source"},{"plugin","source.constant"}});
    p = apply(p,"graph.node.add",{{"id","math"},{"plugin","function.sin"}});
    p = apply(p,"graph.node.add",{{"id","sink"},{"plugin","display.value"}});
    p = apply(p,"graph.wire.add",{{"source",QVariantList{"source","value"}},{"target",QVariantList{"math","x"}}});
    p = apply(p,"graph.wire.add",{{"source",QVariantList{"math","out"}},{"target",QVariantList{"sink","value"}}});
    auto copy = corig::copyEditorObjects(p,"graph",{"source","math"});
    p = apply(p,"editor.paste",{{"page","graph"},{"payload",copy}});
    QCOMPARE(p.value("nodes").toList().size(),5);
    QCOMPARE(p.value("wires").toList().size(),3); // Only the internal copied wire.
    p = apply(p,"editor.replace",{{"page","graph"},{"ids",QStringList{"math"}},{"plugin","function.cos"},{"confirmed",true}});
    QCOMPARE(p.value("wires").toList().size(),3);
    const auto impact = corig::editorImpact(p,corig::Catalog{},{{"page","graph"},{"ids",QStringList{"math"}},{"operation","replace"},{"plugin","control.text"}});
    QVERIFY(impact.value("effects").toList().size()>=1);
    p = apply(p,"editor.replace",{{"page","graph"},{"ids",QStringList{"math"}},{"plugin","control.text"},{"confirmed",true}});
    QCOMPARE(p.value("wires").toList().size(),1);
}

void EditorTest::freeCanvasAndPropertyOwnership()
{
    auto p = apply(corig::emptyProject(),"editor.add",{{"page","layout"},{"id","button"},{"plugin","control.button"}});
    QVERIFY(p.value("nodes").toList().isEmpty());
    p = apply(p,"editor.place",{{"page","layout"},{"id","button"},{"parent","root"},{"position",QVariantList{50.,60.}}});
    p = synchronize(p,"ui_to_graph");
    const auto node = p.value("nodes").toList()[0].toMap();const auto id = node.value("id").toString();
    p = apply(p,"ui.item.patch",{{"id","button"},{"properties",QVariantMap{{"title","界面名称"},{"color","#FFFFFF"}}}});
    QCOMPARE(synchronize(p,"ui_to_graph").value("nodes"),p.value("nodes"));
    QVERIFY_EXCEPTION_THROWN(apply(p,"ui.item.patch",{{"id","button"},{"properties",QVariantMap{{"value",123.}}}}),corig::Error);
    p = apply(p,"editor.withdraw",{{"page","layout"},{"id","button"}});
    QCOMPARE(p.value("nodes").toList()[0].toMap().value("id").toString(),id);
    QVERIFY_EXCEPTION_THROWN(apply(p,"editor.remove",{{"page","layout"},{"ids",QStringList{"button"}}}),corig::Error);
}

void EditorTest::capsuleGeometry()
{
    const corig::Catalog catalog;
    for (const int angle : {0,90,180,270}) {
        const auto g = corig::nodeGeometry({{"plugin","function.exp"},{"orientation",angle},{"config",QVariantMap{}}},catalog);
        QCOMPARE(g.value("symbol").toString(),QString("eˣ"));
        QCOMPARE(g.value("ports").toList().size(),2);
        const auto a = g.value("ports").toList()[0].toMap(), b = g.value("ports").toList()[1].toMap();
        QVERIFY(a.value("nx").toDouble()*b.value("nx").toDouble()+a.value("ny").toDouble()*b.value("ny").toDouble()<0.);
        if (angle==90 || angle==270) for (const auto &value : g.value("ports").toList()) { const auto port = value.toMap();const auto area = g.value(port.value("direction")=="in"?"input_area":"output_area").toList();
            QVERIFY(port.value("label_y").toDouble()>=area[1].toDouble()+30.);
            QVERIFY(port.value("label_y").toDouble()+30.<=area[1].toDouble()+area[3].toDouble()-10.);
        }
    }
    const auto source = corig::nodeGeometry({{"plugin","control.button"},{"config",QVariantMap{}}},catalog);
    QCOMPARE(source.value("input_area").toList()[2].toDouble(),0.);
}

void EditorTest::configurableGridQuantizesSavedGeometry()
{
    auto p = apply(corig::emptyProject(),"editor.add",{{"page","layout"},{"id","a"},{"plugin","control.text"}});
    p = apply(p,"editor.place",{{"page","layout"},{"id","a"},{"parent","root"},{"position",QVariantList{37.,-21.}}});
    const auto props = [&] { for (const auto &entry : p.value("ui").toMap().value("items").toList()) if (entry.toMap().value("id")=="a") return entry.toMap().value("properties").toMap(); return QVariantMap{}; };
    QCOMPARE(props().value("x").toDouble(),32.);QCOMPARE(props().value("y").toDouble(),-16.);
    p = apply(p,"ui.item.patch",{{"id","root"},{"properties",QVariantMap{{"grid_size",10}}}});
    QCOMPARE(props().value("x").toDouble(),30.);QCOMPARE(props().value("y").toDouble(),-20.);
    p = apply(p,"ui.item.patch",{{"id","a"},{"properties",QVariantMap{{"width",237.},{"height",97.}}}});
    QCOMPARE(props().value("width").toDouble(),240.);QCOMPARE(props().value("height").toDouble(),100.);
}

QTEST_MAIN(EditorTest)
#include "editor_test.moc"
