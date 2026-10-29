#include "series.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace corig {

Series::Series(qsizetype capacity) : capacity_(std::max<qsizetype>(1,capacity)) {}

QPointF Series::at(qsizetype index) const
{
    const auto offset = data_.size() < static_cast<size_t>(capacity_) ? 0 : next_;
    return data_[static_cast<size_t>((offset + index) % static_cast<qsizetype>(data_.size()))];
}

void Series::append(double x, double y)
{
    if (!std::isfinite(x) || !std::isfinite(y)) throw Error("曲线只接受有限数值");
    if (!data_.empty() && x < lastTime()) monotonic_ = false;
    if (data_.size() < static_cast<size_t>(capacity_)) data_.emplace_back(x,y);
    else data_[static_cast<size_t>(next_)] = {x,y};
    next_ = (next_ + 1) % capacity_; ++total_;
}

void Series::resize(qsizetype capacity)
{
    capacity = std::max<qsizetype>(1,capacity);
    if (capacity == capacity_) return;
    const auto retained = points();
    data_.clear(); capacity_ = capacity; next_ = 0;
    const auto total = total_;
    for (auto i = std::max<qsizetype>(0,retained.size() - capacity); i < retained.size(); ++i) append(retained[i].x(),retained[i].y());
    total_ = total;
}

double Series::lastTime() const { return data_.empty() ? 0. : at(size() - 1).x(); }

QVector<QPointF> Series::points(double minimum, double maximum, qsizetype displayLimit) const
{
    QVector<QPointF> result;
    if (data_.empty()) return result;
    qsizetype begin = 0, end = size();
    if (monotonic_) {
        qsizetype a = 0, b = size();
        while (a < b) { const auto middle = (a + b) / 2; if (at(middle).x() < minimum) a = middle + 1; else b = middle; }
        begin = std::max<qsizetype>(0,a - 1);
        a = begin; b = size();
        while (a < b) { const auto middle = (a + b) / 2; if (at(middle).x() <= maximum) a = middle + 1; else b = middle; }
        end = std::min(size(),a + 1);
    }
    const auto count = end - begin;
    if (displayLimit <= 0 || count <= displayLimit) {
        result.reserve(count);
        for (auto i = begin; i < end; ++i) if (monotonic_ || (at(i).x() >= minimum && at(i).x() <= maximum)) result.append(at(i));
        return result;
    }
    const qsizetype buckets = std::max<qsizetype>(1,displayLimit / 2);
    result.reserve(buckets * 2 + 2); result.append(at(begin));
    for (qsizetype bucket = 0; bucket < buckets; ++bucket) {
        const auto left = begin + bucket * count / buckets, right = begin + (bucket + 1) * count / buckets;
        auto low = left, high = left;
        for (auto i = left; i < right; ++i) { if (at(i).y() < at(low).y()) low = i; if (at(i).y() > at(high).y()) high = i; }
        if (low <= high) { result.append(at(low)); if (low != high) result.append(at(high)); }
        else { result.append(at(high)); result.append(at(low)); }
    }
    result.append(at(end - 1)); return result;
}

QVector<QPointF> spectrum(const QVector<QPointF> &points)
{
    if (points.size() < 4) return {};
    size_t count = 1;
    while (count * 2 <= static_cast<size_t>(points.size()) && count < 131072) count *= 2;
    const auto start = points.size() - static_cast<qsizetype>(count);
    const double dt = (points.back().x() - points[start].x()) / (count - 1);
    if (!(dt > 0.) || !std::isfinite(dt)) throw Error("FFT 需要递增采样时间");
    std::vector<std::complex<double>> data(count);
    double sum = 0.;
    for (size_t i = 0; i < count; ++i) {
        const auto window = .5 - .5 * std::cos(2 * std::numbers::pi * i / (count - 1));
        data[i] = points[start + static_cast<qsizetype>(i)].y() * window; sum += window;
    }
    for (size_t i = 1, j = 0; i < count; ++i) {
        size_t bit = count >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(data[i],data[j]);
    }
    for (size_t length = 2; length <= count; length <<= 1) {
        const auto base = std::polar(1.,-2 * std::numbers::pi / length);
        for (size_t i = 0; i < count; i += length) {
            std::complex<double> factor(1.,0.);
            for (size_t j = 0; j < length / 2; ++j) {
                const auto a = data[i + j], b = data[i + j + length / 2] * factor;
                data[i + j] = a + b; data[i + j + length / 2] = a - b; factor *= base;
            }
        }
    }
    QVector<QPointF> result; result.reserve(static_cast<qsizetype>(count / 2 + 1));
    for (size_t i = 0; i <= count / 2; ++i) result.append({i / (count * dt), std::abs(data[i]) / sum * ((i == 0 || i == count / 2) ? 1. : 2.)});
    return result;
}

QVariantList describePoints(const QVector<QPointF> &points)
{
    QVariantList result; result.reserve(points.size());
    // Preserve one nested pair per point rather than concatenating its values.
    for (const auto &point : points) result.append(QVariant(QVariantList{point.x(),point.y()}));
    return result;
}

} // namespace corig
