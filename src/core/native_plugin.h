#pragma once

// Public Qt/C++ SDK. A provider creates real nodes; the host owns their
// lifecycle and scheduling. Qt/compiler/SDK compatibility is checked first.
#include "node.h"
#include <QtPlugin>

namespace corig {
struct PluginEvents {
    std::function<void(const QString &,const Values &)> receive;
    std::function<void(const QString &,const QVariantMap &)> observe;
};

class PluginService {
public:
    virtual ~PluginService() = default;
    virtual QVariantMap transact(const QString &node, const QString &operation, const QVariantMap &arguments) = 0;
    virtual void shutdown() = 0;
};

class NativePlugin {
public:
    virtual ~NativePlugin() = default;
    virtual std::unique_ptr<Node> create(QVariantMap design, QVariantMap definition, QVector<Port> ports, NodeHost host) = 0;
    // Declarative plugins inherit checked defaults/types/ranges. Providers with
    // dynamic contracts override these using the same config seen by the host.
    virtual QVariantMap configuration(const QVariantMap &definition, const QVariantMap &changes, const QVariantMap &previous) const
    { return checkedProperties(definition.value("properties").toMap(),changes,previous); }
    virtual QVector<Port> ports(const QVariantMap &, const QVariantMap &, QVector<Port> declared) const { return declared; }
    // Algorithm-only plugins do not need an asynchronous device service.
    virtual std::unique_ptr<PluginService> createService(const PluginEvents &) { return {}; }
};

CORIG_API NativePlugin &nativePlugin(const PluginPackage &package);
CORIG_API QString loadedNativeHash(const QString &packageId);
CORIG_API void validateNativePackage(const PluginPackage &package);
CORIG_API std::unique_ptr<Node> makePythonNode(QVariantMap design, QVariantMap definition, QVector<Port> ports, NodeHost host);
}

#define CoRigNativePlugin_iid "org.corig.NativePlugin/1.0"
Q_DECLARE_INTERFACE(corig::NativePlugin, CoRigNativePlugin_iid)
