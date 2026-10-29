#pragma once

// Qt Quick renderers consume immutable observations. They cannot call devices
// or change graph scheduling; animation merely requests best-effort repainting.
#include "core/mesh.h"
#include <QQuickItem>
#include <QTimer>
#include <QElapsedTimer>
#include <QPainterPath>
#include <QQuickPaintedItem>

namespace corig {

// The same cut-out silhouette paints the graph card and library preview. The
// final stroke is always above all three fills, including the selected top edge.
class NodeSurface : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QVariantMap geometry READ geometry WRITE setGeometry NOTIFY changed)
    Q_PROPERTY(QColor outline READ outline WRITE setOutline NOTIFY changed)
    Q_PROPERTY(bool emphasized READ emphasized WRITE setEmphasized NOTIFY changed)
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY changed)
public:
    explicit NodeSurface(QQuickItem *parent = nullptr);
    QVariantMap geometry() const { return geometry_; }
    QColor outline() const { return outline_; }
    bool emphasized() const { return emphasized_; }
    bool dark() const { return dark_; }
    void setGeometry(const QVariantMap &value);
    void setOutline(const QColor &value);
    void setEmphasized(bool value);
    void setDark(bool value);
    void paint(QPainter *painter) override;
signals:
    void changed();
private:
    QVariantMap geometry_;
    QColor outline_ = QColor("#42566B");
    bool emphasized_ = false, dark_ = true;
};

class WireLayer : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QVariantList nodes READ nodes WRITE setNodes NOTIFY designChanged)
    Q_PROPERTY(QVariantList wires READ wires WRITE setWires NOTIFY designChanged)
    Q_PROPERTY(QVariantMap statistics READ statistics WRITE setStatistics NOTIFY statisticsChanged)
    Q_PROPERTY(QString selected READ selected WRITE setSelected NOTIFY designChanged)
    Q_PROPERTY(bool animated READ animated WRITE setAnimated NOTIFY animatedChanged)
    Q_PROPERTY(bool running READ running WRITE setRunning NOTIFY playbackChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY playbackChanged)
    Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY designChanged)
public:
    explicit WireLayer(QQuickItem *parent = nullptr);
    QVariantList nodes() const { return nodes_; }
    QVariantList wires() const { return wires_; }
    QVariantMap statistics() const { return statistics_; }
    QString selected() const { return selected_; }
    bool animated() const { return animated_; }
    bool running() const { return running_; }
    bool active() const { return active_; }
    double zoom() const { return zoom_; }
    void setNodes(const QVariantList &value);
    void setWires(const QVariantList &value);
    void setStatistics(const QVariantMap &value);
    void setSelected(const QString &value);
    void setAnimated(bool value);
    void setRunning(bool value);
    void setActive(bool value);
    void setZoom(double value);
    Q_INVOKABLE void previewMove(const QString &node, double x, double y);
    Q_INVOKABLE void previewRoute(const QString &wire, const QVariantList &points);
    Q_INVOKABLE void clearPreview();
    Q_INVOKABLE QVariantMap activityState() const;
signals:
    void designChanged();
    void statisticsChanged();
    void animatedChanged();
    void playbackChanged();
protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
    bool event(QEvent *event) override;
private:
    struct Path { QString id; QColor color; double width = 1.6, length = 0.; QVector<QPointF> points; QVector<double> distances; };
    // A bounded set of representative bars, never a queue of data payloads.
    struct Pulse { double distance = 0.; bool stepped = false; };
    struct Activity {
        quint64 count = 0, steps = 0;
        double rate = 0., sampledAt = 0., lastObserved = -1., advancedAt = 0., nextEmit = 0.;
        QVector<Pulse> pulses;
    };
    void rebuild();
    QVariantList nodes_, wires_;
    QVariantMap statistics_;
    QHash<QString,QPointF> preview_;
    QHash<QString,QVariantList> routePreview_;
    QHash<QString,Activity> activity_;
    QVector<Path> paths_;
    QString selected_;
    QTimer animation_;
    QElapsedTimer clock_;
    quint64 revision_ = 0;
    double zoom_ = 1.;
    bool animated_ = true, running_ = false, active_ = false, repaintPending_ = false;
};

class NativePlot : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QVariantMap observation READ data WRITE setData NOTIFY dataChanged)
public:
    explicit NativePlot(QQuickItem *parent = nullptr);
    QVariantMap data() const { return data_; }
    void setData(const QVariantMap &value);
    Q_INVOKABLE QVariantMap exportImage(const QString &path, int width = 1600, int height = 900) const;
signals:
    void dataChanged();
protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
private:
    QVariantMap data_;
};

class NativeAttitude : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QVariantMap observation READ data WRITE setData NOTIFY dataChanged)
    Q_PROPERTY(QVariantMap camera READ camera WRITE setCamera NOTIFY cameraChanged)
public:
    explicit NativeAttitude(QQuickItem *parent = nullptr);
    QVariantMap data() const { return data_; }
    QVariantMap camera() const { return camera_; }
    void setData(const QVariantMap &value);
    void setCamera(const QVariantMap &value);
    Q_INVOKABLE void resetCamera();
    Q_INVOKABLE void orbit(double dx, double dy);
    Q_INVOKABLE void pan(double dx, double dy);
    Q_INVOKABLE void dolly(double factor);
signals:
    void dataChanged();
    void cameraChanged();
protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
private:
    QVariantMap data_, camera_;
};

}
