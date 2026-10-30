// Real ZIP files, the shipped native provider and actual installed/project
// catalogs exercise import/export and dependency choices without fake loaders.
#include <QtTest>
#include <QBuffer>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtCore/private/qzipwriter_p.h>
#include "core/project.h"
#include "core/native_plugin.h"
#include "app/plugin_store.h"
#include <cmath>

using namespace corig;
namespace {
PluginPackage pythonPackage(const QString &version, int gain)
{
    return PluginPackage::fromPython({{"plugin_api",3},{"id","user.test.gain"},{"name","增益"},{"version",version},
        {"code",QString("def process(inputs,state):\n    return {'out': inputs['x'] * %1}\n").arg(gain)},
        {"ports",QVariantList{QVariantMap{{"id","x"},{"direction","in"},{"data_type","number"}},QVariantMap{{"id","out"},{"direction","out"},{"data_type","number"}}}},
        {"resources",QVariantMap{{"data.bin",QByteArray("\0real\xff",6)}}}});
}
QVariantMap usedProject(const QString &plugin)
{
    auto project = emptyProject("插件验证");
    project["nodes"] = QVariantList{QVariantMap{{"id","node"},{"plugin",plugin},{"position",QVariantList{0.,0.}},{"config",QVariantMap{}}}};
    return project;
}
}

class PluginTest : public QObject {
    Q_OBJECT
private slots:
    void zipAndPythonRoundTrip();
    void rejectEscapesAndCorruption();
    void embedOfficialWithoutInstalledCopy();
    void chooseConflictingVersions();
    void enableAndRemovalPreserveReferences();
    void folderClassificationAndOptionalDependencies();
    void localInventoryExcludesProjectCopies();
};

void PluginTest::zipAndPythonRoundTrip()
{
    const auto original = pythonPackage("1.0.0",2);
    QVERIFY(original.archive().startsWith("PK"));
    QTemporaryDir directory; original.save(directory.filePath("gain.crpl"));
    const auto reopened = PluginPackage::open(directory.filePath("gain.crpl"));
    QCOMPARE(reopened.archive(),original.archive()); QCOMPARE(reopened.hash(),original.hash());
    QCOMPARE(reopened.file("data.bin"),QByteArray("\0real\xff",6));
    QVERIFY(reopened.definitions().first().toMap().value("code").toString().contains("* 2"));
    // Different ZIP container metadata must not create a false version conflict.
    QMap<QString,QByteArray> files{{"entry.py",QByteArray("def process(inputs,state):\n return {}\n")}};
    const QVariantMap manifest{{"format","corig-plugin"},{"package_api",1},{"id","user.test.hash"},{"version","1"},{"implementation","python"},
        {"plugins",QVariantList{QVariantMap{{"id","user.test.hash"},{"name","Hash"},{"code_file","entry.py"}}}}};
    QCOMPARE(PluginPackage::create(manifest,files).hash(),PluginPackage::create(manifest,files).hash());
}

void PluginTest::rejectEscapesAndCorruption()
{
    for (const auto &name : {"../escape.dll","/absolute.dll","C:/absolute.dll","a\\escape.dll","a/../escape.dll"})
        QVERIFY_THROWS_EXCEPTION(Error,PluginPackage::checkedPath(name));
    QByteArray archive; QBuffer output(&archive); output.open(QIODevice::WriteOnly);
    QZipWriter zip(&output); zip.setCompressionPolicy(QZipWriter::NeverCompress);
    zip.addFile("plugin.json",QJsonDocument::fromVariant(QVariantMap{{"format","corig-plugin"},{"package_api",1},{"id","user.crc"},{"version","1"},{"implementation","python"},
        {"plugins",QVariantList{QVariantMap{{"id","user.crc"},{"name","CRC"},{"code_file","entry.py"}}}}}).toJson());
    const QByteArray code("def process(inputs,state):\n return {'out': 123456789}\n");
    zip.addFile("entry.py",code); zip.close();
    QVERIFY(!PluginPackage::read(archive).hash().isEmpty());
    const auto offset = archive.indexOf(code); QVERIFY(offset >= 0); archive[offset] = 'X';
    QVERIFY_THROWS_EXCEPTION(Error,PluginPackage::read(archive));
}

void PluginTest::embedOfficialWithoutInstalledCopy()
{
    Catalog installed; auto project = usedProject("function.sin");
    const auto embedded = installed.embed(project);
    QCOMPARE(embedded.value("plugin_packages").toList().size(),1);
    const auto item = embedded.value("plugin_packages").toList().first().toMap();
    QCOMPARE(item.value("id").toString(),QString("org.corig.function.sin"));
    Catalog isolated(QList<PluginPackage>{}); isolated.applyProject(embedded); validateProject(embedded,isolated);
    const auto definition = isolated.definition("function.sin"); QVERIFY(!definition.value("builtin").toBool());
    auto node = makeNode(project.value("nodes").toList().first().toMap(),isolated,{});
    QString input, output;
    for (const auto &port : node->ports()) (port.direction == "in" ? input : output) = port.id;
    const auto result = node->execute({{input,.5}}, {}, {1,0.,false,false});
    QCOMPARE(result.emissions.first().value(output).toDouble(),std::sin(.5));
    QTemporaryDir directory; isolated.package("function.sin").save(directory.filePath("official.crpl"));
    QCOMPARE(PluginPackage::open(directory.filePath("official.crpl")).hash(),item.value("sha256").toString());
}

void PluginTest::chooseConflictingVersions()
{
    const auto first = pythonPackage("1.0.0",2), second = pythonPackage("2.0.0",3);
    Catalog original(QList<PluginPackage>{first}); auto project = original.embed(usedProject("user.test.gain"));
    QTemporaryDir directory; QSettings settings(directory.filePath("settings.ini"),QSettings::IniFormat);
    PluginStore store(directory.filePath("plugins"),settings); store.install(second);
    const auto readiness = store.readiness(project);
    QVERIFY(readiness.value("requires_plugin_selection").toBool()); QCOMPARE(readiness.value("conflicts").toList().size(),1);
    QVERIFY_THROWS_EXCEPTION(Error,store.catalog(project));
    project["plugin_selections"] = QVariantMap{{first.id(),"project"}};
    QVERIFY(store.readiness(project).isEmpty()); QCOMPARE(store.find(first.id(),project).hash(),first.hash());
    project["plugin_selections"] = QVariantMap{{first.id(),"local"}};
    QVERIFY(store.readiness(project).isEmpty()); QCOMPARE(store.find(first.id(),project).hash(),second.hash());
    const auto saved = store.catalog(project).embed(project);
    QVERIFY(!saved.contains("plugin_selections"));
    QCOMPARE(saved.value("plugin_packages").toList().first().toMap().value("sha256").toString(),second.hash());
}

void PluginTest::enableAndRemovalPreserveReferences()
{
    QTemporaryDir directory; QSettings settings(directory.filePath("settings.ini"),QSettings::IniFormat);
    PluginStore store(directory.filePath("plugins"),settings); auto project = usedProject("function.sin");
    QVERIFY_THROWS_EXCEPTION(Error,store.setEnabled("function.sin",false,project));
    QVERIFY_THROWS_EXCEPTION(Error,store.remove("org.corig.function.sin",project));
    const auto embedded = store.catalog(project).embed(project);
    store.setEnabled("function.sin",false,emptyProject());
    QVERIFY_THROWS_EXCEPTION(Error,store.catalog(emptyProject()).definition("function.sin"));
    validateProject(embedded,store.catalog(embedded));
    store.setEnabled("function.sin",true,emptyProject());
    store.remove("org.corig.function.sin",emptyProject());
    QCOMPARE(store.catalog(emptyProject()).describe().size(),77);
    validateProject(embedded,store.catalog(embedded));
    QCOMPARE(store.find("function.sin",embedded).id(),QString("org.corig.function.sin"));
    // A later launch respects removal; first-launch defaults are not repeated.
    PluginStore next(directory.filePath("plugins"),settings);
    QCOMPARE(next.catalog(emptyProject()).describe().size(),77);
}

void PluginTest::folderClassificationAndOptionalDependencies()
{
    const auto package = pythonPackage("1.0.0",2);
    QVERIFY(!package.manifest().contains("category_path"));
    QVERIFY(!package.definitions().first().toMap().contains("category_path"));
    QTemporaryDir directory;
    package.save(directory.filePath("通信/CAN/增益.crpl"));
    Catalog placed(packagesInDirectory(directory.path()));
    QCOMPARE(placed.definition("user.test.gain").value("category_path").toStringList(),QStringList({"通信","CAN"}));
    QCOMPARE(placed.search("",{"通信"}).size(),1);
    QCOMPARE(placed.search("gain",{"数学"}).size(),0);
    auto project = usedProject("user.test.gain");
    const auto attached = placed.embed(project);
    QCOMPARE(attached.value("plugin_dependencies").toList().first().toMap().value("relative_path").toString(),QString("通信/CAN/增益.crpl"));
    const auto light = placed.embed(project,false);
    QVERIFY(light.value("plugin_packages").toList().isEmpty());
    QCOMPARE(light.value("plugin_dependencies").toList().size(),1);
    Catalog absent(QList<PluginPackage>{}); QVERIFY_THROWS_EXCEPTION(Error,absent.applyProject(light));
    QVERIFY(QFile::rename(directory.filePath("通信/CAN/增益.crpl"),directory.filePath("增益.crpl")));
    Catalog moved(packagesInDirectory(directory.path()));
    QCOMPARE(moved.definition("user.test.gain").value("category_path").toStringList(),QStringList{"其他"});
    QCOMPARE(moved.search("",{"其他"}).size(),1);
    QCOMPARE(moved.package("user.test.gain").archive(),package.archive());
}

void PluginTest::localInventoryExcludesProjectCopies()
{
    const auto embeddedPackage = pythonPackage("1.0.0",2), localPackage = pythonPackage("2.0.0",3);
    Catalog source(QMap<QString,PluginPackage>{{"工程/增益.crpl",embeddedPackage}});
    auto project = source.embed(usedProject("user.test.gain"));
    project["plugin_selections"] = QVariantMap{{embeddedPackage.id(),"project"}};
    QTemporaryDir directory; QSettings settings(directory.filePath("settings.ini"),QSettings::IniFormat);
    settings.setValue("plugins/initialized",true);
    PluginStore store(directory.filePath("plugins"),settings); store.install(localPackage);
    const auto localState = store.state(project);
    QCOMPARE(localState.value("packages").toList().size(),1);
    QCOMPARE(localState.value("packages").toList().first().toMap().value("version").toString(),QString("2.0.0"));
    QCOMPARE(store.tree(project,"library").value("plugins").toList().first().toMap().value("version").toString(),QString("2.0.0"));
    QCOMPARE(store.tree(project,"project").value("plugins").toList().first().toMap().value("version").toString(),QString("1.0.0"));
    store.setEnabled("user.test.gain",false,emptyProject());
    QVERIFY(!store.state(project).value("plugins").toList().first().toMap().value("enabled").toBool());
    QVERIFY(store.state(project).value("plugins").toList().first().toMap().value("in_use").toBool());
    store.remove(localPackage.id(),emptyProject());
    QVERIFY(store.state(project).value("packages").toList().isEmpty());
    QVERIFY(store.tree(project,"library").value("tree").toList().isEmpty());
    QCOMPARE(store.tree(project,"project").value("plugins").toList().size(),1);
    validateProject(project,store.catalog(project));
}

QTEST_GUILESS_MAIN(PluginTest)
#include "plugin_test.moc"
