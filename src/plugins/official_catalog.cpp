#include "official_catalog.h"

namespace corig {
QVariantMap officialConfiguration(const QVariantMap &spec, const QVariantMap &changes, const QVariantMap &previous)
{
    const auto id = spec.value("id").toString();
    auto config = checkedProperties(spec.value("properties").toMap(),changes,previous);
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

QVector<Port> officialPorts(const QVariantMap &spec, const QVariantMap &config, QVector<Port> result)
{
    const auto id = spec.value("id").toString();
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
    return result;
}
}
