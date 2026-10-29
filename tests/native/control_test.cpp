// Exercise production boolean nodes and the shared engine scheduler, including
// repeated events, external restarts and a real global pause/resume interval.
#include <QtTest>
#include <QSignalSpy>
#include <QElapsedTimer>
#include "core/engine.h"
#include "core/project.h"

static QVariantMap design(const QString &id, const QString &kind, const QVariantMap &config = {})
{
    return {{"id",id},{"plugin",kind},{"position",QVariantList{0.,0.}},{"config",config}};
}

static QVariant call(cx::Engine &engine, const QString &operation, const QVariantMap &arguments = {})
{
    QSignalSpy completions(&engine,&cx::Engine::completed);
    const auto id = engine.submit(operation,arguments);
    QElapsedTimer timeout; timeout.start();
    while (timeout.elapsed() < 5000) {
        for (const auto &signal : completions) if (signal[0].toULongLong() == id) {
            if (!signal[2].toString().isEmpty()) { QTest::qFail(qPrintable(signal[2].toString()),__FILE__,__LINE__); return {}; }
            return signal[1];
        }
        QTest::qWait(1);
    }
    QTest::qFail("Control engine operation timed out",__FILE__,__LINE__); return {};
}

class ControlTest final : public QObject {
    Q_OBJECT
private slots:
    void truthTablesAndRepeatedUpdates();
    void selectedEdges();
    void distributionLimitsAndButton();
    void countdownRestartAndStop();
    void externalTimerFollowsEngine();
};

void ControlTest::truthTablesAndRepeatedUpdates()
{
    cx::Catalog catalog;
    for (const auto &op : {"and","or","not","nand","nor","xor","xnor"}) for (int bits = 0; bits < 4; ++bits) {
        const bool a = bits & 1, b = bits & 2;
        const QString name(op);
        const bool expected = name == "and" ? a && b : name == "or" ? a || b : name == "not" ? !a : name == "nand" ? !(a && b) : name == "nor" ? !(a || b) : name == "xor" ? a != b : a == b;
        auto gate = cx::makeNode(design("gate","logic." + name),catalog,{});
        gate->setConnections(name == "not" ? QSet<QString>{"a"} : QSet<QString>{"a","b"});
        const cx::Values inputs = name == "not" ? cx::Values{{"a",a}} : cx::Values{{"a",a},{"b",b}};
        QCOMPARE(gate->execute(inputs,{},{}).emissions.front().value("out").toBool(),expected);
        QCOMPARE(gate->execute(inputs,{},{}).emissions.size(),1);
        QVERIFY(gate->execute({},QSet<QString>{"a"},{}).disconnected.contains("out"));
    }
}

void ControlTest::selectedEdges()
{
    cx::Catalog catalog;
    for (const auto &mode : {"rising","falling","both"}) {
        auto edge = cx::makeNode(design("edge","logic.edge",{{"edge",mode}}),catalog,{});
        edge->setConnections({"in"}); int count = 0;
        for (const bool value : {false,true,true,false,false,true,false}) {
            const auto result = edge->execute({{"in",value}}, {},{});
            count += result.emissions.size();
            for (const auto &output : result.emissions) QVERIFY(output.value("out").toBool());
        }
        QCOMPARE(count,QString(mode) == "both" ? 4 : 2);
    }
}

void ControlTest::distributionLimitsAndButton()
{
    cx::Catalog catalog;
    QVariantList outputs; for (const auto &name : {"a","b","c","d","e","f","g","h"}) outputs.append(name);
    auto fork = cx::makeNode(design("fork","data.fork",{{"data_type","boolean"},{"outputs",outputs}}),catalog,{});
    fork->setConnections({"in"});
    const auto result = fork->execute({{"in",true}}, {},{});
    QCOMPARE(result.emissions.front().size(),8);
    for (const auto &name : outputs) QVERIFY(result.emissions.front().value(name.toString()).toBool());
    QVERIFY_THROWS_EXCEPTION(cx::Error,catalog.checkedConfig("data.fork",{{"outputs",QVariantList{"a"}}}));
    outputs.append("i"); QVERIFY_THROWS_EXCEPTION(cx::Error,catalog.checkedConfig("data.fork",{{"outputs",outputs}}));
    QVERIFY_THROWS_EXCEPTION(cx::Error,catalog.checkedConfig("data.fork",{{"data_type","trigger"}}));
    auto button = cx::makeNode(design("button","control.button"),catalog,{});
    QVariant actionResult;
    for (int click = 0; click < 2; ++click) {
        const auto event = button->action("click",{}, {},actionResult);
        QCOMPARE(event.emissions.size(),1); QCOMPARE(event.emissions.front().value("trigger").metaType().id(),int(QMetaType::Bool));
        QVERIFY(event.emissions.front().value("trigger").toBool());
    }
    QVERIFY_THROWS_EXCEPTION(cx::Error,catalog.checkedConfig("control.button",{{"payload","obsolete"}}));
}

void ControlTest::countdownRestartAndStop()
{
    cx::Catalog catalog;
    auto timer = cx::makeNode(design("timer","source.timer",{{"mode","countdown"},{"interval_ms",1000}}),catalog,{});
    timer->setConnections({"start","stop"});
    timer->execute({{"start",true}}, {},{1,0.});
    timer->execute({{"start",true}}, {},{2,.7});
    QVERIFY(timer->execute({}, {},{3,1.},true).emissions.isEmpty());
    const auto completed = timer->execute({}, {},{4,1.7},true);
    QCOMPARE(completed.emissions.size(),1); QVERIFY(completed.emissions.front().value("tick").toBool());
    QVERIFY(timer->sourceInterval() < 0.);
    QVERIFY(timer->execute({}, {},{5,5.},true).emissions.isEmpty());
    timer->execute({{"start",true},{"stop",true}}, {},{6,6.});
    QVERIFY(timer->sourceInterval() < 0.);
    timer->execute({{"start",false}}, {},{7,7.}); QVERIFY(timer->sourceInterval() < 0.);
}

void ControlTest::externalTimerFollowsEngine()
{
    auto project = cx::emptyProject();
    project["nodes"] = QVariantList{design("button","control.button"),design("timer","source.timer",{{"mode","countdown"},{"interval_ms",180}})};
    project["wires"] = QVariantList{QVariantMap{{"id","start"},{"source",QVariantList{"button","trigger"}},{"target",QVariantList{"timer","start"}}}};
    cx::Engine engine({}); call(engine,"project.apply",{{"project",project}}); call(engine,"runtime.start");
    call(engine,"node.action",{{"node","button"},{"action","click"}}); QTest::qWait(70);
    call(engine,"node.action",{{"node","button"},{"action","click"}}); QTest::qWait(90);
    QCOMPARE(call(engine,"node.state",{{"node","timer"}}).toMap().value("state").toMap().value("ticks").toULongLong(),0ull);
    call(engine,"runtime.pause"); const auto frozen = engine.observation().value("elapsed").toDouble(); QTest::qWait(220);
    QCOMPARE(engine.observation().value("elapsed").toDouble(),frozen);
    QCOMPARE(call(engine,"node.state",{{"node","timer"}}).toMap().value("state").toMap().value("ticks").toULongLong(),0ull);
    call(engine,"runtime.start");
    QTRY_COMPARE_WITH_TIMEOUT(engine.observation().value("nodes").toMap().value("timer").toMap().value("state").toMap().value("ticks").toULongLong(),1ull,1000);
    call(engine,"runtime.reset");
    QVERIFY(!call(engine,"node.state",{{"node","timer"}}).toMap().value("state").toMap().value("enabled").toBool());
}

QTEST_GUILESS_MAIN(ControlTest)
#include "control_test.moc"
