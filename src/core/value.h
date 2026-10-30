#pragma once

// Runtime values keep binary buffers native. JSON conversion happens only at
// the external interface and Python process boundary, never on every wire.
#include <QVariant>
#include <QString>
#include <QHash>
#include <QMap>
#include <QSet>
#include <stdexcept>
#include "export.h"

namespace corig {

class CORIG_API Error final : public std::runtime_error {
public:
    explicit Error(const QString &message) : std::runtime_error(message.toUtf8().constData()), message(message) {}
    QString message;
};

using Values = QHash<QString, QVariant>;

CORIG_API QVariant toJsonValue(const QVariant &value);
CORIG_API QVariant fromJsonValue(const QVariant &value);
CORIG_API double finiteNumber(const QVariant &value, const QString &name);
CORIG_API double timeUnit(const QString &unit);
CORIG_API QVariant defaultValue(const QString &type);
CORIG_API bool valueMatches(const QVariant &value, const QString &type);
CORIG_API QVariantList sampleFrames(const QVariant &value);
CORIG_API QString newId(const QString &prefix);

} // namespace corig
