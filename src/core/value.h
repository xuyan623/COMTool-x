#pragma once

// Runtime values keep binary buffers native. JSON conversion happens only at
// the external interface and Python process boundary, never on every wire.
#include <QVariant>
#include <QString>
#include <QHash>
#include <QMap>
#include <QSet>
#include <stdexcept>

namespace corig {

class Error final : public std::runtime_error {
public:
    explicit Error(const QString &message) : std::runtime_error(message.toUtf8().constData()), message(message) {}
    QString message;
};

using Values = QHash<QString, QVariant>;

QVariant toJsonValue(const QVariant &value);
QVariant fromJsonValue(const QVariant &value);
double finiteNumber(const QVariant &value, const QString &name);
double timeUnit(const QString &unit);
QVariant defaultValue(const QString &type);
bool valueMatches(const QVariant &value, const QString &type);
QVariantList sampleFrames(const QVariant &value);
QString newId(const QString &prefix);

} // namespace corig
