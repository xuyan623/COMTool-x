#pragma once

// Package workers own their QProcess on a native thread. Callers exchange
// futures and immutable events; node state stays separate inside each worker.
#include "value.h"
#include <functional>
#include <memory>
#include <mutex>

namespace corig {
class PythonHost {
public:
    using Events = std::function<void(const QVariantMap &)>;
    explicit PythonHost(std::function<bool()> interrupted, Events events = {});
    ~PythonHost();
    void selectInterpreter(const QString &path);
    QString interpreter() const;
    QVariantMap call(const QVariantMap &request);
    QVariantMap task(const QString &id) const;
    QVariantMap cancelTask(const QString &id);
    void close();
private:
    struct Worker;
    void event(const QString &package, quint64 generation, const QVariantMap &message);
    std::function<bool()> interrupted_;
    Events events_;
    mutable std::mutex mutex_;
    QString interpreter_;
    QMap<QString,std::shared_ptr<Worker>> workers_;
    QMap<QString,QVariantMap> tasks_;
    QMap<QString,QString> taskPackages_;
    QMap<QString,quint64> taskGenerations_;
    quint64 nextWorker_ = 0;
};
}
