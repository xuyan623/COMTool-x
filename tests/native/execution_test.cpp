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

static QVariant request(corig::Engine &engine, const QString &operation, const QVariantMap &args = {})
{
    QSignalSpy spy(&engine,&corig::Engine::completed);
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
    void localPythonBooleanUpdates();
    void enginePauseInterruptsPython();
    void packageIsolationAndAsyncCancellation();
};

void ExecutionTest::nativeCatalogConstructs()
{
    corig::Catalog catalog;
    for (const auto &entry : catalog.describe()) {
        const auto spec = entry.toMap();
        const auto instance = corig::makeNode(node("test",spec.value("id").toString()),catalog,{});
        QVERIFY2(instance != nullptr,qPrintable(spec.value("id").toString()));
        QCOMPARE(instance->kind(),spec.value("id").toString());
    }
}

void ExecutionTest::coherentBranchesAndSteps()
{
    auto project = corig::emptyProject();
    project["nodes"] = QVariantList{node("source","source.constant",{{"values",QVariantMap{{"a",.3},{"b",.3}}}}),node("sin","function.sin"),node("cos","function.cos"),
        node("sum","function.add",{{"inputs",QVariantList{"a","b"}}}),node("plot","plot.curves",{{"inputs",QVariantList{"value"}}})};
    project["wires"] = QVariantList{wire("a","source","a","sin","x"),wire("b","source","b","cos","x"),wire("c","sin","out","sum","a"),wire("d","cos","out","sum","b"),wire("e","sum","out","plot","value")};
    corig::Engine engine({}); request(engine,"project.apply",{{"project",project}}); request(engine,"runtime.start");
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
    auto p = corig::emptyProject(); p["nodes"] = QVariantList{node("constant","source.constant")};
    corig::Engine engine({}); request(engine,"project.apply",{{"project",p}}); request(engine,"runtime.start");
    QTest::qWait(80); QCOMPARE(engine.observation().value("processed").toULongLong(),0ull); request(engine,"runtime.stop");
}

void ExecutionTest::historyAndExpression()
{
    corig::Catalog catalog;
    auto integral = corig::makeNode(node("i","function.integral"),catalog,{});
    integral->setConnections({"x","t"});
    integral->execute({{"x",2.},{"t",0.}}, {},{1,0.,false,true});
    integral->execute({{"x",2.},{"t",1.}}, {},{2,1.,false,false});
    QCOMPARE(integral->outputs().value("out").toDouble(),2.);
    integral->pause();
    integral->execute({{"x",2.},{"t",5.}}, {},{3,1.,true,false});
    QCOMPARE(integral->outputs().value("out").toDouble(),2.);
    integral->execute({{"x",2.},{"t",6.}}, {},{4,2.,true,false});
    QCOMPARE(integral->outputs().value("out").toDouble(),4.);
    auto expression = corig::makeNode(node("f","function.expression",{{"inputs",QVariantList{"x","y"}},{"outputs",QVariantMap{{"a","x+y"},{"b","x*y"}}}}),catalog,{});
    const auto result = expression->execute({{"x",2.},{"y",3.}}, {},{});
    QCOMPARE(expression->outputs().value("a").toDouble(),5.); QCOMPARE(expression->outputs().value("b").toDouble(),6.);
    QCOMPARE(result.emissions.size(),1);
    QVERIFY_EXCEPTION_THROWN(corig::makeNode(node("f","function.expression",{{"outputs",QVariantMap{{"out","x.constructor"}}}}),catalog,{}),corig::Error);
}

void ExecutionTest::chunkedProtocol()
{
    corig::Catalog catalog;
    auto decoder = corig::makeNode(node("decoder","protocol.decode",{{"format","firewater"},{"sample_rate_hz",100.}}),catalog,{});
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
    // Renamed protocol IDs still decode real named-channel wire frames.
    auto named = corig::makeNode(node("named","protocol.decode",{{"format","named_ascii"},{"timestamp_unit","s"}}),catalog,{});
    const auto ascii = named->execute({{"bytes",QByteArray("$axis,1.5,2.25\n")}}, {},{}).emissions.front().value("samples").toMap();
    QCOMPARE(ascii.value("time").toDouble(),1.5); QCOMPARE(ascii.value("values").toMap().value("axis").toDouble(),2.25);
    auto binary = corig::makeNode(node("binary","protocol.decode",{{"format","named_binary"},{"timestamp_unit","s"}}),catalog,{});
    const auto frame = QByteArray::fromHex("aacceebb0461786973000000000000f83f000000000000024051");
    const auto decoded = binary->execute({{"bytes",frame}}, {},{}).emissions.front().value("samples").toMap();
    QCOMPARE(decoded,ascii);
}

void ExecutionTest::ringsAndFft()
{
    corig::Series ring(10); for (int i = 0; i < 20; ++i) ring.append(i,i == 14 ? 100. : i);
    QCOMPARE(ring.size(),10); QCOMPARE(ring.total(),20ull); QCOMPARE(ring.points()[0].x(),10.);
    const auto display = ring.points(10.,19.,4);
    QVERIFY(std::any_of(display.cbegin(),display.cend(),[](const QPointF &p) { return p.y() == 100.; }));
    const auto pairs = corig::describePoints(display);
    QCOMPARE(pairs.size(),display.size()); QCOMPARE(pairs[0].toList().size(),2);
    const auto apiPairs = corig::toJsonValue(QVariant::fromValue(display)).toList();
    QCOMPARE(apiPairs,pairs);
    ring.resize(4); QCOMPARE(ring.size(),4); QCOMPARE(ring.total(),20ull); QCOMPARE(ring.points()[0].x(),16.);
    QVector<QPointF> wave;
    for (int i = 0; i < 1024; ++i) wave.append({i / 1024.,std::sin(2 * std::numbers::pi * 32 * i / 1024.)});
    const auto fft = corig::spectrum(wave);
    const auto peak = *std::max_element(fft.cbegin(),fft.cend(),[](const auto &a, const auto &b) { return a.y() < b.y(); });
    QVERIFY(std::abs(peak.x() - 32.) < .001); QVERIFY(std::abs(peak.y() - 1.) < .005);
}

void ExecutionTest::geometryAndRotations()
{
    const auto plane = corig::airplane(); QVERIFY(plane->vertices.size() > 8); QVERIFY(plane->radius > 0.);
    const auto mesh = corig::loadMesh("v 0 0 0\nv 2 0 0\nv 2 2 0\nv 1 1 0\nv 0 2 0\nf 1 2 3 4 5\n","concave.obj");
    QCOMPARE(mesh->triangles.size(),3);
    for (const auto &order : {"XYZ","XZY","YXZ","YZX","ZXY","ZYX"}) {
        const auto matrix = corig::eulerMatrix(.2,.3,.4,order,"intrinsic");
        const auto rows = corig::describeMatrix(matrix);
        QCOMPARE(rows.size(),3); QCOMPARE(rows[0].toList().size(),3);
        QCOMPARE(rows[1].toList()[2].toFloat(),matrix(1,2));
        const auto identity = matrix * matrix.transposed();
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) QVERIFY(std::abs(identity(r,c) - (r == c ? 1.f : 0.f)) < 1e-5f);
    }
    const auto a = corig::checkedQuaternion({1.,2.,3.,4.}).toRotationMatrix(), b = corig::checkedQuaternion({-1.,-2.,-3.,-4.}).toRotationMatrix();
    QCOMPARE(a,b); QVERIFY_EXCEPTION_THROWN(corig::checkedQuaternion({0.,0.,0.,0.}),corig::Error);
}

void ExecutionTest::terminal()
{
    corig::TerminalScreen terminal(20,4,100);
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
    const auto path = qEnvironmentVariable("CORIG_TEST_PYTHON");
    if (path.isEmpty()) QSKIP("Set CORIG_TEST_PYTHON to verify a real local interpreter");
    bool interrupted = false; corig::PythonHost python([&] { return interrupted; }); python.selectInterpreter(path);
    const auto probe = python.call({{"op","probe"},{"dependencies",QVariantList{"math","nonexistent_corig_package"}}});
    QVERIFY(probe.value("modules").toMap().value("math").toMap().value("available").toBool());
    QVERIFY(!probe.value("modules").toMap().value("nonexistent_corig_package").toMap().value("available").toBool());
    QVariantMap call{{"node","test"},{"code","def process(inputs,state):\n    state['n']=state.get('n',0)+1\n    print('stdout is a log')\n    return {'data':inputs['data']+b'!','n':state['n']}\n"},
        {"inputs",QVariantMap{{"data",QByteArray("hello")}}},{"state",QVariantMap{}},{"entry","process"},{"timeout_ms",1000}};
    const auto first = python.call(call); QCOMPARE(first.value("result").toMap().value("data").toByteArray(),QByteArray("hello!"));
    call["state"] = first.value("state"); const auto second = python.call(call); QCOMPARE(second.value("result").toMap().value("n").toInt(),2);
    call["code"] = "def process(inputs,state):\n    while True: pass\n"; call["timeout_ms"] = 40;
    QVERIFY_EXCEPTION_THROWN(python.call(call),corig::Error);
    call["code"] = "def process(inputs,state): return 7\n"; call["timeout_ms"] = 1000;
    QCOMPARE(python.call(call).value("result").toInt(),7); python.close();
}

void ExecutionTest::localPythonBooleanUpdates()
{
    const auto path = qEnvironmentVariable("CORIG_TEST_PYTHON");
    if (path.isEmpty()) QSKIP("Set CORIG_TEST_PYTHON to verify real Python control inputs");
    corig::PythonHost python([] { return false; }); python.selectInterpreter(path);
    corig::Catalog catalog;
    catalog.addPython({{"plugin_api",3},{"id","user.control"},{"name","控制更新验证"},
        {"code","def process(inputs,state):\n    if 'start' in inputs['__context__']['changed'] and inputs['start']:\n        state['starts']=state.get('starts',0)+1\n    return {'out':state.get('starts',0)}\n"},
        {"ports",QVariantList{QVariantMap{{"id","start"},{"direction","in"},{"data_type","boolean"}},QVariantMap{{"id","value"},{"direction","in"},{"data_type","number"}},QVariantMap{{"id","out"},{"direction","out"},{"data_type","number"}}}},
        {"properties",QVariantMap{}}});
    corig::NodeHost host; host.python = [&](const QVariantMap &request) { return python.call(request); };
    auto control = corig::makeNode(node("control","user.control"),catalog,host); control->setConnections({"start","value"});
    control->execute({{"start",true},{"value",1.}}, {},{1,0.}); QCOMPARE(control->outputs().value("out").toInt(),1);
    control->execute({{"value",2.}}, {},{2,.1}); QCOMPARE(control->outputs().value("out").toInt(),1);
    control->execute({{"start",true}}, {},{3,.2}); QCOMPARE(control->outputs().value("out").toInt(),2);
    control->execute({{"start",false}}, {},{4,.3}); QCOMPARE(control->outputs().value("out").toInt(),2);
    control->execute({{"start",true}}, {},{5,.4}); QCOMPARE(control->outputs().value("out").toInt(),3);
    python.close();
}

void ExecutionTest::enginePauseInterruptsPython()
{
    const auto path = qEnvironmentVariable("CORIG_TEST_PYTHON");
    if (path.isEmpty()) QSKIP("Set CORIG_TEST_PYTHON to verify engine priority over Python work");
    corig::Engine *running = nullptr;
    corig::PythonHost python([&] { return running && running->interrupted(); }); python.selectInterpreter(path);
    corig::NodeHost host; host.python = [&](const QVariantMap &call) { return python.call(call); }; host.stopExtensions = [&] { python.close(); };
    corig::Engine engine(host); running = &engine;
    auto project = corig::emptyProject();
    project["nodes"] = QVariantList{node("source","source.constant"),node("python","function.python",{{"code","def process(inputs,state):\n    while True: pass\n"},{"timeout_ms",2000}})};
    project["wires"] = QVariantList{wire("input","source","value","python","x")};
    request(engine,"project.apply",{{"project",project}}); request(engine,"runtime.start"); QTest::qWait(30);
    request(engine,"runtime.pause");
    QVERIFY(!engine.observation().value("running").toBool()); QCOMPARE(engine.observation().value("failed").toULongLong(),0ull);
    QVERIFY(engine.observation().value("errors").toMap().isEmpty());
    request(engine,"runtime.reset"); engine.shutdown(); running = nullptr;
}

void ExecutionTest::packageIsolationAndAsyncCancellation()
{
    const auto path = qEnvironmentVariable("CORIG_TEST_PYTHON");
    if (path.isEmpty()) QSKIP("Set CORIG_TEST_PYTHON to verify real package worker processes");
    corig::PythonHost python([] { return false; }); python.selectInterpreter(path);
    const QString code = QString::fromUtf8(R"PY(
import os,time
def process(inputs,state):
    state['count']=state.get('count',0)+1
    return {'pid':os.getpid(),'count':state['count']}
def validate(config,context):
    if config.get('bad'): raise ValueError('business rejection')
    return config
def invalid(inputs,state):
    state['count']=999
    return object()
def action_work(inputs,state,context):
    for index in range(40):
        context.sleep(.01)
        context.progress((index+1)/40)
    state['finished']=True
    return {'result':inputs['value']*2}
def action_uncooperative(inputs,state,context):
    time.sleep(4)
    state['wrong']=True
    return {'result':1}
)PY");
    const auto call = [&](const QString &package, const QString &node, const QString &entry = "process", const QString &operation = "call", const QVariantMap &extra = QVariantMap{}) {
        QVariantMap args{{"package",package},{"node",node},{"code",code},{"entry",entry},{"op",operation},{"inputs",QVariantMap{{"value",7}}}};
        for (auto i = extra.cbegin(); i != extra.cend(); ++i) args[i.key()] = i.value();
        return python.call(args);
    };
    const auto alpha = call("package.a","same").value("result").toMap();
    const auto sibling = call("package.a","sibling").value("result").toMap();
    const auto beta = call("package.b","same").value("result").toMap();
    QCOMPARE(alpha.value("pid"),sibling.value("pid")); QVERIFY(alpha.value("pid") != beta.value("pid"));
    QCOMPARE(sibling.value("count").toInt(),1);
    QCOMPARE(call("package.a","same").value("result").toMap().value("count").toInt(),2);
    QVERIFY_THROWS_EXCEPTION(corig::Error,call("package.a","same","invalid"));
    QCOMPARE(call("package.a","same").value("result").toMap().value("count").toInt(),3);

    // Metadata cannot collide with the actual node, even with an identical ID.
    QVERIFY_THROWS_EXCEPTION(corig::Error,call("package.a","same","validate","metadata",{{"config",QVariantMap{{"bad",true}}}}));
    QCOMPARE(call("package.a","same").value("result").toMap().value("count").toInt(),4);
    const auto started = call("package.a","same","action_work","task.start");
    const auto task = started.value("result").toMap().value("python_task").toString();
    QVERIFY(!task.isEmpty()); QVERIFY(python.task(task).value("state") != "complete");
    python.call({{"package","package.a"},{"node","same"},{"op","lifecycle"},{"phase","pause"},{"metadata",true}});
    const auto progress = python.task(task).value("progress").toDouble(); QTest::qWait(70);
    QCOMPARE(python.task(task).value("progress").toDouble(),progress);
    python.call({{"package","package.a"},{"node","same"},{"op","lifecycle"},{"phase","resume"},{"metadata",true}});
    QTRY_COMPARE_WITH_TIMEOUT(python.task(task).value("state").toString(),QString("complete"),3000);
    QCOMPARE(python.task(task).value("result").toMap().value("result").toInt(),14);
    QCOMPARE(python.task(task).value("script_state").toMap().value("count").toInt(),4);
    const auto cancel = call("package.a","same","action_work","task.start").value("result").toMap().value("python_task").toString();
    QCOMPARE(python.cancelTask(cancel).value("state").toString(),QString("cancelled"));
    QCOMPARE(call("package.a","same").value("result").toMap().value("pid"),alpha.value("pid"));
    const auto forced = call("package.a","same","action_uncooperative","task.start").value("result").toMap().value("python_task").toString();
    QCOMPARE(python.cancelTask(forced).value("state").toString(),QString("cancelled"));
    QCOMPARE(call("package.b","same").value("result").toMap().value("pid"),beta.value("pid"));
    const auto restarted = call("package.a","same").value("result").toMap();
    QVERIFY(restarted.value("pid") != alpha.value("pid")); QCOMPARE(restarted.value("count").toInt(),1);
    python.close();
}

QTEST_GUILESS_MAIN(ExecutionTest)
#include "execution_test.moc"
