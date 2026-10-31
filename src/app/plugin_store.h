#pragma once

// Installed packages and enable choices belong to a local application session.
// Project-contained copies are resolved without changing the installed library.
#include "core/native_plugin.h"
#include <QHash>
#include <QReadWriteLock>
#include <QSettings>

namespace corig {
class PluginStore {
public:
    PluginStore(QString directory, QSettings &settings, Catalog::PythonCall python = {});
    Catalog catalog(const QVariantMap &project, bool includeDisabled = false) const;
    QVariantMap readiness(const QVariantMap &project) const;
    QVariantMap state(const QVariantMap &project) const;
    QVariantMap install(const PluginPackage &package, const QString &fileName = {});
    void setEnabled(const QStringList &plugins, bool enabled, const QVariantMap &project);
    void remove(const QString &package, const QVariantMap &project);
    PluginPackage find(const QString &id, const QVariantMap &project) const;
    QVariantMap tree(const QVariantMap &project, const QString &scope) const;
    // Picks up packages the user added, moved or deleted outside the program.
    // Callers use it before discovery so no restart is needed.
    void rescan();
private:
    void addScanned(const QString &relative, const PluginPackage &package);
    void seedBundled();
    void recheckLoaded();
    QString directory_;
    QSettings &settings_;
    Catalog::PythonCall python_;
    mutable QReadWriteLock lock_;
    QMap<QString,PluginPackage> installed_;
    QMap<QString,QString> locations_;
    QHash<QString,QString> packageIds_;
    QMap<QString,PluginPackage> pending_;
    QSet<QString> disabled_;
};
}
