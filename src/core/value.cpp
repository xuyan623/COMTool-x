#include "value.h"
#include <QUuid>
#include <QPointF>
#include <cmath>

namespace cx {

QVariant toJsonValue(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QVector<QPointF>>()) {
        QVariantList result;
        const auto points = value.value<QVector<QPointF>>(); result.reserve(points.size());
        // QVariantList::append(list) concatenates. Wrap each pair so the API
        // retains its [point][coordinate] shape.
        for (const auto &p : points) result.append(QVariant(QVariantList{p.x(),p.y()}));
        return result;
    }
    if (value.metaType().id() == QMetaType::QByteArray) {
        const auto bytes = value.toByteArray();
        return QVariantMap{{"type", "bytes"}, {"base64", QString::fromLatin1(bytes.toBase64())}, {"length", bytes.size()}};
    }
    if (value.metaType().id() == QMetaType::QVariantMap) {
        auto map = value.toMap();
        for (auto i = map.begin(); i != map.end(); ++i) i.value() = toJsonValue(i.value());
        return map;
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        auto list = value.toList();
        for (auto &item : list) item = toJsonValue(item);
        return list;
    }
    if (value.metaType().id() == QMetaType::Double && !std::isfinite(value.toDouble())) return {};
    if (value.metaType().id() >= QMetaType::User && value.canConvert<QVariantMap>()) return toJsonValue(value.toMap());
    return value;
}

QVariant fromJsonValue(const QVariant &value)
{
    if (value.metaType().id() == QMetaType::QVariantMap) {
        auto map = value.toMap();
        if (map.value("type") == "bytes" && map.contains("base64")) {
            const auto decoded = QByteArray::fromBase64Encoding(map.value("base64").toByteArray(), QByteArray::AbortOnBase64DecodingErrors);
            if (!decoded) throw Error("字节数据不是有效的 Base64");
            return decoded.decoded;
        }
        for (auto i = map.begin(); i != map.end(); ++i) i.value() = fromJsonValue(i.value());
        return map;
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        auto list = value.toList();
        for (auto &item : list) item = fromJsonValue(item);
        return list;
    }
    return value;
}

double finiteNumber(const QVariant &value, const QString &name)
{
    if (!value.isValid()) return 0.;
    const int type = value.metaType().id();
    bool ok = false;
    const auto number = value.toDouble(&ok);
    if (type == QMetaType::Bool || type == QMetaType::QString || !ok || !std::isfinite(number))
        throw Error(name + " 需要有限数值");
    return number;
}

double timeUnit(const QString &unit)
{
    static const QHash<QString, double> units{{"s",1.}, {"ms",.001}, {"us",.000001}, {"ns",.000000001}, {"min",60.}, {"h",3600.}};
    if (!units.contains(unit)) throw Error("未知时间单位：" + unit);
    return units.value(unit);
}

QVariant defaultValue(const QString &type)
{
    if (type == "number") return 0.;
    if (type == "boolean") return false;
    return {};
}

bool valueMatches(const QVariant &value, const QString &type)
{
    if (!value.isValid() || type == "any" || type == "trigger") return true;
    const int id = value.metaType().id();
    if (type == "bytes") return id == QMetaType::QByteArray;
    if (type == "string") return id == QMetaType::QString;
    if (type == "boolean") return id == QMetaType::Bool;
    if (type == "object") return id == QMetaType::QVariantMap || id == QMetaType::QVariantList;
    if (type == "samples") {
        try { sampleFrames(value); return true; } catch (const Error &) { return false; }
    }
    if (type == "number") {
        try { finiteNumber(value, "输出"); return true; } catch (const Error &) { return false; }
    }
    return false;
}

QVariantList sampleFrames(const QVariant &value)
{
    const auto frames = value.metaType().id() == QMetaType::QVariantList ? value.toList() : QVariantList{value};
    if (frames.size() > 65536) throw Error("单次采样帧数量过多");
    for (const auto &item : frames) {
        const auto frame = item.toMap();
        if (!frame.contains("time") || frame.value("values").metaType().id() != QMetaType::QVariantMap)
            throw Error("采样需要 {time: 秒, values: {通道名: 数值}} 格式");
        finiteNumber(frame.value("time"), "采样时间");
        const auto values = frame.value("values").toMap();
        for (auto i = values.cbegin(); i != values.cend(); ++i) {
            if (i.key().isEmpty()) throw Error("采样通道名不能为空");
            finiteNumber(i.value(), i.key());
        }
    }
    return frames;
}

QString newId(const QString &prefix)
{
    return prefix + "_" + QUuid::createUuid().toString(QUuid::Id128).left(12);
}

} // namespace cx
