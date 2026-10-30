#pragma once

// Additional configuration and dynamic ports belong to the official provider,
// rather than being special cases in the application catalog.
#include "core/catalog.h"
namespace corig {
QVariantMap officialConfiguration(const QVariantMap &spec, const QVariantMap &changes, const QVariantMap &previous);
QVector<Port> officialPorts(const QVariantMap &spec, const QVariantMap &config, QVector<Port> declared);
}
