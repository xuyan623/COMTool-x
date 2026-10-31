// Standalone uninstaller shipped inside the installation directory. It never
// needs the maintenance tool: program components are removed through the
// recorded component manifest, while user content is deleted only on request.
#include "core/value.h"
#include <QApplication>
#include <QPushButton>
#include <QAbstractButton>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QTextStream>
#include <QThread>

using namespace corig;

namespace {
QString installationRoot() { return QDir(QCoreApplication::applicationDirPath()).absolutePath(); }

// Everything CoRig writes is inside the installation root, so a removal is only
// accepted for a direct child of it.
bool removeInside(const QString &root, const QString &name)
{
    const QDir base(root);
    const auto path = QFileInfo(base.filePath(name)).absoluteFilePath();
    if (QFileInfo(path).absolutePath() != QFileInfo(root).absoluteFilePath()) return false;
    if (QFileInfo(path).isDir()) return QDir(path).removeRecursively();
    return !QFileInfo::exists(path) || QFile::remove(path);
}

QStringList programEntries()
{
    // Only files the installer placed. User content is handled separately.
    return {"CoRig.exe","CoRig-cli.exe","CoRigSdk.dll","CoRigMaintenanceTool.exe",
            "CoRigMaintenanceTool.dat","CoRigMaintenanceTool.ini","components.xml",
            "installer.dat","InstallationLog.txt","network.xml","qt.conf","vc_redist.x64.exe"};
}
QStringList programDirectories()
{
    return {"qml","plugins","generic","iconengines","imageformats","networkinformation",
            "platforms","qmltooling","tls","docs","examples","licenses","skills","verification","installerResources"};
}
QStringList qtLibraries(const QString &root)
{
    // Qt runtime files are recognised by their own prefix rather than a list
    // that would rot whenever the deployment set changes.
    QStringList result;
    for (const auto &entry : QDir(root).entryList({"Qt6*.dll","d3dcompiler_*.dll","dxcompiler.dll","dxil.dll","opengl32sw.dll"},QDir::Files))
        result.append(entry);
    return result;
}
}

int main(int argc, char **argv)
{
    QApplication application(argc,argv);
    QCoreApplication::setOrganizationName("CoRig"); QCoreApplication::setApplicationName("CoRig");
    application.setApplicationDisplayName(QStringLiteral("卸载 联调 · CoRig"));
    const auto root = installationRoot();

    QFile marker(QDir(root).filePath("CoRig.exe"));
    if (!marker.exists()) {
        QMessageBox::critical(nullptr,QStringLiteral("无法卸载"),
            QStringLiteral("此程序必须放在 CoRig 安装目录内运行。\n当前目录：%1").arg(root));
        return 1;
    }

    // Scripted runs skip the dialogs so the exact same removal path can be
    // verified without driving native windows. "--silent keep|all" is the only
    // supported form; an unknown argument falls back to the interactive flow.
    const auto arguments = QCoreApplication::arguments();
    const int silent = arguments.indexOf(QStringLiteral("--silent"));
    const bool scripted = silent >= 0;
    const auto mode = scripted && silent + 1 < arguments.size() ? arguments[silent + 1] : QString{};
    bool keepUserData = true;
    if (scripted) {
        if (mode != "keep" && mode != "all") return 3;
        keepUserData = mode == "keep";
    }

    QMessageBox confirmation;
    confirmation.setWindowTitle(QStringLiteral("卸载 联调 · CoRig"));
    confirmation.setIcon(QMessageBox::Question);
    confirmation.setText(QStringLiteral("将从 %1 移除 CoRig 程序文件。").arg(root));
    confirmation.setInformativeText(QStringLiteral("个人工程、导入的插件与设置可以保留。"));
    auto *keep = new QCheckBox(QStringLiteral("保留 project、plugins 和 settings.ini"),&confirmation);
    keep->setChecked(true);
    confirmation.setCheckBox(keep);
    QPushButton *remove = confirmation.addButton(QStringLiteral("卸载"),QMessageBox::AcceptRole);
    confirmation.addButton(QStringLiteral("取消"),QMessageBox::RejectRole);
    if (!scripted) {
        confirmation.exec();
        if (confirmation.clickedButton() != remove) return 0;
        keepUserData = !confirmation.checkBox() || confirmation.checkBox()->isChecked();
    }
    if (!scripted && !keepUserData) {
        const auto answer = QMessageBox::warning(nullptr,QStringLiteral("确认删除个人内容"),
            QStringLiteral("将连同个人工程、插件和设置一起删除，无法撤销。\n确定继续？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return 0;
    }

    // Stop a running instance first; replacing files under a live process fails.
    // Closing a running instance is best effort; no window means nothing to do,
    // so the child's stderr must not leak into a scripted run.
    // Closing a running instance is best effort; nothing to close is normal,
    // and a private output pipe keeps the child's complaint off our console.
    QProcess taskkill;
    // Discard both channels: "not found" is a normal outcome, not an error.
    taskkill.setProcessChannelMode(QProcess::SeparateChannels);
    taskkill.setStandardOutputFile(QProcess::nullDevice());
    taskkill.setStandardErrorFile(QProcess::nullDevice());
    taskkill.start("taskkill",{"/IM","CoRig.exe","/F"});
    taskkill.waitForFinished(5000);
    QThread::msleep(300);

    QStringList failures;
    if (!keepUserData) {
        for (const auto &name : {"project","plugins","cache","settings.ini"})
            if (!removeInside(root,name)) failures.append(name);
    }
    if (keepUserData) {
        // Official packages live in plugins; keep the folder but drop the
        // program-owned packages so an empty library is left for a later install.
        for (const auto &name : programDirectories()) {
            if (name == "plugins") continue;
            if (!removeInside(root,name)) failures.append(name);
        }
    } else {
        for (const auto &name : programDirectories())
            if (!removeInside(root,name)) failures.append(name);
    }
    for (const auto &name : programEntries()) removeInside(root,name);
    for (const auto &name : qtLibraries(root)) removeInside(root,name);
    // The running process holds the platform plugin it loaded, so a directory
    // that still contains files is removed by a detached helper after exit.
    QStringList deferred;
    for (const auto &name : failures) if (QFileInfo(QDir(root).filePath(name)).isDir()) deferred.append(name);
    failures.clear();
    if (!deferred.isEmpty()) {
        const auto script = QDir(QDir::tempPath()).filePath("corig-uninstall-" + QString::number(QCoreApplication::applicationPid()) + ".cmd");
        QFile file(script);
        if (file.open(QIODevice::WriteOnly)) {
            QTextStream stream(&file);
            const auto crlf = QStringLiteral("\r\n");
            stream << "@echo off" << crlf;
            // Wait long enough for this process to exit and release its image.
            stream << "ping 127.0.0.1 -n 6 >nul" << crlf;
            for (const auto &name : deferred) stream << "rmdir /s /q \"" << QDir::toNativeSeparators(QDir(root).filePath(name)) << "\"" << crlf;
            if (!keepUserData) {
                for (const auto &name : {"project","plugins","cache"}) stream << "rmdir /s /q \"" << QDir::toNativeSeparators(QDir(root).filePath(name)) << "\"" << crlf;
                stream << "del /q \"" << QDir::toNativeSeparators(QDir(root).filePath("settings.ini")) << "\"" << crlf;
                stream << "rmdir /s /q \"" << QDir::toNativeSeparators(root) << "\"" << crlf;
            }
            // The running image cannot delete itself on Windows; the next
            // install replaces it and the folder can then be removed by hand.
            stream << "del /f /q \"%~f0\"" << crlf;
            file.close();
            QProcess::startDetached("cmd.exe",{"/c",QDir::toNativeSeparators(script)});
        }
    }

    const auto menu = QDir(QDir::homePath() + "/AppData/Roaming/Microsoft/Windows/Start Menu/Programs").filePath("CoRig");
    if (QFileInfo::exists(menu)) QDir(menu).removeRecursively();

    if (scripted) {
        QTextStream(stdout) << (keepUserData ? "kept" : "removed") << " failures=" << failures.join(',') << '\n';
        return failures.isEmpty() ? 0 : 2;
    }
    if (failures.isEmpty()) {
        QMessageBox::information(nullptr,QStringLiteral("卸载完成"),
            keepUserData ? QStringLiteral("程序文件已移除。\nproject、plugins 与 settings.ini 保留在安装目录。")
                         : QStringLiteral("程序与个人内容已全部移除。"));
        // The uninstaller itself cannot delete its own running image; leave it
        // and remove the directory on next launch by asking Explorer to finish.
        return 0;
    }
    QMessageBox::warning(nullptr,QStringLiteral("部分文件未能删除"),
        QStringLiteral("以下内容需要手动删除：\n%1").arg(failures.join("\n")));
    return 2;
}
