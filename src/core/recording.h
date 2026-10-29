#pragma once

#include "value.h"
#include <QFile>
#include <QTemporaryFile>
#include <functional>
#include <memory>

namespace cx {

// Independent typed recording files contain ordered capture times and original
// values. Streaming temporary storage keeps history out of the project/model.
class RecordingWriter final {
public:
    // Starts a fresh capture, discarding only this writer's temporary content.
    void begin(const QString &type);
    // Appends one value at a nondecreasing active-recording time in seconds.
    void append(double time, const QVariant &value);
    // Atomically saves a finalized copy; failure preserves the capture buffer.
    void save(const QString &path, double duration, const std::function<bool()> &cancelled = {});
    // Explicit reset removes this capture's private temporary file.
    void reset();
    quint64 count() const { return count_; }
    qint64 bytes() const { return validBytes_; }
private:
    std::unique_ptr<QTemporaryFile> file_;
    QString type_;
    quint64 count_ = 0;
    qint64 validBytes_ = 0;
    double lastTime_ = 0.;
};

// Loading validates the whole stream before exposing it, while retaining only
// a compact index. Random seeks decode one record rather than loading history.
class RecordingReader final {
public:
    explicit RecordingReader(const QString &path, const std::function<bool()> &cancelled = {});
    // Finds the sample at or immediately preceding a requested position.
    qsizetype indexAt(double position) const;
    // Reads one original typed value; external file corruption remains visible.
    QVariant valueAt(qsizetype index);
    double timeAt(qsizetype index) const;
    // Encoded size lets playback bound a burst without decoding extra records.
    quint32 bytesAt(qsizetype index) const;
    qsizetype count() const { return entries_.size(); }
    double duration() const { return duration_; }
    QString dataType() const { return type_; }
    QString path() const { return file_.fileName(); }
private:
    struct Entry { qint64 offset; quint32 bytes; double time; };
    QFile file_;
    QVector<Entry> entries_;
    QString type_;
    double duration_ = 0.;
};

} // namespace cx
