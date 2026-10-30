#pragma once

// Installed packages and enable choices belong to a local application session.
// Project-contained copies are resolved without changing the installed library.
#include "core/native_plugin.h"
#include <QReadWriteLock>
#include <QSettings>

namespace corig {
class PluginStore {
public:
    PluginStore(QString directory, QSettings &settings);
    Catalog catalog(const QVariantMap &project) const;
    QVariantMap readiness(const QVariantMap &project) const;
    QVariantMap state(const QVariantMap &project) const;
    QVariantMap install(const PluginPackage &package, const QString &fileName = {});
    void setEnabled(const QString &plugin, bool enabled, const QVariantMap &project);
    void remove(const QString &package, const QVariantMap &project);
    PluginPackage find(const QString &id, const QVariantMap &project) const;
    QVariantMap tree(const QVariantMap &project, const QString &scope) const;
private:
    QString directory_;
    QSettings &settings_;
    mutable QReadWriteLock lock_;
    QMap<QString,PluginPackage> installed_;
    QMap<QString,QString> locations_;
    QMap<QString,PluginPackage> pending_;
    QSet<QString> disabled_;
};
}
