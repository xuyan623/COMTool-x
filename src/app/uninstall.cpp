// The GUI only confirms scope and starts the independent cleanup worker.
// The worker waits for this process to exit before removing locked Qt files.
#include "core/paths.h"
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QTextStream>
#include <QThread>
#include <QElapsedTimer>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

int main(int argc, char **argv)
{
    try { corig::configureRuntimePaths(); }
    catch (const std::exception &error) {
#ifdef Q_OS_WIN
        const auto message = QString::fromUtf8(error.what());
        MessageBoxW(nullptr, reinterpret_cast<LPCWSTR>(message.utf16()), L"CoRig 无法卸载", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName("CoRig");
    QCoreApplication::setApplicationName("CoRig");
    const auto root = QCoreApplication::applicationDirPath();
    const auto arguments = application.arguments();
    const int silent = arguments.indexOf("--silent");
    const bool scripted = silent >= 0;
    bool keepUserData = true;
    if (scripted) {
        const auto mode = arguments.value(silent + 1);
        if (mode != "keep" && mode != "all") return 3;
        keepUserData = mode == "keep";
    }
    // Without a generated manifest there is no reliable ownership boundary.
    if (QDir(root).isRoot() || !QFileInfo::exists(root + "/CoRig.exe") || !QFileInfo::exists(root + "/install-manifest.json")) {
        if (!scripted) QMessageBox::critical(nullptr, QStringLiteral("无法卸载"),
            QStringLiteral("安装文件清单缺失，请先使用新版安装包修复安装。\n%1").arg(root));
        return 1;
    }
    if (!scripted) {
        QMessageBox confirmation;
        confirmation.setWindowTitle(QStringLiteral("卸载 CoRig"));
        confirmation.setText(QStringLiteral("将移除 %1 中的 CoRig 程序文件，包括卸载程序自身。").arg(root));
        confirmation.setInformativeText(QStringLiteral("Keep 保留用户内容；All 清空整个安装目录。\n安装目录之外的数据不会删除。"));
        auto *keep = new QCheckBox(QStringLiteral("Keep：保留工程、用户插件、设置及其他用户文件"), &confirmation);
        keep->setChecked(true); confirmation.setCheckBox(keep);
        auto *remove = confirmation.addButton(QStringLiteral("卸载"), QMessageBox::AcceptRole);
        confirmation.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
        confirmation.exec();
        if (confirmation.clickedButton() != remove) return 0;
        keepUserData = keep->isChecked();
        if (!keepUserData && QMessageBox::warning(nullptr, QStringLiteral("All：确认清空安装目录"),
                QStringLiteral("将删除整个安装目录及其中所有文件，包括手动放入的其他文件，无法撤销。\n目录：%1\n确定继续？").arg(root),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return 0;
    }
    QFile worker(":/uninstall_cleanup.ps1");
    if (!worker.open(QIODevice::ReadOnly)) return 1;
    const auto ready = root + "/cache/uninstall-ready-" + QString::number(application.applicationPid());
    const int reportOption = arguments.indexOf("--report");
    const auto report = reportOption >= 0 ? QFileInfo(arguments.value(reportOption + 1)).absoluteFilePath() : QString{};
    // Parameters are JSON inside Base64, never interpolated into shell syntax.
    const auto parameters = QJsonDocument::fromVariant(QVariantMap{
        {"root", root}, {"parentPid", application.applicationPid()}, {"keep", keepUserData},
        {"interactive", !scripted}, {"ready", ready}, {"report", report}
    }).toJson(QJsonDocument::Compact).toBase64();
    const auto script = QStringLiteral("$options = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('%1')) | ConvertFrom-Json\n")
        .arg(QString::fromLatin1(parameters)) + QString::fromUtf8(worker.readAll());
    const auto encoded = QByteArray(reinterpret_cast<const char *>(script.utf16()), script.size() * 2).toBase64();
    const auto powershell = qEnvironmentVariable("SystemRoot") + "/System32/WindowsPowerShell/v1.0/powershell.exe";
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    // PowerShell's optional module analysis cache must not introduce a new
    // AppData cache or recreate the directory after cleanup has completed.
    environment.insert("PSModuleAnalysisCachePath", "NUL");
    process.setProcessEnvironment(environment);
    process.setProgram(powershell);
    // The worker outlives this executable. It must not inherit a caller's
    // output pipe, otherwise scripted callers wait for EOF until cleanup ends.
    process.setStandardInputFile(QProcess::nullDevice());
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    process.setArguments({"-NoLogo", "-NoProfile", "-NonInteractive", "-WindowStyle", "Hidden", "-EncodedCommand", QString::fromLatin1(encoded)});
    process.setWorkingDirectory(qEnvironmentVariable("SystemRoot"));
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    if (!process.startDetached()) {
        if (!scripted) QMessageBox::critical(nullptr, QStringLiteral("无法卸载"), QStringLiteral("无法启动退出后的清理进程，尚未删除文件。"));
        return 1;
    }
    // A ready marker means the worker has opened our process handle and
    // validated the entire manifest. This is startup acknowledgement, not a
    // fixed delay used to guess when the executable becomes deletable.
    QElapsedTimer deadline; deadline.start();
    while (!QFileInfo::exists(ready) && deadline.elapsed() < 60000) QThread::msleep(25);
    if (!QFileInfo::exists(ready)) {
        if (!scripted) QMessageBox::critical(nullptr, QStringLiteral("无法卸载"), QStringLiteral("清理进程未就绪，尚未删除文件。"));
        return 1;
    }
    if (scripted) QTextStream(stdout) << "scheduled; cleanup completes after this process exits\n";
    return 0;
}
