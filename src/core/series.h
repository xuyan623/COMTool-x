#pragma once

// Numeric acquisition is independent of drawing. The ring retains raw samples;
// display decimation preserves bucket extrema instead of dropping narrow peaks.
#include "value.h"
#include <QPointF>
#include <vector>
#include <limits>

namespace cx {

class Series {
public:
    explicit Series(qsizetype capacity = 200000);
    void append(double x, double y);
    void resize(qsizetype capacity);
    QVector<QPointF> points(double minimum = -std::numeric_limits<double>::infinity(), double maximum = std::numeric_limits<double>::infinity(), qsizetype displayLimit = 0) const;
    qsizetype size() const { return static_cast<qsizetype>(data_.size()); }
    quint64 total() const { return total_; }
    double lastTime() const;
private:
    QPointF at(qsizetype index) const;
    std::vector<QPointF> data_;
    qsizetype capacity_, next_ = 0;
    quint64 total_ = 0;
    bool monotonic_ = true;
};

QVector<QPointF> spectrum(const QVector<QPointF> &points);
QVariantList describePoints(const QVector<QPointF> &points);

} // namespace cx
