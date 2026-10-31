#include "paths.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace corig {
QString defaultDataDirectory()
{
    // Installed and portable builds both keep user data beside the executable.
    // The installer then only replaces program files, and one folder holds
    // everything a user cares about.
    return QCoreApplication::applicationDirPath();
}

void ensureDataDirectory(const QString &directory)
{
    QDir root(directory);
    if (!root.mkpath("project") || !root.mkpath("plugins") || !root.mkpath("cache"))
        throw Error("无法创建数据目录：" + directory);
    // A marker file explains the folder without pretending to be a project.
    const auto note = root.filePath("project/说明.txt");
    if (QFileInfo::exists(note)) return;
    QSaveFile file(note);
    if (!file.open(QIODevice::WriteOnly)) throw Error("无法写入工程目录说明：" + note);
    file.write(QStringLiteral(
        "CoRig 工程目录\n\n"
        "此目录保存 .crpj 单文件工程。保存工程时未指定路径，默认写入这里。\n"
        "升级 CoRig 不会删除此目录中的文件；卸载时默认也保留。\n").toUtf8());
    if (!file.commit()) throw Error("无法写入工程目录说明：" + note);
}
} // namespace corig
