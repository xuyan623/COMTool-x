#include "plugin_store.h"
#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QReadLocker>
#include <QWriteLocker>
#include <QVersionNumber>

namespace corig {
namespace {
QSet<QString> usedPlugins(const QVariantMap &project)
{
    QSet<QString> used;
    for (const auto &entry : project.value("nodes").toList()) used.insert(entry.toMap().value("plugin").toString());
    for (const auto &entry : project.value("ui").toMap().value("items").toList()) {
        const auto item = entry.toMap(); const auto binding = item.value("binding").toMap();
        if (binding.contains("plugin")) used.insert(binding.value("plugin").toString());
        const auto container = item.value("plugin").toString();
        if (!container.isEmpty() && !QStringList{"layout.canvas","layout.row","layout.column","layout.grid","layout.tabs"}.contains(container)) used.insert(container);
    }
    const auto workspaces = project.value("workspaces").toMap();
    for (auto i = workspaces.cbegin(); i != workspaces.cend(); ++i) used.insert(i.key());
    return used;
}
QList<PluginPackage> projectPackages(const QVariantMap &project)
{
    QList<PluginPackage> result;
    for (const auto &entry : project.value("plugin_packages").toList()) result.append(PluginPackage::read(entry.toMap().value("data").toByteArray()));
    return result;
}
}

PluginStore::PluginStore(QString directory, QSettings &settings, Catalog::PythonCall python) : directory_(std::move(directory)), settings_(settings), python_(std::move(python))
{
    if (!QDir().mkpath(directory_)) throw Error("无法创建插件库目录");
    const QDir folder(directory_);
    const auto scanned = packagesInDirectory(directory_);
    for (auto i = scanned.cbegin(); i != scanned.cend(); ++i) {
        const auto package = i.value();
        if (installed_.contains(package.id())) throw Error("插件库存在同 ID 的多个文件，请保留一份：" + locations_.value(package.id()) + "、" + i.key());
        installed_.insert(package.id(),package); locations_.insert(package.id(),i.key());
    }
    const auto disabled = settings_.value("plugins/disabled").toStringList();
    disabled_ = QSet<QString>(disabled.begin(),disabled.end());
    const auto removed = settings_.value("plugins/removed").toStringList();
    const bool initialized = settings_.value("plugins/initialized",false).toBool();
    const auto bundled = packagesInDirectory(QCoreApplication::applicationDirPath() + "/plugins");
    for (auto i = bundled.cbegin(); i != bundled.cend(); ++i) {
        const auto package = i.value();
        const bool missingFirstInstall = !initialized && !installed_.contains(package.id());
        const bool upgrade = installed_.contains(package.id()) && QVersionNumber::fromString(package.version()) > QVersionNumber::fromString(installed_.value(package.id()).version());
        if (!removed.contains(package.id()) && (missingFirstInstall || upgrade)) {
            const auto location = locations_.value(package.id(),i.key());
            package.save(folder.filePath(location)); installed_.insert(package.id(),package); locations_.insert(package.id(),location);
        }
    }
    settings_.setValue("plugins/initialized",true);
}

Catalog PluginStore::catalog(const QVariantMap &project, bool includeDisabled) const
{
    QReadLocker lock(&lock_);
    QMap<QString,PluginPackage> located;
    for (auto i = installed_.cbegin(); i != installed_.cend(); ++i) located.insert(locations_.value(i.key()),i.value());
    Catalog result(located,python_); result.applyProject(project);
    // Opening a self-contained project activates its declared dependencies for
    // that project; a local library preference cannot strand an embedded copy.
    const auto required = usedPlugins(project);
    // Management previews inspect installed declarations even when disabled.
    // Execution and normal library discovery keep the local enable preference.
    if (!includeDisabled) for (const auto &id : disabled_) if (!required.contains(id)) result.remove(id);
    return result;
}

QVariantMap PluginStore::readiness(const QVariantMap &project) const
{
    QVariantList conflicts, missing, restarts;
    const auto choices = project.value("plugin_selections").toMap();
    QMap<QString,PluginPackage> attached;
    for (const auto &package : projectPackages(project)) attached.insert(package.id(),package);
    auto dependencies = project.value("plugin_dependencies").toList();
    if (dependencies.isEmpty()) dependencies = project.value("plugin_packages").toList();
    {
        QReadLocker lock(&lock_);
        for (const auto &entry : dependencies) {
            const auto expected = entry.toMap(); const auto id = expected.value("id").toString();
            if (!installed_.contains(id) && !attached.contains(id)) { missing.append(QVariantMap{{"id",id},{"version",expected.value("version")}}); continue; }
            // A package version is its compatibility identity. Rebuilding an
            // unchanged version changes bytes but not the contract, so content
            // hashes only break ties between genuinely different versions.
            if (!installed_.contains(id)) continue;
            if (installed_.value(id).version() == expected.value("version").toString()) continue;
            const auto choice = choices.value(id).toString();
            if (choice == "local" || (choice == "project" && attached.contains(id))) continue;
            const auto local = installed_.value(id);
            conflicts.append(QVariantMap{{"id",id},{"name",attached.contains(id) ? attached.value(id).manifest().value("name",id) : QVariant(id)},
                {"project_version",expected.value("version")},{"local_version",local.version()},
                {"project_available",attached.contains(id)},{"project_hash",expected.value("sha256")},{"local_hash",local.hash()}});
        }
    }
    if (!missing.isEmpty()) return {{"requires_plugins",true},{"missing_plugins",missing}};
    if (!conflicts.isEmpty()) return {{"requires_plugin_selection",true},{"conflicts",conflicts}};
    const auto selected = catalog(project); QSet<QString> checked;
    for (const auto &id : usedPlugins(project)) {
        const auto package = selected.package(id);
        if (checked.contains(package.id()) || package.manifest().value("implementation") != "native") continue;
        checked.insert(package.id()); const auto loaded = loadedNativeHash(package.id());
        if (!loaded.isEmpty() && loaded != package.hash()) restarts.append(QVariantMap{{"id",package.id()},{"version",package.version()}});
    }
    return restarts.isEmpty() ? QVariantMap{} : QVariantMap{{"requires_restart",true},{"plugins",restarts}};
}

QVariantMap PluginStore::state(const QVariantMap &project) const
{
    QMap<QString,PluginPackage> available, pending; QMap<QString,QString> paths; QSet<QString> disabled;
    {
        QReadLocker lock(&lock_); available = installed_; pending = pending_; paths = locations_; disabled = disabled_;
    }
    // Local management describes installed files and local enable choices.
    // Project-contained versions are reported only by the project inventory.
    const auto required = usedPlugins(project);
    QVariantList packages, plugins;
    for (const auto &package : available) {
        bool inUse = false;
        for (const auto &entry : package.definitions()) inUse |= required.contains(entry.toMap().value("id").toString());
        packages.append(QVariantMap{{"id",package.id()},{"name",package.manifest().value("name",package.id())},{"version",package.version()},
            {"implementation",package.manifest().value("implementation")},{"official",package.manifest().value("official",false)},
            {"relative_path",paths.value(package.id())},{"installed",true},{"in_use",inUse},
            {"pending_version",pending.contains(package.id()) ? pending.value(package.id()).version() : QString{}}});
        for (const auto &entry : package.definitions()) {
            const auto definition = entry.toMap(); const auto id = definition.value("id").toString();
            plugins.append(QVariantMap{{"id",id},{"name",definition.value("name")},{"package",package.id()},
                {"version",definition.value("version")},{"enabled",!disabled.contains(id)},{"in_use",required.contains(id)},{"official",definition.value("official")}});
        }
    }
    return {{"packages",packages},{"plugins",plugins},{"directory",directory_},{"restart_required",!pending.isEmpty()},{"tree",tree(project,"library").value("tree")}};
}

QVariantMap PluginStore::install(const PluginPackage &package, const QString &fileName)
{
    QWriteLocker lock(&lock_);
    auto candidates = installed_; candidates.remove(package.id());
    Catalog trial(candidates.values(),python_); trial.addPackage(package);
    const auto loaded = loadedNativeHash(package.id());
    const bool restart = !loaded.isEmpty() && loaded != package.hash();
    if (package.manifest().value("implementation") == "native") validateNativePackage(package);
    for (const auto &definition : package.definitions()) {
        const auto spec = definition.toMap();
        if (restart) checkedProperties(spec.value("properties").toMap(),{});
        else trial.checkedConfig(spec.value("id").toString(),{});
    }
    const auto location = locations_.value(package.id(),fileName.isEmpty() ? package.id()+".crpl" : QFileInfo(fileName).completeBaseName()+".crpl");
    const auto destination = QDir(directory_).filePath(PluginPackage::checkedPath(location));
    if (QFileInfo::exists(destination) && !installed_.contains(package.id())) throw Error("目标目录已经有同名插件包，请先重命名：" + location);
    package.save(destination); locations_.insert(package.id(),location);
    if (restart) pending_.insert(package.id(),package); else installed_.insert(package.id(),package);
    auto removed = settings_.value("plugins/removed").toStringList(); removed.removeAll(package.id()); settings_.setValue("plugins/removed",removed);
    return {{"id",package.id()},{"version",package.version()},{"restart_required",restart}};
}

void PluginStore::setEnabled(const QStringList &plugins, bool enabled, const QVariantMap &project)
{
    QWriteLocker lock(&lock_);
    if (plugins.isEmpty()) throw Error("请选择需要启用或停用的插件");
    QSet<QString> available;
    for (const auto &package : installed_) for (const auto &entry : package.definitions()) available.insert(entry.toMap().value("id").toString());
    for (const auto &id : plugins) if (!available.contains(id)) throw Error("本机插件不存在：" + id);
    const QSet<QString> selected(plugins.begin(),plugins.end());
    QStringList references;
    if (!enabled) for (const auto &entry : project.value("nodes").toList()) {
        const auto node = entry.toMap(); if (selected.contains(node.value("plugin").toString())) references.append(node.value("title",node.value("id")).toString());
    }
    if (!enabled) for (const auto &entry : project.value("ui").toMap().value("items").toList()) {
        const auto item = entry.toMap(); const auto binding = item.value("binding").toMap();
        if (selected.contains(binding.value("plugin").toString())) references.append(binding.value("title",item.value("id")).toString());
        if (selected.contains(item.value("plugin").toString())) references.append(item.value("id").toString());
    }
    if (!enabled) for (const auto &id : selected) if (project.value("workspaces").toMap().contains(id)) references.append(id);
    // Validate the whole subtree before updating any preference, so a used or
    // unknown plugin cannot leave a category only partly disabled.
    references.removeDuplicates();
    if (!references.isEmpty()) throw Error("插件仍被当前工程使用，请先处理引用：" + references.join("、"));
    for (const auto &id : selected) { if (enabled) disabled_.remove(id); else disabled_.insert(id); }
    settings_.setValue("plugins/disabled",QStringList(disabled_.values()));
}

void PluginStore::remove(const QString &id, const QVariantMap &project)
{
    QWriteLocker lock(&lock_);
    if (!installed_.contains(id)) throw Error("本机插件包不存在：" + id);
    const auto used = usedPlugins(project);
    QStringList references;
    for (const auto &entry : installed_.value(id).definitions()) if (used.contains(entry.toMap().value("id").toString())) references.append(entry.toMap().value("name").toString());
    if (!references.isEmpty()) throw Error("插件包仍被当前工程使用：" + references.join("、"));
    const auto path = QDir(directory_).filePath(locations_.value(id));
    if (!QFile::remove(path)) throw Error("无法移除插件包：" + path);
    installed_.remove(id); pending_.remove(id); locations_.remove(id);
    auto removed = settings_.value("plugins/removed").toStringList(); if (!removed.contains(id)) removed.append(id); settings_.setValue("plugins/removed",removed);
}

PluginPackage PluginStore::find(const QString &id, const QVariantMap &project) const
{
    // The active catalog resolves both package IDs and node/plugin IDs to the
    // exact version selected for this project, including embedded copies.
    const auto selected = catalog(project);
    try { return selected.package(id); } catch (const Error &) {}
    QReadLocker lock(&lock_);
    if (installed_.contains(id)) return installed_.value(id);
    throw Error("找不到可导出的插件：" + id);
}
namespace {
struct Folder {
    QString name, path;
    QMap<QString,std::shared_ptr<Folder>> children;
    QVariantList files;
};
QVariantList describeFolders(const Folder &root)
{
    QVariantList result;
    for (const auto &folder : root.children) result.append(QVariantMap{{"kind","folder"},{"name",folder->name},{"path",folder->path},{"children",describeFolders(*folder)}});
    for (const auto &file : root.files) result.append(file);
    return result;
}
}
QVariantMap PluginStore::tree(const QVariantMap &project, const QString &scope) const
{
    if (scope != "project" && scope != "library") throw Error("插件树范围需要 project 或 library");
    QMap<QString,PluginPackage> available; QMap<QString,QString> paths;
    if (scope == "project") {
        const auto selected = catalog(project);
        for (const auto &entry : selected.embed(project).value("plugin_dependencies").toList()) {
            const auto id = entry.toMap().value("id").toString(); available.insert(id,selected.package(id)); paths.insert(id,selected.packagePath(id));
        }
    } else {
        QReadLocker lock(&lock_); available = installed_; paths = locations_;
    }
    Folder root; QVariantList flattened;
    for (const auto &package : available) {
        const auto relative = paths.value(package.id()); const auto directory = QFileInfo(relative).path();
        const auto parts = directory == "." ? QStringList{"其他"} : directory.split('/');
        Folder *parent = &root; QStringList prefix;
        for (const auto &part : parts) {
            prefix.append(part);
            if (!parent->children.contains(part)) { auto child = std::make_shared<Folder>(); child->name = part; child->path = prefix.join('/'); parent->children.insert(part,std::move(child)); }
            parent = parent->children.value(part).get();
        }
        QVariantMap file{{"kind","plugin"},{"id",package.id()},{"name",package.manifest().value("name",package.id())},
            {"version",package.version()},{"relative_path",relative},{"bytes",package.archive().size()},{"official",package.manifest().value("official",false)}};
        parent->files.append(file); flattened.append(file);
    }
    return {{"tree",describeFolders(root)},{"plugins",flattened},{"scope",scope}};
}
}
