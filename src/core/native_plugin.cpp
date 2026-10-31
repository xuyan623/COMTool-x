#include "native_plugin.h"
#include <QDir>
#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>
#include <QPluginLoader>
#include <QSysInfo>
#include <QCryptographicHash>
#include <QVersionNumber>

namespace corig {
namespace {
struct Modules {
    QMutex mutex;
    QMap<QString,std::shared_ptr<QPluginLoader>> loaders;
    QMap<QString,QString> loadedIds;
};
Modules &modules() { static Modules value; return value; }
}

QString loadedNativeHash(const QString &packageId)
{
    auto &registry = modules(); QMutexLocker lock(&registry.mutex);
    return registry.loadedIds.value(packageId);
}

void validateNativePackage(const PluginPackage &package)
{
    const auto native = package.manifest().value("native").toMap();
    const auto qt = QVersionNumber::fromString(native.value("qt").toString());
    if (native.value("sdk").toInt() != PluginSdkVersion || qt.majorVersion() != QT_VERSION_MAJOR || qt.minorVersion() > QT_VERSION_MINOR
        || native.value("architecture").toString() != QSysInfo::buildCpuArchitecture()
        || native.value("os").toString() != QSysInfo::kernelType())
        throw Error("C++ 插件与当前 CoRig SDK、Qt 或系统架构不匹配：" + package.id());
#ifdef _MSC_VER
    // Microsoft documents compatibility within the v14 ABI family. Actual
    // DLL loading still verifies that its runtime dependencies are available.
    const auto compiler = native.value("compiler_version").toInt();
    if (native.value("compiler").toString() != "msvc" || compiler < 1900 || compiler >= 2000)
        throw Error("C++ 插件编译器与当前 CoRig SDK 不匹配：" + package.id());
#endif
#ifdef QT_DEBUG
    const QString build = "debug";
#else
    const QString build = "release";
#endif
    if (native.value("build").toString() != build) throw Error("C++ 插件构建类型不匹配：" + package.id());
}

NativePlugin &nativePlugin(const PluginPackage &package)
{
    validateNativePackage(package);
    auto &registry = modules(); QMutexLocker lock(&registry.mutex);
    auto &loaders = registry.loaders;
    const auto native = package.manifest().value("native").toMap();
    const auto packageHash = package.hash();
    // Several official node packages share one provider binary. Reuse that
    // module in memory while retaining independent package/version identities.
    const auto key = package.moduleHash();
    if (registry.loadedIds.contains(package.id()) && registry.loadedIds.value(package.id()) != packageHash)
        throw Error("此 C++ 插件的其他版本已加载，需要重启 CoRig：" + package.id());
    if (!loaders.contains(key)) {
        auto loader = std::make_shared<QPluginLoader>(QDir(package.materialize()).filePath(native.value("library").toString()));
        // Native code can remain referenced by nodes and QML objects. Keep the
        // module resident; applying a replacement requires restarting CoRig.
        loader->setLoadHints(QLibrary::PreventUnloadHint);
        if (!loader->load()) throw Error("C++ 插件加载失败：" + loader->errorString());
        if (!qobject_cast<NativePlugin *>(loader->instance())) throw Error("C++ 插件未实现公开的 NativePlugin 接口");
        loaders.insert(key,std::move(loader));
    }
    registry.loadedIds.insert(package.id(),packageHash);
    return *qobject_cast<NativePlugin *>(loaders.value(key)->instance());
}
}
