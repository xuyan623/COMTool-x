// Use production recording files, nodes and engine lifecycle; no replacement
// runtime or synthetic storage stands in for the actual implementation.
#include <QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QElapsedTimer>
#include "core/engine.h"
#include "core/project.h"
#include "core/recording.h"
#include "app/model_commands.h"

static QVariantMap design(const QString &id, const QString &kind, const QVariantMap &config = {})
{
    return {{"id",id},{"plugin",kind},{"position",QVariantList{0.,0.}},{"config",config}};
}

static QVariant call(cx::Engine &engine, const QString &operation, const QVariantMap &args = {})
{
    QSignalSpy completions(&engine,&cx::Engine::completed); const auto id = engine.submit(operation,args);
    QElapsedTimer timeout; timeout.start();
    while (timeout.elapsed() < 5000) {
        for (const auto &entry : completions) if (entry[0].toULongLong() == id) {
            if (!entry[2].toString().isEmpty()) { QTest::qFail(qPrintable(entry[2].toString()),__FILE__,__LINE__); return {}; }
            return entry[1];
        }
        QTest::qWait(1);
    }
    QTest::qFail("Recording engine request timed out",__FILE__,__LINE__); return {};
}

class RecordingTest final : public QObject {
    Q_OBJECT
private slots:
    void typedFilesAndRandomAccess();
    void damagedFilesAndAtomicSave();
    void recorderPauseAndPassthrough();
    void playbackSelectionSeekAndSpeed();
    void designContainsOnlyExternalReference();
    void engineResetOwnsAllRuntimeState();
    void rawByteLoggingRemainsIndependent();
    void globalPauseInterruptsImport();
};

void RecordingTest::typedFilesAndRandomAccess()
{
    QTemporaryDir dir;
    const QList<QPair<QString,QVariant>> values{{"number",3.125},{"boolean",true},{"string",QString::fromUtf8("原始信号")},
        {"bytes",QByteArray("\0\xff\x41",3)},{"object",QVariantMap{{"a",42},{"text","original"}}},
        {"samples",QVariantMap{{"time",123.456},{"values",QVariantMap{{"angle",.75}}}}}};
    for (const auto &[type,value] : values) {
        cx::RecordingWriter writer; writer.begin(type); writer.append(.1,value); writer.append(.3,value);
        const auto path = dir.filePath(type + ".comtoolrec"); writer.save(path,.5);
        cx::RecordingReader reader(path);
        QCOMPARE(reader.dataType(),type); QCOMPARE(reader.count(),2); QCOMPARE(reader.duration(),.5);
        QCOMPARE(reader.valueAt(0),value); QCOMPARE(reader.valueAt(1),value);
        QCOMPARE(reader.indexAt(0.),0); QCOMPARE(reader.indexAt(.2),0); QCOMPARE(reader.indexAt(.5),1);
        QCOMPARE(reader.timeAt(1),.3);
    }
}

void RecordingTest::damagedFilesAndAtomicSave()
{
    QTemporaryDir dir; const auto path = dir.filePath("capture.comtoolrec");
    cx::RecordingWriter writer; writer.begin("number"); writer.append(0.,1.); writer.append(.1,2.); writer.save(path,.2);
    QFile original(path); QVERIFY(original.open(QIODevice::ReadOnly)); const auto bytes = original.readAll(); original.close();
    for (const auto &bad : {bytes.left(bytes.size() - 5),bytes + QByteArray("extra"),QByteArray("invalid")}) {
        const auto broken = dir.filePath("broken.comtoolrec"); QFile file(broken); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(bad); file.close();
        QVERIFY_THROWS_EXCEPTION(cx::Error,cx::RecordingReader{broken});
    }
    QVERIFY_THROWS_EXCEPTION(cx::Error,writer.save(path,.2,[] { return true; }));
    QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(),bytes); original.close();
    QCOMPARE(writer.count(),2ull);
    QVERIFY_THROWS_EXCEPTION(cx::Error,writer.append(.05,3.));
    const auto recovery = dir.filePath("recovery.comtoolrec"); writer.save(recovery,.2);
    QCOMPARE(cx::RecordingReader(recovery).count(),2);
}

void RecordingTest::recorderPauseAndPassthrough()
{
    QTemporaryDir dir; cx::Catalog catalog;
    auto recorder = cx::makeNode(design("rec","data.record",{{"data_type","number"}}),catalog,{});
    recorder->setConnections({"in","start","pause"}); QVariant result;
    recorder->action("start",{}, {1,0.},result);
    QCOMPARE(recorder->execute({{"in",1.}}, {},{2,.1}).emissions.front().value("out").toDouble(),1.);
    recorder->action("pause",{}, {3,.2},result);
    QCOMPARE(recorder->execute({{"in",2.}}, {},{4,5.}).emissions.front().value("out").toDouble(),2.);
    recorder->execute({{"start",true}}, {},{5,10.2});
    recorder->execute({{"in",3.}}, {},{6,10.3});
    const auto path = dir.filePath("joined.comtoolrec"); recorder->action("save",{{"path",path}}, {7,10.4},result);
    cx::RecordingReader reader(path);
    QCOMPARE(reader.count(),2); QCOMPARE(reader.valueAt(0).toDouble(),1.); QCOMPARE(reader.valueAt(1).toDouble(),3.);
    QCOMPARE(reader.timeAt(0),.1); QCOMPARE(reader.timeAt(1),.3); QCOMPARE(reader.duration(),.4);
    recorder->action("reset",{}, {8,11.},result);
    QCOMPARE(recorder->snapshot().value("state").toMap().value("records").toULongLong(),0ull);
    QVERIFY(QFile::exists(path));
}

void RecordingTest::playbackSelectionSeekAndSpeed()
{
    QTemporaryDir dir; cx::Catalog catalog; const auto path = dir.filePath("play.comtoolrec");
    cx::RecordingWriter writer; writer.begin("number"); writer.append(.1,10.); writer.append(.3,30.); writer.save(path,.4);
    auto player = cx::makeNode(design("play","data.playback",{{"data_type","number"},{"path",path}}),catalog,{});
    player->setConnections({"in"}); QVariant result;
    QCOMPARE(player->execute({{"in",99.}}, {},{1,0.}).emissions.front().value("out").toDouble(),99.);
    QVERIFY(player->action("seek",{{"position",.3}}, {2,0.},result).emissions.isEmpty());
    QVERIFY(player->action("cutoff",{{"value",true}}, {3,0.},result).emissions.isEmpty());
    QVERIFY(player->sourceInterval() < 0.);
    QCOMPARE(player->action("seek",{{"position",.1}}, {4,0.},result).emissions.front().value("out").toDouble(),10.);
    QVERIFY(player->execute({{"in",88.}}, {},{5,0.}).emissions.isEmpty());
    QCOMPARE(player->action("step",{{"direction",1}}, {6,0.},result).emissions.front().value("out").toDouble(),30.);
    player->action("seek",{{"position",0.}}, {7,0.},result);
    player->action("set_speed",{{"value",2.}}, {8,0.},result); player->action("play",{}, {9,1.},result);
    QCOMPARE(player->execute({}, {},{10,1.15},true).emissions.front().value("out").toDouble(),30.);
    player->execute({}, {},{11,1.2},true);
    QVERIFY(!player->snapshot().value("state").toMap().value("playing").toBool());
    QVERIFY(player->snapshot().value("state").toMap().value("cutoff").toBool());
    player->action("cutoff",{{"value",false}}, {12,1.2},result);
    QCOMPARE(player->execute({{"in",77.}}, {},{13,1.2}).emissions.front().value("out").toDouble(),77.);
    const auto before = player->snapshot().value("state");
    QVERIFY_THROWS_EXCEPTION(cx::Error,player->action("load",{{"path",dir.filePath("missing")}}, {14,1.2},result));
    QCOMPARE(player->snapshot().value("state"),before);
}

void RecordingTest::designContainsOnlyExternalReference()
{
    QTemporaryDir dir; cx::Catalog catalog; const auto path = dir.filePath("external.comtoolrec");
    cx::RecordingWriter writer; writer.begin("number"); writer.append(0.,1.); writer.save(path,.1);
    const auto edited = cx::editProject(cx::emptyProject(),catalog,"graph.node.add",{{"id","play"},{"plugin","data.playback"},{"config",QVariantMap{{"data_type","number"},{"path",QUrl::fromLocalFile(path).toString()}}}});
    QVERIFY(edited.project.value("resources").toMap().isEmpty());
    QCOMPARE(edited.project.value("nodes").toList().front().toMap().value("config").toMap().value("path").toString(),path);
    const auto projectPath = dir.filePath("design.comtoolx"); cx::saveProject(projectPath,edited.project);
    QVERIFY(QFile::remove(path));
    const auto restored = cx::loadProject(projectPath); cx::validateProject(restored,catalog);
    QVERIFY(restored.value("resources").toMap().isEmpty());
    auto player = cx::makeNode(restored.value("nodes").toList().front().toMap(),catalog,{});
    player->setConnections({"in"});
    QCOMPARE(player->execute({{"in",5.}}, {},{}).emissions.front().value("out").toDouble(),5.);
}

void RecordingTest::engineResetOwnsAllRuntimeState()
{
    QTemporaryDir dir; const auto defaultPath = dir.filePath("default.comtoolrec"), runtimePath = dir.filePath("runtime.comtoolrec");
    cx::RecordingWriter writer; writer.begin("number"); writer.append(0.,1.); writer.append(.1,2.); writer.save(defaultPath,.2); writer.save(runtimePath,.2);
    auto project = cx::emptyProject();
    project["nodes"] = QVariantList{design("source","control.number"),design("rec","data.record",{{"data_type","number"}}),design("play","data.playback",{{"data_type","number"},{"path",defaultPath}})};
    project["wires"] = QVariantList{QVariantMap{{"id","a"},{"source",QVariantList{"source","value"}},{"target",QVariantList{"rec","in"}}},QVariantMap{{"id","b"},{"source",QVariantList{"rec","out"}},{"target",QVariantList{"play","in"}}}};
    cx::Engine engine({}); call(engine,"project.apply",{{"project",project}}); call(engine,"runtime.start");
    call(engine,"node.action",{{"node","rec"},{"action","start"}});
    call(engine,"runtime.emit",{{"node","source"},{"outputs",QVariantMap{{"value",42.}}}});
    QVERIFY(call(engine,"node.state",{{"node","rec"}}).toMap().value("state").toMap().value("records").toULongLong() > 0);
    call(engine,"node.action",{{"node","play"},{"action","load"},{"args",QVariantMap{{"path",runtimePath}}}});
    call(engine,"node.action",{{"node","play"},{"action","cutoff"},{"args",QVariantMap{{"value",true}}}});
    call(engine,"runtime.reset");
    QCOMPARE(call(engine,"node.state",{{"node","rec"}}).toMap().value("state").toMap().value("records").toULongLong(),0ull);
    const auto restored = call(engine,"node.state",{{"node","play"}}).toMap().value("state").toMap();
    QCOMPARE(restored.value("path").toString(),defaultPath); QVERIFY(!restored.value("cutoff").toBool()); QVERIFY(!restored.value("playing").toBool());
    auto temporaryOnly = design("temp","data.playback",{{"data_type","number"}}); project["nodes"] = QVariantList{temporaryOnly}; project["wires"] = QVariantList{};
    call(engine,"project.replace",{{"project",project}});
    call(engine,"node.action",{{"node","temp"},{"action","load"},{"args",QVariantMap{{"path",runtimePath}}}});
    call(engine,"runtime.reset");
    QVERIFY(!call(engine,"node.state",{{"node","temp"}}).toMap().value("state").toMap().value("loaded").toBool());
}

void RecordingTest::rawByteLoggingRemainsIndependent()
{
    QTemporaryDir dir; cx::Catalog catalog; const auto path = dir.filePath("raw.bin");
    auto logger = cx::makeNode(design("raw","data.recorder",{{"path",path},{"append",false}}),catalog,{});
    logger->setConnections({"bytes"}); const QByteArray bytes("\0\xff\x41",3);
    QCOMPARE(logger->execute({{"bytes",bytes}}, {},{}).emissions.front().value("through").toByteArray(),bytes);
    logger->stop(); QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(),bytes);
}

void RecordingTest::globalPauseInterruptsImport()
{
    QTemporaryDir dir; const auto path = dir.filePath("interrupt.comtoolrec");
    cx::RecordingWriter writer; writer.begin("number");
    for (int i = 0; i < 20000; ++i) writer.append(i * .001,double(i));
    writer.save(path,20.);
    auto project = cx::emptyProject(); project["nodes"] = QVariantList{design("play","data.playback",{{"data_type","number"}})};
    cx::Engine engine({}); call(engine,"project.apply",{{"project",project}}); call(engine,"runtime.start");
    QSignalSpy completions(&engine,&cx::Engine::completed);
    const auto loading = engine.submit("node.action",{{"node","play"},{"action","load"},{"args",QVariantMap{{"path",path}}}});
    const auto pausing = engine.submit("runtime.pause");
    QTRY_VERIFY_WITH_TIMEOUT(completions.size() >= 2,2000);
    bool interrupted = false, paused = false;
    for (const auto &entry : completions) {
        if (entry[0].toULongLong() == loading) interrupted = entry[2].toString().contains("中断");
        if (entry[0].toULongLong() == pausing) paused = entry[2].toString().isEmpty();
    }
    QVERIFY(interrupted); QVERIFY(paused); QVERIFY(!engine.observation().value("running").toBool());
    QVERIFY(!call(engine,"node.state",{{"node","play"}}).toMap().value("state").toMap().value("loaded").toBool());
}

QTEST_GUILESS_MAIN(RecordingTest)
#include "recording_test.moc"
