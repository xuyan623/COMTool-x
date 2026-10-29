#include "catalog.h"
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <cmath>
#include <algorithm>

static void initializeCatalogResource()
{
    Q_INIT_RESOURCE(builtin_catalog);
}

namespace corig {

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
        const auto spec = schema.value(i.key()).toMap();
        const auto type = spec.value("type").toString();
        const auto value = i.value();
        if (!value.isValid() || value.isNull()) { if (spec.value("required").toBool()) throw Error("缺少必填参数：" + i.key()); continue; }
        const auto id = value.metaType().id();
        if ((type == "string" && id != QMetaType::QString) || (type == "boolean" && id != QMetaType::Bool))
            throw Error(i.key() + " 参数类型不正确");
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

Catalog::Catalog()
{
    static const bool initialized = [] { initializeCatalogResource(); return true; }();
    Q_UNUSED(initialized);
    QFile file(":/catalog.json");
    if (!file.open(QIODevice::ReadOnly)) throw Error("内置插件声明无法读取");
    const auto nodes = QJsonDocument::fromJson(file.readAll()).toVariant().toList();
    for (const auto &item : nodes) {
        auto spec = item.toMap();
        spec.insert("builtin", true);
        definitions_.insert(spec.value("id").toString(), spec);
    }
}

QVariantMap Catalog::definition(const QString &id) const
{
    if (!definitions_.contains(id)) throw Error("插件不存在：" + id);
    return definitions_.value(id);
}

QVariantList Catalog::describe() const
{
    QVariantList result;
    for (auto i = definitions_.cbegin(); i != definitions_.cend(); ++i) {
        auto spec = i.value();
        QVariantList resolved;
        const auto config = checkedConfig(i.key(), {});
        for (const auto &port : ports(i.key(), config)) resolved.append(port.describe());
        spec.insert("ports", resolved);
        result.append(spec);
    }
    return result;
}

QVariantMap Catalog::checkedConfig(const QString &id, const QVariantMap &changes, const QVariantMap &previous) const
{
    const auto spec = definition(id);
    auto config = checkedProperties(spec.value("properties").toMap(), changes, previous);
    ports(id, config);
    if (config.contains("time_unit")) timeUnit(config.value("time_unit").toString());
    if (id == "function.limit" && config.contains("minimum") && config.value("minimum").toDouble() > config.value("maximum").toDouble())
        throw Error("最小值不能大于最大值");
    if (id == "source.constant") {
        const auto values = config.value("values").toMap();
        if (values.isEmpty()) throw Error("常量需要至少一个命名数值");
        for (auto i = values.cbegin(); i != values.cend(); ++i) finiteNumber(i.value(), i.key());
    }
    if (id == "source.time") {
        for (const auto &item : config.value("outputs").toList()) {
            const auto output = item.toMap();
            if (output.value("clock") != "elapsed" && output.value("clock") != "system") throw Error("时间源时钟需要 elapsed 或 system");
            timeUnit(output.value("unit").toString());
        }
    }
    return config;
}

QVector<Port> Catalog::ports(const QString &id, const QVariantMap &config) const
{
    const auto spec = definition(id);
    QVector<Port> result;
    for (const auto &item : spec.value("ports").toList()) {
        const auto p = item.toMap();
        result.append({p.value("id").toString(), spec.value("builtin").toBool() ? p.value("id").toString() : p.value("label").toString(), p.value("direction").toString(), p.value("data_type").toString(), p.value("unit").toString()});
    }
    const auto named = [&](const QString &name, const QString &direction, const QString &type = "number", const QString &label = QString{}, const QString &unit = QString{}) {
        result.append({name, label.isEmpty() ? name : label, direction, type, unit});
    };
    if (id == "source.constant") {
        const auto values = config.value("values").toMap();
        for (auto i = values.cbegin(); i != values.cend(); ++i) named(i.key(), "out");
    } else if (id == "source.time") {
        for (const auto &item : config.value("outputs").toList()) {
            const auto p = item.toMap();
            named(p.value("name").toString(), "out", "number", p.value("name").toString() + " / " + p.value("unit").toString(), p.value("unit").toString());
        }
    } else if (id == "data.extract") {
        const auto channels = config.value("channels").toMap();
        if (channels.isEmpty()) throw Error("需要配置提取通道");
        for (auto i = channels.cbegin(); i != channels.cend(); ++i) {
            if (i.value().toString().isEmpty()) throw Error("来源通道名不能为空");
            named(i.key(), "out");
        }
    } else if (id == "data.fork") {
        const auto outputs = config.value("outputs").toList();
        if (outputs.size() < 2 || outputs.size() > 8) throw Error("数据分发需要 2～8 个输出端口");
        named("in", "in", config.value("data_type").toString());
        for (const auto &name : outputs) named(name.toString(), "out", config.value("data_type").toString());
    } else if (id == "data.record" || id == "data.playback") {
        named("in","in",config.value("data_type").toString());
        if (id == "data.record") { named("start","in","boolean"); named("pause","in","boolean"); }
        named("out","out",config.value("data_type").toString());
    } else if (id == "data.gate" || id == "data.select") {
        const auto type = config.value("data_type").toString();
        if (id == "data.gate") named("in", "in", type);
        else { named("a", "in", type); named("b", "in", type); }
        named("out", "out", type);
    } else if (id == "function.expression" || id == "function.python") {
        for (const auto &name : config.value("inputs").toList()) named(name.toString(), "in");
        if (id == "function.python") {
            for (const auto &name : config.value("outputs").toList()) named(name.toString(), "out");
        } else {
            const auto outputs = config.value("outputs").toMap();
            for (auto i = outputs.cbegin(); i != outputs.cend(); ++i) named(i.key(), "out");
        }
        named("samples", "out", "samples");
    } else if (id == "plot.curves") {
        const auto labels = config.value("input_labels").toMap();
        for (const auto &name : config.value("inputs").toList()) named(name.toString(), "in", "number", labels.value(name.toString(), name).toString());
    } else if (id == "plot.attitude") {
        if (config.value("mode") == "quaternion") {
            for (const auto &name : {"w","x","y","z"}) named(name, "in");
        } else {
            for (const auto &name : {"roll","pitch","yaw"}) named(name, "in", "number", name, config.value("angle_unit").toString());
        }
    } else if (spec.value("dynamic_ports").toBool() && id.startsWith("function.")) {
        for (const auto &name : config.value("inputs").toList()) named(name.toString(), "in");
    }
    static const QRegularExpression identifier("^[A-Za-z_][A-Za-z0-9_.-]*$");
    static const QSet<QString> types{"number","bytes","string","boolean","object","samples","any"};
    QSet<QString> seen;
    if (result.size() > 128) throw Error("一个节点最多支持 128 个端口");
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
    spec.insert("builtin", false);
    spec.insert("implementation", "python");
    spec.insert("category", spec.value("category", "用户插件"));
    spec.insert("version", spec.value("version", "1.0"));
    if (spec.contains("source_interval_ms")) { const auto interval = finiteNumber(spec.value("source_interval_ms"),"用户信号源周期"); if (interval < 0. || interval > 60000.) throw Error("用户信号源周期需要 0～60000 ms"); }
    definitions_.insert(id, spec);
    try { checkedConfig(id, {}); } catch (...) { definitions_.remove(id); throw; }
}

void Catalog::clearPython()
{
    for (auto i = definitions_.begin(); i != definitions_.end();) {
        if (!i.value().value("builtin").toBool()) i = definitions_.erase(i); else ++i;
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
        if (words.isEmpty() && !category.isEmpty() && path.mid(0, category.size()) != category) continue;
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
