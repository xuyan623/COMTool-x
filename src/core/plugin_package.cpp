#include "plugin_package.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QCache>
#include <QMutex>
#include <QMutexLocker>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtEndian>
#include <QtCore/private/qzipreader_p.h>
#include <QtCore/private/qzipwriter_p.h>
#include <array>

namespace corig {
namespace {
constexpr qint64 ArchiveLimit = 128 * 1024 * 1024;
constexpr qint64 ExpandedLimit = 512 * 1024 * 1024;
QMutex packageCacheMutex;
QCache<QByteArray,PluginPackage> packageCache(128 * 1024);
QCache<QByteArray,QByteArray> binaryFiles(64 * 1024);
QCache<QString,PluginPackage> scannedFiles(128 * 1024);

quint32 zipCrc(const QByteArray &bytes)
{
    // ZIP records use the reflected CRC-32 polynomial. Verify decoded data as
    // well as its declared length; an intact directory alone is insufficient.
    static const auto table = [] {
        std::array<quint32,256> values{};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 crc = i;
            for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0u);
            values[i] = crc;
        }
        return values;
    }();
    quint32 crc = 0xffffffffu;
    for (const auto byte : bytes) crc = (crc >> 8) ^ table[(crc ^ static_cast<unsigned char>(byte)) & 0xff];
    return crc ^ 0xffffffffu;
}

void writeFile(const QString &path, const QByteArray &bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) throw Error("无法创建插件目录");
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
        throw Error("无法写入插件文件：" + path);
}

QString contentHash(const QMap<QString,QByteArray> &files)
{
    QCryptographicHash digest(QCryptographicHash::Sha256);
    for (auto i = files.cbegin(); i != files.cend(); ++i) {
        const auto length = qToBigEndian(static_cast<quint64>(i.value().size()));
        digest.addData(i.key().toUtf8()); digest.addData(QByteArrayView("\0",1));
        digest.addData(QByteArrayView(reinterpret_cast<const char *>(&length),sizeof(length))); digest.addData(i.value());
    }
    return QString::fromLatin1(digest.result().toHex());
}
}

QString PluginPackage::checkedPath(const QString &name)
{
    const auto clean = QDir::cleanPath(name);
    if (name.isEmpty() || name.contains('\\') || name.contains(':') || name.contains(QChar::Null)
        || QDir::isAbsolutePath(name) || clean == "." || clean == ".." || clean.startsWith("../") || clean != name)
        throw Error("插件资源必须使用包内相对路径：" + name);
    return clean;
}

PluginPackage PluginPackage::read(const QByteArray &archive)
{
    if (archive.isEmpty() || archive.size() > ArchiveLimit) throw Error("插件包为空或超过 128 MiB");
    const auto archiveKey = QCryptographicHash::hash(archive,QCryptographicHash::Sha256);
    { QMutexLocker lock(&packageCacheMutex); if (const auto cached = packageCache.object(archiveKey)) return *cached; }
    QBuffer input; input.setData(archive); input.open(QIODevice::ReadOnly);
    QZipReader zip(&input);
    PluginPackage package;
    qint64 total = 0;
    QSet<QString> names;
    const auto entries = zip.fileInfoList();
    if (entries.isEmpty() || entries.size() > 4096) throw Error("插件 ZIP 内容为空或文件数量过多");
    for (const auto &entry : entries) {
        if (entry.isSymLink || (!entry.isDir && !entry.isFile)) throw Error("插件包不能包含符号链接或特殊文件");
        if (entry.isDir) { checkedPath(entry.filePath.endsWith('/') ? entry.filePath.chopped(1) : entry.filePath); continue; }
        const auto name = checkedPath(entry.filePath);
        if (names.contains(name.toCaseFolded())) throw Error("插件包包含重名文件：" + name);
        names.insert(name.toCaseFolded());
        if (entry.size < 0 || entry.size > ExpandedLimit - total) throw Error("插件解压大小超过 512 MiB");
        total += entry.size;
        const auto bytes = zip.fileData(name);
        if (bytes.size() != entry.size || zipCrc(bytes) != entry.crc || zip.status() != QZipReader::NoError) throw Error("插件文件损坏：" + name);
        package.files_.insert(name,bytes);
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(package.files_.value("plugin.json"),&error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) throw Error("插件包缺少有效的 plugin.json");
    package.manifest_ = document.toVariant().toMap();
    const auto &manifest = package.manifest_;
    if (manifest.contains("category") || manifest.contains("category_path")) throw Error("插件包不保存分类，请通过插件库文件夹组织目录");
    if (manifest.value("format") != "corig-plugin" || manifest.value("package_api").toInt() != 1
        || !QRegularExpression("^[A-Za-z_][A-Za-z0-9_.-]+$").match(package.id()).hasMatch()
        || package.version().isEmpty() || manifest.value("plugins").toList().isEmpty())
        throw Error("插件包声明需要 format=corig-plugin、package_api=1、id、version 和 plugins");
    const auto implementation = manifest.value("implementation").toString();
    if (implementation != "native" && implementation != "python") throw Error("插件实现类型需要 native 或 python");
    if (implementation == "native") package.file(manifest.value("native").toMap().value("library").toString());
    package.archive_ = archive;
    package.hash_ = contentHash(package.files_);
    if (implementation == "native") {
        QCryptographicHash binary(QCryptographicHash::Sha256);
        const auto native = manifest.value("native").toMap();
        auto libraries = native.value("dependencies").toStringList(); libraries.prepend(native.value("library").toString());
        for (const auto &name : libraries) {
            const auto bytes = package.file(name); binary.addData(bytes);
            const auto key = QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);
            QMutexLocker lock(&packageCacheMutex);
            if (const auto shared = binaryFiles.object(key)) package.files_[name] = *shared;
            else binaryFiles.insert(key,new QByteArray(bytes),static_cast<int>((bytes.size()+1023)/1024));
            total -= bytes.size();
        }
        package.moduleHash_ = QString::fromLatin1(binary.result().toHex());
    }
    package.definitions();
    { QMutexLocker lock(&packageCacheMutex); packageCache.insert(archiveKey,new PluginPackage(package),static_cast<int>((archive.size()+total+1023)/1024)); }
    return package;
}

PluginPackage PluginPackage::open(const QString &path)
{
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly) || input.size() > ArchiveLimit) throw Error("无法读取插件包：" + path);
    return read(input.readAll());
}

PluginPackage PluginPackage::create(const QVariantMap &manifest, const QMap<QString,QByteArray> &files)
{
    auto portable = manifest;
    portable.remove("category"); portable.remove("category_path");
    auto definitions = portable.value("plugins").toList();
    for (auto &entry : definitions) { auto spec = entry.toMap(); spec.remove("category"); spec.remove("category_path"); entry = spec; }
    portable["plugins"] = definitions;
    QByteArray bytes;
    QBuffer output(&bytes); output.open(QIODevice::WriteOnly);
    QZipWriter zip(&output);
    zip.addFile("plugin.json",QJsonDocument::fromVariant(portable).toJson(QJsonDocument::Indented));
    for (auto i = files.cbegin(); i != files.cend(); ++i) {
        if (i.key() == "plugin.json") throw Error("plugin.json 由插件声明生成");
        zip.addFile(checkedPath(i.key()),i.value());
    }
    zip.close();
    if (zip.status() != QZipWriter::NoError) throw Error("无法生成插件 ZIP");
    return read(bytes);
}

PluginPackage PluginPackage::fromPython(const QVariantMap &definition)
{
    if (definition.value("plugin_api").toInt() != 3) throw Error("Python 插件声明需要 plugin_api=3");
    auto spec = definition;
    QMap<QString,QByteArray> files;
    const auto resources = spec.take("resources").toMap();
    for (auto i = resources.cbegin(); i != resources.cend(); ++i)
        files.insert(checkedPath(i.key()),i.value().metaType().id() == QMetaType::QByteArray ? i.value().toByteArray() : i.value().toString().toUtf8());
    const auto code = spec.take("code").toString();
    if (code.isEmpty()) throw Error("Python 插件缺少代码");
    QString entry = "__corig_entry__.py";
    while (files.contains(entry)) entry.prepend('_');
    files.insert(entry,code.toUtf8()); spec["code_file"] = entry;
    spec["plugin_api"] = 3;
    return create({{"format","corig-plugin"},{"package_api",1},{"id",spec.value("id")},
        {"name",spec.value("name")},{"version",spec.value("version","1.0")},{"implementation","python"},
        {"plugins",QVariantList{spec}}},files);
}

QVariantList PluginPackage::definitions() const
{
    QVariantList result;
    QSet<QString> identifiers;
    for (const auto &entry : manifest_.value("plugins").toList()) {
        auto spec = entry.toMap();
        if (spec.contains("category") || spec.contains("category_path")) throw Error("插件包内不能包含分类结构");
        const auto identifier = spec.value("id").toString();
        if (!QRegularExpression("^[A-Za-z_][A-Za-z0-9_.-]+$").match(identifier).hasMatch()
            || identifiers.contains(identifier) || spec.value("name").toString().isEmpty()) throw Error("插件声明缺少有效且唯一的 id 或 name");
        identifiers.insert(identifier);
        spec["package_id"] = id(); spec["package_hash"] = hash();
        spec["implementation"] = manifest_.value("implementation");
        spec["builtin"] = false;
        spec["official"] = manifest_.value("official",false);
        spec["version"] = spec.value("version",version());
        if (spec.contains("source_interval_ms")) {
            const auto interval = finiteNumber(spec.value("source_interval_ms"),"插件信号源周期");
            if (interval < 0. || interval > 60000.) throw Error("插件信号源周期需要 0～60000 ms");
        }
        const auto views = spec.value("views").toMap();
        for (auto i = views.cbegin(); i != views.cend(); ++i) file(i.value().toMap().value("qml").toString());
        if (manifest_.value("implementation") == "python") {
            spec["plugin_api"] = 3;
            spec["code"] = QString::fromUtf8(file(spec.value("code_file").toString()));
            QVariantMap resources;
            for (auto i = files_.cbegin(); i != files_.cend(); ++i) if (i.key() != "plugin.json") resources.insert(i.key(),i.value());
            spec["resources"] = resources;
        }
        result.append(spec);
    }
    return result;
}

QString PluginPackage::hash() const
{
    // ZIP timestamps do not change a plugin's identity. Hash canonical file
    // names, lengths and bytes so legacy Python imports remain stable too.
    return hash_;
}

QByteArray PluginPackage::file(const QString &name) const
{
    checkedPath(name);
    if (!files_.contains(name)) throw Error("插件包缺少文件：" + name);
    return files_.value(name);
}

QString PluginPackage::materialize() const
{
    const auto root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/plugin-packages/" + hash();
    // Immutable content-addressed directories keep loaded DLLs separate from
    // installed packages; replacing a package takes effect on the next launch.
    for (auto i = files_.cbegin(); i != files_.cend(); ++i) {
        const auto path = root + '/' + checkedPath(i.key());
        QFile existing(path);
        if (existing.open(QIODevice::ReadOnly) && existing.readAll() == i.value()) continue;
        writeFile(path,i.value());
    }
    return root;
}

void PluginPackage::save(const QString &path) const { writeFile(path,archive_); }

QList<PluginPackage> bundledPackages()
{
    return packagesInDirectory(QCoreApplication::applicationDirPath() + "/plugins").values();
}

QMap<QString,PluginPackage> packagesInDirectory(const QString &directory)
{
    QMap<QString,PluginPackage> result;
    const QDir root(directory);
    // Directory placement is the sole classification source. Do not follow
    // junctions/symlinks out of the selected plugin library.
    QDirIterator files(root.absolutePath(),{"*.crpl"},QDir::Files | QDir::NoSymLinks,QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const auto path = files.next();
        const auto relative = PluginPackage::checkedPath(root.relativeFilePath(path));
        const QFileInfo info(path);
        const auto key = info.absoluteFilePath() + ':' + QString::number(info.size()) + ':' + QString::number(info.lastModified().toMSecsSinceEpoch());
        // Repeated catalog snapshots share already checked file contents.
        // External folder/content changes are applied on the next process start.
        bool found = false;
        { QMutexLocker lock(&packageCacheMutex); if (const auto cached = scannedFiles.object(key)) { result.insert(relative,*cached); found = true; } }
        if (!found) {
            const auto package = PluginPackage::open(path); result.insert(relative,package);
            QMutexLocker lock(&packageCacheMutex);
            scannedFiles.insert(key,new PluginPackage(package),static_cast<int>((package.archive().size()+1023)/1024));
        }
    }
    return result;
}
} // namespace corig
