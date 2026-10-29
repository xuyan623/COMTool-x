#include "renderers.h"
#include "graph_geometry.h"
#include <QCoreApplication>
#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>
#include <QMatrix4x4>
#include <QPainter>
#include <QSvgGenerator>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace corig {
NodeSurface::NodeSurface(QQuickItem *parent) : QQuickPaintedItem(parent) { setAntialiasing(true); }
void NodeSurface::setGeometry(const QVariantMap &value) { if (geometry_ == value) return; geometry_ = value; update(); emit changed(); }
void NodeSurface::setOutline(const QColor &value) { if (outline_ == value) return; outline_ = value; update(); emit changed(); }
void NodeSurface::setEmphasized(bool value) { if (emphasized_ == value) return; emphasized_ = value; update(); emit changed(); }
void NodeSurface::setDark(bool value) { if (dark_ == value) return; dark_ = value; update(); emit changed(); }
void NodeSurface::paint(QPainter *painter)
{
    const double w = geometry_.value("width").toDouble(), h = geometry_.value("height").toDouble(), r = geometry_.value("radius",26.).toDouble();
    if (w <= 0. || h <= 0.) return;
    // A 2 px inset keeps the complete selection stroke inside the item texture.
    QPainterPath body; body.addRoundedRect(QRectF(2.,2.,w-4.,h-4.),r,r);
    for (const auto &entry : geometry_.value("ports").toList()) { const auto p = entry.toMap(); if (p.value("direction") == "in") { QPainterPath socket; socket.addEllipse(QPointF(p.value("x").toDouble(),p.value("y").toDouble()),9.,9.); body = body.subtracted(socket); } }
    painter->setRenderHint(QPainter::Antialiasing); painter->fillPath(body,dark_ ? QColor("#182330") : QColor("#F7FAFC")); painter->save(); painter->setClipPath(body);
    for (const auto &key : {"input_area","output_area"}) { const auto area = geometry_.value(key).toList(); if (area.size() == 4 && area[2].toDouble()>0. && area[3].toDouble()>0.) painter->fillRect(QRectF(area[0].toDouble(),area[1].toDouble(),area[2].toDouble(),area[3].toDouble()),dark_ ? QColor("#253749") : QColor("#E3EDF4")); }
    const auto center = geometry_.value("center").toList(); painter->setPen(QPen(dark_ ? QColor("#405367") : QColor("#C1CFDA"),1.));
    if (center.size() == 4) { if (geometry_.value("vertical").toBool()) { painter->drawLine(QPointF(0.,center[1].toDouble()),QPointF(w,center[1].toDouble())); painter->drawLine(QPointF(0.,center[1].toDouble()+center[3].toDouble()),QPointF(w,center[1].toDouble()+center[3].toDouble())); } else { painter->drawLine(QPointF(center[0].toDouble(),0.),QPointF(center[0].toDouble(),h)); painter->drawLine(QPointF(center[0].toDouble()+center[2].toDouble(),0.),QPointF(center[0].toDouble()+center[2].toDouble(),h)); } }
    painter->restore(); painter->setPen(QPen(outline_,emphasized_?2.4:1.2)); painter->setBrush(Qt::NoBrush); painter->drawPath(body);
}
namespace {
struct Vertex { QPointF position; QColor color; };
using Vertices = QVector<Vertex>;

// Triangles render consistently across D3D/OpenGL/RHI backends; native line
// widths vary between drivers and cannot express the required wire bars.
void line(Vertices &vertices, const QPointF &a, const QPointF &b, double width, const QColor &color)
{
    const auto delta = b - a; const double length = std::hypot(delta.x(),delta.y()); if (length < 1e-9) return;
    const QPointF normal(-delta.y() * width / (2. * length),delta.x() * width / (2. * length));
    for (const auto &point : {a + normal,a - normal,b + normal,b + normal,a - normal,b - normal}) vertices.append({point,color});
}

QSGGeometryNode *geometry(const Vertices &vertices)
{
    auto node = new QSGGeometryNode;
    auto mesh = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(),static_cast<int>(vertices.size()));
    mesh->setDrawingMode(QSGGeometry::DrawTriangles); auto output = mesh->vertexDataAsColoredPoint2D();
    for (qsizetype i = 0; i < vertices.size(); ++i) { const auto &v = vertices[i]; output[i].set(static_cast<float>(v.position.x()),static_cast<float>(v.position.y()),static_cast<uchar>(v.color.red()),static_cast<uchar>(v.color.green()),static_cast<uchar>(v.color.blue()),static_cast<uchar>(v.color.alpha())); }
    node->setGeometry(mesh); node->setFlag(QSGNode::OwnsGeometry);
    node->setMaterial(new QSGVertexColorMaterial); node->setFlag(QSGNode::OwnsMaterial); return node;
}

void clear(QSGNode *node) { while (node->firstChild()) { auto child = node->firstChild(); node->removeChildNode(child); delete child; } }

QVector<QPointF> points(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QVector<QPointF>>()) return value.value<QVector<QPointF>>();
    QVector<QPointF> result; for (const auto &entry : value.toList()) { const auto p = entry.toList(); result.append({p.value(0).toDouble(),p.value(1).toDouble()}); } return result;
}

QRectF plotArea(const QSizeF &size) { return {54.,16.,std::max(1.,size.width() - 72.),std::max(1.,size.height() - 56.)}; }
QPointF plotPoint(const QPointF &point, const QVariantMap &data, const QRectF &area)
{
    const auto x0 = data.value("x_min",0.).toDouble(), x1 = data.value("x_max",1.).toDouble(), y0 = data.value("y_min",-1.).toDouble(), y1 = data.value("y_max",1.).toDouble();
    return {area.left() + (point.x() - x0) / std::max(1e-15,x1 - x0) * area.width(),area.bottom() - (point.y() - y0) / std::max(1e-15,y1 - y0) * area.height()};
}

// Liang-Barsky clipping retains peaks crossing the viewport, including segments
// with both endpoints outside; simply discarding endpoints would break traces.
bool clipSegment(QPointF &a, QPointF &b, const QRectF &rect)
{
    const auto delta = b - a; double from = 0., to = 1.;
    const double p[]{-delta.x(),delta.x(),-delta.y(),delta.y()}, q[]{a.x() - rect.left(),rect.right() - a.x(),a.y() - rect.top(),rect.bottom() - a.y()};
    for (int i = 0; i < 4; ++i) { if (std::abs(p[i]) < 1e-12) { if (q[i] < 0.) return false; } else { const auto t = q[i] / p[i]; if (p[i] < 0.) from = std::max(from,t); else to = std::min(to,t); if (from > to) return false; } }
    b = a + delta * to; a += delta * from; return true;
}
}

WireLayer::WireLayer(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents); clock_.start(); animation_.setInterval(16); animation_.setTimerType(Qt::CoarseTimer);
    connect(&animation_,&QTimer::timeout,this,[this] {
        bool moving = running_;
        if (!moving) for (const auto &activity : activity_)
            if (std::any_of(activity.pulses.cbegin(),activity.pulses.cend(),[](const Pulse &pulse) { return pulse.stepped; })) { moving = true; break; }
        if (animated_ && active_ && moving && isVisible() && window() && !repaintPending_) { repaintPending_ = true; QCoreApplication::postEvent(this,new QEvent(QEvent::User),Qt::LowEventPriority); }
    });
    animation_.start();
}

bool WireLayer::event(QEvent *event)
{
    if (event->type() == QEvent::User) { repaintPending_ = false; update(); return true; }
    return QQuickItem::event(event);
}
void WireLayer::setNodes(const QVariantList &value) { nodes_ = value; preview_.clear(); rebuild(); }
void WireLayer::setWires(const QVariantList &value) { wires_ = value; routePreview_.clear(); rebuild(); }
void WireLayer::setSelected(const QString &value) { if (selected_ == value) return; selected_ = value; ++revision_; update(); emit designChanged(); }
void WireLayer::setAnimated(bool value) {
    if (animated_ == value) return;
    animated_ = value;
    const double now = clock_.nsecsElapsed() / 1e9;
    for (auto &activity : activity_) activity.advancedAt = now;
    update(); emit animatedChanged();
}
void WireLayer::setRunning(bool value) {
    if (running_ == value) return;
    running_ = value;
    // Resume from the frozen position rather than integrating paused time.
    const double now = clock_.nsecsElapsed() / 1e9;
    for (auto &activity : activity_) activity.advancedAt = now;
    update(); emit playbackChanged();
}
void WireLayer::setActive(bool value) {
    if (active_ == value) return;
    active_ = value;
    if (!active_) activity_.clear();
    update(); emit playbackChanged();
}
void WireLayer::setZoom(double value) { zoom_ = std::max(.05,value); ++revision_; update(); emit designChanged(); }
void WireLayer::previewMove(const QString &node, double x, double y) { preview_[node] = {x,y}; rebuild(); }
void WireLayer::previewRoute(const QString &wire, const QVariantList &points) { routePreview_[wire] = points; rebuild(); }
void WireLayer::clearPreview() { preview_.clear(); routePreview_.clear(); rebuild(); }

void WireLayer::setStatistics(const QVariantMap &value)
{
    statistics_ = value; const double now = clock_.nsecsElapsed() / 1e9;
    for (auto i = value.cbegin(); i != value.cend(); ++i) {
        auto &activity = activity_[i.key()]; const auto wire = i.value().toMap();
        const auto count = wire.value("count").toULongLong(), steps = wire.value("step_count").toULongLong();
        if (count < activity.count || steps < activity.steps) activity = {};
        const double dt = now - activity.sampledAt;
        if (dt > .001) { const double measured = count >= activity.count ? (count - activity.count) / dt : 0.; activity.rate += (measured - activity.rate) * (1. - std::exp(-dt / .22)); }
        if (count > activity.count) activity.lastObserved = now;
        // A debug step animates only wires actually transferred by that step;
        // ordinary paused bars remain frozen on every other wire.
        if (active_ && animated_ && steps > activity.steps && activity.pulses.size() < 64) {
            activity.pulses.append({0.,true}); activity.advancedAt = now;
        }
        activity.count = count; activity.steps = steps; activity.sampledAt = now;
    }
    update(); emit statisticsChanged();
}

QVariantMap WireLayer::activityState() const
{
    // Qt Quick blocks the GUI during updatePaintNode's synchronization phase;
    // this GUI-thread observation therefore needs no execution-path locks.
    QVariantMap wires;
    const double now = clock_.nsecsElapsed() / 1e9;
    for (const auto &path : paths_) {
        const auto activity = activity_.value(path.id);
        QVariantList positions;
        for (const auto &pulse : activity.pulses) positions.append(pulse.distance * zoom_);
        const double intensity = std::clamp(std::log2(1. + activity.rate) / 16.,0.,1.);
        wires[path.id] = QVariantMap{{"count",QVariant::fromValue(activity.count)},{"step_count",QVariant::fromValue(activity.steps)},
            {"rate_hz",activity.rate},{"rate_basis","sampled_counter"},{"sample_age_ms",(now-activity.sampledAt)*1000.},
            {"length_px",path.length*zoom_},{"speed_px_s",38.+240.*intensity},{"spacing_px",160.-142.*intensity},
            {"bar_length_px",6.},{"bar_positions_px",positions}};
    }
    return {{"enabled",animated_},{"active",active_},{"running",running_},{"wires",wires}};
}

void WireLayer::rebuild()
{
    QHash<QString,QVariantMap> nodes, layouts;
    for (const auto &item : nodes_) { auto node = item.toMap(); const auto id = node.value("id").toString(); if (preview_.contains(id)) node["position"] = QVariantList{preview_[id].x(),preview_[id].y()}; nodes[id] = node; layouts[id] = node.value("geometry").toMap(); }
    paths_.clear();
    for (const auto &item : wires_) {
        auto wire = item.toMap(); if (routePreview_.contains(wire.value("id").toString())) wire["points"] = routePreview_.value(wire.value("id").toString());
        const auto source = wire.value("source").toList(), target = wire.value("target").toList();
        if (!nodes.contains(source.value(0).toString()) || !nodes.contains(target.value(0).toString())) continue;
        Path path; path.id = wire.value("id").toString(); path.color = QColor(wire.value("color","#5E9DCB").toString()); path.width = wire.value("width",1.6).toDouble();
        try { const auto polygons = wirePath(wire,nodes,layouts).toSubpathPolygons(); for (const auto &polygon : polygons) for (const auto &p : polygon) { if (!path.points.isEmpty()) path.length += QLineF(path.points.last(),p).length(); path.points.append(p); path.distances.append(path.length); } paths_.append(path); }
        catch (const Error &) { /* Transient QML bindings may deliver nodes and wires in two successive assignments. The next assignment rebuilds the complete design. */ }
    }
    ++revision_; update(); emit designChanged();
}

QSGNode *WireLayer::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    struct Root : QSGNode { QSGNode *base = new QSGNode, *bars = new QSGNode; quint64 revision = ~quint64(0); Root() { appendChildNode(base); appendChildNode(bars); } };
    auto root = static_cast<Root *>(old); if (!root) root = new Root;
    if (root->revision != revision_) {
        clear(root->base); Vertices base;
        for (const auto &path : paths_) {
            const QColor color = path.id == selected_ ? QColor("#A8D5FC") : path.color;
            for (qsizetype i = 1; i < path.points.size(); ++i) line(base,path.points[i - 1],path.points[i],(path.width + (path.id == selected_ ? 1.4 : 0.)) / zoom_,color);
            if (path.points.size() > 1) {
                const auto vector = path.points.last() - path.points[path.points.size() - 2]; const auto length = std::hypot(vector.x(),vector.y());
                if (length > 1e-6) { const auto direction = vector / length; const QPointF normal(-direction.y(),direction.x()); const auto tip = path.points.last() - direction * 6. / zoom_;
                    for (const auto &p : {tip,tip - direction * 9. / zoom_ + normal * 4. / zoom_,tip - direction * 9. / zoom_ - normal * 4. / zoom_}) base.append({p,color}); }
            }
        }
        root->base->appendChildNode(geometry(base)); root->revision = revision_;
    }
    clear(root->bars);
    const double now = clock_.nsecsElapsed() / 1e9;
    if (animated_ && active_) {
        QElapsedTimer budget; budget.start();
        Vertices bars;
        for (const auto &path : paths_) {
            // Decorative work has a short per-frame budget. Skipped wires
            // resume using elapsed time; no missed transfers or frames queue up.
            if (budget.nsecsElapsed() > 1000000) break;
            auto &activity = activity_[path.id]; if (path.points.size() < 2) continue;
            const double intensity = std::clamp(std::log2(1. + activity.rate) / 16.,0.,1.);
            const double spacing = (160. - 142. * intensity) / zoom_, length = 6. / zoom_;
            const double speed = (38. + 240. * intensity) / zoom_, dt = std::max(0.,now - activity.advancedAt);
            activity.advancedAt = now;
            for (auto &pulse : activity.pulses) if (running_ || pulse.stepped) pulse.distance += (pulse.stepped ? 120./zoom_ : speed) * dt;
            activity.pulses.erase(std::remove_if(activity.pulses.begin(),activity.pulses.end(),[&](const Pulse &pulse) { return pulse.distance > path.length + length; }),activity.pulses.end());
            double nearest = path.length + spacing;
            for (const auto &pulse : activity.pulses) nearest = std::min(nearest,pulse.distance);
            if (running_ && activity.lastObserved >= 0. && now - activity.lastObserved < .15 && now >= activity.nextEmit && nearest >= spacing && activity.pulses.size() < 64) {
                activity.pulses.append({0.,false}); activity.nextEmit = now + spacing / speed;
            }
            const auto sample = [&](double distance) { const auto found = std::upper_bound(path.distances.cbegin(),path.distances.cend(),distance); const auto index = std::clamp<qsizetype>(found - path.distances.cbegin(),1,path.points.size() - 1); const auto span = path.distances[index] - path.distances[index - 1]; return path.points[index - 1] + (path.points[index] - path.points[index - 1]) * ((distance - path.distances[index - 1]) / std::max(1e-9,span)); };
            QVector<double> painted;
            for (const auto &pulse : activity.pulses) {
                const double start = pulse.distance - length;
                if (std::any_of(painted.cbegin(),painted.cend(),[&](double other) { return std::abs(start-other) < 18./zoom_; })) continue;
                painted.append(start);
                const auto from = std::max(0.,start), end = std::min(path.length,start + length); if (end <= from) continue;
                QPointF a = sample(from);
                for (double part = from + std::min(2. / zoom_,length); part < end; part += 2. / zoom_) { const auto b = sample(part); line(bars,a,b,path.width / zoom_,QColor("#D0EAFE")); a = b; }
                line(bars,a,sample(end),path.width / zoom_,QColor("#D0EAFE"));
            }
        }
        root->bars->appendChildNode(geometry(bars));
    }
    return root;
}

NativePlot::NativePlot(QQuickItem *parent) : QQuickItem(parent) { setFlag(ItemHasContents); setClip(true); }
void NativePlot::setData(const QVariantMap &value) { if (data_ == value) return; data_ = value; update(); emit dataChanged(); }

QSGNode *NativePlot::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    auto root = old ? old : new QSGNode; clear(root); const auto area = plotArea({width(),height()}); Vertices vertices;
    for (int i = 0; i <= 5; ++i) { const auto x = area.left() + area.width() * i / 5., y = area.top() + area.height() * i / 5.; line(vertices,{x,area.top()},{x,area.bottom()},1.,QColor("#263747")); line(vertices,{area.left(),y},{area.right(),y},1.,QColor("#263747")); }
    for (const auto &entry : data_.value("channels").toList()) {
        const auto channel = entry.toMap(); if (!channel.value("visible").toBool()) continue;
        const auto series = points(channel.value("points")); const QColor color(channel.value("color").toString());
        for (qsizetype i = 1; i < series.size(); ++i) { auto a = plotPoint(series[i - 1],data_,area), b = plotPoint(series[i],data_,area); if (clipSegment(a,b,area)) line(vertices,a,b,data_.value("line_width",1.4).toDouble(),color); }
    }
    root->appendChildNode(geometry(vertices)); return root;
}

QVariantMap NativePlot::exportImage(const QString &path, int width, int height) const
{
    try {
        if (path.isEmpty() || width < 100 || height < 100 || width > 8192 || height > 8192) throw Error("请选择文件，导出尺寸需要 100～8192");
        QImage raster; QSvgGenerator svg; std::unique_ptr<QPainter> painter;
        if (path.endsWith(".svg",Qt::CaseInsensitive)) { svg.setFileName(path); svg.setSize({width,height}); svg.setViewBox(QRect(0,0,width,height)); svg.setTitle("CoRig 曲线"); painter = std::make_unique<QPainter>(&svg); }
        else { raster = QImage(width,height,QImage::Format_ARGB32_Premultiplied); painter = std::make_unique<QPainter>(&raster); }
        if (!painter->isActive()) throw Error("导出文件无法打开");
        painter->fillRect(QRect(0,0,width,height),QColor("#111923")); painter->setRenderHint(QPainter::Antialiasing);
        const auto area = plotArea(QSizeF(width,height)); painter->setPen(QColor("#2D3E50"));
        for (int i = 0; i <= 5; ++i) { const auto x = area.left() + area.width() * i / 5., y = area.top() + area.height() * i / 5.; painter->drawLine(QPointF(x,area.top()),QPointF(x,area.bottom())); painter->drawLine(QPointF(area.left(),y),QPointF(area.right(),y)); }
        painter->save(); painter->setClipRect(area);
        for (const auto &entry : data_.value("channels").toList()) { const auto channel = entry.toMap(); if (!channel.value("visible").toBool()) continue; const auto series = points(channel.value("points")); QPolygonF polygon; for (const auto &point : series) polygon.append(plotPoint(point,data_,area)); painter->setPen(QPen(QColor(channel.value("color").toString()),data_.value("line_width",1.4).toDouble())); painter->drawPolyline(polygon); }
        painter->restore(); painter->setPen(QColor("#E6EDF5")); painter->drawText(QRectF(54,height - 30,width - 72,24),Qt::AlignCenter,data_.value("x_label").toString());
        painter->end(); if (!raster.isNull() && !raster.save(path)) throw Error("图片保存失败");
        if (!QFileInfo(path).isFile()) throw Error("导出文件未生成"); return {{"path",QFileInfo(path).absoluteFilePath()},{"width",width},{"height",height}};
    } catch (const Error &error) { return {{"ok",false},{"error",error.message}}; }
}

NativeAttitude::NativeAttitude(QQuickItem *parent) : QQuickItem(parent) { setFlag(ItemHasContents); setClip(true); }
void NativeAttitude::setData(const QVariantMap &value) { if (data_ == value) return; data_ = value; if (camera_.isEmpty()) resetCamera(); update(); emit dataChanged(); }
void NativeAttitude::setCamera(const QVariantMap &value) { for (auto i = value.cbegin(); i != value.cend(); ++i) camera_[i.key()] = i.value(); camera_["elevation"] = std::clamp(camera_.value("elevation",24.).toDouble(),-89.9,89.9); camera_["distance"] = std::clamp(camera_.value("distance",4.5).toDouble(),1.05,10000.); update(); emit cameraChanged(); }
void NativeAttitude::resetCamera() { const auto config = data_.value("config").toMap(); camera_.clear(); setCamera({{"azimuth",config.value("camera_azimuth",38.)},{"elevation",config.value("camera_elevation",24.)},{"distance",config.value("camera_distance",4.5)},{"pan",config.value("camera_pan",QVariantList{0.,0.})}}); }
void NativeAttitude::orbit(double dx, double dy) { setCamera({{"azimuth",camera_.value("azimuth").toDouble() - dx * .4},{"elevation",camera_.value("elevation").toDouble() + dy * .4}}); }
void NativeAttitude::pan(double dx, double dy) { const auto p = camera_.value("pan").toList(); const double scale = camera_.value("distance",4.5).toDouble() / std::max(100.,height()); setCamera({{"pan",QVariantList{p.value(0).toDouble() + dx * scale,p.value(1).toDouble() - dy * scale}}}); }
void NativeAttitude::dolly(double factor) { setCamera({{"distance",camera_.value("distance",4.5).toDouble() * factor}}); }

QSGNode *NativeAttitude::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    auto root = old ? old : new QSGNode; clear(root); const auto mesh = data_.value("mesh").value<MeshPtr>(); if (!mesh || height() < 1.) return root;
    const auto config = data_.value("config").toMap(); const auto rows = data_.value("matrix").toList(); QMatrix4x4 pose;
    for (int row = 0; row < 3; ++row) for (int col = 0; col < 3; ++col) pose(row,col) = static_cast<float>(rows.value(row).toList().value(col).toDouble());
    const auto installation = config.value("model_rotation_deg").toList(), offset = config.value("model_offset").toList(), target = config.value("camera_target").toList();
    QMatrix4x4 mount; mount.translate(static_cast<float>(offset.value(0).toDouble() / mesh->radius),static_cast<float>(offset.value(1).toDouble() / mesh->radius),static_cast<float>(offset.value(2).toDouble() / mesh->radius));
    mount.rotate(QQuaternion::fromEulerAngles(static_cast<float>(installation.value(0).toDouble()),static_cast<float>(installation.value(1).toDouble()),static_cast<float>(installation.value(2).toDouble()))); mount.scale(static_cast<float>(config.value("model_scale",1.).toDouble() / mesh->radius)); if (config.value("auto_center",true).toBool()) mount.translate(-mesh->center);
    const auto panValue = camera_.value("pan").toList(); const QVector3D center(static_cast<float>(target.value(0).toDouble()),static_cast<float>(target.value(1).toDouble()),static_cast<float>(target.value(2).toDouble()));
    const auto azimuth = camera_.value("azimuth",38.).toDouble() * std::numbers::pi / 180., elevation = camera_.value("elevation",24.).toDouble() * std::numbers::pi / 180., distance = camera_.value("distance",4.5).toDouble();
    QMatrix4x4 view; view.translate(static_cast<float>(panValue.value(0).toDouble()),static_cast<float>(panValue.value(1).toDouble()),0.f); view.lookAt(center + QVector3D(static_cast<float>(distance * std::cos(elevation) * std::cos(azimuth)),static_cast<float>(distance * std::cos(elevation) * std::sin(azimuth)),static_cast<float>(distance * std::sin(elevation))),center,{0,0,1});
    QMatrix4x4 projection; const auto aspect = static_cast<float>(width() / height());
    if (config.value("projection") == "orthographic") { const float span = static_cast<float>(distance * .42); projection.ortho(-span * aspect,span * aspect,-span,span,.01f,20000.f); } else projection.perspective(42.f,aspect,.01f,20000.f);
    const auto model = pose * mount, transform = projection * view;
    const auto project = [&](const QVector3D &p) { const auto v = transform * QVector4D(p,1.f); if (v.w() <= .001f) return QPointF(-1e6,-1e6); return QPointF((v.x() / v.w() + 1.) * width() / 2.,(1. - v.y() / v.w()) * height() / 2.); };
    struct Face { QVector3D a,b,c; float depth; QColor color; }; QVector<Face> faces; faces.reserve(mesh->triangles.size());
    const QColor base(config.value("model_color","#BCD8E8").toString()); const QVector3D light = QVector3D(1,-2,3).normalized();
    for (const auto &face : mesh->triangles) {
        const auto a = model.map(mesh->vertices[face[0]]), b = model.map(mesh->vertices[face[1]]), c = model.map(mesh->vertices[face[2]]);
        const auto normal = QVector3D::normal(a,b,c); const auto shade = .36f + .64f * std::abs(QVector3D::dotProduct(normal,light));
        faces.append({a,b,c,view.map((a + b + c) / 3.f).z(),QColor::fromRgbF(base.redF() * shade,base.greenF() * shade,base.blueF() * shade)});
    }
    std::sort(faces.begin(),faces.end(),[](const auto &a, const auto &b) { return a.depth < b.depth; }); Vertices vertices;
    for (const auto &face : faces) for (const auto &point : {face.a,face.b,face.c}) vertices.append({project(point),face.color});
    const auto axes = [&](const QMatrix4x4 &rotation, const QVector3D &origin, double width) { const auto a = project(origin); const QVector3D directions[]{{1.35f,0,0},{0,1.35f,0},{0,0,1.35f}}; const QColor colors[]{QColor("#E98A88"),QColor("#8ACDA1"),QColor("#73B8EE")}; for (int i = 0; i < 3; ++i) line(vertices,a,project(origin + rotation.mapVector(directions[i])),width,colors[i]); };
    if (config.value("show_world_axes",true).toBool()) axes(QMatrix4x4{},QVector3D{},1.2);
    if (config.value("show_model_axes",true).toBool()) axes(pose,QVector3D{},2.2);
    root->appendChildNode(geometry(vertices)); return root;
}

}
