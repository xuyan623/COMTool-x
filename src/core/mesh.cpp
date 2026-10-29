#include "mesh.h"
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QtEndian>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>

namespace corig {

QVariantMap Mesh::describe() const
{
    return {{"name",name}, {"vertices",vertices.size()}, {"triangles",triangles.size()}, {"center",QVariantList{center.x(),center.y(),center.z()}}, {"radius",radius}, {"sha256",digest}};
}

static MeshPtr finalizeMesh(std::shared_ptr<Mesh> mesh)
{
    if (mesh->vertices.isEmpty() || mesh->triangles.isEmpty() || mesh->vertices.size() > 300000 || mesh->triangles.size() > 100000)
        throw Error("模型需要有效几何，最多 30 万顶点、10 万三角面");
    QVector3D minimum(std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),std::numeric_limits<float>::max()), maximum(-minimum.x(),-minimum.y(),-minimum.z());
    for (const auto &vertex : mesh->vertices) for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(vertex[axis])) throw Error("模型顶点包含非有限数值");
        minimum[axis] = std::min(minimum[axis],vertex[axis]); maximum[axis] = std::max(maximum[axis],vertex[axis]);
    }
    for (const auto &face : mesh->triangles) for (const auto index : face) if (index < 0 || index >= mesh->vertices.size()) throw Error("模型面片引用无效顶点");
    mesh->center = (minimum + maximum) * .5f; mesh->radius = 0.;
    for (const auto &vertex : mesh->vertices) mesh->radius = std::max(mesh->radius,static_cast<double>((vertex - mesh->center).length()));
    if (!(mesh->radius > 0.) || !std::isfinite(mesh->radius)) throw Error("模型尺寸无效");
    return mesh;
}

MeshPtr airplane()
{
    static MeshPtr model = [] {
        auto mesh = std::make_shared<Mesh>(); mesh->name = "极简飞机"; mesh->digest = "builtin-airframe-v3";
        mesh->vertices = {{2,0,0},{-1.65f,-.12f,-.10f},{-1.65f,.12f,-.10f},{-1.65f,0,.15f},
            {.25f,-.18f,0},{.25f,.18f,0},{-.65f,-1.55f,-.02f},{-.65f,1.55f,-.02f},
            {-.8f,-.12f,0},{-.8f,.12f,0},{-1.65f,-.65f,.04f},{-1.65f,.65f,.04f},{-1.05f,0,.10f},{-1.65f,0,.72f}};
        mesh->triangles = {{0,2,1},{0,1,3},{0,3,2},{1,2,3},{4,6,8},{5,9,7},{8,10,1},{9,2,11},{12,3,13}};
        return finalizeMesh(mesh);
    }();
    return model;
}

static QVector<std::array<int,3>> triangulate(QVector<int> indices, const QVector<QVector3D> &vertices)
{
    if (indices.size() < 3 || indices.size() > 4096) throw Error("OBJ 面片需要 3～4096 个顶点");
    QVector3D normal;
    for (qsizetype i = 0; i < indices.size(); ++i) normal += QVector3D::crossProduct(vertices[indices[i]],vertices[indices[(i + 1) % indices.size()]]);
    int remove = 0; if (std::abs(normal.y()) > std::abs(normal.x())) remove = 1; if (std::abs(normal.z()) > std::abs(normal[remove])) remove = 2;
    const int first = (remove + 1) % 3, second = (remove + 2) % 3;
    const auto cross = [&](int a, int b, int c) {
        return (vertices[b][first] - vertices[a][first]) * (vertices[c][second] - vertices[a][second]) - (vertices[b][second] - vertices[a][second]) * (vertices[c][first] - vertices[a][first]);
    };
    double area = 0.;
    for (qsizetype i = 0; i < indices.size(); ++i) area += vertices[indices[i]][first] * vertices[indices[(i + 1) % indices.size()]][second] - vertices[indices[(i + 1) % indices.size()]][first] * vertices[indices[i]][second];
    if (std::abs(area) < 1e-14) throw Error("OBJ 存在退化面片");
    const auto sign = area > 0 ? 1. : -1.;
    QVector<std::array<int,3>> result;
    while (indices.size() > 3) {
        bool found = false;
        for (qsizetype at = 0; at < indices.size(); ++at) {
            const auto a = indices[(at + indices.size() - 1) % indices.size()], b = indices[at], c = indices[(at + 1) % indices.size()];
            if (sign * cross(a,b,c) <= 1e-14) continue;
            bool contains = false;
            for (const auto p : indices) if (p != a && p != b && p != c && sign * cross(a,b,p) >= -1e-14 && sign * cross(b,c,p) >= -1e-14 && sign * cross(c,a,p) >= -1e-14) { contains = true; break; }
            if (contains) continue;
            result.append({a,b,c}); indices.removeAt(at); found = true; break;
        }
        if (!found) throw Error("OBJ 面片自交或无法三角化");
    }
    result.append({indices[0],indices[1],indices[2]}); return result;
}

MeshPtr loadMesh(const QByteArray &bytes, const QString &name)
{
    if (bytes.size() > 64 * 1024 * 1024) throw Error("模型文件超过 64 MiB");
    auto mesh = std::make_shared<Mesh>(); mesh->name = name; mesh->digest = QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
    if (name.endsWith(".stl",Qt::CaseInsensitive)) {
        const auto count = bytes.size() >= 84 ? qFromLittleEndian<quint32>(bytes.constData() + 80) : 0;
        if (count && 84ull + count * 50ull == static_cast<quint64>(bytes.size())) {
            if (count > 100000) throw Error("STL 超过 10 万三角面");
            for (quint32 i = 0; i < count; ++i) {
                const auto at = bytes.constData() + 84 + i * 50 + 12;
                const auto base = mesh->vertices.size();
                for (int v = 0; v < 3; ++v) {
                    float coordinates[3]; for (int axis = 0; axis < 3; ++axis) coordinates[axis] = std::bit_cast<float>(qFromLittleEndian<quint32>(at + v * 12 + axis * 4));
                    mesh->vertices.append({coordinates[0],coordinates[1],coordinates[2]});
                }
                mesh->triangles.append({static_cast<int>(base),static_cast<int>(base + 1),static_cast<int>(base + 2)});
            }
        } else {
            auto iterator = QRegularExpression("\\bvertex\\s+([^\\s]+)\\s+([^\\s]+)\\s+([^\\s]+)",QRegularExpression::CaseInsensitiveOption).globalMatch(QString::fromUtf8(bytes));
            while (iterator.hasNext()) {
                const auto match = iterator.next(); QVector3D vertex;
                for (int axis = 0; axis < 3; ++axis) { bool ok; vertex[axis] = match.captured(axis + 1).toFloat(&ok); if (!ok) throw Error("STL 顶点无效"); }
                mesh->vertices.append(vertex);
            }
            if (mesh->vertices.size() % 3) throw Error("STL 顶点数量不完整");
            for (qsizetype i = 0; i < mesh->vertices.size(); i += 3) mesh->triangles.append({static_cast<int>(i),static_cast<int>(i + 1),static_cast<int>(i + 2)});
        }
    } else if (name.endsWith(".obj",Qt::CaseInsensitive)) {
        struct Face { QStringList fields; qsizetype vertices; }; QVector<Face> faces;
        for (const auto &line : QString::fromUtf8(bytes).split('\n')) {
            const auto fields = line.section('#',0,0).simplified().split(' ',Qt::SkipEmptyParts);
            if (fields.isEmpty()) continue;
            if (fields[0] == "v") {
                if (fields.size() < 4) throw Error("OBJ 顶点缺少坐标");
                QVector3D vertex;
                for (int axis = 0; axis < 3; ++axis) { bool ok; vertex[axis] = fields[axis + 1].toFloat(&ok); if (!ok) throw Error("OBJ 坐标无效"); }
                if (fields.size() == 5) { bool ok; const auto w = fields[4].toFloat(&ok); if (!ok || w == 0.f) throw Error("OBJ 齐次坐标无效"); vertex /= w; }
                mesh->vertices.append(vertex);
            } else if (fields[0] == "f") faces.append({fields.mid(1),mesh->vertices.size()});
        }
        for (const auto &face : faces) {
            QVector<int> indices;
            for (const auto &field : face.fields) {
                bool ok; const auto number = field.section('/',0,0).toInt(&ok);
                const auto index = number > 0 ? number - 1 : face.vertices + number;
                if (!ok || number == 0 || index < 0 || index >= mesh->vertices.size()) throw Error("OBJ 面片索引无效");
                indices.append(static_cast<int>(index));
            }
            mesh->triangles += triangulate(indices,mesh->vertices);
        }
    } else throw Error("模型需要 STL 或 OBJ 格式");
    return finalizeMesh(mesh);
}

QMatrix3x3 eulerMatrix(double roll, double pitch, double yaw, const QString &order, const QString &frame)
{
    static const QSet<QString> orders{"XYZ","XZY","YXZ","YZX","ZXY","ZYX"};
    if (!orders.contains(order) || (frame != "intrinsic" && frame != "extrinsic")) throw Error("欧拉角约定无效");
    QQuaternion rotation;
    for (const auto axis : order) {
        const auto direction = axis == 'X' ? QVector3D(1,0,0) : axis == 'Y' ? QVector3D(0,1,0) : QVector3D(0,0,1);
        const auto angle = axis == 'X' ? roll : axis == 'Y' ? pitch : yaw;
        const auto turn = QQuaternion::fromAxisAndAngle(direction,static_cast<float>(angle * 180 / std::numbers::pi));
        rotation = frame == "intrinsic" ? rotation * turn : turn * rotation;
    }
    return rotation.toRotationMatrix();
}

QMatrix3x3 axisMapping(const QVariantMap &config)
{
    static const QHash<QString,QVector3D> directions{{"front",{1,0,0}},{"back",{-1,0,0}},{"left",{0,1,0}},{"right",{0,-1,0}},{"up",{0,0,1}},{"down",{0,0,-1}}};
    QMatrix3x3 matrix; QVector<QVector3D> vectors;
    for (int column = 0; column < 3; ++column) {
        const auto name = config.value(QString("axis_") + QString("xyz")[column]).toString();
        if (!directions.contains(name)) throw Error("未知坐标轴方向");
        const auto vector = directions.value(name);
        for (const auto &other : vectors) if (QVector3D::dotProduct(other,vector) != 0.f) throw Error("三个坐标轴必须互相垂直");
        vectors.append(vector);
        for (int row = 0; row < 3; ++row) matrix(row,column) = vector[row];
    }
    return matrix;
}

QQuaternion checkedQuaternion(const QVariantList &values)
{
    if (values.size() != 4) throw Error("四元数需要 w、x、y、z 四个数值");
    double q[4], scale = 0.;
    for (int i = 0; i < 4; ++i) { q[i] = finiteNumber(values[i],"四元数"); scale = std::max(scale,std::abs(q[i])); }
    if (scale == 0.) throw Error("四元数全为零，等待有效输入");
    double norm = 0.; for (auto &value : q) { value /= scale; norm += value * value; } norm = std::sqrt(norm);
    return {static_cast<float>(q[0] / norm),static_cast<float>(q[1] / norm),static_cast<float>(q[2] / norm),static_cast<float>(q[3] / norm)};
}

QVariantList describeMatrix(const QMatrix3x3 &matrix)
{
    QVariantList rows;
    // QML and remote clients consume a 3x3 row matrix.
    for (int row = 0; row < 3; ++row) rows.append(QVariant(QVariantList{matrix(row,0),matrix(row,1),matrix(row,2)}));
    return rows;
}

} // namespace corig
