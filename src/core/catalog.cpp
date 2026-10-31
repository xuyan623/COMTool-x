#include "catalog.h"
#include "native_plugin.h"
#include "resource_policy.h"
#include "plugin_script.h"
#include <QFile>
#include <QDir>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <cmath>
#include <algorithm>

namespace corig {

namespace {
void checkSavedProperty(const QVariant &value, const QString &name, int depth = 0)
{
    if (depth > 128) throw Error(name+" 的数据嵌套过深");
    if (!value.isValid() || value.isNull()) return;
    const auto type = value.metaType().id();
    if (type == QMetaType::QVariantMap) {
        const auto map = value.toMap(); for (auto i = map.cbegin(); i != map.cend(); ++i) checkSavedProperty(i.value(),name+"."+i.key(),depth+1);
    } else if (type == QMetaType::QVariantList || type == QMetaType::QStringList) {
        for (const auto &item : value.toList()) checkSavedProperty(item,name,depth+1);
    } else if (type == QMetaType::QString || type == QMetaType::Bool || type == QMetaType::QByteArray) return;
    else if (type == QMetaType::Double || type == QMetaType::Float || type == QMetaType::Int || type == QMetaType::UInt || type == QMetaType::LongLong || type == QMetaType::ULongLong) finiteNumber(value,name);
    else throw Error(name+" 包含不能保存的数据类型");
}
}

QVariantMap Port::describe() const
{
    return {{"id",id}, {"label",label}, {"direction",direction}, {"data_type",type}, {"unit",unit}};
}

QVariantMap checkedProperties(const QVariantMap &schema, const QVariantMap &changes, const QVariantMap &previous)
{
    auto result = previous;
    for (auto i = schema.cbegin(); i != schema.cend(); ++i)
        if (!result.contains(i.key())) result.insert(i.key(), i.value().toMap().value("default"));
    for (auto i = changes.cbegin(); i != changes.cend(); ++i) { if (!schema.contains(i.key())) throw Error("不支持的参数：" + i.key()); result[i.key()] = i.value(); }
    for (auto i = result.cbegin(); i != result.cend(); ++i) {
        if (!schema.contains(i.key())) throw Error("不支持的参数："+i.key());
        const auto spec = schema.value(i.key()).toMap();
        const auto type = spec.value("type").toString();
        if (!QStringList{"string","boolean","number","integer","json","object","array"}.contains(type)) throw Error("参数类型未声明或不受支持："+i.key());
        const auto value = i.value();
        if (!value.isValid() || value.isNull()) { if (spec.value("required").toBool()) throw Error("缺少必填参数：" + i.key()); continue; }
        const auto id = value.metaType().id();
        if ((type == "string" && id != QMetaType::QString) || (type == "boolean" && id != QMetaType::Bool))
            throw Error(i.key() + " 参数类型不正确");
        if ((type == "object" && id != QMetaType::QVariantMap) || (type == "array" && id != QMetaType::QVariantList && id != QMetaType::QStringList)) throw Error(i.key()+" 参数类型不正确");
        checkSavedProperty(value,i.key());
        if (type == "number" || type == "integer") {
            const auto n = finiteNumber(value, i.key());
            if (type == "integer" && n != std::floor(n)) throw Error(i.key() + " 需要整数");
            if (spec.contains("min") && n < spec.value("min").toDouble()) throw Error(i.key() + " 小于允许范围");
            if (spec.contains("max") && n > spec.value("max").toDouble()) throw Error(i.key() + " 超过允许范围");
        }
        if (spec.contains("choices") && !spec.value("choices").toList().contains(value))
            throw Error(i.key() + " 不是可选值");
        if (type == "string" && spec.contains("pattern")) {
            const QRegularExpression expression(QRegularExpression::anchoredPattern(spec.value("pattern").toString()));
            if (!expression.isValid() || !expression.match(value.toString()).hasMatch()) throw Error(i.key() + " 格式不正确");
        }
    }
    return result;
}

Catalog::Catalog() : Catalog(packagesInDirectory(QCoreApplication::applicationDirPath() + "/plugins")) {}

Catalog::Catalog(const QList<PluginPackage> &packages, PythonCall python) : python_(std::move(python))
{
    for (const auto &package : packages) addPackage(package);
}

Catalog::Catalog(const QMap<QString,PluginPackage> &packages, PythonCall python) : python_(std::move(python))
{
    for (auto i = packages.cbegin(); i != packages.cend(); ++i) addPackage(i.value(),false,i.key());
}

void Catalog::addPackage(const PluginPackage &package, bool replace, const QString &relativePath)
{
    if (packages_.contains(package.id())) {
        if (packages_.value(package.id()).hash() == package.hash()) return;
        if (!replace) throw Error("插件包标识已被使用：" + package.id());
    }
    const auto location = PluginPackage::checkedPath(relativePath.isEmpty() ? package.id()+".crpl" : relativePath);
    auto category = QFileInfo(location).path();
    const auto parts = category == "." ? QStringList{"其他"} : category.split('/');
    auto next = definitions_;
    if (replace) for (auto i = next.begin(); i != next.end();) {
        if (i.value().value("package_id") == package.id()) i = next.erase(i); else ++i;
    }
    for (const auto &entry : package.definitions()) {
        auto spec = entry.toMap(); const auto id = spec.value("id").toString();
        if (spec.value("implementation") == "python") spec["package_directory"] = package.materialize();
        spec["category_path"] = parts; spec["package_path"] = location;
        if (next.contains(id)) throw Error("插件标识已被使用：" + id);
        next.insert(id,spec);
    }
    definitions_ = std::move(next); packages_.insert(package.id(),package); locations_.insert(package.id(),location);
}

void Catalog::remove(const QString &id) { definitions_.remove(id); }

void Catalog::applyProject(const QVariantMap &project)
{
    const auto choices = project.value("plugin_selections").toMap();
    QMap<QString,PluginPackage> embedded;
    QMap<QString,QString> paths;
    auto dependencies = project.value("plugin_dependencies").toList();
    for (const auto &entry : project.value("plugin_packages").toList()) {
        const auto item = entry.toMap(); const auto package = PluginPackage::read(item.value("data").toByteArray());
        if (package.id() != item.value("id").toString() || package.hash() != item.value("sha256").toString()) throw Error("工程内插件包标识或校验值不一致");
        embedded.insert(package.id(),package); paths.insert(package.id(),item.value("relative_path",package.id()+".crpl").toString());
        if (project.value("plugin_dependencies").toList().isEmpty()) dependencies.append(item);
    }
    for (const auto &entry : dependencies) {
        const auto expected = entry.toMap(); const auto id = expected.value("id").toString();
        const auto choice = choices.value(id).toString();
        const bool local = packages_.contains(id), attached = embedded.contains(id);
        if (!local && !attached) throw Error("工程缺少插件：" + id + " " + expected.value("version").toString());
        // Version, not container bytes, decides compatibility. Identical
        // versions may differ in ZIP metadata or rebuild timestamps; an
        // explicit project/local decision still covers a genuinely different
        // version even when the file was rebuilt from the same sources.
        const bool different = local && packages_.value(id).version() != expected.value("version").toString();
        if (different && choice != "project" && choice != "local") throw Error("请选择工程内或本机插件版本：" + id);
        if (choice == "project" && !attached) throw Error("工程没有携带此插件，请导入需要的版本：" + id);
        if (attached && (!local || choice == "project")) addPackage(embedded.value(id),true,paths.value(id));
    }
    // Existing CoRig v3 Python declarations remain readable. Their old category
    // fields are discarded when converted into portable package contents.
    for (const auto &entry : project.value("user_plugins").toList()) {
        const auto package = PluginPackage::fromPython(entry.toMap());
        if (!packages_.contains(package.id())) addPackage(package);
    }
}

PluginPackage Catalog::package(const QString &plugin) const
{
    const auto id = packages_.contains(plugin) ? plugin : definition(plugin).value("package_id").toString();
    if (!packages_.contains(id)) throw Error("插件没有可导出的插件包：" + plugin);
    return packages_.value(id);
}

PluginPackage Catalog::servicePackage(const QString &capability) const
{
    for (const auto &package : packages_) if (package.manifest().value("capabilities").toStringList().contains(capability)) return package;
    throw Error("未安装提供此功能的插件：" + capability);
}

QString Catalog::packagePath(const QString &plugin) const
{
    return locations_.value(package(plugin).id());
}

QVariantMap Catalog::embed(const QVariantMap &project, bool includePlugins) const
{
    QSet<QString> used;
    for (const auto &entry : project.value("nodes").toList()) used.insert(entry.toMap().value("plugin").toString());
    for (const auto &entry : project.value("ui").toMap().value("items").toList()) {
        const auto item = entry.toMap();
        const auto container = item.value("plugin").toString();
        if (!container.isEmpty() && definitions_.contains(container)) used.insert(container);
        const auto snapshot = item.value("binding").toMap();
        if (!snapshot.value("plugin").toString().isEmpty()) used.insert(snapshot.value("plugin").toString());
    }
    const auto workspaces = project.value("workspaces").toMap();
    for (auto i = workspaces.cbegin(); i != workspaces.cend(); ++i) used.insert(i.key());
    QMap<QString,PluginPackage> selected;
    for (const auto &plugin : used) { const auto p = package(plugin); selected.insert(p.id(),p); }
    QVariantList embedded, dependencies;
    for (const auto &p : selected) {
        QVariantMap info{{"id",p.id()},{"version",p.version()},{"sha256",p.hash()},{"relative_path",locations_.value(p.id())}};
        dependencies.append(info);
        if (includePlugins) { info["data"] = p.archive(); embedded.append(info); }
    }
    auto result = project; result["plugin_packages"] = embedded; result["plugin_dependencies"] = dependencies;
    result["save_options"] = QVariantMap{{"include_plugins",includePlugins}};
    result["user_plugins"] = QVariantList{}; result.remove("plugin_selections");
    return result;
}

QVariantMap Catalog::definition(const QString &id) const
{
    if (!definitions_.contains(id)) throw Error("插件不存在：" + id);
    return definitions_.value(id);
}

QString Catalog::kind(const QString &id) const
{
    static const QSet<QString> containers{"layout.canvas","layout.row","layout.column","layout.grid","layout.tabs"};
    if (containers.contains(id)) return "container";
    return definition(id).value("kind","node").toString();
}

QVariantList Catalog::describe() const
{
    QVariantList result;
    for (auto i = definitions_.cbegin(); i != definitions_.cend(); ++i) {
        auto spec = i.value();
        if (spec.value("kind","node") != "node") { result.append(spec); continue; }
        QVariantList resolved;
        try {
            const auto config = checkedConfig(i.key(), {});
            for (const auto &port : ports(i.key(), config)) resolved.append(port.describe());
        } catch (const std::exception &error) {
            // A missing native dependency must remain visible and removable
            // through discovery without taking down the application settings.
            spec["load_error"] = QString::fromUtf8(error.what());
        }
        spec.insert("ports", resolved);
        result.append(spec);
    }
    return result;
}

QVariantMap Catalog::checkedConfig(const QString &id, const QVariantMap &changes, const QVariantMap &previous) const
{
    const auto spec = definition(id);
    checkResourceRequirements(package(id).manifest().value("requirements").toMap(),id);
    if (kind(id) != "node") return checkedPresentation(id,kind(id),spec.value("properties").toMap(),changes,previous);
    auto config = spec.value("implementation") == "native"
        ? nativePlugin(package(id)).configuration(spec,changes,previous)
        : checkedProperties(spec.value("properties").toMap(),changes,previous);
    if (!spec.value("hooks").toMap().value("configure").toString().isEmpty()) {
        const auto returned = hook(id,"configure",config,{{"scope","node"},{"previous",previous},{"schema",spec.value("properties")}});
        if (returned.metaType().id() != QMetaType::QVariantMap) throw Error("configure 必须返回配置对象："+id);
        config = checkedProperties(spec.value("properties").toMap(),returned.toMap());
    }
    ports(id, config);
    return config;
}

QVariant Catalog::hook(const QString &id, const QString &name, const QVariantMap &config, const QVariantMap &context) const
{
    const auto spec = definition(id); const auto hooks = spec.value("hooks").toMap();
    const auto entry = hooks.value(name).toString(); const auto script = hooks.value("script").toString();
    auto details = context; details["plugin"] = id;
    if (!script.isEmpty()) return pluginScript(package(id),script,entry,{config,details});
    if (spec.value("implementation") != "python" || !python_) throw Error("此插件校验需要已配置的 Python 环境："+id);
    const auto stored = package(id);
    return python_({{"package",stored.id()},{"node","metadata."+id},{"op","metadata"},{"metadata",true},
        {"directory",stored.materialize()},{"code_file",spec.value("code_file")},{"entry",entry},
        {"dependencies",spec.value("dependencies")},{"config",config},{"context",details}}).value("result");
}

QVariantMap Catalog::checkedPresentation(const QString &id, const QString &scope, const QVariantMap &schema,
                                         const QVariantMap &changes, const QVariantMap &previous) const
{
    auto spec = definition(id); auto candidate = checkedProperties(schema,changes,previous);
    if (spec.value("implementation") == "native") {
        spec["properties"] = schema;
        candidate = nativePlugin(package(id)).presentationConfiguration(scope,spec,candidate,previous);
    }
    const auto name = scope == "view" ? QString("view_configure") : QString("configure");
    if (!spec.value("hooks").toMap().value(name).toString().isEmpty()) {
        const auto returned = hook(id,name,candidate,{{"scope",scope},{"previous",previous},{"schema",schema}});
        if (returned.metaType().id() != QMetaType::QVariantMap) throw Error(name+" 必须返回属性对象："+id);
        candidate = returned.toMap();
    }
    return checkedProperties(schema,candidate);
}

QVector<Port> Catalog::ports(const QString &id, const QVariantMap &config) const
{
    const auto spec = definition(id);
    QVector<Port> result;
    for (const auto &item : spec.value("ports").toList()) {
        const auto p = item.toMap();
        result.append({p.value("id").toString(), spec.value("official").toBool() ? p.value("id").toString() : p.value("label").toString(), p.value("direction").toString(), p.value("data_type").toString(), p.value("unit").toString()});
    }
    if (spec.value("implementation") == "native") result = nativePlugin(package(id)).ports(spec,config,std::move(result));
    if (!spec.value("hooks").toMap().value("ports").toString().isEmpty()) {
        QVariantList declared; for (const auto &port : result) declared.append(port.describe());
        const auto returned = hook(id,"ports",config,{{"scope","ports"},{"declared",declared}});
        if (returned.metaType().id() != QMetaType::QVariantList) throw Error("ports 必须返回端口数组："+id);
        result.clear();
        for (const auto &item : returned.toList()) { const auto p = item.toMap(); result.append({p.value("id").toString(),p.value("label",p.value("id")).toString(),p.value("direction").toString(),p.value("data_type").toString(),p.value("unit").toString()}); }
    }
    static const QRegularExpression identifier("^[A-Za-z_][A-Za-z0-9_.-]*$");
    static const QSet<QString> types{"number","bytes","string","boolean","object","samples","any"};
    QSet<QString> seen;
    if (result.size() > resourceLimit("ports")) throw Error(QString("节点端口数量超过当前 ports=%1 额度").arg(resourceLimit("ports")));
    for (const auto &port : result) {
        if (!identifier.match(port.id).hasMatch() || seen.contains(port.id) || !types.contains(port.type)
            || (port.direction != "in" && port.direction != "out")) throw Error("端口声明无效或重复：" + port.id);
        seen.insert(port.id);
    }
    return result;
}

void Catalog::addPython(const QVariantMap &manifest)
{
    auto spec = manifest;
    const auto id = spec.value("id").toString();
    if (spec.value("plugin_api").toInt() != 3 || !QRegularExpression("^[A-Za-z_][A-Za-z0-9_.-]+$").match(id).hasMatch())
        throw Error("用户插件需要 plugin_api=3 和有效 id");
    if (definitions_.contains(id)) throw Error("插件标识已被使用：" + id);
    if (spec.value("name").toString().isEmpty() || spec.value("code").toString().isEmpty()) throw Error("用户插件缺少名称或 Python 代码");
    spec.insert("category", spec.value("category", "用户插件"));
    spec.insert("version", spec.value("version", "1.0"));
    if (spec.contains("source_interval_ms")) { const auto interval = finiteNumber(spec.value("source_interval_ms"),"用户信号源周期"); if (interval < 0. || interval > 60000.) throw Error("用户信号源周期需要 0～60000 ms"); }
    auto trial = *this; trial.addPackage(PluginPackage::fromPython(spec));
    trial.checkedConfig(id, {}); *this = std::move(trial);
}

void Catalog::clearPython()
{
    for (auto i = definitions_.begin(); i != definitions_.end();) {
        if (i.value().value("implementation") == "python") i = definitions_.erase(i); else ++i;
    }
}

QVariantList Catalog::search(const QString &query, const QStringList &category) const
{
    struct Match { int score; QVariantMap spec; };
    QVector<Match> matches;
    const auto words = query.simplified().toLower().split(' ', Qt::SkipEmptyParts);
    for (auto i = definitions_.cbegin(); i != definitions_.cend(); ++i) {
        auto spec = i.value();
        auto path = spec.value("category_path").toStringList();
        if (path.isEmpty()) path.append(spec.value("category").toString());
        if (!category.isEmpty() && path.mid(0, category.size()) != category) continue;
        const auto name = spec.value("name").toString().toLower();
        const auto haystack = (name + " " + i.key() + " " + spec.value("description").toString() + " " + path.join(' ') + " " + spec.value("keywords").toStringList().join(' ')).toLower();
        bool found = true;
        int score = 0;
        for (const auto &word : words) { if (!haystack.contains(word)) found = false; if (name.startsWith(word)) score += 10; else if (name.contains(word)) score += 5; }
        if (found) { spec.insert("category_path", path); matches.append({score, spec}); }
    }
    std::stable_sort(matches.begin(), matches.end(), [](const auto &a, const auto &b) { return a.score > b.score; });
    QVariantList result;
    for (const auto &match : matches) result.append(match.spec);
    return result;
}

} // namespace corig
