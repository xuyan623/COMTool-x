#pragma once

// A .crpl is a bounded ZIP archive. Package declarations and file bytes are
// shared by import, project embedding, native loading and plugin export.
#include "value.h"

namespace corig {

class CORIG_API PluginPackage {
public:
    static PluginPackage read(const QByteArray &archive);
    static PluginPackage open(const QString &path);
    static PluginPackage create(const QVariantMap &manifest, const QMap<QString,QByteArray> &files);
    static PluginPackage fromPython(const QVariantMap &manifest);
    const QVariantMap &manifest() const { return manifest_; }
    const QByteArray &archive() const { return archive_; }
    QString id() const { return manifest_.value("id").toString(); }
    QString version() const { return manifest_.value("version").toString(); }
    QString hash() const;
    QString moduleHash() const { return moduleHash_; }
    QVariantList definitions() const;
    QByteArray file(const QString &name) const;
    QString materialize() const;
    void save(const QString &path) const;
    static QString checkedPath(const QString &name);
private:
    QVariantMap manifest_;
    QMap<QString,QByteArray> files_;
    QByteArray archive_;
    QString hash_;
    QString moduleHash_;
};

CORIG_API QList<PluginPackage> bundledPackages();
CORIG_API QMap<QString,PluginPackage> packagesInDirectory(const QString &directory);

} // namespace corig
