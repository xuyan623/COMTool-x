#include "paths.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace corig {
void configureRuntimePaths()
{
    QString directory;
#ifdef Q_OS_WIN
    // applicationDirPath is unavailable before QCoreApplication exists.
    wchar_t executable[32768];
    const auto length = GetModuleFileNameW(nullptr, executable, 32768);
    if (!length || length >= 32768) throw Error("无法确定 CoRig 安装目录");
    directory = QFileInfo(QString::fromWCharArray(executable, int(length))).absolutePath();
#else
    directory = QCoreApplication::applicationDirPath();
#endif
    const QDir root(directory);
    for (const auto &name : {"cache/qml", "cache/shaders", "cache/temp", "cache/installer", "cache/logs"}) {
        const auto path = root.filePath(name);
        if (!QDir().mkpath(path)) throw Error("安装目录不可写，请选择可写的安装位置：" + path);
        QTemporaryFile probe(path + "/.write-XXXXXX");
        if (!probe.open()) throw Error("安装目录不可写，请选择可写的安装位置：" + path);
    }
    qputenv("QML_DISK_CACHE_PATH", root.filePath("cache/qml").toUtf8());
    qputenv("QT_SHADER_CACHE_PATH", root.filePath("cache/shaders").toUtf8());
    // Disable Qt's automatic AppData pipeline cache while explicitly keeping
    // the pipeline cache under this installation for every Quick window.
    qputenv("QSG_RHI_DISABLE_DISK_CACHE", "1");
    qputenv("QSG_RHI_PIPELINE_CACHE_LOAD", root.filePath("cache/shaders/pipeline.cache").toUtf8());
    qputenv("QSG_RHI_PIPELINE_CACHE_SAVE", root.filePath("cache/shaders/pipeline.cache").toUtf8());
    const auto temporary = QDir::toNativeSeparators(root.filePath("cache/temp")).toUtf8();
    qputenv("TEMP", temporary); qputenv("TMP", temporary); qputenv("TMPDIR", temporary);
    // IFW stores its cache setting in network.xml. Preserve repositories and
    // proxy settings, replacing only the cache path after install/relocation.
    QFile network(root.filePath("network.xml"));
    if (network.exists()) {
        if (!network.open(QIODevice::ReadOnly)) throw Error("无法读取更新配置：" + network.fileName());
        const auto bytes = network.readAll(); network.close();
        QXmlStreamReader reader(bytes);
        QByteArray output;
        QXmlStreamWriter writer(&output);
        const auto installerCache = root.filePath("cache/installer");
        bool changed = false, found = false;
        while (!reader.atEnd()) {
            reader.readNext();
            if (reader.isStartElement() && reader.name() == u"LocalCachePath") {
                const auto previous = reader.readElementText();
                writer.writeTextElement("LocalCachePath", installerCache);
                changed = previous != installerCache; found = true;
            } else {
                if (reader.isEndElement() && reader.name() == u"Network" && !found) {
                    writer.writeTextElement("LocalCachePath", installerCache); changed = true;
                }
                writer.writeCurrentToken(reader);
            }
        }
        if (reader.hasError()) throw Error("更新配置 XML 损坏：" + network.fileName());
        if (changed) {
            QSaveFile file(network.fileName());
            if (!file.open(QIODevice::WriteOnly) || file.write(output) != output.size() || !file.commit())
                throw Error("无法写入更新缓存位置：" + network.fileName());
        }
    }
}

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
