#pragma once

// One declaration drives graph validation, editors, execution and external
// discovery. Native and Python providers expose the same port/data contract.
#include "value.h"

namespace cx {

struct Port {
    QString id, label, direction, type, unit;
    QVariantMap describe() const;
};

class Catalog {
public:
    Catalog();
    QVariantMap definition(const QString &id) const;
    QVariantList describe() const;
    QVariantMap checkedConfig(const QString &id, const QVariantMap &changes, const QVariantMap &previous = {}) const;
    QVector<Port> ports(const QString &id, const QVariantMap &config) const;
    void addPython(const QVariantMap &manifest);
    void clearPython();
    QVariantList search(const QString &query, const QStringList &category = {}) const;
private:
    QMap<QString, QVariantMap> definitions_;
};

QVariantMap checkedProperties(const QVariantMap &schema, const QVariantMap &changes, const QVariantMap &previous = {});

} // namespace cx
