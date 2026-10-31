// SDK command-line packer: builds the exact .crpl format accepted by the GUI.
// The build helper supplies the compiler version; Qt's actual plugin metadata
// supplies the Qt version and build mode, independently of the packer's build.
#include "core/native_plugin.h"
#include <QCoreApplication>
#include <QDirIterator>
#include <QJsonDocument>
#include <QSysInfo>
#include <QPluginLoader>
#include <QRegularExpression>
#include <cstdio>

using namespace corig;
namespace {
QByteArray read(const QString &path)
{
    QFile input(path); if (!input.open(QIODevice::ReadOnly)) throw Error("无法读取：" + path); return input.readAll();
}
QVariantMap nativeInfo(const QString &library, const QString &binary, int compilerVersion = 0)
{
    const auto metadata = QPluginLoader(binary).metaData().toVariantMap();
    if (metadata.value("IID") != CoRigNativePlugin_iid) throw Error("动态库没有声明 CoRig NativePlugin 接口：" + binary);
    const auto qt = metadata.value("version").toInt();
    QVariantMap info{{"library",library},{"sdk",PluginSdkVersion},{"qt",QString::number(qt>>16) + '.' + QString::number((qt>>8)&255)},
        {"architecture",QSysInfo::buildCpuArchitecture()},{"os",QSysInfo::kernelType()}};
#ifdef _MSC_VER
    info["compiler"] = "msvc"; info["compiler_version"] = compilerVersion ? compilerVersion : _MSC_VER;
#else
    info["compiler"] = "gcc";
#endif
    info["build"] = metadata.value("debug").toBool() ? "debug" : "release";
    return info;
}
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc,argv); const auto args = app.arguments();
    try {
        QVariantMap manifest; QMap<QString,QByteArray> files; QString output;
        if (args.size() == 6 && args[1] == "official") {
            const auto library = "bin/" + QFileInfo(args[3]).fileName();
            files.insert(library,read(args[3]));
            const QMap<QString,QString> views{{"connection","ConnectionView.qml"},{"control","ControlView.qml"},{"console","ConsoleView.qml"},
                {"recorder","RecorderView.qml"},{"playback","PlaybackView.qml"},{"values","ValueView.qml"},{"timer","ValueView.qml"},
                {"plot","PlotView.qml"},{"attitude","AttitudeView.qml"},{"terminal","TerminalView.qml"}};
            int count = 0;
            for (const auto &entry : QJsonDocument::fromJson(read(args[2])).toVariant().toList()) {
                auto spec = entry.toMap(); QVariantMap declared;
                QMap<QString,QByteArray> packageFiles{{library,files.value(library)}};
                const auto id = spec.value("id").toString();
                if (id.startsWith("io.")) spec["defer_inputs"] = true;
                if (id == "source.timer" || id == "data.playback") spec["source"] = true;
                for (const auto &view : spec.value("views").toList()) {
                    const auto name = views.value(view.toString());
                    if (name.isEmpty()) throw Error("官方视图未打包：" + view.toString());
                    const auto path = "views/" + name;
                    if (!packageFiles.contains(path)) {
                        auto text = QString::fromUtf8(read(QDir(args[4]).filePath(name)));
                        text.prepend("import \"qrc:/qt/qml/CoRig\"\n");
                        packageFiles.insert(path,text.toUtf8());
                    }
                    declared.insert(view.toString(),QVariantMap{{"qml",path},{"interface","host"}});
                }
                if (!declared.isEmpty()) spec["views"] = declared;
                auto category = spec.value("category_path").toStringList();
                if (category.isEmpty()) category.append(spec.value("category","其他").toString());
                static const QStringList mathGroups{"三角函数","反三角函数","双曲函数","指数与对数","数学运算","数值运算","统计","信号处理"};
                if (mathGroups.contains(category.first())) category.prepend("数学");
                for (const auto &part : category) PluginPackage::checkedPath(part);
                auto name = spec.value("name").toString(); name.replace(QRegularExpression("[\\\\/:*?\"<>|]"),"_");
                const auto relative = category.join('/') + '/' + id + ".crpl";
                manifest = {{"format","corig-plugin"},{"package_api",1},{"id","org.corig." + id},{"name",spec.value("name")},
                    {"version",spec.value("version","1.0")},{"official",true},{"implementation","native"},{"native",nativeInfo(library,args[3])},
                    {"plugins",QVariantList{spec}}};
                if (id.startsWith("io.")) manifest["capabilities"] = QStringList{"io"};
                PluginPackage::create(manifest,packageFiles).save(QDir(args[5]).filePath(relative));
                ++count;
            }
            std::printf("Packaged %d official node files\n",count); return 0;
        } else if ((args.size() == 5 || (args.size() == 7 && args[5] == "--compiler-version")) && args[1] == "pack") {
            const auto root = QFileInfo(args[2]).absolutePath();
            manifest = QJsonDocument::fromJson(read(args[2])).toVariant().toMap();
            for (const auto &entry : manifest.take("files").toList()) {
                const auto name = PluginPackage::checkedPath(entry.toString());
                files.insert(name,read(QDir(root).filePath(name)));
            }
            if (manifest.value("implementation") == "native") {
                const auto library = "bin/" + QFileInfo(args[3]).fileName(); files.insert(library,read(args[3]));
                bool valid = true; const auto compiler = args.size() == 7 ? args[6].toInt(&valid) : 0;
                if (!valid || (compiler && (compiler < 1900 || compiler >= 2000))) throw Error("需要有效的 MSVC v14 编译器版本");
                manifest["native"] = nativeInfo(library,args[3],compiler);
            }
            output = args[4];
        } else {
            std::fputs("CoRig-pack pack <plugin.json> <plugin.dll or -> <output.crpl> [--compiler-version N]\n",stderr); return 2;
        }
        const auto package = PluginPackage::create(manifest,files); package.save(output);
        std::printf("%s %s -> %s\n",package.id().toUtf8().constData(),package.version().toUtf8().constData(),output.toUtf8().constData());
        return 0;
    } catch (const std::exception &e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
