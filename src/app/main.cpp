// The desktop executable has no Python startup dependency. User extensions are
// started lazily by the graph worker only after a local interpreter is selected.
#include "controller.h"
#include "quick/renderers.h"
#include "quick/icon_provider.h"
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
    QGuiApplication application(argc,argv); QCoreApplication::setOrganizationName("CoRig"); QCoreApplication::setApplicationName("CoRig"); QCoreApplication::setApplicationVersion("0.3.0");
    application.setApplicationDisplayName(QStringLiteral("联调 · CoRig"));
    application.setQuitOnLastWindowClosed(false);
    QCommandLineParser parser; parser.setApplicationDescription("联调 · CoRig 工程调试工作台"); parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"data-dir","工程、设置和本地连接信息的目录","directory",QCoreApplication::applicationDirPath()});
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
        corig::Controller controller(directory); QQmlApplicationEngine qml;
        qml.addImageProvider("icons",new corig::IconProvider);
        QObject::connect(&application,&QCoreApplication::aboutToQuit,&controller,&corig::Controller::shutdown);
        controller.attachUi(&qml);
        const auto projects = parser.positionalArguments();
        if (!projects.isEmpty()) controller.invoke("project.open",{{"path",projects.first()}});
        const auto result = application.exec(); controller.shutdown(); return result;
    } catch (const std::exception &error) { qCritical("%s",error.what()); return 1; }
}
