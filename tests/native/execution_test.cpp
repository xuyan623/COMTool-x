// Exercise the real compiled graph, codecs, numeric buffers, terminal and local
// Python worker through their public interfaces. No mock runtime substitutes.
#include <QtTest>
#include <QSignalSpy>
#include <QElapsedTimer>
#include <cmath>
#include <numbers>
#include "core/engine.h"
#include "core/project.h"
#include "core/series.h"
#include "core/mesh.h"
#include "core/python_host.h"
#include "plugins/terminal_screen.h"

static QVariantMap node(const QString &id, const QString &plugin, const QVariantMap &config = {})
{
    return {{"id",id},{"plugin",plugin},{"position",QVariantList{0.,0.}},{"config",config}};
}

static QVariantMap wire(const QString &id, const QString &a, const QString &ap, const QString &b, const QString &bp)
{
    return {{"id",id},{"source",QVariantList{a,ap}},{"target",QVariantList{b,bp}}};
}

static QVariant request(cx::Engine &engine, const QString &operation, const QVariantMap &args = {})
{
    QSignalSpy spy(&engine,&cx::Engine::completed);
    const auto id = engine.submit(operation,args);
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < 5000) {
        for (const auto &entry : spy) if (entry[0].toULongLong() == id) {
            if (!entry[2].toString().isEmpty()) { QTest::qFail(qPrintable(entry[2].toString()),__FILE__,__LINE__); return {}; }
            return entry[1];
        }
        QTest::qWait(1);
    }
    QTest::qFail("Native engine request timed out",__FILE__,__LINE__); return {};
}

class ExecutionTest : public QObject {
    Q_OBJECT
private slots:
    void nativeCatalogConstructs();
    void coherentBranchesAndSteps();
    void disconnectedReadySourceSleeps();
    void historyAndExpression();
    void chunkedProtocol();
    void ringsAndFft();
    void geometryAndRotations();
    void terminal();
    void localPython();
};

void ExecutionTest::nativeCatalogConstructs()
{
    cx::Catalog catalog;
    for (const auto &entry : catalog.describe()) {
        const auto spec = entry.toMap();
        const auto instance = cx::makeNode(node("test",spec.value("id").toString()),catalog,{});
        QVERIFY2(instance != nullptr,qPrintable(spec.value("id").toString()));
        QCOMPARE(instance->kind(),spec.value("id").toString());
    }
}

void ExecutionTest::coherentBranchesAndSteps()
{
    auto project = cx::emptyProject();
    project["nodes"] = QVariantList{node("source","source.constant",{{"values",QVariantMap{{"a",.3},{"b",.3}}}}),node("sin","function.sin"),node("cos","function.cos"),
        node("sum","function.add",{{"inputs",QVariantList{"a","b"}}}),node("plot","plot.curves",{{"inputs",QVariantList{"value"}}})};
    project["wires"] = QVariantList{wire("a","source","a","sin","x"),wire("b","source","b","cos","x"),wire("c","sin","out","sum","a"),wire("d","cos","out","sum","b"),wire("e","sum","out","plot","value")};
    cx::Engine engine({}); request(engine,"project.apply",{{"project",project}}); request(engine,"runtime.start");
    QTRY_VERIFY_WITH_TIMEOUT(engine.observation().value("processed").toULongLong() > 1000,3000);
    request(engine,"runtime.pause");
    const auto state = engine.observation();
    QCOMPARE(state.value("failed").toULongLong(),0ull);
    const auto nodes = state.value("nodes").toMap();
    const auto result = nodes.value("sum").toMap().value("outputs").toMap().value("out").toDouble();
    QVERIFY(std::abs(result - (std::sin(.3) + std::cos(.3))) < 1e-12);
    QCOMPARE(nodes.value("sum").toMap().value("executions").toULongLong(),nodes.value("source").toMap().value("executions").toULongLong());
    const auto elapsed = state.value("elapsed").toDouble(); QTest::qWait(20); QCOMPARE(engine.observation().value("elapsed").toDouble(),elapsed);
    request(engine,"runtime.reset");
    for (int index = 0; index < 5; ++index) {
        const auto step = request(engine,"runtime.step",{{"mode","node"}}).toMap();
        QCOMPARE(step.value("batch_complete").toBool(),index == 4);
    }
    QCOMPARE(engine.observation().value("processed").toULongLong(),5ull);
    const auto logs = request(engine,"log.query",{{"limit",200}}).toMap().value("records").toList();
    QVERIFY(std::count_if(logs.cbegin(),logs.cend(),[](const QVariant &v) { return v.toMap().value("message") == "节点执行完成"; }) >= 5);
    request(engine,"runtime.stop");
}

void ExecutionTest::disconnectedReadySourceSleeps()
{
    auto p = cx::emptyProject(); p["nodes"] = QVariantList{node("constant","source.constant")};
    cx::Engine engine({}); request(engine,"project.apply",{{"project",p}}); request(engine,"runtime.start");
    QTest::qWait(80); QCOMPARE(engine.observation().value("processed").toULongLong(),0ull); request(engine,"runtime.stop");
}

void ExecutionTest::historyAndExpression()
{
    cx::Catalog catalog;
    auto integral = cx::makeNode(node("i","function.integral"),catalog,{});
    integral->setConnections({"x","t"});
    integral->execute({{"x",2.},{"t",0.}}, {},{1,0.,false,true});
    integral->execute({{"x",2.},{"t",1.}}, {},{2,1.,false,false});
    QCOMPARE(integral->outputs().value("out").toDouble(),2.);
    integral->pause();
    integral->execute({{"x",2.},{"t",5.}}, {},{3,1.,true,false});
    QCOMPARE(integral->outputs().value("out").toDouble(),2.);
    integral->execute({{"x",2.},{"t",6.}}, {},{4,2.,true,false});
    QCOMPARE(integral->outputs().value("out").toDouble(),4.);
    auto expression = cx::makeNode(node("f","function.expression",{{"inputs",QVariantList{"x","y"}},{"outputs",QVariantMap{{"a","x+y"},{"b","x*y"}}}}),catalog,{});
    const auto result = expression->execute({{"x",2.},{"y",3.}}, {},{});
    QCOMPARE(expression->outputs().value("a").toDouble(),5.); QCOMPARE(expression->outputs().value("b").toDouble(),6.);
    QCOMPARE(result.emissions.size(),1);
    QVERIFY_EXCEPTION_THROWN(cx::makeNode(node("f","function.expression",{{"outputs",QVariantMap{{"out","x.constructor"}}}}),catalog,{}),cx::Error);
}

void ExecutionTest::chunkedProtocol()
{
    cx::Catalog catalog;
    auto decoder = cx::makeNode(node("decoder","protocol.decode",{{"format","firewater"},{"sample_rate_hz",100.}}),catalog,{});
    const auto first = decoder->execute({{"bytes",QByteArray("1,")}}, {},{}); QVERIFY(first.emissions.isEmpty());
    const auto second = decoder->execute({{"bytes",QByteArray("2\n3,4\n")}}, {},{});
    QCOMPARE(second.emissions.size(),2); QVERIFY(second.separateFrames);
    QCOMPARE(second.emissions[1].value("samples").toMap().value("time").toDouble(),.01);
    QCOMPARE(second.emissions[1].value("samples").toMap().value("values").toMap().value("CH1").toDouble(),4.);
    decoder->execute({{"bytes",QByteArray("5,")}}, {},{}); decoder->pause();
    QCOMPARE(decoder->snapshot().value("state").toMap().value("buffered_bytes").toInt(),0);
    const auto bad = decoder->execute({{"bytes",QByteArray("bad\n9,10\n")}}, {},{});
    QCOMPARE(bad.emissions.size(),1);
    QCOMPARE(decoder->snapshot().value("state").toMap().value("bad_frames").toInt(),1);
}

void ExecutionTest::ringsAndFft()
{
    cx::Series ring(10); for (int i = 0; i < 20; ++i) ring.append(i,i == 14 ? 100. : i);
    QCOMPARE(ring.size(),10); QCOMPARE(ring.total(),20ull); QCOMPARE(ring.points()[0].x(),10.);
    const auto display = ring.points(10.,19.,4);
    QVERIFY(std::any_of(display.cbegin(),display.cend(),[](const QPointF &p) { return p.y() == 100.; }));
    const auto pairs = cx::describePoints(display);
    QCOMPARE(pairs.size(),display.size()); QCOMPARE(pairs[0].toList().size(),2);
    const auto apiPairs = cx::toJsonValue(QVariant::fromValue(display)).toList();
    QCOMPARE(apiPairs,pairs);
    ring.resize(4); QCOMPARE(ring.size(),4); QCOMPARE(ring.total(),20ull); QCOMPARE(ring.points()[0].x(),16.);
    QVector<QPointF> wave;
    for (int i = 0; i < 1024; ++i) wave.append({i / 1024.,std::sin(2 * std::numbers::pi * 32 * i / 1024.)});
    const auto fft = cx::spectrum(wave);
    const auto peak = *std::max_element(fft.cbegin(),fft.cend(),[](const auto &a, const auto &b) { return a.y() < b.y(); });
    QVERIFY(std::abs(peak.x() - 32.) < .001); QVERIFY(std::abs(peak.y() - 1.) < .005);
}

void ExecutionTest::geometryAndRotations()
{
    const auto plane = cx::airplane(); QVERIFY(plane->vertices.size() > 8); QVERIFY(plane->radius > 0.);
    const auto mesh = cx::loadMesh("v 0 0 0\nv 2 0 0\nv 2 2 0\nv 1 1 0\nv 0 2 0\nf 1 2 3 4 5\n","concave.obj");
    QCOMPARE(mesh->triangles.size(),3);
    for (const auto &order : {"XYZ","XZY","YXZ","YZX","ZXY","ZYX"}) {
        const auto matrix = cx::eulerMatrix(.2,.3,.4,order,"intrinsic");
        const auto rows = cx::describeMatrix(matrix);
        QCOMPARE(rows.size(),3); QCOMPARE(rows[0].toList().size(),3);
        QCOMPARE(rows[1].toList()[2].toFloat(),matrix(1,2));
        const auto identity = matrix * matrix.transposed();
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) QVERIFY(std::abs(identity(r,c) - (r == c ? 1.f : 0.f)) < 1e-5f);
    }
    const auto a = cx::checkedQuaternion({1.,2.,3.,4.}).toRotationMatrix(), b = cx::checkedQuaternion({-1.,-2.,-3.,-4.}).toRotationMatrix();
    QCOMPARE(a,b); QVERIFY_EXCEPTION_THROWN(cx::checkedQuaternion({0.,0.,0.,0.}),cx::Error);
}

void ExecutionTest::terminal()
{
    cx::TerminalScreen terminal(20,4,100);
    terminal.feed("hello\r\n\x1b[31mred\x1b[0m");
    const auto screen = terminal.describe(true);
    QCOMPARE(screen.value("lines").toStringList()[0],QString("hello"));
    QCOMPARE(screen.value("lines").toStringList()[1],QString("red"));
    QVERIFY(screen.value("html").toString().contains("#cf6067"));
    terminal.feed("\x1b[2;1H中文");
    QVERIFY(terminal.describe(false).value("lines").toStringList()[1].startsWith("中文"));
    for (int i = 0; i < 10; ++i) terminal.feed("\r\nline");
    QVERIFY(terminal.history(0,100,false).value("total").toInt() > 4);
    terminal.resize(30,5); QCOMPARE(terminal.columns(),30);
}

void ExecutionTest::localPython()
{
    const auto path = qEnvironmentVariable("COMTOOLX_TEST_PYTHON");
    if (path.isEmpty()) QSKIP("Set COMTOOLX_TEST_PYTHON to verify a real local interpreter");
    bool interrupted = false; cx::PythonHost python([&] { return interrupted; }); python.selectInterpreter(path);
    const auto probe = python.call({{"op","probe"},{"dependencies",QVariantList{"math","nonexistent_comtoolx_package"}}});
    QVERIFY(probe.value("modules").toMap().value("math").toMap().value("available").toBool());
    QVERIFY(!probe.value("modules").toMap().value("nonexistent_comtoolx_package").toMap().value("available").toBool());
    QVariantMap call{{"node","test"},{"code","def process(inputs,state):\n    state['n']=state.get('n',0)+1\n    print('stdout is a log')\n    return {'data':inputs['data']+b'!','n':state['n']}\n"},
        {"inputs",QVariantMap{{"data",QByteArray("hello")}}},{"state",QVariantMap{}},{"entry","process"},{"timeout_ms",1000}};
    const auto first = python.call(call); QCOMPARE(first.value("result").toMap().value("data").toByteArray(),QByteArray("hello!"));
    call["state"] = first.value("state"); const auto second = python.call(call); QCOMPARE(second.value("result").toMap().value("n").toInt(),2);
    call["code"] = "def process(inputs,state):\n    while True: pass\n"; call["timeout_ms"] = 40;
    QVERIFY_EXCEPTION_THROWN(python.call(call),cx::Error);
    call["code"] = "def process(inputs,state): return 7\n"; call["timeout_ms"] = 1000;
    QCOMPARE(python.call(call).value("result").toInt(),7); python.close();
}

QTEST_GUILESS_MAIN(ExecutionTest)
#include "execution_test.moc"
