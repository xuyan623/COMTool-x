// Real declaration, graph and project serialization tests. No alternate engine
// or synthetic plugin implementation is used to stand in for production code.
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include "core/project.h"

class CoreTest : public QObject {
    Q_OBJECT
private slots:
    void declarations();
    void typedValues();
    void projectRoundTrip();
    void graphValidation();
    void booleanControlProjectMigration();
};

void CoreTest::declarations()
{
    cx::Catalog catalog;
    QVERIFY(catalog.describe().size() >= 68);
    QVERIFY(!catalog.search("正弦").isEmpty());
    QVERIFY(!catalog.search("lowpass").isEmpty());
    const auto config = catalog.checkedConfig("source.constant", {{"values",QVariantMap{{"x",3.}, {"y",4.}}}});
    const auto ports = catalog.ports("source.constant", config);
    QCOMPARE(ports.size(), 2);
    QVERIFY_EXCEPTION_THROWN(catalog.checkedConfig("source.constant", {{"values",QVariantMap{{"x","abc"}}}}), cx::Error);
    QVERIFY_EXCEPTION_THROWN(catalog.checkedConfig("source.time", {{"period_ms",-1.}}), cx::Error);
    QVERIFY_EXCEPTION_THROWN(catalog.checkedConfig("source.time", {{"unknown",1.}}), cx::Error);
}

void CoreTest::typedValues()
{
    const QByteArray bytes("\0\xff\x41", 3);
    QCOMPARE(cx::fromJsonValue(cx::toJsonValue(bytes)).toByteArray(), bytes);
    QVERIFY(cx::valueMatches(bytes, "bytes"));
    QVERIFY(!cx::valueMatches(bytes, "number"));
    QVERIFY(!cx::valueMatches(true, "number"));
    QCOMPARE(cx::sampleFrames(QVariantMap{{"time",.01}, {"values",QVariantMap{{"a",2.}}}}).size(), 1);
    QVERIFY_EXCEPTION_THROWN(cx::sampleFrames(QVariantMap{{"values",QVariantMap{}}}), cx::Error);
    QCOMPARE(cx::timeUnit("ms"), .001);
}

void CoreTest::projectRoundTrip()
{
    QTemporaryDir folder;
    auto project = cx::emptyProject("便携测试");
    cx::Catalog catalog;
    cx::validateProject(project, catalog);
    const auto original = folder.filePath("资源.bin");
    QFile source(original); QVERIFY(source.open(QIODevice::WriteOnly)); source.write("payload\0binary", 14); source.close();
    const auto ref = cx::importResource(project, original);
    QVERIFY(ref.startsWith("asset://"));
    QVERIFY(QFile::remove(original));
    const auto path = folder.filePath("test.comtoolx");
    cx::saveProject(path, project);
    const auto restored = cx::loadProject(path);
    QCOMPARE(restored.value("name").toString(), QString("便携测试"));
    QCOMPARE(cx::resourceBytes(restored, ref), QByteArray("payload\0binary",14));
    cx::validateProject(restored, catalog);
    QFile corrupt(path); QVERIFY(corrupt.open(QIODevice::Append)); corrupt.write("irrelevant trailing bytes"); corrupt.close();
    // Compression handles its own end marker, while the design still validates.
    cx::validateProject(cx::loadProject(path), catalog);
}

void CoreTest::graphValidation()
{
    cx::Catalog catalog;
    auto p = cx::emptyProject();
    QVariantMap a{{"id","a"}, {"plugin","source.constant"}, {"config",QVariantMap{{"values",QVariantMap{{"x",2.}}}}}, {"position",QVariantList{0.,0.}}};
    QVariantMap b{{"id","b"}, {"plugin","function.sin"}, {"config",QVariantMap{}}, {"position",QVariantList{200.,0.}}};
    p["nodes"] = QVariantList{a,b};
    QVariantMap wire{{"id","w"}, {"source",QVariantList{"a","x"}}, {"target",QVariantList{"b","x"}}};
    p["wires"] = QVariantList{wire};
    cx::validateProject(p, catalog);
    QCOMPARE(cx::executionOrder(p, catalog), QVector<QString>({"a","b"}));
    auto duplicate = wire; duplicate["id"] = "w2"; p["wires"] = QVariantList{wire,duplicate};
    QVERIFY_EXCEPTION_THROWN(cx::validateProject(p, catalog), cx::Error);
    p["wires"] = QVariantList{};
    a["plugin"] = "function.sin"; a["config"] = QVariantMap{}; p["nodes"] = QVariantList{a,b};
    p["wires"] = QVariantList{QVariantMap{{"id","ab"}, {"source",QVariantList{"a","out"}}, {"target",QVariantList{"b","x"}}},
                              QVariantMap{{"id","ba"}, {"source",QVariantList{"b","out"}}, {"target",QVariantList{"a","x"}}}};
    QVERIFY_EXCEPTION_THROWN(cx::validateProject(p, catalog), cx::Error);
}

void CoreTest::booleanControlProjectMigration()
{
    QTemporaryDir folder; auto project = cx::emptyProject();
    const QVariantMap button{{"id","button"},{"plugin","control.button"},{"position",QVariantList{0.,0.}},
        {"config",QVariantMap{{"label","执行"},{"payload","old content"}}}};
    const QVariantMap fork{{"id","fork"},{"plugin","data.fork"},{"position",QVariantList{300.,0.}},
        {"config",QVariantMap{{"data_type","trigger"},{"outputs",QVariantList{"left","right"}}}}};
    project["nodes"] = QVariantList{button,fork};
    project["wires"] = QVariantList{QVariantMap{{"id","control"},{"source",QVariantList{"button","trigger"}},{"target",QVariantList{"fork","in"}}}};
    auto ui = project.value("ui").toMap(); auto items = ui.value("items").toList(); auto root = items.front().toMap();
    root["children"] = QVariantList{"button_view"}; items[0] = root;
    items.append(QVariantMap{{"id","button_view"},{"node","button"},{"view","control"},{"binding",button},{"properties",QVariantMap{}}});
    ui["items"] = items; project["ui"] = ui;
    const auto path = folder.filePath("previous-native.comtoolx"); cx::saveProject(path,project);
    const auto restored = cx::loadProject(path); cx::Catalog catalog; cx::validateProject(restored,catalog);
    const auto nodes = restored.value("nodes").toList();
    QVERIFY(!nodes[0].toMap().value("config").toMap().contains("payload"));
    QCOMPARE(nodes[1].toMap().value("config").toMap().value("data_type").toString(),QString("boolean"));
    QCOMPARE(nodes[1].toMap().value("config").toMap().value("outputs").toList(),QVariantList({"left","right"}));
    QCOMPARE(restored.value("wires"),project.value("wires"));
    QVERIFY(!restored.value("ui").toMap().value("items").toList()[1].toMap().value("binding").toMap().value("config").toMap().contains("payload"));
}

QTEST_GUILESS_MAIN(CoreTest)
#include "core_test.moc"
