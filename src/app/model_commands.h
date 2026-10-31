#pragma once

// User gestures and external automation edit the same candidate document.
// Only a validated candidate can become the visible/saved project.
#include "core/project.h"

namespace corig {
struct EditResult {
    QVariantMap project;
    QVariant result;
    QString selected;
    bool engineChanged = false;
};
EditResult editProject(QVariantMap project, const Catalog &catalog, const QString &operation, const QVariantMap &arguments);
QVariantMap uiPropertySchema(const QVariantMap &item, const Catalog &catalog, const QVariantMap &project);
QVariantMap checkedUiProperties(const QVariantMap &item, const Catalog &catalog, const QVariantMap &project,
                               const QVariantMap &changes, const QVariantMap &previous = {});
QVariantMap nodeConfigurationPlan(const QVariantMap &project, const Catalog &catalog, const QString &node, const QVariantMap &changes);
// Editor commands preserve object identity separately from canvas placement.
// Plans describe the actual destructive/synchronization effects before commit.
EditResult editEditorProject(QVariantMap project, const Catalog &catalog, const QString &operation, const QVariantMap &arguments);
QVariantMap editorImpact(const QVariantMap &project, const Catalog &catalog, const QVariantMap &arguments);
QVariantMap synchronizationPlan(const QVariantMap &project, const Catalog &catalog, const QString &direction);
QVariantMap copyEditorObjects(const QVariantMap &project, const QString &page, const QStringList &ids);
}
