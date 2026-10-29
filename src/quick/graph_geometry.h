#pragma once

// Canvas drawing, port hit tests and library previews consume one layout, so
// visual rotation cannot disagree with connection coordinates or occupancy.
#include "core/catalog.h"
#include <QPainterPath>
#include <QRectF>

namespace corig {
QVariantMap nodeGeometry(const QVariantMap &node, const Catalog &catalog);
QPointF portPosition(const QVariantMap &node, const QVariantMap &geometry, const QString &port);
QPainterPath wirePath(const QVariantMap &wire, const QHash<QString,QVariantMap> &nodes, const QHash<QString,QVariantMap> &geometry, QVector<QPainterPath> *segments = nullptr);
QVariantList graphLayout(const QVariantMap &project, const Catalog &catalog);
}
