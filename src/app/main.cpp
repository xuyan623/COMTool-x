// The desktop executable has no Python startup dependency. User extensions are
// started lazily by the graph worker only after a local interpreter is selected.
#include "controller.h"
#include "quick/renderers.h"
#include "quick/icon_provider.h"
#include "core/paths.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>
#include <QFont>
#include <QIcon>
#include <QJsonDocument>
#include <cstdio>
#include <QProcess>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

static QString logPath;
static QMutex logMutex;
static void messageHandler(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    QMutexLocker lock(&logMutex);
    const auto text = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) + " [" + QString::number(type) + "] " + message + '\n';
    if (!logPath.isEmpty()) { QFile file(logPath); const auto mode = QIODevice::WriteOnly | (file.size() > 4 * 1024 * 1024 ? QIODevice::Truncate : QIODevice::Append); if (file.open(mode)) file.write(text.toUtf8()); }
    const auto bytes = text.toLocal8Bit(); std::fwrite(bytes.constData(),1,bytes.size(),stderr);
    if (type == QtFatalMsg) std::abort();
}

int main(int argc, char **argv)
{
    try { corig::configureRuntimePaths(); }
    catch (const std::exception &error) {
#ifdef Q_OS_WIN
        const auto message = QString::fromUtf8(error.what());
        MessageBoxW(nullptr, reinterpret_cast<LPCWSTR>(message.utf16()), L"CoRig 无法启动", MB_OK | MB_ICONERROR);
#else
        std::fprintf(stderr, "%s\n", error.what());
#endif
        return 1;
    }
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QGuiApplication application(argc,argv); QCoreApplication::setOrganizationName("CoRig"); QCoreApplication::setApplicationName("CoRig"); QCoreApplication::setApplicationVersion("0.4.2");
    application.setApplicationDisplayName(QStringLiteral("联调 · CoRig"));
    application.setQuitOnLastWindowClosed(false);
    QCommandLineParser parser; parser.setApplicationDescription("联调 · CoRig 工程调试工作台"); parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"data-dir","工程、设置和本地连接信息的目录","directory",corig::defaultDataDirectory()});
    parser.addOption({"plugin-selections","重启后恢复本次工程的插件版本选择","base64-json"});
    parser.addOption({"update-installer","打开此安装目录的更新工具"});
    parser.addOption({"configure-installer","配置此安装目录的更新缓存后退出"});
    parser.addPositionalArgument("project","启动后打开的新版工程文件（不会自动运行）"); parser.process(application);
    if (parser.isSet("configure-installer")) return 0;
    if (parser.isSet("update-installer")) {
        const auto root = QCoreApplication::applicationDirPath();
        const auto tool = root + "/CoRigMaintenanceTool.exe";
        return QFileInfo::exists(tool) && QProcess::startDetached(tool,
            {"--cache-path", root + "/cache/installer", "--start-updater"}) ? 0 : 1;
    }
    const auto directory = QDir(parser.value("data-dir")).absolutePath();
    try {
        // Data lives with the program; create the three user-facing folders once so
        // an empty project folder is explained instead of silently missing.
        corig::ensureDataDirectory(directory);
        logPath = QCoreApplication::applicationDirPath() + "/cache/logs/native.log"; qInstallMessageHandler(messageHandler);
        QQuickStyle::setStyle("Basic");
        application.setFont(QFont("Microsoft YaHei UI",10));
        application.setWindowIcon(QIcon(":/icon.svg"));
        qmlRegisterType<corig::WireLayer>("CoRig.Native",1,0,"WireLayer");
        qmlRegisterType<corig::NodeSurface>("CoRig.Native",1,0,"NodeSurface");
        qmlRegisterType<corig::NativePlot>("CoRig.Native",1,0,"NativePlot");
        qmlRegisterType<corig::NativeAttitude>("CoRig.Native",1,0,"NativeAttitude");
        const auto projects = parser.positionalArguments();
        const auto choices = QJsonDocument::fromJson(QByteArray::fromBase64(parser.value("plugin-selections").toLatin1())).toVariant().toMap();
        QVariantMap initialCatalog;
        if (!projects.isEmpty() && !choices.isEmpty()) { initialCatalog = corig::loadProject(projects.first()); initialCatalog["plugin_selections"] = choices; }
        corig::Controller controller(directory,nullptr,initialCatalog); QQmlApplicationEngine qml;
        qml.addImageProvider("icons",new corig::IconProvider);
        QObject::connect(&application,&QCoreApplication::aboutToQuit,&controller,&corig::Controller::shutdown);
        controller.attachUi(&qml);
        if (!projects.isEmpty()) {
            if (projects.first().endsWith(".crpl",Qt::CaseInsensitive)) controller.invoke("plugin.import",{{"path",projects.first()}});
            else controller.invoke("project.open",{{"path",projects.first()},{"plugin_selections",choices}});
        }
        const auto result = application.exec(); controller.shutdown(); return result;
    } catch (const std::exception &error) {
        qCritical("%s",error.what());
#ifdef Q_OS_WIN
        const auto message = QString::fromUtf8(error.what());
        MessageBoxW(nullptr, reinterpret_cast<LPCWSTR>(message.utf16()), L"CoRig 无法启动", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }
}
