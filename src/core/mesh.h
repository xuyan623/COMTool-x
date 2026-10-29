#pragma once

// Immutable geometry is shared by the runtime and Qt Quick renderer. External
// callers inspect paged vertices/faces rather than receiving opaque GPU handles.
#include "value.h"
#include <QVector3D>
#include <QMatrix3x3>
#include <QQuaternion>
#include <array>
#include <memory>

namespace cx {
struct Mesh {
    QVector<QVector3D> vertices;
    QVector<std::array<int,3>> triangles;
    QVector3D center;
    double radius = 1.;
    QString name, digest;
    QVariantMap describe() const;
};
using MeshPtr = std::shared_ptr<const Mesh>;
MeshPtr airplane();
MeshPtr loadMesh(const QByteArray &bytes, const QString &name);
QMatrix3x3 eulerMatrix(double roll, double pitch, double yaw, const QString &order, const QString &frame);
QMatrix3x3 axisMapping(const QVariantMap &config);
QQuaternion checkedQuaternion(const QVariantList &values);
QVariantList describeMatrix(const QMatrix3x3 &matrix);
}
Q_DECLARE_METATYPE(cx::MeshPtr)
