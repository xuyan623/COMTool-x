#pragma once

// A project is portable design data. Running sessions, local Python paths and
// view-window placement are deliberately outside this saved document.
#include "catalog.h"

namespace cx {

QVariantMap emptyProject(const QString &name = "未命名工程");
void validateProject(const QVariantMap &project, const Catalog &catalog);
// Quantize saved UI geometry to the design canvas's configurable paper grid.
void alignUiToGrid(QVariantMap &project);
QVector<QString> executionOrder(const QVariantMap &project, const Catalog &catalog);
QVariantMap loadProject(const QString &path);
void saveProject(const QString &path, const QVariantMap &project);
QString importResource(QVariantMap &project, const QString &path);
QByteArray resourceBytes(const QVariantMap &project, const QString &reference);

} // namespace cx
