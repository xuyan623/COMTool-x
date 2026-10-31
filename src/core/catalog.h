#pragma once

// One declaration drives graph validation, editors, execution and external
// discovery. Native and Python providers expose the same port/data contract.
#include "plugin_package.h"
#include <functional>

namespace corig {

struct CORIG_API Port {
    QString id, label, direction, type, unit;
    QVariantMap describe() const;
};

class CORIG_API Catalog {
public:
    using PythonCall = std::function<QVariantMap(const QVariantMap &)>;
    Catalog();
    explicit Catalog(const QList<PluginPackage> &packages, PythonCall python = {});
    explicit Catalog(const QMap<QString,PluginPackage> &packages, PythonCall python = {});
    QVariantMap definition(const QString &id) const;
    QString kind(const QString &id) const;
    QVariantList describe() const;
    QVariantMap checkedConfig(const QString &id, const QVariantMap &changes, const QVariantMap &previous = {}) const;
    QVariantMap checkedPresentation(const QString &id, const QString &scope, const QVariantMap &schema,
                                    const QVariantMap &changes, const QVariantMap &previous = {}) const;
    QVector<Port> ports(const QString &id, const QVariantMap &config) const;
    void addPython(const QVariantMap &manifest);
    void clearPython();
    void addPackage(const PluginPackage &package, bool replace = false, const QString &relativePath = {});
    void remove(const QString &id);
    void applyProject(const QVariantMap &project);
    PluginPackage package(const QString &plugin) const;
    PluginPackage servicePackage(const QString &capability) const;
    QString packagePath(const QString &plugin) const;
    QVariantMap embed(const QVariantMap &project, bool includePlugins = true) const;
    QVariantList search(const QString &query, const QStringList &category = {}) const;
private:
    QVariant hook(const QString &id, const QString &name, const QVariantMap &config, const QVariantMap &context) const;
    PythonCall python_;
    QMap<QString, QVariantMap> definitions_;
    QMap<QString, PluginPackage> packages_;
    QMap<QString,QString> locations_;
};

CORIG_API QVariantMap checkedProperties(const QVariantMap &schema, const QVariantMap &changes, const QVariantMap &previous = {});

} // namespace corig
