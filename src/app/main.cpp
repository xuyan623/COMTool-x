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
#include <QDir>
#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>
#include <QFont>
#include <QIcon>
#include <QJsonDocument>
#include <cstdio>

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
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QGuiApplication application(argc,argv); QCoreApplication::setOrganizationName("CoRig"); QCoreApplication::setApplicationName("CoRig"); QCoreApplication::setApplicationVersion("0.4.1");
    application.setApplicationDisplayName(QStringLiteral("联调 · CoRig"));
    application.setQuitOnLastWindowClosed(false);
    QCommandLineParser parser; parser.setApplicationDescription("联调 · CoRig 工程调试工作台"); parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"data-dir","工程、设置和本地连接信息的目录","directory",corig::defaultDataDirectory()});
    parser.addOption({"plugin-selections","重启后恢复本次工程的插件版本选择","base64-json"});
    parser.addPositionalArgument("project","启动后打开的新版工程文件（不会自动运行）"); parser.process(application);
    const auto directory = QDir(parser.value("data-dir")).absolutePath(); QDir().mkpath(directory); logPath = directory + "/native.log"; qInstallMessageHandler(messageHandler);
    QQuickStyle::setStyle("Basic");
    application.setFont(QFont("Microsoft YaHei UI",10));
    application.setWindowIcon(QIcon(":/icon.svg"));
    qmlRegisterType<corig::WireLayer>("CoRig.Native",1,0,"WireLayer");
    qmlRegisterType<corig::NodeSurface>("CoRig.Native",1,0,"NodeSurface");
    qmlRegisterType<corig::NativePlot>("CoRig.Native",1,0,"NativePlot");
    qmlRegisterType<corig::NativeAttitude>("CoRig.Native",1,0,"NativeAttitude");
    try {
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
    } catch (const std::exception &error) { qCritical("%s",error.what()); return 1; }
}
