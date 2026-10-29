// Real declaration, graph and project serialization tests. No alternate engine
// or synthetic plugin implementation is used to stand in for production code.
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QCborValue>
#include "core/project.h"

class CoreTest : public QObject {
    Q_OBJECT
private slots:
    void declarations();
    void typedValues();
    void projectRoundTrip();
    void graphValidation();
    void rejectPreviousProjectFormats();
};

void CoreTest::declarations()
{
    corig::Catalog catalog;
    QCOMPARE(catalog.describe().size(),78);
    QVERIFY(!catalog.search("正弦").isEmpty());
    QVERIFY(!catalog.search("lowpass").isEmpty());
    const auto config = catalog.checkedConfig("source.constant", {{"values",QVariantMap{{"x",3.}, {"y",4.}}}});
    const auto ports = catalog.ports("source.constant", config);
    QCOMPARE(ports.size(), 2);
    QVERIFY_EXCEPTION_THROWN(catalog.checkedConfig("source.constant", {{"values",QVariantMap{{"x","abc"}}}}), corig::Error);
    QVERIFY_EXCEPTION_THROWN(catalog.checkedConfig("source.time", {{"period_ms",-1.}}), corig::Error);
    QVERIFY_EXCEPTION_THROWN(catalog.checkedConfig("source.time", {{"unknown",1.}}), corig::Error);
}

void CoreTest::typedValues()
{
    const QByteArray bytes("\0\xff\x41", 3);
    QCOMPARE(corig::fromJsonValue(corig::toJsonValue(bytes)).toByteArray(), bytes);
    QVERIFY(corig::valueMatches(bytes, "bytes"));
    QVERIFY(!corig::valueMatches(bytes, "number"));
    QVERIFY(!corig::valueMatches(true, "number"));
    QCOMPARE(corig::sampleFrames(QVariantMap{{"time",.01}, {"values",QVariantMap{{"a",2.}}}}).size(), 1);
    QVERIFY_EXCEPTION_THROWN(corig::sampleFrames(QVariantMap{{"values",QVariantMap{}}}), corig::Error);
    QCOMPARE(corig::timeUnit("ms"), .001);
}

void CoreTest::projectRoundTrip()
{
    QTemporaryDir folder;
    auto project = corig::emptyProject("便携测试");
    corig::Catalog catalog;
    corig::validateProject(project, catalog);
    const auto original = folder.filePath("资源.bin");
    QFile source(original); QVERIFY(source.open(QIODevice::WriteOnly)); source.write("payload\0binary", 14); source.close();
    const auto ref = corig::importResource(project, original);
    QVERIFY(ref.startsWith("asset://"));
    QVERIFY(QFile::remove(original));
    const auto path = folder.filePath("test.corig");
    corig::saveProject(path, project);
    const auto restored = corig::loadProject(path);
    QCOMPARE(restored.value("name").toString(), QString("便携测试"));
    QCOMPARE(corig::resourceBytes(restored, ref), QByteArray("payload\0binary",14));
    corig::validateProject(restored, catalog);
    QFile corrupt(path); QVERIFY(corrupt.open(QIODevice::Append)); corrupt.write("irrelevant trailing bytes"); corrupt.close();
    // Compression handles its own end marker, while the design still validates.
    corig::validateProject(corig::loadProject(path), catalog);
}

void CoreTest::graphValidation()
{
    corig::Catalog catalog;
    auto p = corig::emptyProject();
    QVariantMap a{{"id","a"}, {"plugin","source.constant"}, {"config",QVariantMap{{"values",QVariantMap{{"x",2.}}}}}, {"position",QVariantList{0.,0.}}};
    QVariantMap b{{"id","b"}, {"plugin","function.sin"}, {"config",QVariantMap{}}, {"position",QVariantList{200.,0.}}};
    p["nodes"] = QVariantList{a,b};
    QVariantMap wire{{"id","w"}, {"source",QVariantList{"a","x"}}, {"target",QVariantList{"b","x"}}};
    p["wires"] = QVariantList{wire};
    corig::validateProject(p, catalog);
    QCOMPARE(corig::executionOrder(p, catalog), QVector<QString>({"a","b"}));
    auto duplicate = wire; duplicate["id"] = "w2"; p["wires"] = QVariantList{wire,duplicate};
    QVERIFY_EXCEPTION_THROWN(corig::validateProject(p, catalog), corig::Error);
    p["wires"] = QVariantList{};
    a["plugin"] = "function.sin"; a["config"] = QVariantMap{}; p["nodes"] = QVariantList{a,b};
    p["wires"] = QVariantList{QVariantMap{{"id","ab"}, {"source",QVariantList{"a","out"}}, {"target",QVariantList{"b","x"}}},
                              QVariantMap{{"id","ba"}, {"source",QVariantList{"b","out"}}, {"target",QVariantList{"a","x"}}}};
    QVERIFY_EXCEPTION_THROWN(corig::validateProject(p, catalog), corig::Error);
}

void CoreTest::rejectPreviousProjectFormats()
{
    QTemporaryDir folder; auto project = corig::emptyProject(); corig::Catalog catalog;
    const auto path = folder.filePath("format.corig");
    corig::saveProject(path,project);
    QFile current(path); QVERIFY(current.open(QIODevice::ReadOnly));
    QVERIFY(current.readAll().startsWith(QByteArrayLiteral("CoRig\0CPP3\n"))); current.close();
    // Current suffixes never opt an older product header into the new reader.
    const auto previous = QByteArrayLiteral("COMTool-x\0CPP3\n") + qCompress(QCborValue::fromVariant(project).toCbor(),9);
    QFile oldFile(path); QVERIFY(oldFile.open(QIODevice::WriteOnly)); oldFile.write(previous); oldFile.close();
    QVERIFY_THROWS_EXCEPTION(corig::Error,corig::loadProject(path));
    project["format"] = "comtool-x-native";
    QVERIFY_THROWS_EXCEPTION(corig::Error,corig::validateProject(project,catalog));

    // Old canvas and control schemas are rejected rather than migrated.
    project = corig::emptyProject();
    auto ui = project.value("ui").toMap(); auto items = ui.value("items").toList();
    auto root = items[0].toMap(); root["plugin"] = "layout.row"; items[0] = root;
    ui["items"] = items; project["ui"] = ui;
    QVERIFY_THROWS_EXCEPTION(corig::Error,corig::validateProject(project,catalog));
    project = corig::emptyProject();
    project["nodes"] = QVariantList{QVariantMap{{"id","button"},{"plugin","control.button"},{"position",QVariantList{0.,0.}},
        {"config",QVariantMap{{"payload","previous content"}}}}};
    QVERIFY_THROWS_EXCEPTION(corig::Error,corig::validateProject(project,catalog));
    project["nodes"] = QVariantList{QVariantMap{{"id","fork"},{"plugin","data.fork"},{"position",QVariantList{0.,0.}},
        {"config",QVariantMap{{"data_type","trigger"}}}}};
    QVERIFY_THROWS_EXCEPTION(corig::Error,corig::validateProject(project,catalog));
}

QTEST_GUILESS_MAIN(CoreTest)
#include "core_test.moc"
